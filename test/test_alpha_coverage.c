/* material.alpha_to_coverage on a multisampled canvas (t3_renderer_set_antialias 4), on GLES and WebGPU.
 *
 * A white plane fills the view of a 32 x 32 canvas over black, its alpha ramping with uv.x across it, alpha_test 0.5,
 * drawn with r187's alphaToCoverage program (test/extensions/a2c.mjs, table a2ctab: basic+atest+xa2c). The alpha it
 * writes is sharpened to 0..1 over about a pixel round the cutoff. With alpha to coverage on, the pixels across that
 * edge are covered by some of their samples only, so the resolved edge differs from the same draw with it off (every
 * drawn pixel covering all of its samples). The control (T3_TEST_CONTROL=1) draws both with it off: the rows match
 * and the test must fail. Before 0.187.21 WebGPU never turned the pipeline's alphaToCoverageEnabled on (its rows
 * matched).
 *
 * GLES:   cc ... test/test_alpha_coverage.c build/test/a2ctab/a2ctab_gl.c build/native/libthree.a -lEGL -lGLESv2 -lz -lm
 * WebGPU: cc -DT3_WGPU ... test/test_alpha_coverage.c build/test/a2ctab/a2ctab_wgpu.c build/native-wgpu/libthree.a ...
 * Exits 0 on success, 77 when no WebGPU adapter is available. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "three.h"
#include "gen/programs.h"

#define W 32
#define H 32

#ifdef T3_WGPU
#include <webgpu/webgpu.h>
extern const t3_gen_table a2ctab_wgpu;
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
  t3_register_program_table(NULL, &a2ctab_wgpu);
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
extern const t3_gen_table a2ctab_gl;
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
  t3_register_program_table(&a2ctab_gl, NULL);
  return 0;
}
static t3_renderer *renderer(void) { return t3_renderer_new(W, H); }
static void read_frame(t3_renderer *r, uint8_t *px) { (void)r; glBindFramebuffer(GL_FRAMEBUFFER, 0); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); }
#define BACKEND "gles"
#endif

/* draw the scene multisampled. GLES: the canvas (antialias). WebGPU, whose canvas is single sampled: a 4x
 * render target, then the target's resolved picture drawn over the canvas on a plane */
static void draw(t3_renderer *r, t3_scene *scene, t3_camera *cam) {
#ifdef T3_WGPU
  static t3_render_target *rt;
  static t3_scene *show;
  if (!rt) {
    t3_render_target_options o = t3_render_target_options_default();
    o.samples = 4;
    o.min_filter = o.mag_filter = T3_NEAREST;
    rt = t3_render_target_new(W, H, &o);
    show = t3_scene_new();
    t3_material *sm = t3_mesh_basic_material_new(0xffffff);
    t3_material_set_map(sm, rt->texture);
    t3_mesh *q = t3_mesh_new(t3_plane_geometry_new(2, 2, 1, 1), sm);
    t3_object_add(&show->base, q);
  }
  t3_renderer_set_render_target(r, rt);
  t3_renderer_render(r, scene, cam);
  t3_renderer_set_render_target(r, NULL);
  t3_renderer_render(r, show, cam);
#else
  t3_renderer_render(r, scene, cam);
#endif
}

static void fill(t3_extension_ctx *c, void *u) { (void)c; (void)u; } /* the extension has no values of its own */

int main(void) {
  int st = setup();
  if (st == 77) { puts("test_alpha_coverage (" BACKEND "): no WebGPU adapter, skipped"); return 77; }
  if (st) return st;
  bool control = getenv("T3_TEST_CONTROL") != NULL;
  t3_renderer *r = renderer();
  t3_renderer_set_antialias(r, 4);
  t3_renderer_set_extension(r, "a2c", fill, NULL);
  t3_renderer_set_clear_color(r, 0x000000, 1);
  t3_camera *cam = t3_orthographic_camera_new(-1, 1, 1, -1, 0.1f, 10);
  t3_object_set_position(cam, 0, 0, 5);
  t3_scene *scene = t3_scene_new();
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->alpha_test = 0.5f;
  m->alpha_to_coverage = !control;
  if (!t3_material_set_extension(m, "a2c")) return fprintf(stderr, "no extension a2c\n"), 2;
  t3_mesh *p = t3_mesh_new(t3_plane_geometry_new(2, 2, 1, 1), m);
  t3_object_add(&scene->base, p);
  static uint8_t on[W * H * 4], off[W * H * 4];
  draw(r, scene, cam);
  read_frame(r, on);
  /* the same draw with alpha to coverage off (the control renders both that way) */
  m->alpha_to_coverage = false;
  m->version++;
  draw(r, scene, cam);
  read_frame(r, off);
  int black = 0, white = 0, differ = 0, failures = 0;
  for (int x = 0; x < W; x++) {
    const uint8_t *p = &on[(H / 2 * W + x) * 4], *q = &off[(H / 2 * W + x) * 4];
    if (p[1] < 8) black++;
    else if (p[1] > 247) white++;
    if (abs(p[1] - q[1]) > 8) differ++;
  }
  if (t3_renderer_info(r)->skipped) fprintf(stderr, "renderer: %s (missing: %s)\n", t3_renderer_last_error(r) ? t3_renderer_last_error(r) : "", t3_renderer_generated_missing(r) ? t3_renderer_generated_missing(r) : "");
  printf("row with alpha to coverage: %d black, %d white; %d pixels differ from the row without; draws skipped %u\n", black, white,
         differ, t3_renderer_info(r)->skipped);
  if (t3_renderer_info(r)->skipped) failures++, fprintf(stderr, "FAIL (" BACKEND "): the draw was skipped\n");
  if (!(black >= W / 2 - 2 && white >= W / 2 - 3)) failures++, fprintf(stderr, "FAIL (" BACKEND "): not black then white\n");
  if (differ < 1) failures++, fprintf(stderr, "FAIL (" BACKEND "): the cut-out edge is the same with alpha to coverage as without\n");
#ifdef T3_WGPU
  if (wgpu_errors) failures++, fprintf(stderr, "FAIL (" BACKEND "): %d WebGPU validation errors\n", wgpu_errors);
#endif
  if (failures) return 1;
  puts("test_alpha_coverage (" BACKEND "): ok");
  return 0;
}
