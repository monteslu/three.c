// p1-physical: MeshPhysicalMaterial's lobes, one sphere each: plain physical
// (ior, specularIntensity / specularColor), clearcoat, clearcoat with a
// clearcoatNormalMap, sheen, iridescence, anisotropy. A directional and a
// point light; the spheres turn.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x181c22, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(45, 1280 / 720, 0.1, 100);
camera.position.set(0, 0.6, 9);
camera.lookAt(0, 0, 0);
// a bumpy normal map for the clearcoat
const N = 32, nd = new Uint8Array(N * N * 4);
for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
  const i = (y * N + x) * 4, a = Math.sin(x * 0.8) * 0.5, b = Math.cos(y * 0.6) * 0.5;
  nd[i] = Math.round((a * 0.5 + 0.5) * 255); nd[i + 1] = Math.round((b * 0.5 + 0.5) * 255); nd[i + 2] = 230; nd[i + 3] = 255;
}
const bumps = new THREE.DataTexture(nd, N, N, THREE.RGBAFormat);
bumps.wrapS = bumps.wrapT = THREE.RepeatWrapping;
bumps.repeat.set(3, 2);
bumps.needsUpdate = true;
const geo = new THREE.SphereGeometry(0.8, 48, 24);
const specs = [
  { color: 0xc04040, roughness: 0.5, metalness: 0, ior: 1.8, specularIntensity: 0.7, specularColor: new THREE.Color(0xffd0a0) },
  { color: 0x2050c0, roughness: 0.6, metalness: 0, clearcoat: 1, clearcoatRoughness: 0.05 },
  { color: 0x20a050, roughness: 0.6, metalness: 0, clearcoat: 0.8, clearcoatRoughness: 0.2, clearcoatNormalMap: bumps, clearcoatNormalScale: new THREE.Vector2(0.8, 0.8) },
  { color: 0x602040, roughness: 0.8, metalness: 0, sheen: 1, sheenColor: new THREE.Color(0xffa0c0), sheenRoughness: 0.4 },
  { color: 0x202020, roughness: 0.25, metalness: 0.9, iridescence: 1, iridescenceIOR: 1.6, iridescenceThicknessRange: [100, 520] },
  { color: 0xb0b0b0, roughness: 0.4, metalness: 1, anisotropy: 0.8, anisotropyRotation: 0.6 },
];
const balls = [];
specs.forEach((p, i) => {
  const m = new THREE.Mesh(geo, new THREE.MeshPhysicalMaterial(p));
  m.position.set((i % 3 - 1) * 2.3, i < 3 ? 1.1 : -1.1, 0);
  scene.add(m);
  balls.push(m);
});
scene.add(new THREE.AmbientLight(0x404050, 1));
const sun = new THREE.DirectionalLight(0xffffff, 2.5);
sun.position.set(3, 4, 5);
scene.add(sun);
const lamp = new THREE.PointLight(0xffd8a0, 30, 0, 2);
lamp.position.set(-3, -1, 4);
scene.add(lamp);
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  for (let i = 0; i < balls.length; i++) balls[i].rotation.set(t * 0.3, t * 0.5 + i, 0);
  renderer.render(scene, camera);
};
