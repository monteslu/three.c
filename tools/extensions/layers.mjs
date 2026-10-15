// An example generator extension with an array texture and an integer texture
// (tools/gen-programs.mjs --states standard+xlayers): a 4x4 R32UI texture,
// read with textureLoad, picks for each cell of the surface a layer of an
// RGBA8 DataArrayTexture, sampled with texture(...).depth(layer). A float
// uniform, layerShift, moves every cell to the next layer.
//   layerIndex  RedIntegerFormat + UnsignedIntType, 4 x 4 (R32UI; r186's TSL reads
//               an integer texture as one only for a 32-bit type)
//   layerMap    DataArrayTexture, 4 x 4 x 4, RGBA8, linear filtered
//   layerShift  float, in r186's default object group
// A texture's settings are part of the program (r186 reads a nearest-filtered
// texture with textureLoad, an integer one never filtered): the application's
// textures must have the settings these have.
export const name = 'layers';
export const LAYERS = 4;
export async function apply({ TSL, THREE, mat, props, sentinel }) {
  const { uniform, texture, textureLoad, uv, ivec2, uint, int, materialColor } = TSL;
  const index = new THREE.DataTexture(new Uint32Array(16).map((_, i) => i % LAYERS), 4, 4, THREE.RedIntegerFormat, THREE.UnsignedIntType);
  index.name = 'ext.layerIndex';
  index.needsUpdate = true;
  const data = new Uint8Array(4 * 4 * 4 * LAYERS);
  for (let i = 0; i < data.length; i += 4) { data[i] = 64 + (i % 7) * 20; data[i + 1] = 200 - (i % 5) * 30; data[i + 2] = 40 + (i % 11) * 15; data[i + 3] = 255; }
  const map = new THREE.DataArrayTexture(data, 4, 4, LAYERS);
  map.magFilter = map.minFilter = THREE.LinearFilter;
  map.name = 'ext.layerMap';
  map.needsUpdate = true;
  const s = sentinel();
  const shift = uniform(s).setName('layerShift');
  props['ext.layerShift'] = { kind: 'f32', value: [s] };
  const cell = ivec2(uv().mul(4).floor().clamp(0, 3));
  const layer = textureLoad(index, cell).r.add(uint(shift)).mod(uint(LAYERS));
  mat.colorNode = materialColor.mul(texture(map, uv()).depth(int(layer)).rgb);
}
