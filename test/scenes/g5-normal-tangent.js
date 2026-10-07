// g5-normal-tangent: Khronos NormalTangentMirrorTest (CC-BY 4.0): normal maps
// with supplied tangents, mirrored UVs, double-sided.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202020, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(45, 1280 / 720, 0.1, 50);
camera.position.set(0, 0, 3.2);
camera.lookAt(0, 0, 0);
scene.add(new THREE.AmbientLight(0x404040));
const sun = new THREE.DirectionalLight(0xffffff, 1);
scene.add(sun);
globalThis.ready = new Promise((ok, bad) => new THREE.GLTFLoader().load('NormalTangentMirrorTest.glb', (g) => {
  scene.add(g.scene); ok();
}, undefined, bad));
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const a = frameNo / 40;
  sun.position.set(Math.cos(a) * 2, Math.sin(a * 0.7) * 2, 2);
  renderer.render(scene, camera);
};
