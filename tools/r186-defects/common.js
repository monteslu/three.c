// Shared by the r186 defect pages: three.js (r186, or a dev build) and a renderer
// on the asked backend; every check renders into a RenderTarget and reads it.
const q = new URLSearchParams(location.search);
export const VER = q.get('v'), BACKEND = q.get('b'), CASE = q.get('c');
export const THREE = await import(`/${VER}/build/three.webgpu.js`);
export async function makeRenderer() {
  const canvas = typeof OffscreenCanvas !== 'undefined' ? new OffscreenCanvas(64, 64) : document.createElement('canvas');
  const r = new THREE.WebGPURenderer({ canvas, antialias: false, forceWebGL: BACKEND === 'webgl' });
  await r.init();
  return r;
}
export const W = 64, H = 64;
export async function shot(r, scene, cam) {
  const rt = new THREE.RenderTarget(W, H, { type: THREE.UnsignedByteType });
  r.setRenderTarget(rt);
  for (let i = 0; i < 6; i++) { r.render(scene, cam); await new Promise((ok) => setTimeout(ok, 30)); }   // async resources (PMREM) settle
  r.setRenderTarget(null);
  const px = await r.readRenderTargetPixelsAsync(rt, 0, 0, W, H);
  rt.dispose();
  return px;
}
export function at(px, x, y) { const i = (y * W + x) * 4; return [px[i], px[i + 1], px[i + 2]]; }
export function dataTex(rgb, n = 4) {
  const d = new Uint8Array(n * n * 4);
  for (let i = 0; i < n * n; i++) { d[i * 4] = rgb[0]; d[i * 4 + 1] = rgb[1]; d[i * 4 + 2] = rgb[2]; d[i * 4 + 3] = 255; }
  const t = new THREE.DataTexture(d, n, n, THREE.RGBAFormat);
  t.needsUpdate = true;
  return t;
}
// a CubeTexture of DataTexture faces, as test/scenes/r4-env-map.js builds one
export function cubeOf(rgb) {
  const c = new THREE.CubeTexture([0, 1, 2, 3, 4, 5].map(() => dataTex(rgb)));
  c.format = THREE.RGBAFormat;
  c.needsUpdate = true;
  return c;
}
export function done(result) { window.__result = { ver: VER, backend: BACKEND, revision: THREE.REVISION, ...result }; }
