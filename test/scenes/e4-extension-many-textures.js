// e4-extension-many-textures: three.c's example extension with many textures
// (tools/extensions/mosaic.mjs) written in r187 TSL: ten tiles averaged in the
// fragment stage, and a height texture read with textureLoad in both stages
// (r187 binds it twice): it lifts the vertices by a value per object
// (onObjectUpdate) and shades the surface. Thirteen texture bindings in one
// group.
const { uniform, texture, textureLoad, uv, ivec2, float, vec3, positionLocal, normalLocal, materialColor } = THREE.TSL;
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202028, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 100);
camera.position.set(0, 1.5, 9);
camera.lookAt(0, 0, 0);
const TILES = 10;
let sum = vec3(0);
for (let i = 0; i < TILES; i++) {
  const t = new THREE.DataTexture(new Uint8Array([25 * i, 255 - 20 * i, 128, 255, 200, 30 * i, 255 - 25 * i, 255, 90, 90, 20 * i, 255, 255, 255, 255, 255]), 2, 2);
  t.magFilter = t.minFilter = THREE.LinearFilter;
  t.needsUpdate = true;
  sum = sum.add(texture(t, uv()).rgb);
}
const height = new THREE.DataTexture(new Uint8Array(4 * 4 * 4).map((_, i) => (i * 37) & 255), 4, 4);
height.needsUpdate = true;
const lift = uniform(0).setName('mosaicLift').onObjectUpdate(({ object }) => object.userData.lift);
const h = textureLoad(height, ivec2(uv().mul(4).floor().clamp(0, 3))).r;
const ext = new THREE.MeshStandardMaterial({ roughness: 0.6, metalness: 0.1 });
ext.positionNode = positionLocal.add(normalLocal.mul(h.mul(lift)));
ext.colorNode = materialColor.mul(sum.div(float(TILES))).mul(h.add(0.5));
const plain = new THREE.MeshStandardMaterial({ roughness: 0.6, metalness: 0.1 });
const geos = [new THREE.BoxGeometry(1.6, 1.6, 1.6, 4, 4, 4), new THREE.SphereGeometry(1, 32, 16), new THREE.TorusGeometry(0.8, 0.3, 16, 48), new THREE.SphereGeometry(1, 32, 16)];
const lifts = [0, 0.15, 0.3];
const meshes = [];
for (let i = 0; i < 4; i++) {
  const m = new THREE.Mesh(geos[i], i < 3 ? ext : plain);
  m.position.set(-4.5 + i * 3, 0, 0);
  if (i < 3) m.userData.lift = lifts[i];
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
