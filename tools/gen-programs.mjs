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

// ── the support table ────────────────────────────────────────────────
// A state is a material kind plus features. Lights for the lit materials are a
// fixed vector for now (one ambient, one directional, one point); light-count
// variants come when the renderer's light model is settled.
const KINDS = {
  basic: () => new THREE.MeshBasicMaterial(), lambert: () => new THREE.MeshLambertMaterial(),
  phong: () => new THREE.MeshPhongMaterial(), standard: () => new THREE.MeshStandardMaterial(),
  physical: () => new THREE.MeshPhysicalMaterial(),
  // the shadow pass: the caster program r186 draws depth with (its
  // scene.overrideMaterial); captured from the mesh's shadow-pass draw with
  // one directional shadow. The material here is only the main pass's.
  depth: () => new THREE.MeshStandardMaterial(),
  // the non-mesh kinds: drawn as Points / Line / Sprite objects; normal = MeshNormalMaterial
  // r186's node renderer draws a Points object as 1-pixel GL points whatever the
  // material (PointsNodeMaterial.setupVertex: builder.object.isPoints -> gl_PointSize
  // = 1.0; size is ignored); sized, attenuated points are PointsNodeMaterial on a
  // Sprite ("psprite": size, sizeAttenuation, rotation, the sprite's center)
  points: () => new THREE.PointsMaterial(), psprite: () => new THREE.PointsNodeMaterial(),
  line: () => new THREE.LineBasicMaterial(),
  sprite: () => new THREE.SpriteMaterial(), normal: () => new THREE.MeshNormalMaterial(),
  // r186's OUTPUT PASS (tone mapping + output colour space over the linear
  // framebuffer target): captured from the pass's own draw
  output: () => new THREE.MeshBasicMaterial(),
  // AUXILIARY programs the renderer draws itself, captured from their own
  // draws (see auxCapture): the scene background (a cube texture, a PMREM /
  // blurred one, a 2D texture), and the PMREM generator's passes (a cube map
  // or an equirect to cube-UV, the GGX filter and the blur; the last two bake
  // the target's size: +z<lodMax>)
  bgcube: () => new THREE.MeshBasicMaterial(), bgpmrem: () => new THREE.MeshBasicMaterial(), bg2d: () => new THREE.MeshBasicMaterial(),
  pmremcube: () => new THREE.MeshBasicMaterial(), pmremequi: () => new THREE.MeshBasicMaterial(),
  pmremggx: () => new THREE.MeshBasicMaterial(), pmremblur: () => new THREE.MeshBasicMaterial(),
};
const AUX = new Set(['bgcube', 'bgpmrem', 'bg2d', 'pmremcube', 'pmremequi', 'pmremggx', 'pmremblur']);
const TONE = { tmlinear: 'LinearToneMapping', tmreinhard: 'ReinhardToneMapping', tmcineon: 'CineonToneMapping',
               tmaces: 'ACESFilmicToneMapping', tmagx: 'AgXToneMapping', tmneutral: 'NeutralToneMapping' };
