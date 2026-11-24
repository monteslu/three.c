/* t3_renderer_set_cover: a state no table has draws with the nearest program that covers it.
 *
 * The test links three.c's core table and registers parts of the full table (compiled in under another name) as a
 * project table, so each case has exactly the programs it names:
 *   1. exact: a Lambert mesh under one directional light (state lambert+l1000, not in the table) drawn with
 *      lambert+fog (l1100: one more light and fog the scene has none of, made inert) must come out byte-identical to
 *      the same frame drawn with lambert+l1000 itself, and be listed as covered (exact);
 *   2. approximate: the same mesh receiving a shadow (lambert+shadow+l1000) with only lambert+l1000 in the table draws
 *      unshadowed, listed as covered (not exact);
 *   3. an extension: a material with extension "fake" (no program has it) draws with the plain program when the
 *      embedder's cover_ext accepts dropping it, and its caster (depth+xfake) with the plain caster;
 *   4. never covered: a mesh with a map (lambert+map+nearest+l1000) and only map-less programs stays missing and
 *      undrawn;
 *   control (T3_TEST_CONTROL=1): cover off, so cases 1 to 3 are skipped draws and the checks must fail.
 *
 * GLES:   cc ... -I include -I src test/test_cover.c full_gl.o build/native_core/libthree.a -lEGL -lGLESv2 -lz -lm
 *         (full_gl.o: src/gen/programs_gl.c built with -Dt3_gen_base_gl=t3_full_gl)
 * WebGPU: -DT3_WGPU, build/native-wgpu_core/libthree.a, full_wgpu.o (src/gen/programs_wgpu.c with
 *         -Dt3_gen_base_wgpu=t3_full_wgpu) and Dawn. test/run.sh builds both.
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
extern const t3_gen_table t3_full_wgpu;
#define FULL t3_full_wgpu
static WGPUInstance inst;
static void adapter_cb(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUAdapter *)u1 = st == WGPURequestAdapterStatus_Success ? a : NULL; *(int *)u2 = 1;
}
static void device_cb(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUDevice *)u1 = st == WGPURequestDeviceStatus_Success ? d : NULL; *(int *)u2 = 1;
}
static void error_cb(WGPUDevice const *d, WGPUErrorType t, WGPUStringView m, void *u1, void *u2) {
  (void)d; (void)u1; (void)u2;
  fprintf(stderr, "webgpu error %d: %.*s\n", (int)t, (int)m.length, m.data);
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
static void finish(t3_renderer *r, uint8_t *px) { t3_renderer_wgpu_submit(r); t3_renderer_wgpu_read(r, px); }
#define BACKEND "webgpu"
#else
#include <EGL/egl.h>
#include <GLES3/gl3.h>
extern const t3_gen_table t3_full_gl;
#define FULL t3_full_gl
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
static void finish(t3_renderer *r, uint8_t *px) { (void)r; glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); }
#define BACKEND "gles"
#endif

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL (" BACKEND "): "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* the project table: registered once, holding the named programs of the full table for each case (a new renderer
 * each case, so no lookup of a case before survives into the next) */
static t3_gen_table project;
static t3_gen_program picked[4];
static const t3_gen_table *subset(const char *const *names, int n) {
  int k = 0;
  for (int i = 0; i < n; i++)
    for (unsigned j = 0; j < FULL.count; j++)
      if (!strcmp(FULL.programs[j].state, names[i])) { picked[k++] = FULL.programs[j]; break; }
  if (k != n) { fprintf(stderr, "the full table lacks a program this test names\n"); exit(2); }
  project.programs = picked;
  project.count = (unsigned)k;
  return &project;
}
static void use(const t3_gen_table *t) {
  static bool registered;
  if (registered) return;
  registered = true;
#ifdef T3_WGPU
  t3_register_program_table(NULL, t);
#else
  t3_register_program_table(t, NULL);
#endif
}

static int ext_calls;
static int cover_ext(void *user, const char *want, const char *have, bool caster) {
  (void)user;
  ext_calls++;
  /* "fake" may be dropped: exactly for a caster (it moves nothing), approximately for a colour program */
  if (!strcmp(want, "fake") && !*have) return caster ? 0 : 1;
  return -1;
}
static char seen[4][256];
static bool seen_exact[4];
static int seen_n;
static void cover_hook(void *user, const char *state, const char *used, bool exact, const t3_object *o, const t3_material *m) {
  (void)user; (void)o; (void)m;
  if (seen_n < 4) { snprintf(seen[seen_n], sizeof seen[0], "%s %s", state, used); seen_exact[seen_n++] = exact; }
}

