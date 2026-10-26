function listeners(event, name) {
    if (!event || typeof event.addListener !== 'function' || typeof event.removeListener !== 'function')
        throw new TypeError(`Extension port requires ${name} listener support`);
    return event;
}

export class ExtensionPortTransport {
    #closed = false;
    #finishClosed;
    #listeners = new Set();
    #message;
    #disconnect;
    #port;

    constructor(port, {owned = false} = {}) {
        if (!port || typeof port.postMessage !== 'function')
            throw new TypeError('ExtensionPortTransport requires a runtime Port');
        this.#port = port;
        this.owned = owned;
        const messages = listeners(port.onMessage, 'onMessage');
        const disconnects = listeners(port.onDisconnect, 'onDisconnect');
        this.#message = value => {
            for (const listener of this.#listeners) listener(value);
        };
        this.#disconnect = () => this.#close(false);
        messages.addListener(this.#message);
        disconnects.addListener(this.#disconnect);
        this.closed = new Promise(resolve => { this.#finishClosed = resolve; });
    }

    get open() {
        return !this.#closed;
    }

    get bufferedAmount() {
        return 0;
    }

    post(message) {
        if (this.#closed) throw new Error('Extension port is closed');
        this.#port.postMessage(message);
    }

    subscribe(listener) {
        if (this.#closed) throw new Error('Extension port is closed');
        if (typeof listener !== 'function') throw new TypeError('Message listener must be a function');
        this.#listeners.add(listener);
        return () => this.#listeners.delete(listener);
    }

    close() {
        this.#close(true);
    }

    #close(disconnect) {
        if (this.#closed) return;
        this.#closed = true;
        this.#port.onMessage.removeListener(this.#message);
        this.#port.onDisconnect.removeListener(this.#disconnect);
        this.#listeners.clear();
        if (disconnect && this.owned) this.#port.disconnect?.();
        this.#finishClosed();
    }
}

export function connectExtensionPort(name, {
    runtime = globalThis.browser?.runtime || globalThis.chrome?.runtime,
} = {}) {
    if (!runtime || typeof runtime.connect !== 'function')
        throw new Error('This runtime does not provide extension messaging');
    return new ExtensionPortTransport(runtime.connect({name}), {owned: true});
}
