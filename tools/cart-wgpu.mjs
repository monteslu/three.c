// Run a dual three.c cart (WGPU=1 wasm/build.sh) on wasmcart's Node host
// with WebGPU (webgpu-node / native-dawn) and write frame N as a PNG.
//
//   node tools/cart-wgpu.mjs build/wasm/three-01-cubes-wgpu.wasc out.png [frames=3] [--gl]
//
// --gl runs the same cart on the GL route (wgpu: false) for comparison.
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { writePngRgb } from './png.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const GL = args.includes('--gl');
const [cart, out, framesArg = '3'] = args.filter((a) => a !== '--gl');
const WASMCART = resolve(process.env.WASMCART || join(ROOT, '..', 'wasmcart'));
const { CartHost } = await import(pathToFileURL(join(WASMCART, 'index.js')).href);
const host = new CartHost();
await host.load(resolve(cart), GL ? { wgpu: false } : {});
const tick = () => new Promise((r) => setImmediate(r));
const t0 = performance.now();
for (let i = 0; i < +framesArg; i++) { host.runFrame(); await tick(); }
const ms = (performance.now() - t0) / +framesArg;
let frame;
if (host.usesWgpu) frame = await host.readGpuFrame();
else {
  const gl = host.getGlContext(), { width, height } = host.getInfo();
  const px = new Uint8Array(width * height * 4);
  host.withRenderedFrame(() => gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, px));
  const flip = new Uint8Array(px.length);
  for (let y = 0; y < height; y++) flip.set(px.subarray((height - 1 - y) * width * 4, (height - y) * width * 4), y * width * 4);
  frame = { width, height, data: flip };
}
const rgb = new Uint8Array(frame.width * frame.height * 3);
for (let i = 0; i < frame.width * frame.height; i++) { rgb[i * 3] = frame.data[i * 4]; rgb[i * 3 + 1] = frame.data[i * 4 + 1]; rgb[i * 3 + 2] = frame.data[i * 4 + 2]; }
writePngRgb(out, frame.width, frame.height, rgb);
console.log(JSON.stringify({ cart, route: host.usesWgpu ? 'webgpu' : 'gl', frames: +framesArg, msPerFrame: +ms.toFixed(3), out }));
host.destroy?.();
process.exit(0);
