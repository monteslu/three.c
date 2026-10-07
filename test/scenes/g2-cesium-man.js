// g2-cesium-man: Khronos CesiumMan, a skinned mesh (19 joints) with a
// textured material, walking on its first animation clip.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x2a3036, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(40, 1280 / 720, 0.05, 50);
camera.position.set(1.6, 1.0, 2.4);
camera.lookAt(0, 0.8, 0);
scene.add(new THREE.HemisphereLight(0xddeeff, 0x302010, 0.8));
const sun = new THREE.DirectionalLight(0xffffff, 0.8);
sun.position.set(2, 4, 3);
scene.add(sun);
let mixer = null;
globalThis.ready = new Promise((ok, bad) => new THREE.GLTFLoader().load('CesiumMan.glb', (g) => {
  scene.add(g.scene);
  mixer = new THREE.AnimationMixer(g.scene);
  mixer.clipAction(g.animations[0]).play();
  ok();
}, undefined, bad));
globalThis.frame = function () {
  if (mixer) mixer.update(1 / 60);
  renderer.render(scene, camera);
};
