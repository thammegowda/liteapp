import assert from 'node:assert/strict';
import test from 'node:test';
import {MessageChannel} from 'node:worker_threads';

import {MessageTransport, RequestChannel, ServiceRouter} from '../../web/liteapp.mjs';

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
