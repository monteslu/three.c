// The WebGPU vertex buffers three.c binds (tools/program-model.mjs vertexBufferLayout, the same as
// src/gen_wgpu.c t3_wgpu_pipeline) for the program with every optional attribute at once: an
// InstancedMesh with position, normal, uv, color and tangent, the instance matrix and the instance colour
// (test/run.sh captures it into build/test/vbtab/cache for test_vertex_buffers).
//
//   node test/test_vertex_buffer_layout.mjs <capture dir> <state>
//
// 1. the layout: the four matrix columns in one instance-step buffer (64 bytes, offsets 0 16 32 48), the
//    instance colour in its own, one per geometry attribute: 7, under the 8 every WebGPU device has;
// 2. programModel refuses a program over 8 (nine geometry attributes), naming them;
// 3. with webgpu-node on lavapipe (software Vulkan, no GPU): Dawn makes the captured program's pipeline
//    with that layout, and the control MUST fail: the same program with one buffer per attribute (each
//    matrix column its own: 10) is refused for its vertex buffer count. Skipped without webgpu-node or
//    lavapipe.
import { existsSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { programModel, vertexBufferLayout, WEBGPU_MAX_VERTEX_BUFFERS } from '../tools/program-model.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const [dir, state] = process.argv.slice(2);
if (!dir || !state) { console.error('usage: node test/test_vertex_buffer_layout.mjs <capture dir> <state>'); process.exit(2); }
// Dawn runs on lavapipe only, never a hardware GPU: re-run with only its driver visible
const wn = join(ROOT, 'node_modules', 'webgpu-node', 'index.mjs');
const icd = ['/usr/share/vulkan/icd.d/lvp_icd.json', '/usr/share/vulkan/icd.d/lvp_icd.x86_64.json'].find(existsSync);
if (existsSync(wn) && icd && process.env.VK_ICD_FILENAMES !== icd) {
  const { spawnSync } = await import('node:child_process');
  const r = spawnSync(process.execPath, [fileURLToPath(import.meta.url), dir, state], { stdio: 'inherit',
    env: { ...process.env, VK_ICD_FILENAMES: icd, VK_DRIVER_FILES: icd, LP_NUM_THREADS: '2', DISPLAY: '', WAYLAND_DISPLAY: '' } });
  process.exit(r.status ?? 1);
}
let failed = 0;
const ok = (c, m) => { console.log(`${c ? 'ok  ' : 'FAIL'} ${m}`); if (!c) failed++; };

const json = JSON.parse(readFileSync(join(dir, state + '.wgpu.json'), 'utf8'));
const vert = readFileSync(join(dir, state + '.wgpu.vert'), 'utf8');
const frag = readFileSync(join(dir, state + '.wgpu.frag'), 'utf8');
const names = json.attributes.map((a) => a.name);
for (const n of ['position', 'normal', 'uv', 'color', 'tangent', 'nodeAttribute0', 'nodeAttribute3'])
  ok(names.includes(n), `the capture reads ${n}`);
ok(json.attributes.some((a) => /^nodeAttribute\d+$/.test(a.name) && a.type === 'vec3' && a.instanced), 'the capture reads the instance colour');

// 1. the layout
const layout = vertexBufferLayout(json.attributes);
const inst = layout.filter((b) => b.stepMode === 'instance');
const matrix = inst.find((b) => b.attributes.length === 4);
ok(layout.length === 7, `7 vertex buffers (got ${layout.length}), at most ${WEBGPU_MAX_VERTEX_BUFFERS}`);
ok(matrix && matrix.arrayStride === 64 && matrix.attributes.map((a) => a.offset).join() === '0,16,32,48', 'the matrix columns share one 64-byte instance buffer');
ok(inst.length === 2 && inst.some((b) => b.arrayStride === 12 && b.attributes[0].format === 'float32x3'), 'the instance colour has an instance buffer of its own');
ok(vertexBufferLayout(json.attributes, { batched: true }).find((b) => b.attributes.length === 4).arrayStride === 80, 'a batch record is 80 bytes');

// 2. over the limit
const nine = ['position', 'normal', 'uv', 'color', 'tangent', 'uv1', 'skinIndex', 'skinWeight', 'uv2'].map((name, location) =>
  ({ name, type: name === 'skinIndex' ? 'uvec4' : 'vec4', location, itemSize: 4, instanced: false }));
let refused = null;
try { programModel(state, 'wgpu', { ...json, attributes: nine }, vert, frag); } catch (e) { refused = e.message; }
ok(refused && /9 vertex buffers/.test(refused) && /skinWeight/.test(refused), 'programModel refuses 9 vertex buffers, naming the attributes' + (refused ? '' : ' (it accepted them)'));
let accepted = true;
try { programModel(state, 'wgpu', json, vert, frag); } catch (e) { accepted = false; console.error(e.message); }
ok(accepted, 'programModel accepts the captured program');

// 3. on Dawn
if (!existsSync(wn) || !icd) {
  console.log(`skip: Dawn pipelines (${!existsSync(wn) ? 'no webgpu-node: npm install' : 'no lavapipe'})`);
} else {
  const { createGPU, globals } = await import(pathToFileURL(wn).href);
  Object.assign(globalThis, globals);
  const gpu = createGPU(['backend=vulkan']);
  const adapter = await gpu.requestAdapter({ featureLevel: 'compatibility' });
  ok(adapter && /llvmpipe|lavapipe/i.test(`${adapter.info.device} ${adapter.info.description}`), 'a lavapipe adapter');
  const dev = await adapter.requestDevice();
  ok(dev.limits.maxVertexBuffers === 8, `the device has the default 8 vertex buffers (${dev.limits.maxVertexBuffers})`);
  const vm = dev.createShaderModule({ code: vert }), fm = dev.createShaderModule({ code: frag });
  const pipe = async (buffers) => {
    try {
      await dev.createRenderPipelineAsync({ layout: 'auto', vertex: { module: vm, entryPoint: 'main', buffers },
        fragment: { module: fm, entryPoint: 'main', targets: [{ format: 'rgba16float' }] },
        depthStencil: { format: 'depth24plus', depthWriteEnabled: true, depthCompare: 'less' }, primitive: { topology: 'triangle-list' } });
      return null;
    } catch (e) { return e.message; }
  };
  const good = await pipe(layout);
  ok(good === null, 'Dawn makes the pipeline with three.c\'s layout' + (good ? `: ${good}` : ''));
  // the control: one buffer per attribute, each matrix column its own (what a checker that ignored three.c's layout built)
  const old = json.attributes.map((a) => ({ arrayStride: 16, stepMode: a.instanced ? 'instance' : 'vertex',
    attributes: [{ shaderLocation: a.location, offset: 0, format: vertexBufferLayout([a])[0].attributes[0].format }] }));
  const bad = await pipe(old);
  ok(bad && /Vertex buffer count \(10\) exceeds the maximum number of vertex buffers \(8\)/.test(bad),
    'control: one buffer per attribute (10) is refused for its vertex buffer count' + (bad ? '' : ' (it was accepted)'));
  dev.destroy?.();
}
console.log(failed ? `test_vertex_buffer_layout: ${failed} failed` : 'test_vertex_buffer_layout: ok');
process.exit(failed ? 1 : 0);
