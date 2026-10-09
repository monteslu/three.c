#!/usr/bin/env node
// three.c's perf suite: first-use (compile) stalls, frame hitches and
// per-draw CPU cost, on both backends, compared with a saved baseline.
//
//   node bench/perf.mjs                      run, print, compare with bench/perf-baseline.json
//   node bench/perf.mjs --save-baseline      run and write the baseline
//   node bench/perf.mjs --selftest           prove the comparison catches a slowdown
//   --gpu 7600|890m   the GPU (default 7600: renderD128 / 1002:7480)
//   --only <substr>   metrics whose key contains it
//   --reps N          processes per metric (default 3; the median is kept)
//
// Needs ./build.sh and WGPU=1 ./build.sh. Runs headless (EGL surfaceless,
// Dawn on Vulkan) and under the shared GPU lock, so it never contends with a
// suite another session is running on the same card. Results go to
// build/perf/latest.json.
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const GL = join(ROOT, 'build/native/bench-native');
const WGPU = join(ROOT, 'build/native-wgpu/bench-native');
const BASELINE = join(ROOT, 'bench/perf-baseline.json');
const LOCK = join(process.env.XDG_RUNTIME_DIR || join(ROOT, 'build'), 'cartwheel-gpu-suite.lock');

const GPUS = {
  '7600': { egl: 'renderD128', vk: '1002:7480!' },
  '890m': { egl: 'renderD129', vk: '1002:150e!' },
};

const args = process.argv.slice(2);
const opt = (name, dflt) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : dflt; };
const gpu = GPUS[opt('--gpu', '7600')];
if (!gpu) throw new Error(`--gpu: one of ${Object.keys(GPUS).join(', ')}`);
const reps = Number(opt('--reps', '3'));
const only = opt('--only', '');

// What each metric runs and reads. `tol` is the fraction a value may rise
// before it counts as a regression; `floor` (ms) absorbs timer noise on tiny
// values. Each metric keeps the median of its runs (the best is not used: one
// lucky run became a baseline 40% under every run after it). Warm-cache compiles depend on what the driver cache holds,
// so only a doubling counts there; cold (no cache, a player's first run) is
// the stall to watch.
const CPU_SCENES = ['01-cubes', 'g64p', 'd1-unique-materials', 'd2-unique-textures', 'd3-program-mix', '05-heavy', 's1-stress', 'p1-physical'];
function metrics() {
  const m = [];
  for (const be of ['gl', 'wgpu']) {
    m.push({ key: `inflate.${be}`, be: 'wgpu', run: ['-', '--mode', 'inflate'], read: j => j[`${be}FirstMs`], tol: 0.3, floor: 6 });
    for (const cache of ['warm', 'cold']) {
      const tol = cache === 'cold' ? 0.25 : 1, floor = cache === 'cold' ? 10 : 25;
      m.push({ key: `firstuse.c2-compile-zoo.${be}.${cache}`, be, cache, run: ['c2-compile-zoo', '--mode', 'firstuse'], read: j => j.firstFrameMs, tol, floor });
      m.push({ key: `hitch.c1-material-churn.${be}.${cache}.stallTotal`, be, cache, run: ['c1-material-churn', '--mode', 'hitch', '--frames', '420'], read: j => j.taggedTotalMs, tol, floor, also: ['taggedMaxMs', 'taggedMedianMs', 'over16', 'p99Ms'] });
    }
    for (const s of CPU_SCENES)
      m.push({ key: `cpu.${s}.${be}`, be, run: [s, '--mode', 'cpu', '--frames', '300'], read: j => j.cpuMedianMs, tol: 0.2, floor: 0.1, also: ['calls'] });
  }
  return m.filter(x => x.key.includes(only));
}

function runOnce(m, extraEnv = {}) {
  const env = { ...process.env, EGL_PLATFORM: 'surfaceless', BENCH_EGL_DEVICE: gpu.egl, MESA_VK_DEVICE_SELECT: gpu.vk, ...extraEnv };
  // cold: no driver shader cache (a player's first run); warm: the cache as it is
  if (m.cache === 'cold') env.MESA_SHADER_CACHE_DISABLE = 'true';
  else delete env.MESA_SHADER_CACHE_DISABLE;
  const exe = m.be === 'wgpu' ? WGPU : GL;
  const a = [...m.run, ...(m.be === 'wgpu' && m.run[0] !== '-' ? ['--wgpu'] : [])];
  const out = execFileSync('flock', [LOCK, exe, ...a], { env, cwd: ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], timeout: 300000 });
  const line = out.split('\n').find(l => l.startsWith('BENCH_RESULT '));
  if (!line) throw new Error(`${m.key}: no BENCH_RESULT`);
  const j = JSON.parse(line.slice(13));
  // a scene that skipped draws or failed a program did not do the work
  if (j.error || j.skipped) throw new Error(`${m.key}: ${j.error || `${j.skipped} draws skipped`}`);
  const v = m.read(j);
  if (typeof v !== 'number' || !Number.isFinite(v)) throw new Error(`${m.key}: no value`);
  return { v, j };
}

