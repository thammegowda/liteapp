import {createVariantClient} from './liteapp/liteapp.mjs';

const result = document.getElementById('result');
let client;

async function select(variant) {
    client?.close();
    client = null;
    result.textContent = `Connecting to ${variant}…`;
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    client = await createVariantClient({
        variant,
        desktopUrl: `${protocol}//${location.host}/services`,
        workerUrls: {
            wasm: new URL('./wasm-worker.mjs', import.meta.url),
            webgpu: new URL('./webgpu-worker.mjs', import.meta.url),
        },
    });
    const status = await client.channel.request('app.status', {});
    result.textContent = JSON.stringify(status, null, 2);
    for (const button of document.querySelectorAll('[data-variant]'))
        button.setAttribute('aria-current', String(button.dataset.variant === variant));
}

for (const button of document.querySelectorAll('[data-variant]'))
    button.addEventListener('click', () => select(button.dataset.variant).catch(error => {
        result.textContent = error.message;
    }));

document.getElementById('echo').addEventListener('submit', event => {
    event.preventDefault();
    if (!client) return;
    client.channel.request('app.echo', {value: document.getElementById('value').value}).then(
        value => { result.textContent = JSON.stringify(value, null, 2); },
        error => { result.textContent = error.message; },
    );
});
