// An example generator extension (tools/gen-programs.mjs --states standard+map+xtint):
// the plug-in point for an embedder's TSL nodes. apply() runs on the capture
// scene before the render; whatever it adds (nodes on the material, uniforms,
// textures) lands in the generated program and its manifest, and any uniform
// it fills with a sentinel is fingerprinted into a property slot under the
// name it registers in props.
//
// At run time a material picks the program with t3_material_set_extension(m,
// "tint"), and the hook registered with t3_renderer_set_extension fills what
// the extension declared, by the names after "ext.":
//   tint       vec3, in a group of its own (sharedUniformGroup): the shape an
//              embedder's per-frame data takes, the same for every object;
//   tintScale  float, in r186's default object group: a value per object;
//   tintMap    a texture (its name "ext.tintMap" is the binding's source),
//              bound with the object's other textures.
// The colour is material colour x tint x tintMap x tintScale.
export const name = 'tint';
export async function apply({ TSL, THREE, mat, props, sentinel }) {
  const { uniform, texture, uv, materialColor, sharedUniformGroup } = TSL;
  // (named apart from its uniforms: r186's GLSL names the uniform block after
  // the group, and a block named like its member does not compile)
  // (updated each render, as r186's own renderGroup: a shared group with no
  // update type uploads only when its needsUpdate is set)
  const group = sharedUniformGroup('tintFrame', 0, THREE.NodeUpdateType.RENDER);
  const r = sentinel(), g = sentinel(), b = sentinel();
  const tint = uniform(new THREE.Color().setRGB(r, g, b, THREE.LinearSRGBColorSpace)).setName('tint');
  tint.setGroup(group);
  props['ext.tint'] = { kind: 'vec3', value: [r, g, b] };
  const s = sentinel();
  const scale = uniform(s).setName('tintScale');
  props['ext.tintScale'] = { kind: 'f32', value: [s] };
  // a 2x2 RGBA8 map with a game texture's defaults (linear, mipmapped, clamp:
  // r186 bakes filter and wrap into the program)
  const map = new THREE.DataTexture(new Uint8Array([255, 255, 255, 255, 128, 255, 64, 255, 255, 96, 255, 255, 32, 160, 255, 255]), 2, 2);
  map.magFilter = THREE.LinearFilter; map.minFilter = THREE.LinearMipmapLinearFilter; map.generateMipmaps = true;
  map.wrapS = map.wrapT = THREE.ClampToEdgeWrapping;
  map.name = 'ext.tintMap';
  map.needsUpdate = true;
  const sample = texture(map, uv());
  mat.colorNode = materialColor.mul(tint).mul(sample.rgb).mul(scale);
}
