// Where the tools find their Node packages: the package's environment
// variable (a checkout directory) if set, else this repo's node_modules
// (`npm install`), else a wasmcart checkout's node_modules ($WASMCART,
// default ../wasmcart).
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
export const WASMCART = resolve(process.env.WASMCART || join(ROOT, '..', 'wasmcart'));
const ENV = { 'webgl-node': 'WEBGL_NODE', 'webgpu-node': 'WEBGPU_NODE', playwright: 'PLAYWRIGHT' };

/** The directory of package `name`. */
export function pkgDir(name) {
  const dirs = [process.env[ENV[name]], join(ROOT, 'node_modules', name), join(WASMCART, 'node_modules', name)].filter(Boolean);
  const d = dirs.find((x) => existsSync(join(x, 'package.json')));
  if (!d) throw new Error(`${name} not found (looked in ${dirs.join(', ')}): run npm install in ${ROOT}, or set ${ENV[name] || 'its path'}`);
  return resolve(d);
}
/** A file: URL for the package's index.mjs, to import(). */
export const pkgEntry = (name) => pathToFileURL(join(pkgDir(name), 'index.mjs')).href;
