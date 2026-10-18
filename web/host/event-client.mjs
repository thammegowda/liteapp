export class EventClient {
    #channel;
    #completionEvents;
    #mapError;
    #onEvent;
    #requests = new Map();

    constructor(channel, {
        completionEvents = {},
        mapError = error => ({
            code: error.code || 'service_error',
            message: error.message || 'Service request failed',
            ...(error.details === undefined ? {} : {details: error.details}),
        }),
        onEvent,
    } = {}) {
        if (!channel || typeof channel.request !== 'function' || typeof channel.dispose !== 'function')
            throw new TypeError('EventClient requires a RequestChannel');
        if (typeof onEvent !== 'function') throw new TypeError('EventClient requires an event callback');
        this.#channel = channel;
        this.#completionEvents = completionEvents instanceof Map
            ? completionEvents : new Map(Object.entries(completionEvents));
        this.#mapError = mapError;
        this.#onEvent = onEvent;
    }

    get active() {
        return this.#requests.size;
    }

    has(id) {
        return this.#requests.has(id);
    }

    request(method, id, payload = {}) {
        if (this.#requests.has(id)) throw new Error(`Request ID is already active: ${id}`);
        const controller = new AbortController();
        const request = {controller, method, sequence: 0};
        this.#requests.set(id, request);
        this.#channel.request(method, payload, {
            id,
            signal: controller.signal,
            onEvent: event => {
                request.sequence = event.sequence + 1;
                this.#emit(id, event.type, event.payload, event.sequence);
            },
        }).then(result => {
            const completed = this.#completionEvents.get(method);
            if (completed) this.#emit(id, completed, result, request.sequence);
        }).catch(error => {
            if (error.name === 'AbortError')
                this.#emit(id, 'request.cancelled', {message: error.message}, request.sequence);
            else
                this.#emit(id, 'error', this.#mapError(error, {id, method}), request.sequence);
        }).finally(() => {
            if (this.#requests.get(id) === request) this.#requests.delete(id);
        });
    }

    cancel(id) {
        const request = this.#requests.get(id);
        if (!request) return false;
        request.controller.abort();
        return true;
    }

    dispose(reason) {
        for (const request of this.#requests.values()) request.controller.abort();
        this.#requests.clear();
        this.#channel.dispose(reason);
    }

    #emit(id, type, payload = {}, sequence = 0) {
        this.#onEvent({id, type, payload: payload || {}, sequence});
    }
}
