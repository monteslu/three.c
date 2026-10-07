// g1-box-textured: Khronos BoxTextured (baseColorTexture, sRGB) through
// GLTFLoader, lit by an ambient and a directional light, rotating.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x303438, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(45, 1280 / 720, 0.1, 100);
camera.position.set(0, 1.2, 3);
camera.lookAt(0, 0, 0);
scene.add(new THREE.AmbientLight(0x606060));
const sun = new THREE.DirectionalLight(0xffffff, 1);
sun.position.set(2, 3, 4);
scene.add(sun);
let model = null;
globalThis.ready = new Promise((ok, bad) => new THREE.GLTFLoader().load('BoxTextured.glb', (g) => {
  model = g.scene; scene.add(model); ok();
}, undefined, bad));
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  if (model) model.rotation.y = frameNo / 60;
  renderer.render(scene, camera);
};
