# LiteApp IDE example

A minimal local IDE for one directory, built from LiteApp's workspace services and `web/ide` components: a file
tree with Git status badges, tabbed CodeMirror editors with save-conflict handling and Git diffs, and an xterm.js
terminal. Dark, light and system themes; resizable panes.

```sh
cmake -S examples/ide -B build/ide
cmake --build build/ide
build/ide/liteapp-ide path/to/project
```

The host prints a one-time launch URL and opens it. Over SSH, or on Linux without a display, it only prints the
URL: forward the port and open the link in your local browser. `LITEAPP_IDE_NO_OPEN=1` always skips opening.

## Layout

```text
desktop/main.cpp   the native host: HTTP server, workspace services, event hub
web/app.mjs        composes Connection, FileTree, Editor, TerminalPanel, Theme and splitters
web/index.html     the page layout, using the class names in liteapp/ide/ide.css
web/style.css      this app's pane grid
web/assets.txt     every served file: the build embeds it, LITEAPP_IDE_SOURCE serves it from a checkout
```

LiteApp's browser modules are served under `liteapp/` with their source-tree layout, so relative imports between
them resolve. `LITEAPP_IDE_SOURCE=$PWD build/ide/liteapp-ide` (from the LiteApp checkout) serves the UI from disk,
so UI edits need only a page reload.
