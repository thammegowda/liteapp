(() => {
    if (globalThis.__liteappSelectionBridgeInstalled) return;
    globalThis.__liteappSelectionBridgeInstalled = true;
    const api = globalThis.browser || globalThis.chrome;
    const selections = new Map();
    const maximumSelections = 16;
    const editableInput = element => element instanceof HTMLTextAreaElement ||
        element instanceof HTMLInputElement &&
        /^(?:email|search|tel|text|url)$/u.test(element.type || 'text') && !element.readOnly && !element.disabled;
    const contentEditable = node => {
        const element = node?.nodeType === Node.ELEMENT_NODE ? node : node?.parentElement;
        return element?.closest?.('[contenteditable]:not([contenteditable="false"])') || null;
    };
    const remember = (id, selection) => {
        selections.delete(id);
        selections.set(id, selection);
        while (selections.size > maximumSelections) selections.delete(selections.keys().next().value);
    };
    const capture = id => {
        const element = document.activeElement;
        if (editableInput(element)) {
            const start = element.selectionStart, end = element.selectionEnd;
            if (Number.isInteger(start) && Number.isInteger(end) && end > start) {
                const text = element.value.slice(start, end);
                remember(id, {kind: 'input', element, start, end, text});
                return {text, editable: true};
            }
        }
        const selection = getSelection();
        const text = selection?.toString() || '';
        if (!text) return {text: '', editable: false};
        const range = selection.rangeCount ? selection.getRangeAt(0).cloneRange() : null;
        const editable = range && contentEditable(range.commonAncestorContainer);
        if (editable) {
            remember(id, {kind: 'contenteditable', element: editable, range, text});
            return {text, editable: true};
        }
        remember(id, {kind: 'text', text});
        return {text, editable: false};
    };
    const inputEvent = (type, text, cancelable = false) => new InputEvent(type, {
        bubbles: true,
        cancelable,
        composed: true,
        inputType: 'insertText',
        data: text,
    });
    const replace = (id, expected, replacement) => {
        const saved = selections.get(id);
        if (!saved || saved.text !== expected) throw new Error('The original selection is no longer available');
        if (saved.kind === 'input') {
            const {element, start, end} = saved;
            if (!element.isConnected || element.value.slice(start, end) !== expected)
                throw new Error('The editable text changed before replacement');
            if (!element.dispatchEvent(inputEvent('beforeinput', replacement, true)))
                throw new Error('The page rejected the replacement');
            element.focus();
            element.setSelectionRange(start, end);
            element.setRangeText(replacement, start, end, 'end');
            element.dispatchEvent(inputEvent('input', replacement));
        } else if (saved.kind === 'contenteditable') {
            const {element, range} = saved;
            if (!element.isConnected || !range.commonAncestorContainer.isConnected || range.toString() !== expected)
                throw new Error('The editable text changed before replacement');
            if (!element.dispatchEvent(inputEvent('beforeinput', replacement, true)))
                throw new Error('The page rejected the replacement');
            const node = document.createTextNode(replacement);
            range.deleteContents();
            range.insertNode(node);
            range.setStartAfter(node);
            range.collapse(true);
            const selection = getSelection();
            selection.removeAllRanges();
            selection.addRange(range);
            element.dispatchEvent(inputEvent('input', replacement));
        } else {
            throw new Error('The original selection is not editable');
        }
        selections.delete(id);
        return {replaced: true};
    };
    api.runtime.onMessage.addListener((message, _sender, sendResponse) => {
        if (message?.type === 'liteapp.selection.capture') {
            try {
                sendResponse({ok: true, ...capture(String(message.id || ''))});
            } catch (error) {
                sendResponse({ok: false, error: error.message || String(error)});
            }
            return false;
        }
        if (message?.type === 'liteapp.selection.replace') {
            try {
                sendResponse({ok: true, ...replace(
                    String(message.id || ''), String(message.expected || ''), String(message.replacement || ''))});
            } catch (error) {
                sendResponse({ok: false, error: error.message || String(error)});
            }
            return false;
        }
        return false;
    });
})();
