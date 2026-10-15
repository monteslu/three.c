// e1-extension: three.c's example extension (tools/extensions/tint.mjs) written
// in r186 TSL. Three objects share one MeshStandardMaterial whose colour is the
// material colour x a tint uniform in a group of its own (changed every frame)
// x a texture x a scale per object (onObjectUpdate). three.c draws the
// captured standard+map+xtint program and fills the three from its hook. A
// fourth object has the same material without the extension.
const { uniform, texture, uv, materialColor, sharedUniformGroup } = THREE.TSL;
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202028, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 100);
camera.position.set(0, 1.5, 9);
camera.lookAt(0, 0, 0);
function checker(n, a, b) {
  const d = new Uint8Array(n * n * 4);
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const c = ((x >> 2) + (y >> 2)) & 1 ? a : b, i = (y * n + x) * 4;
    d[i] = c[0]; d[i + 1] = c[1]; d[i + 2] = c[2]; d[i + 3] = 255;
  }
  const t = new THREE.DataTexture(d, n, n, THREE.RGBAFormat);
  // filtered and mipmapped, clamped: the filtering the captured program bakes
  t.magFilter = THREE.LinearFilter; t.minFilter = THREE.LinearMipmapLinearFilter; t.generateMipmaps = true;
  t.needsUpdate = true;
  return t;
}
const map = checker(32, [230, 200, 160], [250, 250, 250]);
const tintMap = checker(16, [255, 120, 60], [80, 160, 255]);
const tint = uniform(new THREE.Color(1, 1, 1)).setName('tint');
tint.setGroup(sharedUniformGroup('tintFrame', 0, THREE.NodeUpdateType.RENDER));
const tintScale = uniform(1).setName('tintScale').onObjectUpdate(({ object }) => object.userData.tintScale);
const ext = new THREE.MeshStandardMaterial({ map, roughness: 0.5, metalness: 0.1 });
ext.colorNode = materialColor.mul(tint).mul(texture(tintMap, uv()).rgb).mul(tintScale);
const plain = new THREE.MeshStandardMaterial({ map, roughness: 0.5, metalness: 0.1 });
const geos = [new THREE.BoxGeometry(1.6, 1.6, 1.6), new THREE.SphereGeometry(1, 32, 16), new THREE.TorusGeometry(0.8, 0.3, 16, 48), new THREE.SphereGeometry(1, 32, 16)];
const scales = [0.6, 1, 1.4];
const meshes = [];
for (let i = 0; i < 4; i++) {
  const m = new THREE.Mesh(geos[i], i < 3 ? ext : plain);
  m.position.set(-4.5 + i * 3, 0, 0);
  if (i < 3) m.userData.tintScale = scales[i];
  scene.add(m);
  meshes.push(m);
}
scene.add(new THREE.AmbientLight(0x404040));
const sun = new THREE.DirectionalLight(0xffffff, 0.9);
sun.position.set(3, 5, 6);
scene.add(sun);
const lamp = new THREE.PointLight(0xffe0c0, 6, 0, 2);
lamp.position.set(-2, 2, 3);
scene.add(lamp);
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  tint.value.setRGB(1, 0.6 + 0.4 * Math.sin(t), 0.8, THREE.LinearSRGBColorSpace);
  for (let i = 0; i < 4; i++) { meshes[i].rotation.x = t * 0.5 + i; meshes[i].rotation.y = t * 0.8 + i * 0.5; }
  renderer.render(scene, camera);
};
