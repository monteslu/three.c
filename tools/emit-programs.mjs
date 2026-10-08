// Emit the captured r186 programs (tools/gen-programs.mjs, build/gen-programs/)
// as C: src/gen/programs.h, programs_gl.c (GLSL ES 3.00 text + manifests),
// programs_wgpu.c (WGSL text + manifests), programs_dfg.c (the DFG LUT), and
// programs_core_gl.c / programs_core_wgpu.c: the same table cut down to the
// programs the renderer itself draws with (output pass, backgrounds, PMREM,
// shadow casters), for a build whose materials come from a project table.
//
//   node tools/gen-programs.mjs --check && node tools/emit-programs.mjs
//
// A project's own table (tools/gen-project-table.mjs):
//   node tools/emit-programs.mjs --in <captures> --states <list file> --table <name> --out <dir>
// writes <dir>/<name>_gl.c and <name>_wgpu.c, each defining
// `const t3_gen_table <name>_gl` (or _wgpu) for t3_register_program_table.
//
// Each backend's file is self-contained (its own lookup), so a GLES build
// links only programs_gl.c. The manifests use backend.h's layout structs as
// they are, so the renderer binds from them directly.
import { readFileSync, writeFileSync, readdirSync, existsSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : null; };
const IN = resolve(opt('--in') ?? join(ROOT, 'build', 'gen-programs'));
const OUT = resolve(opt('--out') ?? join(ROOT, 'src', 'gen'));
const TABLE = opt('--table');   // a project table's name; null = three.c's own tables
if (TABLE && !/^[A-Za-z_][A-Za-z0-9_]*$/.test(TABLE)) throw new Error(`--table ${TABLE}: not a C identifier`);
const STATES_ARG = opt('--states');   // a file of state names (one per line, # comments) or a comma list

const KINDS = ['basic', 'lambert', 'phong', 'standard', 'physical', 'depth', 'points', 'psprite', 'line', 'sprite', 'normal', 'output',
  'bgcube', 'bgpmrem', 'bg2d', 'pmremcube', 'pmremequi', 'pmremggx', 'pmremblur'];
// feature bits (64): the order is the C ABI of the table, append only
const FEATURE_NAMES = ['map', 'vcol', 'fog', 'inst', 'trans', 'flat', 'rep', 'nearest', 'instbig', 'nmap', 'ao', 'emap', 'rmap', 'mmap',
  'amap', 'bump', 'spec', 'shadow', 'skin', 'morph', 'env', 'senv', 'lmap', 'noatten', 'tmlinear', 'tmreinhard', 'tmcineon', 'tmaces',
  'tmagx', 'tmneutral', 'lin', 'fog2', 'clip', 'clipi', 'atest', 'tan', 'morphn', 'dmap', 'cenv', 'refr', 'mix', 'add', 'ds', 'bs', 'cm', 'icol', 'cc', 'ccn', 'sheen', 'irid', 'aniso', 'transm'];
const FEATURE_BITS = Object.fromEntries(FEATURE_NAMES.map((n, i) => [n, 1n << BigInt(i)]));
const SAMPLE = { float: 'T3_BK_SAMPLE_FLOAT', 'unfilterable-float': 'T3_BK_SAMPLE_UNFILTERABLE_FLOAT', depth: 'T3_BK_SAMPLE_DEPTH', sint: 'T3_BK_SAMPLE_SINT', uint: 'T3_BK_SAMPLE_UINT' };
const DIM = { '2d': 'T3_BK_DIM_2D', '2d-array': 'T3_BK_DIM_2D_ARRAY', cube: 'T3_BK_DIM_CUBE', '3d': 'T3_BK_DIM_3D' };

