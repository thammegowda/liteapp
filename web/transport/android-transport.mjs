const CONNECT_MESSAGE = 'liteapp:android:connect';

function listen(port, type, listener) {
    if (typeof port.addEventListener === 'function') {
        port.addEventListener(type, listener);
        return () => port.removeEventListener(type, listener);
    }
    const property = `on${type}`;
    const previous = port[property];
    port[property] = listener;
    return () => {
        if (port[property] === listener) port[property] = previous || null;
    };
}

export class AndroidTransport {
    #binaryListeners = new Set();
    #closed = false;
    #finishClosed;
    #listeners = new Set();
    #port;
    #unsubscribe = [];

    constructor(port) {
        if (!port || typeof port.postMessage !== 'function')
            throw new TypeError('AndroidTransport requires a WebMessage port');
        this.#port = port;
        this.closed = new Promise(resolve => { this.#finishClosed = resolve; });
        this.#unsubscribe.push(listen(port, 'message', event => {
            const data = event && typeof event === 'object' && 'data' in event ? event.data : event;
            if (typeof data === 'string') {
                let message;
                try {
                    message = JSON.parse(data);
                } catch {
                    this.close();
                    return;
                }
                for (const listener of this.#listeners) listener(message);
                return;
            }
            if (data instanceof ArrayBuffer || ArrayBuffer.isView(data))
                for (const listener of this.#binaryListeners) listener(data);
        }));
        this.#unsubscribe.push(listen(port, 'messageerror', () => this.close()));
        port.start?.();
    }

    get bufferedAmount() {
        return 0;
    }

    get open() {
        return !this.#closed;
    }

    post(message) {
        if (this.#closed) throw new Error('Android transport is closed');
        this.#port.postMessage(JSON.stringify(message));
    }

    postBinary(data) {
        if (this.#closed) throw new Error('Android transport is closed');
        const bytes = data instanceof ArrayBuffer
            ? new Uint8Array(data)
            : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
        const buffer = bytes.byteOffset === 0 && bytes.byteLength === bytes.buffer.byteLength
            ? bytes.buffer
            : bytes.slice().buffer;
        this.#port.postMessage(buffer, [buffer]);
    }

    subscribe(listener) {
        if (this.#closed) throw new Error('Android transport is closed');
        if (typeof listener !== 'function') throw new TypeError('Message listener must be a function');
        this.#listeners.add(listener);
        return () => this.#listeners.delete(listener);
    }

    subscribeBinary(listener) {
        if (this.#closed) throw new Error('Android transport is closed');
        if (typeof listener !== 'function') throw new TypeError('Binary listener must be a function');
        this.#binaryListeners.add(listener);
        return () => this.#binaryListeners.delete(listener);
    }

    async waitForDrain() {
        if (this.#closed) throw new Error('Android transport is closed');
    }

    close() {
        if (this.#closed) return;
        this.#closed = true;
        for (const unsubscribe of this.#unsubscribe.splice(0)) unsubscribe();
        this.#listeners.clear();
        this.#binaryListeners.clear();
        this.#port.close?.();
        this.#finishClosed();
    }
}

export function connectAndroid({
    target = globalThis,
    timeoutMs = 15000,
} = {}) {
    const bootstrapped = target?.__liteappAndroidPort;
    if (bootstrapped && typeof bootstrapped.then === 'function')
        return new Promise((resolve, reject) => {
            const timer = setTimeout(
                () => reject(new Error('Android host connection timed out')), timeoutMs);
            bootstrapped.then(port => {
                clearTimeout(timer);
                resolve(new AndroidTransport(port));
            }, error => {
                clearTimeout(timer);
                reject(error);
            });
        });
    if (!target || typeof target.addEventListener !== 'function')
        return Promise.reject(new Error('This runtime cannot receive an Android host port'));
    return new Promise((resolve, reject) => {
        let timer;
        const cleanup = () => {
            target.removeEventListener('message', connected);
            if (timer) clearTimeout(timer);
        };
        const connected = event => {
            if (event.data !== CONNECT_MESSAGE || !event.ports?.length) return;
            cleanup();
            resolve(new AndroidTransport(event.ports[0]));
        };
        target.addEventListener('message', connected);
        timer = setTimeout(() => {
            cleanup();
            reject(new Error('Android host connection timed out'));
        }, timeoutMs);
        target.LiteAppAndroidReady?.();
    });
}
