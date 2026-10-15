/* Extension programs filled at run time: three.c's example extension
 * (tools/extensions/tint.mjs, state standard+map+xtint) multiplies the
 * material colour by a shared uniform (tint), a texture (tintMap) and a value
 * per object (tintScale), all set by the hook t3_renderer_set_extension
 * registers. Two boxes side by side:
 *   - with tint 1, a white tintMap and scale 1 the picture equals the same
 *     material without the extension (standard+map);
 *   - a scale on the left box changes the left box only, with the two drawn
 *     from one program (the per-draw block of each object is its own);
 *   - a new tint changes the picture on the next render, and the old tint
 *     brings the old picture back (the shared group is uploaded on change);
 *   - the controls: with no hook, or a hook that leaves the texture unset, the
 *     two draws are skipped, counted in info.skipped and named in the error.
 *
 * GLES (built against build/native/libthree.a, EGL surfaceless):
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_extension.c \
 *     build/native/libthree.a -lEGL -lGLESv2 -lz -lm
 * WebGPU (-DT3_WGPU, against build/native-wgpu/libthree.a and Dawn):
 *   cc -std=c99 -O2 -DT3_WGPU -I include -I src -I <dawn>/include test/test_extension.c \
 *     build/native-wgpu/libthree.a -L <dawn>/lib -lwebgpu_dawn -Wl,-rpath,<dawn>/lib -lEGL -lGLESv2 -lz -lm
 *
 * Exits 0 on success, 77 when no WebGPU adapter is available. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "three.h"

#define W 128
#define H 64

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
  t3_renderer_set_auto_instancing(r, false);   /* (the plain picture: each box its own draw, as the extension's are) */
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
static t3_renderer *renderer(void) {
  t3_renderer *r = t3_renderer_new(W, H);
  t3_renderer_set_auto_instancing(r, false);   /* (the plain picture: each box its own draw, as the extension's are) */
  return r;
}
static void read_frame(t3_renderer *r, uint8_t *px) { (void)r; glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); }
#define BACKEND "gles"
#endif

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL (" BACKEND "): " __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* texels of a and b differing by more than 1 in a channel, over columns [x0, x1) */
static int differ(const uint8_t *a, const uint8_t *b, int x0, int x1) {
  int n = 0;
  for (int y = 0; y < H; y++)
    for (int x = x0; x < x1; x++) {
      const uint8_t *p = a + (y * W + x) * 4, *q = b + (y * W + x) * 4;
      for (int c = 0; c < 3; c++) if (abs(p[c] - q[c]) > 1) { n++; break; }
    }
  return n;
}

/* what the hook sets */
static float tint[3] = { 1, 1, 1 }, left_scale = 1;
static t3_texture *tint_map;
static const t3_object *left;
static bool set_texture = true;
static int fills;
static void fill(t3_extension_ctx *c, void *user) {
  (void)user;
  fills++;
  float s = t3_extension_object(c) == left ? left_scale : 1;
  t3_extension_set_uniform(c, "tint", tint, 3);
  t3_extension_set_uniform(c, "tintScale", &s, 1);
  if (set_texture) t3_extension_set_texture(c, "tintMap", tint_map);
}

static t3_scene *scene;
static t3_camera *cam;
static t3_material *mat;
static void render(t3_renderer *r, uint8_t *px) {
  t3_renderer_render(r, scene, cam);
#ifdef T3_WGPU
  t3_renderer_wgpu_submit(r);
#endif
  read_frame(r, px);
}

