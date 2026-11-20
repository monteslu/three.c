// c1-array-camera: ArrayCamera. Four cameras, each with its own viewport (a 2 x 2
// grid), in one renderer.render call: a wide view, a close one from the other
// side, one from above and a narrow one that sees only part of the scene.
// A shadowed sun, a point light, spheres that share a geometry and a material,
// boxes and a transparent sphere.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202830, 1);
renderer.shadowMap.enabled = true;
const scene = new THREE.Scene();
const ground = new THREE.Mesh(new THREE.PlaneGeometry(20, 20), new THREE.MeshStandardMaterial({ color: 0x607060, roughness: 0.9 }));
ground.rotation.x = -Math.PI / 2;
ground.receiveShadow = true;
scene.add(ground);
const sg = new THREE.SphereGeometry(0.6, 24, 12), sm = new THREE.MeshStandardMaterial({ color: 0xd06030, roughness: 0.5 });
for (let i = 0; i < 6; i++) {
  const s = new THREE.Mesh(sg, sm);
  s.position.set((i - 2.5) * 1.6, 0.6, -1 + (i % 2) * 2);
  s.castShadow = true;
  s.receiveShadow = true;
  scene.add(s);
}
const bg = new THREE.BoxGeometry(1, 1, 1);
[[-4, 0.5, 3, 0x4080d0], [4, 0.5, -3, 0xd0c040], [0, 0.5, 4, 0x60c080]].forEach(([x, y, z, c]) => {
  const b = new THREE.Mesh(bg, new THREE.MeshStandardMaterial({ color: c, roughness: 0.6 }));
  b.position.set(x, y, z);
  b.castShadow = true;
  scene.add(b);
});
const glass = new THREE.Mesh(new THREE.SphereGeometry(0.9, 24, 12), new THREE.MeshStandardMaterial({ color: 0x80c0ff, transparent: true, opacity: 0.5 }));
glass.position.set(0, 1.4, 1);
scene.add(glass);
scene.add(new THREE.AmbientLight(0x303840, 1));
const sun = new THREE.DirectionalLight(0xffffff, 2.5);
sun.position.set(5, 9, 4);
sun.castShadow = true;
sun.shadow.mapSize.set(1024, 1024);
sun.shadow.camera.left = -10; sun.shadow.camera.right = 10; sun.shadow.camera.top = 10; sun.shadow.camera.bottom = -10;
sun.shadow.camera.near = 1; sun.shadow.camera.far = 30;
sun.shadow.bias = -0.0005;
scene.add(sun);
const lamp = new THREE.PointLight(0xff8040, 30, 0, 2);
lamp.position.set(-3, 2, 2);
scene.add(lamp);
const W = 640, H = 360;
function cam(fov, px, py, pz, tx, ty, tz, vx, vy) {
  const c = new THREE.PerspectiveCamera(fov, W / H, 0.1, 100);
  c.position.set(px, py, pz);
  c.lookAt(tx, ty, tz);
  c.updateMatrixWorld();
  c.viewport = new THREE.Vector4(vx, vy, W, H);
  return c;
}
const cams = [cam(50, 0, 5, 11, 0, 0.5, 0, 0, 0), cam(40, -6, 2, -6, 0, 0.6, 0, W, 0), cam(60, 0.01, 14, 0, 0, 0, 0, 0, H), cam(20, 9, 3, 9, -4, 0.5, 3, W, H)];
const array = new THREE.ArrayCamera(cams);
globalThis.frame = function () { renderer.render(scene, array); };
