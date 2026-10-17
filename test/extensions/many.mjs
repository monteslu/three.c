// Test extension (test/test_many_samples.c): what a stack of shading parts
// adds up to on one material (a splat terrain under clustered lights and
// cascades): one texture sampled at 24 places, as the terrain samples its
// layer arrays (base and overlay, three triplanar axes, albedo and normal,
// near and far), so its GL program has 24 flipY flags for that texture; and
// 40 uniforms of its own (the terrain's 8, the cluster's 2 and the cascades'
// 11 are 21). The colour is the texture's mean times every uniform (the hook
// fills them all with 1).
export const name = 'many';
export const UNIFORMS = 40;
export async function apply({ TSL, THREE, mat, props, sentinel }) {
  const { texture, uv, vec2, vec3, uniform, materialColor } = TSL;
  const map = new THREE.DataTexture(new Uint8Array(16).fill(255), 2, 2);
  map.name = 'ext.manyMap';
  map.magFilter = map.minFilter = THREE.LinearFilter;
  map.needsUpdate = true;
  let sum = vec3(0);
  for (let i = 0; i < 24; i++) sum = sum.add(texture(map, uv().mul(1 + i * 0.01).add(vec2(i * 0.001))).rgb);
  let k = sum.div(24);
  for (let i = 0; i < UNIFORMS; i++) {
    const s = sentinel();
    const u = uniform(s).setName(`many${i}`);
    props[`ext.many${i}`] = { kind: 'f32', value: [s] };
    k = k.mul(u);
  }
  mat.colorNode = materialColor.mul(k);
}
