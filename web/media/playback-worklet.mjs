// Streaming mono playback. The page posts {type, streamId} messages: 'start' begins a stream, 'chunk' queues
// `samples` (an ArrayBuffer of Float32 at the context rate, optionally tagged with `segmentId`), 'end' marks the last
// chunk, 'pause'/'resume' hold output, and 'reset' drops everything. The worklet reports 'buffered' frame counts,
// 'segment-started'/'segment-ended' for tagged chunks, and 'drained' once an ended stream has played out.
class PlaybackProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        this.clear(null);
        this.port.onmessage = ({data}) => {
            if (data.type === 'start') this.clear(data.streamId);
            else if (data.type === 'reset') this.clear(null);
            if (data.streamId !== this.streamId) return;
            if (data.type === 'chunk') {
                const samples = new Float32Array(data.samples);
                this.queue.push({samples, segmentId: data.segmentId, started: false});
                this.frames += samples.length;
                this.report();
            } else if (data.type === 'end') {
                this.ending = true;
            } else if (data.type === 'pause') {
                this.paused = true;
            } else if (data.type === 'resume') {
                this.paused = false;
            }
        };
    }

    clear(streamId) {
        this.queue = [];
        this.offset = 0;
        this.ending = false;
        this.paused = false;
        this.streamId = streamId;
        this.frames = 0;
        this.playedFrames = 0;
        this.reported = 0;
    }

    report() {
        const {streamId, frames, playedFrames} = this;
        this.port.postMessage({type: 'buffered', streamId, frames, playedFrames});
    }

    segment(type, current) {
        if (current.segmentId !== undefined)
            this.port.postMessage({type, streamId: this.streamId, segmentId: current.segmentId});
    }

    process(_inputs, outputs) {
        const output = outputs[0][0];
        output.fill(0);
        if (this.paused) return true;
        let written = 0;
        while (written < output.length && this.queue.length) {
            const current = this.queue[0];
            if (!current.started) {
                current.started = true;
                this.segment('segment-started', current);
            }
            const count = Math.min(output.length - written, current.samples.length - this.offset);
            output.set(current.samples.subarray(this.offset, this.offset + count), written);
            written += count;
            this.offset += count;
            this.frames -= count;
            this.playedFrames += count;
            this.reported += count;
            if (this.offset === current.samples.length) {
                this.segment('segment-ended', current);
                this.queue.shift();
                this.offset = 0;
            }
        }
        if (this.reported >= 2048 || (written && !this.frames)) {
            this.reported = 0;
            this.report();
        }
        if (this.ending && !this.queue.length) {
            this.ending = false;
            this.port.postMessage({type: 'drained', streamId: this.streamId});
        }
        return true;
    }
}

registerProcessor('liteapp-playback', PlaybackProcessor);
