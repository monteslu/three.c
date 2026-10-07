// An example generator extension (tools/gen-programs.mjs --states standard+map+xtint):
// the plug-in point for an embedder's TSL nodes. apply() runs on the capture
// scene before the render; whatever it adds (nodes on the material, uniforms,
// textures) lands in the generated program and its manifest, and any uniform
// it fills with a sentinel is fingerprinted into a property slot under the
// name it registers in props.
//
// This one multiplies the material colour by a renderer-wide tint uniform in a
// group of its own, the shape an engine's own per-frame lighting data takes.
export const name = 'tint';
export async function apply({ TSL, THREE, mat, props, sentinel }) {
  const { uniform, materialColor } = TSL;
  const r = sentinel(), g = sentinel(), b = sentinel();
  const tint = uniform(new THREE.Color().setRGB(r, g, b, THREE.LinearSRGBColorSpace)).setName('tint');
  // its own bind group, updated once per frame
  tint.setGroup(TSL.sharedUniformGroup('tint'));
  mat.colorNode = materialColor.mul(tint);
  props['ext.tint'] = { kind: 'vec3', value: [r, g, b] };
}
