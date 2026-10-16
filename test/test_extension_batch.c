/* Auto-instancing of extension materials (t3_renderer_set_extension_batching):
 * sixteen boxes of one geometry and one material carrying the example
 * extension tint (tools/extensions/tint.mjs), whose hook here sets only values
 * equal for every box. Program states come from a small table of the test's
 * own (test/run.sh generates build/test/exttab with tools/gen-project-table.mjs:
 * standard+map+l1000+xtint and standard+map+instbig+l1000+xtint).
 *   - batching off (the default): every box is its own draw (16 calls);
 *   - batching on: the boxes are one instanced draw (1 call), the hook runs
 *     once, and the picture is the same;
 *   - the control: batching off again gives 16 calls again (the draw count
 *     must differ from the batched one, or the flag did nothing);
 *   - nothing is skipped in any of them (the instanced state is in the table).
 *
 * GLES: cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src -I build/test/exttab \
 *         test/test_extension_batch.c build/test/exttab/exttab_gl.c build/native/libthree.a -lEGL -lGLESv2 -lz -lm
 * WebGPU: the same with -DT3_WGPU, build/test/exttab/exttab_wgpu.c too, against
 *         build/native-wgpu/libthree.a and Dawn.
 * Exits 0 on success, 77 when no WebGPU adapter is available. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "three.h"
#include "gen/programs.h"

#define W 128
#define H 96

extern const t3_gen_table exttab_gl;
#ifdef T3_WGPU
extern const t3_gen_table exttab_wgpu;
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
static void read_frame(t3_renderer *r, uint8_t *px) { t3_renderer_wgpu_read(r, px); }
#define BACKEND "webgpu"
#else
#include <EGL/egl.h>
#include <GLES3/gl3.h>
static int setup(void) {
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(d, NULL, NULL)) return fprintf(stderr, "eglInitialize failed\n"), 2;
  /* a depth buffer: without one the boxes overlap in draw order, which batching changes */
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

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL (" BACKEND "): "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static int maxd;
static int differ(const uint8_t *a, const uint8_t *b) {
  int n = 0;
  maxd = 0;
  for (int i = 0; i < W * H; i++) {
    bool off = false;
    for (int c = 0; c < 3; c++) {
      int d = abs(a[i * 4 + c] - b[i * 4 + c]);
      if (d > maxd) maxd = d;
      off |= d > 1;
    }
    n += off;
  }
  return n;
}

/* the hook: values every box shares (it never looks at the object) */
static const float tint[3] = { 1, 0.8f, 0.6f }, scale = 1;
static t3_texture *tint_map;
static int fills;
static void fill(t3_extension_ctx *c, void *user) {
  (void)user;
  fills++;
  t3_extension_set_uniform(c, "tint", tint, 3);
  t3_extension_set_uniform(c, "tintScale", &scale, 1);
  t3_extension_set_texture(c, "tintMap", tint_map);
}

static t3_scene *scene;
static t3_camera *cam;
static void render(t3_renderer *r, uint8_t *px) {
  fills = 0;
  t3_renderer_render(r, scene, cam);
#ifdef T3_WGPU
  t3_renderer_wgpu_submit(r);
#endif
  read_frame(r, px);
}

