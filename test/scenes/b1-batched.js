// b1-batched: BatchedMesh. Four different geometries (box, sphere, cone,
// torus), 48 instances in a grid, each with its own position, rotation and
// scale, one textured material, under a shadowed sun: they cast on the ground
// and on each other. One instance is hidden (setVisibleAt) and one deleted.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x1c2430, 1);
renderer.shadowMap.enabled = true;
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(45, 1280 / 720, 0.1, 100);
camera.position.set(0, 9, 14);
camera.lookAt(0, 0.5, 0);
const N = 32, data = new Uint8Array(N * N * 4);
for (let y = 0; y < N; y++)
  for (let x = 0; x < N; x++) {
    const i = (y * N + x) * 4, on = ((x >> 2) + (y >> 2)) & 1;
    data[i] = on ? 235 : 150; data[i + 1] = on ? 225 : 120; data[i + 2] = on ? 200 : 90; data[i + 3] = 255;
  }
const map = new THREE.DataTexture(data, N, N, THREE.RGBAFormat);
map.magFilter = map.minFilter = THREE.LinearFilter;
map.colorSpace = THREE.SRGBColorSpace;
map.needsUpdate = true;
const geos = [new THREE.BoxGeometry(1, 1, 1), new THREE.SphereGeometry(0.6, 20, 12), new THREE.ConeGeometry(0.55, 1.2, 16), new THREE.TorusGeometry(0.45, 0.2, 10, 24)];
let nv = 0, ni = 0;
for (const g of geos) { nv += g.attributes.position.count; ni += g.index.count; }
const batch = new THREE.BatchedMesh(48, nv * 12, ni * 12, new THREE.MeshStandardMaterial({ map, roughness: 0.7 }));
batch.castShadow = true;
batch.receiveShadow = true;
const ids = geos.map((g) => batch.addGeometry(g));
const m4 = new THREE.Matrix4(), p = new THREE.Vector3(), q = new THREE.Quaternion(), s = new THREE.Vector3(), e = new THREE.Euler();
const inst = [];
for (let k = 0; k < 48; k++) {
  const col = k % 8, row = (k / 8) | 0;
  const id = batch.addInstance(ids[(k * 7 + row) % 4]);
  p.set((col - 3.5) * 1.9, 0.75 + 0.25 * ((k * 5) % 3), (row - 2.5) * 1.9);
  e.set(0.3 * (k % 5), 0.7 * (k % 7), 0.2 * (k % 3));
  q.setFromEuler(e);
  const sc = 0.7 + 0.1 * (k % 4);
  s.set(sc, sc, sc);
  batch.setMatrixAt(id, m4.compose(p, q, s));
  inst.push(id);
}
batch.setVisibleAt(inst[10], false);
batch.deleteInstance(inst[29]);
scene.add(batch);
const ground = new THREE.Mesh(new THREE.PlaneGeometry(24, 18), new THREE.MeshStandardMaterial({ color: 0x70808c, roughness: 0.9 }));
ground.rotation.x = -Math.PI / 2;
ground.receiveShadow = true;
scene.add(ground);
scene.add(new THREE.AmbientLight(0x404858, 1));
const sun = new THREE.DirectionalLight(0xffffff, 2.5);
sun.position.set(6, 10, 5);
sun.castShadow = true;
sun.shadow.mapSize.set(2048, 2048);
sun.shadow.camera.left = -12; sun.shadow.camera.right = 12; sun.shadow.camera.top = 12; sun.shadow.camera.bottom = -12;
sun.shadow.camera.near = 1; sun.shadow.camera.far = 40;
sun.shadow.bias = -0.0005;
scene.add(sun);
globalThis.frame = function () { renderer.render(scene, camera); };
