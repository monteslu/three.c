// Reference renders from unmodified three.js r186 in Chromium (its
// WebGPURenderer on the WebGL2 backend, or on WebGPU with --webgpu): the
// second r186 oracle, on a browser's GPU stack, for when webgl-node or
// webgpu-node and three.c disagree.
//
//   node tools/ref-r186-browser.mjs <scene> <out.png> [frame=100]
//
// A scene is the same classic script tools/ref-r186.mjs runs: THREE (r186,
// plus THREE.GLTFLoader), a THREE.WebGPURenderer drawing into the harness's
// 1280x720 canvas (antialias off, depth + stencil on), globalThis.frame and
// optionally globalThis.ready.
import { createServer } from 'node:http';
import { readFileSync, existsSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname, extname, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateSync, crc32 } from 'node:zlib';
const require_write = (log) => writeFileSync(process.env.HITLOG || '/dev/null', log.join('\n') + '\n');

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const THREE_LUA = resolve(process.env.THREE_LUA || join(ROOT, '..', 'three.lua'));
const WASMCART = resolve(process.env.WASMCART || join(ROOT, '..', 'wasmcart'));
// THREE_PKG: another three.js tree (with build/ and examples/), e.g. a dev checkout; SCENES: another scene directory
const PKG = resolve(process.env.THREE_PKG || join(ROOT, 'build', 'three-r186', 'package'));
const SCENE_DIR = resolve(process.env.SCENES || join(ROOT, 'test', 'scenes'));
// --webgpu: r186's WebGPU backend in Chromium (its adapter: SwiftShader when headless)
const WEBGPU = process.argv.includes('--webgpu');
const [scene, out, frameArg = '100'] = process.argv.slice(2).filter((a) => a !== '--webgpu');
if (!scene || !out) { console.error('usage: ref-browser.mjs <scene> <out.png> [frame]'); process.exit(1); }
const FRAMES = +frameArg;
const MIME = { '.js': 'text/javascript', '.html': 'text/html', '.glb': 'model/gltf-binary', '.gltf': 'model/gltf+json',
  '.png': 'image/png', '.jpg': 'image/jpeg', '.bin': 'application/octet-stream' };

