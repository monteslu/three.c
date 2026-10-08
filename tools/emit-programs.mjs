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
import { deflateSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { programModel, KINDS, FEATURE_NAMES } from './program-model.mjs';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : null; };
const IN = resolve(opt('--in') ?? join(ROOT, 'build', 'gen-programs'));
const OUT = resolve(opt('--out') ?? join(ROOT, 'src', 'gen'));
const TABLE = opt('--table');   // a project table's name; null = three.c's own tables
if (TABLE && !/^[A-Za-z_][A-Za-z0-9_]*$/.test(TABLE)) throw new Error(`--table ${TABLE}: not a C identifier`);
const STATES_ARG = opt('--states');   // a file of state names (one per line, # comments) or a comma list

const FEATURE_BITS = Object.fromEntries(FEATURE_NAMES.map((n, i) => [n, 1n << BigInt(i)]));
const SAMPLE = ['T3_BK_SAMPLE_FLOAT', 'T3_BK_SAMPLE_UNFILTERABLE_FLOAT', 'T3_BK_SAMPLE_DEPTH', 'T3_BK_SAMPLE_SINT', 'T3_BK_SAMPLE_UINT'];
const DIM = ['T3_BK_DIM_2D', 'T3_BK_DIM_2D_ARRAY', 'T3_BK_DIM_CUBE', 'T3_BK_DIM_3D'];

const sym = (s) => s.replace(/[^A-Za-z0-9]/g, '_');
const cq = (s) => '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';

// The shader text, packed: a table's ~1,900 shaders are ~30 MB of text
// that differs by a few lines per feature, so each backend stores its
// distinct lines once and each shader as the list of its lines, deflated
// together (gen_program.c's t3_gen_source inflates it on first use).
// Before deflate: u32 n_lines, n_shaders, lines_bytes; the lines, each ending
// in '\n'; then per shader a varint line count and varint zigzag deltas of
// line indices (from 0 at each shader).
function packText(shaders) {
  const index = new Map(), lines = [];
  const refs = [];
  const varint = (v) => { do { let b = v & 127; v >>>= 7; refs.push(v ? b | 128 : b); } while (v); };
  for (const text of shaders) {
    const ls = text.split('\n');
    varint(ls.length);
    let prev = 0;
    for (const l of ls) {
      let i = index.get(l);
      if (i === undefined) { i = lines.length; lines.push(l); index.set(l, i); }
      const d = i - prev; prev = i;
      varint(((d << 1) ^ (d >> 31)) >>> 0);
    }
  }
  const lineBytes = Buffer.from(lines.map((l) => l + '\n').join(''), 'utf8');
  const head = Buffer.alloc(12);
  head.writeUInt32LE(lines.length, 0); head.writeUInt32LE(shaders.length, 4); head.writeUInt32LE(lineBytes.length, 8);
  const raw = Buffer.concat([head, lineBytes, Buffer.from(refs)]);
  return { z: deflateSync(raw, { level: 9 }), raw: raw.length };
}
function cbytes(buf) {
  let out = '';
  for (let i = 0; i < buf.length; i += 32) out += '  ' + [...buf.subarray(i, i + 32)].join(',') + ',\n';
  return out;
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
  return programModel(state, be, j, readFileSync(base + '.vert', 'utf8'), readFileSync(base + '.frag', 'utf8'));
}

