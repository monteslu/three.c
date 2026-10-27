# The r186 renderer

three.js r186 does not ship shaders: its node renderer builds each program
from the material, the object and the scene at run time. three.c renders with
those same programs by capturing them from three.js itself and turning them
into C tables. This document covers how the tables are made, how the
renderer uses them, and what is specific to WebGPU.

## Capturing the programs

`tools/gen-programs.mjs` loads three.js 0.186.1 (downloaded from npm into
`build/three-r186` on first use) and, for each **state**, builds a small scene
that exercises it: one mesh with the material, the lights, the fog, the
shadows. It renders that scene once with `WebGPURenderer` on webgpu-node
(WGSL) and once with `forceWebGL` on webgl-node (GLSL ES 3.00), each in a
fresh Node process, and reads the program back from the mesh's own render
object.

Next to each program it writes a JSON manifest: the bind groups, every
uniform's name, type and byte offset, the textures (sample type and view
dimension) and samplers, the extra buffers (bone matrices, morph influences),
and the vertex attributes.

**Which property feeds which bytes** is found by fingerprinting. Before the
render, every supported property gets a value nothing else in the scene has
(`material.color`, `material.roughness`, each light's colour and position,
the camera matrices and so on). After the render, the uploaded uniform
buffers are searched for each value. Then:

- **Coverage:** every uniform byte must be explained by a property. A uniform
  nothing explains is captured a second time in a different world (another
  buffer size, viewport and seed). If its bytes are the same both times it is
  a constant and recorded with its bytes; if not, the capture fails.
- **Copies:** a value found in more than one place is recorded at each place.
- **Ambiguity:** two properties that claim overlapping bytes fail the
  capture, unless one is optional and smaller than the other.
