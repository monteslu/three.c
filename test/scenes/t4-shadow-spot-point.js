// t4-shadow-spot-point: a spot light and a point light both casting shadows
// (PCFSoft), plus an unshadowed directional fill. Exercises the spot shadow
// camera and the point light's six-face cube-in-2D shadow atlas.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x101418, 1);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(55, 1280 / 720, 0.1, 100);
camera.position.set(0, 8, 13);
camera.lookAt(0, 0, 0);
const room = new THREE.MeshPhongMaterial({ color: 0xa0a0a0, shininess: 10 });
const floor = new THREE.Mesh(new THREE.PlaneGeometry(24, 24), room);
floor.rotation.x = -Math.PI / 2;
floor.receiveShadow = true;
scene.add(floor);
const wall = new THREE.Mesh(new THREE.PlaneGeometry(24, 10), room);
wall.position.set(0, 5, -6);
wall.receiveShadow = true;
scene.add(wall);
const casters = [];
const geo = [new THREE.BoxGeometry(1, 2, 1), new THREE.CylinderGeometry(0.6, 0.6, 1.6, 24), new THREE.SphereGeometry(0.7, 24, 12)];
const mat = [new THREE.MeshStandardMaterial({ color: 0xd06040 }), new THREE.MeshLambertMaterial({ color: 0x50a0d0 }), new THREE.MeshPhongMaterial({ color: 0xd0d050 })];
for (let i = 0; i < 6; i++) {
  const m = new THREE.Mesh(geo[i % 3], mat[i % 3]);
  m.position.set(i * 2.2 - 5.5, 1, (i % 2) * 2 - 1);
  m.castShadow = true;
  m.receiveShadow = true;
  scene.add(m);
  casters.push(m);
}
const spot = new THREE.SpotLight(0xffe0c0, 1.2, 40, Math.PI / 6, 0.3, 1);
spot.position.set(-6, 9, 6);
spot.target.position.set(-2, 0, 0);
scene.add(spot.target);
spot.castShadow = true;
spot.shadow.mapSize.set(1024, 1024);
spot.shadow.bias = -0.0005;
scene.add(spot);
const pt = new THREE.PointLight(0x80c0ff, 1.0, 30, 1);
pt.position.set(3, 4, 2);
pt.castShadow = true;
pt.shadow.bias = -0.001;
scene.add(pt);
scene.add(new THREE.AmbientLight(0x202020));
const fill = new THREE.DirectionalLight(0x404050, 0.4);
fill.position.set(0, 5, 10);
scene.add(fill);
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  for (let i = 0; i < casters.length; i++) casters[i].rotation.y = t + i;
  pt.position.x = 3 + Math.sin(t) * 2;
  renderer.render(scene, camera);
};
