// A capture (tools/capture-core.mjs, merged with its probe) as the program
// three.c draws with: the fields of src/gen/programs.h's t3_gen_program, as
// plain values. tools/emit-programs.mjs writes it as C; the runtime builder
// (src/builder.c) reads it straight from QuickJS. Plain JS, no Node.
//
// The only edit to three.js's text: r186 names instanced attributes by node
// id (nodeAttribute<n>), so the instance colour's number depends on the
// state; it is renamed instanceColor. The instance matrix columns stay
// nodeAttribute0..3, which is checked.

export const KINDS = ['basic', 'lambert', 'phong', 'standard', 'physical', 'depth', 'points', 'psprite', 'line', 'sprite', 'normal', 'output',
  'bgcube', 'bgpmrem', 'bg2d', 'pmremcube', 'pmremequi', 'pmremggx', 'pmremblur'];
// feature bits (64): the order is the C ABI of the table, append only
export const FEATURE_NAMES = ['map', 'vcol', 'fog', 'inst', 'trans', 'flat', 'rep', 'nearest', 'instbig', 'nmap', 'ao', 'emap', 'rmap', 'mmap',
  'amap', 'bump', 'spec', 'shadow', 'skin', 'morph', 'env', 'senv', 'lmap', 'noatten', 'tmlinear', 'tmreinhard', 'tmcineon', 'tmaces',
  'tmagx', 'tmneutral', 'lin', 'fog2', 'clip', 'clipi', 'atest', 'tan', 'morphn', 'dmap', 'cenv', 'refr', 'mix', 'add', 'ds', 'bs', 'cm', 'icol', 'cc', 'ccn', 'sheen', 'irid', 'aniso', 'transm'];
// backend.h's t3_bk_sample_type / t3_bk_tex_dim, by index
export const SAMPLES = ['float', 'unfilterable-float', 'depth', 'sint', 'uint'];
export const DIMS = ['2d', '2d-array', 'cube', '3d'];

// WGSL declares binding numbers; GLSL binds blocks by name at link time and
// samplers by location, so there the binding is the position in the group.
// The capture lists groups in @group order (r186's BindGroup carries no
// index); on WebGPU the declarations say which group and binding each name
// has, and the model checks the group matches the position.
function bindingNumbers(be, code, groupIndex) {
  const out = {};
  if (be !== 'wgpu') return out;
  const re = /@binding\(\s*(\d+)\s*\)\s*@group\(\s*(\d+)\s*\)\s*var(?:<[^>]*>)?\s+([A-Za-z_][A-Za-z0-9_]*)/g;
  let m;
  while ((m = re.exec(code))) if (+m[2] === groupIndex) out[m[3]] = +m[1];
  return out;
}
function groupOf(be, code, name) {
  if (be !== 'wgpu') return null;
  const m = new RegExp(`@binding\\(\\s*\\d+\\s*\\)\\s*@group\\(\\s*(\\d+)\\s*\\)\\s*var<uniform>\\s+${name}\\b`).exec(code);
  return m ? +m[1] : null;
}

