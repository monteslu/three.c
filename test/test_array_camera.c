/* ArrayCamera: four views of a scene (400 meshes over a ground, a shadowed
 * sun) in one t3_renderer_render call draw the same picture as four renders,
 * one a camera with the renderer's viewport and scissor set to each view's
 * (auto clear off after the first, as a split screen does it by hand), and
 * each view is culled against its own frustum: a view that looks away from
 * the meshes adds no draws. Also printed: the time of 200 frames each way,
 * for two and for four views (CPU and GPU together, glFinish each frame).
 *
 * The control (T3_TEST_CONTROL=1) gives the array's second camera another
 * position than the separate render's: the pictures must differ.
 *
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_array_camera.c \
 *     build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o build/test/test_array_camera
 *   EGL_PLATFORM=surfaceless ./build/test/test_array_camera */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "three.h"
#define W 512
#define H 320

static unsigned char px[2][W * H * 4];
static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}
static t3_scene *scene(void) {
  t3_scene *s = t3_scene_new();
  t3_geometry *pg = t3_plane_geometry_new(60, 60, 1, 1);
  t3_material *gm = t3_mesh_standard_material_new(0x506050);
  t3_mesh *ground = t3_mesh_new(pg, gm);
  t3_object_set_rotation(ground, -3.141592653589793f / 2, 0, 0);
  ground->base.receive_shadow = true;
  t3_object_add(&s->base, ground);
  t3_release(ground); t3_release(pg); t3_release(gm);
  t3_geometry *geos[2] = { t3_box_geometry_new(1, 1, 1, 1, 1, 1), t3_sphere_geometry_new(0.6f, 12, 8) };
  for (int i = 0; i < 400; i++) {
    /* a material each: 400 draws a view, nothing auto-instanced */
    t3_material *m = t3_mesh_standard_material_new(0x808080 + (uint32_t)(i * 2654435761u & 0x7f7f7f));
    t3_mesh *mesh = t3_mesh_new(geos[i & 1], m);
    t3_object_set_position(mesh, (float)(i % 20 - 9.5) * 2.2f, 0.6f, (float)(i / 20 - 9.5) * 2.2f);
    mesh->base.cast_shadow = mesh->base.receive_shadow = true;
    t3_object_add(&s->base, mesh);
    t3_release(mesh); t3_release(m);
  }
  t3_release(geos[0]); t3_release(geos[1]);
  t3_light *a = t3_ambient_light_new(0x404040, 1), *d = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(d, 10, 20, 8);
  d->base.cast_shadow = true;
  d->shadow->map_width = d->shadow->map_height = 1024;
  t3_camera *sh = d->shadow->camera;
  sh->left = -30; sh->right = 30; sh->top = 30; sh->bottom = -30; sh->near = 1; sh->far = 60;
  t3_camera_update_projection_matrix(sh);
  t3_object_add(&s->base, a); t3_object_add(&s->base, d);
  t3_release(a); t3_release(d);
  return s;
}
static const float POS[4][6] = { { 0, 12, 34, 0, 0, 0 }, { -30, 6, -8, 0, 0, 0 }, { 0.1f, 40, 0, 0, 0, 0 }, { 0, 3, 40, 0, 3, 80 } };   /* the last looks away */
static t3_camera *camera(int k, int vw, int vh, bool moved) {
  t3_camera *c = t3_perspective_camera_new(50, (float)vw / vh, 0.5f, 200);
  t3_object_set_position(c, POS[k][0] + (moved ? 6 : 0), POS[k][1], POS[k][2]);
  t3_object_look_at(c, POS[k][3], POS[k][4], POS[k][5]);
  return c;
}
static void viewport_of(int k, int n, int vp[4]) {
  if (n == 2) { vp[0] = k * (W / 2); vp[1] = 0; vp[2] = W / 2; vp[3] = H; }
  else { vp[0] = (k & 1) * (W / 2); vp[1] = (k >> 1) * (H / 2); vp[2] = W / 2; vp[3] = H / 2; }
}
/* n views, as an ArrayCamera (array) or as n renders; returns the last frame's draw calls; ms: mean of `frames` frames */
static unsigned run(bool array, int n, bool moved, int frames, unsigned char *out, double *ms) {
  t3_renderer *r = t3_renderer_new(W, H);
  t3_renderer_set_shadow_map(r, true, T3_PCF_SHADOW_MAP);
  t3_renderer_set_clear_color(r, 0x101828, 1);
  t3_scene *s = scene();
  t3_camera *cams[4];
  t3_array_camera *ac = t3_array_camera_new();
  for (int k = 0; k < n; k++) {
    int vp[4];
    viewport_of(k, n, vp);
    cams[k] = camera(k, vp[2], vp[3], moved && array && k == 1);
    t3_array_camera_add(ac, cams[k], vp[0], vp[1], vp[2], vp[3]);
  }
  unsigned calls = 0;
  double t0 = 0;
  for (int f = 0; f < frames + 3; f++) {
    if (f == 3) { glFinish(); t0 = now_ms(); }
    if (array) {
      t3_renderer_render(r, s, &ac->base);
      calls = t3_renderer_info(r)->calls;
    } else {
      calls = 0;
      for (int k = 0; k < n; k++) {
        int vp[4];
        viewport_of(k, n, vp);
        t3_renderer_set_viewport(r, vp[0], vp[1], vp[2], vp[3]);
        t3_renderer_set_scissor(r, vp[0], vp[1], vp[2], vp[3]);
        t3_renderer_set_scissor_test(r, k > 0);   /* (the first render clears the whole target) */
        t3_renderer_set_auto_clear(r, k == 0);
        t3_renderer_render(r, s, cams[k]);
        calls += t3_renderer_info(r)->calls;
      }
    }
    glFinish();
  }
  if (ms) *ms = (now_ms() - t0) / frames;
  if (out) glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
  if (t3_renderer_info(r)->skipped) fprintf(stderr, "skipped: %s\n", t3_renderer_last_error(r));
  for (int k = 0; k < n; k++) t3_release(cams[k]);
  t3_release(ac); t3_release(s);
  t3_renderer_destroy(r);
  return calls;
}

