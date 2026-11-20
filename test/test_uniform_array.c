/* An extension whose values are an r187 uniformArray (test/extensions/arr.mjs):
 * a buffer binding of its own in the object group, beside the draw's uniform
 * block. The colour is material colour x arr[0].rgb x arr[3].rgb; the hook
 * sets arr0..arr3, and a plane filling the view must come out in that colour.
 *   - arr0 = (1, 0.5, 0.25), arr3 = (0.8, 1, 1), arr1 = arr2 = 0 (not read):
 *     the plane is (0.8, 0.5, 0.25) of the unlit-ish colour checked by ratio;
 *   - nothing is skipped;
 *   - the control (T3_TEST_CONTROL=1) leaves arr3 at zero: the plane comes
 *     out black and the check must fail (test/run.sh expects that).
 * The table is the test's own (test/run.sh: build/test/arrtab, state
 * standard+xarr, captured on the mock GPU). Exits 77 with no WebGPU adapter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "three.h"
#include "gen/programs.h"

#define W 64
#define H 64

extern const t3_gen_table arrtab_gl;
#ifdef T3_WGPU
extern const t3_gen_table arrtab_wgpu;
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
  WGPURequestAdapterOptions ao = { .powerPreference = WGPUPowerPreference_HighPerformance };
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
static void read_frame(t3_renderer *r, uint8_t *px) { t3_renderer_wgpu_submit(r); t3_renderer_wgpu_read(r, px); }
#define BACKEND "webgpu"
#else
#include <EGL/egl.h>
#include <GLES3/gl3.h>
static int setup(void) {
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(d, NULL, NULL)) return fprintf(stderr, "eglInitialize failed\n"), 2;
  EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_DEPTH_SIZE, 24, EGL_NONE };
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

static bool control;
static void fill(t3_extension_ctx *c, void *user) {
  (void)user;
  const float a0[4] = { 1, 0.5f, 0.25f, 0 }, z[4] = { 0, 0, 0, 0 }, a3[4] = { 0.8f, 1, 1, 0 };
  t3_extension_set_uniform(c, "arr0", a0, 4);
  t3_extension_set_uniform(c, "arr1", z, 4);
  t3_extension_set_uniform(c, "arr2", z, 4);
  t3_extension_set_uniform(c, "arr3", control ? z : a3, 4);
}

int main(void) {
  control = getenv("T3_TEST_CONTROL") != NULL;
  int st = setup();
  if (st == 77) { puts("test_uniform_array (" BACKEND "): no WebGPU adapter, skipped"); return 77; }
  if (st) return st;
#ifdef T3_WGPU
  t3_register_program_table(&arrtab_gl, &arrtab_wgpu);
#else
  t3_register_program_table(&arrtab_gl, NULL);
#endif
  t3_scene *s = t3_scene_new();
  t3_camera *cam = t3_perspective_camera_new(40, 1, 0.1f, 20);
  t3_object_set_position(cam, 0, 0, 3);
  t3_object_look_at(cam, 0, 0, 0);
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->roughness = 1; m->metalness = 0;
  if (!t3_material_set_extension(m, "arr")) { fprintf(stderr, "set_extension(arr) failed\n"); return 1; }
  t3_geometry *g = t3_plane_geometry_new(8, 8, 1, 1);
  t3_mesh *plane = t3_mesh_new(g, m);
  t3_object_add(&s->base, plane);
  t3_light *amb = t3_ambient_light_new(0xffffff, 1);
  t3_object_add(&s->base, amb);
  t3_renderer *r = renderer();
  t3_renderer_set_extension(r, "arr", fill, NULL);
  static uint8_t px[W * H * 4];
  t3_renderer_render(r, s, cam);
  read_frame(r, px);
  const uint8_t *c = px + ((H / 2) * W + W / 2) * 4;
  const char *err = t3_renderer_last_error(r);
  unsigned skipped = t3_renderer_info(r)->skipped;
  printf("test_uniform_array (%s%s): centre %d %d %d, %u skipped%s%s\n", BACKEND, control ? ", control" : "", c[0], c[1], c[2], skipped,
         err && *err ? ": " : "", err ? err : "");
  int fail = 0;
  if (skipped) fail = 1;
  /* r : g : b = 0.8 : 0.5 : 0.25 before the output encoding: r > g > b, b well above black (the control is black) */
  if (!(c[0] > c[1] + 20 && c[1] > c[2] + 20 && c[2] > 20)) fail = 1;
  if (fail) fprintf(stderr, "FAIL (" BACKEND "%s)\n", control ? ", control" : "");
  return fail;
}
