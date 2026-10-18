export const PROTOCOL_VERSION = 1;
export const TERMINAL_EVENTS = new Set(['completed', 'cancelled', 'error']);

export class ProtocolError extends Error {
    constructor(message) {
        super(message);
        this.name = 'ProtocolError';
    }
}

export class RemoteError extends Error {
    constructor(code, message, details) {
        super(message);
        this.name = 'RemoteError';
        this.code = code || 'remote_error';
        this.details = details;
    }
}

export class RequestCancelledError extends Error {
    constructor(message = 'Request cancelled') {
        super(message);
        this.name = 'AbortError';
    }
}

export function requestMessage(id, method, payload) {
    return {version: PROTOCOL_VERSION, kind: 'request', id, method, payload};
}

export function cancelMessage(id) {
    return {version: PROTOCOL_VERSION, kind: 'cancel', id};
}

export function eventMessage(id, sequence, type, payload) {
    return {version: PROTOCOL_VERSION, kind: 'event', id, sequence, type, payload};
}

export function validateEnvelope(message) {
    if (!message || typeof message !== 'object' || Array.isArray(message))
        throw new ProtocolError('Protocol message must be an object');
    if (message.version !== PROTOCOL_VERSION)
        throw new ProtocolError(`Unsupported protocol version: ${String(message.version)}`);
    if (typeof message.kind !== 'string')
        throw new ProtocolError('Protocol message kind must be a string');
    if (typeof message.id !== 'string' || !message.id || message.id.length > 128)
        throw new ProtocolError('Protocol request ID must contain 1-128 characters');
    if (message.kind === 'request') {
        if (typeof message.method !== 'string' || !message.method || message.method.length > 128)
            throw new ProtocolError('Protocol method must contain 1-128 characters');
    } else if (message.kind === 'event') {
        if (!Number.isSafeInteger(message.sequence) || message.sequence < 0)
            throw new ProtocolError('Protocol event sequence must be a non-negative integer');
        if (typeof message.type !== 'string' || !message.type || message.type.length > 128)
            throw new ProtocolError('Protocol event type must contain 1-128 characters');
    } else if (message.kind !== 'cancel') {
        throw new ProtocolError(`Unsupported protocol message kind: ${message.kind}`);
    }
    return message;
}