const sym = (s) => s.replace(/[^A-Za-z0-9]/g, '_');
const cq = (s) => '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
// one C string literal per line keeps the file diffable
function cstr(s) {
  const lines = s.split('\n');
  const last = lines.pop();
  let body = lines.map((l) => '  ' + cq(l).replace(/"$/, '\\n"')).join('\n');
  body += (body ? '\n' : '') + '  ' + cq(last);
  return body;
}

const captured = [...new Set(readdirSync(IN).filter((f) => f.endsWith('.json') && f !== 'dfg_lut.json').map((f) => f.replace(/\.(gl|wgpu)\.json$/, '')))].sort();
let states = captured;
if (STATES_ARG) {
  const list = existsSync(STATES_ARG) ? readFileSync(STATES_ARG, 'utf8').split('\n').map((l) => l.replace(/#.*/, '').trim()).filter(Boolean) : STATES_ARG.split(',');
  const missing = list.filter((st) => !captured.includes(st));
  if (missing.length) throw new Error(`not captured in ${IN}: ${missing.join(' ')}`);
  states = [...new Set(list)].sort();
}
if (!states.length) throw new Error(`no captures in ${IN}; run tools/gen-programs.mjs first`);
// the programs the renderer draws with itself, whatever the materials
const CORE_KINDS = new Set(['depth', 'output', 'bgcube', 'bgpmrem', 'bg2d', 'pmremcube', 'pmremequi', 'pmremggx', 'pmremblur']);
let threeVersion = null;

function load(state, be) {
  const base = join(IN, `${state}.${be}`);
  const j = JSON.parse(readFileSync(base + '.json', 'utf8'));
  threeVersion ??= j.three;
  if (j.three !== threeVersion) throw new Error(`${state}.${be}: captured from three ${j.three}, others from ${threeVersion}`);
  let vert = readFileSync(base + '.vert', 'utf8'), frag = readFileSync(base + '.frag', 'utf8');
  if (j.stages.includes('compute')) throw new Error(`${state}.${be}: compute stage not supported by the emitter yet`);
  // r186 names instanced attributes by node id (nodeAttribute<n>): the
  // instance matrix's four vec4 columns and the instance colour (vec3) share
  // the scheme, so the colour's number depends on the state. It is renamed
  // instanceColor; the columns stay nodeAttribute0..3, which is checked.
  const cols = j.attributes.filter((a) => /^nodeAttribute\d+$/.test(a.name) && a.type === 'vec4');
  if (cols.length && (cols.length !== 4 || cols.some((a, k) => a.name !== `nodeAttribute${k}`)))
    throw new Error(`${state}.${be}: instance matrix columns are ${cols.map((a) => a.name)}`);
  for (const a of j.attributes) {
    if (!/^nodeAttribute\d+$/.test(a.name) || a.type === 'vec4') continue;
    if (a.type !== 'vec3' || !a.instanced) throw new Error(`${state}.${be}: unknown instanced attribute ${a.name} ${a.type}`);
    const re = new RegExp(`\\b${a.name}\\b`, 'g');
    vert = vert.replace(re, 'instanceColor');
    frag = frag.replace(re, 'instanceColor');
    a.name = 'instanceColor';
  }
  return { j, vert, frag };
}

// WGSL declares binding numbers; GLSL binds blocks by name at link time and
// samplers by location, so there the binding is the position in the group.
// The capture lists groups in @group order (r186's BindGroup carries no
// index); on WebGPU the declarations say which group and binding each name
// has, and the emitter checks the group matches the position.
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

function emitBackend(be, list, file, tableName) {
  const lang = be === 'gl' ? 'GLSL ES 3.00' : 'WGSL';
  let out = `/* GENERATED by tools/emit-programs.mjs from tools/gen-programs.mjs captures. Do not edit.
 *
 * The ${lang} below is what three.js ${threeVersion ?? ''}'s node renderer generates for each
 * material state (its ${be === 'gl' ? 'WebGL2 backend' : 'WebGPU backend'}), unmodified, with the binding
 * manifest the capture found. three.js is MIT licensed, Copyright 2010-2026
 * three.js authors; see LICENSE.three.js. */
#include "${TABLE ? 'gen/programs.h' : 'programs.h'}"
#include <string.h>

`;
  const entries = [];
  for (const state of list) {
    const f = join(IN, `${state}.${be}.json`);
    if (!existsSync(f)) continue;
    const { j, vert, frag } = load(state, be);
    const S = sym(state);
    out += `/* ── ${state} ── */\nstatic const char v_${S}[] =\n${cstr(vert)};\nstatic const char f_${S}[] =\n${cstr(frag)};\n`;
    // groups
    const groupNames = [];
    j.groups.forEach((g, gi) => {
      const G = `${S}_${sym(g.name)}`;
      groupNames.push(g.name);
      const code = vert + '\n' + frag;
      const declared = groupOf(be, code, g.name);
      if (declared !== null && declared !== gi) throw new Error(`${state}.${be}: group ${g.name} is @group(${declared}) but listed at ${gi}`);
      g.index = gi;
      const nums = bindingNumbers(be, code, gi);
      const uniforms = g.bindings.filter((b) => b.kind === 'uniforms');
      if (uniforms.length > 1) throw new Error(`${state}.${be}: group ${g.name} has ${uniforms.length} uniform structs`);
      const textures = g.bindings.filter((b) => b.kind === 'texture');
      const samplers = g.bindings.filter((b) => b.kind === 'sampler');
      const buffers = g.bindings.filter((b) => b.kind === 'buffer');
      const other = g.bindings.filter((b) => !['uniforms', 'texture', 'sampler', 'buffer'].includes(b.kind));
      if (other.length) throw new Error(`${state}.${be}: group ${g.name} has a binding kind the emitter does not know: ${other.map((b) => b.kind).join(' ')}`);
      if (textures.length) {
        out += `static const t3_bk_texture_layout t_${G}[] = {\n`;
        for (const t of textures) {
          const sampler = samplers.find((s) => s.name === t.name + '_sampler');
          const binding = nums[t.name] ?? g.bindings.indexOf(t);
          const sbinding = sampler ? (nums[sampler.name] ?? g.bindings.indexOf(sampler)) : 0;
          out += `  { ${cq(t.name)}, ${binding}, ${SAMPLE[t.sample]}, ${DIM[t.dimension]}, ${sampler ? 'true' : 'false'}, ${sampler && sampler.compare ? 'true' : 'false'}, ${t.storage ? 'true' : 'false'}, ${cq(t.source ?? '')}, ${sbinding} },\n`;
        }
        out += `};\n`;
      }
      const u = uniforms[0];
      const ub = u ? (nums[u.name] ?? g.bindings.indexOf(u)) : 0;
      if (be === 'wgpu' && u && nums[u.name] === undefined) throw new Error(`${state}.${be}: uniform struct ${u.name} of group ${g.name} not declared in the WGSL`);
      if (buffers.length) {
        out += `static const t3_bk_buffer_layout b_${G}[] = {\n`;
        for (const b of buffers) {
          const bname = b.shaderName ?? b.name;   // the shader's name (NodeBuffer_<id>), not the binding object's
          if (be === 'wgpu' && nums[bname] === undefined) throw new Error(`${state}.${be}: buffer ${bname} of group ${g.name} not declared in the WGSL`);
          out += `  { ${cq(bname)}, ${nums[bname] ?? g.bindings.indexOf(b)}, ${b.byteLength ?? 0}, ${b.storage ? 'true' : 'false'}, ${b.source ? cq(b.source) : 'NULL'} },\n`;
        }
        out += `};\n`;
      }
      out += `static const t3_bk_group_layout g_${G} = { ${cq(g.name)}, ${g.index}, ${u ? u.byteLength : 0}, ${ub}, ${textures.length ? `t_${G}` : 'NULL'}, ${textures.length}, ${buffers.length ? `b_${G}` : 'NULL'}, ${buffers.length} };\n`;
    });
    out += `static const t3_bk_group_layout *const groups_${S}[] = { ${j.groups.map((g) => `&g_${S}_${sym(g.name)}`).join(', ')} };\n`;
    // properties: name -> group index, byte offset, byte length
    const props = Object.entries(j.properties).filter(([, v]) => v);
    const missing = Object.entries(j.properties).filter(([, v]) => !v).map(([k]) => k);
    if (missing.length) throw new Error(`${state}.${be}: properties without a slot: ${missing.join(' ')}`);
    out += `static const t3_gen_property p_${S}[] = {\n`;
    for (const [path, v] of props) {
      const gi = groupNames.indexOf(v.group);
      if (gi < 0) throw new Error(`${state}.${be}: property ${path} in unknown group ${v.group}`);
      out += `  { ${cq(path)}, ${gi}, ${v.byteOffset}, ${v.byteLength}, ${v.layout ? 'true' : 'false'} },\n`;
    }
    out += `};\n`;
    // constants: uniforms nothing varies, with the bytes r186 uploaded
    const consts = j.constants ?? [];
    if (j.unexplained && j.unexplained.length) throw new Error(`${state}.${be}: ${j.unexplained.length} unexplained uniform(s): ${j.unexplained.map((u) => u.name).join(' ')}`);
    if (consts.length) {
      out += `static const uint8_t kb_${S}[] = {`;
      const rows = [];
      for (const c of consts) { const bytes = c.bytes.match(/../g).map((h) => '0x' + h); rows.push({ c, at: rows.reduce((n, r) => n + r.bytes.length, 0), bytes }); }
      out += rows.flatMap((r) => r.bytes).join(', ') + ` };\n`;
      out += `static const t3_gen_constant k_${S}[] = {\n`;
      for (const r of rows) {
        const gi = groupNames.indexOf(r.c.group);
        if (gi < 0) throw new Error(`${state}.${be}: constant in unknown group ${r.c.group}`);
        out += `  { ${gi}, ${r.c.offset}, ${r.bytes.length}, kb_${S} + ${r.at} },\n`;
      }
      out += `};\n`;
    }
    // attributes
    out += `static const t3_gen_attribute a_${S}[] = {\n`;
    for (const a of j.attributes) out += `  { ${cq(a.name)}, ${cq(a.type)}, ${a.location}, ${a.instanced ? 'true' : 'false'} },\n`;
    out += `};\n\n`;
    const kind = KINDS.indexOf(j.kind);
    if (kind < 0) throw new Error(`${state}: unknown kind ${j.kind}`);
    let bits = 0n;
    for (const ft of j.features) { if (!(ft in FEATURE_BITS)) throw new Error(`${state}: unknown feature ${ft}`); bits |= FEATURE_BITS[ft]; }
    const lights = j.lights ?? [0, 0, 0, 0];
    if (lights.length !== 4 || lights.some((n) => !Number.isInteger(n) || n < 0 || n > 15)) throw new Error(`${state}: bad light vector ${JSON.stringify(j.lights)}`);
    const tpl = j.templates ?? {};
    entries.push({ state, S, kind, bits, lights, extension: j.extension ?? null, nGroups: j.groups.length, nProps: props.length, nAttrs: j.attributes.length, nConsts: consts.length,
      tpl: [tpl.bones ?? 0, tpl.morphs ?? 0, tpl.morphWidth ?? 0], cast: j.cast ?? [0, 0, 0] });
  }
  const BE = be === 'gl' ? 'T3_GEN_GL' : 'T3_GEN_WGPU';
  out += `static const t3_gen_program programs[] = {\n`;
  for (const e of entries) out += `  { ${cq(e.state)}, ${KINDS[e.kind].toUpperCase().replace(/^/, 'T3_GEN_')}, ${e.bits}ull, { ${e.lights.join(', ')} }, ${e.extension ? cq(e.extension) : 'NULL'}, ${BE}, groups_${e.S}, ${e.nGroups}, p_${e.S}, ${e.nProps}, a_${e.S}, ${e.nAttrs}, ${e.nConsts ? `k_${e.S}` : 'NULL'}, ${e.nConsts}, { ${e.tpl.join(', ')} }, { ${e.cast.join(', ')} }, v_${e.S}, f_${e.S} },\n`;
  out += `};
const t3_gen_table ${tableName} = { programs, ${entries.length} };
`;
  writeFileSync(join(OUT, file), out);
  return { n: entries.length, kb: out.length / 1024 };
}

const header = () => `/* GENERATED by tools/emit-programs.mjs. Do not edit.
 *
 * The programs three.js ${threeVersion}'s node renderer generates per material
 * state, for both backends, with their binding manifests (src/gen/programs_gl.c,
 * programs_wgpu.c) and the DFG LUT the standard / physical programs sample
 * (programs_dfg.c). A backend links only its own file. */
#ifndef T3_GEN_PROGRAMS_H
#define T3_GEN_PROGRAMS_H
#include <stdbool.h>
#include <stdint.h>
#include "../backend.h"

typedef enum { T3_GEN_GL = 0, T3_GEN_WGPU = 1 } t3_gen_backend;
typedef enum { ${KINDS.map((k) => 'T3_GEN_' + k.toUpperCase()).join(', ')} } t3_gen_kind;
/* feature bits of a state (the support table in tools/gen-programs.mjs) */
${Object.entries(FEATURE_BITS).map(([k, v]) => `#define T3_GEN_${k.toUpperCase()} ${v}ull`).join('\n')}
/* the feature names by bit, as in state names */
#define T3_GEN_FEATURE_NAMES { ${FEATURE_NAMES.map((n) => `"${n}"`).join(', ')} }

/* where a renderer-side property lives in the program's uniform data:
 * "material.color" -> group (index into groups), byte offset and length;
 * std140_mat3: three vec3 columns at 16-byte stride (length 44) */
typedef struct { const char *path; uint8_t group; uint16_t offset, length; bool std140_mat3; } t3_gen_property;
typedef struct { const char *name, *type; uint8_t location; bool instanced; } t3_gen_attribute;
/* a uniform nothing varies (a flag, an identity matrix, a default): the bytes
 * r186 uploaded, written once when the program is built */
typedef struct { uint8_t group; uint16_t offset, length; const uint8_t *bytes; } t3_gen_constant;
typedef struct {
  const char *state;            /* "standard+map+fog+l2200" */
  t3_gen_kind kind;
  uint64_t features;            /* T3_GEN_* bits */
  uint8_t lights[4];            /* directional, point, spot, hemisphere counts the program was generated for (lit kinds also have one ambient) */
  const char *extension;        /* the generator extension applied (tools/extensions/<name>.mjs), or NULL */
  t3_gen_backend backend;
  const t3_bk_group_layout *const *groups; uint8_t n_groups;
  const t3_gen_property *properties; uint16_t n_properties;
  const t3_gen_attribute *attributes; uint8_t n_attributes;   /* in location order */
  const t3_gen_constant *constants; uint8_t n_constants;
  /* template parameters baked into the text at capture (0 = none): bone count,
   * morph target count, morph texture row width; gen_program.c substitutes the
   * object's own values */
  uint16_t tpl[3];
  uint8_t cast[3];              /* how many directional, point, spot lights cast (the first ones by id) */
  const char *vertex, *fragment;
} t3_gen_program;

/* a table of programs: three.c's own (programs_gl.c, or programs_core_gl.c
 * in a core-only build) and any a project registers
 * (t3_register_program_table, tools/gen-project-table.mjs) */
typedef struct t3_gen_table { const t3_gen_program *programs; unsigned count; } t3_gen_table;
extern const t3_gen_table t3_gen_base_gl, t3_gen_base_wgpu;

/* r186's precomputed DFG LUT: 16 x 16, RG16F (scale, bias), linear, clamp */
#define T3_GEN_DFG_LUT_SIZE 16
extern const uint16_t t3_gen_dfg_lut[T3_GEN_DFG_LUT_SIZE * T3_GEN_DFG_LUT_SIZE * 2];

#endif
`;

if (TABLE) {
  // a project's table: its programs only, under its own symbol (the header and DFG LUT are three.c's)
  const gl = emitBackend('gl', states, `${TABLE}_gl.c`, `${TABLE}_gl`);
  const wgpu = emitBackend('wgpu', states, `${TABLE}_wgpu.c`, `${TABLE}_wgpu`);
  console.log(`wrote ${OUT}/${TABLE}_gl.c (${gl.n} states, ${gl.kb.toFixed(0)} KB), ${TABLE}_wgpu.c (${wgpu.n} states, ${wgpu.kb.toFixed(0)} KB)`);
  process.exit(0);
}
const core = states.filter((st) => CORE_KINDS.has(st.split('+')[0]));
const gl = emitBackend('gl', states, 'programs_gl.c', 't3_gen_base_gl');
const wgpu = emitBackend('wgpu', states, 'programs_wgpu.c', 't3_gen_base_wgpu');
const cgl = emitBackend('gl', core, 'programs_core_gl.c', 't3_gen_base_gl');
const cwgpu = emitBackend('wgpu', core, 'programs_core_wgpu.c', 't3_gen_base_wgpu');
writeFileSync(join(OUT, 'programs.h'), header());
const lut = JSON.parse(readFileSync(join(IN, 'dfg_lut.json'), 'utf8'));
if (lut.width !== 16 || lut.height !== 16 || lut.data.length !== 512) throw new Error('dfg_lut.json: not 16x16 RG');
let dfg = `/* GENERATED by tools/emit-programs.mjs from three.js ${lut.three}'s DFGLUT.js. Do not edit.
 * three.js is MIT licensed, Copyright 2010-2026 three.js authors; see LICENSE.three.js. */
#include "programs.h"

const uint16_t t3_gen_dfg_lut[T3_GEN_DFG_LUT_SIZE * T3_GEN_DFG_LUT_SIZE * 2] = {
`;
for (let i = 0; i < 512; i += 16) dfg += '  ' + lut.data.slice(i, i + 16).join(', ') + ',\n';
dfg += '};\n';
writeFileSync(join(OUT, 'programs_dfg.c'), dfg);
console.log(`wrote src/gen/programs.h, programs_gl.c (${gl.n} states, ${gl.kb.toFixed(0)} KB), programs_wgpu.c (${wgpu.n} states, ${wgpu.kb.toFixed(0)} KB), programs_core_{gl,wgpu}.c (${cgl.n} states, ${cgl.kb.toFixed(0)} + ${cwgpu.kb.toFixed(0)} KB), programs_dfg.c (three ${threeVersion})`);
