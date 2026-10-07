// r10-light-map: material.lightMap + lightMapIntensity. Basic, Lambert, Phong
// and Standard materials with lightmaps (a generated DataTexture, linear and
// sRGB, one with an offset / repeat, one beside an aoMap with a transform of
// its own), drawn twice side by side: left with a directional light, right
// lit by the lightmaps and the ambient light alone.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x080810, 1);
const camera = new THREE.PerspectiveCamera(55, 640 / 720, 0.1, 60);
camera.position.set(0, 3.2, 7);
camera.lookAt(0, 0.6, 0);
function lightTexture(seed) {
  const n = 32, data = new Uint8Array(n * n * 4);
  for (let y = 0; y < n; y++)
    for (let x = 0; x < n; x++) {
      const i = (y * n + x) * 4;
      data[i] = (x * 8 + seed * 40) & 255;
      data[i + 1] = (y * 8) & 255;
      data[i + 2] = ((x ^ y) * 8 + seed * 16) & 255;
      data[i + 3] = 255;
    }
  const t = new THREE.DataTexture(data, n, n, THREE.RGBAFormat);
  t.magFilter = THREE.LinearFilter;
  t.minFilter = THREE.LinearFilter;
  t.needsUpdate = true;
  return t;
}
const lmA = lightTexture(0), lmB = lightTexture(1), lmC = lightTexture(2), ao = lightTexture(3);
lmB.colorSpace = THREE.SRGBColorSpace;
lmC.offset.set(0.25, 0.1);
lmC.repeat.set(2, 1.5);
lmC.wrapS = lmC.wrapT = THREE.RepeatWrapping;
ao.repeat.set(0.5, 0.5);
const floorGeo = new THREE.PlaneGeometry(10, 10);
const boxGeo = new THREE.BoxGeometry(1.2, 1.2, 1.2);
const sphereGeo = new THREE.SphereGeometry(0.7, 32, 16);
const scenes = [], movers = [];
for (let i = 0; i < 2; i++) {
  const s = new THREE.Scene();
  const floor = new THREE.Mesh(floorGeo, new THREE.MeshStandardMaterial({ color: 0x9a9a9a, roughness: 0.8, lightMap: lmA, lightMapIntensity: 0.6 }));
  floor.rotation.x = -Math.PI / 2;
  s.add(floor);
  const a = new THREE.Mesh(sphereGeo, new THREE.MeshPhongMaterial({ color: 0xff8040, shininess: 40, lightMap: lmB }));
  a.position.set(-2.4, 0.7, 0);
  s.add(a);
  const b = new THREE.Mesh(boxGeo, new THREE.MeshLambertMaterial({ color: 0x80ff60, lightMap: lmC, lightMapIntensity: 1.5 }));
  b.position.set(-0.8, 0.6, 0.6);
  s.add(b);
  const c = new THREE.Mesh(boxGeo, new THREE.MeshBasicMaterial({ color: 0x8080ff, lightMap: lmA, lightMapIntensity: 0.8 }));
  c.position.set(0.8, 0.6, 0.6);
  s.add(c);
  const d = new THREE.Mesh(sphereGeo, new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.5, metalness: 0.1, lightMap: lmC, aoMap: ao }));
  d.position.set(2.4, 0.7, 0);
  s.add(d);
  s.add(new THREE.AmbientLight(0x101010, 1));
  if (i === 0) {
    const sun = new THREE.DirectionalLight(0xffffff, 0.5);
    sun.position.set(-3, 5, 2);
    s.add(sun);
  }
  scenes.push(s);
  movers.push(b, c);
}
let frameNo = 0;
globalThis.frame = function () {
  renderer.setScissorTest(true);
  frameNo++;
  const t = frameNo / 60;
  for (let i = 0; i < 2; i++) {
    movers[i * 2].rotation.set(0, t, 0);
    movers[i * 2 + 1].rotation.set(t * 0.5, 0, 0);
    renderer.setViewport(i * 640, 0, 640, 720);
    renderer.setScissor(i * 640, 0, 640, 720);
    renderer.render(scenes[i], camera);
  }
};
