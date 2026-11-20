/* MeshToonMaterial (r187 ToonLightingModel): a sphere under one directional
 * light shows as many tones as its gradientMap has texels (two without one:
 * r187's 0.7 and 1), emissive adds on top, and a gradientMap that is not
 * NearestFilter is refused with the reason (three.js asks for NearestFilter;
 * the program reads the map that way).
 *
 * The control (T3_TEST_CONTROL=1) draws the same spheres with
 * MeshLambertMaterial: a smooth falloff has far more than three tones, so the
 * counts must fail.
 *
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_toon.c \
 *     build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o build/test/test_toon
 *   EGL_PLATFORM=surfaceless ./build/test/test_toon
 *
 * Exits 0 when every check holds. */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "three.h"
#define W 128
#define H 128

static unsigned char px[W * H * 4];
static bool control;

/* a grey sphere, lit from the side (every tone from facing the light to facing away shows); steps: gradient texels (0: no map); returns the
 * number of distinct red levels on the sphere that each cover at least 1% of it */
static int tones(t3_renderer *r, int steps, t3_filter filter, uint32_t emissive, int *brightest) {
  t3_scene *s = t3_scene_new();
  t3_geometry *g = t3_sphere_geometry_new(1, 64, 32);
  t3_material *m = control ? t3_mesh_lambert_material_new(0xffffff) : t3_mesh_toon_material_new(0xffffff);
  if (steps && !control) {
    uint8_t d[8 * 4];
    for (int i = 0; i < steps; i++) { d[i * 4] = d[i * 4 + 1] = d[i * 4 + 2] = (uint8_t)((i + 1) * 255 / steps); d[i * 4 + 3] = 255; }
    t3_texture *t = t3_data_texture_new(steps, 1, d);
    t->mag_filter = t->min_filter = filter;
    t3_material_set_texture(m, T3_GRADIENT_MAP, t);
    t3_release(t);
  }
  m->emissive = t3_color_hex(emissive);
  t3_mesh *mesh = t3_mesh_new(g, m);
  t3_object_add(&s->base, mesh);
  t3_light *sun = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(sun, 5, 0, 1);
  t3_object_add(&s->base, sun);
  t3_camera *cam = t3_orthographic_camera_new(-1.05f, 1.05f, 1.05f, -1.05f, 0.1f, 10);
  t3_object_set_position(cam, 0, 0, 5);
  t3_renderer_set_clear_color(r, 0x0000ff, 1);
  t3_renderer_render(r, s, cam);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
  int hist[256] = { 0 }, total = 0, n = 0;
  *brightest = 0;
  for (int i = 0; i < W * H; i++) {
    const unsigned char *p = px + i * 4;
    if (p[2] > 250 && p[0] < 5) continue;   /* the blue clear */
    hist[p[0]]++; total++;
  }
  for (int v = 0; v < 256; v++) if (hist[v] * 100 >= total && total) { n++; *brightest = v; }
  t3_release(mesh); t3_release(g); t3_release(m); t3_release(sun); t3_release(cam); t3_release(s);
  return n;
}

int main(void) {
  control = getenv("T3_TEST_CONTROL") != NULL;
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(d, NULL, NULL)) return fprintf(stderr, "eglInitialize failed\n"), 2;
  EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
  EGLConfig cfg;
  EGLint n = 0;
  if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) return fprintf(stderr, "no config\n"), 2;
  EGLint sa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
  EGLSurface surf = eglCreatePbufferSurface(d, cfg, sa);
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint cxa[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cxa);
  if (!eglMakeCurrent(d, surf, surf, ctx)) return fprintf(stderr, "makeCurrent failed\n"), 2;

  int fails = 0, top = 0, top_e = 0, got[3];
  static const int steps[3] = { 0, 3, 5 }, want[3] = { 2, 3, 5 };
  t3_renderer *r = t3_renderer_new(W, H);
  for (int i = 0; i < 3; i++) {
    got[i] = tones(r, steps[i], T3_NEAREST, 0, &top);
    if (t3_renderer_info(r)->skipped) { fprintf(stderr, "FAIL %d steps: skipped (%s)\n", steps[i], t3_renderer_last_error(r)); fails++; }
    if (got[i] != want[i]) { fprintf(stderr, "FAIL gradient of %d texels: %d tones, want %d\n", steps[i], got[i], want[i]); fails++; }
  }
  /* emissive adds to every tone: the brightest one gets brighter in red */
  tones(r, 3, T3_NEAREST, 0, &top);
  tones(r, 3, T3_NEAREST, 0x400000, &top_e);
  if (!control && top_e <= top) { fprintf(stderr, "FAIL emissive: brightest tone %d, without %d\n", top_e, top); fails++; }
  t3_renderer_destroy(r);
  /* a linear gradientMap is refused, and the error says why */
  r = t3_renderer_new(W, H);
  int none = 0;
  tones(r, 3, T3_LINEAR, 0, &none);
  const char *err = t3_renderer_last_error(r);
  if (!control && !(t3_renderer_info(r)->skipped > 0 && err && strstr(err, "NearestFilter"))) {
    fprintf(stderr, "FAIL a linear gradientMap was not refused (%s)\n", err ? err : "no error"); fails++;
  }
  t3_renderer_destroy(r);
  if (fails) return 1;
  printf("test_toon: tones %d / %d / %d for no map, 3 and 5 texels; emissive %d > %d; a linear gradientMap refused, ok\n", got[0], got[1], got[2], top_e, top);
  return 0;
}
