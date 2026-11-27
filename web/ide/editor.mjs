// Tabbed CodeMirror editors for workspace files, with save conflict detection, side-by-side diffs and inline
// (unified) diffs. Talks to the fs.read / fs.write / git.diff service methods of liteapp::workspace.
import * as cm from '../libs/codemirror/codemirror.mjs';

function language(path) {
    const name = path.split('/').pop().toLowerCase();
    const extension = name.includes('.') ? name.split('.').pop() : name;
    const stream = mode => () => cm.StreamLanguage.define(mode);
    const modes = {
        js: () => cm.javascript(), mjs: () => cm.javascript(), cjs: () => cm.javascript(),
        jsx: () => cm.javascript({jsx: true}), ts: () => cm.javascript({typescript: true}),
        tsx: () => cm.javascript({jsx: true, typescript: true}), py: () => cm.python(), json: () => cm.json(),
        c: () => cm.cpp(), h: () => cm.cpp(), cc: () => cm.cpp(), cpp: () => cm.cpp(), hpp: () => cm.cpp(),
        cu: () => cm.cpp(), cuh: () => cm.cpp(), mm: () => cm.cpp(), m: () => cm.cpp(), md: () => cm.markdown(),
        html: () => cm.html(), htm: () => cm.html(), css: () => cm.css(), yaml: () => cm.yaml(),
        yml: () => cm.yaml(), rs: () => cm.rust(), go: () => cm.go(), sh: stream(cm.shell), bash: stream(cm.shell),
        toml: stream(cm.toml), cmake: stream(cm.cmake), 'cmakelists.txt': stream(cm.cmake),
    };
    return (modes[name] ?? modes[extension])?.() ?? [];
}

export class Editor {
    #active = null;
    #area;
    #call;
    #loadDiff;
    #onReveal;
    #pendingThemes = [];
    #tabs = [];
    #tabsElement;
    #theme;
    #themed = new Set();  // every editor view's theme compartment
    #welcome;

    // call(method, payload): the service call function (e.g. Connection#call).
    // tabs, area: the tab strip and the element that hosts editor panes; welcome: shown when no tab is open.
    // theme: a Theme. onReveal(): called before showing a file or diff, e.g. to switch to a layout that shows the
    // editor. loadDiff(path, base): the {old, new, label} to compare for bases other than "disk" and "git".
    constructor({call, tabs, area, welcome = null, theme, onReveal = () => {}, loadDiff = null}) {
        this.#call = call;
        this.#tabsElement = tabs;
        this.#area = area;
        this.#welcome = welcome;
        this.#theme = theme;
        this.#onReveal = onReveal;
        this.#loadDiff = loadDiff;
        theme.onChange(dark => {
            for (const entry of this.#themed) {
                if (!entry.view.dom.isConnected) {
                    this.#themed.delete(entry);
                    continue;
                }
                entry.view.dispatch({effects: entry.compartment.reconfigure(dark ? cm.oneDark : [])});
            }
        });
        addEventListener('beforeunload', event => { if (this.hasUnsavedChanges()) event.preventDefault(); });
        addEventListener('keydown', event => {
            // A focused editor handles Mod-s itself; this covers diff tabs and focus elsewhere on the page.
            if ((event.ctrlKey || event.metaKey) && event.key === 's' && !event.defaultPrevented) {
                event.preventDefault();
                this.save();
            }
        });
    }

    hasUnsavedChanges() {
        return this.#tabs.some(tab => this.#dirty(tab));
    }

