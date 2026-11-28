// Sanitized Markdown for untrusted text such as model output: GitHub-flavored Markdown, syntax highlighting, LaTeX
// math rendered to native MathML (Temml), copy buttons on code, scrollable wide blocks, optional diagrams, and a
// streaming mode that never shows half a code block or formula. Pair with markdown.css.
import {marked} from '../libs/marked/marked.esm.js';
import DOMPurify from '../libs/dompurify/purify.es.mjs';
import hljs from '../libs/highlightjs/highlight.min.js';
import temml from '../libs/temml/temml.mjs';

const TAGS = [
    'p', 'br', 'strong', 'em', 'del', 'blockquote', 'ul', 'ol', 'li', 'input', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6',
    'pre', 'code', 'hr', 'a', 'table', 'thead', 'tbody', 'tr', 'th', 'td', 'span', 'div',
];
const ATTRIBUTES = ['href', 'title', 'start', 'align', 'class', 'type', 'checked', 'disabled'];

// Math delimiters: $$…$$ and \[…\] display, $…$ and \(…\) inline. A dollar sign opens inline math only before a
// non-space character and closes after one, not followed by a digit, so prices like "$5 and $10" stay text.
const DISPLAY = /^(?:\$\$([\s\S]+?)\$\$|\\\[([\s\S]+?)\\\])/;
const INLINE = /^(?:\$(?!\s)((?:\\.|[^\\$\n])+?)(?<!\s)\$(?!\d)|\\\(([\s\S]+?)\\\))/;

