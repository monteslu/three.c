// r7-lod: LOD objects with three levels (icosahedron detail 3, 1, 0), each a
// different colour, at distances 0 / 8 / 16, in a row going away from the
// camera, and one LOD with autoUpdate off switched by hand. The camera
// dollies, so levels change over the frames.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202020, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 200);
const geos = [new THREE.IcosahedronGeometry(1, 3), new THREE.IcosahedronGeometry(1, 1), new THREE.IcosahedronGeometry(1, 0)];
const mats = [new THREE.MeshPhongMaterial({ color: 0x44aaff, flatShading: true }),
  new THREE.MeshPhongMaterial({ color: 0x44ff88, flatShading: true }),
  new THREE.MeshPhongMaterial({ color: 0xff6644, flatShading: true })];
const lods = [];
for (let i = 0; i < 10; i++) {
  const lod = new THREE.LOD();
  for (let k = 2; k >= 0; k--) lod.addLevel(new THREE.Mesh(geos[k], mats[k]), k * 8);
  lod.position.set((i % 2 ? 2.5 : -2.5), 0, -i * 5);
  scene.add(lod);
  lods.push(lod);
}
const manual = new THREE.LOD();
manual.autoUpdate = false;
for (let k = 0; k < 3; k++) manual.addLevel(new THREE.Mesh(geos[k], mats[k]), k * 8);
manual.position.set(0, 3, -6);
scene.add(manual);
scene.add(new THREE.AmbientLight(0x404040));
const sun = new THREE.DirectionalLight(0xffffff, 0.9);
sun.position.set(3, 5, 4);
scene.add(sun);
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  camera.position.set(Math.sin(t) * 2, 2, 8 - t * 4);
  camera.lookAt(0, 0, -25);
  manual.update(camera);
  for (const l of lods) l.rotation.y = t;
  renderer.render(scene, camera);
  globalThis.hits = manual.getCurrentLevel() + 10 * lods[3].getCurrentLevel();
};
