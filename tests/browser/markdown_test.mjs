import assert from 'node:assert/strict';
import test from 'node:test';

import {unfinishedBlock} from '../../web/markdown/markdown.mjs';
import {marked} from '../../web/libs/marked/marked.esm.js';

// markdown.mjs registers its math syntax on the shared marked instance; rendering to MathML needs a DOM and is
// covered by applications' browser tests.
const html = source => marked.parse(source, {gfm: true});

test('recognizes LaTeX delimiters but leaves prices and code alone', () => {
    assert.match(html('Area $\\pi r^2$ here.'), /<span class="math-inline">\\pi r\^2<\/span>/);
    assert.match(html('Inline \\(a+b\\) too.'), /<span class="math-inline">a\+b<\/span>/);
    assert.match(html('$$\nE = mc^2\n$$\n'), /<div class="math-display">E = mc\^2<\/div>/);
    assert.match(html('\\[\n\\int_0^1 x\\,dx\n\\]\n'), /<div class="math-display">\\int_0\^1 x\\,dx<\/div>/);
    assert.doesNotMatch(html('It costs $5 and $10 today.'), /math-/);
    assert.doesNotMatch(html('A lone $ sign.'), /math-/);
    assert.doesNotMatch(html('Run `echo $HOME $PATH` now.'), /math-/);
    assert.doesNotMatch(html('```sh\necho $a $b\n```\n'), /math-/);
    assert.match(html('Escapes: $a < b$'), /<span class="math-inline">a &#60; b<\/span>/, 'TeX is escaped as text');
});

test('streaming holds back unfinished code blocks and display formulas', () => {
    assert.equal(unfinishedBlock('Done.\n'), null);
    assert.deepEqual(unfinishedBlock('Intro\n```js\nconst a = 1;\n'), {kind: 'code', start: 6});
    assert.equal(unfinishedBlock('```\n$$ inside code\n```\n'), null, 'math markers inside code are ignored');
    assert.deepEqual(unfinishedBlock('Text\n$$\nx^2\n'), {kind: 'math', start: 5});
    assert.equal(unfinishedBlock('$$\nx^2\n$$\n'), null);
    assert.deepEqual(unfinishedBlock('See \\[ a'), {kind: 'math', start: 4});
});
