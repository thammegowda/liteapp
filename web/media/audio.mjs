// Browser audio for speech apps: microphone capture as 16 kHz mono PCM frames and a streaming PCM player.
// The worklets load relative to this module, so serve media/ as a directory.

export const CAPTURE_RATE = 16000;
export const SPEECH_CONSTRAINTS = {
    channelCount: 1, echoCancellation: true, noiseSuppression: true, autoGainControl: true,
};

const stopTracks = media => {
    for (const track of media.getTracks()) track.stop();
};

// Captures `media` (by default, a newly opened microphone) and calls `onFrame(Int16Array)` for every 20 ms of
// 16 kHz mono PCM. `close()` stops the tracks and the audio context.
export async function openMicrophone({media, constraints = SPEECH_CONSTRAINTS, onFrame}) {
    media ??= await navigator.mediaDevices.getUserMedia({audio: constraints});
    const context = new AudioContext();
    try {
        await context.audioWorklet.addModule(new URL('./capture-worklet.mjs', import.meta.url));
        const source = context.createMediaStreamSource(media);
        const node = new AudioWorkletNode(context, 'liteapp-capture');
        const silence = context.createGain();
        silence.gain.value = 0;
        source.connect(node);
        node.connect(silence);
        silence.connect(context.destination);
        node.port.onmessage = ({data}) => onFrame(new Int16Array(data));
        const close = () => {
            node.port.onmessage = null;
            source.disconnect();
            node.disconnect();
            stopTracks(media);
            return context.close();
        };
        return {context, media, close};
    } catch (error) {
        stopTracks(media);
        context.close().catch(() => {});
        throw error;
    }
}

// A resumed audio context (call it from a user gesture) with a connected playback-worklet.mjs node. Post 'start',
// 'chunk', 'end', 'pause', 'resume' and 'reset' messages to `node.port`; see the worklet for the protocol.
export async function openPlayer({sampleRate} = {}) {
    const context = sampleRate ? new AudioContext({sampleRate}) : new AudioContext();
    try {
        await Promise.all([context.resume(),
            context.audioWorklet.addModule(new URL('./playback-worklet.mjs', import.meta.url))]);
        const node = new AudioWorkletNode(context, 'liteapp-playback', {outputChannelCount: [1]});
        node.connect(context.destination);
        return {context, node};
    } catch (error) {
        context.close().catch(() => {});
        throw error;
    }
}

// Linear-interpolation resampling of Float32 samples; returns the input unchanged when the rates match.
export function resample(samples, sourceRate, targetRate) {
    if (sourceRate === targetRate) return samples;
    const output = new Float32Array(Math.max(1, Math.floor(samples.length * targetRate / sourceRate)));
    const scale = sourceRate / targetRate;
    for (let index = 0; index < output.length; index++) {
        const position = index * scale;
        const left = Math.floor(position);
        const fraction = position - left;
        output[index] = samples[left] * (1 - fraction) + samples[Math.min(left + 1, samples.length - 1)] * fraction;
    }
    return output;
}

export function pcm16ToFloat32(pcm) {
    const samples = new Float32Array(pcm.length);
    for (let index = 0; index < pcm.length; index++) samples[index] = pcm[index] / 32768;
    return samples;
}
