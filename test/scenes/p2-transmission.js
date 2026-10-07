// p2-transmission: MeshPhysicalMaterial transmission (r186's framebuffer
// copy, sampled at a roughness-dependent mip): glass blocks in front of a
// striped wall: clear, frosted (rough), thick tinted (attenuation), and a
// double-sided one (r186's back-face pass first). A directional light.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202830, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(45, 1280 / 720, 0.1, 100);
camera.position.set(0, 0.5, 9);
camera.lookAt(0, 0, 0);
// the wall: stripes to see through the glass
for (let i = 0; i < 12; i++) {
  const s = new THREE.Mesh(new THREE.BoxGeometry(0.6, 6, 0.2), new THREE.MeshStandardMaterial({ color: [0xe04040, 0xe0e040, 0x40c060, 0x4080e0][i % 4], roughness: 0.7 }));
  s.position.set(-6.6 + i * 1.2, 0, -2);
  scene.add(s);
}
const geo = new THREE.BoxGeometry(1.6, 1.6, 1.6);
const glass = [
  { color: 0xffffff, transmission: 1, roughness: 0.05, thickness: 0.5, ior: 1.5 },
  { color: 0xffffff, transmission: 1, roughness: 0.45, thickness: 0.5, ior: 1.5 },
  { color: 0xffffff, transmission: 1, roughness: 0.1, thickness: 2, ior: 1.4, attenuationColor: new THREE.Color(0x40c0ff), attenuationDistance: 1.5 },
  { color: 0xfff0e0, transmission: 0.9, roughness: 0.15, thickness: 1, ior: 1.6, side: THREE.DoubleSide },
];
const blocks = [];
glass.forEach((p, i) => {
  const m = new THREE.Mesh(geo, new THREE.MeshPhysicalMaterial({ metalness: 0, ...p }));
  m.position.set(-3.6 + i * 2.4, 0, 0);
  scene.add(m);
  blocks.push(m);
});
scene.add(new THREE.AmbientLight(0x606070, 1));
const sun = new THREE.DirectionalLight(0xffffff, 2);
sun.position.set(3, 5, 4);
scene.add(sun);
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  for (let i = 0; i < blocks.length; i++) blocks[i].rotation.set(t * 0.4 + i, t * 0.6, 0);
  renderer.render(scene, camera);
};
