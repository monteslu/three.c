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
together (about 0.2 MB per backend for the full table's 321 states). The
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
  adds TSL nodes and uniforms of its own before the capture.

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
- Generator extensions (`tools/extensions/`) are offline only.

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

`src/gen_program.c` reads the manifest into a list of fill operations, one
per property, and splits them into per-frame operations (camera, lights, fog)
and per-draw ones (matrices, material values). Per-draw uniform blocks for
the whole frame are packed into one buffer:

- on GLES, uploaded once and bound with `glBindBufferRange`;
- on WebGPU, streamed and bound with dynamic offsets.

On GLES the GLSL is compiled with fixed attribute locations. In the default
fast path the program also gets an epilogue that encodes sRGB, so it can
draw straight to the screen. `t3_renderer_set_exact_output` turns that off.

Several pieces follow r186's own code:

- **Output pass:** r186's generated output program (tone mapping and colour
  space).
- **Environment maps:** r186's PMREM generator (`src/renderer_env.inc`). It
  converts a cube or equirectangular map to cube-UV, blurs scene captures,
  and builds the GGX mip chain, all with r186's own generated programs.
- **Shadows:** depth maps drawn with the captured caster programs.
  Point lights use a cube map with r186's face directions for each backend.

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
  (`t3_renderer_wgpu_set_output`).

**Compatibility mode** (no `core-features-and-limits`, which is what
wasmcart gives carts) needs a few things, and the renderer handles them:

- Cube textures declare their cube binding dimension when created.
- Cube mipmaps are built on the CPU, because a GPU pass cannot sample one
  face.
- A DepthTexture used as a map takes r186's compatibility program (`cm`),
  which reads it as `texture_2d<f32>`.

## Checking against three.js

`tools/parity-r186.mjs` renders every scene with three.c (`bench-native`,
GLES, or WebGPU with `--wgpu`) and with unmodified three.js r186 on the same
backend (`tools/ref-r186.mjs`, webgl-node or webgpu-node), then compares the
two images. `tools/ref-r186-browser.mjs` is the same reference in Chromium.
Measured three.js defects that three.c does not copy are listed in the tool
and reported apart from failures.
