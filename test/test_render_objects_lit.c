/* t3_renderer_render_objects_lit: a few lit meshes of a scene drawn as the scene's render would light them. A scene of
 * 60 boxes (MeshStandardMaterial, a colour each) on a ground plane, an ambient light and a directional light casting
 * shadows (PCF); every third box is the set. After a render of the whole scene (the
 * shadow maps drawn from all of it), the set is drawn two ways, with the shadow maps held, and must give the same
 * picture byte for byte, at frames 1, 20 and 40:
 *   - the scene with everything outside the set hidden, through t3_renderer_render;
 *   - the whole scene left visible, through t3_renderer_render_objects_lit with the set.
 * The picture must be one (a share of pixels unlike the clear colour), and the set must be lit: its pixels differ
 * from what t3_renderer_render_objects (unlit: no lights) draws. The control (T3_TEST_CONTROL=1) draws the set
 * with t3_renderer_render_objects instead and must fail. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "three.h"

#define W 192
#define H 144
#define N 60
#define FRAMES 40

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

static t3_scene *s;
static t3_mesh *box[N], *ground;
static t3_camera *cam;
static t3_object *set[N], *all[N + 1];
static int nset, nall;

static void world(void) {
  s = t3_scene_new();
  t3_light *amb = t3_ambient_light_new(0x404858, 0.6f);
  t3_object_add(&s->base, amb);
  t3_release(amb);
  t3_light *sun = t3_directional_light_new(0xfff0e0, 2.5f);
  t3_object_set_position(sun, 4, 9, 3);
  sun->base.cast_shadow = true;
  t3_object_add(&s->base, sun);
  t3_release(sun);
  t3_geometry *pg = t3_plane_geometry_new(30, 30, 1, 1);
  t3_material *pm = t3_mesh_standard_material_new(0x808070);
  ground = t3_mesh_new(pg, pm);
  t3_object_set_rotation(ground, -1.5707963f, 0, 0);
  ground->base.receive_shadow = true;
  t3_object_add(&s->base, ground);
  t3_release(ground);
  t3_release(pm);
  t3_release(pg);
  all[nall++] = &ground->base;
  t3_geometry *g = t3_box_geometry_new(0.5f, 0.9f, 0.5f, 1, 1, 1);
  for (int i = 0; i < N; i++) {
    t3_material *m = t3_mesh_standard_material_new(0x305070u + (uint32_t)i * 0x030201u);
    box[i] = t3_mesh_new(g, m);
    box[i]->base.cast_shadow = box[i]->base.receive_shadow = true;
    t3_object_add(&s->base, box[i]);
    t3_release(box[i]);
    t3_release(m);
    all[nall++] = &box[i]->base;
    if (i % 3 == 0) set[nset++] = &box[i]->base;
  }
  t3_release(g);
  cam = t3_perspective_camera_new(50, (float)W / H, 0.5f, 60);
  t3_object_set_position(cam, 0, 6, 11);
  t3_object_look_at(cam, 0, 0, 0);
}

static void pose(int f) {
  for (int i = 0; i < N; i++) {
    float x = (float)(i % 10) * 1.0f - 4.5f, z = (float)(i / 10) * 1.1f - 3.0f;
    t3_object_set_position(box[i], x, 0.45f + 0.4f * (float)((f * 5 + i * 11) % 13) / 13.0f, z);
    t3_object_set_rotation(box[i], 0, (float)((f + i) % 23) * 0.25f, 0);
  }
  t3_object_update_matrix_world(s, false);
}

static bool in_set(const t3_object *o) {
  for (int i = 0; i < nset; i++) if (set[i] == o) return true;
  return false;
}

int main(void) {
  bool control = getenv("T3_TEST_CONTROL") != NULL;
  int st = setup();
  if (st) return st;
  world();
  t3_renderer *r = t3_renderer_new(W, H);
  t3_renderer_set_clear_color(r, 0x102030, 1);
  t3_renderer_set_shadow_map(r, true, T3_PCF_SHADOW_MAP);
  static uint8_t a[W * H * 4], b[W * H * 4], u[W * H * 4];
  int fail = 0;
  for (int f = 1; f <= FRAMES; f++) {
    pose(f);
    if (f != 1 && f != FRAMES / 2 && f != FRAMES) continue;
    /* the frame's shadow maps, from the whole scene */
    t3_renderer_set_shadow_hold(r, false);
    t3_renderer_render(r, s, cam);
    t3_renderer_set_shadow_hold(r, true);
    /* the whole-scene way: everything outside the set hidden */
    for (int i = 0; i < nall; i++) all[i]->visible = in_set(all[i]);
    t3_renderer_render(r, s, cam);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, a);
    for (int i = 0; i < nall; i++) all[i]->visible = true;
    /* the set alone, lit (the control: unlit) */
    if (control) t3_renderer_render_objects(r, s, cam, set, nset);
    else t3_renderer_render_objects_lit(r, s, cam, set, nset);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, b);
    /* what the unlit way draws, to prove the light shows */
    t3_renderer_render_objects(r, s, cam, set, nset);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, u);
    t3_renderer_set_shadow_hold(r, false);
    int diff = 0, lit = 0;
    unsigned drawn = 0;
    for (int i = 0; i < W * H * 4; i++) diff += a[i] != b[i], lit += a[i] != u[i];
    for (int i = 0; i < W * H * 4; i += 4) drawn += memcmp(a + i, a, 4) != 0;
    printf("frame %d: %d of %d meshes drawn, %d bytes differ, %d bytes unlike the unlit draw, %u pixels unlike the first\n", f, nset,
           nall, diff, lit, drawn);
    if (diff) fail = 1;
    if (lit < W * H / 10) printf("frame %d: the set does not look lit\n", f), fail = 1;
    if (drawn < (unsigned)(W * H / 20)) printf("frame %d: not a picture\n", f), fail = 1;
  }
  if (t3_renderer_info(r)->skipped) printf("%u draws skipped (a program missing)\n", t3_renderer_info(r)->skipped), fail = 1;
  const char *e = t3_renderer_last_error(r);
  if (e && *e) printf("renderer error: %s\n", e), fail = 1;
  t3_renderer_destroy(r);
  t3_release(cam);
  t3_release(s);
  return fail;
}
