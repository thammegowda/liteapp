import {
    RequestCancelledError,
    eventMessage,
    validateEnvelope,
} from './protocol.mjs';

function errorPayload(error) {
    return {
        code: typeof error?.code === 'string' ? error.code : 'service_error',
        message: error?.message || String(error),
        ...(error?.details === undefined ? {} : {details: error.details}),
    };
}

export class ServiceRouter {
    #active = new Map();
    #disposed = false;
    #handlers = new Map();
    #transport;
    #unsubscribe;

    constructor(transport) {
        if (!transport || typeof transport.post !== 'function' || typeof transport.subscribe !== 'function')
            throw new TypeError('ServiceRouter requires a transport with post and subscribe methods');
        this.#transport = transport;
        this.#unsubscribe = transport.subscribe(message => this.#receive(message));
    }

    register(method, handler) {
        if (typeof method !== 'string' || !method || method.length > 128)
            throw new TypeError('Service method must contain 1-128 characters');
        if (typeof handler !== 'function') throw new TypeError('Service handler must be a function');
        if (this.#handlers.has(method)) throw new Error(`Service method is already registered: ${method}`);
        this.#handlers.set(method, handler);
        return () => this.#handlers.delete(method);
    }

    dispose() {
        if (this.#disposed) return;
        this.#disposed = true;
        this.#unsubscribe?.();
        this.#unsubscribe = null;
        for (const request of this.#active.values()) request.controller.abort();
        this.#active.clear();
        this.#handlers.clear();
        this.#transport.close?.();
    }

    #receive(value) {
        if (this.#disposed) return;
        let message;
        try {
            message = validateEnvelope(value);
        } catch {
            return;
        }
        if (message.kind === 'cancel') {
            this.#active.get(message.id)?.controller.abort();
            return;
        }
        if (message.kind !== 'request') return;
        if (this.#active.has(message.id)) {
            this.#transport.post(eventMessage(message.id, 0, 'error', {
                code: 'duplicate_request',
                message: 'Request ID is already active',
            }));
            return;
        }
        const handler = this.#handlers.get(message.method);
        if (!handler) {
            this.#transport.post(eventMessage(message.id, 0, 'error', {
                code: 'unknown_method',
                message: `Unknown service method: ${message.method}`,
            }));
            return;
        }
        const controller = new AbortController();
        const request = {controller, sequence: 0, terminal: false};
        this.#active.set(message.id, request);
        const emit = (type, payload, transfer) => {
            if (request.terminal || controller.signal.aborted) return false;
            if (type === 'completed' || type === 'cancelled' || type === 'error')
                throw new Error(`Service handlers cannot emit reserved terminal event: ${type}`);
            this.#transport.post(eventMessage(message.id, request.sequence++, type, payload), transfer);
            return true;
        };
        Promise.resolve().then(() => handler({
            emit,
            id: message.id,
            method: message.method,
            payload: message.payload,
            signal: controller.signal,
        })).then(
            result => this.#finish(message.id, 'completed', result),
            error => {
                if (controller.signal.aborted || error instanceof RequestCancelledError ||
                    error?.name === 'AbortError')
                    this.#finish(message.id, 'cancelled', {message: error?.message || 'Request cancelled'});
                else
                    this.#finish(message.id, 'error', errorPayload(error));
            },
        );
    }

    #finish(id, type, payload) {
        const request = this.#active.get(id);
        if (!request || request.terminal) return;
        request.terminal = true;
        this.#active.delete(id);
        this.#transport.post(eventMessage(id, request.sequence++, type, payload));
    }
}
