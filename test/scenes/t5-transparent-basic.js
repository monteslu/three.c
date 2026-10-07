// t5-transparent-basic: Basic materials only, one of them transparent, under
// an orthographic camera (a 2D game's frame). A transparent item sends the
// frame through r186's linear target and output pass; with no textured
// material in the scene, nothing else rebinds the texture unit the output
// pass samples.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202a33, 1);
const scene = new THREE.Scene();
const camera = new THREE.OrthographicCamera(-16, 16, 16.5, -1.5, -10, 10);
camera.position.set(0, 0, 5);
const solid = new THREE.MeshBasicMaterial({ color: 0xe0a040 });
const ground = new THREE.MeshBasicMaterial({ color: 0x5a6676 });
const glass = new THREE.MeshBasicMaterial({ color: 0x4aa3df, transparent: true, opacity: 0.6 });
const box = new THREE.BoxGeometry(1, 1, 1);
const floor = new THREE.Mesh(box, ground);
floor.scale.set(32, 1, 1);
scene.add(floor);
const boxes = [];
for (let i = 0; i < 8; i++) {
  const m = new THREE.Mesh(box, i % 3 === 0 ? glass : solid);
  m.position.set(-12 + i * 3.2, 3, i % 3 === 0 ? 1 : 0);
  m.scale.set(1.6, 1.6, 1);
  scene.add(m);
  boxes.push(m);
}
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  for (let i = 0; i < boxes.length; i++) {
    boxes[i].position.y = 3 + Math.sin(t * 2 + i) * 2;
    boxes[i].rotation.z = t + i;
  }
  renderer.render(scene, camera);
};