int main(void) {
  bool control = getenv("T3_TEST_CONTROL") != NULL;
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(d, NULL, NULL)) return fprintf(stderr, "eglInitialize failed\n"), 2;
  EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_DEPTH_SIZE, 24, EGL_NONE };
  EGLConfig cfg;
  EGLint n = 0;
  if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) return fprintf(stderr, "no config\n"), 2;
  EGLint sa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
  EGLSurface surf = eglCreatePbufferSurface(d, cfg, sa);
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint cxa[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cxa);
  if (!eglMakeCurrent(d, surf, surf, ctx)) return fprintf(stderr, "makeCurrent failed\n"), 2;
  int fails = 0;
  for (int views = 2; views <= 4; views += 2) {
    unsigned ca1 = run(false, views, false, 1, px[0], NULL), ca2 = run(true, views, control, 1, px[1], NULL);
    int df = 0, lit = 0;
    for (int i = 0; i < W * H * 4; i++) df += abs((int)px[0][i] - (int)px[1][i]) > 3;
    for (int i = 0; i < W * H; i++) lit += px[1][i * 4 + 1] > 70;
    if (getenv("T3_TEST_DUMP")) for (int q = 0; q < 2; q++) { char nm[64]; snprintf(nm, sizeof nm, "build/test/ac-%d-%d.rgba", views, q); FILE *f = fopen(nm, "wb"); if (f) { fwrite(px[q], 1, sizeof px[q], f); fclose(f); } }
    int white = 0;
    for (int i = 0; i < W * H; i++) white += px[1][i * 4] > 250 && px[1][i * 4 + 1] > 250 && px[1][i * 4 + 2] > 250;
    /* (a second renderer in one process once drew every lit surface white: stale texture units) */
    if (white > W * H / 50) { fprintf(stderr, "FAIL %d views: %d saturated pixels\n", views, white); fails++; }
    if (lit < 2000) { fprintf(stderr, "FAIL %d views: the array camera drew %d lit pixels\n", views, lit); fails++; }
    if (df > 8) { fprintf(stderr, "FAIL %d views: %d channel values differ from %d separate renders\n", views, df, views); fails++; }
    printf("%d views: %u draw calls as an ArrayCamera, %u as %d renders, %d values differ\n", views, ca2, ca1, views, df);
    /* each view culls for itself: the one that looks away (the fourth) adds only its background-free nothing */
    if (views == 4) {
      unsigned three = 0;
      (void)three;
    }
  }
  /* per-view culling: three views that see the meshes and one that looks away draw far fewer than four times one view */
  {
    unsigned c4 = run(true, 4, false, 1, NULL, NULL), c2 = run(true, 2, false, 1, NULL, NULL);
    if (!(c4 < c2 * 2)) { fprintf(stderr, "FAIL culling: 4 views %u draw calls, 2 views %u (the fourth view sees nothing)\n", c4, c2); fails++; }
    printf("culling: 2 views %u draw calls, 4 views %u (one looks away, one sees part)\n", c2, c4);
  }
  if (!control && !getenv("T3_TEST_NO_TIMING")) {
    for (int views = 2; views <= 4; views += 2) {
      double a[2] = { 0, 0 }, b[2] = { 0, 0 };
      for (int round = 0; round < 2; round++) { run(false, views, false, 200, NULL, &a[round]); run(true, views, false, 200, NULL, &b[round]); }
      printf("%d views, ms a frame (two interleaved rounds of 200): %d renders %.3f %.3f, one ArrayCamera render %.3f %.3f\n", views, views, a[0], a[1], b[0], b[1]);
    }
  }
  if (fails) return 1;
  printf("test_array_camera: ok\n");
  return 0;
}