function emitBackend(be, list, file, tableName, guard = null) {
  const lang = be === 'gl' ? 'GLSL ES 3.00' : 'WGSL';
  let out = `/* GENERATED by tools/emit-programs.mjs from tools/gen-programs.mjs captures. Do not edit.
 *
 * The ${lang} packed below (text_z) is what three.js @THREE@'s node renderer generates for each
 * material state (its ${be === 'gl' ? 'WebGL2 backend' : 'WebGPU backend'}), unmodified, with the binding
 * manifest the capture found. three.js is MIT licensed, Copyright 2010-2026
 * three.js authors; see LICENSE.three.js. */
#include "${TABLE ? 'gen/programs.h' : 'programs.h'}"
#include <string.h>
${guard ? `/* the ${guard === 'ifdef' ? 'core' : 'full'} table: T3_TABLE_CORE picks the core one (a build that compiles
 * every file in src/gen gets exactly one base table) */
#${guard} T3_TABLE_CORE
` : ''}
`;
  const entries = [];
  const shaders = [], shaderIndex = new Map();
  const shaderOf = (text) => { let i = shaderIndex.get(text); if (i === undefined) { i = shaders.length; shaders.push(text); shaderIndex.set(text, i); } return i; };
  for (const state of list) {
    if (!existsSync(join(IN, `${state}.${be}.json`))) continue;
    const m = load(state, be);
    const S = sym(state);
    out += `/* ── ${state} ── */\n`;
    for (const g of m.groups) {
      const G = `${S}_${sym(g.name)}`;
      if (g.textures.length) {
        out += `static const t3_bk_texture_layout t_${G}[] = {\n`;
        for (const t of g.textures)
          out += `  { ${cq(t.name)}, ${t.binding}, ${SAMPLE[t.sample]}, ${DIM[t.dim]}, ${t.hasSampler}, ${t.compare}, ${t.storage}, ${cq(t.source)}, ${t.samplerBinding} },\n`;
        out += `};\n`;
      }
      if (g.buffers.length) {
        out += `static const t3_bk_buffer_layout b_${G}[] = {\n`;
        for (const b of g.buffers) out += `  { ${cq(b.name)}, ${b.binding}, ${b.bytes}, ${b.storage}, ${b.source ? cq(b.source) : 'NULL'} },\n`;
        out += `};\n`;
      }
      out += `static const t3_bk_group_layout g_${G} = { ${cq(g.name)}, ${g.index}, ${g.uniformBytes}, ${g.uniformBinding}, ${g.textures.length ? `t_${G}` : 'NULL'}, ${g.textures.length}, ${g.buffers.length ? `b_${G}` : 'NULL'}, ${g.buffers.length} };\n`;
    }
    out += `static const t3_bk_group_layout *const groups_${S}[] = { ${m.groups.map((g) => `&g_${S}_${sym(g.name)}`).join(', ')} };\n`;
    out += `static const t3_gen_property p_${S}[] = {\n`;
    for (const p of m.properties) out += `  { ${cq(p.path)}, ${p.group}, ${p.offset}, ${p.length}, ${p.mat3} },\n`;
    out += `};\n`;
    // constants: uniforms nothing varies, with the bytes r186 uploaded
    if (m.constants.length) {
      const rows = [];
      for (const c of m.constants) rows.push({ c, at: rows.reduce((n, r) => n + r.bytes.length, 0), bytes: c.bytes.match(/../g).map((h) => '0x' + h) });
      out += `static const uint8_t kb_${S}[] = {` + rows.flatMap((r) => r.bytes).join(', ') + ` };\n`;
      out += `static const t3_gen_constant k_${S}[] = {\n`;
      for (const r of rows) out += `  { ${r.c.group}, ${r.c.offset}, ${r.bytes.length}, kb_${S} + ${r.at} },\n`;
      out += `};\n`;
    }
    out += `static const t3_gen_attribute a_${S}[] = {\n`;
    for (const a of m.attributes) out += `  { ${cq(a.name)}, ${cq(a.type)}, ${a.location}, ${a.instanced} },\n`;
    out += `};\n\n`;
    const bits = m.features.reduce((acc, b) => acc | (1n << BigInt(b)), 0n);
    entries.push({ m, S, bits, v: shaderOf(m.vertex), f: shaderOf(m.fragment) });
  }
  const BE = be === 'gl' ? 'T3_GEN_GL' : 'T3_GEN_WGPU';
  const packed = packText(shaders);
  out += `/* ${shaders.length} distinct shaders, ${(packed.raw / 1024).toFixed(0)} KB packed, deflated (packText) */\nstatic const uint8_t text_z[] = {\n${cbytes(packed.z)}};\nstatic t3_gen_text_cache text_cache;\nstatic const t3_gen_text text = { text_z, ${packed.z.length}, ${packed.raw}, &text_cache };\n\n`;
  out += `static const t3_gen_program programs[] = {\n`;
  for (const { m, S, bits, v, f } of entries) out += `  { ${cq(m.state)}, T3_GEN_${KINDS[m.kind].toUpperCase()}, ${bits}ull, { ${m.lights.join(', ')} }, ${m.extension ? cq(m.extension) : 'NULL'}, ${BE}, groups_${S}, ${m.groups.length}, p_${S}, ${m.properties.length}, a_${S}, ${m.attributes.length}, ${m.constants.length ? `k_${S}` : 'NULL'}, ${m.constants.length}, { ${m.tpl.join(', ')} }, { ${m.cast.join(', ')} }, NULL, NULL, &text, ${v}, ${f} },\n`;
  out += `};
const t3_gen_table ${tableName} = { programs, ${entries.length} };
${guard ? '#endif\n' : ''}`;
  writeFileSync(join(OUT, file), out.replace('@THREE@', threeVersion));
  return { n: entries.length, kb: out.length / 1024, zkb: packed.z.length / 1024 };
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
/* a table's shader text, packed and deflated by tools/emit-programs.mjs
 * (packText); the cache holds it inflated once a program of the table is built */
typedef struct t3_gen_text_cache { char *lines; uint32_t *line_at; uint8_t *refs; uint32_t *shader_at; uint32_t n_lines, n_shaders; bool failed; } t3_gen_text_cache;
typedef struct t3_gen_text { const uint8_t *z; uint32_t zlen, raw; t3_gen_text_cache *cache; } t3_gen_text;
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
  /* the shader text: vertex / fragment when the program carries it (the
   * runtime builder's), else shaders vtext / ftext of the table's packed
   * text; t3_gen_source returns it either way */
  const char *vertex, *fragment;
  const struct t3_gen_text *text; uint32_t vtext, ftext;
} t3_gen_program;

