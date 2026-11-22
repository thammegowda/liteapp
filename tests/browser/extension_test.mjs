import assert from 'node:assert/strict';
import test from 'node:test';
import {MessageChannel} from 'node:worker_threads';

import {
    ExtensionPortTransport,
    MessageTransport,
    RequestChannel,
    ServiceBroker,
    ServiceRouter,
} from '../../web/liteapp.mjs';

class ListenerEvent {
    listeners = new Set();

    addListener(listener) {
        this.listeners.add(listener);
    }

    removeListener(listener) {
        this.listeners.delete(listener);
    }

    emit(value) {
        for (const listener of this.listeners) listener(value);
    }
}

function extensionPorts() {
    const leftMessage = new ListenerEvent(), rightMessage = new ListenerEvent();
    const leftDisconnect = new ListenerEvent(), rightDisconnect = new ListenerEvent();
    let closed = false;
    const left = {
        onMessage: leftMessage,
        onDisconnect: leftDisconnect,
        postMessage: value => {
            if (closed) throw new Error('port is closed');
            queueMicrotask(() => rightMessage.emit(structuredClone(value)));
        },
        disconnect: () => {
            if (closed) return;
            closed = true;
            leftDisconnect.emit();
            rightDisconnect.emit();
        },
    };
    const right = {
        onMessage: rightMessage,
        onDisconnect: rightDisconnect,
        postMessage: value => {
            if (closed) throw new Error('port is closed');
            queueMicrotask(() => leftMessage.emit(structuredClone(value)));
        },
        disconnect: left.disconnect,
    };
    return {left, right};
}

function provider() {
    const ports = new MessageChannel();
    const client = new MessageTransport(ports.port1, {owned: true});
    const services = new ServiceRouter(new MessageTransport(ports.port2, {owned: true}));
    services.register('text.upper', async ({payload, emit}) => {
        emit('progress', {stage: 'editing'});
        await Promise.resolve();
        return {text: payload.text.toUpperCase()};
    });
    return {client, services};
}

test('ExtensionPortTransport routes JSON-compatible messages and closure', async () => {
    const {left, right} = extensionPorts();
    const first = new ExtensionPortTransport(left, {owned: true});
    const second = new ExtensionPortTransport(right, {owned: true});
    const received = [];
    second.subscribe(message => received.push(message));
    first.post({hello: 'extension'});
    await new Promise(resolve => setImmediate(resolve));
    assert.deepEqual(received, [{hello: 'extension'}]);
    first.close();
    await Promise.all([first.closed, second.closed]);
    assert.equal(first.open, false);
    assert.equal(second.open, false);
});

test('ServiceBroker multiplexes clients without exposing provider request IDs', async t => {
    const runtime = provider();
    const broker = new ServiceBroker({connect: async () => runtime.client});
    t.after(() => {
        broker.dispose();
        runtime.services.dispose();
    });
    const firstPorts = extensionPorts(), secondPorts = extensionPorts();
    broker.attach(firstPorts.right);
    broker.attach(secondPorts.right);
    const first = new RequestChannel(new ExtensionPortTransport(firstPorts.left, {owned: true}));
    const second = new RequestChannel(new ExtensionPortTransport(secondPorts.left, {owned: true}));
    t.after(() => {
        first.dispose();
        second.dispose();
    });
    const firstEvents = [], secondEvents = [];
    const [one, two] = await Promise.all([
        first.request('text.upper', {text: 'first'}, {
            id: 'same-client-id',
            onEvent: event => firstEvents.push(event),
        }),
        second.request('text.upper', {text: 'second'}, {
            id: 'same-client-id',
            onEvent: event => secondEvents.push(event),
        }),
    ]);
    assert.deepEqual(one, {text: 'FIRST'});
    assert.deepEqual(two, {text: 'SECOND'});
    assert.equal(firstEvents[0].type, 'progress');
    assert.equal(secondEvents[0].type, 'progress');
});

test('ServiceBroker cancels provider work when a client disconnects', async t => {
    const ports = new MessageChannel();
    const providerClient = new MessageTransport(ports.port1, {owned: true});
    const services = new ServiceRouter(new MessageTransport(ports.port2, {owned: true}));
    let cancelled = false;
    services.register('hold', ({signal}) => new Promise((resolve, reject) => {
        signal.addEventListener('abort', () => {
            cancelled = true;
            reject(new DOMException('Stopped', 'AbortError'));
        }, {once: true});
    }));
    const broker = new ServiceBroker({connect: async () => providerClient});
    t.after(() => {
        broker.dispose();
        services.dispose();
    });
    const clientPorts = extensionPorts();
    broker.attach(clientPorts.right);
    const transport = new ExtensionPortTransport(clientPorts.left, {owned: true});
    const channel = new RequestChannel(transport);
    channel.request('hold', {}).catch(() => {});
    await new Promise(resolve => setImmediate(resolve));
    transport.close();
    for (let attempt = 0; attempt < 20 && !cancelled; attempt++)
        await new Promise(resolve => setTimeout(resolve, 1));
    assert.equal(cancelled, true);
});
