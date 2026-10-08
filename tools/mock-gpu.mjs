// A GPU that draws nothing, for capturing r186's programs without one.
//
// tools/gen-programs.mjs reads each program's text and every uniform byte from
// three.js's own CPU-side state, so the device under it only has to accept
// three.js's calls and answer its capability queries the way the real device
// did (which features exist, how big a uniform buffer may be, which WebGL
// extensions there are): those answers change the programs three.js writes.
//
// record: wraps a real WebGL2 context / WebGPU adapter and keeps every answer
//         (tools/gen-programs.mjs --record-gpu <file>)
// mock:   replays a recorded file (tools/gen-programs.mjs --mock-gpu <file>)

// ── recording ───────────────────────────────────────────────────────────
const recorded = { gl: { params: {}, extensions: {}, supported: null, precision: {}, attributes: null, constants: {} }, wgpu: {} };
const key = (...a) => a.map(String).join(',');

export function recordGL(gl) {
  for (const k in gl) if (typeof gl[k] === 'number' && /^[A-Z0-9_]+$/.test(k)) recorded.gl.constants[k] = gl[k];
  const wrap = (name, fn) => { const f = gl[name].bind(gl); gl[name] = (...a) => fn(f, ...a); };
  wrap('getParameter', (f, p) => { const v = f(p); recorded.gl.params[p] = plain(v); return v; });
  wrap('getExtension', (f, n) => { const v = f(n); recorded.gl.extensions[n] = v ? Object.fromEntries(Object.entries(Object.getOwnPropertyDescriptors(Object.getPrototypeOf(v) ?? {})).filter(([k]) => /^[A-Z0-9_]+$/.test(k)).map(([k]) => [k, v[k]])) : null; return v; });
  wrap('getSupportedExtensions', (f) => { const v = f(); recorded.gl.supported = v; return v; });
  wrap('getShaderPrecisionFormat', (f, s, p) => { const v = f(s, p); recorded.gl.precision[key(s, p)] = v && { rangeMin: v.rangeMin, rangeMax: v.rangeMax, precision: v.precision }; return v; });
  wrap('getContextAttributes', (f) => { const v = f(); recorded.gl.attributes = v; return v; });
  return gl;
}
function plain(v) {
  if (v === null || typeof v !== 'object') return v;
  if (ArrayBuffer.isView(v)) return { typed: v.constructor.name, values: Array.from(v) };
  return { object: true };
}
export async function recordWGPU(adapter, name) {
  const lim = {};
  for (const k in adapter.limits) lim[k] = adapter.limits[k];
  recorded.wgpu[name] = { features: [...adapter.features].sort(), limits: lim, info: adapter.info ? { vendor: adapter.info.vendor, architecture: adapter.info.architecture } : {} };
}
export function recordedData() { return recorded; }

// ── the mock ────────────────────────────────────────────────────────────
let token = 0;
const obj = (extra = {}) => Object.assign({ __mock: ++token }, extra);

// WebGL2: the recorded answers; every other call is accepted and does nothing
export function mockGL(env, width, height) {
  const g = env.gl;
  const canvas = { width, height, style: {}, addEventListener() {}, removeEventListener() {}, getBoundingClientRect: () => ({ left: 0, top: 0, width, height }) };
  const answers = {
    canvas,
    drawingBufferWidth: width, drawingBufferHeight: height,
    getParameter: (p) => { const v = g.params[p]; if (v && v.typed) return new globalThis[v.typed](v.values); if (v && v.object) return obj(); return v ?? null; },
    getExtension: (n) => (n in g.extensions && g.extensions[n] ? obj(g.extensions[n]) : null),
    getSupportedExtensions: () => g.supported ?? Object.keys(g.extensions).filter((k) => g.extensions[k]),
    getShaderPrecisionFormat: (s, p) => g.precision[key(s, p)] ?? { rangeMin: 127, rangeMax: 127, precision: 23 },
    getContextAttributes: () => g.attributes,
    isContextLost: () => false,
    getError: () => 0,
    checkFramebufferStatus: () => g.constants.FRAMEBUFFER_COMPLETE,
    getShaderParameter: () => true,
    getProgramParameter: (prog, p) => (p === g.constants.ACTIVE_UNIFORMS || p === g.constants.ACTIVE_ATTRIBUTES || p === g.constants.ACTIVE_UNIFORM_BLOCKS ? 0 : true),
    getShaderInfoLog: () => '', getProgramInfoLog: () => '',
    getUniformBlockIndex: () => 0,
    getAttribLocation: () => 0,
    getUniformLocation: () => obj(),
    fenceSync: () => obj(), clientWaitSync: () => g.constants.ALREADY_SIGNALED, getSyncParameter: () => g.constants.SIGNALED,
    getQueryParameter: () => 0,
  };
  return new Proxy(answers, {
    get(t, k) {
      if (k in t) return t[k];
      if (typeof k === 'string' && k in g.constants) return g.constants[k];
      if (typeof k === 'string' && /^(create|get)/.test(k)) return () => obj();
      if (typeof k === 'string') return () => undefined;
      return undefined;
    },
  });
}

