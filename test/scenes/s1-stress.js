// s1-stress: a CPU-bound scene, for the CPU side of three.c (culling,
// raycasting, matrix updates) rather than the GPU. 200 groups of 100 small
// boxes, every group spinning (20,000 world matrices a frame), a camera
// orbiting the field so much of it is off screen, and 8 raycasts a frame
// against all of it. bench/scenes.c "s1-stress" is the same scene in C.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x202028);
const camera = new THREE.PerspectiveCamera(60, 1280 / 720, 0.1, 500);
let seed = 20261005;
const rand = () => (seed = seed * 16807 % 2147483647) / 2147483647;
scene.add(new THREE.AmbientLight(0x404040));
const sun = new THREE.DirectionalLight(0xffffff, 0.8);
sun.position.set(1, 2, 1);
scene.add(sun);
const geo = new THREE.BoxGeometry(0.4, 0.4, 0.4);
const mats = [];
for (let i = 0; i < 8; i++) mats.push(new THREE.MeshLambertMaterial({ color: new THREE.Color().setHSL(i / 8, 0.6, 0.5) }));
const groups = [];
for (let g = 0; g < 200; g++) {
  const grp = new THREE.Group();
  grp.position.set(rand() * 160 - 80, rand() * 20 - 10, rand() * 160 - 80);
  scene.add(grp);
  groups.push(grp);
  for (let i = 0; i < 100; i++) {
    const m = new THREE.Mesh(geo, mats[(g + i) % 8]);
    m.position.set(rand() * 8 - 4, rand() * 8 - 4, rand() * 8 - 4);
    m.rotation.set(rand() * 6.28, rand() * 6.28, 0);
    grp.add(m);
  }
}
const raycaster = new THREE.Raycaster();
const ndc = new THREE.Vector2();
let frameNo = 0;
globalThis.hits = 0;
globalThis.frame = () => {
  frameNo++;
  const t = frameNo * 0.01;
  for (let g = 0; g < groups.length; g++) groups[g].rotation.y = t + g;
  camera.position.set(Math.cos(t * 0.5) * 60, 25, Math.sin(t * 0.5) * 60);
  camera.lookAt(0, 0, 0);
  scene.updateMatrixWorld();
  camera.updateMatrixWorld(); // not in the scene: rays need this frame's camera
  for (let i = 0; i < 8; i++) {
    ndc.set(Math.sin(i * 1.7 + t) * 0.9, Math.cos(i * 1.3 + t) * 0.9);
    raycaster.setFromCamera(ndc, camera);
    globalThis.hits += raycaster.intersectObjects(scene.children, true).length;
  }
  renderer.render(scene, camera);
};