const server = createServer((req, res) => {
  const p = decodeURIComponent(req.url.split('?')[0]);
  const send = (b, t) => { res.writeHead(200, { 'Content-Type': t, 'Cache-Control': 'no-store' }); res.end(b); };
  if (p === '/') return send('<!doctype html><script type="importmap">{"imports":{"three":"/build/three.webgpu.js"}}</script><canvas id="c" width="1280" height="720"></canvas>', 'text/html');
  let f;
  if (p.startsWith('/build/') || p.startsWith('/examples/')) f = join(PKG, normalize(p.slice(1)));
  else if (p.startsWith('/scenes/')) f = join(SCENE_DIR, normalize(p.slice(8)));
  else f = join(ROOT, 'test', 'assets', normalize(p.slice(1)));
  if (!existsSync(f)) { res.writeHead(404); res.end(); return; }
  send(readFileSync(f), MIME[extname(f)] || 'application/octet-stream');
});
await new Promise((ok) => server.listen(8790, '127.0.0.1', ok));
const { chromium } = await import((await import(new URL('./deps.mjs', import.meta.url))).pkgEntry('playwright'));
const browser = await chromium.launch({ args: WEBGPU ? ['--enable-unsafe-webgpu'] : ['--enable-gpu', '--ignore-gpu-blocklist', '--use-angle=gl'] });
try {
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') errors.push(m.text()); });
  await page.goto('http://127.0.0.1:8790/');
  const px = await page.evaluate(async ({ scene, FRAMES, WEBGPU }) => {
    // WebGPU: a canvas whose texture this page owns (headless Chromium loses
    // the instance once a real canvas texture is used; wasmcart's
    // createTextureCanvas does the same for its carts)
    let canvas = document.getElementById('c');
    if (WEBGPU) {
      let cfg = null, texture = null;
      const ctx = { configure(c) { cfg = c; texture = null; }, unconfigure() { cfg = null; }, getConfiguration() { return cfg; },
        getCurrentTexture() {
          if (!texture) texture = cfg.device.createTexture({ size: [fake.width, fake.height], format: cfg.format, usage: cfg.usage | GPUTextureUsage.COPY_SRC });
          return texture;
        } };
      const fake = { width: 1280, height: 720, style: {}, getContext: (t) => (t === 'webgpu' ? ctx : null), addEventListener() {}, removeEventListener() {},
        getBoundingClientRect: () => ({ left: 0, top: 0, width: 1280, height: 720 }) };
      ctx.canvas = fake;
      canvas = fake;
    }
    const gl = WEBGPU ? null : canvas.getContext('webgl2', { antialias: false, depth: true, stencil: true, preserveDrawingBuffer: false });
    globalThis.__canvas = canvas;
    globalThis.__gl = gl;
    const load = (src) => new Promise((ok, bad) => { const el = document.createElement('script'); el.src = src; el.onload = ok; el.onerror = () => bad(new Error(src)); document.head.appendChild(el); });
    const mod = await import('/build/three.webgpu.js');
    const { GLTFLoader } = await import('/examples/jsm/loaders/GLTFLoader.js');
    const renderers = [];
    const THREE = Object.assign(Object.create(null), mod);
    THREE.WebGPURenderer = class WebGPURenderer extends mod.WebGPURenderer {
      constructor(p = {}) {
        super(WEBGPU ? { ...p, canvas, forceWebGL: false } : { ...p, canvas, context: gl, forceWebGL: true });
        renderers.push(this);
      }
      // one browser frame of r186's animation loop (Animation.start's update),
      // which a scene calling render() directly never runs: without it the node
      // frame id never advances and per-frame node updates (skeletons) run once
      __tick() { if (this.info.autoReset === true) this.info.reset(); this._nodes.nodeFrame.update(); this.info.frame = this._nodes.nodeFrame.frameId; }
    };
    THREE.GLTFLoader = GLTFLoader;
    globalThis.THREE = THREE;
    await load('/scenes/' + scene + '.js');
    if (globalThis.ready) await globalThis.ready;
    await Promise.all(renderers.map((r) => r.init()));
    const px = new Uint8Array(1280 * 720 * 4);
    if (WEBGPU) {
      // the last frame read in the same task (a canvas texture goes back at composite); rows top-down, flipped below to match GL
      for (let i = 0; i < FRAMES - 1; i++) { for (const r of renderers) r.__tick(); globalThis.frame(); }
      for (const r of renderers) r.__tick();
      globalThis.frame();
      // (mapAsync fails on a device that has taken a canvas texture in headless
      // Chromium: read the canvas through a 2D canvas in the same task instead)
      const device = renderers[0].backend.device, tex = canvas.getContext('webgpu').getCurrentTexture();
      const buf = device.createBuffer({ size: 1280 * 4 * 720, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
      const enc = device.createCommandEncoder();
      enc.copyTextureToBuffer({ texture: tex }, { buffer: buf, bytesPerRow: 1280 * 4 }, { width: 1280, height: 720 });
      device.queue.submit([enc.finish()]);
      await buf.mapAsync(GPUMapMode.READ);
      const m = new Uint8Array(buf.getMappedRange()), bgra = tex.format.startsWith('bgra');
      for (let y = 0; y < 720; y++) for (let x = 0; x < 1280; x++) {
        const s = (y * 1280 + x) * 4, d = ((719 - y) * 1280 + x) * 4;
        px[d] = m[s + (bgra ? 2 : 0)]; px[d + 1] = m[s + 1]; px[d + 2] = m[s + (bgra ? 0 : 2)]; px[d + 3] = 255;
      }
    } else {
      for (let i = 0; i < FRAMES; i++) { for (const r of renderers) r.__tick(); globalThis.frame(); }
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.readPixels(0, 0, 1280, 720, gl.RGBA, gl.UNSIGNED_BYTE, px);
    }
    let bin = '';
    for (let i = 0; i < px.length; i += 0x8000) bin += String.fromCharCode.apply(null, px.subarray(i, i + 0x8000));
    return { b64: btoa(bin), hits: globalThis.hits, log: globalThis.log };
  }, { scene, FRAMES, WEBGPU });
  const rgba = Buffer.from(px.b64, 'base64');
  const W = 1280, H = 720, raw = Buffer.alloc(H * (1 + W * 3));
  for (let y = 0; y < H; y++) {
    const s = (H - 1 - y) * W * 4, d = y * (1 + W * 3) + 1;
    for (let x = 0; x < W; x++) { raw[d + x * 3] = rgba[s + x * 4]; raw[d + x * 3 + 1] = rgba[s + x * 4 + 1]; raw[d + x * 3 + 2] = rgba[s + x * 4 + 2]; }
  }
  const chunk = (type, data) => {
    const b = Buffer.alloc(12 + data.length);
    b.writeUInt32BE(data.length, 0); b.write(type, 4, 'latin1'); data.copy(b, 8);
    b.writeUInt32BE(crc32(Buffer.concat([Buffer.from(type, 'latin1'), data])) >>> 0, 8 + data.length);
    return b;
  };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 2;
  writeFileSync(out, Buffer.concat([Buffer.from([0x89, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
  if (px.log) require_write(px.log);
  console.log(`${scene}: wrote ${out}${px.hits !== undefined ? ' hits=' + px.hits : ''}${errors.length ? '  console: ' + errors.slice(0, 3).join(' | ') : ''}`);
} finally {
  await browser.close();
  server.close();
}