// j: the capture JSON (with constants / unexplained), vert / frag: its stages,
// be: 'gl' | 'wgpu'. Throws on anything three.c cannot draw from.
export function programModel(state, be, j, vert, frag) {
  if (j.stages.includes('compute')) throw new Error(`${state}.${be}: compute stage not supported yet`);
  const attributes = j.attributes.map((a) => ({ ...a }));
  const cols = attributes.filter((a) => /^nodeAttribute\d+$/.test(a.name) && a.type === 'vec4');
  if (cols.length && (cols.length !== 4 || cols.some((a, k) => a.name !== `nodeAttribute${k}`)))
    throw new Error(`${state}.${be}: instance matrix columns are ${cols.map((a) => a.name)}`);
  for (const a of attributes) {
    if (!/^nodeAttribute\d+$/.test(a.name) || a.type === 'vec4') continue;
    if (a.type !== 'vec3' || !a.instanced) throw new Error(`${state}.${be}: unknown instanced attribute ${a.name} ${a.type}`);
    const re = new RegExp(`\\b${a.name}\\b`, 'g');
    vert = vert.replace(re, 'instanceColor');
    frag = frag.replace(re, 'instanceColor');
    a.name = 'instanceColor';
  }
  const code = vert + '\n' + frag;
  const groupNames = j.groups.map((g) => g.name);
  const groups = j.groups.map((g, gi) => {
    const declared = groupOf(be, code, g.name);
    if (declared !== null && declared !== gi) throw new Error(`${state}.${be}: group ${g.name} is @group(${declared}) but listed at ${gi}`);
    const nums = bindingNumbers(be, code, gi);
    const uniforms = g.bindings.filter((b) => b.kind === 'uniforms');
    if (uniforms.length > 1) throw new Error(`${state}.${be}: group ${g.name} has ${uniforms.length} uniform structs`);
    const textures = g.bindings.filter((b) => b.kind === 'texture');
    const samplers = g.bindings.filter((b) => b.kind === 'sampler');
    const buffers = g.bindings.filter((b) => b.kind === 'buffer');
    const other = g.bindings.filter((b) => !['uniforms', 'texture', 'sampler', 'buffer'].includes(b.kind));
    if (other.length) throw new Error(`${state}.${be}: group ${g.name} has a binding kind three.c does not know: ${other.map((b) => b.kind).join(' ')}`);
    const u = uniforms[0];
    if (be === 'wgpu' && u && nums[u.name] === undefined) throw new Error(`${state}.${be}: uniform struct ${u.name} of group ${g.name} not declared in the WGSL`);
    return {
      name: g.name, index: gi, uniformBytes: u ? u.byteLength : 0, uniformBinding: u ? (nums[u.name] ?? g.bindings.indexOf(u)) : 0,
      textures: textures.map((t) => {
        const sampler = samplers.find((s) => s.name === t.name + '_sampler');
        const sample = SAMPLES.indexOf(t.sample), dim = DIMS.indexOf(t.dimension);
        if (sample < 0 || dim < 0) throw new Error(`${state}.${be}: texture ${t.name}: ${t.sample} ${t.dimension}`);
        return { name: t.name, binding: nums[t.name] ?? g.bindings.indexOf(t), sample, dim, hasSampler: !!sampler, compare: !!(sampler && sampler.compare),
          storage: !!t.storage, source: t.source ?? '', samplerBinding: sampler ? (nums[sampler.name] ?? g.bindings.indexOf(sampler)) : 0 };
      }),
      buffers: buffers.map((b) => {
        const bname = b.shaderName ?? b.name;   // the shader's name (NodeBuffer_<id>), not the binding object's
        if (be === 'wgpu' && nums[bname] === undefined) throw new Error(`${state}.${be}: buffer ${bname} of group ${g.name} not declared in the WGSL`);
        return { name: bname, binding: nums[bname] ?? g.bindings.indexOf(b), bytes: b.byteLength ?? 0, storage: !!b.storage, source: b.source ?? null };
      }),
    };
  });
  const missing = Object.entries(j.properties).filter(([, v]) => !v).map(([k]) => k);
  if (missing.length) throw new Error(`${state}.${be}: properties without a slot: ${missing.join(' ')}`);
  const properties = Object.entries(j.properties).map(([path, v]) => {
    const gi = groupNames.indexOf(v.group);
    if (gi < 0) throw new Error(`${state}.${be}: property ${path} in unknown group ${v.group}`);
    return { path, group: gi, offset: v.byteOffset, length: v.byteLength, mat3: !!v.layout };
  });
  if (j.unexplained && j.unexplained.length) throw new Error(`${state}.${be}: ${j.unexplained.length} unexplained uniform(s): ${j.unexplained.map((u) => u.name).join(' ')}`);
  const constants = (j.constants ?? []).map((c) => {
    const gi = groupNames.indexOf(c.group);
    if (gi < 0) throw new Error(`${state}.${be}: constant in unknown group ${c.group}`);
    return { group: gi, offset: c.offset, bytes: c.bytes };   /* (bytes: hex) */
  });
  const kind = KINDS.indexOf(j.kind);
  if (kind < 0) throw new Error(`${state}: unknown kind ${j.kind}`);
  const features = j.features.map((ft) => { const b = FEATURE_NAMES.indexOf(ft); if (b < 0) throw new Error(`${state}: unknown feature ${ft}`); return b; });
  const lights = j.lights ?? [0, 0, 0, 0];
  if (lights.length !== 4 || lights.some((n) => !Number.isInteger(n) || n < 0 || n > 15)) throw new Error(`${state}: bad light vector ${JSON.stringify(j.lights)}`);
  const tpl = j.templates ?? {};
  return { state, backend: be, kind, features, lights, extension: j.extension ?? null, groups, properties, constants,
    attributes: attributes.map((a) => ({ name: a.name, type: a.type, location: a.location, instanced: !!a.instanced })),
    tpl: [tpl.bones ?? 0, tpl.morphs ?? 0, tpl.morphWidth ?? 0], cast: j.cast ?? [0, 0, 0], vertex: vert, fragment: frag, three: j.three };
}
