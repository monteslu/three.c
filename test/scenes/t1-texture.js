// t1-texture: DataTextures on Basic, Lambert, Phong and Standard materials,
// with repeat, offset, rotation, wrapping and both colour spaces. Animated by
// frame number so a frame index means the same picture on both sides.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202028, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 100);
camera.position.set(0, 1.5, 9);
camera.lookAt(0, 0, 0);
function checker(n, a, b) {
  const d = new Uint8Array(n * n * 4);
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const c = ((x >> 2) + (y >> 2)) & 1 ? a : b, i = (y * n + x) * 4;
    d[i] = c[0]; d[i + 1] = c[1]; d[i + 2] = c[2]; d[i + 3] = 255;
  }
  const t = new THREE.DataTexture(d, n, n, THREE.RGBAFormat);
  t.needsUpdate = true;
  return t;
}
const t1 = checker(32, [230, 60, 40], [250, 240, 220]);
const t2 = checker(16, [40, 90, 220], [240, 240, 240]);
t2.wrapS = t2.wrapT = THREE.RepeatWrapping;
t2.repeat.set(3, 2);
t2.offset.set(0.25, 0.1);
t2.rotation = 0.3;
t2.magFilter = THREE.LinearFilter;
const t3 = checker(64, [30, 160, 80], [250, 220, 90]);
t3.colorSpace = THREE.SRGBColorSpace;
t3.minFilter = THREE.LinearMipmapLinearFilter;
t3.magFilter = THREE.LinearFilter;
t3.generateMipmaps = true;
const mats = [
  new THREE.MeshBasicMaterial({ map: t1 }),
  new THREE.MeshLambertMaterial({ map: t2 }),
  new THREE.MeshPhongMaterial({ map: t3, shininess: 40 }),
  new THREE.MeshStandardMaterial({ map: t1, roughness: 0.6, metalness: 0.1 }),
];
const geos = [new THREE.BoxGeometry(1.6, 1.6, 1.6), new THREE.SphereGeometry(1, 32, 16),
  new THREE.TorusGeometry(0.8, 0.3, 16, 48), new THREE.PlaneGeometry(2, 2)];
const meshes = [];
for (let i = 0; i < 4; i++) {
  const m = new THREE.Mesh(geos[i], mats[i]);
  m.position.set(-4.5 + i * 3, 0, 0);
  scene.add(m);
  meshes.push(m);
}
scene.add(new THREE.AmbientLight(0x404040));
const sun = new THREE.DirectionalLight(0xffffff, 0.9);
sun.position.set(3, 5, 6);
scene.add(sun);
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  for (let i = 0; i < 4; i++) { meshes[i].rotation.x = t * 0.5 + i; meshes[i].rotation.y = t * 0.8 + i * 0.5; }
  renderer.render(scene, camera);
};
