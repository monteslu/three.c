/* Texture sampling on GLES and WebGPU: the same picture. A fine pattern (one-texel lines, speckle) on a floor seen
 * at a grazing angle, mipmapped and anisotropically filtered (anisotropy 8), drawn by both backends: they must
 * agree (under 1% of pixels differ by more than 16 levels). Controls that must fail: WebGPU drawn with anisotropy 1
 * against GL with 8 (the sampler is what makes them agree), and the GL texture's sampler fields changed after its
 * upload (anisotropy, wrap) must change the GL picture to what a texture made that way draws, as the WebGPU
 * backend's sampler (made from the fields at every draw) does.
 *
 * Build after WGPU=1 ./build.sh (test/run.sh does this when Dawn is there):
 *   cc -std=c99 -O2 -DT3_WGPU -I include -I src -I <dawn>/include test/test_texture_parity.c \
 *     build/native-wgpu/libthree.a -L <dawn>/lib -lwebgpu_dawn -Wl,-rpath,<dawn>/lib -lEGL -lGLESv2 -lz -lm
 * T3_TEST_ADAPTER=low-power picks the integrated GPU (default), high-performance the discrete one.
 * Exits 0 on success, 77 when no GLES context or WebGPU adapter is available. */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webgpu/webgpu.h>

#include "three.h"

#define W 256
#define H 160
#define TN 128

static WGPUInstance inst;
static int errors;
static void adapter_cb(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUAdapter *)u1 = st == WGPURequestAdapterStatus_Success ? a : NULL; *(int *)u2 = 1;
}
static void device_cb(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUDevice *)u1 = st == WGPURequestDeviceStatus_Success ? d : NULL; *(int *)u2 = 1;
}
static void error_cb(WGPUDevice const *d, WGPUErrorType t, WGPUStringView m, void *u1, void *u2) {
  (void)d; (void)u1; (void)u2;
  if (errors++ < 8) fprintf(stderr, "webgpu error %d: %.*s\n", (int)t, (int)m.length, m.data);
}

static uint8_t pattern[TN * TN * 4];
static void make_pattern(void) {
  for (int y = 0; y < TN; y++)
    for (int x = 0; x < TN; x++) {
      uint32_t h = (uint32_t)(y * TN + x) * 2654435761u;
      h ^= h >> 15;
      bool line = (x & 15) == 0 || (y & 15) == 0;
      uint8_t v = line ? 20 : (uint8_t)(120 + (h & 127));
      uint8_t *p = pattern + (y * TN + x) * 4;
      p[0] = v, p[1] = (uint8_t)(v * 3 / 4), p[2] = ((x >> 4) + (y >> 4)) & 1 ? 230 : 60, p[3] = 255;
    }
}
typedef struct { t3_scene *s; t3_camera *cam; t3_texture *tex; } scene_t;
static scene_t make_scene(float aniso) {
  scene_t sc;
  sc.s = t3_scene_new();
  sc.tex = t3_texture_new(TN, TN, pattern);
  sc.tex->wrap_s = sc.tex->wrap_t = T3_REPEAT;
  sc.tex->color_space = T3_SRGB_COLOR_SPACE;
  sc.tex->anisotropy = aniso;
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  t3_material_set_texture(m, T3_MAP, sc.tex);
  t3_geometry *g = t3_plane_geometry_new(40, 40, 1, 1);
  float *uv = g->attributes[T3_ATTR_UV]->array;
  for (int i = 0; i < 8; i++) uv[i] *= 24;
  t3_mesh *floor = t3_mesh_new(g, m);
  t3_object_set_rotation(floor, -1.5707963f, 0, 0);
  t3_object_add(&sc.s->base, floor);
  t3_release(floor), t3_release(g), t3_release(m);
  sc.cam = t3_perspective_camera_new(60, (float)W / H, 0.1f, 100);
  t3_object_set_position(sc.cam, 0, 0.6f, 4);
  t3_object_look_at(sc.cam, 0, 0.3f, -20);
  return sc;
}
/* one-texel black and white columns (sRGB), 8 texels a pixel (mip level 3, seen head on): a mip chain made in
 * linear light averages them to 0.5 (188 in sRGB), one made on the sRGB values to 128 */
