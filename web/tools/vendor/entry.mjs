// Everything web/ide/editor.mjs uses from CodeMirror, bundled into one ES module (web/libs/codemirror).
export {EditorState, Compartment} from '@codemirror/state';
export {EditorView, keymap, lineNumbers, highlightActiveLine, highlightActiveLineGutter, drawSelection,
    highlightSpecialChars, rectangularSelection, crosshairCursor, dropCursor} from '@codemirror/view';
export {defaultKeymap, history, historyKeymap, indentWithTab} from '@codemirror/commands';
export {searchKeymap, highlightSelectionMatches} from '@codemirror/search';
export {autocompletion, closeBrackets, closeBracketsKeymap, completionKeymap} from '@codemirror/autocomplete';
export {syntaxHighlighting, defaultHighlightStyle, bracketMatching, foldGutter, foldKeymap, indentOnInput,
    StreamLanguage} from '@codemirror/language';
export {MergeView, unifiedMergeView} from '@codemirror/merge';
export {oneDark} from '@codemirror/theme-one-dark';
export {javascript} from '@codemirror/lang-javascript';
export {python} from '@codemirror/lang-python';
export {cpp} from '@codemirror/lang-cpp';
export {json} from '@codemirror/lang-json';
export {markdown} from '@codemirror/lang-markdown';
export {html} from '@codemirror/lang-html';
export {css} from '@codemirror/lang-css';
export {yaml} from '@codemirror/lang-yaml';
export {rust} from '@codemirror/lang-rust';
export {go} from '@codemirror/lang-go';
export {shell} from '@codemirror/legacy-modes/mode/shell';
export {cmake} from '@codemirror/legacy-modes/mode/cmake';
export {toml} from '@codemirror/legacy-modes/mode/toml';
