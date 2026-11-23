/* t3_renderer_render_opaque then t3_renderer_render_transparent: the picture one t3_renderer_render draws, with
 * room for an embedder's pass between them.
 *
 * A scene with a background colour, an opaque box, a normal-blended plane and an additive plane over it is drawn
 * three ways: whole (A); opaque then transparent (B, which must equal A to the byte); and opaque, then an embedder's
 * pass (a render of another scene, autoClear off: a blue quad over the left half), then transparent (C). In C the
 * blue quad must stay where nothing transparent covers it and the transparent planes must blend over it, so the
 * transparent render cleared nothing, drew no background and drew no opaque mesh. A transparent render of a scene
 * with nothing transparent draws nothing (no draw calls). The control (T3_TEST_CONTROL=1) draws the second step with
 * t3_renderer_render, which clears and draws everything again, and must fail.
 *
 * GLES (built against build/native/libthree.a, EGL surfaceless):
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_render_transparent.c \
 *     build/native/libthree.a -lEGL -lGLESv2 -lz -lm
 * WebGPU (-DT3_WGPU, against build/native-wgpu/libthree.a and Dawn):
 *   cc -std=c99 -O2 -DT3_WGPU -I include -I src -I <dawn>/include test/test_render_transparent.c \
 *     build/native-wgpu/libthree.a -L <dawn>/lib -lwebgpu_dawn -Wl,-rpath,<dawn>/lib -lEGL -lGLESv2 -lz -lm
 *
 * Exits 0 on success, 77 when no WebGPU adapter is available. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "three.h"

#define W 32
#define H 32

#ifdef T3_WGPU
#include <webgpu/webgpu.h>
static WGPUInstance inst;
static void adapter_cb(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUAdapter *)u1 = st == WGPURequestAdapterStatus_Success ? a : NULL; *(int *)u2 = 1;
}
static void device_cb(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUDevice *)u1 = st == WGPURequestDeviceStatus_Success ? d : NULL; *(int *)u2 = 1;
}
static int wgpu_errors;
static void error_cb(WGPUDevice const *d, WGPUErrorType t, WGPUStringView m, void *u1, void *u2) {
  (void)d; (void)u1; (void)u2;
  if (wgpu_errors++ < 8) fprintf(stderr, "webgpu error %d: %.*s\n", (int)t, (int)m.length, m.data);
}
static WGPUDevice dev;
static WGPUQueue queue;
static int setup(void) {
  inst = wgpuCreateInstance(NULL);
  WGPUAdapter ad = NULL; int done = 0;
  WGPURequestAdapterOptions ao = { .powerPreference = WGPUPowerPreference_LowPower, .featureLevel = WGPUFeatureLevel_Compatibility };
  wgpuInstanceRequestAdapter(inst, &ao, (WGPURequestAdapterCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = adapter_cb, .userdata1 = &ad, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!ad) return 77;
  WGPUDeviceDescriptor dd = { .uncapturedErrorCallbackInfo = { .callback = error_cb } };
  done = 0;
  wgpuAdapterRequestDevice(ad, &dd, (WGPURequestDeviceCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = device_cb, .userdata1 = &dev, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!dev) return 77;
  queue = wgpuDeviceGetQueue(dev);
  return 0;
}
static t3_renderer *renderer(void) {
  t3_renderer *r = t3_renderer_new(W, H);
  t3_renderer_use_wgpu(r, dev, queue, WGPUTextureFormat_RGBA8Unorm);
  return r;
}
static void read_frame(t3_renderer *r, uint8_t *px) { t3_renderer_wgpu_read(r, px); }
#define BACKEND "webgpu"
#else
#include <EGL/egl.h>
#include <GLES3/gl3.h>
static int setup(void) {
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(d, NULL, NULL)) return fprintf(stderr, "eglInitialize failed\n"), 2;
  EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
  EGLConfig cfg;
  EGLint n = 0;
  if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) return fprintf(stderr, "no config\n"), 2;
  EGLint sa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
  EGLSurface surf = eglCreatePbufferSurface(d, cfg, sa);
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint xa[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xa);
  if (!ctx || !eglMakeCurrent(d, surf, surf, ctx)) return fprintf(stderr, "no context\n"), 2;
  return 0;
}
static t3_renderer *renderer(void) { return t3_renderer_new(W, H); }
static void read_frame(t3_renderer *r, uint8_t *px) { (void)r; glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); }
#define BACKEND "gles"
#endif

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL (" BACKEND "): "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static t3_renderer *r;
static void finish(uint8_t *px) {
#ifdef T3_WGPU
  t3_renderer_wgpu_submit(r);
#endif
  read_frame(r, px);
}
static const uint8_t *at(const uint8_t *px, int x, int y) { return px + (y * W + x) * 4; }

int main(void) {
  int st = setup();
  if (st == 77) { puts("test_render_transparent (" BACKEND "): no WebGPU adapter, skipped"); return 77; }
  if (st) return st;
  bool control = getenv("T3_TEST_CONTROL") != NULL;
  r = renderer();
  t3_renderer_set_clear_color(r, 0x000000, 1);
#ifndef T3_WGPU
  /* (the embedder's pass draws into the linear target the two halves share, as an embedder's target would hold it) */
  t3_renderer_set_exact_output(r, true);
