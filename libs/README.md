# Liteapp Dependencies

| Dependency | Form | Purpose |
| --- | --- | --- |
| cpp-httplib | Vendored at `32abac3de5a1a5c57e0b06b4a8c261263c9e6499`, with upstream license | Local HTTP transport |
| miniz | Reader-only 3.1.2 source subset with verified release SHA-256 | Embedded ZIP assets |
| WebView2 | Minimal native x64 SDK subset at 1.0.4129.50 with verified NuGet SHA-256 | Windows embedded web window |
| SQLite | Official 3.53.4 amalgamation with published SHA3-256 | Embedded relational storage and FTS5 |
| nlohmann/json | Git submodule at `acf076a677c161643e64d8f83fd02624fd8c4bd3` | Structured JSON |
| GoogleTest | Git submodule at `985d542c73604f9d7d5ff3ba2a4b39a6c45bfbe0` | SDK and application tests |

Application code does not include cpp-httplib directly. The wrapper boundary under `src/liteapp/http` keeps transport replacement possible and limits third-party API exposure.