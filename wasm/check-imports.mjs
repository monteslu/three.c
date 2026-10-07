// Fail if a cart imports anything but GL ("gl"), wasmcart's own env functions
// (wc_*, plus emscripten_notify_memory_growth, which every host provides) or WASI. With ERROR_ON_UNDEFINED_SYMBOLS=0 a symbol whose object was
// never linked becomes an env import and the cart builds anyway.
// A WebGPU cart (WGPU=1 build.sh) may also import wasmcart's frozen
// emdawnwebgpu set (wasmcart src/wgpu/emdawnwebgpu-glue-manifest.js).
import { readFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
const path = process.argv[2];
const mod = new WebAssembly.Module(readFileSync(path));
const wasmcart = process.env.WASMCART || join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'wasmcart');
const mf = join(wasmcart, 'src', 'wgpu', 'emdawnwebgpu-glue-manifest.js');
const wgpu = new Set(existsSync(mf) ? (await import(pathToFileURL(mf).href)).default.imports : []);
const bad = WebAssembly.Module.imports(mod).filter((i) =>
  !(i.module === 'gl' || i.module === 'wasi_snapshot_preview1' ||
    (i.module === 'env' && (/^wc_/.test(i.name) || i.name === 'emscripten_notify_memory_growth' || wgpu.has(i.name)))));
if (bad.length) {
  console.error(`${path}: unexpected imports:\n` + bad.map((i) => `  ${i.module}.${i.name}`).join('\n'));
  process.exit(1);
}