int main(void) {
  int st = setup();
  if (st == 77) { puts("test_extension (" BACKEND "): no WebGPU adapter, skipped"); return 77; }
  if (st) return st;
  /* the same filtering the captured program bakes: linear, mipmapped, clamped */
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
  t3_object_set_position(cam, 0, 1.2f, 5);
  t3_object_look_at(cam, 0, 0, 0);
  t3_light *amb = t3_ambient_light_new(0x404040, 1), *sun = t3_directional_light_new(0xffffff, 1.5f);
  t3_object_set_position(sun, 2, 4, 3);
  t3_object_add(&scene->base, amb); t3_object_add(&scene->base, sun);
  t3_release(amb); t3_release(sun);
  mat = t3_mesh_standard_material_new(0xffffff);
  mat->roughness = 0.6f;
  t3_material_set_map(mat, map);
  t3_geometry *g = t3_box_geometry_new(1.2f, 1.2f, 1.2f, 1, 1, 1);
  t3_mesh *boxes[2];
  for (int i = 0; i < 2; i++) {
    boxes[i] = t3_mesh_new(g, mat);
    t3_object_set_position(boxes[i], i ? 1.1f : -1.1f, 0, 0);
    t3_object_set_rotation(boxes[i], 0.4f, 0.6f, 0);
    t3_object_add(&scene->base, boxes[i]);
    t3_release(boxes[i]);
  }
  left = &boxes[0]->base;
  /* (the camera looks at the gap between the boxes: columns [0, W/2) hold the left one) */

  static uint8_t plain[W * H * 4], full[W * H * 4], scaled[W * H * 4], red[W * H * 4], back[W * H * 4];
  t3_renderer *r = renderer();
  render(r, plain);   /* the material without the extension */
  CHECK(t3_renderer_info(r)->skipped == 0, "the plain material skipped %u draws (%s)", t3_renderer_info(r)->skipped, t3_renderer_generated_missing(r) ? t3_renderer_generated_missing(r) : "");

  CHECK(t3_material_set_extension(mat, "tint"), "set_extension(tint)");
  CHECK(!strcmp(t3_material_get_extension(mat), "tint"), "get_extension");
  CHECK(!t3_material_set_extension(mat, "Not A Name"), "a bad extension name was accepted");
  t3_renderer_set_extension(r, "tint", fill, NULL);
  render(r, full);
  CHECK(t3_renderer_info(r)->skipped == 0, "the extension material skipped %u draws: %s", t3_renderer_info(r)->skipped,
        t3_renderer_last_error(r) ? t3_renderer_last_error(r) : "");
  CHECK(fills >= 2, "the hook ran %d times for two draws", fills);
  int d = differ(plain, full, 0, W);
  CHECK(d == 0, "tint 1, a white tintMap and scale 1 differ from the plain material at %d texels", d);

  left_scale = 0.4f;
  render(r, scaled);
  int dl = differ(full, scaled, 0, W / 2), dr = differ(full, scaled, W / 2, W);
  CHECK(dl > 100, "a scale on the left box changed only %d of its texels", dl);
  CHECK(dr == 0, "a scale on the left box changed %d texels of the right one", dr);
  left_scale = 1;

  tint[1] = tint[2] = 0;
  render(r, red);
  d = differ(full, red, 0, W);
  CHECK(d > 200, "a new tint changed only %d texels", d);
  tint[1] = tint[2] = 1;
  render(r, back);
  d = differ(full, back, 0, W);
  CHECK(d == 0, "the old tint left %d texels changed", d);

  /* the tintMap: a checker of its own changes the picture */
  t3_texture *other = t3_data_texture_new(8, 8, checker);
  other->mag_filter = T3_LINEAR; other->min_filter = T3_LINEAR_MIPMAP_LINEAR; other->generate_mipmaps = true;
  t3_texture *keep = tint_map;
  tint_map = other;
  render(r, back);
  d = differ(full, back, 0, W);
  CHECK(d > 200, "another tintMap changed only %d texels", d);
  tint_map = keep;

  /* the controls, each on a fresh renderer (the error is the first one) */
  t3_renderer *r2 = renderer();
  render(r2, back);
  CHECK(t3_renderer_info(r2)->skipped == 2, "with no hook %u draws were skipped, not 2", t3_renderer_info(r2)->skipped);
  CHECK(t3_renderer_last_error(r2) && strstr(t3_renderer_last_error(r2), "no hook for extension tint"), "with no hook the error is: %s",
        t3_renderer_last_error(r2) ? t3_renderer_last_error(r2) : "(none)");
  d = differ(full, back, 0, W);
  CHECK(d > 200, "with no hook the picture still matches the filled one (%d texels differ)", d);
  t3_renderer_set_extension(r2, "tint", fill, NULL);
  t3_renderer_set_extension(r2, "tint", NULL, NULL);   /* removed again */
  render(r2, back);
  CHECK(t3_renderer_info(r2)->skipped == 2, "a removed hook still filled (%u skipped)", t3_renderer_info(r2)->skipped);

  t3_renderer *r3 = renderer();
  t3_renderer_set_extension(r3, "tint", fill, NULL);
  set_texture = false;
  render(r3, back);
  set_texture = true;
  CHECK(t3_renderer_info(r3)->skipped == 2, "with tintMap unset %u draws were skipped, not 2", t3_renderer_info(r3)->skipped);
  CHECK(t3_renderer_last_error(r3) && strstr(t3_renderer_last_error(r3), "left texture tintMap unset"), "with tintMap unset the error is: %s",
        t3_renderer_last_error(r3) ? t3_renderer_last_error(r3) : "(none)");

  /* and without the extension again: the plain program */
  t3_material_set_extension(mat, NULL);
  render(r, back);
  d = differ(plain, back, 0, W);
  CHECK(d == 0 && t3_material_get_extension(mat) == NULL, "clearing the extension left %d texels changed", d);

  t3_release(other); t3_release(tint_map); t3_release(map); t3_release(g); t3_release(mat);
  t3_release(scene); t3_release(cam);
  t3_renderer_destroy(r); t3_renderer_destroy(r2); t3_renderer_destroy(r3);
#ifdef T3_WGPU
  CHECK(wgpu_errors == 0, "%d WebGPU validation errors", wgpu_errors);
#endif
  if (!failures) printf("test_extension (" BACKEND "): ok\n");
  return failures ? 1 : 0;
}
