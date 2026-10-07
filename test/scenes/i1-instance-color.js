// i1-instance-color: InstancedMesh.instanceColor (setColorAt). A grid of
// Standard boxes casting and receiving a directional shadow, a ring of
// Lambert spheres, and a row of Basic boxes whose colours change every frame
// (instanceColor.needsUpdate). Instances move by their matrices each frame.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x182028, 1);
renderer.shadowMap.enabled = true;
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 100);
camera.position.set(0, 8, 14);
camera.lookAt(0, 0, 0);
const ground = new THREE.Mesh(new THREE.PlaneGeometry(24, 24), new THREE.MeshStandardMaterial({ color: 0x8090a0, roughness: 0.9 }));
ground.rotation.x = -Math.PI / 2;
ground.receiveShadow = true;
scene.add(ground);

const grid = new THREE.InstancedMesh(new THREE.BoxGeometry(0.8, 0.8, 0.8), new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.6 }), 64);
grid.castShadow = true;
grid.receiveShadow = true;
const c = new THREE.Color();
for (let i = 0; i < 64; i++) grid.setColorAt(i, c.setRGB((i % 8) / 7, Math.floor(i / 8) / 7, 0.5));
scene.add(grid);

const ring = new THREE.InstancedMesh(new THREE.SphereGeometry(0.35, 16, 8), new THREE.MeshLambertMaterial({ color: 0xffffff }), 300);
for (let i = 0; i < 300; i++) ring.setColorAt(i, c.setRGB(0.5 + 0.5 * Math.sin(i * 0.3), 0.5 + 0.5 * Math.sin(i * 0.3 + 2), 0.5 + 0.5 * Math.sin(i * 0.3 + 4)));
scene.add(ring);

const row = new THREE.InstancedMesh(new THREE.BoxGeometry(0.6, 0.6, 0.6), new THREE.MeshBasicMaterial({ color: 0xffffff }), 16);
scene.add(row);

scene.add(new THREE.AmbientLight(0x303040));
const sun = new THREE.DirectionalLight(0xffffff, 1.5);
sun.position.set(5, 10, 4);
sun.castShadow = true;
sun.shadow.mapSize.set(1024, 1024);
sun.shadow.camera.left = -10; sun.shadow.camera.right = 10; sun.shadow.camera.top = 10; sun.shadow.camera.bottom = -10;
sun.shadow.camera.near = 1; sun.shadow.camera.far = 30;
sun.shadow.bias = -0.0005;
scene.add(sun);

const m = new THREE.Matrix4();
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  for (let i = 0; i < 64; i++) {
    m.makeTranslation((i % 8) * 1.2 - 4.2, 0.4 + 0.3 * Math.sin(t * 2 + i * 0.5), Math.floor(i / 8) * 1.2 - 4.2);
    grid.setMatrixAt(i, m);
  }
  grid.instanceMatrix.needsUpdate = true;
  for (let i = 0; i < 300; i++) {
    const a = i / 300 * Math.PI * 2 + t * 0.3, rad = 7 + 0.5 * Math.sin(i * 0.7);
    m.makeScale(0.6 + 0.4 * ((i * 7) % 5) / 4, 0.6 + 0.4 * ((i * 7) % 5) / 4, 0.6 + 0.4 * ((i * 7) % 5) / 4);
    m.setPosition(Math.cos(a) * rad, 1.5 + Math.sin(i * 1.3) * 0.8, Math.sin(a) * rad);
    ring.setMatrixAt(i, m);
  }
  ring.instanceMatrix.needsUpdate = true;
  for (let i = 0; i < 16; i++) {
    m.makeTranslation(i * 0.9 - 6.75, 4.5, -3);
    row.setMatrixAt(i, m);
    row.setColorAt(i, c.setRGB(0.5 + 0.5 * Math.sin(t * 3 + i * 0.4), 0.3, 0.5 + 0.5 * Math.cos(t * 2 + i * 0.4)));
  }
  row.instanceMatrix.needsUpdate = true;
  row.instanceColor.needsUpdate = true;
  renderer.render(scene, camera);
};
