// g4-water-bottle: Khronos WaterBottle (CC0): baseColor, metallicRoughness,
// normal (with vertex tangents), occlusion and emissive textures, rotating.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x1c2024, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(35, 1280 / 720, 0.01, 10);
camera.position.set(0, 0.05, 0.6);
camera.lookAt(0, 0, 0);
scene.add(new THREE.HemisphereLight(0xffffff, 0x404040, 0.7));
const sun = new THREE.DirectionalLight(0xffffff, 1.2);
sun.position.set(1, 2, 2);
scene.add(sun);
const rim = new THREE.PointLight(0x80a0ff, 1.0, 5, 1);
rim.position.set(-0.4, 0.2, -0.3);
scene.add(rim);
let model = null;
globalThis.ready = new Promise((ok, bad) => new THREE.GLTFLoader().load('WaterBottle.glb', (g) => {
  model = g.scene; scene.add(model); ok();
}, undefined, bad));
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  if (model) { model.rotation.y = frameNo / 50; model.rotation.x = 0.3; }
  renderer.render(scene, camera);
};