const NON_MESH = new Set(['points', 'psprite', 'line', 'sprite']);
const SIMPLE = new Set(['points', 'psprite', 'line', 'sprite', 'normal']);   // their own, shorter feature lists
const FEATURES = {
  map: (m) => { m.map = dataTexture(); },
  vcol: (m) => { m.vertexColors = true; },
  fog: () => {},            // the scene gets a fog
  fog2: () => {},           // ... a FogExp2
  atest: (m) => { m.alphaTest = 0.25 + sentinel(); },   // alphaTest > 0: a discard (value fingerprinted in capture())
  tan: () => {},             // the geometry has a tangent attribute (computeTangents in capture())
  morphn: () => {},          // morph targets with normals (texel stride 2)
  inst: () => {},           // the mesh is an InstancedMesh
  flat: (m) => { m.flatShading = true; },
  trans: (m) => { m.transparent = true; m.opacity = 0.5; },
  // r186 puts instance matrices in a uniform buffer while count * 64 bytes fits
  // the backend's uniform limit (WebGPU 64 KB: 1024 instances; WebGL2
  // GL_MAX_UNIFORM_BLOCK_SIZE, 16 KB: 256) and in interleaved instanced
  // attributes beyond that (Instance.js): a game's big batches get the
  // attribute program, so both are captured (inst = 4 instances, instbig = 2000)
  instbig: () => {},
  icol: () => {},
  // MeshPhysicalMaterial's optional lobes: r186 compiles each in when its strength is above 0
  cc: (m) => { needs(m, 'clearcoat'); m.clearcoat = 0.5; },
  ccn: (m) => { needs(m, 'clearcoatNormalMap'); m.clearcoatNormalMap = dataTexture(); },
  sheen: (m) => { needs(m, 'sheen'); m.sheen = 0.5; },
  irid: (m) => { needs(m, 'iridescence'); m.iridescence = 0.5; },
  aniso: (m) => { needs(m, 'anisotropy'); m.anisotropy = 0.5; },
  transm: (m) => { needs(m, 'transmission'); m.transmission = 0.5; },           // the InstancedMesh has instance colours (setColorAt): an instanced vec3 attribute
  // the other maps, each its own texture so the manifest names its source;
  // a feature a material kind lacks is an error (the table never pairs them)
  nmap: (m) => { needs(m, 'normalMap'); m.normalMap = dataTexture(); },
  ao: (m) => { needs(m, 'aoMap'); m.aoMap = dataTexture(); },             // samples uv1 (aoMap.channel is 1 in r186): the geometry gets a uv1
  emap: (m) => { needs(m, 'emissiveMap'); m.emissiveMap = dataTexture(); },
  rmap: (m) => { needs(m, 'roughnessMap'); m.roughnessMap = dataTexture(); },
  mmap: (m) => { needs(m, 'metalnessMap'); m.metalnessMap = dataTexture(); },
  amap: (m) => { needs(m, 'alphaMap'); m.alphaMap = dataTexture(); },
  bump: (m) => { needs(m, 'bumpMap'); m.bumpMap = dataTexture(); },
  spec: (m) => { needs(m, 'specularMap'); m.specularMap = dataTexture(); },
  shadow: () => {},   // every light in the vector casts, the mesh receives (renderer.shadowMap.enabled)
  // the classic environment of Basic / Lambert / Phong (BasicEnvironmentNode):
  // material.envMap a CubeTexture sampled along the reflection (or, +refr,
  // refraction) vector, combined by material.combine (multiply; +mix, +add)
  cenv: (m) => { needs(m, 'envMap'); m.envMap = cubeTexture(THREE.CubeReflectionMapping); },
  refr: (m) => { m.envMap.mapping = THREE.CubeRefractionMapping; },    // after cenv
  mix: (m) => { m.combine = THREE.MixOperation; },
  // material.side: r186 bakes it into the program (negateOnBackSide: DoubleSide
  // multiplies normals, tangents and bitangents by faceDirection, BackSide negates them)
  // captured on a COMPATIBILITY device (no core-features-and-limits): r186's
  // WGSL differs there for a DepthTexture map (texture_2d<f32>, textureLoad .x);
  // wasmcart's WebGPU tier gives carts a compatibility device
  cm: () => {},
  ds: (m) => { m.side = THREE.DoubleSide; },
  bs: (m) => { m.side = THREE.BackSide; },
  add: (m) => { m.combine = THREE.AddOperation; },
  env: () => {},      // material.envMap = a PMREM of a small scene (r186's cube-UV sampling); envMapIntensity, envMapRotation
  senv: () => {},     // scene.environment instead: scene.environmentIntensity / environmentRotation feed the same uniforms
  lmap: (m) => { needs(m, 'lightMap'); m.lightMap = dataTexture(); },
  skin: () => {},     // a SkinnedMesh with a two-bone skeleton (bone texture, bind matrices)
  morph: () => {},    // two position morph targets (morph texture array, influences)
  noatten: (m) => { needs(m, 'sizeAttenuation'); m.sizeAttenuation = false; },   // points: a program key
  // output pass keys (applied to the renderer in capture())
  tmlinear: () => {}, tmreinhard: () => {}, tmcineon: () => {}, tmaces: () => {}, tmagx: () => {}, tmneutral: () => {}, lin: () => {},
  rep: (m) => { m.map.wrapS = m.map.wrapT = THREE.RepeatWrapping; },      // after map
  nearest: (m) => { m.map.magFilter = m.map.minFilter = THREE.NearestFilter; m.map.generateMipmaps = false; },   // after map: r186 emits textureLoad
  // the map is a render target's DepthTexture: on WebGPU a texture_depth_2d
  // read with textureLoad (a render target's colour texture compiles exactly
  // as a DataTexture map does)
  dmap: (m) => { const t = new THREE.RenderTarget(4, 4); t.depthTexture = new THREE.DepthTexture(4, 4); m.map = t.depthTexture; },
};
const needs = (m, key) => { if (!(key in m)) throw new Error(`${m.type} has no ${key}`); };
const LIT = new Set(['lambert', 'phong', 'standard', 'physical']);
function allStates() {
  const out = [];
  out.push('output');
  for (const tm of Object.keys(TONE)) out.push(`output+${tm}`, `output+${tm}+lin`);
  for (const kind of Object.keys(KINDS)) {
    if (kind === 'depth' || kind === 'output' || AUX.has(kind)) continue;
    if (SIMPLE.has(kind)) {
      out.push(kind, `${kind}+fog`, `${kind}+trans`);
      if (kind !== 'normal') out.push(`${kind}+vcol`);
      if (kind !== 'normal') out.push(`${kind}+map`);
      if (kind === 'points') out.push('points+map+vcol+fog');
      if (kind === 'psprite') out.push('psprite+noatten', 'psprite+map+fog');
      if (kind === 'sprite') out.push('sprite+map+fog');
      if (kind === 'normal') out.push('normal+instbig', 'normal+flat');
      continue;
    }
    out.push(kind);
    for (const f of ['map', 'vcol', 'fog', 'inst', 'instbig', 'trans']) out.push(`${kind}+${f}`);
    if (LIT.has(kind)) out.push(`${kind}+flat`);
    out.push(`${kind}+map+vcol+fog`);
    out.push(`${kind}+map+rep`, `${kind}+map+nearest`);
    if (LIT.has(kind)) out.push(`${kind}+l0000`, `${kind}+l1000`, `${kind}+l1111`, `${kind}+l2200`, `${kind}+l4410`);
    // the other maps
    const maps = { basic: ['amap', 'ao'], lambert: ['nmap', 'emap', 'ao', 'amap'], phong: ['nmap', 'emap', 'bump', 'spec', 'amap'],
                   standard: ['nmap', 'ao', 'emap', 'rmap', 'mmap', 'amap'], physical: ['nmap', 'ao', 'emap', 'rmap', 'mmap', 'amap'] }[kind];
    for (const f of maps) out.push(`${kind}+${f}`);
    if (kind === 'standard' || kind === 'physical') out.push(`${kind}+map+nmap+ao+emap+rmap+mmap`);   // the glTF PBR set
  }
  out.push('standard+map+xtint');   // the extension hook, exercised by the example extension
  for (const kind of LIT) out.push(`${kind}+shadow`);
  out.push('standard+l0100+shadow', 'standard+l0010+shadow');   // point (cube depth) and spot shadows
  // (Lambert / Phong take no PMREM environment in r186: their envMap is the classic cube / equirect reflection, a later feature)
  for (const kind of ['standard', 'physical']) out.push(`${kind}+env`, `${kind}+senv`);
  out.push('standard+map+env', 'standard+map+nmap+ao+emap+rmap+mmap+senv');
  for (const kind of ['basic', 'lambert', 'phong', 'standard']) out.push(`${kind}+lmap`);
  out.push('standard+l2200+shadow', 'standard+skin', 'standard+morph', 'standard+skin+morph', 'basic+skin', 'basic+morph');
  out.push('depth', 'depth+inst', 'depth+instbig', 'depth+skin', 'depth+morph');
  // InstancedMesh.instanceColor (three.c draws every InstancedMesh with the attribute program)
  for (const kind of ['basic', 'lambert', 'phong', 'standard', 'physical']) out.push(`${kind}+instbig+icol`);
  out.push('standard+instbig+icol+shadow');
  // MeshPhysicalMaterial's lobes, alone and together
  out.push('physical+cc', 'physical+cc+ccn', 'physical+sheen', 'physical+irid', 'physical+aniso', 'physical+cc+sheen+irid');
  // double-sided and back-sided materials (glTF doubleSided is common)
  for (const kind of ['basic', 'lambert', 'phong', 'standard', 'physical']) out.push(`${kind}+ds`, `${kind}+map+ds`, `${kind}+bs`);
  out.push('standard+map+nmap+ao+emap+rmap+mmap+ds', 'normal+ds');
  // the classic cube environment
  for (const kind of ['basic', 'lambert', 'phong']) out.push(`${kind}+cenv`, `${kind}+cenv+mix`, `${kind}+cenv+add`, `${kind}+cenv+refr`);
  // backgrounds and the PMREM generator (cube sizes 16 .. 1024)
  out.push('bgcube', 'bgpmrem', 'bg2d', 'pmremcube', 'pmremequi');
  for (let z = 4; z <= 10; z++) out.push(`pmremggx+z${z}`, `pmremblur+z${z}`);
  return out;
}
// A state name is kind+feature+...; two more token kinds: lDPSH (digits:
// directional, point, spot, hemisphere light counts; lit kinds default to
// l1100 and always get one ambient light) and x<name> (an extension module,
// tools/extensions/<name>.mjs or a path given with --extend, applied to the
// scene before the capture: external TSL nodes, extra uniforms; see
// tools/extensions/tint.mjs). Light counts are part of the state because
// three.js keys its programs on them.
function parseState(stateName) {
  const [kind, ...tokens] = stateName.split('+');
  if (!(kind in KINDS)) throw new Error(`${stateName}: unknown material kind ${kind}`);
  const lightsTok = tokens.find((t) => /^l\d{4}$/.test(t));
  const extTok = tokens.find((t) => /^x[a-z0-9_]+$/.test(t));
  // sDPS: how many of each type's lights cast (the first ones in creation order:
  // r186 orders a program's lights by object id); absent = all of them
  const castTok = tokens.find((t) => /^s\d{3}$/.test(t));
  // z<lodMax>: the PMREM target size a pmremggx / pmremblur program bakes
  const zTok = tokens.find((t) => /^z\d+$/.test(t));
  if (zTok && !(kind === 'pmremggx' || kind === 'pmremblur')) throw new Error(`${stateName}: z<lodMax> is for pmremggx / pmremblur`);
  if (!zTok && (kind === 'pmremggx' || kind === 'pmremblur')) throw new Error(`${stateName}: needs z<lodMax>`);
  const features = tokens.filter((t) => t !== lightsTok && t !== extTok && t !== castTok && t !== zTok);
  for (const f of features) if (!(f in FEATURES)) throw new Error(`${stateName}: unknown feature ${f}`);
  const lights = lightsTok ? lightsTok.slice(1).split('').map(Number) : LIT.has(kind) ? [1, 1, 0, 0] : kind === 'depth' ? [1, 0, 0, 0] : [0, 0, 0, 0];
  if (kind === 'depth' && lightsTok) throw new Error(`${stateName}: the depth kind takes no light vector`);
  const cast = castTok ? castTok.slice(1).split('').map(Number) : null;
  if (cast && !features.includes('shadow')) throw new Error(`${stateName}: sDPS needs +shadow`);
  return { kind, features, lights, cast, extension: extTok ? extTok.slice(1) : null, lod: zTok ? +zTok.slice(1) : null };
}
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

// ── texture binding layout, as r186's WebGPU backend classifies it ────
// (WebGPUBindingUtils.createBindingsLayout): the manifest needs the sample
// type and dimension, not just a name, so integer textures (an engine's
// clustered-light data, say: RG32UI records, R32UI index lists, texelFetch,
// no sampler) can be declared by extension groups.
function textureLayout(b, features = []) {
  const t = b.texture || {};
  let sample;
  if (t.isDepthTexture) sample = t.compareFunction === null && features.includes('cm') ? 'unfilterable-float' : 'depth';   // (WebGPUBindingUtils: compatibility mode)
  else if (t.type === THREE.IntType) sample = 'sint';
  else if (t.type === THREE.UnsignedIntType) sample = 'uint';
  else if (t.normalized === true && (t.type === THREE.ShortType || t.type === THREE.UnsignedShortType)) sample = 'unfilterable-float';
  else if (t.type === THREE.FloatType) sample = 'unfilterable-float';   // float32-filterable is a feature, not assumed
  else sample = 'float';
  const dimension = b.isSampledCubeTexture ? 'cube' : b.isSampledTexture3D ? '3d' : t.isArrayTexture || t.isDataArrayTexture || t.isCompressedArrayTexture ? '2d-array' : '2d';
  return { sample, dimension, sampled: !(sample === 'uint' || sample === 'sint'), storage: !!b.store };
}

