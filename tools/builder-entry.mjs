// The runtime program builder's JS side (src/builder.c runs it in an embedded
// QuickJS with no host APIs): one r186 capture on the mock GPU, the same code
// as tools/gen-programs.mjs --mock-gpu, then the program model three.c draws
// from (tools/program-model.mjs). Every capture runs in a fresh runtime:
// three.js numbers its nodes per process, and the numbers are in the text.
//
// src/builder.c sets globalThis.__t3_answers (tools/gpu-answers.json) and
// drives the timers below (__t3_tick) once the job queue is empty.
import { mockWGPU, mockGL, installGPUGlobals } from './mock-gpu.mjs';
import * as core from './capture-core.mjs';
import { programModel } from './program-model.mjs';

const g = globalThis;
const timers = [];
g.setTimeout = (fn, ms, ...a) => { timers.push({ fn, a, at: ms | 0, seq: timers.length }); return timers.length; };
g.clearTimeout = () => {};
g.__t3_tick = () => {
  if (!timers.length) return false;
  timers.sort((x, y) => x.at - y.at || x.seq - y.seq);
  const t = timers.shift();
  t.fn(...t.a);
  return true;
};
const quiet = () => {};
g.console = { log: quiet, info: quiet, debug: quiet, warn: quiet, error: quiet, trace: quiet, group: quiet, groupEnd: quiet, groupCollapsed: quiet, table: quiet, time: quiet, timeEnd: quiet };
g.navigator ??= { userAgent: 'three.c', language: 'en' };
g.window ??= g;
installGPUGlobals();
for (const k of ['ImageBitmap', 'HTMLImageElement', 'HTMLCanvasElement', 'HTMLVideoElement', 'OffscreenCanvas', 'VideoFrame']) g[k] ??= class {};

// a capture as JSON: { stages: { vertex, fragment }, out }
g.__t3_capture = async (state, backend, probe) => {
  const answers = JSON.parse(g.__t3_answers);
  const THREE = await import('three.webgpu.js');
  core.init({
    THREE, TSL: null, threeVersion: THREE.REVISION === '186' ? '0.186.1' : THREE.REVISION, probe, debug: false,
    loadExtension: () => { throw new Error('extensions are not available at runtime'); },
    makeDevice: async (be, features, world) => {
      if (be === 'wgpu') {
        const m = mockWGPU(answers, world.w, world.h, features.includes('cm') ? 'compat' : 'default');
        Object.defineProperty(g.navigator, 'gpu', { value: m.gpu, configurable: true });
        let device;
        if (features.includes('cm')) {
          const ad = await g.navigator.gpu.requestAdapter({ featureLevel: 'compatibility', powerPreference: 'high-performance' });
          device = await ad.requestDevice({ requiredFeatures: [...ad.features].filter((f) => f !== 'core-features-and-limits') });
        }
        return { canvas: m.canvas, device };
      }
      const gl = mockGL(answers, world.w, world.h);
      return { canvas: gl.canvas, gl };
    },
  });
  const a = await core.capture(state, backend);
  if (a.missing && a.missing.length) throw new Error(`${state}.${backend}: properties not found: ${a.missing.join(' ')}`);
  return JSON.stringify({ stages: a.stages, out: a.out });
};
// does the primary capture need a probe world?
g.__t3_needs_probe = (primary) => JSON.parse(primary).out.uncovered.length > 0;
// the program model from a primary capture and its probe's (or null)
g.__t3_model = (state, backend, primary, probe) => {
  const a = JSON.parse(primary);
  const j = core.mergeProbe(a.out, probe ? JSON.parse(probe).out : null);
  return programModel(state, backend, j, a.stages.vertex, a.stages.fragment);
};
