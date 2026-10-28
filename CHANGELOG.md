# Changelog

What each release added, changed and fixed. Releases are git tags named
`v0.<three.js revision>.<three.c release>`; `T3_VERSION` in `include/three.h`
says which one a checkout is. Releases before 0.186.19 are described by their
tag's commit message.

## v0.186.22 (2026-10-28)

A scene structure counter for embedders' caches, cheaper per-draw extension
lookups and texture re-uploads, and shadow maps drawn before a renderer's
first render on GLES.

### Added
- `t3_graph_version()`: a counter that moves on every add, remove and
  reparent (in any scene) and when an object's layers or raycast override
  change through their setters, not on moves, visibility or field writes. An
  embedder keeps a cache of a scene's lights or meshes valid while the value
  is unchanged instead of walking the scene each frame. three.c's own:
  three.js has no such counter.

### Changed
- GLES: a texture's new version of the same size and format is uploaded into
  its existing storage (`texSubImage2D`, as r186's WebGLTextures re-uploads
  into `texStorage2D`) instead of specifying the storage again
  (`texImage2D`); a data texture rewritten every view (clustered light lists,
  froxel lists) no longer reallocates its storage each time.
- `t3_extension_set_uniform` and `t3_extension_set_texture` remember the
  entry a name found, keyed by the name's address and checked against the
  name before use, instead of comparing the name with every uniform of the
  program on each draw (a stack of shading parts filling 15 names a draw over
  up to 64 entries costs one lookup a name).

