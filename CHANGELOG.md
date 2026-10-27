# Changelog

What each release added, changed and fixed. Releases are git tags named
`v0.<three.js revision>.<three.c release>`; `T3_VERSION` in `include/three.h`
says which one a checkout is. Releases before 0.186.19 are described by their
tag's commit message.

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
  sample alpha-to-coverage for the material's draws into a multisampled
  target. The tables hold no r186 alpha-to-coverage program yet (r186 keeps
  such a material's alpha instead of writing 1, and smooths its alpha test),
  so it changes the picture only where the program writes an alpha below 1: a
  transparent material, or an extension that sets the alpha itself. WebGPU
  draws these materials as before.

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
