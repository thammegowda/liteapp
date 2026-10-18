import {RequestChannel} from '../transport/channel.mjs';
import {MessageTransport} from '../transport/message-transport.mjs';
import {connectWebSocket} from '../transport/websocket-transport.mjs';

const VARIANTS = new Set(['desktop', 'wasm', 'webgpu']);

export async function createVariantClient({
    variant,
    desktopUrl,
    workerUrls = {},
    WebSocketImpl = globalThis.WebSocket,
    WorkerImpl = globalThis.Worker,
    webGpu = globalThis.navigator?.gpu,
} = {}) {
    if (!VARIANTS.has(variant)) throw new Error(`Unknown LiteApp host variant: ${String(variant)}`);
    let transport;
    if (variant === 'desktop') {
        if (!desktopUrl) throw new Error('The desktop variant requires a WebSocket URL');
        transport = await connectWebSocket(desktopUrl, {WebSocketImpl});
    } else {
        if (variant === 'webgpu' && !webGpu)
            throw new Error('The WebGPU variant is unavailable in this browser');
        if (typeof WorkerImpl !== 'function') throw new Error('This runtime does not provide Worker');
        const url = workerUrls[variant];
        if (!url) throw new Error(`The ${variant} variant requires a worker URL`);
        transport = new MessageTransport(new WorkerImpl(url, {type: 'module'}), {owned: true});
    }
    const channel = new RequestChannel(transport);
    return {
        variant,
        channel,
        close: () => channel.dispose(),
        get bufferedAmount() {
            return transport.bufferedAmount || 0;
        },
    };
}
