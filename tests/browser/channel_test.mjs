import assert from 'node:assert/strict';
import test from 'node:test';
import {MessageChannel} from 'node:worker_threads';

import {
    MessageTransport,
    AndroidTransport,
    EventClient,
    RequestChannel,
    ServiceRouter,
    WebSocketTransport,
    connectAndroid,
    connectWebSocket,
    createVariantClient,
} from '../../web/liteapp.mjs';

class MockWebSocket extends EventTarget {
    static instances = [];

    bufferedAmount = 0;
    readyState = 0;
    sent = [];

    constructor(url) {
        super();
        this.url = url;
        MockWebSocket.instances.push(this);
        queueMicrotask(() => {
            this.readyState = 1;
            this.dispatchEvent(new Event('open'));
        });
    }

    send(value) {
        if (this.readyState !== 1) throw new Error('socket is not open');
        this.sent.push(value);
    }

    receive(value) {
        this.dispatchEvent(new MessageEvent('message', {data: value}));
    }

    close() {
        if (this.readyState >= 2) return;
        this.readyState = 3;
        this.dispatchEvent(new Event('close'));
    }
}

class MockAndroidPort extends EventTarget {
    closed = false;
    sent = [];

    postMessage(value) {
        if (this.closed) throw new Error('port is closed');
        this.sent.push(value);
    }

    receive(value) {
        this.dispatchEvent(new MessageEvent('message', {data: value}));
    }

    start() {}

    close() {
        this.closed = true;
    }
}

function runtime() {
    const ports = new MessageChannel();
    const clientTransport = new MessageTransport(ports.port1, {owned: true});
    const serviceTransport = new MessageTransport(ports.port2, {owned: true});
    return {
        channel: new RequestChannel(clientTransport),
        router: new ServiceRouter(serviceTransport),
    };
}

test('routes requests and ordered progress events', async t => {
    const {channel, router} = runtime();
    t.after(() => {
        channel.dispose();
        router.dispose();
    });
    router.register('math.double', async ({payload, emit}) => {
        emit('progress', {value: payload.value});
        await Promise.resolve();
        emit('progress', {value: payload.value * 1.5});
        return {value: payload.value * 2};
    });
    const events = [];
    const result = await channel.request('math.double', {value: 4}, {
        onEvent: event => events.push(event),
    });
    assert.deepEqual(result, {value: 8});
    assert.deepEqual(events, [
        {type: 'progress', payload: {value: 4}, sequence: 0},
        {type: 'progress', payload: {value: 6}, sequence: 1},
    ]);
});

test('cancels an active service through AbortSignal', async t => {
    const {channel, router} = runtime();
    t.after(() => {
        channel.dispose();
        router.dispose();
    });
    let serviceCancelled = false;
    router.register('timer.wait', ({signal, emit}) => new Promise((resolve, reject) => {
        emit('started');
        const timer = setTimeout(resolve, 5000);
        signal.addEventListener('abort', () => {
            clearTimeout(timer);
            serviceCancelled = true;
            reject(new DOMException('Stopped', 'AbortError'));
        }, {once: true});
    }));
    const controller = new AbortController();
    const pending = channel.request('timer.wait', {}, {
        signal: controller.signal,
        onEvent: event => {
            if (event.type === 'started') controller.abort();
        },
    });
    await assert.rejects(pending, error => error.name === 'AbortError');
    await new Promise(resolve => setTimeout(resolve, 0));
    assert.equal(serviceCancelled, true);
});

test('returns structured errors for unknown methods', async t => {
    const {channel, router} = runtime();
    t.after(() => {
        channel.dispose();
        router.dispose();
    });

    test('accepts caller-owned request IDs and rejects active duplicates', async t => {
        const {channel, router} = runtime();
        t.after(() => {
            channel.dispose();
            router.dispose();
        });
        let release;
        router.register('hold', () => new Promise(resolve => { release = resolve; }));
        const first = channel.request('hold', {}, {id: 'client-request'});
        await new Promise(resolve => setImmediate(resolve));
        await assert.rejects(channel.request('hold', {}, {id: 'client-request'}), /already active/);
        release({ok: true});
        assert.deepEqual(await first, {ok: true});
    });
    await assert.rejects(
        channel.request('missing.method', {}),
        error => error.name === 'RemoteError' && error.code === 'unknown_method' &&
            error.message === 'Unknown service method: missing.method',
    );
});

test('returns application service errors without exposing framework internals', async t => {
    const {channel, router} = runtime();
    t.after(() => {
        channel.dispose();
        router.dispose();
    });
    router.register('documents.open', () => {
        const error = new Error('Document is unavailable');
        error.code = 'not_found';
        error.details = {documentId: 7};
        throw error;
    });
    await assert.rejects(
        channel.request('documents.open', {documentId: 7}),
        error => error.name === 'RemoteError' && error.code === 'not_found' &&
            error.message === 'Document is unavailable' && error.details.documentId === 7,
    );
});

test('transfers binary payloads without retaining the sender buffer', async t => {
    const {channel, router} = runtime();
    t.after(() => {
        channel.dispose();
        router.dispose();
    });
    router.register('binary.size', ({payload}) => ({bytes: payload.buffer.byteLength}));
    const buffer = new Uint8Array([1, 2, 3, 4]).buffer;
    const result = await channel.request('binary.size', {buffer}, {transfer: [buffer]});
    assert.equal(buffer.byteLength, 0);
    assert.deepEqual(result, {bytes: 4});
});