    async openFile(path) {
        this.#onReveal();
        const existing = this.#tabs.find(tab => tab.kind === 'file' && tab.path === path);
        if (existing) return this.#show(existing);
        const tab = {kind: 'file', path, saved: '', modified: 0};
        this.#tabs.push(tab);
        const element = this.#panel(tab);
        const body = document.createElement('div');
        body.className = 'editor-body';
        element.append(body);
        this.#show(tab);
        try {
            const file = await this.#call('fs.read', {path});
            tab.saved = file.content;
            tab.modified = file.modified_ns;
            tab.view = this.#track(new cm.EditorView({
                parent: body,
                state: cm.EditorState.create({
                    doc: file.content,
                    extensions: [
                        this.#extensions(path),
                        cm.keymap.of([{
                            key: 'Mod-s', preventDefault: true, run: () => { this.save(tab); return true; },
                        }]),
                        cm.EditorView.updateListener.of(update => { if (update.docChanged) this.#render(); }),
                    ],
                }),
            }));
            tab.view.focus();
        } catch (error) {
            body.textContent = error.code === 'binary' ? 'This is a binary file.' : `Cannot open: ${error.message}`;
            body.className = 'editor-body empty';
        }
        this.#render();
    }

    async save(tab = this.#active, overwrite = false) {
        if (!tab || tab.kind !== 'file' || !tab.view) return;
        const content = tab.view.state.doc.toString();
        try {
            const result = await this.#call('fs.write',
                {path: tab.path, content, modified_ns: overwrite ? undefined : tab.modified});
            tab.saved = content;
            tab.modified = result.modified_ns;
            this.#message(tab, '');
            this.#render();
        } catch (error) {
            if (error.code !== 'conflict') return this.#message(tab, `Save failed: ${error.message}`);
            this.#message(tab, 'Changed on disk since you opened it.', [
                ['Compare', () => this.openDiff(tab.path, 'disk', content)],
                ['Overwrite', () => this.save(tab, true)],
                ['Reload', () => this.#reload(tab, true)],
            ]);
        }
    }

    // Reloads open files that something else changed; null means anything may have changed.
    filesChanged(paths) {
        for (const tab of this.#tabs)
            if (tab.kind === 'file' && (!paths || paths.includes(tab.path))) this.#reload(tab);
    }

    // Compares old and new text side by side. base "disk" compares the saved file with `mine` (unsaved text);
    // "git" compares HEAD with the working tree; anything else goes to loadDiff.
    async openDiff(path, base, mine) {
        let diff;
        if (base === 'disk') {
            const file = await this.#call('fs.read', {path});
            diff = {old: file.content, new: mine, label: 'disk ↔ your edits'};
        } else if (base === 'git' || !this.#loadDiff) {
            const result = await this.#call('git.diff', {path});
            diff = {old: result.old, new: result.new, label: 'HEAD ↔ working tree'};
        } else {
            diff = await this.#loadDiff(path, base);
        }
        this.showDiff(path, diff);
    }

    showDiff(path, diff, {title} = {}) {
        this.#onReveal();
        const key = `${path}:${diff.label}`;
        const existing = this.#tabs.find(tab => tab.kind === 'diff' && tab.key === key);
        if (existing) this.#close(existing);
        const tab = {kind: 'diff', key, path};
        this.#tabs.push(tab);
        const note = document.createElement('span');
        note.className = 'pane-label';
        note.textContent = title ?? diff.label;
        const element = this.#panel(tab, note);
        const open = document.createElement('button');
        open.className = 'ghost small';
        open.textContent = 'Open file';
        open.addEventListener('click', () => this.openFile(path));
        tab.header.append(open);
        const body = document.createElement('div');
        body.className = 'editor-body merge';
        element.append(body);
        tab.merge = new cm.MergeView({
            parent: body,
            a: {doc: diff.old, extensions: this.#extensions(path, {readOnly: true})},
            b: {doc: diff.new, extensions: this.#extensions(path, {readOnly: true})},
            collapseUnchanged: {margin: 3, minSize: 6},
            gutter: true,
        });
        const [left, right] = this.#pendingThemes.splice(0);
        this.#themed.add({compartment: left, view: tab.merge.a});
        this.#themed.add({compartment: right, view: tab.merge.b});
        this.#show(tab);
    }

    // A read-only inline diff (deletions above insertions) inside `parent`, e.g. for an approval prompt.
    unifiedDiff(parent, path, oldText, newText) {
        return this.#track(new cm.EditorView({
            parent,
            state: cm.EditorState.create({
                doc: newText,
                extensions: [this.#extensions(path, {readOnly: true}),
                    cm.unifiedMergeView({
                        original: oldText, mergeControls: false, collapseUnchanged: {margin: 2, minSize: 4},
                    })],
            }),
        }));
    }

    #extensions(path, {readOnly = false} = {}) {
        // Each view gets its own theme compartment so a theme switch can restyle it in place.
        const compartment = new cm.Compartment();
        this.#pendingThemes.push(compartment);
        return [
            cm.lineNumbers(), cm.highlightActiveLineGutter(), cm.highlightSpecialChars(), cm.history(),
            cm.foldGutter(), cm.drawSelection(), cm.dropCursor(), cm.EditorState.allowMultipleSelections.of(true),
            cm.indentOnInput(), cm.syntaxHighlighting(cm.defaultHighlightStyle, {fallback: true}),
            cm.bracketMatching(), cm.closeBrackets(), cm.autocompletion(), cm.rectangularSelection(),
            cm.crosshairCursor(), cm.highlightActiveLine(), cm.highlightSelectionMatches(),
            cm.keymap.of([...cm.closeBracketsKeymap, ...cm.defaultKeymap, ...cm.searchKeymap, ...cm.historyKeymap,
                ...cm.foldKeymap, ...cm.completionKeymap, cm.indentWithTab]),
            language(path), compartment.of(this.#theme.dark ? cm.oneDark : []), cm.EditorState.readOnly.of(readOnly),
            cm.EditorView.theme({'&': {height: '100%'}, '.cm-scroller': {fontFamily: 'var(--mono)'}}),
        ];
    }

    #track(view) {
        for (const compartment of this.#pendingThemes) this.#themed.add({compartment, view});
        this.#pendingThemes = [];
        return view;
    }

    #dirty(tab) {
        return tab.kind === 'file' && tab.view && tab.view.state.doc.toString() !== tab.saved;
    }

    #render() {
        this.#tabsElement.replaceChildren(...this.#tabs.map(tab => {
            const element = document.createElement('div');
            element.className = 'tab' + (tab === this.#active ? ' active' : '') + (tab.kind === 'diff' ? ' diff' : '');
            element.title = tab.path;
            const label = document.createElement('span');
            label.textContent = (tab.kind === 'diff' ? 'Δ ' : '') + tab.path.split('/').pop();
            const dirty = this.#dirty(tab);
            const state = document.createElement('button');
            state.className = 'tab-close' + (dirty ? ' dirty' : '');
            state.title = 'Close';
            state.textContent = dirty ? '●' : '×';
            state.addEventListener('click', event => { event.stopPropagation(); this.#close(tab); });
            element.append(label, state);
            element.addEventListener('click', () => this.#show(tab));
            element.addEventListener('auxclick', event => { if (event.button === 1) this.#close(tab); });
            return element;
        }));
        if (this.#welcome) this.#welcome.hidden = this.#tabs.length > 0;
    }

    #show(tab) {
        this.#active = tab;
        for (const other of this.#tabs) other.element.hidden = other !== tab;
        this.#render();
        tab.view?.focus?.();
    }

    #close(tab) {
        if (this.#dirty(tab) && !confirm(`Discard unsaved changes to ${tab.path}?`)) return;
        const index = this.#tabs.indexOf(tab);
        this.#tabs.splice(index, 1);
        tab.view?.destroy();
        tab.merge?.destroy();
        tab.element.remove();
        if (this.#active === tab) this.#active = null;
        if (!this.#active && this.#tabs.length) this.#show(this.#tabs[Math.max(0, index - 1)]);
        this.#render();
    }

    #panel(tab, banner) {
        const element = document.createElement('div');
        element.className = 'editor-pane';
        const header = document.createElement('div');
        header.className = 'pane-header';
        const path = document.createElement('span');
        path.className = 'pane-path';
        path.textContent = tab.path;
        header.append(path, banner ?? '');
        element.append(header);
        this.#area.append(element);
        tab.element = element;
        tab.header = header;
        return element;
    }

    #message(tab, text, actions = []) {
        tab.header.querySelector('.pane-message')?.remove();
        if (!text) return;
        const note = document.createElement('span');
        note.className = 'pane-message';
        note.textContent = text;
        for (const [label, action] of actions) {
            const button = document.createElement('button');
            button.className = 'ghost small';
            button.textContent = label;
            button.addEventListener('click', action);
            note.append(button);
        }
        tab.header.append(note);
    }

    async #reload(tab, force = false) {
        if (!tab.view) return;
        if (this.#dirty(tab) && !force) {
            this.#message(tab, 'Changed on disk.',
                [['Reload', () => this.#reload(tab, true)], ['Keep mine', () => this.#message(tab, '')]]);
            return;
        }
        try {
            const file = await this.#call('fs.read', {path: tab.path});
            if (file.content !== tab.view.state.doc.toString())
                tab.view.dispatch({changes: {from: 0, to: tab.view.state.doc.length, insert: file.content}});
            tab.saved = file.content;
            tab.modified = file.modified_ns;
            this.#message(tab, '');
        } catch {
            this.#message(tab, 'The file was deleted or moved.');
        }
        this.#render();
    }
}
