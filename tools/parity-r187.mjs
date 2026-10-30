// Render each scene with three.c and with three.js r187 on the same backend
// (WebGL2 on webgl-node, or --wgpu: WebGPU on webgpu-node) and compare the
// pixels at the same frame.
//   node tools/parity-r187.mjs [scene-prefix ...] [--wgpu] [--tol 2] [--out DIR]
// A scene that needed material states the program table lacks (draws three.c
// skipped) fails, and the states are listed at the end.
import { spawnSync } from 'node:child_process';
import { mkdirSync, existsSync, readFileSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const LUA = join(ROOT, '..', 'three.lua');
const EDGE_PIXELS = 8;
const MISSING = new Set();
let tol = 2, out = join(ROOT, 'build', 'parity');
const want = [];
const a = process.argv.slice(2);
// --wgpu: three.c's WebGPU backend (build/native-wgpu, WGPU=1 ./build.sh) against r187's WebGPU backend
let wgpu = false;
for (let i = 0; i < a.length; i++) { if (a[i] === '--tol') tol = +a[++i]; else if (a[i] === '--out') out = resolve(a[++i]); else if (a[i] === '--wgpu') wgpu = true; else want.push(a[i]); }
if (wgpu) out = join(out, 'wgpu');
mkdirSync(out, { recursive: true });
const FEATURES = ['t1-texture', 't2-fog', 't3-shadow-dir', 't4-shadow-spot-point', 'g1-box-textured', 'g2-cesium-man', 'g3-morph-cube',
  'g4-water-bottle', 'g5-normal-tangent', 'r1-render-target', 'r2-tone-mapping', 'r4-env-map', 'r5-pmrem', 'r11-pmrem-probe', 'r6-sprite-points',
  'r7-lod', 'r9-physical-lights', 'r10-light-map', 'w1-world-matrix', 'i1-instance-color', 't5-transparent-basic', 'p1-physical', 'p3-eon', 'u1-uv-channel', 'r12-depth-only', ...(wgpu ? ['z1-reversed-depth', 'z2-reversed-far'] : []), 'p2-transmission', 't6-shadow-cover'];
// extension programs, filled at run time (tools/extensions/tint.mjs, swell.mjs, layers.mjs, mosaic.mjs)
FEATURES.push('e1-extension', 'e2-extension-shadow', 'e3-extension-textures', 'e4-extension-many-textures');
const scenes = ['01-cubes', '02-lit', '03-instanced', '04-geometry', '05-heavy', '06-heavy-instanced', '07-static-instanced', '09-static-64', '10-static-256', '11-mixed', '12-suzanne']
  .concat(FEATURES)
  .filter((s) => !want.length || want.some((w) => s.startsWith(w)));
const env = { THREE_JS_VERSION: '0.187.0', ...process.env, EGL_PLATFORM: 'surfaceless' };
// differences that are r187 defects, measured, which three.c does not copy:
// reported as R187BUG (not a failure) with the reason; each was confirmed by
// changing the scene so the defect moves or disappears
const R187_BUGS = {
  'g5-normal-tangent:gl': 'not an r187 defect: the native bench draws straight into its EGL pbuffer surface, where a few pixel-centre ties on triangle edges resolve differently; the same three.c frame drawn into a framebuffer object matches r187 at 0 pixels (the wasmcart GL cart, or BENCH_EXACT_OUTPUT=1)',
  'i1-instance-color:gl': 'not an r187 defect: the native bench draws straight into its EGL pbuffer surface, where a few pixel-centre ties on triangle edges resolve differently; the same three.c frame drawn into a framebuffer object matches r187 at 0 pixels (the wasmcart GL cart, or BENCH_EXACT_OUTPUT=1)',
  'p1-physical:gl': 'not an r187 defect: the native bench draws straight into its EGL pbuffer surface, where a few pixel-centre ties on triangle edges resolve differently; the same three.c frame drawn into a framebuffer object matches r187 at 0 pixels (the wasmcart GL cart, or BENCH_EXACT_OUTPUT=1)',
  't6-shadow-cover:gl': 'not an r187 defect: the native bench draws straight into its EGL pbuffer surface, where a few pixel-centre ties on triangle edges resolve differently; the same three.c frame drawn into a framebuffer object matches r187 at 0 pixels (the wasmcart GL cart, or BENCH_EXACT_OUTPUT=1)',
  'r1-render-target:gl': 'r187 WebGL shares one program between a DepthTexture map and a colour map; the first built wins (build the depth quad first and every quad reads .x)',
  'r11-pmrem-probe:gl': 'not an r187 defect: the native bench\'s EGL pbuffer surface resolves a few triangle-edge pixel-centre ties differently (as t6-shadow-cover:gl); drawn into a framebuffer object (BENCH_EXACT_OUTPUT=1) it matches r187 at 0 pixels',
};
let failures = 0;
for (const s of scenes) {
  const c = join(out, `c-${s}.png`), j = join(out, `js-${s}.png`);
  // r187 shows an InstancedMesh's instance matrices two renders late when the
  // matrices are a static-usage instanced ATTRIBUTE (count above the uniform
  // buffer limit) and are rewritten every frame (06): compare three.c's
  // render 1 with r187's render 3, which is what r187 puts on screen
  const feature = FEATURES.includes(s);
  const nframes = feature ? '100' : '3';
  const n = spawnSync(join(ROOT, wgpu ? 'build/native-wgpu/bench-native' : 'build/native/bench-native'), [s, '--mode', 'check', '--frames', nframes, ...(wgpu ? ['--wgpu'] : []), '--png', c], { env, encoding: 'utf8' });
  const nm = /"skipped":(\d+)/.exec(n.stdout || ''), ne = /"error":(null|"[^"]*")/.exec(n.stdout || '');
  const ms = /"missingStates":("(?:[^"\\]|\\.)*")/.exec(n.stdout || '');
  if (ms) for (const st of JSON.parse(ms[1]).split('\n').filter(Boolean)) MISSING.add(st);
  const job = JSON.stringify({ scene: s, mode: 'check', checkFrame: 3, renderProbe: '({})', png: j });
  const r = feature
    ? spawnSync(process.execPath, [join(ROOT, 'tools/ref-r187.mjs'), s, j, '100', ...(wgpu ? ['--webgpu'] : [])], { env, encoding: 'utf8', cwd: ROOT, timeout: 300000 })
    : spawnSync(process.execPath, [join(LUA, wgpu ? 'bench/lanes/node-js-webgpu.mjs' : 'bench/lanes/node-js.mjs'), job], { env, encoding: 'utf8', cwd: LUA });
  if (!existsSync(c) || !existsSync(j)) { console.log(`${s.padEnd(22)} NO IMAGE (native ${n.status}, js ${r.status}) ${(n.stderr || r.stderr || '').slice(-200)}`); failures++; continue; }
  const d = spawnSync(process.execPath, [join(ROOT, 'tools/compare-png.mjs'), c, j, '--tol', String(tol)], { encoding: 'utf8' });
  let res; try { res = JSON.parse(d.stdout); } catch { res = { raw: d.stdout }; }
  const skipped = nm ? +nm[1] : '?';
  // a handful of edge pixels (rasterisation of the same triangles through
  // different float paths) is a match; more is a difference
  const ok = res.differ <= EDGE_PIXELS && skipped === 0;
  const bug = !ok && (R187_BUGS[s] || R187_BUGS[`${s}:${wgpu ? 'wgpu' : 'gl'}`]);
  console.log(`${s.padEnd(22)} ${ok ? 'MATCH ' : bug ? (bug.startsWith('not an r187 defect') ? 'KNOWN ' : 'R187BUG') : 'DIFFER'} differ=${res.differ} max=${res.maxChannelDiff} skipped=${skipped} err=${ne ? ne[1] : '?'}${bug ? ' (' + bug + ')' : ''}`);
  if (!ok && !bug) failures++;
}
if (MISSING.size) console.log(`table states the scenes need (add to tools/gen-states.txt):\n${[...MISSING].sort().join('\n')}`);
process.exit(failures ? 1 : 0);
