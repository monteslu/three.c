// t2-fog: linear Fog and FogExp2 over a field of lit and unlit boxes. The
// left half of the frame uses Fog, the right FogExp2, via two scissored renders.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x8899aa, 1);
renderer.autoClear = false;
const sceneA = new THREE.Scene(), sceneB = new THREE.Scene();
sceneA.fog = new THREE.Fog(0x8899aa, 5, 30);
sceneB.fog = new THREE.FogExp2(0x8899aa, 0.06);
const camera = new THREE.PerspectiveCamera(60, 640 / 720, 0.1, 100);
camera.position.set(0, 2, 6);
camera.lookAt(0, 0, -10);
const geo = new THREE.BoxGeometry(1, 1, 1);
const mats = [new THREE.MeshBasicMaterial({ color: 0xff8040 }), new THREE.MeshLambertMaterial({ color: 0x40c0ff }),
  new THREE.MeshPhongMaterial({ color: 0x80ff60 }), new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.5 })];
for (const s of [sceneA, sceneB]) {
  for (let i = 0; i < 40; i++) {
    const m = new THREE.Mesh(geo, mats[i % 4]);
    m.position.set((i % 5) * 2 - 4, 0, -Math.floor(i / 5) * 4);
    m.rotation.y = i * 0.3;
    s.add(m);
  }
  s.add(new THREE.AmbientLight(0x404040));
  const d = new THREE.DirectionalLight(0xffffff, 0.8);
  d.position.set(2, 4, 3);
  s.add(d);
}
globalThis.frame = function () {
  renderer.setScissorTest(false);
  renderer.clear();
  renderer.setScissorTest(true);
  renderer.setViewport(0, 0, 640, 720); renderer.setScissor(0, 0, 640, 720);
  renderer.render(sceneA, camera);
  renderer.setViewport(640, 0, 640, 720); renderer.setScissor(640, 0, 640, 720);
  renderer.render(sceneB, camera);
};
