// Regenerates every library under web/libs from the exact versions in package.json:
//   cd web/tools/vendor && npm ci --ignore-scripts && node build.mjs
// CodeMirror is published as many small ES modules, so it is bundled into one minified file, and Temml's ES module is
// minified. The other libraries already ship ES modules and are copied unmodified with their licenses.
import {build} from 'esbuild';
import {copyFile, mkdir, readFile, writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';

const here = fileURLToPath(new URL('.', import.meta.url));
const libraries = join(here, '..', '..', 'libs');
const modules = join(here, 'node_modules');

await mkdir(join(libraries, 'codemirror'), {recursive: true});
await build({
    entryPoints: [join(here, 'entry.mjs')],
    bundle: true,
    format: 'esm',
    minify: true,
    legalComments: 'eof',
    target: 'es2022',
    outfile: join(libraries, 'codemirror', 'codemirror.mjs'),
});
const pins = JSON.parse(await readFile(join(here, 'package.json'), 'utf8')).devDependencies;
const notice = Object.entries(pins).filter(([name]) => name.startsWith('@codemirror/'))
    .map(([name, version]) => `${name} ${version}`).join('\n');
await writeFile(join(libraries, 'codemirror', 'VERSIONS'), notice + '\n');
await copyFile(join(modules, '@codemirror', 'view', 'LICENSE'), join(libraries, 'codemirror', 'LICENSE'));

await mkdir(join(libraries, 'xterm'), {recursive: true});
for (const [source, target] of [
    ['@xterm/xterm/lib/xterm.mjs', 'xterm.mjs'],
    ['@xterm/xterm/css/xterm.css', 'xterm.css'],
    ['@xterm/xterm/LICENSE', 'LICENSE'],
    ['@xterm/addon-fit/lib/addon-fit.mjs', 'addon-fit.mjs'],
])
    await copyFile(join(modules, source), join(libraries, 'xterm', target));

// Unmodified upstream files: [package directory, file, vendored name].
const copies = {
    marked: [['marked', 'lib/marked.esm.js', 'marked.esm.js'], ['marked', 'LICENSE', 'LICENSE']],
    dompurify: [['dompurify', 'dist/purify.es.mjs', 'purify.es.mjs'], ['dompurify', 'LICENSE', 'LICENSE'],
        ['dompurify', 'LICENSE-MPL', 'LICENSE-MPL']],
    highlightjs: [['@highlightjs/cdn-assets', 'es/highlight.min.js', 'highlight.min.js'],
        ['@highlightjs/cdn-assets', 'LICENSE', 'LICENSE']],
    temml: [['temml', 'dist/Temml-Local.css', 'temml.css'], ['temml', 'dist/Temml.woff2', 'Temml.woff2'],
        ['temml', 'LICENSE', 'LICENSE']],
};
for (const [directory, files] of Object.entries(copies)) {
    await mkdir(join(libraries, directory), {recursive: true});
    for (const [module, source, target] of files)
        await copyFile(join(modules, module, source), join(libraries, directory, target));
}
await build({
    entryPoints: [join(modules, 'temml', 'dist', 'temml.mjs')],
    format: 'esm',
    minify: true,
    legalComments: 'eof',
    target: 'es2022',
    outfile: join(libraries, 'temml', 'temml.mjs'),
});
