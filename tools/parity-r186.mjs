// Render each scene with three.c and with three.js r186 on the same backend
// (WebGL2 on webgl-node, or --wgpu: WebGPU on webgpu-node) and compare the
// pixels at the same frame.
//   node tools/parity-r186.mjs [scene-prefix ...] [--wgpu] [--tol 2] [--out DIR]
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
// --wgpu: three.c's WebGPU backend (build/native-wgpu, WGPU=1 ./build.sh) against r186's WebGPU backend
let wgpu = false;
for (let i = 0; i < a.length; i++) { if (a[i] === '--tol') tol = +a[++i]; else if (a[i] === '--out') out = resolve(a[++i]); else if (a[i] === '--wgpu') wgpu = true; else want.push(a[i]); }
if (wgpu) out = join(out, 'wgpu');
mkdirSync(out, { recursive: true });
const FEATURES = ['t1-texture', 't2-fog', 't3-shadow-dir', 't4-shadow-spot-point', 'g1-box-textured', 'g2-cesium-man', 'g3-morph-cube',
  'g4-water-bottle', 'g5-normal-tangent', 'r1-render-target', 'r2-tone-mapping', 'r4-env-map', 'r5-pmrem', 'r6-sprite-points',
  'r7-lod', 'r9-physical-lights', 'r10-light-map', 'w1-world-matrix', 'i1-instance-color', 't5-transparent-basic'];
const scenes = ['01-cubes', '02-lit', '03-instanced', '04-geometry', '05-heavy', '06-heavy-instanced', '07-static-instanced', '09-static-64', '10-static-256', '11-mixed', '12-suzanne']
  .concat(FEATURES)
  .filter((s) => !want.length || want.some((w) => s.startsWith(w)));
const env = { ...process.env, EGL_PLATFORM: 'surfaceless' };
// differences that are r186 defects, measured, which three.c does not copy:
// reported as R186BUG (not a failure) with the reason; each was confirmed by
// changing the scene so the defect moves or disappears
const R186_BUGS = {
  'r6-sprite-points': 'r186 uploads another Sprite\'s center (both backends, Node and Chromium)',
  'r4-env-map': 'r186 shares one Phong program between combine Mix and Add (the cache key leaves combine out); with the Mix material removed its Add sphere matches three.c exactly',
  'r5-pmrem': 'r186 binds the first material\'s environment to every material of the same program (rows 2 and 3 show row 1\'s PMREM); rendered alone, r186\'s equirect row and scene row match three.c (1 and 0 pixels)',
  'g5-normal-tangent:gl': 'not an r186 defect: the native bench draws straight into its EGL pbuffer surface, where a few pixel-centre ties on triangle edges resolve differently; the same three.c frame drawn into a framebuffer object matches r186 at 0 pixels (the wasmcart GL cart, or BENCH_EXACT_OUTPUT=1)',
  'i1-instance-color:gl': 'not an r186 defect: the native bench draws straight into its EGL pbuffer surface, where a few pixel-centre ties on triangle edges resolve differently; the same three.c frame drawn into a framebuffer object matches r186 at 0 pixels (the wasmcart GL cart, or BENCH_EXACT_OUTPUT=1)',
  'r1-render-target:gl': 'r186 WebGL shares one program between a DepthTexture map and a colour map; the first built wins (build the depth quad first and every quad reads .x)',
};
let failures = 0;
for (const s of scenes) {
  const c = join(out, `c-${s}.png`), j = join(out, `js-${s}.png`);
  // r186 shows an InstancedMesh's instance matrices two renders late when the
  // matrices are a static-usage instanced ATTRIBUTE (count above the uniform
  // buffer limit) and are rewritten every frame (06): compare three.c's
  // render 1 with r186's render 3, which is what r186 puts on screen
  const feature = FEATURES.includes(s);
  const nframes = feature ? '100' : '3';
  const n = spawnSync(join(ROOT, wgpu ? 'build/native-wgpu/bench-native' : 'build/native/bench-native'), [s, '--mode', 'check', '--frames', nframes, ...(wgpu ? ['--wgpu'] : []), '--png', c], { env, encoding: 'utf8' });
  const nm = /"skipped":(\d+)/.exec(n.stdout || ''), ne = /"error":(null|"[^"]*")/.exec(n.stdout || '');
  const ms = /"missingStates":("(?:[^"\\]|\\.)*")/.exec(n.stdout || '');
  if (ms) for (const st of JSON.parse(ms[1]).split('\n').filter(Boolean)) MISSING.add(st);
  const job = JSON.stringify({ scene: s, mode: 'check', checkFrame: 3, renderProbe: '({})', png: j });
  const r = feature
    ? spawnSync(process.execPath, [join(ROOT, 'tools/ref-r186.mjs'), s, j, '100', ...(wgpu ? ['--webgpu'] : [])], { env, encoding: 'utf8', cwd: ROOT, timeout: 300000 })
    : spawnSync(process.execPath, [join(LUA, wgpu ? 'bench/lanes/node-js-webgpu.mjs' : 'bench/lanes/node-js.mjs'), job], { env, encoding: 'utf8', cwd: LUA });
  if (!existsSync(c) || !existsSync(j)) { console.log(`${s.padEnd(22)} NO IMAGE (native ${n.status}, js ${r.status}) ${(n.stderr || r.stderr || '').slice(-200)}`); failures++; continue; }
  const d = spawnSync(process.execPath, [join(ROOT, 'tools/compare-png.mjs'), c, j, '--tol', String(tol)], { encoding: 'utf8' });
  let res; try { res = JSON.parse(d.stdout); } catch { res = { raw: d.stdout }; }
  const skipped = nm ? +nm[1] : '?';
  // a handful of edge pixels (rasterisation of the same triangles through
  // different float paths) is a match; more is a difference
  const ok = res.differ <= EDGE_PIXELS && skipped === 0;
  const bug = !ok && (R186_BUGS[s] || R186_BUGS[`${s}:${wgpu ? 'wgpu' : 'gl'}`]);
  console.log(`${s.padEnd(22)} ${ok ? 'MATCH ' : bug ? (bug.startsWith('not an r186 defect') ? 'KNOWN ' : 'R186BUG') : 'DIFFER'} differ=${res.differ} max=${res.maxChannelDiff} skipped=${skipped} err=${ne ? ne[1] : '?'}${bug ? ' (' + bug + ')' : ''}`);
  if (!ok && !bug) failures++;
}
if (MISSING.size) console.log(`table states the scenes need (add to tools/gen-states.txt):\n${[...MISSING].sort().join('\n')}`);
process.exit(failures ? 1 : 0);