static uint8_t stripes[TN * TN * 4];
static scene_t make_stripes(void) {
  for (int i = 0; i < TN * TN; i++) { uint8_t v = (i & 1) ? 255 : 0; stripes[i * 4] = stripes[i * 4 + 1] = stripes[i * 4 + 2] = v; stripes[i * 4 + 3] = 255; }
  scene_t sc;
  sc.s = t3_scene_new();
  sc.tex = t3_texture_new(TN, TN, stripes);
  sc.tex->wrap_s = sc.tex->wrap_t = T3_REPEAT;
  sc.tex->color_space = T3_SRGB_COLOR_SPACE;
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  t3_material_set_texture(m, T3_MAP, sc.tex);
  t3_geometry *g = t3_plane_geometry_new(2, 2, 1, 1);
  float *uv = g->attributes[T3_ATTR_UV]->array;
  for (int i = 0; i < 8; i++) uv[i] *= (float)(W * 8) / TN;   /* W pixels show W * 8 texels */
  t3_mesh *q = t3_mesh_new(g, m);
  t3_object_add(&sc.s->base, q);
  t3_release(q), t3_release(g), t3_release(m);
  sc.cam = t3_orthographic_camera_new(-1, 1, 1, -1, 0.1f, 10);
  t3_object_set_position(sc.cam, 0, 0, 5);
  return sc;
}
/* the mean red of the middle quarter */
static double mid_mean(const uint8_t *a) {
  double s = 0; int n = 0;
  for (int y = H / 4; y < H * 3 / 4; y++) for (int x = W / 4; x < W * 3 / 4; x++) s += a[(y * W + x) * 4], n++;
  return s / n;
}
static void free_scene(scene_t *sc) { t3_release(sc->tex), t3_release(sc->cam), t3_release(sc->s); }

static void gl_read(uint8_t *out) {
  static uint8_t tmp[W * H * 4];
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
  for (int y = 0; y < H; y++) memcpy(out + y * W * 4, tmp + (H - 1 - y) * W * 4, W * 4);   /* top row first */
}
/* share of pixels whose largest channel difference is over 16 */
static double differ(const uint8_t *a, const uint8_t *b) {
  int n = 0;
  for (int i = 0; i < W * H; i++) {
    int d = 0;
    for (int c = 0; c < 3; c++) { int e = abs(a[i * 4 + c] - b[i * 4 + c]); if (e > d) d = e; }
    n += d > 16;
  }
  return (double)n / (W * H);
}
static int fails;
static void check(bool ok, const char *what, double v) { printf("%s  %s (%.3f%%)\n", ok ? "ok  " : "FAIL", what, v * 100); if (!ok) fails++; }

