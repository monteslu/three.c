// r6-sprite-points: Sprite + SpriteMaterial (map, colour, rotation, center,
// sizeAttenuation on and off) and Points + PointsMaterial (vertex colours,
// a map with alphaTest, size with and without attenuation).
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x101820, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(55, 1280 / 720, 0.1, 100);
camera.position.set(0, 1, 14);
camera.lookAt(0, 0, 0);
let seed = 12345;
const rand = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647; };

// a soft disc
const N = 64, d = new Uint8Array(N * N * 4);
for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
  const dx = (x + 0.5) / N - 0.5, dy = (y + 0.5) / N - 0.5, r = Math.sqrt(dx * dx + dy * dy) * 2;
  const i = (y * N + x) * 4, a = Math.max(0, Math.min(1, (1 - r) * 3));
  d[i] = 255; d[i + 1] = Math.round(255 - 120 * r); d[i + 2] = Math.round(200 * (1 - r)); d[i + 3] = Math.round(a * 255);
}
const disc = new THREE.DataTexture(d, N, N, THREE.RGBAFormat);
disc.magFilter = disc.minFilter = THREE.LinearFilter;
disc.needsUpdate = true;

const sprites = [];
const specs = [
  [-5, 2, 0x00ffff, 1.5, 0.0, 0.5, 0.5, true],
  [-2.5, 2, 0xffffff, 2.0, 0.6, 0.5, 0.5, true],
  [0, 2, 0xff8080, 1.2, 0.0, 0.0, 0.0, true],
  [2.5, 2, 0x80ff80, 1.8, 1.2, 1.0, 1.0, true],
  [5, 2, 0xffff60, 0.08, 0.3, 0.5, 0.5, false],
];
for (const [x, y, c, s, rot, cx, cy, att] of specs) {
  const m = new THREE.SpriteMaterial({ map: disc, color: c, rotation: rot, sizeAttenuation: att });
  const sp = new THREE.Sprite(m);
  sp.position.set(x, y, 0);
  sp.scale.set(s, s * (att ? 0.75 : 1), 1);
  sp.center.set(cx, cy);
  scene.add(sp);
  sprites.push(sp);
}
// a sprite without a map, behind a box
const plain = new THREE.Sprite(new THREE.SpriteMaterial({ color: 0x8844ff, opacity: 0.7 }));
plain.position.set(-4, -2.5, -1);
plain.scale.set(2.5, 2.5, 1);
scene.add(plain);
const box = new THREE.Mesh(new THREE.BoxGeometry(1.5, 1.5, 1.5), new THREE.MeshLambertMaterial({ color: 0xcc8844 }));
box.position.set(-4, -2.5, 0.5);
scene.add(box);

// a point cloud with vertex colours and the disc, alpha tested
const n = 1500, pos = new Float32Array(n * 3), col = new Float32Array(n * 3);
for (let i = 0; i < n; i++) {
  const u = rand() * 2 - 1, t = rand() * Math.PI * 2, r = 2 + rand() * 0.4, q = Math.sqrt(1 - u * u);
  pos[i * 3] = r * q * Math.cos(t); pos[i * 3 + 1] = r * u; pos[i * 3 + 2] = r * q * Math.sin(t);
  col[i * 3] = 0.5 + 0.5 * u; col[i * 3 + 1] = rand(); col[i * 3 + 2] = 1 - 0.5 * rand();
}
const cloudGeo = new THREE.BufferGeometry();
cloudGeo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
cloudGeo.setAttribute('color', new THREE.BufferAttribute(col, 3));
const cloud = new THREE.Points(cloudGeo, new THREE.PointsMaterial({ size: 0.35, map: disc, vertexColors: true, alphaTest: 0.5 }));
cloud.position.set(1.5, -2.2, 0);
scene.add(cloud);
// fixed-size points in a line, no attenuation
const m2 = 40, pos2 = new Float32Array(m2 * 3);
for (let i = 0; i < m2; i++) { pos2[i * 3] = 5 + Math.cos(i * 0.4) * 1.2; pos2[i * 3 + 1] = -3.5 + i * 0.12; pos2[i * 3 + 2] = Math.sin(i * 0.4) * 1.2; }
const lineGeo = new THREE.BufferGeometry();
lineGeo.setAttribute('position', new THREE.BufferAttribute(pos2, 3));
const dots = new THREE.Points(lineGeo, new THREE.PointsMaterial({ color: 0xff60c0, size: 6, sizeAttenuation: false }));
scene.add(dots);
scene.add(new THREE.AmbientLight(0x404040));
const sun = new THREE.DirectionalLight(0xffffff, 0.8);
sun.position.set(2, 3, 4);
scene.add(sun);

let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  cloud.rotation.set(t * 0.3, t * 0.5, 0);
  dots.rotation.y = t * 0.4;
  sprites[1].material.rotation = 0.6 + t;
  sprites[0].position.y = 2 + Math.sin(t * 2) * 0.5;
  box.rotation.set(t * 0.5, t * 0.3, 0);
  renderer.render(scene, camera);
};