#endif
  t3_camera *cam = t3_orthographic_camera_new(-1, 1, 1, -1, 0.1f, 10);
  t3_object_set_position(cam, 0, 0, 5);

  t3_scene *scene = t3_scene_new();
  scene->has_background = true;
  scene->background = t3_color_hex(0x808080);
  /* an opaque box over the lower half, a normal-blended plane over the right half, an additive plane over the top */
  t3_mesh *box = t3_mesh_new(t3_box_geometry_new(2, 1, 0.5f, 1, 1, 1), t3_mesh_basic_material_new(0xc02020));
  t3_object_set_position(box, 0, -0.5f, -1);
  t3_object_add(&scene->base, box);
  t3_material *mn = t3_mesh_basic_material_new(0x20c020);
  mn->transparent = true;
  mn->opacity = 0.5f;
  t3_mesh *pn = t3_mesh_new(t3_plane_geometry_new(1, 2, 1, 1), mn);
  t3_object_set_position(pn, 0.5f, 0, 0);
  t3_object_add(&scene->base, pn);
  t3_material *ma = t3_mesh_basic_material_new(0x2020c0);
  ma->transparent = true;
  ma->blending = T3_ADDITIVE_BLENDING;
  t3_mesh *pa = t3_mesh_new(t3_plane_geometry_new(2, 1, 1, 1), ma);
  t3_object_set_position(pa, 0, 0.5f, 1);
  t3_object_add(&scene->base, pa);

  /* the embedder's pass: an opaque blue quad over the left half, in a scene of its own */
  t3_scene *mark = t3_scene_new();
  t3_mesh *q = t3_mesh_new(t3_plane_geometry_new(1, 2, 1, 1), t3_mesh_basic_material_new(0x0000ff));
  t3_object_set_position(q, -0.5f, 0, 2);
  t3_object_add(&mark->base, q);

  static uint8_t A[W * H * 4], B[W * H * 4], C[W * H * 4], D[W * H * 4];
  t3_renderer_render(r, scene, cam);
  unsigned calls_a = t3_renderer_info(r)->calls;
  finish(A);
  t3_renderer_render_opaque(r, scene, cam);
  unsigned calls_o = t3_renderer_info(r)->calls;
  if (control) t3_renderer_render(r, scene, cam);
  else t3_renderer_render_transparent(r, scene, cam);
  unsigned calls_t = t3_renderer_info(r)->calls;
  finish(B);
  /* (each render counts its output pass) */
  printf("draws: whole %u, opaque %u, transparent %u\n", calls_a, calls_o, calls_t);
  CHECK(calls_o + calls_t == calls_a + 1, "opaque %u + transparent %u draws, whole render %u (+ one output pass)", calls_o, calls_t, calls_a);
  int diff = 0;
  for (int i = 0; i < W * H * 4; i++) diff += A[i] != B[i];
  CHECK(diff == 0, "opaque then transparent differs from one render in %d bytes", diff);

  t3_renderer_render_opaque(r, scene, cam);
  bool ac = t3_renderer_get_auto_clear(r);
  t3_renderer_set_auto_clear(r, false);
  t3_renderer_render(r, mark, cam);
  t3_renderer_set_auto_clear(r, ac);
  if (control) t3_renderer_render(r, scene, cam);
  else t3_renderer_render_transparent(r, scene, cam);
  finish(C);
  /* lower left: the blue quad over the box (nothing transparent there) */
  const uint8_t *ll = at(C, W / 4, H / 4);
  CHECK(ll[2] > 200 && ll[0] < 40 && ll[1] < 40, "lower left %d %d %d: the embedder's quad was drawn over", ll[0], ll[1], ll[2]);
  /* upper left: the additive plane over the blue quad (blue, brighter than none) */
  const uint8_t *ul = at(C, W / 4, 3 * H / 4), *ula = at(A, W / 4, 3 * H / 4);
  CHECK(ul[2] > 200 && ul[0] < 100 && ula[0] > 100, "upper left %d %d %d (whole render %d %d %d): the additive plane not over the quad", ul[0], ul[1], ul[2], ula[0], ula[1], ula[2]);
  /* right half: as the whole render (the embedder drew nothing there) */
  const uint8_t *rr = at(C, 3 * W / 4, H / 4), *ra = at(A, 3 * W / 4, H / 4);
  CHECK(abs(rr[0] - ra[0]) <= 1 && abs(rr[1] - ra[1]) <= 1 && abs(rr[2] - ra[2]) <= 1, "right %d %d %d, whole render %d %d %d", rr[0], rr[1], rr[2], ra[0], ra[1], ra[2]);

  /* nothing transparent: no draw */
  t3_object_remove(&scene->base, pn);
  t3_object_remove(&scene->base, pa);
  t3_renderer_render_opaque(r, scene, cam);
  finish(D);
  t3_renderer_render_transparent(r, scene, cam);
  CHECK(control || t3_renderer_info(r)->calls == 0, "a transparent render with nothing transparent made %u draws", t3_renderer_info(r)->calls);
  finish(C);
  diff = 0;
  for (int i = 0; i < W * H * 4; i++) diff += C[i] != D[i];
  CHECK(diff == 0, "a transparent render with nothing transparent changed %d bytes", diff);
#ifdef T3_WGPU
  CHECK(wgpu_errors == 0, "%d WebGPU validation errors", wgpu_errors);
#endif
  if (failures) return 1;
  puts("test_render_transparent (" BACKEND "): ok");
  return 0;
}
