// Regenerates the vendored editor libraries from the exact versions in package.json:
//   cd web/tools/editor-bundle && npm ci --ignore-scripts && node build.mjs
// CodeMirror is published as many small ES modules, so it is bundled into one minified file. xterm.js already ships
// ES modules and is copied unmodified.
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
