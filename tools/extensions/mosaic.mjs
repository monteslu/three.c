// An example generator extension with many textures (tools/gen-programs.mjs
// --states standard+xmosaic): ten 2x2 tiles (ext.tile0 .. ext.tile9), averaged
// in the fragment stage, and a 4x4 height texture (ext.height) read with
// textureLoad in both stages: the vertex stage pushes each vertex along its
// normal by the height of its cell times mosaicLift (a value per object), the
// fragment stage shades with it. r186 binds a texture read in both stages
// twice, so with the material's own DFG LUT the program has thirteen texture
// bindings, more than a WebGPU bind group needs for anything else three.c
// draws.
export const name = 'mosaic';
export const TILES = 10;
export async function apply({ TSL, THREE, mat, props, sentinel }) {
  const { uniform, texture, textureLoad, uv, ivec2, float, vec3, positionLocal, normalLocal, materialColor } = TSL;
  let sum = vec3(0);
  for (let i = 0; i < TILES; i++) {
    const t = new THREE.DataTexture(new Uint8Array([25 * i, 255 - 20 * i, 128, 255, 200, 30 * i, 255 - 25 * i, 255, 90, 90, 20 * i, 255, 255, 255, 255, 255]), 2, 2);
    t.magFilter = t.minFilter = THREE.LinearFilter;
    t.name = `ext.tile${i}`;
    t.needsUpdate = true;
    sum = sum.add(texture(t, uv()).rgb);
  }
  const height = new THREE.DataTexture(new Uint8Array(4 * 4 * 4).map((_, i) => (i * 37) & 255), 4, 4);
  height.name = 'ext.height';
  height.needsUpdate = true;
  const s = sentinel();
  const lift = uniform(s).setName('mosaicLift');
  props['ext.mosaicLift'] = { kind: 'f32', value: [s] };
  const h = textureLoad(height, ivec2(uv().mul(4).floor().clamp(0, 3))).r;
  mat.positionNode = positionLocal.add(normalLocal.mul(h.mul(lift)));
  mat.colorNode = materialColor.mul(sum.div(float(TILES))).mul(h.add(0.5));
}
