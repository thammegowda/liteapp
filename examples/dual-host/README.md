# LiteApp dual-host skeleton

This skeleton shows one shared browser UI running against three host variants:

- `desktop`: a native C++ HTTP/WebSocket application;
- `wasm`: a dedicated Worker where an application can load its Wasm module; and
- `webgpu`: a dedicated Worker with WebGPU capability detection.

All variants expose the same versioned LiteApp request/event protocol. Domain
methods, schemas, Wasm bindings, GPU kernels, and UI design belong to the
application.

Build the native skeleton from the LiteApp repository:

```sh
cmake -S examples/dual-host -B build/dual-host
cmake --build build/dual-host
build/dual-host/liteapp-dual-host
```

The desktop host opens the shared UI. Use the buttons to switch between
desktop, Wasm, and WebGPU without changing application code.

## Layout

```text
desktop/main.cpp       native HTTP/WebSocket host
web/app.mjs            shared UI controller
web/services.mjs       shared application service definitions
web/wasm-worker.mjs    Wasm worker host boundary
web/webgpu-worker.mjs  WebGPU worker host boundary
web/index.html         minimal shared UI
web/assets.txt         explicit development asset manifest
```

For a production app, embed `web/` with `liteapp_embed_archive()`, mount it
through `liteapp::vfs::FileSystem`, and replace the example echo/status methods
with application services.