// WebGPU: an adapter with the recorded features and limits, a device that
// accepts everything and keeps buffer contents (three.js maps a few back)
export function mockWGPU(env, name, width, height) {
  const rec = env.wgpu[name];
  const features = new Set(rec.features);
  const limits = { ...rec.limits };
  const mkTexture = (d = {}) => obj({ width: d.size?.width ?? d.size?.[0] ?? width, height: d.size?.height ?? d.size?.[1] ?? height,
    depthOrArrayLayers: d.size?.depthOrArrayLayers ?? d.size?.[2] ?? 1, format: d.format, mipLevelCount: d.mipLevelCount ?? 1, sampleCount: d.sampleCount ?? 1,
    dimension: d.dimension ?? '2d', usage: d.usage ?? 0, createView: () => obj(), destroy() {} });
  const mkBuffer = (d) => { const data = new ArrayBuffer(Math.max(4, d.size)); return obj({ size: d.size, usage: d.usage, mapState: 'unmapped', getMappedRange: (o = 0, s) => data.slice ? new Uint8Array(data, o, s ?? d.size - o).buffer.slice(o, o + (s ?? d.size - o)) : data,
    mapAsync: async () => {}, unmap() {}, destroy() {} }); };
  const pass = new Proxy({}, { get: (t, k) => (k === 'end' ? () => {} : () => undefined) });
  const encoder = () => new Proxy({ beginRenderPass: () => pass, beginComputePass: () => pass, finish: () => obj() }, { get: (t, k) => (k in t ? t[k] : () => undefined) });
  const device = new Proxy({
    features, limits, adapterInfo: rec.info,
    queue: new Proxy({ submit() {}, writeBuffer() {}, writeTexture() {}, copyExternalImageToTexture() {}, onSubmittedWorkDone: async () => {} }, { get: (t, k) => (k in t ? t[k] : () => undefined) }),
    lost: new Promise(() => {}),
    createBuffer: mkBuffer, createTexture: mkTexture, createSampler: () => obj(), createBindGroupLayout: () => obj(), createPipelineLayout: () => obj(),
    createBindGroup: () => obj(), createShaderModule: () => obj({ getCompilationInfo: async () => ({ messages: [] }) }),
    createRenderPipeline: () => obj({ getBindGroupLayout: () => obj() }), createComputePipeline: () => obj({ getBindGroupLayout: () => obj() }),
    createRenderPipelineAsync: async () => obj({ getBindGroupLayout: () => obj() }), createComputePipelineAsync: async () => obj({ getBindGroupLayout: () => obj() }),
    createCommandEncoder: encoder, createQuerySet: () => obj({ destroy() {} }), createRenderBundleEncoder: encoder,
    pushErrorScope() {}, popErrorScope: async () => null, addEventListener() {}, removeEventListener() {}, destroy() {},
  }, { get: (t, k) => (k in t ? t[k] : () => undefined) });
  const adapter = { features, limits, info: rec.info, isFallbackAdapter: false, requestDevice: async () => device, requestAdapterInfo: async () => rec.info };
  const context = { configure() {}, unconfigure() {}, getCurrentTexture: () => mkTexture({ size: { width, height }, format: 'rgba8unorm' }), getConfiguration: () => null };
  const canvas = { width, height, style: {}, getContext: (k) => (k === 'webgpu' ? context : null), addEventListener() {}, removeEventListener() {},
    getBoundingClientRect: () => ({ left: 0, top: 0, width, height }) };
  context.canvas = canvas;
  return { gpu: { requestAdapter: async () => adapter, getPreferredCanvasFormat: () => 'bgra8unorm', wgslLanguageFeatures: new Set() }, canvas, device };
}
