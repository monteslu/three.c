// t6-shadow-cover: a shadowed sun, one unshadowed point light and a hemisphere
// light, with scene.environment: Standard spheres on a ground. three.c has no
// program captured for exactly this light vector, so it draws with one
// captured for more point lights (the extra lights inert); the pixels must be
// r186's.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x182028, 1);
renderer.shadowMap.enabled = true;
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 100);
camera.position.set(0, 5, 11);
camera.lookAt(0, 0.5, 0);
// a cube environment (PMREM'd by r186 as scene.environment)
const S = 16, tints = [[200, 120, 90], [90, 160, 220], [230, 230, 240], [60, 70, 60], [160, 200, 140], [120, 100, 180]];
const faces = tints.map((c) => { const d = new Uint8Array(S * S * 4); for (let i = 0; i < S * S; i++) { d[i * 4] = c[0]; d[i * 4 + 1] = c[1]; d[i * 4 + 2] = c[2]; d[i * 4 + 3] = 255; } const t = new THREE.DataTexture(d, S, S, THREE.RGBAFormat); t.needsUpdate = true; return t; });
const env = new THREE.CubeTexture(faces);
env.format = THREE.RGBAFormat;
env.needsUpdate = true;
scene.environment = env;
const ground = new THREE.Mesh(new THREE.PlaneGeometry(16, 16), new THREE.MeshStandardMaterial({ color: 0x8090a0, roughness: 0.9 }));
ground.rotation.x = -Math.PI / 2;
ground.receiveShadow = true;
scene.add(ground);
const balls = [];
const geo = new THREE.SphereGeometry(0.8, 32, 16);
[0xe05040, 0x40a0e0, 0xe0c040].forEach((c, i) => {
  const m = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({ color: c, roughness: 0.3 + i * 0.2, metalness: i * 0.4 }));
  m.position.set((i - 1) * 2.5, 0.8, 0);
  m.castShadow = true;
  m.receiveShadow = true;
  scene.add(m);
  balls.push(m);
});
scene.add(new THREE.HemisphereLight(0x8090ff, 0x302010, 0.6));
const sun = new THREE.DirectionalLight(0xffffff, 1.5);
sun.position.set(4, 8, 3);
sun.castShadow = true;
sun.shadow.mapSize.set(1024, 1024);
sun.shadow.camera.left = -8; sun.shadow.camera.right = 8; sun.shadow.camera.top = 8; sun.shadow.camera.bottom = -8;
sun.shadow.camera.near = 1; sun.shadow.camera.far = 30;
sun.shadow.bias = -0.0005;
scene.add(sun);
const flash = new THREE.PointLight(0xffa040, 25, 0, 2);
scene.add(flash);
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  flash.position.set(Math.sin(t) * 3, 1.5, Math.cos(t) * 2 + 1);
  for (let i = 0; i < balls.length; i++) balls[i].position.y = 0.8 + Math.abs(Math.sin(t * 2 + i)) * 0.8;
  renderer.render(scene, camera);
};
