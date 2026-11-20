/* Shadow casters of auto-instanced meshes draw instanced: 200 boxes of one
 * geometry and one material, casting onto a ground plane under a directional
 * light with a shadow map. render's lists draw the boxes as one instanced
 * draw; the caster pass now does too (depth+instbig, the InstancedMesh caster
 * r187 draws), so the frame is a handful of draws, not one a box in the shadow
 * pass. The same scene with the casters drawn one by one
 * (T3_TEST_CAST_ONE_BY_ONE, set for a second renderer) must give the same
 * picture with about 200 draws more. The control (T3_TEST_CONTROL=1) sets it
 * for the first renderer too: the draw counts then match and the test fails.
 * Uses three.c's full table (standard+shadow+l1000, standard+instbig+shadow+l1000,
 * depth, depth+instbig). Exits 77 with no WebGPU adapter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "three.h"

#define W 256
#define H 192

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

static t3_scene *scene_new(void) {
  t3_scene *s = t3_scene_new();
  t3_geometry *box = t3_box_geometry_new(0.5f, 1.2f, 0.5f, 1, 1, 1);
  t3_material *m = t3_mesh_standard_material_new(0xd0b090);
  for (int i = 0; i < 200; i++) {
    t3_mesh *b = t3_mesh_new(box, m);
    t3_object_set_position(b, (float)(i % 20) * 0.9f - 8.55f, 0.6f, (float)(i / 20) * 0.9f - 4.05f);
    b->base.cast_shadow = b->base.receive_shadow = true;
    t3_object_add(&s->base, b);
    t3_release(b);
  }
  t3_release(box); t3_release(m);
  t3_geometry *pg = t3_plane_geometry_new(30, 30, 1, 1);
  t3_material *gm = t3_mesh_standard_material_new(0x808890);
  t3_mesh *ground = t3_mesh_new(pg, gm);
  t3_object_set_rotation(ground, -1.5707963f, 0, 0);
  ground->base.receive_shadow = true;
  t3_object_add(&s->base, ground);
  t3_release(ground); t3_release(pg); t3_release(gm);
  t3_light *a = t3_ambient_light_new(0x404040, 1), *d = t3_directional_light_new(0xffffff, 2.5f);
  t3_object_set_position(d, 6, 10, 4);
  d->base.cast_shadow = true;
  d->shadow->map_width = d->shadow->map_height = 1024;
  t3_camera *sh = d->shadow->camera;
  sh->left = -14; sh->right = 14; sh->top = 14; sh->bottom = -14; sh->near = 1; sh->far = 40;
  t3_camera_update_projection_matrix(sh);
  t3_object_add(&s->base, a); t3_object_add(&s->base, d);
  t3_release(a); t3_release(d);
  return s;
}

static unsigned frame(bool one_by_one, uint8_t *px, unsigned *skipped, char *err, size_t errcap) {
  if (one_by_one) setenv("T3_TEST_CAST_ONE_BY_ONE", "1", 1); else unsetenv("T3_TEST_CAST_ONE_BY_ONE");
  t3_renderer *r = renderer();
  t3_renderer_set_shadow_map(r, true, T3_PCF_SHADOW_MAP);
  t3_scene *s = scene_new();
  t3_camera *cam = t3_perspective_camera_new(50, (float)W / H, 0.5f, 100);
  t3_object_set_position(cam, 0, 9, 13);
  t3_object_look_at(cam, 0, 0, 0);
  t3_renderer_render(r, s, cam);
  t3_renderer_render(r, s, cam);
  read_frame(r, px);
  unsigned calls = t3_renderer_info(r)->calls;
  *skipped = t3_renderer_info(r)->skipped;
  const char *e = t3_renderer_last_error(r);
  snprintf(err, errcap, "%s", e ? e : "");
  t3_release(cam);
  t3_release(s);
  t3_renderer_destroy(r);
  return calls;
}

#include <time.h>
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
/* T3_TEST_TIME=n: n frames of each way, in interleaved rounds of 100; CPU ms per frame (the GPU waited for at the end
 * of each round) and, on GLES, the GPU ms of the render (timer queries) */
