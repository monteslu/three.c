// r2-tone-mapping: one over-lit scene drawn six times, in six viewports, with
// renderer.toneMapping None / Linear / Reinhard / Cineon / ACESFilmic and
// ACESFilmic into an sRGB output, at different toneMappingExposure values.
// A material with toneMapped: false sits in every viewport. Each viewport
// has its own scene and materials.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x101018, 1);
const LIN = THREE.LinearSRGBColorSpace, SRGB = THREE.SRGBColorSpace;
const modes = [
  [THREE.NoToneMapping, 1, LIN],
  [THREE.LinearToneMapping, 0.6, LIN],
  [THREE.ReinhardToneMapping, 1.5, LIN],
  [THREE.CineonToneMapping, 1, LIN],
  [THREE.ACESFilmicToneMapping, 0.8, LIN],
  [THREE.ACESFilmicToneMapping, 1.2, SRGB],
];
const camera = new THREE.PerspectiveCamera(50, 426 / 360, 0.1, 50);
camera.position.set(0, 1, 5);
camera.lookAt(0, 0, 0);
const sphereGeo = new THREE.SphereGeometry(0.9, 32, 16);
const torusGeo = new THREE.TorusGeometry(0.5, 0.2, 16, 40);
const planeGeo = new THREE.PlaneGeometry(1.2, 1.2);
const scenes = [], spin = [];
for (let i = 0; i < 6; i++) {
  const s = new THREE.Scene();
  const sphere = new THREE.Mesh(sphereGeo, new THREE.MeshStandardMaterial({ color: 0xff8844, roughness: 0.35, metalness: 0.1 }));
  sphere.position.x = -0.9;
  s.add(sphere);
  const torus = new THREE.Mesh(torusGeo, new THREE.MeshPhongMaterial({ color: 0x66aaff, shininess: 80, emissive: 0x220000 }));
  torus.position.set(1.1, 0.3, 0);
  s.add(torus);
  const flat = new THREE.Mesh(planeGeo, new THREE.MeshBasicMaterial({ color: 0xffcc66, toneMapped: false }));
  flat.position.set(1.1, -0.9, 0.5);
  s.add(flat);
  const bright = new THREE.Mesh(planeGeo, new THREE.MeshBasicMaterial({ color: 0xffcc66 }));
  bright.position.set(-1.6, -1.1, 0.5);
  s.add(bright);
  s.add(new THREE.AmbientLight(0x404040, 1));
  const sun = new THREE.DirectionalLight(0xffffff, 3);
  sun.position.set(2, 3, 4);
  s.add(sun);
  const fill = new THREE.PointLight(0x88aaff, 2, 20, 2);
  fill.position.set(-3, 1, 2);
  s.add(fill);
  scenes.push(s);
  spin.push(torus);
}
let frameNo = 0;
globalThis.frame = function () {
  renderer.setScissorTest(true);
  frameNo++;
  const t = frameNo / 60;
  for (let i = 0; i < 6; i++) {
    spin[i].rotation.set(t, t * 0.7, 0);
    const x = (i % 3) * 426 + 1, y = i < 3 ? 360 : 0;
    renderer.setViewport(x, y, 426, 360);
    renderer.setScissor(x, y, 426, 360);
    renderer.toneMapping = modes[i][0];
    renderer.toneMappingExposure = modes[i][1];
    renderer.outputColorSpace = modes[i][2];
    renderer.render(scenes[i], camera);
  }
};
