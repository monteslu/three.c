# Changelog

What each release added, changed and fixed. Releases are git tags named
`v0.<three.js revision>.<three.c release>`; `T3_VERSION` in `include/three.h`
says which one a checkout is. Releases before 0.186.19 are described by their
tag's commit message.

## v0.187.16 (2026-11-19)

### Added
- **ArrayCamera** (`t3_array_camera_new`, `t3_array_camera_add`): several cameras, each with a viewport, drawn
  by one `t3_renderer_render` call (split screen, a map beside the main view). The views share the scene's matrix
  update, the light list, the shadow maps, the target's bind and clear, the instance and per-draw uniform uploads,
  the WebGPU render pass and the GLES output pass; each view has its own lists, culled against its own frustum
  and sorted by its own depth. It draws with the ordinary programs (r187 has array-indexed camera programs for
  this; three.c refills the per-frame values per view), so no table changes. The parity scene `c1-array-camera`
  (four views: wide, close, from above, a narrow one; shadows, a transparent sphere, auto-instanced spheres)
  matches r187's ArrayCamera on WebGPU and GLES. test/test_array_camera.c: two and four views equal the same views
  rendered one by one, a view that looks away adds no draws, and its control (one camera moved) fails.
  Measured on a Radeon 890M (GLES, 512 x 320, 400 meshes with a material each, a shadowed sun, two interleaved rounds of 200 frames, CPU and GPU together): two views 0.81 / 0.75 ms as two renders against 0.69 / 0.67 ms as one ArrayCamera render; four views 1.06 / 1.01 ms against 0.89 / 0.84 ms (the renders one by one draw the shadow map once each). Transmission's framebuffer copy and back-face pass are not made under an ArrayCamera.
  `T3_ARRAY_CAMERA` is appended to the object types.

### Fixed
- tools/emit-programs.mjs writes the `buffer` member of `t3_gen_property` that 0.187.14 added to src/gen/programs.h
  by hand: regenerating three.c's own tables dropped it and the library no longer compiled.
- **A second renderer in one process drew its lit materials white.** What each texture unit holds is tracked per
  process; a renderer that was destroyed had deleted textures (its shadow maps, the DFG table) whose names the driver
  hands out again, and a unit believed to hold such a name was never bound again for the next renderer.
  `t3_renderer_new` and `t3_renderer_destroy` now forget the units. Found by test/test_array_camera.c, which makes
  several renderers and checks that the picture is not saturated.

## v0.187.15 (2026-11-19)

### Fixed
- `T3_VERSION` says 0.187.15: the v0.187.14 tag's checkout still said 0.187.13, and this file had no section for it
  (the one below). Nothing else changed.

## v0.187.14 (2026-11-19)

### Added
- **Extensions can read an r187 `uniformArray`.** Its buffer binding (one of its own in the object group) is filled
  by the extension hook, element by element, with `t3_extension_set_uniform` under the names the extension registered
  (`ext.arr0` .. `ext.arrN`), and uploaded for each draw that changed it, on GLES and WebGPU. `t3_gen_property`
  gains `buffer` as its last field (0: the group's uniform struct, k + 1: the group's buffer k); tables made before
  stay valid as they are. A buffer no property fills is still refused, by the generator and at build, naming the
  binding. test/test_uniform_array.c draws a plane in the colour two elements make, on GLES and WebGPU; its control
  leaves an element at zero and must fail; test/extensions/arrbare.mjs (elements no property names) is refused.

## v0.187.13 (2026-11-19)

### Fixed
- **A program with a buffer binding three.c does not fill is refused, loudly.** An extension reading an r187
  `uniformArray` gets a buffer binding of its own in the object group (`NodeBuffer_object_1` on GLES), which three.c
  never filled; its values were written over the start of the object block instead, and the mesh silently did not
  draw in the colour pass. The generator (tools/program-model.mjs) now stops with the state, the binding and its
  group, and a program from an older table is incomplete at build with the same words in
  `t3_renderer_last_error` (its draws are skipped and counted). Bone matrices and morph influences, the buffers
  three.c fills, are unchanged. test/run.sh `test_uniform_array_refused` (test/extensions/arr.mjs); the generator
  without the check is the control and fails it.

## v0.187.12 (2026-11-19)

