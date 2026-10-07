// Reference renders from unmodified three.js r186 (its WebGPURenderer on the
// WebGL2 backend, as three.lua's bench runs it) on webgl-node, for three.c's
// feature scenes (test/scenes/<name>.js) and the compare scenes.
//
//   node tools/ref-r186.mjs <scene> <out.png> [frames=100] [--dir test/scenes]
//
// A scene is a classic script: it gets THREE (r186, plus THREE.GLTFLoader),
// creates a THREE.WebGPURenderer (the harness supplies the canvas, and on GL
// the webgl-node context with forceWebGL), may fetch test/assets/* by
// relative URL, sets globalThis.frame and optionally globalThis.ready.
// three.lua's compare scenes (--dir) use the WebGLRenderer API instead, which
// is mapped onto WebGPURenderer below.
import vm from 'node:vm';
import { readFileSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { writePngRgb } from './png.mjs';
import { pkgEntry } from './deps.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
// --webgpu: r186's WebGPU backend on webgpu-node instead of WebGL2 on webgl-node
const WEBGPU = args.includes('--webgpu');
if (WEBGPU) args.splice(args.indexOf('--webgpu'), 1);
const dirIdx = args.indexOf('--dir');
const SCENES = dirIdx >= 0 ? resolve(args.splice(dirIdx, 2)[1]) : join(ROOT, 'test', 'scenes');
const [scene, out, framesArg = '100'] = args;
if (!scene || !out) { console.error('usage: ref-r186.mjs <scene> <out.png> [frames] [--dir DIR]'); process.exit(1); }
const W = 1280, H = 720, FRAMES = +framesArg;
const ASSETS = [join(ROOT, 'test', 'assets'), join(ROOT, '..', 'three.lua', 'compare', 'assets')];

let made, gl = null, canvas;
if (WEBGPU) {
  const wn = await import(pkgEntry('webgpu-node'));
  wn.installGlobals();
  canvas = wn.createCanvas(W, H);
  canvas.getContext = ((orig) => (kind, ...a) => (kind === 'webgpu' ? orig.call(canvas, kind, ...a) : null))(canvas.getContext);
  made = {};
  // Dawn's Node bindings lack copyExternalImageToTexture (r186 uploads images
  // with it): do what a browser does with a decoded RGBA8 bitmap, flipY and
  // premultipliedAlpha included, through writeTexture
  globalThis.GPUQueue.prototype.copyExternalImageToTexture = function (src, dst, size) {
    const img = src.source, w = size.width ?? size[0], h = size.height ?? size[1] ?? 1;
    let data = new Uint8Array(img.data.buffer, img.data.byteOffset, img.width * img.height * 4);
    if (src.flipY || dst.premultipliedAlpha) {
      const o = new Uint8Array(w * h * 4);
      for (let y = 0; y < h; y++) o.set(data.subarray((src.flipY ? h - 1 - y : y) * img.width * 4, (src.flipY ? h - 1 - y : y) * img.width * 4 + w * 4), y * w * 4);
      if (dst.premultipliedAlpha) for (let i = 0; i < o.length; i += 4) { const a = o[i + 3] / 255; o[i] = Math.round(o[i] * a); o[i + 1] = Math.round(o[i + 1] * a); o[i + 2] = Math.round(o[i + 2] * a); }
      data = o;
    }
    const origin = dst.origin ?? { x: 0, y: 0, z: 0 };
    this.writeTexture({ texture: dst.texture, mipLevel: dst.mipLevel ?? 0, origin: { x: origin.x ?? 0, y: origin.y ?? 0, z: origin.z ?? 0 } },
      data, { bytesPerRow: w * 4, rowsPerImage: h }, { width: w, height: h, depthOrArrayLayers: 1 });
  };
} else {
  const { createWebGL2Context } = await import(pkgEntry('webgl-node'));
  made = createWebGL2Context(W, H);
  gl = made.gl; canvas = made.canvas;
  canvas.getContext = (k) => (k === 'webgl2' ? gl : null);
}
// WebGL2 accepts an image source in texSubImage2D's 9-argument form; webgl-node
// takes typed arrays there, so a decoded bitmap passes its pixels
if (gl) {
  const sub = gl.texSubImage2D.bind(gl);
  gl.texSubImage2D = (...a) => {
    if (a.length >= 9 && a[8] && typeof a[8] === 'object' && !ArrayBuffer.isView(a[8]) && a[8].data) a[8] = a[8].data;
    return sub(...a);
  };
}
canvas.style ??= {};
canvas.addEventListener ??= () => {};
canvas.removeEventListener ??= () => {};

globalThis.requestAnimationFrame ??= () => 0;
globalThis.cancelAnimationFrame ??= () => {};
globalThis.self ??= globalThis;
globalThis.ProgressEvent ??= class ProgressEvent extends Event { constructor(t, o = {}) { super(t); Object.assign(this, { lengthComputable: false, loaded: 0, total: 0 }, o); } };
for (const k of ['ImageBitmap', 'HTMLImageElement', 'HTMLCanvasElement', 'HTMLVideoElement', 'OffscreenCanvas', 'VideoFrame']) globalThis[k] ??= class {};
// images: GLTFLoader / ImageBitmapLoader call createImageBitmap(blob); decode
// with stb_image (build/native/imgdecode) into an ImageBitmap-like object that
// webgl-node uploads as ImageData ({ width, height, data })
{
  const { execFileSync } = await import('node:child_process');
  const dec = join(ROOT, 'build', 'native', 'imgdecode');
  if (!existsSync(dec)) {   // built on first use: stb_image behind a tiny CLI (tools/imgdecode.c)
    const { mkdirSync } = await import('node:fs');
    mkdirSync(join(ROOT, 'build', 'native'), { recursive: true });
    execFileSync(process.env.CC || 'cc', ['-O2', '-I', join(ROOT, 'third_party'), join(ROOT, 'tools', 'imgdecode.c'), '-o', dec, '-lm']);
  }
  globalThis.createImageBitmap = async (src, opts = {}) => {
    const bytes = Buffer.from(await (src.arrayBuffer ? src.arrayBuffer() : new Response(src).arrayBuffer()));
    const out = execFileSync(dec, { input: bytes, maxBuffer: 1 << 28 });
    const nl = out.indexOf(10);
    const [w, h] = out.subarray(0, nl).toString().split(' ').map(Number);
    let data = new Uint8Array(out.buffer, out.byteOffset + nl + 1, w * h * 4);
    if (opts.imageOrientation === 'flipY') {
      const f = new Uint8Array(data.length);
      for (let y = 0; y < h; y++) f.set(data.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
      data = f;
    }
    const bmp = Object.create(globalThis.ImageBitmap.prototype);
    return Object.assign(bmp, { width: w, height: h, data, close() {} });
  };
}
// relative asset URLs: three's FileLoader makes a Request, which Node only
// accepts with an absolute URL; give it a private base and serve the files
const BASE = 'http://assets.local/';
const RealRequest = globalThis.Request;
globalThis.Request = class Request extends RealRequest { constructor(u, o) { super(typeof u === 'string' ? new URL(u, BASE) : u, o); } };
const realFetch = globalThis.fetch;
globalThis.fetch = async (url, opts) => {
  let u = String(url instanceof RealRequest ? url.url : url);
  if (u.startsWith('blob:')) {
    const { resolveObjectURL } = await import('node:buffer');
    const b = resolveObjectURL(u);
    return b ? new Response(b, { status: 200 }) : new Response(null, { status: 404 });
  }
  if (u.startsWith(BASE)) u = u.slice(BASE.length);
  else if (/^https?:/.test(u)) return realFetch(u, opts);
  const rel = decodeURIComponent(u.replace(/^\.?\//, ''));
  for (const d of ASSETS) {
    const f = join(d, rel);
    if (existsSync(f)) return new Response(readFileSync(f), { status: 200 });
  }
  return new Response(null, { status: 404, statusText: 'not found: ' + u });
};

const PKG = join(ROOT, 'build', 'three-r186', 'package');
const mod = await import(pathToFileURL(join(PKG, 'build', 'three.webgpu.js')).href);
const { GLTFLoader } = await import(pathToFileURL(join(PKG, 'examples', 'jsm', 'loaders', 'GLTFLoader.js')).href);
const renderers = [];
const THREE = Object.assign(Object.create(null), mod);
// the harness owns the canvas (and on GL the context): a scene's renderer draws into them
THREE.WebGPURenderer = class WebGPURenderer extends mod.WebGPURenderer {
  constructor(p = {}) {
    super(WEBGPU ? { powerPreference: 'high-performance', ...p, canvas, forceWebGL: false }
                 : { ...p, canvas, context: gl, forceWebGL: true });
    renderers.push(this);
  }
  // one browser frame of r186's animation loop (Animation.start's update),
  // which a scene calling render() directly never runs: without it the node
  // frame id never advances and per-frame node updates (skeletons) run once
  __tick() { if (this.info.autoReset === true) this.info.reset(); this._nodes.nodeFrame.update(); this.info.frame = this._nodes.nodeFrame.frameId; }
};
// three.lua's compare scenes are written against the WebGLRenderer API and
// set state right after construction; WebGPURenderer needs init() before its
// backend exists, so setters made before init are replayed after it
const DEFERRED = ['setScissorTest', 'setScissor', 'setViewport', 'setClearColor', 'setPixelRatio', 'setSize'];
THREE.WebGLRenderer = class WebGLRenderer extends THREE.WebGPURenderer {
  constructor(p = {}) {
    super({ antialias: !!p.antialias });
    this._pending = [];
    for (const k of DEFERRED) { const f = this[k].bind(this); this[k] = (...a) => { if (!this._initialized) { this._pending.push([f, a]); return this; } return f(...a); }; }
  }
};
THREE.GLTFLoader = GLTFLoader;
globalThis.THREE = THREE;
globalThis.__canvas = canvas;
globalThis.__gl = gl;

const file = join(SCENES, scene + '.js');
vm.runInThisContext(readFileSync(file, 'utf8'), { filename: file });
if (globalThis.ready) await globalThis.ready;
for (let i = 0; i < 200 && globalThis.scene && globalThis.scene.children?.length === 0; i++) await new Promise((r) => setTimeout(r, 10));
await Promise.all(renderers.map((r) => r.init()));
for (const r of renderers) { for (const [f, a] of r._pending ?? []) f(...a); r._pending = []; }
// R186_DUMP=Sprite: print each draw of that object type's uniform groups (debugging an oracle disagreement)
if (process.env.R186_DUMP) for (const r of renderers) {
  const draw = r.backend.draw.bind(r.backend);
  r.backend.draw = (ro, info) => {
    if (ro.object && ro.object.type === process.env.R186_DUMP) {
      for (const g of ro.getBindings()) for (const b of g.bindings) if (b.uniforms) {
        const buf = b.buffer instanceof ArrayBuffer ? b.buffer : b.buffer.buffer ?? b.buffer;
        console.error(`[dump] ${ro.object.type}#${ro.object.id} ${g.name}: ${Array.from(new Float32Array(buf)).map((x) => +x.toPrecision(5)).join(' ')}`);
      }
    }
    return draw(ro, info);
  };
}
for (let i = 0; i < FRAMES; i++) { for (const r of renderers) r.__tick(); globalThis.frame(); }
const rgb = new Uint8Array(W * H * 3);
if (WEBGPU) {
  const device = renderers[0].backend.device;
  await device.queue.onSubmittedWorkDone();
  const tex = canvas.getContext('webgpu').getCurrentTexture();
  const bpr = Math.ceil(W * 4 / 256) * 256;
  const buf = device.createBuffer({ size: bpr * H, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const enc = device.createCommandEncoder();
  enc.copyTextureToBuffer({ texture: tex }, { buffer: buf, bytesPerRow: bpr }, { width: W, height: H, depthOrArrayLayers: 1 });
  device.queue.submit([enc.finish()]);
  await buf.mapAsync(GPUMapMode.READ);
  const px = new Uint8Array(buf.getMappedRange());
  const bgra = tex.format.startsWith('bgra');
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const s = y * bpr + x * 4, d = (y * W + x) * 3;
    rgb[d] = bgra ? px[s + 2] : px[s]; rgb[d + 1] = px[s + 1]; rgb[d + 2] = bgra ? px[s] : px[s + 2];
  }
  buf.unmap();
} else {
  gl.finish();
  const px = new Uint8Array(W * H * 4);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.readPixels(0, 0, W, H, gl.RGBA, gl.UNSIGNED_BYTE, px);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const s = ((H - 1 - y) * W + x) * 4, d = (y * W + x) * 3;
    rgb[d] = px[s]; rgb[d + 1] = px[s + 1]; rgb[d + 2] = px[s + 2];
  }
}
writePngRgb(out, W, H, rgb);
console.log(`${scene}: wrote ${out}`);
made.destroy?.();
process.exit(0);
