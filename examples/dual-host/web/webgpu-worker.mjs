import {MessageTransport, ServiceRouter} from './liteapp/liteapp.mjs';
import {registerExampleServices} from './services.mjs';

const router = new ServiceRouter(new MessageTransport(self));
registerExampleServices(router, 'webgpu', {
    wasm: true,
    webgpu: Boolean(navigator.gpu),
});