### Added
- **BatchedMesh** (`t3_batched_mesh_new`, `_add_geometry`, `_add_instance`, `_set_matrix_at`,
  `_set_visible_at`, `_delete_instance`, `per_object_frustum_culled`, `sort_objects`): many different
  geometries with one material, each instance with its own matrix and visibility, drawn with r187's BatchedMesh
  programs (matrices texture read through the indirect texture) and casting and receiving shadows
  (`depth+batch`). **It feeds the instance id differently from r187, on purpose.** r187 gives the program the
  id once per draw (a uniform on WebGL2, the instance index on WebGPU), so it issues one draw call an instance.
  three.c writes the id into a vertex attribute (`T3_ATTR_BATCH_ID`; each instance owns a copy of its
  geometry's vertices), so a run of visible instances that follow each other in the buffer is one draw on GLES
  and on WebGPU, and culling an instance uploads nothing. The picture is r187's: the parity scene `b1-batched`
  matches r187's BatchedMesh on WebGPU and GLES. Each instance is frustum culled by its bounding sphere, against
  the light's frustum in a shadow pass. test/test_batched.c: a batch equals the same 60 meshes drawn one by one,
  in 1 draw (3 with one hidden and one deleted), and a camera that sees part of the grid draws only those
  instances; its control (another instance hidden) fails. `bridge_indices` (three.c's own, 0 by default) lets two runs with only
  out-of-view instances between them draw as one, the gap clipped; `version` moves with every change to the
  instances, for an embedder's shadow cache. Not there yet: setColorAt, setGeometryAt, optimize(),
  raycasting; opaque instances are not depth sorted.
- State feature `batch` (it takes the place of the feature name `clip`, which no state ever carried: no table
  changes), texture sources `batch.indirect` / `batch.matrices`, 18 states in the base tables (396 in all).
  `T3_ATTR_BATCH_ID` is appended to the attribute slots, so `T3_ATTR_COUNT` grows by one (code that loops to
  it needs no change), and `T3_BATCHED_MESH` to the object types.

## v0.187.11 (2026-11-19)

### Added
- **IESSpotLight and IESLoader.** `t3_ies_spot_light_new` and `t3_light_set_ies_map` (`light->ies_map`): a spot
  light whose shape is a photometric profile, drawn with the programs r187 generates for its IESSpotLightNode (the
  profile replaces the cone: its x is the tilt off the light's axis, its y the twist around it), with or without a
  shadow. `t3_ies_texture_parse` is IESLoader.parse: the text of an IES file (LM-63) as that texture, 180 texels of
  tilt by one row (a single horizontal angle) or 360 (mirrored as the file's range asks; a range ending between
  180 and 360 gets its missing 360 entry), half floats normalized to the brightest direction. `T3_IES` says it
  exists. test/test_ies.mjs compares three profiles with three.js's IESLoader texel for texel (its control, another
  profile, differs); the parity scene `l1-ies` (a symmetric profile casting a shadow, a 0 to 180 degree one, a plain
  spot) matches r187 on WebGPU and GLES.
- **State token `ies<hex>`**: the spot lights of the vector (by id) that are IESSpotLights with an iesMap, as a bit
  mask (`standard+l0030+ies3`); `t3_gen_program` gains `ies` as its last field. States without the token are
  unchanged; a project's table needs regenerating against this version's programs.h, as for any release that changes
  it. Up to eight spot lights can be IES ones.

### Fixed
- A capture with spot lights of two classes (a SpotLight after an IESSpotLight) counted shadow casters per class,
  not per light type (tools/capture-core.mjs); no state of the base tables had both.

## v0.187.10 (2026-11-19)

### Changed
- **Extension names and hooks grow on demand.** A material's extension name may be up to 63 characters (was 31), a
  process may name up to 4096 extensions (was 64), and a renderer holds a hook and a batching flag for every one of
  them (was 64 hooks; the hook of a draw is now found by the extension's id, not by a search). An application that
  joins several extensions into one name per combination ran out of all three. Signatures are unchanged.
  `test/test_extension_names.c` names 300 extensions, one of 63 characters, with hooks on one renderer; its control
  asks for the old limits to hold and fails.

## v0.187.9 (2026-11-19)

### Added
- **MeshToonMaterial** (`t3_mesh_toon_material_new`, `T3_MESH_TOON_MATERIAL`): r187's ToonLightingModel, drawn with
  the programs r187 generates for it. Colour and map, `gradient_map` (`t3_material_set_texture( T3_GRADIENT_MAP )`:
  the light ramp, one tone a texel; without one r187's two tones), emissive with its map and intensity, normal map,
  ambient occlusion, light map, alpha map, vertex and instance colours, fog, skinning, every light type and shadows.
  As three.js asks, a gradientMap is NearestFilter for both filters and clamped: the program reads it that way, and
  one that is not is refused with the reason in the renderer's error. New state kind `toon` and feature `gmap`
  (36 states in the base tables, 376 in all). The parity scene `m1-toon` matches r187 on WebGPU and GLES;
  test/test_toon.c counts the tones a sphere shows (2, 3 and 5 for no map, three and five texels), and its control
  (the same spheres as Lambert) fails.

## v0.187.8 (2026-11-18)

### Fixed
- **Distant surfaces no longer break up into holes on Metal when a depth prepass runs.** The prepass and the
  colour pass draw the same vertices through two programs (WebGPU: two pipelines), and the colour pass keeps a pixel
  only where its depth is not behind the prepass's. Nothing told the compiler to compute the clip position the same
  in both, and Metal (Dawn, so Chrome on macOS and native WebGPU there) did not: about half the pixels of distant,
  nearly flat-on ground failed the depth test and the background showed through in blotches (a racing game's far
  mountains looked bright and patchy). Every WebGPU vertex program now declares its position `@invariant` and every
  GL vertex program `invariant gl_Position` (GL has the prepass too, and WebGL on a Mac runs on Metal through
  ANGLE). `t3_gen_wgsl_invariant_position`, `t3_gen_glsl_invariant_position` (gen_program.h); a program without
  the one declaration they expect is refused, naming it. test/test_invariant_position.c checks all 340 WebGPU and
  340 GL vertex programs of the base tables; its control (the programs without the rewrite) fails. Proved on a
  macOS runner (Apple GPU): the game's cart in Chrome draws the mountains whole with this, with holes without it.

## v0.187.7 (2026-11-09)

### Added
- `t3_renderer_set_missing_hook`: a callback once for each program state the tables lack, when a draw first
  asks for it, with that draw's object and material (valid during the call), so an embedder can say which of its
  objects is not drawn instead of only listing states. The draw is still skipped and the state still listed by
  `t3_renderer_generated_missing`. `T3_MISSING_HOOK` says it exists. test/test_missing_hook.c: the core
  table alone names the sphere and its material once over two frames; the full table never calls it.

## v0.187.6 (2026-11-09)

### Added
- `t3_pmrem_scene_options.min_size`: a scene capture smaller than r187's 256 cube (16 at least).
  r187 raises every `fromScene` to 256, so a probe that asked for 64 or 128 paid for 256.
- `t3_pmrem_scene_job_*`: `fromScene` over several calls, a face of the scene a step, then the
  blur, then the filtered levels, for a capture that would otherwise be one frame's spike. A job keeps
  a source cube of its own, so other PMREMs made between its steps leave it alone.
  `T3_PMREM_SCENE_JOB` says both exist.
- `t3_renderer_set_shadow_hold`: renders made while held draw no shadow map that has been drawn before;
  they use it as it stands, and a map due to be drawn again is still drawn by the first render after the
  hold. For extra views of a scene within a frame (a reflection probe's faces).

### Changed
- PMREM (every `fromScene`, `fromCubemap`, `fromEquirectangular` and a scene's environment):
  the filtered levels' six faces are drawn side by side into one 2D target and copied into the
  cube's faces, where r187 draws each face of each level into the cube on its own (30 passes, each
  waiting on the one before it for a new attachment, and on WebGPU a submit each). The same programs
  and values draw every texel, so the levels are unchanged (GL frames byte-identical). Level 0
  (roughness 0) is the source's own faces, copied: r187's GGX pass reads the source once a texel
  there. On an AMD 890M a 256 scene capture went from about 3.7 to 2.4 ms of GPU time on GL and
  from 5.2 to 3.0 ms on WebGPU.

## v0.187.5 (2026-11-07)

### Added
- WebGPU: `t3_renderer_set_encoder_hook` tells an embedder of each command encoder the
  renderer opens on its own (the frame encoder when no external one is set, a mip chain's,
  a cube's mip chain) right after it opens and right before it finishes, so the embedder can
  write timestamps into those command buffers and time work such as a PMREM capture without
  the gaps between submits. `T3_ENCODER_HOOK` says it exists. GLES: accepted and ignored.

## v0.187.4 (2026-11-01)

### Fixed
- WebGPU: dynamic vertex/index and auto-instance data remain intact when an external
  encoder is detached before submission, including partial attribute updates and
  uniform-stream growth. Track buffers actually bound by outstanding commands and
  reuse their storage after submission; attaching an encoder alone does not force
  allocation. Dynamic streams discard obsolete capacities and release their pool
  on destruction. The constant output triangle is uploaded once.

## v0.187.3 (2026-10-30)

### Fixed
- macOS: a program using three.c failed to link, on an undefined `eglGetProcAddress` from
  `t3_renderer_set_reversed_depth`, since v0.187.0. The weak reference that Linux and Android resolve to
  nothing is an error on macOS. A reversed depth buffer on GLES there now notes and stays off, as on Windows;
  WebGPU has it on every platform.

## v0.187.2 (2026-10-30)

### Added
- `t3_renderer_set_forward_shadows`: the renderer's shadow maps stay on forward depth under a
  reversed depth buffer, so a project's program tables need no `+rdepth` states. three.c's own:
  three.js reverses them too, which stays the default.

## v0.187.1 (2026-10-30)

### Fixed
- Reversed depth at a large far / near: the projection's z row was taken as `w - z` from
  the rounded floats, where `-1 + f / (f - n)` cancels and most of the term is rounding
  error. It now comes from near and far in double, as three.js builds it.

## v0.187.0 (2026-10-30)

three.c on three.js r187: every program recaptured from 0.187.0, PMREMs as cube
render targets, a reversed depth buffer, depth-only render targets, rough diffuse
and a second UV set.

### Added
- `t3_renderer_set_reversed_depth`: reversedDepthBuffer. Depth runs from 1 at the
  near plane to 0 at the far plane in a 32-bit float buffer, so precision is spread
  evenly out to the horizon: no z-fighting far away, and a much farther far plane.
  WebGPU always; GLES where the context has EXT_clip_control, otherwise it notes
  that and stays off, as three.js does. A cart on GL has no such call, so it runs
  with forward depth. Shadow programs take a `+rdepth` state.
- `t3_render_target_options.depth_only` (three.js `count: 0`): a render target with
  no colour attachment, only its depth texture.
- `t3_material.diffuse_roughness`: MeshPhysicalMaterial.diffuseRoughness, EON rough
  diffuse. Rough surfaces look soft and flat at grazing angles instead of plastic.
- `t3_texture.channel`: the second UV set on map, normalMap, aoMap, emissiveMap,
  roughnessMap, metalnessMap, alphaMap and lightMap (states `+c1map` and so on).
  The glTF loader honours `texCoord` and KHR_texture_transform's.
- `t3_cube_render_target_new` and `t3_renderer_set_render_target_ex`: CubeRenderTarget
  and setRenderTarget(rt, face, mip). A cube target with generate_mipmaps builds its
  chain after a render into face 5.
- `t3_texture.mip_levels`: a PMREM's prefiltered levels (isPMREMTexture).
- `t3_external_target.keep_multisampled` and the WebGPU depth prepass, from main since
  0.186.22: `t3_renderer_render_prepass` draws on WebGPU too (`T3_WGPU_DEPTH_PREPASS`).

### Changed
- PMREMs are cube render targets with a mip chain, as in r187, instead of the cube-UV
  atlas. Materials look slightly different, as they do in r187. Face sizes are at least
  256 (a smaller `size` is raised) and no longer capped at 1024.
- WebGPU point shadows use r187's face directions, and point shadow programs read the
  shadow camera's near and far.
- Project program tables must be regenerated: every program changed, and the
  `pmremggx+z<n>` and `pmremblur+z<n>` states are now `pmremggx`, `pmremintegration` and
  `pmremblur`.
- WebGPU: the pass-state cache no longer adds and releases references on every change,
  two host calls fewer per changed binding in a cart.

### Removed
- `T3_CUBE_UV_REFLECTION_MAPPING` and `T3_CUBE_UV_REFRACTION_MAPPING`, as in r187. Test
  `texture->mip_levels > 0` to tell a PMREM.

### Fixed
- Everything r187 fixed in its programs arrives with the recapture, among them specular
  intensity in direct lighting, the ACES Filmic fit, point light shadow seams and the
  PCF radius on non-square shadow maps.
- A PMREM made before the renderer's first render drew with no camera and came out black.

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