// ── sentinels: values nothing else in the scene has ──────────────────
// floats chosen so their bit patterns are unique and survive sRGB->linear
let sentinelSeed = 0;
// template parameter values at capture (see TEMPLATE PARAMETERS in capture())
const TPL = { bones: 7, morphs: 3, morphWidth: 667 };   // 667 = SphereGeometry(1, 28, 22)'s vertex count
// (no clipping states: r186's node renderer ignores material / renderer clippingPlanes; its clipping is ClippingGroup)
// Two capture "worlds": the primary and a probe that differs in everything the
// renderer's environment can vary (drawing-buffer size, viewport, sentinel
// seed). A uniform no property explains is a CONSTANT when both worlds upload
// the same bytes (flags, identity matrices, defaults) and renderer state when
// they differ; the latter must be explained by a property or the capture fails.
const PROBE = process.env.GEN_PROBE === '1';
const WORLD = PROBE ? { w: 80, h: 60, vx: 1, vy: 2, vw: 70, vh: 50, seed: 700 } : { w: 96, h: 72, vx: 2, vy: 3, vw: 90, vh: 66, seed: 0 };
const sentinel = () => 0.12345 + 0.00731 * ++sentinelSeed;
function cubeTexture(mapping) {
  const faces = [];
  for (let f = 0; f < 6; f++) { const t = new THREE.DataTexture(new Uint8Array([40 * f, 255 - 30 * f, 128, 255, 10, 20, 30, 255, 200, 100, 50, 255, 90, 90, 90, 255]), 2, 2); t.needsUpdate = true; faces.push(t); }
  const c = new THREE.CubeTexture(faces, mapping);
  c.format = THREE.RGBAFormat; c.colorSpace = THREE.SRGBColorSpace; c.needsUpdate = true;
  return c;
}
function dataTexture() {
  // a 2x2 RGBA8 texture with the Texture class DEFAULTS (linear, mipmapped,
  // clamp): r186 bakes filter and wrap into the program (textureLoad for a
  // nearest/unmipmapped map, a tsl_coord_<wrapS>S_<wrapT>T function for the
  // wrap), so the capture must use the settings a game texture has. DataTexture
  // itself defaults to nearest/no mipmaps, which is why these are set.
  const t = new THREE.DataTexture(new Uint8Array([255, 128, 64, 255, 32, 200, 16, 255, 8, 8, 240, 255, 100, 100, 100, 255]), 2, 2);
  t.magFilter = THREE.LinearFilter; t.minFilter = THREE.LinearMipmapLinearFilter; t.generateMipmaps = true;
  t.wrapS = t.wrapT = THREE.ClampToEdgeWrapping;
  t.needsUpdate = true;
  return t;
}
const fround = (x) => Math.fround(x);

