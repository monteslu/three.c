// e3-extension-textures: three.c's example extension with an array texture
// and an integer texture (tools/extensions/layers.mjs) written in r187 TSL: an
// R32UI texture read with textureLoad picks, per cell of the surface, a layer
// of an RGBA8 DataArrayTexture sampled with texture(...).depth(layer); a
// float per object (onObjectUpdate) shifts the layers. three.c draws the
// captured standard+xlayers program and binds both textures from its hook.
const { uniform, texture, textureLoad, uv, ivec2, uint, int, materialColor } = THREE.TSL;
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202028, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 100);
camera.position.set(0, 1.5, 9);
camera.lookAt(0, 0, 0);
const LAYERS = 4;
const index = new THREE.DataTexture(new Uint32Array(16).map((_, i) => (i * 7 + (i >> 2)) % LAYERS), 4, 4, THREE.RedIntegerFormat, THREE.UnsignedIntType);
index.needsUpdate = true;
const data = new Uint8Array(4 * 4 * 4 * LAYERS);
for (let l = 0; l < LAYERS; l++) for (let i = 0; i < 16; i++) {
  const o = (l * 16 + i) * 4;
  data[o] = [230, 60, 240, 40][l] - (i & 3) * 12; data[o + 1] = [70, 200, 220, 90][l] + (i >> 2) * 8; data[o + 2] = [50, 80, 60, 230][l]; data[o + 3] = 255;
}
const map = new THREE.DataArrayTexture(data, 4, 4, LAYERS);
map.magFilter = map.minFilter = THREE.LinearFilter;
map.needsUpdate = true;
const shift = uniform(0).setName('layerShift').onObjectUpdate(({ object }) => object.userData.shift);
const cell = ivec2(uv().mul(4).floor().clamp(0, 3));
const layer = textureLoad(index, cell).r.add(uint(shift)).mod(uint(LAYERS));
const ext = new THREE.MeshStandardMaterial({ roughness: 0.6, metalness: 0.1 });
ext.colorNode = materialColor.mul(texture(map, uv()).depth(int(layer)).rgb);
const plain = new THREE.MeshStandardMaterial({ roughness: 0.6, metalness: 0.1 });
const geos = [new THREE.BoxGeometry(1.6, 1.6, 1.6), new THREE.SphereGeometry(1, 32, 16), new THREE.TorusGeometry(0.8, 0.3, 16, 48), new THREE.SphereGeometry(1, 32, 16)];
const meshes = [];
for (let i = 0; i < 4; i++) {
  const m = new THREE.Mesh(geos[i], i < 3 ? ext : plain);
  m.position.set(-4.5 + i * 3, 0, 0);
  if (i < 3) m.userData.shift = i;
  scene.add(m);
  meshes.push(m);
}
scene.add(new THREE.AmbientLight(0x404040));
const sun = new THREE.DirectionalLight(0xffffff, 1.2);
sun.position.set(3, 5, 6);
scene.add(sun);
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  for (let i = 0; i < 4; i++) { meshes[i].rotation.x = t * 0.5 + i; meshes[i].rotation.y = t * 0.8 + i * 0.5; }
  renderer.render(scene, camera);
};
