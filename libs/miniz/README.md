# miniz

Official miniz 3.1.2 amalgamation, downloaded from:

`https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip`

Archive SHA-256:

`f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a`

When upgrading, retain only these files from the release archive:

- `miniz.c`
- `miniz.h`
- `LICENSE`

Do not copy the examples, changelog, split source tree, or upstream build files. Liteapp compiles the amalgamation without stdio, time, compression, ZIP writing, or zlib-compatible APIs. It is used only to read embedded ZIP archives from memory.
