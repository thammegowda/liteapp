import {ExtensionPortTransport} from './runtime-port-transport.mjs';
import {eventMessage, validateEnvelope} from '../transport/protocol.mjs';

function identifier() {
    if (!globalThis.crypto?.randomUUID)
        throw new Error('A cryptographically secure randomUUID implementation is required');
    return globalThis.crypto.randomUUID();
}

export class ServiceBroker {
    #clients = new Set();
    #connect;
    #disposed = false;
    #provider;
    #providerPromise;
    #providerUnsubscribe;
    #requests = new Map();

    constructor({connect}) {
        if (typeof connect !== 'function') throw new TypeError('ServiceBroker requires a provider connector');
        this.#connect = connect;
    }

    attach(port) {
        if (this.#disposed) throw new Error('Service broker is disposed');
        const transport = port instanceof ExtensionPortTransport
            ? port : new ExtensionPortTransport(port, {owned: true});
        const client = {transport, requests: new Map(), unsubscribe: null};
        client.unsubscribe = transport.subscribe(message => {
            this.#receiveClient(client, message).catch(error => {
                this.#failClientRequest(client, message?.id, error);
            });
        });
        transport.closed.then(() => this.#detach(client));
        this.#clients.add(client);
        return () => {
            transport.close();
            this.#detach(client);
        };
    }

    dispose(reason = 'Service broker disposed') {
        if (this.#disposed) return;
        this.#disposed = true;
        for (const client of [...this.#clients]) {
            for (const request of client.requests.values())
                this.#provider?.post({version: 1, kind: 'cancel', id: request.providerId});
            client.transport.close();
        }
        this.#clients.clear();
        this.#failProviderRequests(reason);
        this.#providerUnsubscribe?.();
        this.#providerUnsubscribe = null;
        this.#provider?.close?.();
        this.#provider = null;
        this.#providerPromise = null;
    }

    async #ensureProvider() {
        if (this.#provider) return this.#provider;
        if (!this.#providerPromise) {
            this.#providerPromise = Promise.resolve().then(() => this.#connect()).then(provider => {
                if (!provider || typeof provider.post !== 'function' || typeof provider.subscribe !== 'function')
                    throw new TypeError('Service provider must implement the LiteApp transport contract');
                if (this.#disposed) {
                    provider.close?.();
                    throw new Error('Service broker is disposed');
                }
                this.#provider = provider;
                this.#providerUnsubscribe = provider.subscribe(message => this.#receiveProvider(message));
                provider.closed?.then(() => {
                    if (provider !== this.#provider) return;
                    this.#providerUnsubscribe?.();
                    this.#providerUnsubscribe = null;
                    this.#provider = null;
                    this.#providerPromise = null;
                    this.#failProviderRequests('Service provider disconnected');
                });
                return provider;
            }).catch(error => {
                this.#providerPromise = null;
                throw error;
            });
        }
        return this.#providerPromise;
    }

    async #receiveClient(client, value) {
        const message = validateEnvelope(value);
        if (message.kind === 'event') throw new Error('Extension clients cannot send service events');
        const provider = await this.#ensureProvider();
        if (message.kind === 'cancel') {
            const request = client.requests.get(message.id);
            if (request) provider.post({version: 1, kind: 'cancel', id: request.providerId});
            return;
        }
        if (client.requests.has(message.id)) {
            client.transport.post(eventMessage(message.id, 0, 'error', {
                code: 'duplicate_request',
                message: 'Request ID is already active',
            }));
            return;
        }
        const providerId = identifier();
        const request = {client, originalId: message.id, providerId, nextSequence: 0};
        client.requests.set(message.id, request);
        this.#requests.set(providerId, request);
        try {
            provider.post({...message, id: providerId});
        } catch (error) {
            this.#removeRequest(request);
            throw error;
        }
    }

    #receiveProvider(value) {
        let message;
        try {
            message = validateEnvelope(value);
        } catch {
            return;
        }
        if (message.kind !== 'event') return;
        const request = this.#requests.get(message.id);
        if (!request) return;
        request.nextSequence = message.sequence + 1;
        try {
            request.client.transport.post({...message, id: request.originalId});
        } catch {
            this.#cancelRequest(request);
            return;
        }
        if (['completed', 'cancelled', 'error'].includes(message.type)) this.#removeRequest(request);
    }

    #failClientRequest(client, id, error) {
        if (typeof id !== 'string' || !id || client.requests.has(id)) return;
        try {
            client.transport.post(eventMessage(id, 0, 'error', {
                code: 'broker_error',
                message: error?.message || String(error),
            }));
        } catch {}
    }

    #failProviderRequests(message) {
        for (const request of [...this.#requests.values()]) {
            try {
                request.client.transport.post(eventMessage(request.originalId, request.nextSequence, 'error', {
                    code: 'provider_disconnected',
                    message,
                }));
            } catch {}
            this.#removeRequest(request);
        }
    }

    #cancelRequest(request) {
        try {
            this.#provider?.post({version: 1, kind: 'cancel', id: request.providerId});
        } catch {}
        this.#removeRequest(request);
    }

    #removeRequest(request) {
        request.client.requests.delete(request.originalId);
        this.#requests.delete(request.providerId);
    }

    #detach(client) {
        if (!this.#clients.delete(client)) return;
        client.unsubscribe?.();
        for (const request of [...client.requests.values()]) this.#cancelRequest(request);
    }
}
