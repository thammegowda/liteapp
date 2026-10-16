# liteapp

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

When copying `liteapp/` to another repository, copy an initialized working tree so `libs/json` and `libs/googletest` contain their sources. The new repository may then vendor those populated directories or register the same upstream URLs and commits as submodules.

## Safety Baseline

- C++23, RAII, value types, and explicit `std::expected` results.
- No raw owning pointers, raw allocation, C string manipulation, or shell command construction.
- OS CSPRNGs for capabilities: BCrypt, Security.framework, or `getrandom`.
- Strict warnings as errors, control-flow/stack hardening, and optional ASan/UBSan.
- A one-time launch query token exchanged for a required HttpOnly capability cookie.
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

`liteapp_embed_archive()` creates a deterministic ZIP from an explicit file list and compiles it into a target. Apps open those bytes with `liteapp::vfs::FileSystem::from_zip()` and mount the immutable filesystem through `HttpServer::serve_static()`. ZIP contents are validated and expanded into shared memory on the first request; nothing is extracted to disk.

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