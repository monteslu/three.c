// r12-depth-only: a render target with no colour attachment (count: 0, r187's
// depth-only targets): a turning box drawn into its DepthTexture, shown on a plane.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202028, 1);
const inner = new THREE.Scene();
const box = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), new THREE.MeshBasicMaterial({ color: 0xff0000 }));
inner.add(box);
const innerCam = new THREE.PerspectiveCamera(50, 1, 1, 6);
innerCam.position.set(0, 0, 3);
const rt = new THREE.RenderTarget(256, 256, { count: 0 });
rt.depthTexture = new THREE.DepthTexture(256, 256);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 50);
camera.position.set(0, 0, 5);
scene.add(new THREE.Mesh(new THREE.PlaneGeometry(4, 4), new THREE.MeshBasicMaterial({ map: rt.depthTexture })));
let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  box.rotation.set(frameNo / 60, frameNo / 40, 0);
  renderer.setRenderTarget(rt);
  renderer.render(inner, innerCam);
  renderer.setRenderTarget(null);
  renderer.render(scene, camera);
};
