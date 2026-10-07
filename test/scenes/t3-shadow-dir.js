// t3-shadow-dir: a directional light casting PCF shadows from moving boxes,
// spheres and a torus knot onto a ground plane and onto each other.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202830, 1);
renderer.shadowMap.enabled = true;
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 100);
camera.position.set(0, 7, 12);
camera.lookAt(0, 0, 0);
const ground = new THREE.Mesh(new THREE.PlaneGeometry(20, 20), new THREE.MeshStandardMaterial({ color: 0x8090a0, roughness: 0.9 }));
ground.rotation.x = -Math.PI / 2;
ground.receiveShadow = true;
scene.add(ground);
const casters = [];
const shapes = [new THREE.BoxGeometry(1.2, 1.2, 1.2), new THREE.SphereGeometry(0.8, 32, 16), new THREE.TorusKnotGeometry(0.6, 0.2, 96, 12)];
const cmats = [new THREE.MeshStandardMaterial({ color: 0xe05040, roughness: 0.5 }), new THREE.MeshLambertMaterial({ color: 0x40a0e0 }),
  new THREE.MeshPhongMaterial({ color: 0xe0c040, shininess: 60 })];
for (let i = 0; i < 9; i++) {
  const m = new THREE.Mesh(shapes[i % 3], cmats[(i + Math.floor(i / 3)) % 3]);
  m.position.set((i % 3) * 3 - 3, 1.2 + (i % 2) * 0.8, Math.floor(i / 3) * 3 - 3);
  m.castShadow = true;
  m.receiveShadow = true;
  scene.add(m);
  casters.push(m);
}
scene.add(new THREE.AmbientLight(0x303040));
const sun = new THREE.DirectionalLight(0xffffff, 1);
sun.position.set(5, 10, 4);
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
  for (let i = 0; i < casters.length; i++) { casters[i].rotation.x = t * 0.7 + i; casters[i].rotation.y = t * 0.5 + i * 0.3; }
  renderer.render(scene, camera);
};