const median = a => [...a].sort((x, y) => x - y)[a.length >> 1];
function measure(list, extraEnv) {
  const res = {};
  for (const m of list) {
    const runs = [];
    let last;
    for (let i = 0; i < reps; i++) { const r = runOnce(m, extraEnv); runs.push(r.v); last = r.j; }
    const rec = { value: median(runs), runs, tol: m.tol, floor: m.floor };
    for (const k of m.also || []) if (k in last) rec[k] = last[k];
    if (last.adapter) rec.adapter = last.adapter;
    else if (last.renderer) rec.renderer = last.renderer;
    res[m.key] = rec;
    process.stderr.write(`  ${m.key.padEnd(52)} ${rec.value.toFixed(3).padStart(10)} ms\n`);
  }
  return res;
}

// a regression: above base * (1 + tol) and by more than the floor
function compare(base, cur) {
  const rows = [];
  for (const [k, c] of Object.entries(cur)) {
    const b = base[k];
    if (!b) { rows.push({ k, c: c.value, status: 'new' }); continue; }
    const limit = Math.max(b.value * (1 + c.tol), b.value + c.floor);
    rows.push({ k, b: b.value, c: c.value, ratio: c.value / b.value, status: c.value > limit ? 'SLOWER' : c.value < b.value / (1 + c.tol) && b.value - c.value > c.floor ? 'faster' : 'ok' });
  }
  return rows;
}

function print(rows) {
  for (const r of rows)
    console.log(`${r.status.padEnd(7)} ${r.k.padEnd(52)} ${r.b === undefined ? ''.padStart(10) : r.b.toFixed(3).padStart(10)} -> ${r.c.toFixed(3).padStart(10)} ms${r.ratio ? `  x${r.ratio.toFixed(2)}` : ''}`);
}

for (const exe of [GL, WGPU]) if (!existsSync(exe)) throw new Error(`${exe} missing: ./build.sh and WGPU=1 ./build.sh`);
mkdirSync(join(ROOT, 'build/perf'), { recursive: true });

if (args.includes('--selftest')) {
  // A comparison nothing can fail is not a check: on each backend, a run
  // with a spin of 35% of the frame added must come back SLOWER (the
  // tolerance is 20%), and an unchanged run must not.
  let ok = true;
  for (const key of ['cpu.d1-unique-materials.gl', 'cpu.d1-unique-materials.wgpu']) {
    const list = metrics().filter(m => m.key === key);
    const base = measure(list);
    const same = compare(base, measure(list));
    const slow = compare(base, measure(list, { BENCH_SLOW_US: String(Math.round(base[key].value * 350)) }));
    print(same); print(slow);
    ok &&= same.every(r => r.status !== 'SLOWER') && slow.every(r => r.status === 'SLOWER');
  }
  console.log(ok ? 'selftest ok: unchanged runs pass, a 35% slowdown is caught on both backends' : 'selftest FAILED');
  process.exit(ok ? 0 : 1);
}

const cur = measure(metrics());
const doc = { date: new Date().toISOString(), gpu: opt('--gpu', '7600'), head: execFileSync('git', ['-C', ROOT, 'rev-parse', '--short', 'HEAD'], { encoding: 'utf8' }).trim(), metrics: cur };
writeFileSync(join(ROOT, 'build/perf/latest.json'), JSON.stringify(doc, null, 2) + '\n');
if (args.includes('--save-baseline')) {
  const all = existsSync(BASELINE) ? JSON.parse(readFileSync(BASELINE, 'utf8')) : {};
  all[doc.gpu] = doc;
  writeFileSync(BASELINE, JSON.stringify(all, null, 2) + '\n');
  console.log(`baseline for ${doc.gpu} saved (${Object.keys(cur).length} metrics)`);
} else {
  const all = existsSync(BASELINE) ? JSON.parse(readFileSync(BASELINE, 'utf8')) : {};
  const base = all[doc.gpu]?.metrics;
  if (!base) { console.log(`no baseline for ${doc.gpu}: --save-baseline`); process.exit(0); }
  const rows = compare(base, cur);
  print(rows);
  const bad = rows.filter(r => r.status === 'SLOWER').length;
  console.log(bad ? `${bad} metric(s) slower than the baseline (${all[doc.gpu].head})` : `no regressions against ${all[doc.gpu].head}`);
  process.exit(bad ? 1 : 0);
}
