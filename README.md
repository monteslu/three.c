three.c
=======

The [three.js](https://threejs.org) API in C99, drawing with the programs
three.js r186 (0.186.1) generates, on **WebGPU** (Dawn's `webgpu.h`
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
`t3_renderer_render(r, scene, camera)`. Defaults are r186's: colour
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
lightMap, vertex colours, transparency, alphaTest, flat shading, double and
back sides, Texture / DataTexture / CubeTexture / DepthTexture, Box / Plane
/ Sphere / Cylinder / Cone / Torus / TorusKnot / Circle / Ring / Polyhedron
/ Icosahedron geometry, InstancedMesh (with instance colours), SkinnedMesh /
Bone / Skeleton, morph targets (positions and normals), render targets
(depth textures, mipmaps, half float and float, multisample), every r186
tone mapping (Linear, Reinhard, Cineon, ACESFilmic, AgX, Neutral),
environment maps (PMREM cube-UV for Standard / Physical, cube reflection and
refraction for Basic / Lambert / Phong), PMREMGenerator (fromCubemap,
fromEquirectangular, fromScene), scene backgrounds and scene.environment,
Sprite and Points, Line, LOD, Raycaster, curves, AnimationMixer, a glTF 2.0
loader (cgltf + stb_image: PBR materials, skins, morph targets, animations)
and BufferGeometryLoader. Header-only bridges to Box2D v3 (`three_box2d.h`)
and Box3D (`three_box3d.h`).

For embedders: `t3_set_allocator`, world matrices the embedder owns
(`t3_object_set_matrix_world`), per-render GPU timing, the shadow caster pass
into any target (`t3_renderer_render_depth`, GLES), and GL / WebGPU hooks to
draw into the embedder's own framebuffer, command encoder and swapchain view.

Not covered yet: wireframe, premultipliedAlpha, dithering, mirrored repeat
on a map, a texture's `channel` (a second UV set), an equirectangular
background without blurriness, and the node materials' own extension points
(TSL). A draw the program tables do not cover is skipped and named by
`t3_renderer_generated_missing`.

## How it draws

three.js r186 does not ship shaders: its node renderer builds each program
from the material, the object and the scene at run time.
`tools/gen-programs.mjs` runs unmodified three.js 0.186.1 (on webgl-node and
webgpu-node) for each material state three.c supports and captures the
program it compiles, in GLSL and in WGSL, together with a manifest of its
bind groups, uniform layouts, textures and vertex attributes. Which three.js
property feeds each uniform byte is found by giving every property a value
nothing else has and searching the uploaded bytes for it; a uniform nothing
explains fails the capture. `tools/emit-programs.mjs` turns the captures into
C tables (`src/gen/programs_gl.c`, `src/gen/programs_wgpu.c`), and the
renderer picks the program for each draw by material kind, features and
light counts. [docs/r186-renderer.md](docs/r186-renderer.md) explains the
pipeline, the state names, how to add a state and how an application builds
its own table (`tools/gen-project-table.mjs`, `t3_register_program_table`,
three.c built with `T3_TABLE=core`). Built with `T3_BUILDER=1`, three.c also
carries three.js itself in an embedded QuickJS and builds a state no table
has on first use, the same program the tables would hold.

## Same pixels as three.js r186

`tools/parity-r186.mjs` renders 34 scenes (three.lua's compare scenes and
three.c's feature scenes in `test/scenes`) with three.c and with unmodified
three.js r186 on the same backend, and compares frame 100 (frame 3 for the
compare scenes). A scene matches when at most 8 pixels differ by more than 2
levels, which is what rasterising the same triangles through different float
paths produces.

| Backend | three.js reference | Match | Differ |
| --- | --- | --- | --- |
| WebGPU | r186 WebGPURenderer on webgpu-node | 31 | 3, all r186 defects below |
| GLES / WebGL2 | r186 WebGPURenderer (forceWebGL) on webgl-node | 25 | 9: 4 r186 defects, 4 notes and 1 open bug below |

The defects are three.js's. `tools/r186-defects/` reproduces each with
three.js alone (no three.c) in Chromium, next to a control that shows the
correct result: the same object alone in a fresh renderer. All four reproduce
on both backends in r186 and in the three.js dev branch as of r187dev (in
r186 the minimal environment-map page draws no environment at all, so that
one is shown there with test/scenes/r5-pmrem.js against the same scene with
one row of spheres). In each case a program is shared between objects or
materials that should not share it:

- A Sprite takes another Sprite's `center`.
- A Phong material with `combine` Add draws as Mix when a Mix material is in
  the scene.
- Every Standard material of a program reflects the first one's
  environment map.
- A DepthTexture map and a colour map share one program: the colour map
  samples black on WebGL; on WebGPU the draws fail validation. (The r1
  scene does not hit it on WebGPU, so parity lists it for WebGL only.)

On GLES, with nothing blended, no tone mapping and an sRGB output, three.c
encodes colour in each program and draws straight to the screen instead of
drawing into a half-float target and encoding in a separate pass, which
saves about 0.07 ms per 720p frame of GPU time. The four GL notes come from
the native bench drawing straight into its EGL pbuffer surface, where a few
pixel-centre ties on triangle edges (specular sparkles, one box edge)
resolve differently. The same three.c frames drawn into a framebuffer
object match r186 at 0 pixels: through a wasmcart GL cart, or with
`t3_renderer_set_exact_output`, which takes r186's target and output
pass.

Open bug: on GLES, the back faces of a BackSide or double-sided transmissive
Physical material show a grid of about 4 pixels where r186 is smooth (the
p2-transmission scene's double-sided block). WebGPU matches r186 there, and
front-sided transmission matches on both backends.

Carts were also checked on every wasmcart host: a dual GL / WebGPU cart
renders the same frames on wasmcart's Node host and wasmcart-native, and in
Chromium it matches r186's WebGPURenderer on the same adapter
(`tools/cart-browser.mjs`, `tools/ref-r186-browser.mjs --webgpu`).

## Performance

Milliseconds per frame at 1280x720 on an AMD Radeon RX 7600 (Linux, Mesa),
median of two rounds of three timed runs (`bench/matrix.mjs` in three.lua):

| Scene | three.c GLES | three.c WebGPU | three.js r186 WebGL2 | three.js r186 WebGPU |
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
  are drawn as one instanced draw with r186's instanced program
- per-draw uniform blocks are streamed from one buffer per frame (one upload
  on GLES, dynamic offsets on WebGPU); per-frame uniforms are filled once per
  program; GL state, pipelines and bind groups are cached
- the scene is flattened and culled in one pass, with a stable merge sort
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

    node tools/parity-r186.mjs [--wgpu] [scene ...]          # three.c vs three.js r186
    node tools/gen-programs.mjs [--states a,b] [--check]     # capture r186's programs
    node tools/emit-programs.mjs                             # captures -> src/gen tables
    node tools/ref-r186.mjs <scene> out.png [frames] [--webgpu]
    node tools/ref-r186-browser.mjs <scene> out.png [frames] [--webgpu]
    node tools/cart-wgpu.mjs <cart.wasc> out.png [frames] [--gl]
    node tools/cart-browser.mjs <cart.wasc> out.png [frames] [--gl]

three.js 0.186.1 is downloaded from npm into `build/three-r186` on first use.

## License

MIT (`LICENSE`). three.c includes code and data from three.js (MIT,
`LICENSE.three.js`): the r186 generated programs, r186's DFG lookup table
and the PMREM and mipmap algorithms it ports. `third_party/` holds cgltf
(MIT) and stb_image (public domain / MIT). The test models in `test/assets`
are Khronos glTF samples under CC0 1.0 and CC-BY 4.0, each with its license
file beside it.
