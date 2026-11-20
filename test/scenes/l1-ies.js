// l1-ies: IESSpotLight. Three spot lights over a floor and a back wall: one
// with a symmetric profile (a single horizontal angle: a bright ring) that
// casts a shadow, one with a profile measured from 0 to 180 degrees of twist
// (lopsided, mirrored to the full circle), and a plain SpotLight beside them.
// The profiles are IES files (made for this test), read by IESLoader.
const IES_RING = `IESNA:LM-63-2002
[TEST] three.c l1-ies ring
[MANUFAC] none
TILT=NONE
1 1000 1 10 1 1 2 0 0 0
1 1 100
0 5 10 15 20 25 30 40 60 90
0
300 320 500 900 1000 700 260 80 10 0
`;
const IES_LOBES = `IESNA:LM-63-2002
[TEST] three.c l1-ies lobes
TILT=NONE
1 1000 2 7 5 1 2 0 0 0
1 0.5 100
0 10 20 30 45 60 90
0 45 90 135 180
1000 950 800 500 200 40 0
700 700 650 600 420 200 0
300 320 380 420 380 160 0
120 110 100 80 40 10 0
60 50 30 10 0 0 0
`;
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x05060a, 1);
renderer.shadowMap.enabled = true;
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 100);
camera.position.set(0, 5.5, 11);
camera.lookAt(0, 1.2, 0);
const loader = new THREE.IESLoader();
const ring = loader.parse(IES_RING), lobes = loader.parse(IES_LOBES);
const mat = new THREE.MeshStandardMaterial({ color: 0x9a9690, roughness: 0.85 });
const floor = new THREE.Mesh(new THREE.PlaneGeometry(22, 14), mat);
floor.rotation.x = -Math.PI / 2;
floor.receiveShadow = true;
scene.add(floor);
const wall = new THREE.Mesh(new THREE.PlaneGeometry(22, 8), mat);
wall.position.set(0, 4, -4);
scene.add(wall);
const box = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), new THREE.MeshStandardMaterial({ color: 0xc04030, roughness: 0.6 }));
box.position.set(-5.4, 0.5, 0.6);
box.castShadow = true;
scene.add(box);
scene.add(new THREE.AmbientLight(0x202430, 1));
function spot(light, x, map) {
  light.position.set(x, 5, 1.5);
  light.target.position.set(x, 0, -0.5);
  if (map) light.iesMap = map;
  scene.add(light);
  scene.add(light.target);
  return light;
}
const a = spot(new THREE.IESSpotLight(0xfff0d8, 260, 0, 1.0, 0.2, 2), -6, ring);
a.castShadow = true;
a.shadow.mapSize.set(1024, 1024);
a.shadow.camera.near = 0.5; a.shadow.camera.far = 20;
a.shadow.bias = -0.0005;
spot(new THREE.IESSpotLight(0xd8e8ff, 260, 0, 1.0, 0.2, 2), 0, lobes);
spot(new THREE.SpotLight(0xffe0c0, 160, 0, 0.5, 0.3, 2), 6, null);
globalThis.frame = function () { renderer.render(scene, camera); };
