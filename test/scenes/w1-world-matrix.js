// w1-world-matrix: a moving group with two child meshes and a row of
// spinning boxes. three.js computes every world matrix; the C port writes the
// group's and the boxes' world matrices itself (an embedder that owns them,
// t3_object_set_matrix_world), so the pixels must still match.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x181818, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 100);
camera.position.set(0, 3, 12);
camera.lookAt(0, 0, 0);
const group = new THREE.Group();
scene.add(group);
const a = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), new THREE.MeshPhongMaterial({ color: 0xff8844 }));
a.position.set(2, 0, 0);
group.add(a);
const b = new THREE.Mesh(new THREE.SphereGeometry(0.6, 24, 12), new THREE.MeshPhongMaterial({ color: 0x44aaff }));
b.position.set(-2, 0.5, 0);
group.add(b);
const boxes = [];
const boxGeo = new THREE.BoxGeometry(0.7, 0.7, 0.7), boxMat = new THREE.MeshLambertMaterial({ color: 0x88ee66 });
for (let i = 0; i < 6; i++) {
  const m = new THREE.Mesh(boxGeo, boxMat);
  scene.add(m);
  boxes.push(m);
}
scene.add(new THREE.AmbientLight(0x404040));
const sun = new THREE.DirectionalLight(0xffffff, 0.9);
sun.position.set(3, 5, 4);
scene.add(sun);
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  group.position.set(Math.sin(t) * 1.5, 1, 0);
  group.rotation.set(0, t, 0.2);
  a.rotation.x = t * 2;
  for (let i = 0; i < 6; i++) {
    boxes[i].position.set(-5 + i * 2, -2, Math.sin(t + i));
    boxes[i].rotation.set(t + i, t * 0.5, 0);
    boxes[i].scale.set(1, 1 + 0.3 * Math.sin(t * 2 + i), 1);
  }
  renderer.render(scene, camera);
};
