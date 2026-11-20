// m1-toon: MeshToonMaterial. Five spheres on a toon ground under a shadowed
// sun, a point light and an ambient light: r187's two tones (no gradientMap),
// a three-tone and a five-tone gradientMap (NearestFilter, as three.js asks),
// a colour map with a gradientMap, and an emissive one.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x20242c, 1);
renderer.shadowMap.enabled = true;
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(45, 1280 / 720, 0.1, 100);
camera.position.set(0, 4, 12);
camera.lookAt(0, 0.6, 0);
function ramp(n) {
  const d = new Uint8Array(n * 4);
  for (let i = 0; i < n; i++) { const v = Math.round(((i + 1) / n) * 255); d[i * 4] = d[i * 4 + 1] = d[i * 4 + 2] = v; d[i * 4 + 3] = 255; }
  const t = new THREE.DataTexture(d, n, 1, THREE.RGBAFormat);
  t.needsUpdate = true;
  return t;
}
const three = ramp(3), five = ramp(5);
const N = 32, data = new Uint8Array(N * N * 4);
for (let y = 0; y < N; y++)
  for (let x = 0; x < N; x++) {
    const i = (y * N + x) * 4, on = ((x >> 3) + (y >> 3)) & 1;
    data[i] = on ? 230 : 60; data[i + 1] = on ? 200 : 120; data[i + 2] = on ? 90 : 200; data[i + 3] = 255;
  }
const map = new THREE.DataTexture(data, N, N, THREE.RGBAFormat);
map.magFilter = map.minFilter = THREE.LinearFilter;
map.colorSpace = THREE.SRGBColorSpace;
map.needsUpdate = true;
const ground = new THREE.Mesh(new THREE.PlaneGeometry(18, 12), new THREE.MeshToonMaterial({ color: 0x6f8f5f, gradientMap: three }));
ground.rotation.x = -Math.PI / 2;
ground.receiveShadow = true;
scene.add(ground);
const geo = new THREE.SphereGeometry(0.9, 48, 24);
const mats = [
  new THREE.MeshToonMaterial({ color: 0xe05040 }),
  new THREE.MeshToonMaterial({ color: 0x40a0e0, gradientMap: three }),
  new THREE.MeshToonMaterial({ color: 0xe0c040, gradientMap: five }),
  new THREE.MeshToonMaterial({ map, gradientMap: three }),
  new THREE.MeshToonMaterial({ color: 0x303030, gradientMap: five, emissive: 0xff6020, emissiveIntensity: 0.6 }),
];
mats.forEach((m, i) => {
  const s = new THREE.Mesh(geo, m);
  s.position.set((i - 2) * 2.3, 0.9, 0);
  s.castShadow = true;
  s.receiveShadow = true;
  scene.add(s);
});
scene.add(new THREE.AmbientLight(0x404858, 1));
const sun = new THREE.DirectionalLight(0xffffff, 2.5);
sun.position.set(5, 8, 4);
sun.castShadow = true;
sun.shadow.mapSize.set(1024, 1024);
sun.shadow.camera.left = -9; sun.shadow.camera.right = 9; sun.shadow.camera.top = 9; sun.shadow.camera.bottom = -9;
sun.shadow.camera.near = 1; sun.shadow.camera.far = 30;
sun.shadow.bias = -0.0005;
scene.add(sun);
const lamp = new THREE.PointLight(0x60a0ff, 30, 0, 2);
lamp.position.set(-4, 2.5, 3);
scene.add(lamp);
globalThis.frame = function () { renderer.render(scene, camera); };
