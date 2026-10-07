// A three.c cart (build/wasm/three-<scene>.wasc) in wasmcart's Node host on
// webgl-node: the same wasm as in the browser, with no browser. Times frames
// the way bench-native --mode cpu does (GPU idle before each, median), and in
// blocks without syncs (throughput: submit cost, GPU overlapped).
//
//   BENCH_EGL_DEVICE=renderD128 LD_PRELOAD=<three.lua egl-device-select.so> \
//     node tools/node-cart.mjs <scene> [frames=300]
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const WASMCART = resolve(process.env.WASMCART || join(ROOT, '..', 'wasmcart'));
const [scene, framesArg = '300', rtFlag, rtMs, aaArg] = process.argv.slice(2);
const N = +framesArg;
const { CartHost } = await import(pathToFileURL(join(WASMCART, 'src', 'CartHost.js')));
const { createWebGL2Context } = await import((await import('./deps.mjs')).pkgEntry('webgl-node'));
/* the canvas the example asks for: antialias (webgl-node opt-in) for the antialias examples */
const made = createWebGL2Context(1280, 720, { antialias: aaArg === '1' });
const gl = made.gl;
const host = new CartHost();
/* direct present, as the browser host does for a matching canvas (NODE_REDIRECT=1: the old redirect path) */
const direct = process.env.NODE_REDIRECT ? false : aaArg === '1' ? 'msaa' : true;
await host.load(new Uint8Array(readFileSync(join(ROOT, 'build', 'wasm', `three-${scene}.wasc`))), { glBackend: gl, directPresent: direct });
const pads = [{ connected: true, buttons: 0 }];
const frame = () => host.runFrame(pads);
if (rtFlag === '--check') {
  /* frame 101 (an example's reference frame 100), read the way a host reads a cart (withRenderedFrame) */
  for (let i = 0; i < 101; i++) frame();
  const px = new Uint8Array(1280 * 720 * 4);
  const ok = host.withRenderedFrame(() => gl.readPixels(0, 0, 1280, 720, gl.RGBA, gl.UNSIGNED_BYTE, px));
  const { pngRgb } = await import('./example-ref.mjs');
  pngRgb(rtMs, Buffer.from(px.buffer), 1280, 720);
  console.log(JSON.stringify({ scene, read: ok, direct: host._glFuncs?._isDirectPresent?.(), png: rtMs }));
  process.exit(0);
}
for (let i = 0; i < 60; i++) frame();
if (rtFlag === '--rt') {
  /* bench/rt/driver.mjs lane */
  for (let i = 0; i < 60; i++) frame();
  gl.finish();
  console.log('RT_BEGIN');
  await new Promise((r) => setTimeout(r, 100));
  const t0 = performance.now();
  let n = 0;
  while (performance.now() - t0 < +rtMs) { for (let k = 0; k < 10; k++) frame(); n += 10; }
  gl.finish();
  console.log(`RT_END ${n} ${performance.now() - t0}`);
  await new Promise((r) => setTimeout(r, 500)); /* the driver reads memory now */
  process.exit(0);
}
const t = [];
for (let i = 0; i < N; i++) { gl.finish(); const a = performance.now(); frame(); t.push(performance.now() - a); }
gl.finish();
t.sort((a, b) => a - b);
const a = performance.now();
for (let i = 0; i < N; i++) frame();
const submit = (performance.now() - a) / N;
gl.finish();
const whole = (performance.now() - a) / N;
console.log(JSON.stringify({ scene, renderer: gl.getParameter(gl.RENDERER), cpuMedianMs: t[N >> 1],
  blockSubmitMs: submit, blockWholeMs: whole }));
process.exit(0);
