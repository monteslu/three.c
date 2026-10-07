// r4-env-map: a procedural CubeTexture as scene.background and as envMap on
// Basic (reflection, multiply), Lambert (refraction), Phong (mix, partial
// reflectivity), Standard (metal, rough with envMapIntensity) and a 2D
// texture background in a second viewport. The camera orbits.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
const S = 64;
const faceTint = [[255, 90, 70], [80, 220, 120], [90, 140, 255], [240, 220, 80], [220, 100, 230], [90, 230, 230]];
function face(f) {
  const d = new Uint8Array(S * S * 4);
  for (let y = 0; y < S; y++) for (let x = 0; x < S; x++) {
    const i = (y * S + x) * 4, g = ((x >> 3) + (y >> 3)) & 1, k = 0.55 + 0.45 * (y / S);
    for (let c = 0; c < 3; c++) d[i + c] = Math.round(faceTint[f][c] * k * (g ? 1 : 0.7));
    if (x === S >> 1 || y === S >> 1) { d[i] = d[i + 1] = d[i + 2] = 255; }
    d[i + 3] = 255;
  }
  const t = new THREE.DataTexture(d, S, S, THREE.RGBAFormat);
  return t;
}
const faces = [0, 1, 2, 3, 4, 5].map(face);
function cube(mapping) {
  const c = new THREE.CubeTexture(faces, mapping);
  c.format = THREE.RGBAFormat;
  c.needsUpdate = true;
  return c;
}
const env = cube(THREE.CubeReflectionMapping);
env.colorSpace = THREE.SRGBColorSpace;
const refr = cube(THREE.CubeRefractionMapping);
const scene = new THREE.Scene();
scene.background = env;
const camera = new THREE.PerspectiveCamera(60, 960 / 720, 0.1, 100);
const geo = new THREE.SphereGeometry(1, 48, 24);
const mats = [
  new THREE.MeshBasicMaterial({ color: 0xffeecc, envMap: env, reflectivity: 0.8 }),
  new THREE.MeshLambertMaterial({ color: 0xffffff, envMap: refr, refractionRatio: 0.9 }),
  new THREE.MeshPhongMaterial({ color: 0x3366aa, envMap: env, combine: THREE.MixOperation, reflectivity: 0.5, shininess: 80 }),
  new THREE.MeshStandardMaterial({ color: 0xffffff, envMap: env, metalness: 1, roughness: 0.15 }),
  new THREE.MeshStandardMaterial({ color: 0xff8844, envMap: env, metalness: 0.4, roughness: 0.6, envMapIntensity: 0.7 }),
  new THREE.MeshPhongMaterial({ color: 0x88ff88, envMap: env, combine: THREE.AddOperation, reflectivity: 0.3 }),
];
for (let i = 0; i < 6; i++) {
  const m = new THREE.Mesh(geo, mats[i]);
  m.position.set((i % 3 - 1) * 2.6, i < 3 ? 1.3 : -1.3, 0);
  scene.add(m);
}
scene.add(new THREE.AmbientLight(0x404040));
const sun = new THREE.DirectionalLight(0xffffff, 0.8);
sun.position.set(3, 4, 5);
scene.add(sun);
// a second scene: a 2D texture background (with uv transform)
const scene2 = new THREE.Scene();
const bg2 = face(2);
bg2.wrapS = bg2.wrapT = THREE.RepeatWrapping;
bg2.repeat.set(2, 1.5);
bg2.needsUpdate = true;
scene2.background = bg2;
const knot = new THREE.Mesh(new THREE.TorusKnotGeometry(0.8, 0.3, 80, 10), new THREE.MeshStandardMaterial({ color: 0xffffff, metalness: 0.8, roughness: 0.3 }));
scene2.environment = env;
scene2.add(knot);
const cam2 = new THREE.PerspectiveCamera(50, 320 / 720, 0.1, 50);
cam2.position.set(0, 0, 6);
let frameNo = 0;
globalThis.frame = function () {
  renderer.setScissorTest(true);
  frameNo++;
  const t = frameNo / 60;
  camera.position.set(Math.sin(t * 0.5) * 7, 1.5, Math.cos(t * 0.5) * 7);
  camera.lookAt(0, 0, 0);
  knot.rotation.set(t, t * 0.6, 0);
  renderer.setViewport(0, 0, 960, 720);
  renderer.setScissor(0, 0, 960, 720);
  renderer.render(scene, camera);
  renderer.setViewport(960, 0, 320, 720);
  renderer.setScissor(960, 0, 320, 720);
  renderer.render(scene2, cam2);
};
