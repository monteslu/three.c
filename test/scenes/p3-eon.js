// p3-eon: MeshPhysicalMaterial.diffuseRoughness (r187's EON rough diffuse): a
// row of spheres from 0 (Lambert) to 1, under a light from the side (the
// grazing terminator is where EON differs), the top row with a map and a
// scene environment.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x101018, 1);
const camera = new THREE.PerspectiveCamera(40, 1280 / 720, 0.1, 60);
camera.position.set(0, 0, 11);
const scene = new THREE.Scene();
const n = 32, data = new Uint8Array(n * n * 4);
for (let y = 0; y < n; y++)
  for (let x = 0; x < n; x++) {
    const i = (y * n + x) * 4;
    data[i] = 120 + ((x ^ y) & 7) * 16; data[i + 1] = 90 + ((x + y) & 7) * 12; data[i + 2] = 70; data[i + 3] = 255;
  }
const map = new THREE.DataTexture(data, n, n, THREE.RGBAFormat);
map.magFilter = map.minFilter = THREE.LinearFilter;
map.colorSpace = THREE.SRGBColorSpace;
map.needsUpdate = true;
const geo = new THREE.SphereGeometry(0.85, 48, 24);
const rough = [0, 0.25, 0.5, 0.75, 1];
for (let i = 0; i < 5; i++) {
  const a = new THREE.Mesh(geo, new THREE.MeshPhysicalMaterial({ color: 0xc08060, roughness: 0.9, metalness: 0, diffuseRoughness: rough[i] }));
  a.position.set((i - 2) * 2, -1.1, 0);
  scene.add(a);
  const b = new THREE.Mesh(geo, new THREE.MeshPhysicalMaterial({ map, roughness: 0.7, metalness: 0, diffuseRoughness: rough[i] }));
  b.position.set((i - 2) * 2, 1.1, 0);
  scene.add(b);
}
scene.add(new THREE.AmbientLight(0x202030, 1));
const sun = new THREE.DirectionalLight(0xffffff, 3);
sun.position.set(6, 1, 1);
scene.add(sun);
globalThis.frame = function () { renderer.render(scene, camera); };
