// r5-pmrem: PMREMGenerator from a CubeTexture, from an equirectangular
// DataTexture and from a scene (with a colour background and a blur sigma);
// rows of MeshStandardMaterial spheres from mirror to rough read each one, and
// the cubemap's PMREM is also the scene background (CubeUV on the box).
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x000000, 1);
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
  return new THREE.DataTexture(d, S, S, THREE.RGBAFormat);
}
const cube = new THREE.CubeTexture([0, 1, 2, 3, 4, 5].map(face));
cube.format = THREE.RGBAFormat;
cube.colorSpace = THREE.SRGBColorSpace;
cube.needsUpdate = true;
// an equirectangular sky: a gradient, a sun and bands
const EW = 128, EH = 64, ed = new Uint8Array(EW * EH * 4);
for (let y = 0; y < EH; y++) for (let x = 0; x < EW; x++) {
  const i = (y * EW + x) * 4, v = y / (EH - 1), band = (x >> 4) & 1;
  const sun = Math.max(0, 1 - Math.hypot(x - 90, y - 44) / 6);
  ed[i] = Math.min(255, Math.round(40 + 150 * v + 200 * sun + 30 * band));
  ed[i + 1] = Math.min(255, Math.round(60 + 120 * v + 180 * sun));
  ed[i + 2] = Math.min(255, Math.round(120 + 100 * (1 - v) + 100 * sun));
  ed[i + 3] = 255;
}
const equirect = new THREE.DataTexture(ed, EW, EH, THREE.RGBAFormat);
equirect.colorSpace = THREE.SRGBColorSpace;
equirect.needsUpdate = true;
// a small scene to capture
const capture = new THREE.Scene();
capture.background = new THREE.Color(0x203040);
const colours = [0xff4040, 0x40ff40, 0x4040ff, 0xffff40, 0xff40ff, 0x40ffff];
const dirs = [[3, 0, 0], [-3, 0, 0], [0, 3, 0], [0, -3, 0], [0, 0, 3], [0, 0, -3]];
for (let i = 0; i < 6; i++) {
  const b = new THREE.Mesh(new THREE.BoxGeometry(1.5, 1.5, 1.5), new THREE.MeshBasicMaterial({ color: colours[i] }));
  b.position.set(...dirs[i]);
  capture.add(b);
}
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(45, 1280 / 720, 0.1, 100);
camera.position.set(0, 0, 13);
// PMREMGenerator needs an initialised backend
globalThis.ready = renderer.init().then(() => {
const pmrem = new THREE.PMREMGenerator(renderer);
const envs = [pmrem.fromCubemap(cube), pmrem.fromEquirectangular(equirect), pmrem.fromScene(capture, 0.04)];
scene.background = envs[0].texture;
const geo = new THREE.SphereGeometry(0.75, 48, 24);
const rough = [0, 0.25, 0.5, 0.75, 1];
for (let row = 0; row < 3; row++) for (let i = 0; i < 5; i++) {
  const m = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({ color: row === 2 ? 0xffffff : 0xffeedd,
    metalness: row === 1 ? 0.5 : 1, roughness: rough[i], envMap: envs[row].texture }));
  m.position.set((i - 2) * 1.9, (1 - row) * 1.9, 0);
  scene.add(m);
}
});
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  camera.position.set(Math.sin(t * 0.4) * 4, Math.sin(t * 0.3), 12);
  camera.lookAt(0, 0, 0);
  renderer.render(scene, camera);
};
