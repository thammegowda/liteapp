// Composes LiteApp's IDE modules into a small editor for one directory.
import {Connection} from './liteapp/host/connection.mjs';
import {Editor} from './liteapp/ide/editor.mjs';
import {FileTree, renderChanges} from './liteapp/ide/file-tree.mjs';
import {enableSplitters} from './liteapp/ide/splitters.mjs';
import {TerminalPanel} from './liteapp/ide/terminal.mjs';
import {Theme} from './liteapp/ide/theme.mjs';

const $ = id => document.getElementById(id);
const connection = new Connection();
const call = (method, payload, options) => connection.call(method, payload, options);
const theme = new Theme({storageKey: 'liteapp-ide-theme', button: $('theme')});
const editor = new Editor({call, tabs: $('editor-tabs'), area: $('editor-area'), welcome: $('welcome'), theme});
const tree = new FileTree({call, element: $('file-tree'), onOpen: path => editor.openFile(path)});
const terminal = new TerminalPanel({
    call, panel: $('terminal-panel'), host: $('terminal-host'), splitter: $('terminal-splitter'), theme,
});

function status(text) {
    $('status').textContent = text;
}

async function refreshChanges() {
    const {repository, changes} = await call('git.status');
    tree.setDecorations(new Map(changes.map(change => [change.path, change.status])));
    renderChanges($('changes-list'), [{
        title: 'Git working tree',
        empty: repository ? 'No uncommitted changes.' : 'Not a Git repository.',
        items: changes,
        onOpen: change => editor.openDiff(change.path, 'git').catch(error => status(error.message)),
    }]);
    $('changes-count').textContent = changes.length ? String(changes.length) : '';
}

async function refresh() {
    await refreshChanges().catch(error => status(error.message));
    await tree.refresh();
}

async function newFile() {
    const path = prompt('New file path (relative to the workspace):');
    if (!path) return;
    try {
        await call('fs.write', {path, content: '', modified_ns: 0});
        await tree.refresh();
        editor.openFile(path.replace(/^\.\//, ''));
    } catch (error) {
        status(error.code === 'conflict' ? `${path} already exists.` : error.message);
    }
}

for (const tab of document.querySelectorAll('.sidebar-tabs [role=tab]'))
    tab.addEventListener('click', () => {
        for (const other of document.querySelectorAll('.sidebar-tabs [role=tab]'))
            other.classList.toggle('active', other === tab);
        for (const panel of document.querySelectorAll('.sidebar .panel'))
            panel.classList.toggle('active', panel.dataset.panel === tab.dataset.panel);
        if (tab.dataset.panel === 'changes') refreshChanges().catch(error => status(error.message));
    });
$('refresh-files').addEventListener('click', refresh);
$('refresh-changes').addEventListener('click', refresh);
$('new-file').addEventListener('click', newFile);
$('toggle-terminal').addEventListener('click', () => terminal.toggle());
$('restart-terminal').addEventListener('click', () => terminal.restart());
$('close-terminal').addEventListener('click', () => terminal.toggle(false));
addEventListener('keydown', event => {
    if (event.ctrlKey && event.key === '`') {
        event.preventDefault();
        terminal.toggle();
    }
});
enableSplitters();

connection.on('files_changed', ({paths}) => {
    editor.filesChanged(paths);
    refresh();
});
await connection.connect({
    onClosed: () => { $('banner').textContent = 'Connection lost. Reconnecting…'; $('banner').hidden = false; },
    onReconnected: async () => { $('banner').hidden = true; connection.subscribe(); await refresh(); },
});
connection.subscribe();
const info = await call('app.info');
$('workspace-name').textContent = info.name;
document.title = `${info.name} — LiteApp IDE`;
await refresh();
