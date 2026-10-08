// Capture the programs three.js's node renderer generates, both languages, for
// the material states three.c supports (the support table below), with a
// manifest of every binding the program needs and which three.js property
// feeds each uniform byte range.
//
//   node tools/gen-programs.mjs [--states basic,standard+map] [--backend wgpu|gl|both]
//                               [--out build/gen-programs] [--check]
//
// three.js is pinned (THREE_VERSION); the package is fetched once into
// build/three-r<n>. The WebGPU backend runs on webgpu-node (native-dawn), the
// WebGL2 one on webgl-node (WEBGL_NODE=<checkout> for an unreleased fix).
// Output per state and backend: <state>.<be>.vert / .frag (the shader text
// exactly as three.js compiled it) and <state>.<be>.json (bind groups, uniform
// structs with byte lengths, textures, samplers, attributes, and the property
// map found by FINGERPRINTING: every supported property is given a sentinel
// value nothing else has, the uniform bytes are searched for it). --check runs
// everything twice and fails on any difference (the generator must be
// deterministic).
import { readFileSync, writeFileSync, existsSync, mkdirSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { execFileSync, spawnSync } from 'node:child_process';
import { tmpdir } from 'node:os';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
export const THREE_VERSION = '0.186.1';
const REV = 'r' + THREE_VERSION.split('.')[1];

const opt = { states: 'all', backend: 'both', out: join(ROOT, 'build', 'gen-programs'), check: false, single: false };
for (let i = 2; i < process.argv.length; i++) {
  const a = process.argv[i], v = () => process.argv[++i];
  if (a === '--states') opt.states = v();
  else if (a === '--backend') opt.backend = v();
  else if (a === '--out') opt.out = resolve(v());
  else if (a === '--check') opt.check = true;
  else if (a === '--extend') (opt.extend ??= []).push(resolve(v()));
  else if (a === '--single') opt.single = true;   // one capture in this process (the parent spawns one per state and backend)
  // --record-gpu <file>: keep the real devices' capability answers (tools/mock-gpu.mjs);
  // --mock-gpu <file>: capture on a mock device answering from such a file (no GPU)
  else if (a === '--record-gpu') process.env.GEN_GPU = 'record:' + resolve(v());
  else if (a === '--mock-gpu') process.env.GEN_GPU = 'mock:' + resolve(v());
  else throw new Error(`unknown option ${a}`);
}

// ── three.js, the host libraries ─────────────────────────────────────
function threePkg() {
  const dir = join(ROOT, 'build', 'three-' + REV), pkg = join(dir, 'package');
  if (!existsSync(join(pkg, 'build', 'three.webgpu.js'))) {
    mkdirSync(dir, { recursive: true });
    execFileSync('npm', ['pack', 'three@' + THREE_VERSION, '--silent'], { cwd: dir, stdio: 'ignore' });
    execFileSync('tar', ['-xzf', `three-${THREE_VERSION}.tgz`], { cwd: dir });
  }
  return pkg;
}
function findModule(name, env, candidates) {
  const dirs = [process.env[env], ...candidates].filter(Boolean);
  const d = dirs.find((x) => existsSync(join(x, 'index.mjs')));
  if (!d) throw new Error(`${name} not found; set ${env}=<dir> (looked in ${dirs.join(', ')})`);
  return pathToFileURL(join(d, 'index.mjs')).href;
}
const WASMCART = resolve(process.env.WASMCART || join(ROOT, '..', 'wasmcart'));
const webgpuNodeUrl = () => findModule('webgpu-node', 'WEBGPU_NODE', [join(ROOT, 'node_modules', 'webgpu-node'), join(WASMCART, 'node_modules', 'webgpu-node')]);
const webglNodeUrl = () => findModule('webgl-node', 'WEBGL_NODE', [join(ROOT, 'node_modules', 'webgl-node'), join(WASMCART, 'node_modules', 'webgl-node')]);

// browser-isms the node renderer touches on Node
globalThis.requestAnimationFrame ??= () => 0;
globalThis.cancelAnimationFrame ??= () => {};
globalThis.self ??= globalThis;
for (const k of ['ImageBitmap', 'HTMLImageElement', 'HTMLCanvasElement', 'HTMLVideoElement', 'OffscreenCanvas', 'VideoFrame']) globalThis[k] ??= class {};

const THREE = await import(pathToFileURL(join(threePkg(), 'build', 'three.webgpu.js')).href);
const TSL_MODULE = import(pathToFileURL(join(threePkg(), 'build', 'three.tsl.js')).href);
const core = await import('./capture-core.mjs');
const { parseState, allStates, capture } = core;
// the project's own states (what its scenes need beyond the base table), one
// per line, # comments: tools/gen-states.txt
const EXTRA = existsSync(join(ROOT, 'tools', 'gen-states.txt'))
  ? readFileSync(join(ROOT, 'tools', 'gen-states.txt'), 'utf8').split('\n').map((l) => l.replace(/#.*/, '').trim()).filter(Boolean) : [];
const states = opt.states === 'all' ? [...new Set([...allStates(), ...EXTRA])] : opt.states.split(',');
for (const st of states) parseState(st);   // every name parses before any capture runs
async function loadExtension(name) {
  const cands = [...(opt.extend || []), join(ROOT, 'tools', 'extensions', name + '.mjs')].filter((f) => existsSync(f) && (f.endsWith('/' + name + '.mjs') || f.endsWith(name + '.mjs')));
  if (!cands.length) throw new Error(`extension ${name}: no module (tools/extensions/${name}.mjs or --extend <path>)`);
  const m = await import(pathToFileURL(cands[0]).href);
  if (typeof m.apply !== 'function') throw new Error(`extension ${name}: the module exports no apply()`);
  return m;
}


// devices: real (webgpu-node, webgl-node), recorded (--record-gpu) or mocked (--mock-gpu)
const GPU_MODE = (process.env.GEN_GPU || '').split(':')[0] || null, GPU_FILE = (process.env.GEN_GPU || '').slice((GPU_MODE ?? '').length + 1);
const mockGpu = GPU_MODE ? await import('./mock-gpu.mjs') : null;
async function makeDevice(backend, features, world) {
  if (backend === 'wgpu') {
    let canvas;
    if (GPU_MODE === 'mock') {
      const m = mockGpu.mockWGPU(JSON.parse(readFileSync(GPU_FILE, 'utf8')), world.w, world.h, features.includes('cm') ? 'compat' : 'default');
      mockGpu.installGPUGlobals();
      globalThis.navigator ??= {};
      Object.defineProperty(globalThis.navigator, 'gpu', { value: m.gpu, configurable: true });
      canvas = m.canvas;
    } else {
      const wn = await import(webgpuNodeUrl());
      wn.installGlobals();
      canvas = wn.createCanvas(world.w, world.h);
    }
    let device;
    if (features.includes('cm')) {
      const ad = await navigator.gpu.requestAdapter({ featureLevel: 'compatibility', powerPreference: 'high-performance' });
      device = await ad.requestDevice({ requiredFeatures: [...ad.features].filter((f) => f !== 'core-features-and-limits') });
    }
    return { canvas, device };
  }
  if (GPU_MODE === 'mock') {
    const gl = mockGpu.mockGL(JSON.parse(readFileSync(GPU_FILE, 'utf8')), world.w, world.h);
    return { canvas: gl.canvas, gl };
  }
  const { createWebGL2Context } = await import(webglNodeUrl());
  const made = createWebGL2Context(world.w, world.h);
  if (GPU_MODE === 'record') mockGpu.recordGL(made.gl);
  return { canvas: made.canvas, gl: made.gl };
}
core.init({ THREE, TSL: TSL_MODULE, threeVersion: THREE_VERSION, probe: process.env.GEN_PROBE === '1', debug: !!process.env.GEN_DEBUG, loadExtension, makeDevice,
  afterInit: GPU_MODE === 'record' ? (r, be, features) => (be === 'wgpu' ? mockGpu.recordWGPU(r.backend.device, features.includes('cm') ? 'compat' : 'default') : null) : null });

// ── run ──────────────────────────────────────────────────────────────
mkdirSync(opt.out, { recursive: true });
const backends = opt.backend === 'both' ? ['wgpu', 'gl'] : [opt.backend];
let failures = 0;
const log = (s) => process.stdout.write(s + '\n');
// Every (state, backend) is captured in a FRESH process: the node renderer
// numbers its uniforms per process, so the output must not depend on what
// was captured before it. The parent only orchestrates.
const self = fileURLToPath(import.meta.url);
function captureInChild(state, be, out, extraEnv = {}) {
  const r = spawnSync(process.execPath, [self, '--states', state, '--backend', be, '--out', out, '--single'], { encoding: 'utf8', env: { ...process.env, ...extraEnv, GEN_ALLOW_UNCOVERED: '1' }, maxBuffer: 64 << 20 });
  if (r.status !== 0) throw new Error(`capture ${state} ${be} failed: ${(r.stderr || '').slice(-500)}`);
  return (r.stdout || '').split('\n').filter((l) => l.startsWith(state + ' ')).join('\n');
}
// capture + the probe world: uncovered uniforms whose bytes are the same in both
// worlds are constants (recorded with their bytes), the rest must be explained
function captureFull(state, be, out) {
  const line = captureInChild(state, be, out);
  const jf = join(out, `${state}.${be}.json`);
  const j = JSON.parse(readFileSync(jf, 'utf8'));
  let pj = null;
  if (j.uncovered.length) {
    const tmp = join(tmpdir(), `gen-programs-probe-${process.pid}`);
    mkdirSync(tmp, { recursive: true });
    captureInChild(state, be, tmp, { GEN_PROBE: '1' });
    pj = JSON.parse(readFileSync(join(tmp, `${state}.${be}.json`), 'utf8'));
  }
  core.mergeProbe(j, pj);
  writeFileSync(jf, JSON.stringify(j, null, 2) + '\n');
  if (j.unexplained.length && !process.env.GEN_ALLOW_UNCOVERED) throw new Error(`${state}/${be}: uniforms that change with the renderer and have no property: ${j.unexplained.map((u) => `${u.group}.${u.name}:${u.type}@${u.offset}`).join(' ')}`);
  return line + (j.unexplained.length ? ` (UNEXPLAINED: ${j.unexplained.map((u) => u.group + '.' + u.name).join(' ')})` : '') + (j.constants.length ? ` [${j.constants.length} constants]` : '');
}
for (const state of states) for (const be of backends) {
  const base = join(opt.out, `${state}.${be}`);
  const files = {};
  if (!opt.single) {
    const line = captureFull(state, be, opt.out);
    log(line);
    if (/missing:|UNEXPLAINED/.test(line)) failures++;
    for (const suffix of ['vert', 'frag', 'json']) if (existsSync(`${base}.${suffix}`)) files[`${base}.${suffix}`] = readFileSync(`${base}.${suffix}`, 'utf8');
  } else {
    const a = await capture(state, be);
    if (GPU_MODE === 'record') await mockGpu.saveRecorded(GPU_FILE);
    for (const [stage, code] of Object.entries(a.stages)) files[`${base}.${stage === 'vertex' ? 'vert' : stage === 'fragment' ? 'frag' : stage}`] = code;
    files[`${base}.json`] = JSON.stringify(a.out, null, 2) + '\n';
    for (const [f, c] of Object.entries(files)) writeFileSync(f, c);
    const nu = a.out.groups.reduce((n, g) => n + g.bindings.length, 0);
    log(`${state.padEnd(26)} ${be.padEnd(4)} stages ${a.out.stages.join('+')}, ${a.out.groups.length} groups / ${nu} bindings, props ${Object.keys(a.out.properties).length - a.missing.length}/${Object.keys(a.out.properties).length} found${a.missing.length ? ' (missing: ' + a.missing.join(' ') + ')' : ''}`);
    if (a.missing.length) failures++;
    continue;
  }
  if (opt.check) {
    // a second capture in a FRESH process (the node renderer numbers its
    // uniforms per process, so two captures in one process differ by design)
    const tmp = join(tmpdir(), `gen-programs-check-${process.pid}`);
    mkdirSync(tmp, { recursive: true });
    captureFull(state, be, tmp);
    for (const f of Object.keys(files)) {
      const g = join(tmp, f.slice(opt.out.length + 1));
      if (!existsSync(g) || readFileSync(g, 'utf8') !== files[f]) { log(`NONDETERMINISTIC ${state} ${be}: ${f.slice(opt.out.length + 1)}`); failures++; }
    }
  }
}
// the DFG LUT (r186's precomputed BRDF scale/bias, 16x16 RG16F, bound by the
// standard/physical programs as "dfg_lut"): its data is a literal in the
// package source, copied out as is
if (!opt.single) {
  const src = readFileSync(join(threePkg(), 'src', 'nodes', 'functions', 'BSDF', 'DFGLUT.js'), 'utf8');
  const m = /new Uint16Array\(\s*\[([\s\S]*?)\]\s*\)/.exec(src);
  if (!m) { log('DFG LUT literal not found in DFGLUT.js'); failures++; }
  else {
    const data = m[1].split(',').map((x) => x.trim()).filter(Boolean).map(Number);
    if (data.length !== 16 * 16 * 2 || data.some((x) => !Number.isInteger(x) || x < 0 || x > 0xffff)) { log(`DFG LUT: unexpected data (${data.length} values)`); failures++; }
    else writeFileSync(join(opt.out, 'dfg_lut.json'), JSON.stringify({ three: THREE_VERSION, width: 16, height: 16, format: 'RG16F', filter: 'linear', wrap: 'clamp', data }) + '\n');
  }
}
log(failures ? `${failures} problem(s)` : 'ok');
process.exit(failures ? 1 : 0);