const escapeHtml = text => text.replace(/[&<>"]/g, character => `&#${character.charCodeAt(0)};`);

marked.use({
    extensions: [
        {
            name: 'mathBlock',
            level: 'block',
            start: source => source.match(/\$\$|\\\[/)?.index,
            tokenizer(source) {
                const match = /^ {0,3}(?:\$\$([\s\S]+?)\$\$|\\\[([\s\S]+?)\\\])[ \t]*(?:\n|$)/.exec(source);
                if (match) return {type: 'mathBlock', raw: match[0], text: (match[1] ?? match[2]).trim()};
            },
            renderer: token => `<div class="math-display">${escapeHtml(token.text)}</div>\n`,
        },
        {
            name: 'mathInline',
            level: 'inline',
            start: source => source.match(/\$|\\[([]/)?.index,
            tokenizer(source) {
                const display = DISPLAY.exec(source);
                if (display)
                    return {type: 'mathInline', raw: display[0], text: (display[1] ?? display[2]).trim(), display: true};
                const inline = INLINE.exec(source);
                if (inline) return {type: 'mathInline', raw: inline[0], text: inline[1] ?? inline[2], display: false};
            },
            renderer: token =>
                `<span class="${token.display ? 'math-display' : 'math-inline'}">${escapeHtml(token.text)}</span>`,
        },
    ],
});

// Where an unfinished fenced code block or display formula starts in `source`; streaming holds it back as raw text.
export function unfinishedBlock(source) {
    let offset = 0;
    let fence = null;
    let math = null;
    for (const line of source.split(/(?<=\n)/)) {
        const marker = /^ {0,3}(`{3,}|~{3,})[^\n]*\n?$/.exec(line);
        if (marker) {
            if (!fence) fence = {delimiter: marker[1][0], length: marker[1].length, start: offset};
            else if (marker[1][0] === fence.delimiter && marker[1].length >= fence.length) fence = null;
        } else if (!fence) {
            for (const delimiter of line.matchAll(/\$\$|\\\[|\\\]/g)) {
                if (!math && delimiter[0] !== '\\]')
                    math = {close: delimiter[0] === '$$' ? '$$' : '\\]', start: offset + delimiter.index};
                else if (math && delimiter[0] === math.close) math = null;
            }
        }
        offset += line.length;
    }
    if (fence) return {kind: 'code', start: fence.start};
    return math ? {kind: 'math', start: math.start} : null;
}

function renderMath(fragment) {
    for (const node of fragment.querySelectorAll('.math-inline, .math-display')) {
        const tex = node.textContent;
        node.textContent = '';
        try {
            // Temml builds MathML elements directly; \href, \class and similar commands stay disabled (trust: false).
            temml.render(tex, node, {displayMode: node.classList.contains('math-display'), throwOnError: false});
        } catch {
            node.textContent = tex;
            node.classList.add('math-error');
        }
        node.title = tex;
    }
}

function copyButton(code) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'copy-code';
    button.textContent = 'Copy';
    button.addEventListener('click', () => {
        navigator.clipboard?.writeText(code.textContent);
        button.textContent = 'Copied';
        setTimeout(() => { button.textContent = 'Copy'; }, 1200);
    });
    return button;
}

function scrollable(block) {
    const scroll = document.createElement('div');
    scroll.className = 'markdown-scroll';
    scroll.tabIndex = 0;
    scroll.setAttribute('role', 'region');
    scroll.setAttribute('aria-label', block.tagName === 'PRE' ? 'Code block' : 'Table');
    block.replaceWith(scroll);
    scroll.append(block);
}

// Replaces a code block with its rendered diagram, keeping the source in a collapsed <details>.
function diagram(code, language, renderDiagram, onChange) {
    const rendered = renderDiagram(language, code.textContent);
    if (!rendered) return;
    const block = code.closest('.markdown-scroll') ?? code.parentElement;
    const details = document.createElement('details');
    details.className = 'diagram-source';
    details.open = true;
    const summary = document.createElement('summary');
    summary.textContent = 'Diagram source';
    block.replaceWith(details);
    details.append(summary, block);
    details.addEventListener('toggle', () => onChange?.());
    Promise.resolve(rendered).then(node => {
        if (!details.isConnected) return;
        details.before(node);
        details.open = false;
        onChange?.();
    }).catch(() => { summary.textContent = 'Diagram unavailable - view source'; });
}

// Renders `source` into `element`. Options:
//   breaks: treat single newlines as line breaks (default false)
//   copyButtons: add a Copy button to code blocks (default true)
//   renderDiagram(language, source): a Node or Promise<Node> for a diagram code block (for example Mermaid), or null
//     to keep it as code; not called while streaming
//   onChange(): called when an asynchronous diagram changes the layout
export function renderMarkdown(element, source, {
    breaks = false, copyButtons = true, renderDiagram = null, onChange = null, streaming = false,
} = {}) {
    const fragment = DOMPurify.sanitize(marked.parse(source, {gfm: true, breaks}), {
        ALLOWED_TAGS: TAGS,
        ALLOWED_ATTR: ATTRIBUTES,
        ALLOW_DATA_ATTR: false,
        ALLOW_ARIA_ATTR: false,
        RETURN_DOM_FRAGMENT: true,
    });
    for (const link of fragment.querySelectorAll('a')) {
        const href = link.getAttribute('href');
        if (!href || !/^(https?:|mailto:)/i.test(href)) {
            link.removeAttribute('href');
            continue;
        }
        link.target = '_blank';
        link.rel = 'noopener noreferrer';
    }
    renderMath(fragment);
    const diagrams = [];
    for (const code of fragment.querySelectorAll('pre > code')) {
        const language = /(?:^|\s)language-([\w+-]+)/.exec(code.className)?.[1]?.toLowerCase();
        code.removeAttribute('class');
        if (language && renderDiagram && !streaming) diagrams.push([code, language]);
        if (language && hljs.getLanguage(language)) {
            const highlighted = hljs.highlight(code.textContent, {language, ignoreIllegals: true}).value;
            code.innerHTML = DOMPurify.sanitize(highlighted, {ALLOWED_TAGS: ['span'], ALLOWED_ATTR: ['class']});
            code.className = 'hljs';
        }
        if (copyButtons) code.parentElement.append(copyButton(code));
    }
    for (const block of fragment.querySelectorAll('pre, table')) scrollable(block);
    element.replaceChildren(fragment);
    for (const [code, language] of diagrams) diagram(code, language, renderDiagram, onChange);
}

const streams = new WeakMap();

// Renders text that is still arriving: completed lines as Markdown, the line being written as plain text, and an open
// code block or display formula as raw text until it closes. Call it with the whole growing source each time.
export function renderStreamingMarkdown(element, source, options = {}) {
    let state = streams.get(element);
    if (!state) {
        const stable = document.createElement('div');
        stable.className = 'markdown-stream-stable';
        const tail = document.createElement('span');
        tail.className = 'markdown-stream-tail';
        element.replaceChildren(stable, tail);
        state = {stable, tail, rendered: null, mode: 'text'};
        streams.set(element, state);
    }
    const completed = source.lastIndexOf('\n') + 1;
    let renderable = source.slice(0, completed);
    let tail = source.slice(completed);
    let mode = 'text';
    const open = unfinishedBlock(renderable);
    if (open) {
        tail = source.slice(open.start);
        renderable = renderable.slice(0, open.start);
        mode = open.kind;
    }
    if (state.rendered !== renderable) {
        renderMarkdown(state.stable, renderable, {...options, streaming: true});
        state.rendered = renderable;
    }
    if (state.mode !== mode) {
        state.tail.replaceChildren();
        state.tail.className = mode === 'code' ? 'markdown-stream-code' : 'markdown-stream-tail';
        if (mode === 'code') state.tail.append(document.createElement('pre'));
        state.mode = mode;
    }
    (mode === 'code' ? state.tail.firstChild : state.tail).textContent = tail;
}

// Drops the streaming state of `element`; the next renderMarkdown() or renderStreamingMarkdown() starts fresh.
export function endStreaming(element) {
    streams.delete(element);
}