test('WebSocket transport serializes protocol messages and tracks closure', async () => {
    const transport = await connectWebSocket('ws://127.0.0.1/session', {WebSocketImpl: MockWebSocket});
    const socket = MockWebSocket.instances.at(-1);
    const received = [];
    const binary = [];
    transport.subscribe(message => received.push(message));
    transport.subscribeBinary(message => binary.push(message));
    transport.post({version: 1, kind: 'cancel', id: 'request'});
    assert.deepEqual(JSON.parse(socket.sent[0]), {version: 1, kind: 'cancel', id: 'request'});
    socket.receive(JSON.stringify({version: 1, kind: 'event', id: 'request', sequence: 0,
        type: 'completed', payload: {ok: true}}));
    assert.deepEqual(received, [{
        version: 1, kind: 'event', id: 'request', sequence: 0, type: 'completed', payload: {ok: true},
    }]);
    const bytes = new Uint8Array([1, 2, 3]);
    transport.postBinary(bytes);
    assert.equal(socket.sent.at(-1), bytes);
    socket.receive(bytes.buffer);
    assert.deepEqual(binary, [bytes.buffer]);
    socket.close();
    await transport.closed;
    assert.throws(() => transport.post({}), /not open/);
});

test('Android transport serializes protocol messages and separates binary frames', async () => {
    const port = new MockAndroidPort();
    const transport = new AndroidTransport(port);
    const received = [];
    const binary = [];
    transport.subscribe(message => received.push(message));
    transport.subscribeBinary(message => binary.push(message));
    transport.post({version: 1, kind: 'cancel', id: 'request'});
    assert.deepEqual(JSON.parse(port.sent[0]), {version: 1, kind: 'cancel', id: 'request'});
    port.receive(JSON.stringify({version: 1, kind: 'event', id: 'request', sequence: 0,
        type: 'completed', payload: {ok: true}}));
    assert.equal(received[0].type, 'completed');
    port.receive(new Uint8Array([1, 2, 3]).buffer);
    assert.equal(new Uint8Array(binary[0])[2], 3);
    transport.postBinary(new Uint8Array([4, 5]));
    assert.deepEqual([...new Uint8Array(port.sent.at(-1))], [4, 5]);
    transport.close();
    await transport.closed;
    assert.equal(port.closed, true);
    assert.throws(() => transport.post({}), /closed/);
});

test('Android transport consumes a document-start port promise', async () => {
    const port = new MockAndroidPort();
    const transport = await connectAndroid({
        target: {__liteappAndroidPort: Promise.resolve(port)},
        timeoutMs: 100,
    });
    transport.post({version: 1, kind: 'cancel', id: 'request'});
    assert.equal(JSON.parse(port.sent[0]).id, 'request');
    transport.close();
});

test('variant client creates a desktop request channel over WebSocket', async () => {
    const client = await createVariantClient({
        variant: 'desktop',
        desktopUrl: 'ws://127.0.0.1/services',
        WebSocketImpl: MockWebSocket,
    });
    assert.equal(client.variant, 'desktop');
    assert.equal(typeof client.channel.request, 'function');
    client.close();
    assert.equal(MockWebSocket.instances.at(-1).readyState, 3);
});

test('variant client creates an Android request channel over a transferred port', async () => {
    const target = new EventTarget();
    const port = new MockAndroidPort();
    const pending = createVariantClient({variant: 'android', androidTarget: target});
    target.dispatchEvent(Object.assign(new Event('message'), {
        data: 'liteapp:android:connect',
        ports: [port],
    }));
    const client = await pending;
    assert.equal(client.variant, 'android');
    assert.equal(typeof client.channel.request, 'function');
    client.close();
    assert.equal(port.closed, true);
});

test('variant client rejects unavailable and incomplete hosts', async () => {
    await assert.rejects(createVariantClient({variant: 'webgpu', webGpu: null}), /unavailable/);
    await assert.rejects(createVariantClient({variant: 'wasm', WorkerImpl: null}), /Worker/);
    await assert.rejects(createVariantClient({variant: 'other'}), /Unknown/);
    assert.throws(() => new WebSocketTransport({}), /requires a WebSocket/);
});

test('event client adapts progress, completion, errors, and cancellation', async t => {
    const {channel, router} = runtime();
    t.after(() => router.dispose());
    router.register('work.run', ({emit}) => {
        emit('progress', {step: 1});
        return {value: 2};
    });
    const events = [];
    const client = new EventClient(channel, {
        completionEvents: {'work.run': 'work.completed'},
        onEvent: event => events.push(event),
    });
    client.request('work.run', 'work', {});
    for (let attempt = 0; attempt < 20 && events.length < 2; ++attempt)
        await new Promise(resolve => setImmediate(resolve));
    assert.deepEqual(events, [
        {id: 'work', type: 'progress', payload: {step: 1}, sequence: 0},
        {id: 'work', type: 'work.completed', payload: {value: 2}, sequence: 1},
    ]);
    client.dispose();
});
