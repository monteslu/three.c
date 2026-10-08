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
`tools/gen-states.txt` adds the states a project's scenes need.

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
  Node, three.c's `npm install` and a GPU (webgl-node, webgpu-node); the
  generated C does not, so it is usually checked in.

The tool writes `gen/mygame_gl.c` and `gen/mygame_wgpu.c`. Compile them with
three.c's `src/` on the include path and register them before the first
render:

    extern const struct t3_gen_table mygame_gl, mygame_wgpu;
    t3_register_program_table(&mygame_gl, &mygame_wgpu);

Registered tables are searched before three.c's own. Built with
`T3_TABLE=core`, three.c keeps only the programs the renderer draws with
itself (output pass, backgrounds, PMREM, shadow casters: about 0.35 MB per
backend instead of the full table's tens of MB), so the project's table is
the only source of material programs.

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
