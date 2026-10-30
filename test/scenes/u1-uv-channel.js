// u1-uv-channel: texture.channel = 1, the second UV set: a box per map slot
// whose uv1 is a shifted and scaled copy of uv, so a map sampled with the wrong
// set shows a different picture: map, aoMap, lightMap, then all three, on the
// built-in materials.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x080810, 1);
const camera = new THREE.PerspectiveCamera(45, 1280 / 720, 0.1, 60);
camera.position.set(0, 1.5, 8);
camera.lookAt(0, 0, 0);
function tex(seed) {
  const n = 32, data = new Uint8Array(n * n * 4);
  for (let y = 0; y < n; y++)
    for (let x = 0; x < n; x++) {
      const i = (y * n + x) * 4;
      data[i] = (x * 8 + seed * 40) & 255; data[i + 1] = (y * 8) & 255; data[i + 2] = ((x ^ y) * 8 + seed * 16) & 255; data[i + 3] = 255;
    }
  const t = new THREE.DataTexture(data, n, n, THREE.RGBAFormat);
  t.magFilter = t.minFilter = THREE.LinearFilter;
  t.channel = 1;
  t.needsUpdate = true;
  return t;
}
const geo = new THREE.BoxGeometry(1.6, 1.6, 1.6);
const uv = geo.attributes.uv, uv1 = new Float32Array(uv.count * 2);
for (let i = 0; i < uv.count; i++) { uv1[i * 2] = uv.getX(i) * 0.25 + 0.6; uv1[i * 2 + 1] = uv.getY(i) * 0.25 + 0.1; }
geo.setAttribute('uv1', new THREE.BufferAttribute(uv1, 2));
const scene = new THREE.Scene();
const a = tex(0), b = tex(1), c = tex(2);
const mats = [
  new THREE.MeshStandardMaterial({ map: a, roughness: 0.6 }),
  new THREE.MeshStandardMaterial({ color: 0xffffff, aoMap: b, roughness: 0.6 }),
  new THREE.MeshStandardMaterial({ color: 0xffffff, lightMap: c, roughness: 0.6 }),
  new THREE.MeshStandardMaterial({ map: a, aoMap: b, lightMap: c, roughness: 0.6 }),
  new THREE.MeshBasicMaterial({ map: a }),
  new THREE.MeshLambertMaterial({ color: 0xffffff, lightMap: c }),
];
mats.forEach((m, i) => {
  const o = new THREE.Mesh(geo, m);
  o.position.set((i % 3 - 1) * 3, i < 3 ? 1.3 : -1.3, 0);
  o.rotation.set(0.4, 0.5, 0);
  scene.add(o);
});
scene.add(new THREE.AmbientLight(0x808080, 1));
const sun = new THREE.DirectionalLight(0xffffff, 1.5);
sun.position.set(-2, 4, 5);
scene.add(sun);
globalThis.frame = function () { renderer.render(scene, camera); };
