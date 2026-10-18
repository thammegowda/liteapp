export class MessageTransport {
    #closed = false;
    #listeners = new Set();
    #onMessage;
    #owned;
    #port;

    constructor(port, {owned = false} = {}) {
        if (!port || typeof port.postMessage !== 'function')
            throw new TypeError('MessageTransport requires a Worker or MessagePort');
        this.#port = port;
        this.#owned = owned;
        this.#onMessage = event => {
            const value = event && typeof event === 'object' && 'data' in event ? event.data : event;
            for (const listener of this.#listeners) listener(value);
        };
        if (typeof port.addEventListener === 'function') {
            port.addEventListener('message', this.#onMessage);
            port.start?.();
        } else if (typeof port.on === 'function') {
            port.on('message', this.#onMessage);
        } else {
            throw new TypeError('MessageTransport port cannot receive messages');
        }
    }

    post(message, transfer = []) {
        if (this.#closed) throw new Error('Message transport is closed');
        this.#port.postMessage(message, transfer);
    }

    subscribe(listener) {
        if (this.#closed) throw new Error('Message transport is closed');
        if (typeof listener !== 'function') throw new TypeError('Message listener must be a function');
        this.#listeners.add(listener);
        return () => this.#listeners.delete(listener);
    }

    close() {
        if (this.#closed) return;
        this.#closed = true;
        this.#listeners.clear();
        if (typeof this.#port.removeEventListener === 'function')
            this.#port.removeEventListener('message', this.#onMessage);
        else
            this.#port.off?.('message', this.#onMessage);
        if (this.#owned) {
            if (typeof this.#port.terminate === 'function') this.#port.terminate();
            else this.#port.close?.();
        }
    }
}
