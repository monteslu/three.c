// Run a three.c cart in Chromium on wasmcart's browser host (CartHostWeb),
// WebGPU route unless --gl, and write frame N as a PNG.
//
//   node tools/cart-browser.mjs build/wasm/three-05-heavy-wgpu.wasc out.png [frames=4] [--gl] [--swiftshader]
//
// By default Chromium is asked for the real GPU (Vulkan); --swiftshader takes
// its software adapter.
import { createServer } from 'node:http';
import { readFileSync, existsSync } from 'node:fs';
import { join, resolve, dirname, extname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { writePngRgb } from './png.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const WASMCART = resolve(process.env.WASMCART || join(ROOT, '..', 'wasmcart'));
const args = process.argv.slice(2);
const GL = args.includes('--gl'), SW = args.includes('--swiftshader');
const [cart, out, framesArg = '4'] = args.filter((a) => !a.startsWith('--'));
const { chromium } = await import((await import(new URL('./deps.mjs', import.meta.url))).pkgEntry('playwright'));

const MIME = { '.js': 'text/javascript', '.mjs': 'text/javascript', '.html': 'text/html', '.wasm': 'application/wasm' };
const PAGE = '<!doctype html><script type="importmap">{ "imports": { "fflate": "/node_modules/fflate/esm/browser.js" } }</script><body></body>';
const http = createServer((req, res) => {
  const path = decodeURIComponent(req.url.split('?')[0]);
  const file = path === '/cart.wasc' ? resolve(cart) : join(WASMCART, path);
  if (path === '/') { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end(PAGE); return; }
  if (!existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIME[extname(file)] ?? 'application/octet-stream',
    'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
  res.end(readFileSync(file));
});
await new Promise((r) => http.listen(0, '127.0.0.1', r));
const port = http.address().port;
const flags = ['--enable-unsafe-webgpu', ...(SW ? [] : ['--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'])];
const browser = await chromium.launch({ headless: true, args: flags });
const page = await browser.newPage();
const logs = [];
page.on('console', (m) => logs.push(m.text()));
await page.goto(`http://127.0.0.1:${port}/`);
const r = await page.evaluate(async ({ frames, gl }) => {
  const { CartHostWeb } = await import('/web.js');
  const tick = () => new Promise((res) => setTimeout(res, 0));
  const adapter = await navigator.gpu.requestAdapter();
  const host = new CartHostWeb();
  if (!gl) { const av = await host._wgpuAvailability({}); if (!av.ok) return { route: 'none', reason: av.reason, adapter: String(!!adapter) }; }
  // GL: the page's own context, the cart's size, single sampled (a host-made
  // context is antialiased, and WebGL2 cannot blit into a multisampled canvas)
  let glOpts = {};
  if (gl) {
    const c = Object.assign(document.createElement('canvas'), { width: 1280, height: 720 });
    glOpts = { wgpu: false, glBackend: c.getContext('webgl2', { antialias: false, depth: true, stencil: true, alpha: false }), preferredWidth: 1280, preferredHeight: 720 };
  }
  await host.load('/cart.wasc', glOpts);
  if (!gl && !host.usesWgpu) return { route: 'none', reason: 'loaded without WebGPU', usesGL: host.usesGL, gpuApi: host.getInfo?.()?.gpuApi };
  const t0 = performance.now();
  let frame = null;
  // WebGPU: frames count once the cart draws (it waits for its device first)
  let drawn = 0, waited = 0;
  for (let i = 0; drawn < frames && i < frames + 200; i++) {
    host.runFrame([]);
    if (host.usesWgpu) {
      const f = await host.readGpuFrame();   // the same task as the frame
      if (f && f.data.some((v, k) => k % 4 !== 3 && v)) { drawn++; frame = f; } else if (!drawn) waited++;
    } else if (++drawn === frames) {
      // GL: the cart drew into the host's canvas; read it in the same task,
      // before the browser may discard the drawing buffer
      const glc = host.getGlContext(), { width, height } = host.getInfo();
      const px = new Uint8Array(width * height * 4);
      glc.bindFramebuffer(glc.FRAMEBUFFER, null);
      glc.readPixels(0, 0, width, height, glc.RGBA, glc.UNSIGNED_BYTE, px);
      const flip = new Uint8Array(px.length);
      for (let y = 0; y < height; y++) flip.set(px.subarray((height - 1 - y) * width * 4, (height - y) * width * 4), y * width * 4);
      frame = { width, height, data: flip };
    }
    await tick();
  }
  if (host.usesWgpu && drawn < frames) return { route: 'none', reason: `only ${drawn} WebGPU frames drawn`, waited };
  const ms = (performance.now() - t0) / frames;
  if (!frame && !host.getGlContext) return { route: 'none', adapter: String(!!adapter), usesWgpu: host.usesWgpu, usesGL: host.usesGL, keys: Object.keys(host).slice(0, 40) };
  let s = '';
  for (let i = 0; i < frame.data.length; i += 4) s += String.fromCharCode(frame.data[i], frame.data[i + 1], frame.data[i + 2]);
  return { route: host.usesWgpu ? 'webgpu' : 'gl', adapter: adapter?.info ? `${adapter.info.vendor} ${adapter.info.architecture} ${adapter.info.description}` : String(!!adapter),
           width: frame.width, height: frame.height, rgb: btoa(s), ms, waited };
}, { frames: +framesArg, gl: GL });
if (r.route === 'none') { console.log(JSON.stringify(r)); process.exit(1); }
writePngRgb(out, r.width, r.height, Buffer.from(r.rgb, 'base64'));
const errs = logs.filter((l) => /error|invalid|validation/i.test(l));
console.log(JSON.stringify({ cart, route: r.route, adapter: r.adapter, frames: +framesArg, waitedFrames: r.waited, msPerFrame: +r.ms.toFixed(2), consoleErrors: errs.length, firstError: errs[0]?.slice(0, 600) ?? null, out }));
await browser.close();
http.close();
