// A lazily expanded workspace file tree over fs.list, with optional per-path status badges (for example Git
// changes), and a grouped change list to pair with it.

function element(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
}

// A colored one-letter badge for a change status: modified, renamed, added, untracked or deleted.
export function statusBadge(status) {
    return element('span', `tree-badge ${status}`, status[0].toUpperCase());
}

export class FileTree {
    #call;
    #decorations = new Map();
    #element;
    #expanded = new Set(['']);
    #onOpen;

    // call(method, payload): the service call function; element: the tree container; onOpen(path): a file was
    // clicked.
    constructor({call, element: container, onOpen}) {
        this.#call = call;
        this.#element = container;
        this.#onOpen = onOpen;
        container.setAttribute('role', 'tree');
    }

    async refresh() {
        const scroll = this.#element.scrollTop;
        this.#element.replaceChildren(await this.#directory('', 0));
        this.#element.scrollTop = scroll;
    }

    // Badges shown next to paths; a Map from path to status. Takes effect on the next refresh().
    setDecorations(decorations) {
        this.#decorations = decorations;
    }

    async #directory(path, depth) {
        const group = element('div', 'tree-group');
        try {
            const {entries} = await this.#call('fs.list', {path});
            for (const entry of entries) group.append(...await this.#row(path, entry, depth));
        } catch (error) {
            group.append(element('div', 'tree-error', error.message));
        }
        return group;
    }

    async #row(parent, entry, depth) {
        const path = parent ? `${parent}/${entry.name}` : entry.name;
        const row = element('div', `tree-row ${entry.kind}`);
        row.style.paddingLeft = `${8 + depth * 14}px`;
        row.setAttribute('role', 'treeitem');
        row.dataset.path = path;
        const directory = entry.kind === 'directory';
        const icon = element('span', 'tree-icon', directory ? (this.#expanded.has(path) ? '▾' : '▸') : '');
        row.append(icon, element('span', 'tree-name', entry.name));
        const status = this.#decorations.get(path);
        if (status) row.append(statusBadge(status));
        if (!directory) {
            if (entry.kind === 'file')
                row.addEventListener('click', () => {
                    for (const selected of this.#element.querySelectorAll('.tree-row.selected'))
                        selected.classList.remove('selected');
                    row.classList.add('selected');
                    this.#onOpen(path);
                });
            return [row];
        }
        const children = element('div', 'tree-children');
        if (this.#expanded.has(path)) children.append(await this.#directory(path, depth + 1));
        row.addEventListener('click', async () => {
            if (this.#expanded.has(path)) {
                this.#expanded.delete(path);
                children.replaceChildren();
            } else {
                this.#expanded.add(path);
                children.replaceChildren(await this.#directory(path, depth + 1));
            }
            icon.textContent = this.#expanded.has(path) ? '▾' : '▸';
        });
        return [row, children];
    }
}

// Renders groups of changed files: [{title, empty, items: [{path, status}], onOpen(item)}].
export function renderChanges(container, groups) {
    container.replaceChildren(...groups.map(group => {
        const section = element('div', 'change-group');
        section.append(element('h3', '', group.title));
        if (!group.items.length) section.append(element('p', 'empty', group.empty ?? 'No changes.'));
        for (const item of group.items) {
            const row = element('div', 'change-row');
            row.append(statusBadge(item.status), element('span', 'change-path', item.path));
            row.title = `${item.status} — click to compare`;
            row.addEventListener('click', () => group.onOpen(item));
            section.append(row);
        }
        return section;
    }));
}
