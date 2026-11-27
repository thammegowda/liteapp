// A long-lived application connection: request/event calls, session-wide broadcast events, heartbeats and automatic
// reconnection. By default it opens the desktop host's authenticated WebSocket; `open` can supply any transport
// (a Worker, an Android port). After a drop (a port forwarder closing an idle socket, a laptop waking up) it reopens
// the transport; the application then resynchronizes its state and calls subscribe() again.
import {RequestChannel} from '../transport/channel.mjs';
import {connectWebSocket} from '../transport/websocket-transport.mjs';

export class Connection {
    #channel = null;
    #heartbeat = null;
    #listeners = new Map();
    #options;

    // url: the desktop WebSocket; open(): returns a transport instead (url is then unused); heartbeat: a cheap
    // method called regularly so forwarders keep an idle socket open (null disables it); subscribeMethod: the
    // long-lived request that delivers broadcast events.
    constructor({
        url = `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`,
        open = () => connectWebSocket(url),
        heartbeat = {method: 'app.ping', intervalMs: 20000},
        subscribeMethod = 'events.subscribe',
    } = {}) {
        this.#options = {open, heartbeat, subscribeMethod};
    }

    // Opens the socket. onClosed runs when it drops; onReconnected after a later attempt succeeds.
    async connect({onClosed = () => {}, onReconnected = async () => {}} = {}) {
        const watch = transport => transport.closed.then(async () => {
            clearInterval(this.#heartbeat);
            this.#channel = null;
            onClosed();
            for (let delay = 1000; ; delay = Math.min(delay * 2, 10000)) {
                await new Promise(resolve => setTimeout(resolve, delay));
                try {
                    watch(await this.#open());
                    await onReconnected();
                    return;
                } catch {}
            }
        });
        watch(await this.#open());
    }

    call(method, payload = {}, options = {}) {
        if (!this.#channel) return Promise.reject(new Error('not connected; reconnecting'));
        return this.#channel.request(method, payload, options);
    }

    // Starts receiving broadcast events; the server may replay an operation that is already in progress.
    subscribe() {
        this.call(this.#options.subscribeMethod, {}, {
            onEvent: event => {
                for (const listener of this.#listeners.get(event.type) ?? []) listener(event.payload);
            },
        }).catch(() => {});
    }

    on(type, listener) {
        if (!this.#listeners.has(type)) this.#listeners.set(type, []);
        this.#listeners.get(type).push(listener);
    }

    async #open() {
        const transport = await this.#options.open();
        this.#channel = new RequestChannel(transport);
        const heartbeat = this.#options.heartbeat;
        clearInterval(this.#heartbeat);
        if (heartbeat)
            this.#heartbeat = setInterval(() => this.#channel?.request(heartbeat.method, {}).catch(() => {}),
                heartbeat.intervalMs);
        return transport;
    }
}
