// Test extension (test/test_alpha_coverage.c): r187's alphaToCoverage program on the capture material (its alpha
// test as smoothstep(alphaTest, alphaTest + fwidth(a), a), the alpha kept), with an alpha that ramps across the
// mesh: uv.x.
export const name = 'a2c';
export async function apply({ TSL, mat }) {
  mat.alphaToCoverage = true;
  mat.opacityNode = TSL.materialOpacity.mul(TSL.uv().x);
}
