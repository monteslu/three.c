// e2-extension-shadow: three.c's example extension that moves vertices
// (tools/extensions/swell.mjs) written in r186 TSL: each vertex pushed along
// its normal by a value per object (onObjectUpdate). r186 draws the shadow
// pass with the material's positionNode, so the shadows on the ground are the
// swollen shapes; three.c draws them with the captured depth+xswell caster and
// fills swell from its hook for both passes. The plain sphere casts with the
// plain caster.
const { uniform, positionLocal, normalLocal } = THREE.TSL;
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202830, 1);
renderer.shadowMap.enabled = true;
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 100);
camera.position.set(0, 6, 11);
camera.lookAt(0, 0, 0);
const ground = new THREE.Mesh(new THREE.PlaneGeometry(20, 20), new THREE.MeshStandardMaterial({ color: 0x8090a0, roughness: 0.9 }));
ground.rotation.x = -Math.PI / 2;
ground.receiveShadow = true;
scene.add(ground);
const swell = uniform(0).setName('swell').onObjectUpdate(({ object }) => object.userData.swell);
const ext = new THREE.MeshStandardMaterial({ color: 0xe08040, roughness: 0.5 });
ext.positionNode = positionLocal.add(normalLocal.mul(swell));
const plain = new THREE.MeshStandardMaterial({ color: 0x40a0e0, roughness: 0.5 });
const geos = [new THREE.BoxGeometry(1.2, 1.2, 1.2, 4, 4, 4), new THREE.SphereGeometry(0.8, 32, 16), new THREE.TorusGeometry(0.7, 0.25, 16, 48), new THREE.SphereGeometry(0.8, 32, 16)];
const swells = [0.3, 0.5, 0.15];
const meshes = [];
for (let i = 0; i < 4; i++) {
  const m = new THREE.Mesh(geos[i], i < 3 ? ext : plain);
  m.position.set(i * 3 - 4.5, 1.6, 0);
  if (i < 3) m.userData.swell = swells[i];
  m.castShadow = true;
  scene.add(m);
  meshes.push(m);
}
scene.add(new THREE.AmbientLight(0x303040));
const sun = new THREE.DirectionalLight(0xffffff, 1.2);
sun.position.set(3, 10, 5);
sun.castShadow = true;
sun.shadow.mapSize.set(1024, 1024);
sun.shadow.camera.left = -8; sun.shadow.camera.right = 8; sun.shadow.camera.top = 8; sun.shadow.camera.bottom = -8;
sun.shadow.camera.near = 1; sun.shadow.camera.far = 30;
sun.shadow.bias = -0.0005;
scene.add(sun);
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  for (let i = 0; i < 4; i++) { meshes[i].rotation.x = t * 0.6 + i; meshes[i].rotation.y = t * 0.4 + i * 0.3; }
  renderer.render(scene, camera);
};
