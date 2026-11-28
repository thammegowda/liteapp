// An xterm.js terminal panel connected to the terminal.open / terminal.input / terminal.resize service methods of
// liteapp::workspace. The shell starts when the panel is first shown and can be restarted after it exits.
import {Terminal} from '../libs/xterm/xterm.mjs';
import {FitAddon} from '../libs/xterm/addon-fit.mjs';

function decode(base64) {
    const bytes = atob(base64);
    const result = new Uint8Array(bytes.length);
    for (let index = 0; index < bytes.length; ++index) result[index] = bytes.charCodeAt(index);
    return result;
}

// xterm.js needs concrete colors, so they are read from the --terminal-bg, --fg and --selection-bg variables.
function colors() {
    const style = getComputedStyle(document.documentElement);
    const value = name => style.getPropertyValue(name).trim();
    return {background: value('--terminal-bg'), foreground: value('--fg'), cursor: value('--fg'),
        selectionBackground: value('--selection-bg')};
}

export class TerminalPanel {
    #call;
    #fit = null;
    #host;
    #onReveal;
    #panel;
    #session = null;  // {id, abort}
    #splitter;
    #terminal = null;

    // panel: the element shown and hidden; host: where xterm renders; splitter: an optional resize handle shown
    // with the panel; theme: a Theme; onReveal(): called before the panel is shown.
    constructor({call, panel, host, splitter = null, theme, onReveal = () => {}}) {
        this.#call = call;
        this.#panel = panel;
        this.#host = host;
        this.#splitter = splitter;
        this.#onReveal = onReveal;
        theme.onChange(() => { if (this.#terminal) this.#terminal.options.theme = colors(); });
    }

    toggle(show = this.#panel.hidden) {
        if (show) this.#onReveal();
        this.#panel.hidden = !show;
        if (this.#splitter) this.#splitter.hidden = !show;
        if (!show) return;
        if (!this.#terminal) this.#create();
        if (!this.#session) this.#start();
        requestAnimationFrame(() => {
            this.#fit.fit();
            this.#terminal.focus();
        });
    }

    restart() {
        this.#session?.abort.abort();
        this.#session = null;
        this.#terminal?.reset();
        if (this.#terminal) this.#start();
    }

    #create() {
        const style = getComputedStyle(document.documentElement);
        this.#terminal = new Terminal({
            cursorBlink: true,
            fontFamily: style.getPropertyValue('--mono'),
            fontSize: 13,
            scrollback: 5000,
            theme: colors(),
        });
        this.#fit = new FitAddon();
        this.#terminal.loadAddon(this.#fit);
        this.#terminal.open(this.#host);
        this.#terminal.onData(data => {
            if (this.#session?.id) this.#call('terminal.input', {id: this.#session.id, data}).catch(() => {});
        });
        this.#terminal.onResize(({cols, rows}) => {
            if (this.#session?.id) this.#call('terminal.resize', {id: this.#session.id, cols, rows}).catch(() => {});
        });
        new ResizeObserver(() => { if (!this.#panel.hidden) this.#fit.fit(); }).observe(this.#host);
    }

    #start() {
        const current = {id: null, abort: new AbortController()};
        this.#session = current;
        this.#fit.fit();
        this.#call('terminal.open', {cols: this.#terminal.cols, rows: this.#terminal.rows}, {
            signal: current.abort.signal,
            onEvent: event => {
                if (event.type === 'opened') current.id = event.payload.id;
                if (event.type === 'output') this.#terminal.write(decode(event.payload.data));
            },
        }).then(result => {
            if (this.#session !== current) return;
            this.#terminal.write(`\r\n\x1b[2m[process exited with code ${result.exit_code}; press Restart]\x1b[0m\r\n`);
            this.#session = null;
        }).catch(() => {});
    }
}
