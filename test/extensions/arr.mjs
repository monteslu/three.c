// Test extension (test/run.sh test_uniform_array_refused): an r187 uniformArray, a buffer
// binding of its own in the object group beside the draw's uniform block. The
// colour is material colour x arr[0].rgb x arr[3].rgb (vec4 elements).
export const name = 'arr';
export const COUNT = 4;
export async function apply({ TSL, THREE, mat, props, sentinel }) {
  const { uniformArray, materialColor } = TSL;
  const vals = [];
  for (let i = 0; i < COUNT; i++) {
    const s = [sentinel(), sentinel(), sentinel(), sentinel()];
    vals.push(new THREE.Vector4(...s));
    props[`ext.arr${i}`] = { kind: 'vec4', value: s };
  }
  const arr = uniformArray(vals, 'vec4').setName('arr');
  mat.colorNode = materialColor.mul(arr.element(0).xyz).mul(arr.element(3).xyz);
}
