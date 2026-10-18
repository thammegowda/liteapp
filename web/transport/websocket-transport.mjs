function listen(socket, type, listener) {
    if (typeof socket.addEventListener === 'function') {
        socket.addEventListener(type, listener);
        return () => socket.removeEventListener(type, listener);
    }
    if (typeof socket.on === 'function') {
        socket.on(type, listener);
        return () => socket.off?.(type, listener);
    }
    throw new TypeError('WebSocket cannot receive events');
}

export class WebSocketTransport {
    #binaryListeners = new Set();
    #closed = false;
    #closedPromise;
    #finishClosed;
    #listeners = new Set();
    #owned;
    #ready;
    #socket;
    #unsubscribe = [];

    constructor(socket, {binaryType = 'arraybuffer', owned = false} = {}) {
        if (!socket || typeof socket.send !== 'function' || typeof socket.close !== 'function')
            throw new TypeError('WebSocketTransport requires a WebSocket');
        this.#socket = socket;
        this.#owned = owned;
        if ('binaryType' in socket) socket.binaryType = binaryType;
        this.#closedPromise = new Promise(resolve => { this.#finishClosed = resolve; });
        this.#ready = socket.readyState === 1 ? Promise.resolve() : new Promise((resolve, reject) => {
            const opened = () => {
                cleanup();
                resolve();
            };
            const failed = () => {
                cleanup();
                reject(new Error('WebSocket connection failed'));
            };
            const cleanup = () => {
                removeOpen();
                removeError();
                removeClose();
            };
            const removeOpen = listen(socket, 'open', opened);
            const removeError = listen(socket, 'error', failed);
            const removeClose = listen(socket, 'close', failed);
        });
        this.#unsubscribe.push(
            listen(socket, 'message', event => {
                const data = event && typeof event === 'object' && 'data' in event ? event.data : event;
                if (typeof data !== 'string') {
                    for (const listener of this.#binaryListeners) listener(data);
                    return;
                }
                let message;
                try {
                    message = JSON.parse(data);
                } catch {
                    return;
                }
                for (const listener of this.#listeners) listener(message);
            }),
            listen(socket, 'close', () => this.#finish()),
        );
    }

    get bufferedAmount() {
        return Number(this.#socket?.bufferedAmount || 0);
    }

    get closed() {
        return this.#closedPromise;
    }

    get ready() {
        return this.#ready;
    }

    get open() {
        return !this.#closed && this.#socket.readyState === 1;
    }

    post(message) {
        if (!this.open) throw new Error('WebSocket transport is not open');
        this.#socket.send(JSON.stringify(message));
    }

    postBinary(data) {
        if (!this.open) throw new Error('WebSocket transport is not open');
        this.#socket.send(data);
    }

    subscribe(listener) {
        if (this.#closed) throw new Error('WebSocket transport is closed');
        if (typeof listener !== 'function') throw new TypeError('Message listener must be a function');
        this.#listeners.add(listener);
        return () => this.#listeners.delete(listener);
    }

    subscribeBinary(listener) {
        if (this.#closed) throw new Error('WebSocket transport is closed');
        if (typeof listener !== 'function') throw new TypeError('Binary listener must be a function');
        this.#binaryListeners.add(listener);
        return () => this.#binaryListeners.delete(listener);
    }

    async waitForDrain({highWaterMark = 512 * 1024, timeoutMs = 30000, intervalMs = 5} = {}) {
        if (!Number.isSafeInteger(highWaterMark) || highWaterMark < 0 ||
            !Number.isSafeInteger(timeoutMs) || timeoutMs <= 0 ||
            !Number.isSafeInteger(intervalMs) || intervalMs <= 0)
            throw new TypeError('WebSocket drain limits must be positive integers');
        const started = performance.now();
        while (!this.#closed && this.#socket.readyState === 1 && this.bufferedAmount > highWaterMark) {
            if (performance.now() - started > timeoutMs)
                throw new Error('WebSocket transport could not drain');
            await new Promise(resolve => setTimeout(resolve, intervalMs));
        }
        if (this.#closed || this.#socket.readyState !== 1)
            throw new Error('WebSocket transport closed while draining');
    }

    close(code, reason) {
        if (this.#closed) return;
        if (this.#owned && this.#socket.readyState < 2) this.#socket.close(code, reason);
        this.#finish();
    }

    #finish() {
        if (this.#closed) return;
        this.#closed = true;
        for (const unsubscribe of this.#unsubscribe.splice(0)) unsubscribe();
        this.#listeners.clear();
        this.#binaryListeners.clear();
        this.#finishClosed();
    }
}

export async function connectWebSocket(url, {
    WebSocketImpl = globalThis.WebSocket,
    protocols,
    ...transportOptions
} = {}) {
    if (typeof WebSocketImpl !== 'function')
        throw new Error('This runtime does not provide WebSocket');
    const socket = protocols === undefined ? new WebSocketImpl(url) : new WebSocketImpl(url, protocols);
    const transport = new WebSocketTransport(socket, {owned: true, ...transportOptions});
    await transport.ready;
    return transport;
}
