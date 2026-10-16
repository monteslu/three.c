// Test extension (test/test_flip_copies.mjs): one texture sampled at two
// places, so its program has a flipY flag per sample.
export const name = 'twice';
export async function apply({ TSL, THREE, mat }) {
  const { texture, uv, vec2, materialColor } = TSL;
  const map = new THREE.DataTexture(new Uint8Array(16).fill(200), 2, 2);
  map.name = 'ext.twiceMap';
  map.magFilter = map.minFilter = THREE.LinearFilter;
  map.needsUpdate = true;
  const a = texture(map, uv());
  const b = texture(map, uv().mul(0.5).add(vec2(0.25)));
  mat.colorNode = materialColor.mul(a.rgb).mul(b.rgb);
}
