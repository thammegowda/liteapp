import {
    ProtocolError,
    RemoteError,
    RequestCancelledError,
    TERMINAL_EVENTS,
    cancelMessage,
    requestMessage,
    validateEnvelope,
} from './protocol.mjs';

function newRequestId() {
    if (!globalThis.crypto?.randomUUID)
        throw new Error('A cryptographically secure randomUUID implementation is required');
    return globalThis.crypto.randomUUID();
}

export class RequestChannel {
    #disposed = false;
    #pending = new Map();
    #transport;
    #unsubscribe;

    constructor(transport) {
        if (!transport || typeof transport.post !== 'function' || typeof transport.subscribe !== 'function')
            throw new TypeError('RequestChannel requires a transport with post and subscribe methods');
        this.#transport = transport;
        this.#unsubscribe = transport.subscribe(message => this.#receive(message));
    }

    request(method, payload, {id = newRequestId(), signal, onEvent, transfer} = {}) {
        if (this.#disposed) return Promise.reject(new Error('Request channel is disposed'));
        if (signal?.aborted) return Promise.reject(new RequestCancelledError());
        if (typeof id !== 'string' || !id || id.length > 128)
            return Promise.reject(new TypeError('Request ID must contain 1-128 characters'));
        if (this.#pending.has(id)) return Promise.reject(new Error(`Request ID is already active: ${id}`));
        return new Promise((resolve, reject) => {
            const abort = () => {
                const pending = this.#pending.get(id);
                if (!pending) return;
                this.#pending.delete(id);
                pending.cleanup();
                try {
                    this.#transport.post(cancelMessage(id));
                } catch {}
                reject(new RequestCancelledError());
            };
            const cleanup = () => signal?.removeEventListener('abort', abort);
            this.#pending.set(id, {
                cleanup,
                expectedSequence: 0,
                onEvent: typeof onEvent === 'function' ? onEvent : null,
                reject,
                resolve,
            });
            signal?.addEventListener('abort', abort, {once: true});
            try {
                this.#transport.post(requestMessage(id, method, payload), transfer);
            } catch (error) {
                this.#pending.delete(id);
                cleanup();
                reject(error);
            }
        });
    }

    dispose(reason = 'Request channel disposed') {
        if (this.#disposed) return;
        this.#disposed = true;
        this.#unsubscribe?.();
        this.#unsubscribe = null;
        for (const pending of this.#pending.values()) {
            pending.cleanup();
            pending.reject(new Error(reason));
        }
        this.#pending.clear();
        this.#transport.close?.();
    }

    #receive(value) {
        let message;
        try {
            message = validateEnvelope(value);
        } catch {
            return;
        }
        if (message.kind !== 'event') return;
        const pending = this.#pending.get(message.id);
        if (!pending) return;
        if (message.sequence !== pending.expectedSequence) {
            this.#pending.delete(message.id);
            pending.cleanup();
            pending.reject(new ProtocolError(
                `Expected event sequence ${pending.expectedSequence}, received ${message.sequence}`,
            ));
            return;
        }
        pending.expectedSequence++;
        if (!TERMINAL_EVENTS.has(message.type)) {
            try {
                pending.onEvent?.({type: message.type, payload: message.payload, sequence: message.sequence});
            } catch (error) {
                this.#pending.delete(message.id);
                pending.cleanup();
                try {
                    this.#transport.post(cancelMessage(message.id));
                } catch {}
                pending.reject(error);
            }
            return;
        }
        this.#pending.delete(message.id);
        pending.cleanup();
        if (message.type === 'completed') {
            pending.resolve(message.payload);
        } else if (message.type === 'cancelled') {
            pending.reject(new RequestCancelledError(message.payload?.message));
        } else {
            pending.reject(new RemoteError(
                message.payload?.code,
                message.payload?.message || 'Remote request failed',
                message.payload?.details,
            ));
        }
    }
}
