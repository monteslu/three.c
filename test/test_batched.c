/* BatchedMesh: 60 instances of three geometries in a 10 x 6 grid draw the
 * same picture as 60 separate meshes with the same matrices, in a handful of
 * draws: the visible instances that follow each other in the buffer are one
 * draw (t3_batched_mesh_last_draws). Checked: everything in view is one run;
 * a hidden instance and a deleted one split it into three and are not
 * drawn; a camera that sees only part of the grid draws fewer instances
 * (per-instance frustum culling) and still matches the separate meshes.
 *
 * The control (T3_TEST_CONTROL=1) hides a different instance in the batch
 * than in the separate meshes: the pictures must differ.
 *
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_batched.c \
 *     build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o build/test/test_batched
 *   EGL_PLATFORM=surfaceless ./build/test/test_batched */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "three.h"
#define W 256
#define H 160
#define N 60

static unsigned char px[2][W * H * 4];
static t3_mat4 mats[N];
static t3_geometry *geos[3];

static void matrices(void) {
  for (int k = 0; k < N; k++) {
    float sc = 0.6f + 0.1f * (float)(k % 3);
    t3_mat4_compose(&mats[k], t3_v3((float)(k % 10 - 4.5) * 1.6f, (float)(k / 10 - 2.5) * 1.6f, 0),
                    t3_quat_from_euler((t3_euler){ 0.4f * (float)(k % 4), 0.5f * (float)(k % 5), 0, T3_XYZ }), t3_v3(sc, sc, sc));
  }
}
static void lights(t3_scene *s) {
  t3_light *a = t3_ambient_light_new(0x404040, 1), *d = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(d, 2, 3, 5);
  t3_object_add(&s->base, a); t3_object_add(&s->base, d);
  t3_release(a); t3_release(d);
}
/* batched: one BatchedMesh; else N meshes. hide / gone: instances not drawn (-1 none). cam_x: the camera's x */
static int bridge;
static void draw(t3_renderer *r, bool batched, int hide, int gone, float cam_x, float half, unsigned char *out, int *runs, int *insts) {
  t3_scene *s = t3_scene_new();
  lights(s);
  t3_material *m = t3_mesh_standard_material_new(0xc08040);
  if (batched) {
    t3_batched_mesh *b = t3_batched_mesh_new(N, 0, 0, m);
    b->bridge_indices = bridge;
    int ids[3];
    for (int k = 0; k < 3; k++) ids[k] = t3_batched_mesh_add_geometry(b, geos[k]);
    for (int k = 0; k < N; k++) {
      int id = t3_batched_mesh_add_instance(b, ids[k % 3]);
      t3_batched_mesh_set_matrix_at(b, id, &mats[k]);
    }
    if (hide >= 0) t3_batched_mesh_set_visible_at(b, hide, false);
    if (gone >= 0) t3_batched_mesh_delete_instance(b, gone);
    t3_object_add(&s->base, b);
    t3_camera *cam = t3_orthographic_camera_new(-half, half, half * H / W, -half * H / W, 0.1f, 50);
    t3_object_set_position(cam, cam_x, 0, 10);
    t3_renderer_set_clear_color(r, 0x101020, 1);
    t3_renderer_render(r, s, cam);
    t3_batched_mesh_last_draws(b, runs, insts);
    t3_release(cam); t3_release(b);
  } else {
    for (int k = 0; k < N; k++) {
      if (k == hide || k == gone) continue;
      t3_mesh *mesh = t3_mesh_new(geos[k % 3], m);
      mesh->base.matrix = mats[k];
      mesh->base.matrix_auto_update = false;
      mesh->base.matrix_world_needs_update = true;
      t3_object_add(&s->base, mesh);
      t3_release(mesh);
    }
    t3_camera *cam = t3_orthographic_camera_new(-half, half, half * H / W, -half * H / W, 0.1f, 50);
    t3_object_set_position(cam, cam_x, 0, 10);
    t3_renderer_set_clear_color(r, 0x101020, 1);
    t3_renderer_render(r, s, cam);
    t3_release(cam);
  }
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
  t3_release(m); t3_release(s);
}
static int differ(void) {
  int n = 0;
  for (int i = 0; i < W * H * 4; i++) n += abs((int)px[0][i] - (int)px[1][i]) > 3;
  return n;
}
static int lit(const unsigned char *p) {
  int n = 0;
  for (int i = 0; i < W * H; i++) n += p[i * 4] > 60;
  return n;
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
  geos[0] = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  geos[1] = t3_sphere_geometry_new(0.6f, 16, 10);
  geos[2] = t3_cone_geometry_new(0.55f, 1.1f, 14, 1, false);
  matrices();
  int fails = 0, runs = 0, insts = 0;
  struct { const char *name; int hide, gone; float cam_x, half; int want_runs, want_insts; } cases[] = {
    { "all in view", -1, -1, 0, 9, 1, N },
    { "one hidden, one deleted", 17, 42, 0, 9, 3, N - 2 },
    { "the left part in view", -1, -1, -6, 3.2f, -1, -1 },
  };
  for (int c = 0; c < 3; c++) {
    t3_renderer *r = t3_renderer_new(W, H);
    draw(r, false, cases[c].hide, cases[c].gone, cases[c].cam_x, cases[c].half, px[0], NULL, NULL);
    t3_renderer_destroy(r);
    r = t3_renderer_new(W, H);
    draw(r, true, control && cases[c].hide >= 0 ? cases[c].hide + 1 : cases[c].hide, cases[c].gone, cases[c].cam_x, cases[c].half, px[1], &runs, &insts);
    if (t3_renderer_info(r)->skipped) { fprintf(stderr, "FAIL %s: skipped (%s)\n", cases[c].name, t3_renderer_last_error(r)); fails++; }
    t3_renderer_destroy(r);
    int df = differ(), on = lit(px[1]);
    if (on < 500) { fprintf(stderr, "FAIL %s: the batch drew %d lit pixels\n", cases[c].name, on); fails++; }
    if (df > 8) { fprintf(stderr, "FAIL %s: %d channel values differ from the separate meshes\n", cases[c].name, df); fails++; }
    if (cases[c].want_runs > 0 && (runs != cases[c].want_runs || insts != cases[c].want_insts)) {
      fprintf(stderr, "FAIL %s: %d draws of %d instances, want %d of %d\n", cases[c].name, runs, insts, cases[c].want_runs, cases[c].want_insts); fails++;
    }
    /* culled: only the instances the camera can see, in few draws */
    if (cases[c].want_runs < 0 && !(insts > 0 && insts <= 30 && runs <= 6)) { fprintf(stderr, "FAIL %s: %d draws of %d instances (per-instance culling)\n", cases[c].name, runs, insts); fails++; }
    printf("%s: %d draw(s), %d instances, %d values differ\n", cases[c].name, runs, insts, df);
  }
  /* bridge_indices: the runs of the part view join over the out-of-view instances between them (same picture, one draw) */
  {
    bridge = 1 << 20;
    t3_renderer *r = t3_renderer_new(W, H);
    draw(r, false, -1, -1, -6, 3.2f, px[0], NULL, NULL);
    t3_renderer_destroy(r);
    r = t3_renderer_new(W, H);
    draw(r, true, -1, -1, -6, 3.2f, px[1], &runs, &insts);
    t3_renderer_destroy(r);
    int df = differ();
    if (runs != 1 || df > 8) { fprintf(stderr, "FAIL bridged: %d draws, %d values differ\n", runs, df); fails++; }
    printf("the left part in view, bridged: %d draw(s), %d values differ\n", runs, df);
    bridge = 0;
  }
  if (fails) return 1;
  printf("test_batched: ok\n");
  return 0;
}
