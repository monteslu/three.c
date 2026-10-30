// z2-reversed-far: reversedDepthBuffer at a far / near of 1e7, walls just inside far (a z row rounded from w - z clips them): a floor, a box and a ball
// under a directional and a point light, both casting (r187's programs mirror
// the shadow compare), and two walls 100 km apart 9900 km out with a far plane
// of 10000 km: the far wall sorts last, so wherever forward depth rounds the two
// together it draws over the near one (red); reversed depth keeps them apart (green).
const renderer = new THREE.WebGPURenderer({ reversedDepthBuffer: true });
renderer.setSize(1280, 720);
renderer.setClearColor(0x101820, 1);
renderer.shadowMap.enabled = true;
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 1, 10000000);
camera.position.set(0, 3, 9);
camera.lookAt(0, 1, 0);
const scene = new THREE.Scene();
const floor = new THREE.Mesh(new THREE.PlaneGeometry(30, 30), new THREE.MeshStandardMaterial({ color: 0x808890 }));
floor.rotation.x = -Math.PI / 2;
floor.receiveShadow = true;
scene.add(floor);
const box = new THREE.Mesh(new THREE.BoxGeometry(1.5, 1.5, 1.5), new THREE.MeshStandardMaterial({ color: 0xd06030 }));
box.position.set(-2, 1, 0);
box.castShadow = box.receiveShadow = true;
scene.add(box);
const ball = new THREE.Mesh(new THREE.SphereGeometry(0.8, 32, 16), new THREE.MeshPhongMaterial({ color: 0x40a0e0 }));
ball.position.set(2, 1, 0.5);
ball.castShadow = ball.receiveShadow = true;
scene.add(ball);
const wall = new THREE.PlaneGeometry(6000000, 3000000);
const w1 = new THREE.Mesh(wall, new THREE.MeshBasicMaterial({ color: 0x40c040 }));
w1.position.set(0, 0, -9800000);
scene.add(w1);
const w0 = new THREE.Mesh(wall, new THREE.MeshBasicMaterial({ color: 0xc04040 }));
w0.position.set(0, 500000, -9900000);
w0.renderOrder = 1;   // drawn after the near wall, whatever the sort
scene.add(w0);
scene.add(new THREE.AmbientLight(0x303040, 1));
const sun = new THREE.DirectionalLight(0xffffff, 2);
sun.position.set(4, 8, 3);
sun.castShadow = true;
sun.shadow.mapSize.set(1024, 1024);
scene.add(sun);
const pt = new THREE.PointLight(0xffc080, 30, 0, 2);
pt.position.set(0, 4, 2);
pt.castShadow = true;
pt.shadow.mapSize.set(512, 512);
scene.add(pt);
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  box.rotation.y = frameNo / 60;
  renderer.render(scene, camera);
};
