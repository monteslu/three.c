/* The blend modes, the same on both backends.
 *
 * A transparent 0x404040 plane covers a 0x808080 view, drawn with
 * MeshBasicMaterial in every blending mode and with every CustomBlending
 * equation (Add, Subtract, ReverseSubtract, Min, Max), each row with its own
 * expected value. The renderer blends in its linear working target and
 * encodes sRGB at the output (so additive is 141, not 192), and the output
 * keeps a pixel's alpha (a mode that leaves alpha 0 there draws black).
 * NormalBlending is the control: a backend that ignored the material's blend
 * state (WebGPU before 0.186.20 drew every mode but Additive as Normal) draws
 * 64 for every row.
 *
 * GLES (built against build/native/libthree.a, EGL surfaceless):
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_blend_modes.c \
 *     build/native/libthree.a -lEGL -lGLESv2 -lz -lm
 * WebGPU (-DT3_WGPU, against build/native-wgpu/libthree.a and Dawn):
 *   cc -std=c99 -O2 -DT3_WGPU -I include -I src -I <dawn>/include test/test_blend_modes.c \
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
static t3_scene *scene;
static t3_camera *cam;
static uint8_t px[W * H * 4];

/* render and return the centre texel */
static const uint8_t *render(void) {
  t3_renderer_render(r, scene, cam);
#ifdef T3_WGPU
  t3_renderer_wgpu_submit(r);
#endif
  read_frame(r, px);
  CHECK(t3_renderer_info(r)->skipped == 0, "a draw was skipped: %s", t3_renderer_generated_missing(r));
  return px + ((H / 2) * W + W / 2) * 4;
}

int main(void) {
  int st = setup();
  if (st == 77) { puts("test_blend_modes (" BACKEND "): no WebGPU adapter, skipped"); return 77; }
  if (st) return st;
  r = renderer();
  t3_renderer_set_clear_color(r, 0x808080, 1);
  cam = t3_orthographic_camera_new(-1, 1, 1, -1, 0.1f, 10);
  t3_object_set_position(cam, 0, 0, 5);
  const uint8_t *p;

  /* the background */
  scene = t3_scene_new();
  p = render();
  int bg = p[0];
  CHECK(abs(bg - 128) <= 5 && abs(p[1] - 128) <= 5, "background %d %d, expected 128", p[0], p[1]);

  /* the blend modes: a 0x404040 plane (64) over the 0x808080 background (128), sRGB values blended as stored */
  struct { const char *name; int blending, eq, src, dst, expect; } rows[] = {
    { "normal", T3_NORMAL_BLENDING, 0, 0, 0, 64 },
    { "additive", T3_ADDITIVE_BLENDING, 0, 0, 0, 141 },          /* linear 0.216 + 0.051 */
    { "subtractive", T3_SUBTRACTIVE_BLENDING, 0, 0, 0, 0 },     /* alpha dst * (1 - 1): 0 */
    { "multiply", T3_MULTIPLY_BLENDING, 0, 0, 0, 27 },         /* linear 0.216 * 0.051 */
    { "custom add", T3_CUSTOM_BLENDING, T3_ADD_EQUATION, T3_ONE_FACTOR, T3_ONE_FACTOR, 141 },
    { "custom subtract", T3_CUSTOM_BLENDING, T3_SUBTRACT_EQUATION, T3_ONE_FACTOR, T3_ONE_FACTOR, 0 },
    { "custom reverse subtract", T3_CUSTOM_BLENDING, T3_REVERSE_SUBTRACT_EQUATION, T3_ONE_FACTOR, T3_ONE_FACTOR, 0 },   /* alpha 1 - 1 */
    { "custom min", T3_CUSTOM_BLENDING, T3_MIN_EQUATION, T3_ONE_FACTOR, T3_ONE_FACTOR, 64 },
    { "custom max", T3_CUSTOM_BLENDING, T3_MAX_EQUATION, T3_ONE_FACTOR, T3_ONE_FACTOR, 128 },
    /* GL ignores the factors of Min / Max; WebGPU requires them One, so others must still draw */
    { "custom max, other factors", T3_CUSTOM_BLENDING, T3_MAX_EQUATION, T3_SRC_ALPHA_FACTOR, T3_ZERO_FACTOR, 128 },
  };
  t3_geometry *g = t3_plane_geometry_new(2, 2, 1, 1);
  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    t3_material *m = t3_mesh_basic_material_new(0x404040);
    m->transparent = true;
    m->blending = rows[i].blending;
    if (rows[i].blending == T3_CUSTOM_BLENDING) {
      m->blend_equation = rows[i].eq; m->blend_src = rows[i].src; m->blend_dst = rows[i].dst;
    }
    scene = t3_scene_new();
    t3_object_add(&scene->base, t3_mesh_new(g, m));
    p = render();
    printf("%-26s %3d %3d %3d (expected %d)\n", rows[i].name, p[0], p[1], p[2], rows[i].expect);
    CHECK(abs(p[0] - rows[i].expect) <= 5 && abs(p[1] - rows[i].expect) <= 5 && abs(p[2] - rows[i].expect) <= 5,
          "%s drew %d %d %d, expected %d", rows[i].name, p[0], p[1], p[2], rows[i].expect);
  }
#ifdef T3_WGPU
  CHECK(wgpu_errors == 0, "%d WebGPU validation errors", wgpu_errors);
#endif
  if (failures) return 1;
  puts("test_blend_modes (" BACKEND "): ok");
  return 0;
}
