// g3-morph-cube: Khronos AnimatedMorphCube, morph targets (positions and
// normals) driven by its weights animation.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x283038, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(40, 1280 / 720, 0.01, 50);
camera.position.set(2.5, 2, 3.5);
camera.lookAt(0, 0, 0);
scene.add(new THREE.AmbientLight(0x505050));
const sun = new THREE.DirectionalLight(0xffffff, 0.9);
sun.position.set(3, 5, 2);
scene.add(sun);
let mixer = null;
globalThis.ready = new Promise((ok, bad) => new THREE.GLTFLoader().load('AnimatedMorphCube.glb', (g) => {
  scene.add(g.scene);
  mixer = new THREE.AnimationMixer(g.scene);
  mixer.clipAction(g.animations[0]).play();
  ok();
}, undefined, bad));
globalThis.frame = function () {
  if (mixer) mixer.update(1 / 60);
  renderer.render(scene, camera);
};
