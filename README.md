# liteapp

[![LiteApp CI](https://github.com/thammegowda/liteapp/actions/workflows/ci.yml/badge.svg)](https://github.com/thammegowda/liteapp/actions/workflows/ci.yml)

`liteapp` is a small C++23 SDK for applications that use a local HTTP backend and an HTML/CSS/JavaScript frontend. The directory is intentionally self-contained so it can be copied into another repository.

## Layout

```text
liteapp/
  cmake/          compiler hardening and toolchains
  libs/           pinned third-party dependencies
  src/liteapp/    SDK code; paths match namespaces
  tests/          SDK security and behavior tests
```

Applications include headers such as `liteapp/http/server.h` and link `liteapp::core`, `liteapp::db`, `liteapp::fs`, `liteapp::http`, `liteapp::platform`, or `liteapp::vfs`.

Native desktop applications can link `liteapp::app` and call
`run_desktop_application()` after registering their routes. LiteApp owns
bind/start/readiness, optional WebView or browser launch, profile paths, and
server shutdown/wait behavior.

When copying `liteapp/` to another repository, copy an initialized working tree so `libs/json` and `libs/googletest` contain their sources. The new repository may then vendor those populated directories or register the same upstream URLs and commits as submodules.

## Browser application runtime

`web/liteapp.mjs` is an optional, dependency-free browser runtime for
applications that communicate with services through Workers or MessagePorts.
It is independent of the native C++ libraries and does not assume an
application domain, schema, user interface, storage engine, or inference
runtime.

The browser runtime provides:

- versioned request, event, and cancellation envelopes;
- request IDs and strict per-request event sequencing;
- `AbortSignal` cancellation;
- structured remote errors;
- transferable binary payloads;
- a generic service router; and
- transports for browser Workers, MessagePorts, and WebSockets; and
- a host-variant client for desktop, Wasm, and WebGPU backends.

Applications register their own method names, validate their own payloads, and
own all business behavior:

```js
import {
    MessageTransport,
    RequestChannel,
    ServiceRouter,
} from './liteapp/web/liteapp.mjs';

const ports = new MessageChannel();
const client = new RequestChannel(new MessageTransport(ports.port1));
const services = new ServiceRouter(new MessageTransport(ports.port2));

services.register('documents.wordCount', ({payload, emit}) => {
    emit('progress', {stage: 'counting'});
    return {words: payload.text.trim().split(/\s+/).length};
});

const result = await client.request(
    'documents.wordCount',
    {text: 'A reusable application service'},
    {onEvent: event => console.log(event.type, event.payload)},
);
```

`MessageTransport` can wrap a `Worker`, `DedicatedWorkerGlobalScope`, or
`MessagePort`. Construct it with `{owned: true}` only when disposing the
transport should also terminate or close the wrapped endpoint. UI, storage,
media, and static/native packaging remain separate optional modules; importing
the channel runtime does not pull them in.

`WebSocketTransport` implements the same transport contract for native desktop
services. `createVariantClient()` selects a desktop WebSocket or a Wasm/WebGPU
Worker while returning the same `RequestChannel` interface. See
[`examples/dual-host`](examples/dual-host/) for a minimal shared-UI skeleton.

Native services can link `liteapp::service` and register handlers with the C++
`ServiceRouter`. It uses the same request/event/cancellation envelopes as the
JavaScript `RequestChannel`, runs bounded concurrent handlers, preserves
per-request event ordering, propagates structured errors, and can carry binary
frames beside JSON services.

## Safety Baseline

- C++23, RAII, value types, and explicit `std::expected` results.
- No raw owning pointers, raw allocation, C string manipulation, or shell command construction.
- OS CSPRNGs for capabilities: BCrypt, Security.framework, or `getrandom`.
- Strict warnings as errors, control-flow/stack hardening, and optional ASan/UBSan.
- A one-time launch query token exchanged for a required HttpOnly capability cookie.
- Authenticated same-origin WebSockets with independent text/binary frame limits.
- Host/Origin validation, CSP, cache prevention, and loopback-only local mode.
- Liteapp-owned recursive filesystem events; symlink/reparse traversal is disabled.
- cpp-httplib remains private behind Liteapp request/response types.

## Build

Initialize submodules from the containing repository, then:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Use the `sanitize` preset on Linux/macOS to run with AddressSanitizer and UndefinedBehaviorSanitizer.

The `LiteApp CI` workflow runs the complete C++ and browser suites on Linux,
macOS, and Windows, repeats the C++ suite with ASan/UBSan on Linux, builds the
self-contained dual-host skeleton, and uploads one runnable skeleton artifact
per native platform.

Apps may opt into `LITEAPP_PORTABLE_RUNTIME` and call `liteapp_portable_runtime(target)` for executable packaging. Linux statically bundles the C++ and compiler runtimes while retaining the system glibc loader and libraries for security updates; a post-link allowlist rejects other shared dependencies. Windows uses the static MinGW runtime. Unsupported platforms fail configuration instead of silently producing a partially portable binary.

## Database

Apps own their schema and migrations; Liteapp owns connection, transaction, binding, and row lifetimes:

```cpp
constexpr std::array schema{
  liteapp::db::Migration{1, "CREATE TABLE notes(id INTEGER PRIMARY KEY, body TEXT NOT NULL);"},
  liteapp::db::Migration{2, "CREATE INDEX notes_body ON notes(body);"},
};

auto database = liteapp::db::Database::open("app.db");
database->migrate(schema);
database->execute("INSERT INTO notes(body) VALUES (?)", {std::string{"hello"}});

for (const auto& result : database->query("SELECT id, body FROM notes")) {
  if (!result) continue;
  const auto& row = *result;
}
```

`query()` returns `Generator<Result<Row>>`: statements advance only when iterated, and preparation/step failures appear as yielded errors. Supported values are null, 64-bit integer, double, UTF-8 text, and blob. Arbitrary SQL, FTS5, app-declared versioned migrations, and RAII transactions are supported without exposing SQLite handles.

The bundled SQLite database is plaintext. Liteapp builds with in-memory temporary storage, secure deletion, foreign keys, disabled extension loading, and FTS5, but those settings are not encryption. SQLCipher is a future replaceable backend; applications requiring encrypted data must also define OS-backed key storage and recovery.

## Embedded assets

`liteapp_embed_archive()` creates a deterministic ZIP from an explicit file list
and links it into a target as a native binary resource. macOS and ELF platforms
use assembler `.incbin`; Windows uses a target-named `RCDATA` resource. The C++
compiler sees only a small generated `std::span` accessor and never parses asset
byte initializers. Apps open those bytes with
`liteapp::vfs::FileSystem::from_zip()` and mount the immutable filesystem through
`HttpServer::serve_static()`. ZIP contents are validated and expanded into
shared memory on the first request; nothing is extracted to disk.

Development servers can use `HttpServer::serve_manifest(root, manifest)` to
mount an explicit newline-separated asset list. Routes are validated and MIME
types are inferred by LiteApp. `index.html` maps to the route prefix root.
`serve_files()` also infers MIME types whenever `StaticFile::content_type` is
left empty.

## WebSockets

`HttpServer::websocket()` registers a same-port WebSocket endpoint. Local-access
Host, Origin, and capability-cookie checks run before the upgrade. The handler
receives a `liteapp::WebSocket` with typed text/binary reads, serialized sends,
configured message-size limits, and explicit close handling. Application
protocols remain responsible for request IDs, sequencing, bounded work queues,
and cancellation.

`FileSystem::read()` returns immutable shared bytes plus normalized path, leaf name, extension, media type, optional text encoding, and byte size. `FileSystem::stat()` returns the same metadata without exposing archive-library types. Binary images, fonts, WebAssembly, and other raw assets use the same API as text resources.

```cmake
liteapp_embed_archive(
  TARGET example_web
  NAMESPACE example::embedded_web
  HEADER example/embedded_web.h
  ROOT "${CMAKE_CURRENT_SOURCE_DIR}/web"
  FILES app.js index.html styles.css
)
```

```cpp
auto assets = liteapp::vfs::FileSystem::from_zip(example::embedded_web::archive());
server.serve_file(R"(/project/thread/[1-9][0-9]*)", *assets, "index.html");
server.serve_static(*assets);
```

The VFS rejects absolute, parent-relative, duplicate, encrypted, unsupported, oversized, and malformed entries before exposing content. Apps can lower the default archive, file-count, file-size, expanded-size, and path-length limits.

## Filesystem events

`liteapp::fs::Watcher` watches one or more directory trees recursively and reports normalized add, remove, modify, move, or rescan events. Liteapp owns its inotify, IOCP, and FSEvents backends. The first version intentionally has no polling fallback; applications can remain usable through explicit reload or refresh behavior if native watcher setup fails.

Liteapp is licensed under the MIT License; see `LICENSE`. Third-party licenses remain beside their dependencies under `libs/`.