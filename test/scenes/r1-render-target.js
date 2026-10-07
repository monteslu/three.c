// r1-render-target: an offscreen scene rendered every frame into four
// targets, each read back as a material map: a RenderTarget with a
// DepthTexture and mipmaps, a HalfFloatType target written in sRGB, a
// multisampled target (4 samples, resolved after the render), and a small
// FloatType target.
const renderer = new THREE.WebGPURenderer();
renderer.setSize(1280, 720);
renderer.setClearColor(0x202028, 1);

// the offscreen scene
const inner = new THREE.Scene();
inner.background = new THREE.Color(0x335577);
const innerCam = new THREE.PerspectiveCamera(60, 1, 2, 12);
innerCam.position.set(0, 0, 6);
const knot = new THREE.Mesh(new THREE.TorusKnotGeometry(1.2, 0.4, 96, 12),
  new THREE.MeshPhongMaterial({ color: 0xffaa33, shininess: 60 }));
inner.add(knot);
const box = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), new THREE.MeshLambertMaterial({ color: 0x44ff88 }));
box.position.set(1.8, 1.2, -1);
inner.add(box);
inner.add(new THREE.AmbientLight(0x404040));
const innerSun = new THREE.DirectionalLight(0xffffff, 1);
innerSun.position.set(2, 3, 4);
inner.add(innerSun);

const rtA = new THREE.RenderTarget(256, 256, { minFilter: THREE.LinearMipmapLinearFilter, generateMipmaps: true });
rtA.depthTexture = new THREE.DepthTexture(256, 256);
const rtB = new THREE.RenderTarget(256, 256, { type: THREE.HalfFloatType });
rtB.texture.colorSpace = THREE.SRGBColorSpace;
const rtC = new THREE.RenderTarget(256, 256, { samples: 4 });
const rtD = new THREE.RenderTarget(64, 64, { type: THREE.FloatType });

// the main scene: one quad per target, and a lit cube wearing target A
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, 1280 / 720, 0.1, 100);
camera.position.set(0, 0, 10);
const quad = new THREE.PlaneGeometry(2.6, 2.6);
const maps = [rtA.texture, rtA.depthTexture, rtB.texture, rtC.texture];
for (let i = 0; i < 4; i++) {
  const m = new THREE.Mesh(quad, new THREE.MeshBasicMaterial({ map: maps[i] }));
  m.position.set(-4.5 + i * 3, 1.6, 0);
  scene.add(m);
}
const cube = new THREE.Mesh(new THREE.BoxGeometry(1.4, 1.4, 1.4),
  new THREE.MeshStandardMaterial({ map: rtA.texture, roughness: 0.5 }));
cube.position.set(-1.5, -2, 0);
scene.add(cube);
const small = new THREE.Mesh(new THREE.PlaneGeometry(0.5, 0.5), new THREE.MeshBasicMaterial({ map: rtA.texture }));
const floaty = new THREE.Mesh(new THREE.PlaneGeometry(1.6, 1.6), new THREE.MeshBasicMaterial({ map: rtD.texture }));
floaty.position.set(4, -2, 0);
scene.add(floaty);
small.position.set(1.5, -2, 0);
scene.add(small);
scene.add(new THREE.AmbientLight(0x606060));
const sun = new THREE.DirectionalLight(0xffffff, 0.8);
sun.position.set(-2, 3, 5);
scene.add(sun);

let frameNo = 0;
globalThis.frame = function () {
  frameNo++;
  const t = frameNo / 60;
  knot.rotation.set(t * 0.7, t * 1.1, 0);
  box.rotation.set(t, t * 0.5, 0);
  cube.rotation.set(t * 0.4, t * 0.6, 0);
  renderer.setRenderTarget(rtA);
  renderer.render(inner, innerCam);
  renderer.setRenderTarget(rtB);
  renderer.render(inner, innerCam);
  renderer.setRenderTarget(rtC);
  renderer.render(inner, innerCam);
  renderer.setRenderTarget(rtD);
  renderer.render(inner, innerCam);
  renderer.setRenderTarget(null);
  renderer.render(scene, camera);
};