### Fixed
- GLES `t3_renderer_render_depth` bumps the render stamp, as the WebGPU path
  does. Before a renderer's first render the stamp and every program's were
  0, so shadow casters drawn first (an embedder's cascaded or atlas shadows)
  got no camera uniforms and the shadow map came out empty on the first
  frame.

### Tests
- `test/test_graph_version.c` (no GL): the counter moves on adds, removes,
  reparents and the layers setter and not on moves; the control
  (`T3_TEST_CONTROL=1`, which expects no change across an add) must fail.

## v0.186.21 (2026-10-28)

WebGPU vertex buffers: the layout three.c binds is a tool API, and a program
over the device's limit is refused by name. Destroying a WebGPU renderer makes
no GL call.

### Added
- `tools/program-model.mjs` exports `vertexBufferLayout(attributes, { batched })`
  and `WEBGPU_MAX_VERTEX_BUFFERS` (8): the WebGPU vertex buffers three.c
  binds for a captured program (one per geometry attribute, the instance
  matrix's four columns in one instance-step buffer as r186's
  InstancedInterleavedBuffer, the instance colour in its own). A tool that
  compiles a captured program the way a host draws it uses this layout: one
  buffer per attribute counts the matrix four times, 10 buffers for an
  InstancedMesh with vertex colours, tangents and instance colours that
  three.c draws with 7, and Dawn refuses that pipeline ("Vertex buffer count
  exceeds the maximum number of vertex buffers (8)").
- `programModel` refuses a WebGPU program that needs more than 8 vertex
  buffers, naming its attributes, so a table never carries one.

### Fixed
- `t3_renderer_destroy` on a WebGPU renderer makes no GL call. It deleted
  GL objects it never made (`glDeleteTextures(0)` for each generated shadow
  map among them), and an embedder that imports both APIs (a dual GL /
  WebGPU wasm cart running on WebGPU) trapped when a scene was freed.
  `test/test_wgpu_destroy.c` counts GL deletes around a WebGPU renderer's
  destroy (2 before the fix, 0 now); the control, a GLES renderer, must see
  them.
- WebGPU: a program needing more vertex buffers than the device's
  `maxVertexBuffers` does not build, and the renderer's error names it,
  where its pipeline was a validation error and the draw was lost.

### Tests
- `test/test_vertex_buffers.c`: an InstancedMesh with every optional vertex
  attribute (uv1, vertex colours, tangents, instance colours) and map,
  normal map and ambient occlusion map on a physical material, on GLES and on
  a WebGPU compatibility-mode device: nothing skipped, no validation error,
  each instance its own colour, GL vs WebGPU within 8 per channel (4 on the
  same GPU). The control (white instances) must fail.
- `test/test_vertex_buffer_layout.mjs`: that program's layout is 7 buffers
  (the matrix in one), `programModel` refuses 9, and on Dawn (lavapipe) the
  pipeline builds while the control, one buffer per attribute (10), is
  refused for its vertex buffer count.

## v0.186.20 (2026-10-27)

Material names and KHR_materials factors from glTF, morph targets by name,
blend modes and texture sampling the same on WebGPU as on GLES.

### Added
- The glTF loader keeps each material's name (`t3_material.name`) and reads
  the factors of KHR_materials_sheen, clearcoat, specular, ior,
  transmission, volume, emissive_strength, iridescence and anisotropy into a
  MeshPhysicalMaterial, as r186's GLTFLoader. `test/test_gltf_materials.c`,
  with a control that renames the extensions.
- Morph targets by name: `t3_mesh.morph_target_names` (r186's
  `morphTargetDictionary`, from the glTF mesh's `extras.targetNames`) and
  `t3_mesh_get_morph_target_index`.

### Fixed
- WebGPU draws every blend mode: Subtractive, Multiply and CustomBlending
  (its factors and equations) drew as Normal before, and premultiplied alpha
  was ignored. Min and Max take factors One there, as WebGPU requires.
  GLES's premultiplied Subtractive and Multiply and straight Additive now set
  r186's functions (premultiplied Subtractive turned the colour black).
  `test/test_blend_modes.c` runs every mode on both backends; Normal is the
  control.
- WebGPU samplers take `texture.anisotropy` (it was always 1, so an
  anisotropic texture was blurrier at grazing angles than on GLES), and on
  GLES a texture's wrap, filters and anisotropy changed after its upload apply
  at the next bind, as the WebGPU sampler made at every draw does.
  `test/test_texture_parity.c` compares the two on a grazing mipmapped floor,
  with controls.
- A WebGPU `flip_y` draw (an embedder's bottom-up target) negates the
  fragment program's screen y derivatives: a normal map on geometry without
  tangents was lit from the wrong side against GLES. `test_wgpu_external`
  section I, with a control.
- A mesh with one morph target draws on a WebGPU compatibility-mode device
  (its morph texture now names its 2D-array binding when it is created; it
  was a validation error and nothing drew).


## v0.186.19 (2026-10-27)

A WebGPU viewport depth range for embedder passes, rgba vertex colours, and
the full program table's GL programs brought up to date with the capture.

### Added
- `t3_external_target.depth_min` / `depth_max`: the viewport depth range of a
  WebGPU pass into an external target (both 0 means 0..1). An embedder
  squeezes a pass into the near end of the range (0 to 0.02, say) to draw over
  a depth buffer it keeps, as a first person weapon drawn after the world
  does. GLES ignores the fields (`glDepthRangef` does the same there).
  `test/test_wgpu_external.c` checks the squeezed depths, with a control that
  keeps the full range.
- An rgba colour attribute (`COLOR_0` as glTF exporters write it by default)
  draws with vertex colours instead of the draw being skipped. The program
  reads its first three components; the alpha is ignored, as r186's
  `USE_COLOR_ALPHA` programs are not in the tables. `test/test_vertex_color_rgba.c`
  checks that it draws the same picture as rgb, with a control that a two
  component attribute is still refused.
- `material.alpha_to_coverage` (r186's `alphaToCoverage`) turns on GL's
  sample alpha-to-coverage for the material's draws. It does not soften an
  opaque alpha-tested material's edges yet: r186 gives such a material its
  own program (the alpha kept instead of written as 1, the alpha test
  smoothed), and the tables do not have those programs, so the coverage stays
  full and the picture is the plain alpha test's. WebGPU draws these
  materials as before.

### Changed
- An extension texture may be sampled up to 128 times in one program (was 64).

### Fixed
- The full and core program tables' GL programs of 87 states (shadow maps,
  environment maps, transmission, PMREM and the DFG lookup among them) fill
  every flipY flag of a texture sampled at several places. The capture has
  recorded those flags since 0.186.12, but the checked-in tables were not
  regenerated, so the flags stayed constant 0. Shader text is unchanged.
  `test_builder_parity` (the runtime builder against the tables, field for
  field) passes again.
- `GL_SAMPLE_ALPHA_TO_COVERAGE` is defined where a GL header lacks it (wasm
  carts).