// ── one state on one backend ─────────────────────────────────────────
async function capture(stateName, backend) {
  sentinelSeed = WORLD.seed;
  const { kind, features, lights, cast, extension, lod } = parseState(stateName);
  let canvas, renderer, gl = null;
  if (backend === 'wgpu') {
    const wn = await import(webgpuNodeUrl());
    wn.installGlobals();
    canvas = wn.createCanvas(WORLD.w, WORLD.h);
    let device;
    if (features.includes('cm')) {
      const ad = await navigator.gpu.requestAdapter({ featureLevel: 'compatibility', powerPreference: 'high-performance' });
      device = await ad.requestDevice({ requiredFeatures: [...ad.features].filter((f) => f !== 'core-features-and-limits') });
    }
    renderer = new THREE.WebGPURenderer({ canvas, antialias: false, forceWebGL: false, powerPreference: 'high-performance', ...(device ? { device } : {}) });
  } else {
    const { createWebGL2Context } = await import(webglNodeUrl());
    const made = createWebGL2Context(WORLD.w, WORLD.h);
    gl = made.gl; canvas = made.canvas;
    canvas.getContext = (k) => (k === 'webgl2' ? gl : null);
    renderer = new THREE.WebGPURenderer({ canvas, context: gl, antialias: false, forceWebGL: true });
  }
  await renderer.init();
  renderer.setViewport(WORLD.vx, WORLD.vy, WORLD.vw, WORLD.vh);
  let exposure = null;
  if (kind === 'output') {
    for (const f of features) if (TONE[f]) renderer.toneMapping = THREE[TONE[f]];
    if (features.includes('lin')) renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
    exposure = 0.5 + sentinel();
    renderer.toneMappingExposure = exposure;
  }
  // (the programs are read from the mesh's own render object after the draw:
  // hooking createProgram also sees the renderer's OUTPUT PASS program, which
  // is created last and used to overwrite the mesh's)

  const M = AUX.has(kind) ? await auxCapture() : await (async () => {
  // the scene: one mesh with the material, lights for a lit kind, the fog
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(60, 1, 0.1, 100); camera.position.z = 4;
  // transmission reads the camera's world position: make it a sentinel there
  if (features.includes('transm')) camera.position.set(sentinel(), sentinel(), 4 + sentinel());
  const props = {};   // property name -> [value, bytesExpectedLinear?]
  const mat = KINDS[kind]();
  for (const f of features) FEATURES[f](mat);
  // material sentinels (set before the first render, so creation-time caching sees them too)
  const setColor = (obj, key, path) => { const r = sentinel(), g = sentinel(), b = sentinel(); obj[key].setRGB(r, g, b, THREE.LinearSRGBColorSpace); props[path] = { kind: 'vec3', value: [r, g, b] }; };
  const setNum = (obj, key, path) => { const v = sentinel(); obj[key] = v; props[path] = { kind: 'f32', value: [v] }; };
  const wantShadow = features.includes('shadow') || kind === 'depth';
  if (kind !== 'depth' && 'color' in mat) setColor(mat, 'color', 'material.color');
  if (kind === 'psprite') setNum(mat, 'size', 'material.size');   // (rotation / center are SpriteNodeMaterial's, not PointsNodeMaterial's)
  if (kind === 'sprite') setNum(mat, 'rotation', 'material.rotation');
  // (a transparent material's opacity is a sentinel too: 0.5 also sits in a shadow matrix)
  if (kind !== 'depth') setNum(mat, 'opacity', 'material.opacity'); else props['material.opacity'] = { kind: 'f32', value: [0.5] };
  if ('emissive' in mat) setColor(mat, 'emissive', 'material.emissive');
  if ('emissiveIntensity' in mat) setNum(mat, 'emissiveIntensity', 'material.emissiveIntensity');
  if ('specular' in mat) setColor(mat, 'specular', 'material.specular');
  if ('shininess' in mat) setNum(mat, 'shininess', 'material.shininess');
  if ('roughness' in mat) setNum(mat, 'roughness', 'material.roughness');
  if ('metalness' in mat) setNum(mat, 'metalness', 'material.metalness');
  if ('ior' in mat) { const v = 1.3 + sentinel(); mat.ior = v; props['material.ior'] = { kind: 'f32', value: [v] }; }
  // physical: each lobe's values only when the feature turns it on (a value above 0 is what turns it on)
  if (kind === 'physical') {
    setNum(mat, 'specularIntensity', 'material.specularIntensity');
    setColor(mat, 'specularColor', 'material.specularColor');
    if (features.includes('cc')) { setNum(mat, 'clearcoat', 'material.clearcoat'); setNum(mat, 'clearcoatRoughness', 'material.clearcoatRoughness'); }
    if (features.includes('ccn')) { const nx = sentinel(), ny = sentinel(); mat.clearcoatNormalScale.set(nx, ny); props['material.clearcoatNormalScale'] = { kind: 'vec2', value: [nx, ny] }; }
    if (features.includes('sheen')) { setNum(mat, 'sheen', 'material.sheen'); setColor(mat, 'sheenColor', 'material.sheenColor'); setNum(mat, 'sheenRoughness', 'material.sheenRoughness'); }
    if (features.includes('irid')) {
      setNum(mat, 'iridescence', 'material.iridescence');
      const v = 1.2 + sentinel(); mat.iridescenceIOR = v; props['material.iridescenceIOR'] = { kind: 'f32', value: [v] };
      const t = 300 + sentinel(); mat.iridescenceThicknessRange[1] = t; props['material.iridescenceThicknessMax'] = { kind: 'f32', value: [t] };
    }
    if (features.includes('aniso')) {
      const a = sentinel(), rot = sentinel(); mat.anisotropy = a; mat.anisotropyRotation = rot;
      props['material.anisotropyVector'] = { kind: 'vec2', value: [a * Math.cos(rot), a * Math.sin(rot)] };
    }
    if (features.includes('transm')) {
      setNum(mat, 'transmission', 'material.transmission'); setNum(mat, 'thickness', 'material.thickness');
      setNum(mat, 'attenuationDistance', 'material.attenuationDistance'); setColor(mat, 'attenuationColor', 'material.attenuationColor');
    }
  }
  // every texture's uv transform is a uniform of its own (texture.matrix): sentinel offset / repeat on each
  const TEX_KEYS = ['map', 'normalMap', 'aoMap', 'emissiveMap', 'roughnessMap', 'metalnessMap', 'alphaMap', 'bumpMap', 'specularMap', 'lightMap', 'clearcoatNormalMap'];
  for (const key of TEX_KEYS) if (mat[key]) { mat[key].offset.set(sentinel(), sentinel()); mat[key].repeat.set(1 + sentinel(), 1 + sentinel()); }
  if (mat.normalMap) { const nx = sentinel(), ny = sentinel(); mat.normalScale.set(nx, ny); props['material.normalScale'] = { kind: 'vec2', value: [nx, ny] }; }
  if (mat.aoMap) setNum(mat, 'aoMapIntensity', 'material.aoMapIntensity');
  if (mat.bumpMap) setNum(mat, 'bumpScale', 'material.bumpScale');
  if (mat.lightMap) setNum(mat, 'lightMapIntensity', 'material.lightMapIntensity');
  if (features.includes('atest')) props['material.alphaTest'] = { kind: 'f32', value: [mat.alphaTest] };
  if (features.includes('env') && 'envMapIntensity' in mat) setNum(mat, 'envMapIntensity', 'material.envMapIntensity');
  if (features.includes('cenv')) {
    setNum(mat, 'reflectivity', 'material.reflectivity');
    if (features.includes('refr')) setNum(mat, 'refractionRatio', 'material.refractionRatio');
    mat.envMapRotation.set(sentinel(), sentinel(), sentinel());
    const m4 = new THREE.Matrix4().makeRotationFromEuler(mat.envMapRotation);
    props['material.envMapRotation'] = { kind: 'mat4', value: Array.from(m4.clone().transpose().elements), optional: true };
    props['material.envMapRotationNT'] = { kind: 'mat4', value: Array.from(m4.elements), optional: true };
  }
  if (features.includes('senv')) { const v = sentinel(); scene.environmentIntensity = v; props['scene.environmentIntensity'] = { kind: 'f32', value: [v] }; }
  const viewPos = [];                // [path, vector] pairs to turn into view-space sentinels once the camera is placed
  if (LIT.has(kind)) {
    const amb = new THREE.AmbientLight(); scene.add(amb);
    const ar = sentinel(), ag = sentinel(), ab = sentinel(); amb.color.setRGB(ar, ag, ab, THREE.LinearSRGBColorSpace); amb.intensity = 1; props['ambient.color'] = { kind: 'vec3', value: [ar, ag, ab] };
  }
  const [nDir, nPoint, nSpot, nHemi] = lights;
  for (let i = 0; i < nDir; i++) {
    const dl = new THREE.DirectionalLight(); scene.add(dl);
    const dr = sentinel(), dg = sentinel(), db = sentinel(); dl.color.setRGB(dr, dg, db, THREE.LinearSRGBColorSpace); dl.intensity = 1; props[`directional[${i}].color`] = { kind: 'vec3', value: [dr, dg, db] };
    const dx = sentinel(), dy = sentinel(), dz = sentinel(); dl.position.set(dx, dy, dz); props[`directional[${i}].position`] = { kind: 'vec3', value: [dx, dy, dz] };
    // the direction is light position minus target position, both uniforms (a target at the origin hides the second)
    scene.add(dl.target);
    const tx = sentinel(), ty = sentinel(), tz = sentinel(); dl.target.position.set(tx, ty, tz); props[`directional[${i}].targetPosition`] = { kind: 'vec3', value: [tx, ty, tz] };
  }
  for (let i = 0; i < nPoint; i++) {
    const pl = new THREE.PointLight(); scene.add(pl);
    const pr = sentinel(), pg = sentinel(), pb = sentinel(); pl.color.setRGB(pr, pg, pb, THREE.LinearSRGBColorSpace); pl.intensity = 1; props[`point[${i}].color`] = { kind: 'vec3', value: [pr, pg, pb] };
    const px = sentinel(), py = sentinel(), pz = sentinel(); pl.position.set(px, py, pz); viewPos.push([`point[${i}].position`, pl.position]);
    const pd = 3 + sentinel(); pl.distance = pd; props[`point[${i}].distance`] = { kind: 'f32', value: [pd] };
    const pdc = 1 + sentinel(); pl.decay = pdc; props[`point[${i}].decay`] = { kind: 'f32', value: [pdc] };
  }
  for (let i = 0; i < nSpot; i++) {
    const sl = new THREE.SpotLight(); scene.add(sl); scene.add(sl.target);
    const sr = sentinel(), sg = sentinel(), sb = sentinel(); sl.color.setRGB(sr, sg, sb, THREE.LinearSRGBColorSpace); sl.intensity = 1; props[`spot[${i}].color`] = { kind: 'vec3', value: [sr, sg, sb] };
    const sx = sentinel(), sy = sentinel(), sz = sentinel(); sl.position.set(sx, sy, sz); viewPos.push([`spot[${i}].position`, sl.position]);
    props[`spot[${i}].worldPosition`] = { kind: 'vec3', value: [sx, sy, sz] };
    const tx = sentinel(), ty = sentinel(), tz = sentinel(); sl.target.position.set(tx, ty, tz); props[`spot[${i}].targetPosition`] = { kind: 'vec3', value: [tx, ty, tz] };
    const sd = 3 + sentinel(); sl.distance = sd; props[`spot[${i}].distance`] = { kind: 'f32', value: [sd] };
    const sdc = 1 + sentinel(); sl.decay = sdc; props[`spot[${i}].decay`] = { kind: 'f32', value: [sdc] };
    const ang = 0.3 + sentinel(), pen = 0.1 + sentinel(); sl.angle = ang; sl.penumbra = pen;
    props[`spot[${i}].coneCos`] = { kind: 'f32', value: [Math.cos(ang)] };
    props[`spot[${i}].penumbraCos`] = { kind: 'f32', value: [Math.cos(ang * (1 - pen))] };
  }
  for (let i = 0; i < nHemi; i++) {
    const hl = new THREE.HemisphereLight(); scene.add(hl);
    const hr = sentinel(), hg = sentinel(), hb = sentinel(); hl.color.setRGB(hr, hg, hb, THREE.LinearSRGBColorSpace); hl.intensity = 1; props[`hemisphere[${i}].skyColor`] = { kind: 'vec3', value: [hr, hg, hb] };
    const gr = sentinel(), gg = sentinel(), gb = sentinel(); hl.groundColor.setRGB(gr, gg, gb, THREE.LinearSRGBColorSpace); props[`hemisphere[${i}].groundColor`] = { kind: 'vec3', value: [gr, gg, gb] };
    const hx = sentinel(), hy = sentinel(), hz = sentinel(); hl.position.set(hx, hy, hz); props[`hemisphere[${i}].position`] = { kind: 'vec3', value: [hx, hy, hz] };
  }
  if (wantShadow) {
    renderer.shadowMap.enabled = true;
    const all = scene.children.filter((o) => o.isDirectionalLight || o.isSpotLight || o.isPointLight);
    const nth = (l) => all.filter((o) => o.constructor === l.constructor).indexOf(l);
    const limit = (l) => !cast ? Infinity : l.isDirectionalLight ? cast[0] : l.isPointLight ? cast[1] : cast[2];
    const casting = all.filter((l) => nth(l) < limit(l));
    if (!casting.length) throw new Error(`${stateName}: +shadow needs a directional, spot or point light in the vector`);
    const counters = { directional: 0, spot: 0, point: 0 };
    for (const l of casting) {
      const type = l.isDirectionalLight ? 'directional' : l.isSpotLight ? 'spot' : 'point';
      const i = counters[type]++;
      l.castShadow = true;
      const sh = l.shadow;
      const ms = [640 + 32 * casting.indexOf(l), 352 + 16 * casting.indexOf(l)];   // distinct per light, or two shadows' mapSize fingerprints collide
      sh.mapSize.set(ms[0], ms[1]);
      if (kind !== 'depth') {
        const bias = -0.01 * sentinel(), nb = sentinel(), rad = 1 + sentinel(), inten = sentinel();
        sh.bias = bias; sh.normalBias = nb; sh.radius = rad; sh.intensity = inten;
        props[`${type}[${i}].shadow.bias`] = { kind: 'f32', value: [bias] };
        props[`${type}[${i}].shadow.normalBias`] = { kind: 'f32', value: [nb] };
        props[`${type}[${i}].shadow.radius`] = { kind: 'f32', value: [rad] };
        props[`${type}[${i}].shadow.intensity`] = { kind: 'f32', value: [inten] };
        props[`${type}[${i}].shadow.mapSize`] = { kind: 'vec2', value: ms };
      }
    }
  }
  if (features.includes('fog2')) {
    const fog = new THREE.FogExp2(0x000000, 0.1); scene.fog = fog;
    const fr = sentinel(), fg = sentinel(), fb = sentinel(); fog.color.setRGB(fr, fg, fb, THREE.LinearSRGBColorSpace); props['fog.color'] = { kind: 'vec3', value: [fr, fg, fb] };
    const d = 0.05 + sentinel(); fog.density = d; props['fog.density'] = { kind: 'f32', value: [d] };
  }
  if (features.includes('fog')) {
    const fog = new THREE.Fog(0x000000, 1, 10); scene.fog = fog;
    const fr = sentinel(), fg = sentinel(), fb = sentinel(); fog.color.setRGB(fr, fg, fb, THREE.LinearSRGBColorSpace); props['fog.color'] = { kind: 'vec3', value: [fr, fg, fb] };
    const fn = 1 + sentinel(), ff = 10 + sentinel(); fog.near = fn; fog.far = ff; props['fog.near'] = { kind: 'f32', value: [fn] }; props['fog.far'] = { kind: 'f32', value: [ff] };
  }
  // TEMPLATE PARAMETERS: r186 bakes some per-object counts into the program text
// (bone count as a uniform array size, morph target count as a loop bound and
// array size, the morph texture's row width). They are captured at values that
// appear nowhere else in the program, recorded in the manifest, and substituted
// by the renderer for the object at hand (gen_program.c).
const geo = features.includes('morph') || features.includes('morphn') ? new THREE.SphereGeometry(1, 28, 22) : new THREE.BoxGeometry(1, 1, 1);
if (features.includes('morph') && geo.attributes.position.count !== TPL.morphWidth) throw new Error('morph capture geometry changed size');
  if (mat.aoMap) geo.setAttribute('uv1', geo.attributes.uv.clone());
  if (features.includes('tan')) geo.computeTangents();
  if (features.includes('vcol')) { const n = geo.attributes.position.count; geo.setAttribute('color', new THREE.BufferAttribute(new Float32Array(n * 3).fill(0.5), 3)); }
  if (features.includes('morphn') && !features.includes('morph')) throw new Error(`${stateName}: morphn goes with morph`);
  if (features.includes('morph')) {
    const pos = geo.attributes.position;
    const mk = (d) => { const a = new Float32Array(pos.array.length); for (let i = 0; i < a.length; i++) a[i] = pos.array[i] + d; return new THREE.BufferAttribute(a, 3); };
    geo.morphAttributes.position = [mk(0.1), mk(-0.1), mk(0.05)];
    if (features.includes('morphn')) { const nrm = geo.attributes.normal; geo.morphAttributes.normal = [0, 1, 2].map(() => nrm.clone()); }
  }
  let mesh;
  if (features.includes('skin')) {
    if (features.includes('inst') || features.includes('instbig')) throw new Error(`${stateName}: skin and instancing do not combine`);
    const n = geo.attributes.position.count;
    const si = new Uint16Array(n * 4), sw = new Float32Array(n * 4);
    for (let i = 0; i < n; i++) { si[i * 4] = i % TPL.bones; si[i * 4 + 1] = (i + 1) % TPL.bones; sw[i * 4] = 0.75; sw[i * 4 + 1] = 0.25; }
    geo.setAttribute('skinIndex', new THREE.BufferAttribute(si, 4));
    geo.setAttribute('skinWeight', new THREE.BufferAttribute(sw, 4));
    const bones = [];
    for (let k = 0; k < TPL.bones; k++) { const b = new THREE.Bone(); b.position.set(sentinel(), sentinel(), sentinel()); if (k) bones[k - 1].add(b); bones.push(b); }
    mesh = new THREE.SkinnedMesh(geo, mat);
    mesh.add(bones[0]);
    const bind = new THREE.Matrix4().makeTranslation(sentinel(), sentinel(), sentinel());
    mesh.bind(new THREE.Skeleton(bones), bind);
    props['skin.bindMatrix'] = { kind: 'mat4', value: Array.from(mesh.bindMatrix.elements) };
    props['skin.bindMatrixInverse'] = { kind: 'mat4', value: Array.from(mesh.bindMatrixInverse.elements) };
  } else if (kind === 'points') {
    mesh = new THREE.Points(geo, mat);
  } else if (kind === 'line') {
    mesh = new THREE.Line(geo, mat);
  } else if (kind === 'sprite' || kind === 'psprite') {
    mesh = new THREE.Sprite(mat);
    if (kind === 'sprite') { const cx0 = sentinel(), cy0 = sentinel(); mesh.center.set(cx0, cy0); props['sprite.center'] = { kind: 'vec2', value: [cx0, cy0] }; }
  } else {
    mesh = features.includes('instbig') ? new THREE.InstancedMesh(geo, mat, 2000) : features.includes('inst') ? new THREE.InstancedMesh(geo, mat, 4) : new THREE.Mesh(geo, mat);
  }
  if (features.includes('icol')) {
    if (!mesh.isInstancedMesh) throw new Error(`${stateName}: icol needs inst or instbig`);
    for (let i = 0; i < mesh.count; i++) mesh.setColorAt(i, new THREE.Color(0.25 + (i % 4) * 0.25, 0.5, 1 - (i % 3) * 0.25));
  }
  if (features.includes('morph')) {
    const inf = [sentinel(), sentinel(), sentinel()];
    mesh.morphTargetInfluences = inf;
    inf.forEach((v, k) => { props[`morph.influence[${k}]`] = { kind: 'f32', value: [v] }; });
    props['morph.baseInfluence'] = { kind: 'f32', value: [1 - inf.reduce((a, b) => a + b, 0)] };   // MorphNode: 1 - sum(influences) (not relative)
  }
  if (wantShadow) { mesh.castShadow = true; mesh.receiveShadow = true; }
  mesh.frustumCulled = false;   // programs do not depend on it; a skinned mesh's bound (bones at sentinel positions) must not drop a pass
  // rotation AND a non-uniform scale: with a pure rotation the normal matrix
  // equals the model matrix's 3x3 and the search finds it INSIDE matrixWorld
  // (that false positive once overwrote the model matrix and drew meshes 4x too big)
  mesh.rotation.set(0.3 + sentinel(), 0.4 + sentinel(), 0.5 + sentinel());
  mesh.scale.set(1.2 + sentinel(), 0.6 + sentinel(), 1.7 + sentinel());
  const mx = sentinel(), my = sentinel(), mz = sentinel(); mesh.position.set(mx, my, mz); props['object.position'] = { kind: 'vec3', value: [mx, my, mz], optional: true };   // also lives inside matrixWorld's translation: only a separate uniform counts
  scene.add(mesh);
  const cx = sentinel(); camera.position.x = cx; camera.updateMatrixWorld();
  // what three.js uploads is derived from the scene: the view matrix (named
  // uniform), a point light's position in VIEW space, a texture's uv transform
  // matrix; the sentinels are those derived values
  camera.updateMatrixWorld(); camera.matrixWorldInverse.copy(camera.matrixWorld).invert();
  props['camera.matrixWorldInverse'] = { kind: 'mat4', value: Array.from(camera.matrixWorldInverse.elements) };
  mesh.updateMatrixWorld();
  props['object.matrixWorld'] = { kind: 'mat4', value: Array.from(mesh.matrixWorld.elements) };
  // renderer state some programs read: the drawing buffer, the viewport, half the buffer height (sprite
  // point scale); found only by the programs that use them
  props['renderer.drawingBufferSize'] = { kind: 'vec2', value: [WORLD.w, WORLD.h], optional: true };
  props['renderer.viewportSize'] = { kind: 'vec2', value: [WORLD.vw, WORLD.vh], optional: true };
  props['renderer.viewport'] = { kind: 'vec4', value: [WORLD.vx, WORLD.vy, WORLD.vw, WORLD.vh], optional: true };
  props['renderer.halfHeight'] = { kind: 'f32', value: [WORLD.h / 2], optional: true };
  props['camera.worldMatrix'] = { kind: 'mat4', value: Array.from(camera.matrixWorld.elements), optional: true };
  if (features.includes('transm')) { camera.updateMatrixWorld(); const cp = new THREE.Vector3().setFromMatrixPosition(camera.matrixWorld); props['camera.position'] = { kind: 'vec3', value: [cp.x, cp.y, cp.z], optional: true }; }   // (also the translation of camera.worldMatrix)
  // the normal matrix r186 uploads: Matrix3.getNormalMatrix(object.matrixWorld) (ModelNode.js)
  props['object.normalMatrix'] = { kind: 'mat3', value: Array.from(new THREE.Matrix3().getNormalMatrix(mesh.matrixWorld).elements), optional: true };   // only programs that transform normals have it
  for (const [path, pos] of viewPos) {
    const v = pos.clone().applyMatrix4(camera.matrixWorldInverse);
    props[path] = { kind: 'vec3', value: [v.x, v.y, v.z], note: 'view space' };
  }
  // the extension's nodes and uniforms, with sentinels of its own
  if (extension) {
    const ext = await loadExtension(extension);
    await ext.apply({ THREE, TSL: await TSL_MODULE, state: stateName, kind, features, lights, backend, scene, mesh, mat, camera, props, sentinel });
  }
  for (const key of TEX_KEYS) if (mat[key]) { mat[key].updateMatrix(); props[`${key}.matrix`] = { kind: 'mat3', value: Array.from(mat[key].matrix.elements) }; }

  if (features.includes('env') || features.includes('senv')) {
    const own = features.includes('env');
    // a PMREM (prefiltered mip-mapped radiance environment) of a small lit
    // scene, as a game would make from an equirect or a cube; three.js's own
    // generator on this backend, so the capture samples the layout the
    // programs expect (cube-UV). Its own draws pass through the hook untouched.
    const pm = new THREE.PMREMGenerator(renderer);
    const envScene = new THREE.Scene();
    const sky = new THREE.Mesh(new THREE.SphereGeometry(10, 8, 6), new THREE.MeshBasicMaterial({ color: 0x8090ff, side: THREE.BackSide }));
    envScene.add(sky);
    const rt = pm.fromScene(envScene, 0.04, 0.1, 100, { size: PROBE ? 128 : 256 });   // (the cube-UV size uniforms differ between worlds)
    {
      const h = rt.texture.image.height, mm = Math.log2(h) - 2;
      props['cubeuv.texelWidth'] = { kind: 'f32', value: [1 / (3 * Math.max(Math.pow(2, mm), 112))] };
      props['cubeuv.texelHeight'] = { kind: 'f32', value: [1 / h] };
      props['cubeuv.maxMip'] = { kind: 'f32', value: [mm] };
    }
    if (own) { needs(mat, 'envMap'); mat.envMap = rt.texture; } else scene.environment = rt.texture;
    // the rotation uniform is the transposed rotation matrix (MaterialProperties.js), a mat4
    const rx = sentinel(), ry = sentinel(), rz = sentinel();
    const rot = own ? mat.envMapRotation : scene.environmentRotation;
    rot.set(rx, ry, rz);
    const m = new THREE.Matrix4().makeRotationFromEuler(rot).transpose();
    props[own ? 'material.envMapRotation' : 'scene.environmentRotation'] = { kind: 'mat4', value: Array.from(m.elements) };
  }
  // the depth kind keeps only the caster program's inputs
  if (kind === 'depth') for (const k of Object.keys(props)) if (!/^(object\.|skin\.|morph\.)/.test(k)) delete props[k];
  if (kind === 'output') {
    for (const k of Object.keys(props)) if (!/^renderer\./.test(k)) delete props[k];
    props['renderer.toneMappingExposure'] = { kind: 'f32', value: [exposure], optional: true };   // only tone-mapped passes read it
  }
  let outRo = null;
  let ro = null, casterRo = null;
  const origDraw = renderer.backend.draw.bind(renderer.backend);
  renderer.backend.draw = (r, info) => { if (r.object === mesh) { if (r.material === mat) ro = r; else casterRo = r; } else outRo = r; return origDraw(r, info); };
  renderer.render(scene, camera);
  if (backend === 'wgpu') await new Promise((r) => setTimeout(r, 20)); else gl.finish();
  if (!ro) throw new Error(`${stateName}/${backend}: the mesh was not drawn`);
  if (wantShadow && !casterRo) throw new Error(`${stateName}/${backend}: the mesh was not drawn by the shadow pass`);
  if (kind === 'output') {
    if (!outRo) throw new Error(`${stateName}/${backend}: no output pass was drawn`);
    ro = outRo;
  } else if (kind === 'depth') {
    // the caster program, drawn with the shadow camera
    ro = casterRo;
    if (mesh.isSkinnedMesh) props['skin.bindMatrixInverse'] = { kind: 'mat4', value: Array.from(mesh.bindMatrixInverse.elements) };
    const sc = scene.children.find((o) => o.isDirectionalLight).shadow.camera;
    props['camera.projectionMatrix'] = { kind: 'mat4', value: Array.from(sc.projectionMatrix.elements) };
    props['camera.matrixWorldInverse'] = { kind: 'mat4', value: Array.from(sc.matrixWorldInverse.elements) };
  } else {
    // the projection matrix as the renderer made it for its coordinate system
    // (WebGPU's depth range differs from GL's), read after the render
    props['camera.projectionMatrix'] = { kind: 'mat4', value: Array.from(camera.projectionMatrix.elements) };
    // a SkinnedMesh in AttachedBindMode recomputes bindMatrixInverse from its own matrixWorld each update
    if (mesh.isSkinnedMesh) props['skin.bindMatrixInverse'] = { kind: 'mat4', value: Array.from(mesh.bindMatrixInverse.elements) };
    // a shadow's matrix (the light camera's projection x view x bias), made by the shadow pass
    if (wantShadow) {
      const counters = { directional: 0, spot: 0, point: 0 };
      for (const l of scene.children) {
        if (!(l.isDirectionalLight || l.isSpotLight || l.isPointLight) || !l.castShadow) continue;
        const type = l.isDirectionalLight ? 'directional' : l.isSpotLight ? 'spot' : 'point';
        props[`${type}[${counters[type]++}].shadow.matrix`] = { kind: 'mat4', value: Array.from(l.shadow.matrix.elements) };
      }
    }
  }

  return { scene, mat, mesh, ro, props };
  })();
  const { scene, mat, mesh, props } = M;
  let ro = M.ro;
  async function auxCapture() {
    const props = {};
    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(60, 1, 0.1, 100);
    camera.position.set(sentinel(), 0.2, 4); camera.lookAt(0, 0, 0); camera.updateMatrixWorld();
    let want = null;      // (renderObject) -> is it the draw to capture
    const pm = kind.startsWith('pmrem') ? new THREE.PMREMGenerator(renderer) : null;
    const smallScene = () => { const sc = new THREE.Scene(); sc.add(new THREE.Mesh(new THREE.SphereGeometry(10, 8, 6), new THREE.MeshBasicMaterial({ color: 0x8090ff, side: THREE.BackSide }))); return sc; };
    if (kind.startsWith('bg')) {
      if (kind === 'bgcube') scene.background = cubeTexture(THREE.CubeReflectionMapping);
      else if (kind === 'bgpmrem') {
        const pg = new THREE.PMREMGenerator(renderer);
        const size = PROBE ? 128 : 256;
        scene.background = pg.fromScene(smallScene(), 0.04, 0.1, 100, { size }).texture;
        const v = sentinel(); scene.backgroundBlurriness = v; props['scene.backgroundBlurriness'] = { kind: 'f32', value: [v] };
        const h = scene.background.image.height, mm = Math.log2(h) - 2;
        props['cubeuv.texelWidth'] = { kind: 'f32', value: [1 / (3 * Math.max(Math.pow(2, mm), 112))] };
        props['cubeuv.texelHeight'] = { kind: 'f32', value: [1 / h] };
        props['cubeuv.maxMip'] = { kind: 'f32', value: [mm] };
      } else {
        const t = dataTexture(); scene.background = t;
        t.offset.set(sentinel(), sentinel()); t.repeat.set(1 + sentinel(), 1 + sentinel()); t.updateMatrix();
        props['background.matrix'] = { kind: 'mat3', value: Array.from(t.matrix.elements) };
        props['renderer.viewportSize'] = { kind: 'vec2', value: [WORLD.vw, WORLD.vh], optional: true };
        props['renderer.viewport'] = { kind: 'vec4', value: [WORLD.vx, WORLD.vy, WORLD.vw, WORLD.vh], optional: true };
        props['renderer.drawingBufferSize'] = { kind: 'vec2', value: [WORLD.w, WORLD.h], optional: true };
      }
      const vi = sentinel(); scene.backgroundIntensity = vi; props['scene.backgroundIntensity'] = { kind: 'f32', value: [vi] };
      if (kind !== 'bg2d') {
        scene.backgroundRotation.set(sentinel(), sentinel(), sentinel());
        const m4 = new THREE.Matrix4().makeRotationFromEuler(scene.backgroundRotation);
        props['scene.backgroundRotation'] = { kind: 'mat4', value: Array.from(m4.elements), optional: true };
        props['scene.backgroundRotationT'] = { kind: 'mat4', value: Array.from(m4.clone().transpose().elements), optional: true };
        props['scene.backgroundRotation3'] = { kind: 'mat3', value: Array.from(new THREE.Matrix3().setFromMatrix4(m4).elements), optional: true };
        props['scene.backgroundRotation3T'] = { kind: 'mat3', value: Array.from(new THREE.Matrix3().setFromMatrix4(m4).transpose().elements), optional: true };
      }
      props['camera.matrixWorldInverse'] = { kind: 'mat4', value: Array.from(camera.matrixWorldInverse.elements) };
      want = (r) => r.object && r.object.name === 'Background.mesh';
    } else if (kind === 'pmremcube' || kind === 'pmremequi') {
      want = (r) => r.material && r.material.name === (kind === 'pmremcube' ? 'PMREM_cubemap' : 'PMREM_equirect');
    } else if (kind === 'pmremggx') {
      const n = lod + 3, target = 1 / (n - 1);
      props['pmrem.roughness'] = { kind: 'f32', value: [target * target * 1.25] };
      props['pmrem.mipInt'] = { kind: 'f32', value: [lod] };
      want = (r) => r.material && r.material.name === 'PMREM_ggx';
    } else if (kind === 'pmremblur') {
      props['pmrem.sigma'] = { kind: 'f32', value: [0.3 / Math.SQRT2] };
      props['pmrem.mipInt'] = { kind: 'f32', value: [lod] };
      want = (r) => r.material && r.material.name === 'PMREM_blur';
    }
    let got = null;
    const origDraw = renderer.backend.draw.bind(renderer.backend);
    renderer.backend.draw = (r, info) => { if (!got && want(r)) got = r; return origDraw(r, info); };
    if (kind.startsWith('bg')) renderer.render(scene, camera);
    else if (kind === 'pmremcube') pm.fromCubemap(cubeTexture(THREE.CubeReflectionMapping));
    else if (kind === 'pmremequi') { const e = dataTexture(); e.mapping = THREE.EquirectangularReflectionMapping; pm.fromEquirectangular(e); }
    else if (kind === 'pmremggx') pm.fromScene(smallScene(), 0, 0.1, 100, { size: 1 << lod });
    else pm.fromScene(smallScene(), 0.3, 0.1, 100, { size: 1 << lod });
    if (backend === 'wgpu') await new Promise((r) => setTimeout(r, 20)); else gl.finish();
    if (!got) throw new Error(`${stateName}/${backend}: the ${kind} draw was not seen`);
    if (kind.startsWith('bg')) props['camera.projectionMatrix'] = { kind: 'mat4', value: Array.from(camera.projectionMatrix.elements) };
    return { scene, mat: got.material, mesh: got.object, ro: got, props };
  }
  // the bindings and the fingerprint
  const stagesText = () => { const n = ro.getNodeBuilderState(); stagesText.nbs = n; return { vertex: n.vertexShader, fragment: n.fragmentShader }; };
  const textureSource = (t) => { if (!t) return null;
    if (mesh.isSkinnedMesh && t === mesh.skeleton.boneTexture) return 'boneTexture';
    if (t.isDataArrayTexture && features.includes('morph')) return 'morphTexture';
    for (const l of scene.children) if (l.shadow && (t === l.shadow.map?.depthTexture || t === l.shadow.map?.texture)) {
      const type = l.isDirectionalLight ? 'directional' : l.isSpotLight ? 'spot' : 'point';
      return `shadow:${type}[${scene.children.filter((o) => o.constructor === l.constructor && o.castShadow).indexOf(l)}]`;
    }
    for (const k of ['map', 'normalMap', 'aoMap', 'emissiveMap', 'roughnessMap', 'metalnessMap', 'alphaMap', 'specularMap', 'envMap', 'lightMap', 'bumpMap', 'displacementMap', 'clearcoatNormalMap']) if (mat[k] === t) return k;
    if (scene.environment && t === scene.environment) return 'environment';   /* (the material's own is 'envMap') */
    if (t.name === 'DFG_LUT') return 'dfg_lut';
    if (kind === 'output') return 'output';   // the framebuffer target the scene was drawn into
    if (AUX.has(kind)) return kind.startsWith('bg') ? 'background' : 'pmrem.source';
    if (t.isFramebufferTexture) return 'viewport';   // ViewportTextureNode: a mipmapped copy of the framebuffer (transmission)
    return 'unknown:' + (t.name || t.constructor?.name); };
  const bufOf = (b) => (b.buffer instanceof ArrayBuffer ? b.buffer : b.buffer.buffer ?? b.buffer);
  const groups = [];
  for (const g of ro.getBindings()) {
    const G = { name: g.name, index: g.index, bindings: [] };
    for (const b of g.bindings) {
      if (b.uniforms) {
        const bytes = new Uint8Array(bufOf(b)).slice();
        G.bindings.push({ kind: 'uniforms', name: b.name, byteLength: bytes.length,
          uniforms: b.uniforms.map((u) => ({ name: u.name, type: u.constructor.name.replace('NodeUniform', ''), offset: (u.offset ?? 0) * 4, floats: u.itemSize ?? null})), _bytes: bytes });
      } else if (b.isSampledTexture) G.bindings.push({ kind: 'texture', name: b.name, texture: b.texture?.constructor?.name, source: textureSource(b.texture), ...textureLayout(b, features) });
      else if (b.isSampler) G.bindings.push({ kind: 'sampler', name: b.name, source: textureSource(b.texture), compare: !!b.texture?.isDepthTexture && b.texture.compareFunction !== null });
      else if (b.isStorageBuffer || b.isUniformBuffer) {
        // (a uniform array, e.g. morph influences, is a buffer binding too: its bytes are searched like a uniform struct's)
        let bytes = null;
        try { const raw = bufOf(b); if (raw) bytes = new Uint8Array(raw).slice(); } catch (e) { bytes = null; }
        // what the buffer carries, by its size (r186 names them NodeBuffer_<id>):
        // instance matrices (count x mat4), bone matrices (bones x mat4), morph
        // influences (targets x 16 bytes, a std140 float array)
        const bl = b.byteLength ?? b.buffer?.byteLength ?? null;
        let source = null;
        if (mesh.isInstancedMesh && bl === mesh.count * 64) source = 'instanceMatrix';
        else if (mesh.isSkinnedMesh && bl === mesh.skeleton.bones.length * 64) source = 'boneMatrices';
        else if (mesh.morphTargetInfluences && bl === mesh.morphTargetInfluences.length * 16) source = 'morphInfluences';
        G.bindings.push({ kind: 'buffer', name: b.name, storage: !!b.isStorageBuffer, byteLength: bl, source, class: b.constructor.name, _bytes: bytes });
      }
      else G.bindings.push({ kind: b.constructor.name, name: b.name });
    }
    groups.push(G);
  }
  // A buffer binding's object name (UniformBuffer_0) is not its shader name
  // (NodeBuffer_<id>): read the declarations, in order, and check the counts
  {
    const code = Object.values(stagesText()).join('\n');
    const declared = [...code.matchAll(backend === 'wgpu' ? /var<(?:uniform|storage[^>]*)>\s+(NodeBuffer_\d+)\b/g : /^uniform (NodeBuffer_\d+) \{/gm)].map((m) => m[1]);
    const bufs = groups.flatMap((G) => G.bindings.filter((B) => B.kind === 'buffer'));
    const uniq = [...new Set(declared)];
    if (uniq.length !== bufs.length) throw new Error(`${stateName}/${backend}: ${bufs.length} buffer binding(s) but ${uniq.length} NodeBuffer declaration(s) in the shaders`);
    bufs.forEach((B, i) => { B.shaderName = uniq[i]; });
  }
  if (process.env.GEN_DEBUG) for (const G of groups) for (const B of G.bindings) if (B._bytes) console.error(`[debug] ${G.name}/${B.name}:`, Array.from(new Float32Array(B._bytes.buffer, B._bytes.byteOffset, B._bytes.length >> 2)).map((x) => +x.toPrecision(6)).join(' '));
  // search every uniform buffer for each sentinel pattern (float32, consecutive)
  const map = {};
  for (const [path, p] of Object.entries(props)) {
    const want = p.value.map(fround);
    const stride = p.kind === 'mat3' ? 4 : 1, cols = p.kind === 'mat3' ? 3 : 1, per = p.kind === 'mat3' ? 3 : want.length;
    // every place the value is uploaded: r186 can hold the same quantity in
    // several uniforms (a normal matrix for world and for view); the first is
    // the property, the others are `path#1`, `path#2` ...
    const hits = [];
    for (const G of groups) for (const B of G.bindings) if (B._bytes) {
      const f = new Float32Array(B._bytes.buffer, B._bytes.byteOffset, B._bytes.length >> 2);
      const span = (cols - 1) * stride + per;
      for (let i = 0; i + span <= f.length; i++) {
        let ok = true;
        for (let c = 0; c < cols && ok; c++) for (let k = 0; k < per; k++) { const w = want[c * per + k]; if (Math.abs(f[i + c * stride + k] - w) > 2e-6 * Math.max(1, Math.abs(w))) { ok = false; break; } }
        if (ok) { hits.push({ group: G.name, binding: B.name, byteOffset: i * 4, byteLength: (cols - 1) * stride * 4 + per * 4, layout: p.kind === 'mat3' ? 'std140 mat3 (vec3 columns at 16 bytes)' : undefined }); i += span - 1; }
      }
    }
    map[path] = hits[0] ?? null;
    hits.slice(1).forEach((h, k) => { map[`${path}#${k + 1}`] = h; });
  }
  // each texture's flipY flag (TextureNode: true for render-target, depth and
  // framebuffer textures and flipped ImageBitmaps; GL only) is a uint the
  // capture's DataTextures leave 0. The shader reads it as
  // `v = bool( nodeUniformK )` just before sampling its texture: pair each flag
  // with the next texture( ... ) call's sampler, whose binding names the source
  if (backend === 'gl') {
    const frag = stagesText().fragment;
    const re = /bool\( (nodeUniform\d+) \)|texture(?:Lod|Grad)?\( (nodeUniform\d+),|texelFetch\( (nodeUniform\d+),/g;
    let pend = null, m;
    while ((m = re.exec(frag))) {
      if (m[1]) { pend = m[1]; continue; }
      const sampler = m[2] || m[3];
      if (!pend) continue;
      const tex = groups.flatMap((G) => G.bindings).find((B) => B.kind === 'texture' && B.name === sampler);
      for (const G of groups) for (const B of G.bindings) if (B.kind === 'uniforms') {
        const u = B.uniforms.find((x) => x.name === pend);
        if (u && tex && tex.source) map[`${tex.source}.flipY`] = { group: G.name, binding: B.name, byteOffset: u.offset, byteLength: 4 };
      }
      pend = null;
    }
  }
  // COVERAGE: every uniform r186 uploads must be explained by a found property;
  // one that is not is data the renderer would leave zero (a Lambert whose
  // normal matrix was missing drew with no direct light and no failed check).
  // Its uploaded bytes are kept: the parent compares them with a probe capture
  // in a different world and turns the unchanged ones into constants.
  const uncovered = [];
  for (const G of groups) for (const B of G.bindings) if (B.kind === 'uniforms') for (const u of B.uniforms) {
    const lo = u.offset, hi = u.offset + (u.floats ?? 1) * 4;
    const hit = Object.values(map).some((f) => f && f.group === G.name && f.binding === B.name && f.byteOffset < hi && f.byteOffset + f.byteLength > lo);
    if (!hit) uncovered.push({ group: G.name, binding: B.name, name: u.name, type: u.type, offset: u.offset, bytes: Buffer.from(B._bytes.subarray(lo, Math.min(hi, B._bytes.length))).toString('hex') });
  }
  // two DIFFERENT properties may not claim overlapping bytes; an optional one
  // that sits inside a bigger property (viewportSize inside the viewport vec4)
  // is dropped, anything else is an ambiguous fingerprint and fails the capture
  {
    const base = (k) => k.replace(/#\d+$/, '');
    const keys = Object.keys(map).filter((k) => map[k]);
    for (let a = 0; a < keys.length; a++) for (let b = a + 1; b < keys.length; b++) {
      const A = map[keys[a]], Bq = map[keys[b]];
      if (!A || !Bq || base(keys[a]) === base(keys[b]) || A.group !== Bq.group || A.binding !== Bq.binding) continue;
      if (A.byteOffset < Bq.byteOffset + Bq.byteLength && Bq.byteOffset < A.byteOffset + A.byteLength) {
        const optA = props[base(keys[a])]?.optional, optB = props[base(keys[b])]?.optional;
        if (optA && A.byteLength <= Bq.byteLength) map[keys[a]] = null;
        else if (optB && Bq.byteLength <= A.byteLength) map[keys[b]] = null;
        else throw new Error(`${stateName}/${backend}: ambiguous fingerprint: ${keys[a]} and ${keys[b]} overlap at ${A.group}@${A.byteOffset}`);
      }
    }
    for (const k of Object.keys(map)) if (!map[k] && props[base(k)]?.optional) delete map[k];
  }
  for (const G of groups) for (const B of G.bindings) delete B._bytes;
  // the mesh's programs and attributes, from ITS node builder state (never from
  // a renderer-wide hook: the output pass has programs too)
  const nbs = ro.getNodeBuilderState();
  const geoAttrs = ro.getAttributes();
  if (stagesText.nbs !== nbs) throw new Error('node builder state changed between reads');
  const attributes = nbs.nodeAttributes.map((na, i) => {
    const a = geoAttrs[i];
    return { name: na.name, type: na.type, location: i, itemSize: a?.itemSize, array: a?.array?.constructor?.name, instanced: !!a?.isInstancedBufferAttribute };
  });
  const stages = { vertex: nbs.vertexShader, fragment: nbs.fragmentShader };
  if (nbs.computeShader) stages.compute = nbs.computeShader;
  for (const [st, code] of Object.entries(stages)) {
    if (!code || !/main\s*\(/.test(code)) throw new Error(`${stateName}/${backend}: ${st} shader missing or has no main()`);
    // the mesh's program, not the output pass: the output pass declares no vertex attributes beyond position and samples a framebuffer
    if (st === 'vertex' && attributes.length < 2 && !/position/.test(code)) throw new Error(`${stateName}/${backend}: vertex program is not the mesh's`);
  }
  // (an ambient-only Lambert / Phong needs no normal at all, so the check wants a real light)
  if (kind === 'depth' && !/depth|Depth|fragDepth|builtinClipSpace|gl_Position/.test(stages.vertex)) throw new Error(`${stateName}/${backend}: caster program has no position output`);
  if (LIT.has(kind) && lights.some((n) => n > 0) && !/normal/i.test(stages.fragment) && !/normal/i.test(stages.vertex)) throw new Error(`${stateName}/${backend}: lit material program mentions no normal; wrong program captured`);
  if (features.includes('map') && !/texture/i.test(stages.fragment)) throw new Error(`${stateName}/${backend}: map state program samples no texture`);
  // the template parameters this program bakes, each checked to occur in the text
  const templates = {};
  if (features.includes('skin')) templates.bones = TPL.bones;
  if (features.includes('morph')) { templates.morphs = TPL.morphs; templates.morphWidth = TPL.morphWidth * (features.includes('morphn') ? 2 : 1); }
  // casting counts per type (directional, point, spot) for a shadow state
  const castOut = features.includes('shadow') ? (cast ?? [lights[0], lights[1], lights[2]]) : [0, 0, 0];
  const out = { three: THREE_VERSION, backend, state: stateName, kind, features, lights, cast: castOut, extension, templates, uncovered, stages: Object.keys(stages), groups, attributes, properties: map };
  const missing = Object.entries(map).filter(([k, v]) => !v && !props[k]?.optional).map(([k]) => k);
  for (const k of Object.keys(map)) if (!map[k] && props[k]?.optional) delete map[k];
  renderer.dispose?.();
  return { out, stages, missing };
}

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
  j.constants = []; j.unexplained = [];
  if (j.uncovered.length) {
    const tmp = join(tmpdir(), `gen-programs-probe-${process.pid}`);
    mkdirSync(tmp, { recursive: true });
    captureInChild(state, be, tmp, { GEN_PROBE: '1' });
    const pj = JSON.parse(readFileSync(join(tmp, `${state}.${be}.json`), 'utf8'));
    for (const u of j.uncovered) {
      const q = pj.uncovered.find((x) => x.group === u.group && x.binding === u.binding && x.offset === u.offset && x.type === u.type);
      if (q && q.bytes === u.bytes) j.constants.push({ group: u.group, binding: u.binding, name: u.name, type: u.type, offset: u.offset, bytes: u.bytes });
      else j.unexplained.push({ group: u.group, binding: u.binding, name: u.name, type: u.type, offset: u.offset, primary: u.bytes, probe: q ? q.bytes : null });
    }
  }
  delete j.uncovered;
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
