three.c
=======

The [three.js](https://threejs.org) API in C99, drawing with the programs
three.js r187 (0.187.0) generates, on **WebGPU** (Dawn's `webgpu.h`
natively, Emscripten's emdawnwebgpu in WebAssembly) and on **OpenGL ES 3.0 /
WebGL2**.

It runs natively (EGL, SDL, any GLES 3 context, any `webgpu.h` device) and as
WebAssembly carts for [wasmcart](https://github.com/wasmcart), on GL and on
wasmcart's WebGPU tier.

three.c exists because of three.js and its authors. The API, the scene graph,
the materials, the node renderer and the programs it generates are theirs;
three.c follows them name for name, and [the three.js documentation](https://threejs.org/docs/)
is the reference for what every type and field does. three.js is MIT
licensed (`LICENSE.three.js`).

```c
#include "three.h"

t3_renderer *r = t3_renderer_new(1280, 720);   // GLES 3 / WebGL2
// or WebGPU: t3_renderer_use_wgpu(r, device, queue, WGPUTextureFormat_BGRA8Unorm);

t3_scene *scene = t3_scene_new();
t3_camera *camera = t3_perspective_camera_new(60, 1280.0f / 720, 0.1f, 100);
t3_object_set_position(camera, 0, 1, 5);

t3_geometry *geo = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
t3_material *mat = t3_mesh_standard_material_new(0x44aa88);
t3_mesh *cube = t3_mesh_new(geo, mat);
t3_object_add(scene, cube);
t3_release(cube); t3_release(geo); t3_release(mat);   // the scene keeps them

t3_light *sun = t3_directional_light_new(0xffffff, 3);
t3_object_set_position(sun, 2, 4, 3);
t3_object_add(scene, sun);
t3_release(sun);

t3_renderer_render(r, scene, camera);
```

Names follow three.js: `new THREE.Mesh(geo, mat)` is `t3_mesh_new(geo, mat)`,
`mesh.position` is `mesh->base.position`, `renderer.render(scene, camera)` is
`t3_renderer_render(r, scene, camera)`. Defaults are r187's: colour
management is on (hex colours are sRGB, stored linear), the output colour
space is sRGB, lights are physical. Ownership is reference counting; see the
top of `include/three.h`.

## What it covers

Object3D / Group / Scene, perspective and orthographic cameras, ambient /
hemisphere / directional / point / spot lights with shadows (PCF, 2D and
cube), Fog and FogExp2, Basic / Lambert / Phong / Standard / Physical /
Normal materials (Physical with clearcoat and its normal map, sheen,
iridescence, anisotropy, transmission with thickness and attenuation, ior
and specular intensity / colour) with map, normalMap (tangent space, vertex
tangents), aoMap, emissiveMap, roughnessMap, metalnessMap, alphaMap and
lightMap, vertex colours (rgb, or rgba with the alpha ignored), transparency, alphaTest, flat shading, double and
back sides, Texture / DataTexture / CubeTexture / DepthTexture, Box / Plane
/ Sphere / Cylinder / Cone / Torus / TorusKnot / Circle / Ring / Polyhedron
/ Icosahedron geometry, InstancedMesh (with instance colours), SkinnedMesh /
Bone / Skeleton, morph targets (positions and normals), render targets
(depth textures, mipmaps, half float and float, multisample, depth-only, cube
targets drawn by face and mip level), a reversed depth buffer (WebGPU, and GLES
with EXT_clip_control), a second UV set on any built-in map (`texture.channel`),
rough diffuse on Physical (`diffuse_roughness`, EON), every r187
tone mapping (Linear, Reinhard, Cineon, ACESFilmic, AgX, Neutral),
environment maps (PMREM cube targets for Standard / Physical, cube reflection and
refraction for Basic / Lambert / Phong), PMREMGenerator (fromCubemap,
fromEquirectangular, fromScene with its size, position and renderTarget
options, each able to draw into a target it made before, as a reflection
probe refreshed every few frames does), scene backgrounds and scene.environment,
Sprite and Points, Line, LOD, Raycaster, curves, AnimationMixer, a glTF 2.0
loader (cgltf + stb_image: PBR materials with their names and the factors of
KHR_materials_sheen / clearcoat / specular / ior / transmission / volume /
emissive_strength / iridescence / anisotropy, skins, morph targets with
their names (morphTargetDictionary), animations)
and BufferGeometryLoader. Header-only bridges to Box2D v3 (`three_box2d.h`)
and Box3D (`three_box3d.h`).

For embedders: `t3_set_allocator`, world matrices the embedder owns
(`t3_object_set_matrix_world`), per-render GPU timing, the shadow caster pass
into any target (`t3_renderer_render_depth`, GLES), the opaque queue alone
(`t3_renderer_render_opaque`, alpha-tested cutouts included) and a depth
prepass of it that writes no colour and skips the background and output pass
(`t3_renderer_render_prepass`, GLES), and GL / WebGPU hooks to draw into the
embedder's own framebuffer, command encoder and swapchain view (on WebGPU
with a viewport depth range of its choice: `depth_min` / `depth_max`). Custom
shading is written as r187 TSL in a generator extension, captured into a
program table like any other state, and filled at run time from a C hook
(`t3_material_set_extension`, `t3_renderer_set_extension`, and
`t3_renderer_set_extension_batching` to auto-instance an extension whose hook
reads only per-frame values; see docs/r187-renderer.md, "Extension
programs").

Not covered yet: wireframe, premultipliedAlpha, dithering, mirrored repeat
on a map, an equirectangular
background without blurriness, and TSL written at run time (an extension's
TSL is captured offline). A draw the program tables do not cover is skipped and named by
`t3_renderer_generated_missing`.

## How it draws

three.js r187 does not ship shaders: its node renderer builds each program
from the material, the object and the scene at run time.
`tools/gen-programs.mjs` runs unmodified three.js 0.187.0 (on webgl-node and
webgpu-node) for each material state three.c supports and captures the
program it compiles, in GLSL and in WGSL, together with a manifest of its
bind groups, uniform layouts, textures and vertex attributes. Which three.js
property feeds each uniform byte is found by giving every property a value
nothing else has and searching the uploaded bytes for it; a uniform nothing
explains fails the capture. `tools/emit-programs.mjs` turns the captures into
C tables (`src/gen/programs_gl.c`, `src/gen/programs_wgpu.c`), and the
renderer picks the program for each draw by material kind, features and
light counts. [docs/r187-renderer.md](docs/r187-renderer.md) explains the
pipeline, the state names, how to add a state and how an application builds
its own table (`tools/gen-project-table.mjs`, `t3_register_program_table`,
three.c built with `T3_TABLE=core`). Built with `T3_BUILDER=1`, three.c also
carries three.js itself in an embedded QuickJS and builds a state no table
has on first use, the same program the tables would hold.

## Same pixels as three.js r187

`tools/parity-r187.mjs` renders 44 scenes (three.lua's compare scenes and
three.c's feature scenes in `test/scenes`) with three.c and with unmodified
three.js r187 on the same backend, and compares frame 100 (frame 3 for the
compare scenes). A scene matches when at most 8 pixels differ by more than 2
levels, which is what rasterising the same triangles through different float
paths produces.

| Backend | three.js reference | Match | Differ |
| --- | --- | --- | --- |
| WebGPU | r187 WebGPURenderer on webgpu-node | 44 | 0 |
| GLES / WebGL2 | r187 WebGPURenderer (forceWebGL) on webgl-node | 36 | 6: 1 r187 defect and 5 notes below |

The one defect is three.js's, on WebGL: a DepthTexture map and a colour map
share one program, so the first built wins (`tools/r187-defects/` reproduces it
with three.js alone). three.c draws the depth quad correctly, as r187's WebGPU
backend does. r187 fixed the other three defects this list had on r186 (a
Sprite's `center`, Phong `combine`, a program's first environment map), so
parity no longer excuses them.

On GLES, with nothing blended, no tone mapping and an sRGB output, three.c
encodes colour in each program and draws straight to the screen instead of
drawing into a half-float target and encoding in a separate pass, which
saves about 0.07 ms per 720p frame of GPU time. The five GL notes come from
the native bench drawing straight into its EGL pbuffer surface, where a few
pixel-centre ties on triangle edges (specular sparkles, one box edge)
resolve differently. The same three.c frames drawn into a framebuffer
object match r187 at 0 pixels: through a wasmcart GL cart, or with
`t3_renderer_set_exact_output`, which takes r187's target and output
pass.

Carts were also checked on every wasmcart host: a dual GL / WebGPU cart
renders the same frames on wasmcart's Node host and wasmcart-native, and in
Chromium it matches r187's WebGPURenderer on the same adapter
(`tools/cart-browser.mjs`, `tools/ref-r187-browser.mjs --webgpu`).

## Performance

First-use (compile) stalls, frame hitches and per-draw CPU cost on both
backends have their own suite with a baseline and a regression check:
[docs/perf.md](docs/perf.md) (`node bench/perf.mjs`).

Milliseconds per frame at 1280x720 on an AMD Radeon RX 7600 (Linux, Mesa),
median of two rounds of three timed runs (`bench/matrix.mjs` in three.lua):

| Scene | three.c GLES | three.c WebGPU | three.js r187 WebGL2 | three.js r187 WebGPU |
| --- | ---: | ---: | ---: | ---: |
| 01-cubes | 0.006 | 0.063 | 0.021 | 0.075 |
| 05-heavy | 0.173 | 0.164 | 7.217 | 14.232 |
| 06-heavy-instanced | 0.078 | 0.071 | 0.129 | 0.193 |
| 11-mixed | 0.096 | 0.144 | 1.952 | 5.575 |
| 12-suzanne | 0.111 | 0.139 | 0.122 | 0.152 |

The three.js columns are Node (webgl-node, webgpu-node). On WebGPU, every
frame pays Dawn's queue submit (about 25 us); `t3_renderer_wgpu_defer` and
`t3_renderer_wgpu_submit` keep that to one submit per frame however many
renders it has.

What makes it fast:

- automatic instancing: opaque meshes that share a geometry and a material
  are drawn as one instanced draw with r187's instanced program
- an InstancedMesh's changed instance data goes to the GPU, on GLES, as new
  buffer storage for the instances drawn (`glBufferData`, not an update in
  place), so the upload never waits for the GPU to finish reading the
  previous frame's copy
- per-draw uniform blocks are streamed from one buffer per frame (one upload
  on GLES, dynamic offsets on WebGPU); per-frame uniforms are filled once per
  program; GL state, pipelines and bind groups are cached
- the scene is flattened and culled in one pass, with a stable merge sort
  (`t3_renderer_set_sort_bounds` sorts opaque meshes by their world bounding
  sphere's centre instead of their origin, for geometry built far from its
  object's origin, such as merged world-space chunks and instanced scenery)
- SIMD matrix math, LTO, no per-frame allocation

## Build

    ./build.sh                       # build/native: libthree.a, GLES 3 + the bench host
    npm install                      # Node tools, and native-dawn for WebGPU
    WGPU=1 ./build.sh                # build/native-wgpu: GLES 3 and WebGPU
    wasm/build.sh                    # build/wasm: one GL cart per bench scene
    tools/fetch-emdawnwebgpu.sh      # Dawn's emdawnwebgpu port, for:
    WGPU=1 wasm/build.sh             # dual GL / WebGPU carts
    test/run.sh                      # the C tests

The library needs a C99 compiler and GLES 3 headers (EGL / GLESv2 / zlib for
the bench host). WebGPU needs Dawn's `webgpu.h` and `libwebgpu_dawn`:
`NATIVE_DAWN=<dir with include/ and lib/>`, by default the native-dawn
package `npm install` puts in `node_modules`. Carts need Emscripten and a
wasmcart checkout (`WASMCART`, default `../wasmcart`). The bench host and the
physics demos need Box2D v3 and Box3D checkouts (`BOX2D`, `BOX3D`, default
`../box2d`, `../box3d`). The compare scenes and their assets come from a
three.lua checkout (`THREE_LUA`, default `../three.lua`).

## Tools

`npm install` brings webgl-node, webgpu-node and Playwright (run
`npx playwright install chromium` once for the browser tools). Each can also
come from a checkout (`WEBGL_NODE`, `WEBGPU_NODE`, `PLAYWRIGHT`) or a
wasmcart checkout's `node_modules`.

    node tools/parity-r187.mjs [--wgpu] [scene ...]          # three.c vs three.js r187
    node tools/gen-programs.mjs [--states a,b] [--check]     # capture r187's programs
    node tools/emit-programs.mjs                             # captures -> src/gen tables
    node tools/ref-r187.mjs <scene> out.png [frames] [--webgpu]
    node tools/ref-r187-browser.mjs <scene> out.png [frames] [--webgpu]
    node tools/cart-wgpu.mjs <cart.wasc> out.png [frames] [--gl]
    node tools/cart-browser.mjs <cart.wasc> out.png [frames] [--gl]

three.js 0.187.0 is downloaded from npm into `build/three-r187` on first use.

## Versions

three.c's versions follow the three.js release they match: `0.187.N` is
three.js r187, and N counts three.c's own releases on it (fixes and features
that keep the same three.js). A new three.js release starts the next series
(`0.188.0`). `T3_VERSION` and `T3_REVISION` in `three.h` say which.

## License

MIT (`LICENSE`). three.c includes code and data from three.js (MIT,
`LICENSE.three.js`): the r187 generated programs, r187's DFG lookup table
and the PMREM and mipmap algorithms it ports. `third_party/` holds cgltf
(MIT) and stb_image (public domain / MIT). The test models in `test/assets`
are Khronos glTF samples under CC0 1.0 and CC-BY 4.0, each with its license
file beside it.
