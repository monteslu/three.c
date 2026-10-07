// r9-physical-lights: punctual light falloff. The same room is drawn twice
// side by side: left with the default inverse-square decay (2), right with
// decay 1. Standard, Phong and Lambert materials, a point light with and
// without a cutoff distance, a spot light, a directional light, ambient and
// hemisphere light. Each half has its own scene and materials.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x080810, 1);
const camera = new THREE.PerspectiveCamera(55, 640 / 720, 0.1, 60);
camera.position.set(0, 2.2, 7);
camera.lookAt(0, 0.8, 0);
const floorGeo = new THREE.PlaneGeometry(10, 10);
const sphereGeo = new THREE.SphereGeometry(0.6, 32, 16);
const boxGeo = new THREE.BoxGeometry(1, 1, 1);
const scenes = [], movers = [];
for (let i = 0; i < 2; i++) {
  const s = new THREE.Scene(), decay = i ? 1 : 2;
  const floor = new THREE.Mesh(floorGeo, new THREE.MeshStandardMaterial({ color: 0x9a9a9a, roughness: 0.8 }));
  floor.rotation.x = -Math.PI / 2;
  s.add(floor);
  const a = new THREE.Mesh(sphereGeo, new THREE.MeshStandardMaterial({ color: 0xff8040, roughness: 0.4, metalness: 0.2 }));
  a.position.set(-1.6, 0.6, 0);
  s.add(a);
  const b = new THREE.Mesh(sphereGeo, new THREE.MeshPhongMaterial({ color: 0x40a0ff, shininess: 60 }));
  b.position.set(0, 0.6, 0.5);
  s.add(b);
  const c = new THREE.Mesh(boxGeo, new THREE.MeshLambertMaterial({ color: 0x80ff60 }));
  c.position.set(1.6, 0.5, 0);
  s.add(c);
  s.add(new THREE.AmbientLight(0x202020, 1));
  s.add(new THREE.HemisphereLight(0x8090ff, 0x302010, 0.4));
  const sun = new THREE.DirectionalLight(0xffffff, 0.8);
  sun.position.set(-3, 5, 2);
  s.add(sun);
  const p1 = new THREE.PointLight(0xffaa66, 6, 0, decay);
  p1.position.set(-1, 1.8, 1.5);
  s.add(p1);
  const p2 = new THREE.PointLight(0x66ccff, 8, 6, decay);
  p2.position.set(1.5, 1.2, 1.8);
  s.add(p2);
  const spot = new THREE.SpotLight(0xffffff, 20, 0, Math.PI / 6, 0.3, decay);
  spot.position.set(0, 4, 2);
  spot.target.position.set(0, 0, 0.5);
  s.add(spot);
  s.add(spot.target);
  scenes.push(s);
  movers.push(c);
}
let frameNo = 0;
globalThis.frame = function () {
  renderer.setScissorTest(true);
  frameNo++;
  const t = frameNo / 60;
  for (let i = 0; i < 2; i++) {
    movers[i].rotation.set(0, t, 0);
    renderer.setViewport(i * 640, 0, 640, 720);
    renderer.setScissor(i * 640, 0, 640, 720);
    renderer.render(scenes[i], camera);
  }
};