static bool control;
/* one frame of a lit mesh: map, fog, shadow and extension as asked; the frame's pixels in px */
static void frame(const t3_gen_table *table, bool map, bool shadow, const char *ext, uint8_t *px, char *missing, char *covered) {
  use(table);
  t3_renderer *r = renderer();
  t3_renderer_set_clear_color(r, 0x000000, 1);
#ifndef T3_WGPU
  t3_renderer_set_exact_output(r, true);
#endif
  seen_n = 0;
  if (!control) t3_renderer_set_cover(r, true, cover_ext, cover_hook, NULL);
  t3_scene *s = t3_scene_new();
  t3_camera *cam = t3_perspective_camera_new(50, 1, 0.1f, 20);
  t3_object_set_position(cam, 0, 1.5f, 4);
  t3_object_look_at(cam, 0, 0, 0);
  t3_material *m = t3_mesh_lambert_material_new(0xd0a060);
  t3_texture *tex = NULL;
  if (map) { static uint8_t white[4] = { 255, 255, 255, 255 }; tex = t3_data_texture_new(1, 1, white); m->map = tex; }
  if (ext) t3_material_set_extension(m, ext);
  t3_mesh *ball = t3_mesh_new(t3_sphere_geometry_new(1, 24, 12), m);
  ball->base.receive_shadow = shadow;
  ball->base.cast_shadow = shadow;
  t3_object_add(&s->base, ball);
  t3_light *sun = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(sun, 2, 4, 3);
  t3_object_add(&s->base, sun);
  if (shadow) {
    t3_renderer_set_shadow_map(r, true, T3_PCF_SHADOW_MAP);
    sun->base.cast_shadow = true;
    /* a block above the ball that casts on it */
    t3_mesh *block = t3_mesh_new(t3_box_geometry_new(0.6f, 0.2f, 0.6f, 1, 1, 1), t3_mesh_lambert_material_new(0xffffff));
    t3_object_set_position(block, 0.5f, 1.6f, 0.7f);
    block->base.cast_shadow = true;
    t3_object_add(&s->base, block);
  }
  t3_renderer_render(r, s, cam);
  finish(r, px);
  snprintf(missing, 512, "%s", t3_renderer_generated_missing(r));
  snprintf(covered, 512, "%s", t3_renderer_generated_covered(r));
  t3_renderer_destroy(r);
}

static int lit(const uint8_t *px) {
  int n = 0;
  for (int i = 0; i < W * H; i++) n += px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2] > 30;
  return n;
}

int main(void) {
  int st = setup();
  if (st == 77) { puts("test_cover (" BACKEND "): no WebGPU adapter, skipped"); return 77; }
  if (st) return st;
  control = getenv("T3_TEST_CONTROL") != NULL;
  static uint8_t ref[W * H * 4], px[W * H * 4];
  char missing[512], covered[512];

  /* 1. exact: the reference with the state itself, then the covering program */
  frame(subset((const char *[]){ "lambert+l1000" }, 1), false, false, NULL, ref, missing, covered);
  CHECK(!*missing && lit(ref) > 50, "the reference frame: missing \"%s\", %d lit pixels", missing, lit(ref));
  frame(subset((const char *[]){ "lambert+fog" }, 1), false, false, NULL, px, missing, covered);
  int diff = 0;
  for (int i = 0; i < W * H * 4; i++) diff += ref[i] != px[i];
  CHECK(!diff, "lambert+l1000 drawn with lambert+fog differs from its own program in %d bytes", diff);
  CHECK(!strcmp(covered, "lambert+l1000 lambert+fog\n") && seen_n == 1 && seen_exact[0] && !*missing,
        "case 1: covered \"%s\" (exact %d), missing \"%s\"", covered, seen_n ? seen_exact[0] : -1, missing);
  printf("1. lambert+l1000 drawn with lambert+fog: %d bytes differ from its own program\n", diff);

  /* 2. approximate: a received shadow the only program lacks */
  frame(subset((const char *[]){ "lambert+l1000" }, 1), false, true, NULL, px, missing, covered);
  CHECK(strstr(covered, "lambert+shadow+l1000 lambert+l1000\n") && !strstr(missing, "lambert") && lit(px) > 50 && seen_n >= 1 && !seen_exact[0],
        "case 2: covered \"%s\", missing \"%s\", %d lit pixels", covered, missing, lit(px));
  printf("2. lambert+shadow+l1000 drawn with lambert+l1000 (approximate): %d lit pixels\n", lit(px));

  /* 3. an extension the embedder lets go of, colour and caster */
  ext_calls = 0;
  frame(subset((const char *[]){ "lambert+l1000", "lambert+shadow+l1000" }, 2), false, true, "fake", px, missing, covered);
  CHECK(strstr(covered, "lambert+shadow+l1000+xfake lambert+shadow+l1000\n") && strstr(covered, "depth+xfake depth\n") && !*missing && lit(px) > 50,
        "case 3: covered \"%s\", missing \"%s\", %d lit pixels", covered, missing, lit(px));
  printf("3. an extension dropped: covered %s", covered);

  /* 4. a map no program has is never covered */
  frame(subset((const char *[]){ "lambert+l1000", "lambert+fog" }, 2), true, false, NULL, px, missing, covered);
  CHECK(!strncmp(missing, "lambert+map+", 12) && !strchr(missing, ' ') && !*covered && !lit(px), "case 4: missing \"%s\", covered \"%s\", %d lit pixels", missing, covered, lit(px));
  printf("4. a mapped Lambert with map-less programs: missing (%.*s), not drawn\n", (int)strcspn(missing, "\n"), missing);

  if (failures) fprintf(stderr, "test_cover (" BACKEND "): %d failure(s)%s\n", failures, control ? " (control: expected)" : "");
  else printf("test_cover (" BACKEND "): ok\n");
  return failures ? 1 : 0;
}
