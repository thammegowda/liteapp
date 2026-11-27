// Dark, light or system color theme, remembered per browser. Sets data-theme="dark"|"light" on <html>, which
// ide.css maps to its color variables, and notifies editors and terminals so they restyle in place.
const CHOICES = ['dark', 'light', 'system'];
const LABELS = {dark: '☾ Dark', light: '☀ Light', system: '◐ System'};

export class Theme {
    #button;
    #choice;
    #listeners = [];
    #storageKey;
    #system = matchMedia('(prefers-color-scheme: dark)');

    // button: optional element that shows the choice and cycles it when clicked.
    constructor({storageKey = 'liteapp-theme', button = null, initial = 'dark'} = {}) {
        this.#storageKey = storageKey;
        this.#button = button;
        const stored = localStorage.getItem(storageKey);
        this.#choice = CHOICES.includes(stored) ? stored : initial;
        this.#system.addEventListener('change', () => { if (this.#choice === 'system') this.#apply(); });
        button?.addEventListener('click', () => this.cycle());
        this.#apply();
    }

    get dark() {
        return this.#choice === 'dark' || (this.#choice === 'system' && this.#system.matches);
    }

    onChange(listener) {
        this.#listeners.push(listener);
    }

    cycle() {
        this.#choice = CHOICES[(CHOICES.indexOf(this.#choice) + 1) % CHOICES.length];
        localStorage.setItem(this.#storageKey, this.#choice);
        this.#apply();
    }

    #apply() {
        document.documentElement.dataset.theme = this.dark ? 'dark' : 'light';
        if (this.#button) {
            this.#button.textContent = LABELS[this.#choice];
            this.#button.title = 'Color theme: click to switch between dark, light and system';
        }
        for (const listener of this.#listeners) listener(this.dark);
    }
}
