# Changelog

What each release added, changed and fixed. Releases are git tags named
`v0.<three.js revision>.<three.c release>`; `T3_VERSION` in `include/three.h`
says which one a checkout is. Releases before 0.186.19 are described by their
tag's commit message.

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
