// r11-pmrem-probe: PMREMGenerator.fromScene's options (size, position,
// renderTarget) as a reflection probe refreshed every frame: a ring of
// coloured boxes round a point away from the origin, one of them orbiting,
// captured from that point at 64 into the same target each frame, read by
// a row of mirror-to-rough spheres
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x000000, 1);
const P = new THREE.Vector3(6, 2, -4);
const capture = new THREE.Scene();
// no background colour: r187 draws its solid-colour box round the origin, not
// round options.position (an r187 defect three.c does not copy), so the probe
// sees the clear colour behind the boxes on both
const colours = [0xff4040, 0x40ff40, 0x4040ff, 0xffff40, 0xff40ff, 0x40ffff];
const dirs = [[3, 0, 0], [-3, 0, 0], [0, 3, 0], [0, -3, 0], [0, 0, 3], [0, 0, -3]];
for (let i = 0; i < 6; i++) {
  const b = new THREE.Mesh(new THREE.BoxGeometry(1.5, 1.5, 1.5), new THREE.MeshBasicMaterial({ color: colours[i] }));
  b.position.set(P.x + dirs[i][0], P.y + dirs[i][1], P.z + dirs[i][2]);
  capture.add(b);
}
const orbiter = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), new THREE.MeshBasicMaterial({ color: 0xffffff }));
capture.add(orbiter);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(45, 1280 / 720, 0.1, 100);
camera.position.set(0, 0, 9);
camera.lookAt(0, 0, 0);
let pmrem = null, probe = null;
const spheres = [];
globalThis.ready = renderer.init().then(() => {
  pmrem = new THREE.PMREMGenerator(renderer);
  const geo = new THREE.SphereGeometry(0.75, 48, 24);
  const rough = [0, 0.2, 0.4, 0.7, 1];
  for (let i = 0; i < 5; i++) {
    const m = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({ color: 0xffffff, metalness: 1, roughness: rough[i] }));
    m.position.set((i - 2) * 1.9, 0, 0);
    scene.add(m);
    spheres.push(m);
  }
});
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  orbiter.position.set(P.x + Math.cos(t * 2) * 2, P.y + Math.sin(t * 3), P.z + Math.sin(t * 2) * 2);
  probe = pmrem.fromScene(capture, 0, 0.1, 100, { size: 64, position: P, renderTarget: probe });
  for (const s of spheres) s.material.envMap = probe.texture;
  renderer.render(scene, camera);
};