int main(void) {
  int st = setup();
  if (st == 77) { puts("test_extension_batch (" BACKEND "): no WebGPU adapter, skipped"); return 77; }
  if (st) return st;
#ifdef T3_WGPU
  t3_register_program_table(&exttab_gl, &exttab_wgpu);
#else
  t3_register_program_table(&exttab_gl, NULL);
#endif
  uint8_t white[4 * 4 * 4], checker[8 * 8 * 4];
  memset(white, 255, sizeof white);
  for (int i = 0; i < 64; i++) {
    bool on = ((i % 8) / 2 + (i / 8) / 2) & 1;
    checker[i * 4] = on ? 230 : 40; checker[i * 4 + 1] = on ? 200 : 90; checker[i * 4 + 2] = on ? 120 : 220; checker[i * 4 + 3] = 255;
  }
  tint_map = t3_data_texture_new(4, 4, white);
  t3_texture *map = t3_data_texture_new(8, 8, checker);
  t3_texture *texs[2] = { tint_map, map };
  for (int i = 0; i < 2; i++) { texs[i]->mag_filter = T3_LINEAR; texs[i]->min_filter = T3_LINEAR_MIPMAP_LINEAR; texs[i]->generate_mipmaps = true; }

  scene = t3_scene_new();
  cam = t3_perspective_camera_new(40, (float)W / H, 0.1f, 50);
  t3_object_set_position(cam, 0, 4, 9);
  t3_object_look_at(cam, 0, 0, 0);
  t3_light *amb = t3_ambient_light_new(0x404040, 1), *sun = t3_directional_light_new(0xffffff, 1.5f);
  t3_object_set_position(sun, 2, 4, 3);
  t3_object_add(&scene->base, amb); t3_object_add(&scene->base, sun);
  t3_release(amb); t3_release(sun);
  t3_material *mat = t3_mesh_standard_material_new(0xffffff);
  mat->roughness = 0.6f;
  t3_material_set_map(mat, map);
  CHECK(t3_material_set_extension(mat, "tint"), "set_extension(tint)");
  t3_geometry *g = t3_box_geometry_new(0.8f, 0.8f, 0.8f, 1, 1, 1);
  for (int i = 0; i < 16; i++) {
    t3_mesh *b = t3_mesh_new(g, mat);
    t3_object_set_position(b, (float)(i % 4) * 1.6f - 2.4f, 0, (float)(i / 4) * 1.6f - 2.4f);
    t3_object_set_rotation(b, 0.3f * (float)i, 0.5f, 0);
    t3_object_add(&scene->base, b);
    t3_release(b);
  }

  static uint8_t one[W * H * 4], batched[W * H * 4], again[W * H * 4];
  t3_renderer *r = renderer();
  t3_renderer_set_extension(r, "tint", fill, NULL);
  render(r, one);
  unsigned calls_one = t3_renderer_info(r)->calls;
  CHECK(t3_renderer_info(r)->skipped == 0, "unbatched: %u draws skipped (%s)", t3_renderer_info(r)->skipped,
        t3_renderer_last_error(r) ? t3_renderer_last_error(r) : "");
  CHECK(calls_one >= 16, "unbatched: %u draw calls, expected one a box (16)", calls_one);
  CHECK(fills >= 16, "unbatched: the hook ran %d times for 16 boxes", fills);

  t3_renderer_set_extension_batching(r, "tint", true);
  render(r, batched);
  unsigned calls_batched = t3_renderer_info(r)->calls;
  CHECK(t3_renderer_info(r)->skipped == 0, "batched: %u draws skipped (%s)", t3_renderer_info(r)->skipped,
        t3_renderer_last_error(r) ? t3_renderer_last_error(r) : "");
  CHECK(calls_batched + 15 <= calls_one, "batched: %u draw calls, unbatched %u: the boxes were not instanced", calls_batched, calls_one);
  CHECK(fills >= 1 && fills < 16, "batched: the hook ran %d times (once a batch)", fills);
  int d = differ(one, batched);
  CHECK(d == 0, "batched and unbatched pictures differ at %d texels (max %d levels)", d, maxd);

  /* the control: the flag off again, one draw a box again */
  t3_renderer_set_extension_batching(r, "tint", false);
  render(r, again);
  CHECK(t3_renderer_info(r)->calls == calls_one, "batching off again: %u draw calls, not %u", t3_renderer_info(r)->calls, calls_one);
  d = differ(one, again);
  CHECK(d == 0, "batching off again changed %d texels", d);
  printf("test_extension_batch (" BACKEND "): %u draw calls one by one, %u batched%s\n", calls_one, calls_batched,
         failures ? "" : ", ok");
  return failures ? 1 : 0;
}