- **Templates:** a few counts are baked into the program text instead of
  uniforms (bone count, morph target count, the morph texture's width). They
  are captured at values that occur nowhere else, recorded, and substituted
  for the object at hand when the program is built.

`--check` captures every state twice in fresh processes and fails if
anything differs.

`tools/emit-programs.mjs` turns the captures into `src/gen/programs_gl.c`,
`src/gen/programs_wgpu.c` and `src/gen/programs.h`, plus r186's DFG lookup
table (`src/gen/programs_dfg.c`). The tables are checked in, so building
three.c needs neither Node nor three.js.

A backend's shaders differ by a few lines per feature, so a table stores its
distinct lines once and each shader as the list of its lines, deflated
together (about 0.2 MB per backend for the full table's 325 states). The
first program built from a table inflates its text (kept for the table's
other programs) with stb_image's inflater, which three.c already carries for
PNG. A full-table wasm cart is about 1.25 MB (0.9 MB with the core table).

## State names

A state is a material kind, then features, then optional counts, joined
with `+`:

    standard+map+nmap+shadow+l1101+s100

- **Kind:** `basic`, `lambert`, `phong`, `standard`, `physical`, `normal`,
  `points`, `psprite`, `line`, `sprite`; `depth` (the shadow caster);
  `output` (the output pass); `bg*` (scene backgrounds); `pmrem*` (the PMREM
  generator's passes).
- **Features:** the material, object and scene properties that change the
  program text. Examples: `map`, `nmap`, `vcol`, `fog`, `fog2`, `trans`,
  `atest`, `inst`, `instbig`, `icol` (instance colours), `skin`, `morph`,
  `shadow`, `env`, `senv`, `cenv`, `ds` (double sided), `bs` (back side),
  `tm*` (tone mappings), `lin` (linear output). The full list is `FEATURES` in
  `tools/gen-programs.mjs`.
- **`lDPSH`:** the number of directional, point, spot and hemisphere lights.
  Lit kinds default to `l1100` and always have an ambient light.
- **`sDPS`:** how many directional, point and spot lights cast shadows.
- **`z<n>`:** the PMREM target size, for `pmremggx` and `pmremblur`.
- **`x<name>`:** an extension module (`tools/extensions/<name>.mjs`) that
  adds TSL nodes and uniforms of its own before the capture (see
  "Extension programs").

`allStates()` in `tools/gen-programs.mjs` is the base table.
`tools/gen-states.txt` adds the states three.c's own scenes and tests draw;
`tools/emit-programs.mjs` emits exactly those two lists.

## Adding a state

When a scene draws a material state the table lacks, the renderer records
it. `t3_renderer_generated_missing(r)` returns the names, one per line. Then:

    echo "standard+fog+l1001" >> tools/gen-states.txt
    node tools/gen-programs.mjs --states standard+fog+l1001
    node tools/emit-programs.mjs
    ./build.sh

## A project's own table

three.c's own table covers its tests and common states. An application that
draws other combinations, or wants a smaller binary, builds its own:

    node tools/gen-project-table.mjs --name mygame --out gen/ --states mygame-states.txt \
        [--missing missing.log ...] [--cache build/t3-captures] [--check]

- `mygame-states.txt` lists the states the project draws, one per line. The
  same list serves both backends.
- `--missing` merges logs of states a run could not draw into the list. A
  native build writes that log with `T3_MISSING_LOG=<file>`; a cart can collect
  `t3_renderer_generated_missing`.
- Captures are cached, so a rerun only captures new states. Capturing needs
  Node and three.c's `npm install`, but no GPU: it runs on a mock device that
  answers three.js's capability queries the way the recorded real devices did
  (`tools/mock-gpu.mjs`, `tools/gpu-answers.json`), and every capture of the
  full table comes out byte-identical to one on real devices. `--real-gpu`
  captures on webgl-node and webgpu-node instead. The generated C needs none
  of this, so it is usually checked in.

The tool writes `gen/mygame_gl.c` and `gen/mygame_wgpu.c`. Compile them with
three.c's `src/` on the include path and register them before the first
render:

    extern const struct t3_gen_table mygame_gl, mygame_wgpu;
    t3_register_program_table(&mygame_gl, &mygame_wgpu);

Registered tables are searched before three.c's own. Built with
`T3_TABLE=core`, three.c keeps only the programs the renderer draws with
itself (output pass, backgrounds, PMREM, shadow casters: about 0.1 MB per
backend instead of the full table's 0.5 MB), so the project's table is
the only source of material programs.

An embedder that compiles every file in `src/` and `src/gen/` itself (rather
than through `build.sh`) gets the full table; `-DT3_TABLE_CORE` picks the core
one instead. `src/builder.c` compiles to nothing without `-DT3_BUILDER`.

## Building states at run time

Built with `T3_BUILDER=1` (`./build.sh` or `wasm/build.sh`), three.c carries
three.js r186 itself, in an embedded QuickJS (quickjs-ng), and a draw whose
state no table has gets its program built on first use: the same capture
`tools/gen-programs.mjs` runs (`tools/capture-core.mjs` on the mock GPU),
turned into a program by the same code the emitter uses
(`tools/program-model.mjs`), kept for the rest of the process.
`tools/fetch-builder-deps.sh` fetches QuickJS and the three.js package
(checked against their SHA-256); the build embeds them.

    T3_BUILDER=1 T3_TABLE=core ./build.sh

- A built program equals the one the emitter writes for that state, field
  for field (`test/test_builder_parity.c` builds every state of the full
  table both ways), and draws the same pixels.
- Building costs a few hundred milliseconds per state on first use (each
  capture runs in a fresh JS runtime, since three.js numbers its nodes per
  process), so it suits development and the long tail. Built states are
  still listed by `t3_renderer_generated_missing` and `T3_MISSING_LOG`:
  feeding that list to `tools/gen-project-table.mjs` moves them into the
  project's table, where they cost nothing.
- QuickJS and three.js's 3.7 MB of JS add about 4.7 MB to the binary (a
  core-table cart grows from 0.9 MB to 5.6 MB of wasm, more than the 1.25 MB
  of a full-table cart). The thread that renders needs a little over 1 MB of stack for a
  build (QuickJS's own bound); `wasm/build.sh` gives a builder cart 4 MB.
- Generator extensions are offline only: an x-state no table has is listed
  as missing, not built.

## Extension programs

An application's own shading is written the way a three.js r186 application
writes it: TSL on the material. A generator extension is a module that
exports `name` and `apply()`; `apply` gets the capture scene (`THREE`,
`TSL`, `mat`, `mesh`, `props`, `sentinel`, ...) and changes the material
before the capture, for example:

    const tint = uniform(new THREE.Color()).setName('tint');
    tint.setGroup(sharedUniformGroup('tintFrame', 0, THREE.NodeUpdateType.RENDER));
    mat.colorNode = materialColor.mul(tint).mul(texture(map, uv()).rgb).mul(scale);

- A uniform the application fills at run time gets a sentinel value and is
  registered as `props['ext.<name>']`, so the fingerprint finds its bytes.
- A texture the application binds is named `ext.<name>` (`texture.name`),
  which becomes the binding's source in the manifest.
- A uniform in the default object group travels with each draw; one in a
  group of its own (`sharedUniformGroup`, with an update type, as r186's own
  `renderGroup`) is uploaded only when its bytes change. r186 names a GLSL
  uniform block after its group, so a group needs a name none of its
  uniforms has.

The state takes `x<name>` (`standard+map+xtint`) and is captured and emitted
like any other. three.c's own table carries only `xtint`
(`tools/extensions/tint.mjs`, which its tests draw). A project keeps its
modules and states in its own tree:

    node tools/gen-project-table.mjs --name mygame --out gen/ --states mygame-states.txt \
        --extend path/to/terrain.mjs

At run time a material picks the extension and a renderer registers the hook
that fills it:

    static void fill_tint(t3_extension_ctx *c, void *user) {
      const struct game *g = user;
      float scale = object_scale(g, t3_extension_object(c));
      t3_extension_set_uniform(c, "tint", g->tint, 3);
      t3_extension_set_uniform(c, "tintScale", &scale, 1);
      t3_extension_set_texture(c, "tintMap", g->tint_map);
    }
    t3_material_set_extension(m, "tint");
    t3_renderer_set_extension(r, "tint", fill_tint, game);

- The material's extension is part of its program key: the renderer picks the
  table program whose state has the same kind, features, lights and `x<name>`.
- The hook runs for each draw of an extension program, as r186's
  `onObjectUpdate` does, on GLES and WebGPU alike; it may run more than once
  for a draw (GLES fills the frame's per-draw blocks before drawing), so it
  only sets values. Uniforms land where the manifest put them; textures bind
  with the program's other textures, and a texture's `flipY` flag follows
  the texture (`gpu_made`). r186 gives each sample of a texture its own
  `flipY` flag on GLES, and every one is filled.
- A DepthTexture the extension samples with compare (r186's
  `texture(depth).compare(ref)`) gets a LessEqual comparison sampler on
  WebGPU, linear unless the texture is nearest filtered; on GLES the GL
  texture's own compare mode applies.
- TSL that reads a second UV set (`uv(1)`) reads the geometry's `T3_ATTR_UV1`
  attribute, at its own fixed location on GLES.
- Limits: an extension program holds up to 64 uniforms of its own, and an
  extension texture may be sampled up to 128 times in it; a renderer holds
  hooks for up to 64 extensions. A program past a limit is refused as
  incomplete: its draws are skipped and the renderer's error says which
  ("too many extension uniforms", "sampled too many times").
- A draw whose extension has no hook, or whose hook left a texture unset, is
  skipped, counted in `info.skipped` and named in the renderer's error.
- Meshes with an extension are not auto-instanced: each object is its own
  draw, so its values are its own. `t3_renderer_set_extension_batching(r,
  name, true)` says the extension's hook reads nothing of the object (only
  per-frame values): its meshes are then auto-instanced like any other (the
  table needs the `+instbig` state too), and the hook runs once a batch,
  with the batch's first mesh. Batching is off by default; it is three.c's
  own (three.js renders each mesh on its own).
- Shadow casters: r186 draws the shadow pass with the material's
  `positionNode` (and its `colorNode`'s alpha), so an extension material
  casts with `depth+x<name>` (`depth+instbig+x<name>` and so on for the
  object's deformations), filled by the same hook. A table without that
  state lists it as missing and the plain caster draws.
  `tools/extensions/swell.mjs` (vertices pushed along their normals) and the
  `e2-extension-shadow` scene check it.

- Array and integer textures, as r186 has them: `t3_data_array_texture_new`
  (DataArrayTexture, sampled with `texture(map, uv).depth(layer)`) and
  `t3_data_texture_new_format` with `T3_RED_INTEGER_FORMAT`,
  `T3_RG_INTEGER_FORMAT` or `T3_RGBA_INTEGER_FORMAT` (read with
  `textureLoad`, texelFetch on GLES). r186's TSL types an integer texture as
  one only for a 32-bit type (`UnsignedIntType` / `IntType`: R32UI, RG32UI,
  RGBA32UI and the signed ones); with a smaller type it declares a float
  texture, which the integer data cannot bind to. Pack 16-bit values into a
  32-bit texture instead. `tools/extensions/layers.mjs` and the
  `e3-extension-textures` scene check both.
- A program may have up to 16 texture bindings over all its groups (WebGPU's
  default per-stage limits for textures and samplers, and GLES 3's units).
  r186 binds a texture that both stages read twice. A program over the
  limit (or the device's, if lower) does not build: its draws are skipped
  and the renderer's error names it. `tools/extensions/mosaic.mjs` and the
  `e4-extension-many-textures` scene draw thirteen.
- A texture's sampling settings are part of the program (r186 reads a
  nearest-filtered texture with `textureLoad` and bakes wrap modes into the
  code), so an extension's textures at run time need the filters and
  wrapping its module captured them with.

`test/test_extension.c` (both backends) and the `e1-extension`,
`e2-extension-shadow`, `e3-extension-textures` and
`e4-extension-many-textures` scenes (under `tools/parity-r186.mjs`, against r186
running the same TSL) check it.

## renderer.info

`t3_renderer_info` counts what r186's `renderer.info.render` counts, including
r186's output pass: one draw and one triangle whenever a render to the screen
has an sRGB output or tone mapping, also when three.c encoded in each program
and drew no output pass itself.

## Drawing with the tables

For each draw the renderer works out the state from the material, the
object, the scene and the light counts, and looks it up. If there is no exact
light vector, a captured program whose counts cover the scene's is used and
the unused light slots are left empty.

Draws are sorted as r186 sorts them: render order, then opaque front to back
and transparent back to front by the projected depth of the object's origin.
`t3_renderer_set_sort_bounds(r, true)` (three.c's own, off by default) sorts
opaque meshes and instanced meshes by the centre of their cached world
bounding sphere instead, which is what geometry built far from its object's
origin (merged world-space chunks, instanced scenery) needs for front to
back order to hold. Render order, transparent objects and skinned meshes keep
r186's order. `test/test_scene_sort.c` checks it.

`src/gen_program.c` reads the manifest into a list of fill operations, one
per property, and splits them into per-frame operations (camera, lights, fog)
and per-draw ones (matrices, material values). Per-draw uniform blocks for
the whole frame are packed into one buffer:

- on GLES, uploaded once and bound with `glBindBufferRange`;
- on WebGPU, streamed and bound with dynamic offsets.

On GLES the GLSL is compiled with fixed attribute locations. In the default
fast path the program also gets an epilogue that encodes sRGB, so it can
draw straight to the screen. `t3_renderer_set_exact_output` turns that off.

On GLES an InstancedMesh's instance matrices and colours are uploaded when
their version changes or more instances are drawn than were sent: the drawn
instances (the first `count`) go into new storage for the buffer
(`glBufferData`), not into the old storage in place, which could wait for
the GPU to finish the previous frame's reads. Drawing fewer instances, with
nothing changed, uploads nothing. `test/test_instance_stream.c` checks it.

## Embedder passes

For an embedder that composes three.c's drawing with its own:

- `t3_renderer_render_opaque(r, scene, camera)` renders the opaque queue
  only, alpha-tested cutouts included; transparent and transmissive
  materials are left out. Shadows and the rest of the scene setup run as in
  `t3_renderer_render`, and no material is changed, so the next ordinary
  render draws the transparents again.
- `t3_renderer_render_prepass(r, scene, camera)` (GLES; does nothing on
  WebGPU) is a depth prepass into the embedder's scene framebuffer: the
  opaque queue with colour writes off whatever the materials say, alpha
  discard and vertex displacement kept, and no background or output pass.
  The usual clearing and render target rules apply, and colour writes are
  back on when it returns. `test/test_prepass_mask.c` checks that no
  material turns colour writes back on during it.
- `t3_renderer_render_depth` (GLES) and `t3_renderer_render_depth_to` (both
  backends, into a rect of a depth-only target) draw the shadow caster pass
  from any camera, for the embedder's own shadow maps.

Several pieces follow r186's own code:

- **Output pass:** r186's generated output program (tone mapping and colour
  space).
- **Environment maps:** r186's PMREM generator (`src/renderer_env.inc`). It
  converts a cube or equirectangular map to cube-UV, blurs scene captures,
  and builds the GGX mip chain, all with r186's own generated programs.
  fromScene takes r186's options (`t3_pmrem_from_scene_ex`: the face size,
  the position the cube camera renders from, and a target to draw into
  again), and `t3_pmrem_from_cubemap_into` / `_equirectangular_into` are
  fromCubemap / fromEquirectangular's target argument, so a reflection probe
  refreshed every few frames keeps one target. The `r11-pmrem-probe` scene
  checks it.
- **Shadows:** depth maps drawn with the captured caster programs.
  Point lights use a cube map with r186's face directions for each backend.
  A renderer keeps depth maps for up to 16 shadow-casting lights at a time.
  The map of a light that no longer casts in the render (removed or
  destroyed) is reused by the next new caster, so a scene that adds and
  removes lights does not run out of maps. A map belongs to a light by
  address and id together, so a new light that a reused allocation puts at
  a destroyed light's address never gets that light's stale map.
  `test/test_shadow_cache.c` checks both.

## WebGPU

`t3_renderer_use_wgpu(r, device, queue, format)` takes any `webgpu.h`
device: Dawn natively (native-dawn), emdawnwebgpu in a wasm cart.
`src/gen_wgpu.c` builds the following from the manifests:

- shader modules;
- bind group layouts;
- pipelines, cached per render state;
- bind groups, cached per texture set.

What else it does:

- **Uniform stream:** one buffer per frame, written with a single queue
  write.
- **Submits:** by default each render submits, as r186 does. With
  `t3_renderer_wgpu_defer` the frame's renders share one command buffer, and
  `t3_renderer_wgpu_submit` sends it.
- **Embedder hooks:** an embedder can pass its own command encoder
  (`t3_renderer_wgpu_set_encoder`) and swapchain view
  (`t3_renderer_wgpu_set_output`). An external target
  (`t3_renderer_set_external_target`) may set a viewport depth range
  (`depth_min`, `depth_max`; both 0 means 0..1): a pass squeezed into the
  near end (0 to 0.02, say) draws over a depth buffer the embedder keeps,
  as a first person weapon drawn after the world does. GLES ignores the
  fields (`glDepthRangef` does the same there).
- **Projection:** three.js builds WebGPU's projection z row from near and far.
  three.c does that for the usual projection, and a projection the
  application edited (an oblique near plane, as r186's Reflector and
  Refractor set to clip at the mirror plane) keeps its edit, its z row
  remapped to WebGPU's clip space (z' = 0.5 z + 0.5 w).

**Compatibility mode** (no `core-features-and-limits`, which is what
wasmcart gives carts) needs a few things, and the renderer handles them:

- Cube textures declare their cube binding dimension when created.
- Cube mipmaps are built on the CPU, because a GPU pass cannot sample one
  face.
- A DepthTexture used as a map takes r186's compatibility program (`cm`),
  which reads it as `texture_2d<f32>`.
- A DataArrayTexture is created to bind as a 2D array, and its mipmaps are
  drawn from the level above bound that way, the layer picked per draw
  (r186's `main_2d_array` mipmap pass).

## Checking against three.js

`tools/parity-r186.mjs` renders every scene with three.c (`bench-native`,
GLES, or WebGPU with `--wgpu`) and with unmodified three.js r186 on the same
backend (`tools/ref-r186.mjs`, webgl-node or webgpu-node), then compares the
two images. `tools/ref-r186-browser.mjs` is the same reference in Chromium.
Measured three.js defects that three.c does not copy are listed in the tool
and reported apart from failures.
