// An example generator extension that moves vertices (tools/gen-programs.mjs
// --states standard+xswell,depth+xswell): each vertex is pushed along its
// normal by a value per object, swell (r187's default object group). r187
// draws the shadow pass with the material's positionNode, so the caster
// state depth+xswell carries the same push and the shadow follows the
// surface; the hook fills swell for both draws.
export const name = 'swell';
export async function apply({ TSL, mat, props, sentinel }) {
  const { uniform, positionLocal, normalLocal } = TSL;
  const s = sentinel();
  const swell = uniform(s).setName('swell');
  props['ext.swell'] = { kind: 'f32', value: [s] };
  mat.positionNode = positionLocal.add(normalLocal.mul(swell));
}
