// Test extension (test/test_many_samples.c): one texture sampled at 24
// places, as a splat terrain samples its layer arrays (base and overlay,
// three triplanar axes, albedo and normal, near and far), so its GL program
// has 24 flipY flags for that texture.
export const name = 'many';
export async function apply({ TSL, THREE, mat }) {
  const { texture, uv, vec2, vec3, materialColor } = TSL;
  const map = new THREE.DataTexture(new Uint8Array(16).fill(255), 2, 2);
  map.name = 'ext.manyMap';
  map.magFilter = map.minFilter = THREE.LinearFilter;
  map.needsUpdate = true;
  let sum = vec3(0);
  for (let i = 0; i < 24; i++) sum = sum.add(texture(map, uv().mul(1 + i * 0.01).add(vec2(i * 0.001))).rgb);
  mat.colorNode = materialColor.mul(sum.div(24));
}