static void timing(int n) {
  t3_renderer *rr[2];
  t3_scene *ss[2];
  t3_camera *cam = t3_perspective_camera_new(50, (float)W / H, 0.5f, 100);
  t3_object_set_position(cam, 0, 9, 13);
  t3_object_look_at(cam, 0, 0, 0);
  for (int k = 0; k < 2; k++) {
    if (k) setenv("T3_TEST_CAST_ONE_BY_ONE", "1", 1); else unsetenv("T3_TEST_CAST_ONE_BY_ONE");
    rr[k] = renderer();
    t3_renderer_set_shadow_map(rr[k], true, T3_PCF_SHADOW_MAP);
#ifndef T3_WGPU
    t3_renderer_set_gpu_timing(rr[k], true);
#endif
    ss[k] = scene_new();
  }
  static uint8_t px[W * H * 4];
  double cpu[2] = { 0, 0 }, gpu[2] = { 0, 0 };
  int gn[2] = { 0, 0 };
  for (int round = 0; round < n / 100; round++)
    for (int k = 0; k < 2; k++) {
      double t0 = now_ms();
      for (int i = 0; i < 100; i++) {
        t3_renderer_shadow_needs_update(rr[k]);   /* (the map drawn every frame: its cache would skip an unchanged one) */
        t3_renderer_render(rr[k], ss[k], cam);
#ifdef T3_WGPU
        t3_renderer_wgpu_submit(rr[k]);
#endif
        const t3_render_info *inf = t3_renderer_info(rr[k]);
        if (inf->gpu_timer_supported > 0 && inf->gpu_ms > 0) gpu[k] += inf->gpu_ms, gn[k]++;
      }
      read_frame(rr[k], px);
      cpu[k] += now_ms() - t0;
    }
  int frames = n / 100 * 100;
  printf("timing (%s, %d frames each): instanced casters %.3f ms/frame CPU%s", BACKEND, frames, cpu[0] / frames, gn[0] ? "" : "\n");
  if (gn[0]) printf(", %.3f ms GPU\n", gpu[0] / gn[0]);
  printf("timing (%s): casters one by one %.3f ms/frame CPU%s", BACKEND, cpu[1] / frames, gn[1] ? "" : "\n");
  if (gn[1]) printf(", %.3f ms GPU\n", gpu[1] / gn[1]);
}

int main(void) {
  bool control = getenv("T3_TEST_CONTROL") != NULL;
  int st = setup();
  if (st == 77) { puts("test_cast_batch (" BACKEND "): no WebGPU adapter, skipped"); return 77; }
  if (st) return st;
  if (getenv("T3_TEST_TIME")) { timing(atoi(getenv("T3_TEST_TIME"))); return 0; }
  static uint8_t a[W * H * 4], b[W * H * 4];
  unsigned sa, sb;
  char ea[256], eb[256];
  unsigned ca = frame(control, a, &sa, ea, sizeof ea), cb = frame(true, b, &sb, eb, sizeof eb);
  int off = 0, maxd = 0, shadowed = 0;
  for (int i = 0; i < W * H; i++) {
    bool o = false;
    for (int c = 0; c < 3; c++) { int d = abs(a[i * 4 + c] - b[i * 4 + c]); if (d > maxd) maxd = d; o |= d > 2; }
    off += o;
  }
  /* the boxes cast: some ground texels are in shadow (darker than the lit ground), whatever the rows order */
  for (int i = 0; i < W * H; i++) shadowed += a[i * 4 + 1] < 60;
  const char *dump = getenv("T3_TEST_DUMP");
  if (dump) {
    FILE *fp = fopen(dump, "wb");
    if (fp) { fprintf(fp, "P6\n%d %d\n255\n", W, H); for (int i = 0; i < W * H; i++) fwrite(a + i * 4, 1, 3, fp); fclose(fp); }
  }
  printf("test_cast_batch (%s%s): %u draws batched, %u one by one; pictures %d texels apart (max %d); %d dark texels; skipped %u / %u%s%s\n",
         BACKEND, control ? ", control" : "", ca, cb, off, maxd, shadowed, sa, sb, *ea ? ": " : "", ea);
  int fail = 0;
  if (sa || sb) fail = 1;
  if (ca + 150 > cb) fail = 1;     /* the 200 casters: one draw, not 200 */
  if (off > W * H / 1000) fail = 1;
  if (fail) fprintf(stderr, "FAIL (" BACKEND "%s)\n", control ? ", control" : "");
  return fail;
}
