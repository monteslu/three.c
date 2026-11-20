// Test extension (test/run.sh test_uniform_array_refused): an r187 uniformArray
// whose elements no property names, so nothing would fill its buffer binding;
// the generator must refuse the program and name the binding.
export const name = 'arrbare';
export async function apply({ TSL, THREE, mat }) {
  const { uniformArray, materialColor } = TSL;
  const arr = uniformArray([new THREE.Vector4(1, 0.5, 0.25, 1), new THREE.Vector4(1, 1, 1, 1)], 'vec4').setName('arrbare');
  mat.colorNode = materialColor.mul(arr.element(1).xyz);
}