int main(void) {
  make_pattern();
  static uint8_t gl8[W * H * 4], gl1[W * H * 4], gl_live[W * H * 4], gl_clamp[W * H * 4], gl_live_clamp[W * H * 4];
  static uint8_t wg8[W * H * 4], wg1[W * H * 4], gl_st[W * H * 4], wg_st[W * H * 4];

  /* GLES */
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(d, NULL, NULL)) { puts("test_texture_parity: no EGL, skipped"); return 77; }
  EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
  EGLConfig cfg;
  EGLint n = 0;
  if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) { puts("test_texture_parity: no EGL config, skipped"); return 77; }
  EGLint sa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
  EGLSurface surf = eglCreatePbufferSurface(d, cfg, sa);
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint xa[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xa);
  if (!ctx || !eglMakeCurrent(d, surf, surf, ctx)) { puts("test_texture_parity: no GLES context, skipped"); return 77; }
  t3_renderer *r = t3_renderer_new(W, H);
  {
    scene_t a = make_scene(8);
    t3_renderer_render(r, a.s, a.cam); gl_read(gl8);
    /* fields changed after the upload, no version bump: the GL texture follows them */
    a.tex->anisotropy = 1;
    t3_renderer_render(r, a.s, a.cam); gl_read(gl_live);
    a.tex->wrap_s = a.tex->wrap_t = T3_CLAMP_TO_EDGE;
    t3_renderer_render(r, a.s, a.cam); gl_read(gl_live_clamp);
    free_scene(&a);
    scene_t b = make_scene(1);
    t3_renderer_render(r, b.s, b.cam); gl_read(gl1);
    b.tex->wrap_s = b.tex->wrap_t = T3_CLAMP_TO_EDGE;
    b.tex->version++;   /* a texture made clamped (uploaded again) */
    t3_renderer_render(r, b.s, b.cam); gl_read(gl_clamp);
    free_scene(&b);
    scene_t c = make_stripes();
    t3_renderer_render(r, c.s, c.cam); gl_read(gl_st);
    free_scene(&c);
  }
  t3_renderer_destroy(r);

  /* WebGPU */
  inst = wgpuCreateInstance(NULL);
  WGPUAdapter ad = NULL;
  int done = 0;
  const char *pp = getenv("T3_TEST_ADAPTER");
  WGPURequestAdapterOptions ao = { .powerPreference = pp && !strcmp(pp, "high-performance") ? WGPUPowerPreference_HighPerformance : WGPUPowerPreference_LowPower };
  wgpuInstanceRequestAdapter(inst, &ao, (WGPURequestAdapterCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = adapter_cb, .userdata1 = &ad, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!ad) { puts("test_texture_parity: no WebGPU adapter, skipped"); return 77; }
  WGPUDevice dev = NULL;
  WGPUDeviceDescriptor dd = { .uncapturedErrorCallbackInfo = { .callback = error_cb } };
  done = 0;
  wgpuAdapterRequestDevice(ad, &dd, (WGPURequestDeviceCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = device_cb, .userdata1 = &dev, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!dev) { puts("test_texture_parity: no WebGPU device, skipped"); return 77; }
  WGPUQueue q = wgpuDeviceGetQueue(dev);
  for (int k = 0; k < 3; k++) {
    t3_renderer *w = t3_renderer_new(W, H);
    t3_renderer_use_wgpu(w, dev, q, WGPUTextureFormat_RGBA8Unorm);
    scene_t a = k == 2 ? make_stripes() : make_scene(k ? 1 : 8);
    t3_renderer_render(w, a.s, a.cam);
    t3_renderer_wgpu_read(w, k == 2 ? wg_st : k ? wg1 : wg8);
    free_scene(&a);
    t3_renderer_destroy(w);
  }

  check(differ(gl8, wg8) < 0.01, "anisotropy 8: GLES and WebGPU draw the same picture", differ(gl8, wg8));
  check(differ(gl1, wg1) < 0.01, "anisotropy 1: GLES and WebGPU draw the same picture", differ(gl1, wg1));
  check(differ(gl8, wg1) > 0.03, "control: WebGPU without anisotropy differs from GLES with it", differ(gl8, wg1));
  check(differ(gl_live, gl1) < 0.001, "GLES: anisotropy changed after the upload applies", differ(gl_live, gl1));
  check(differ(gl_live, gl8) > 0.03, "control: and changes the picture", differ(gl_live, gl8));
  check(differ(gl_live_clamp, gl_clamp) < 0.001, "GLES: wrap changed after the upload applies", differ(gl_live_clamp, gl_clamp));
  check(differ(gl_clamp, gl1) > 0.03, "control: clamped differs from repeated", differ(gl_clamp, gl1));
  double mg = mid_mean(gl_st), mw = mid_mean(wg_st);
  printf("      sRGB stripes at mip level 3: GLES %.1f, WebGPU %.1f (linear-light average 188, sRGB-value average 128)\n", mg, mw);
  check(mg > 175 && mg < 200, "GLES: an sRGB texture's mip chain averages in linear light (mean / 100)", mg / 10000);
  check(mw > 175 && mw < 200, "WebGPU: an sRGB texture's mip chain averages in linear light (mean / 100)", mw / 10000);
  check(errors == 0, "no WebGPU errors", errors);
  printf("test_texture_parity: %s\n", fails ? "FAILED" : "ok");
  return fails ? 1 : 0;
}