/* a table of programs: three.c's own (programs_gl.c, or programs_core_gl.c
 * in a core-only build) and any a project registers
 * (t3_register_program_table, tools/gen-project-table.mjs) */
typedef struct t3_gen_table { const t3_gen_program *programs; unsigned count; } t3_gen_table;
extern const t3_gen_table t3_gen_base_gl, t3_gen_base_wgpu;
/* a program's vertex (fragment = false) or fragment text, malloc'd; NULL with
 * err set when the packed text does not inflate */
char *t3_gen_source(const t3_gen_program *p, bool fragment, char *err, size_t errcap);

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
const gl = emitBackend('gl', states, 'programs_gl.c', 't3_gen_base_gl', 'ifndef');
const wgpu = emitBackend('wgpu', states, 'programs_wgpu.c', 't3_gen_base_wgpu', 'ifndef');
const cgl = emitBackend('gl', core, 'programs_core_gl.c', 't3_gen_base_gl', 'ifdef');
const cwgpu = emitBackend('wgpu', core, 'programs_core_wgpu.c', 't3_gen_base_wgpu', 'ifdef');
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
console.log(`wrote src/gen/programs.h, programs_gl.c (${gl.n} states, ${gl.kb.toFixed(0)} KB, text ${gl.zkb.toFixed(0)} KB), programs_wgpu.c (${wgpu.n} states, ${wgpu.kb.toFixed(0)} KB, text ${wgpu.zkb.toFixed(0)} KB), programs_core_{gl,wgpu}.c (${cgl.n} states, ${cgl.kb.toFixed(0)} + ${cwgpu.kb.toFixed(0)} KB), programs_dfg.c (three ${threeVersion})`);
