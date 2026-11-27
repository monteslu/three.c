/* The per-draw block ring (renderer_gen.inc gl_ring): every render call's and
 * every caster pass's per-draw blocks are suballocated from one uniform
 * buffer, with no orphaning glBufferData per call. 300 frames of three render
 * calls each (two views and a third renderer viewport) over 240 boxes with a
 * material each (one streamed block a draw), six skinned CesiumMan walkers
 * (Khronos glTF sample, CC-BY-4.0: their bone matrices go to the draw ring,
 * a slice a draw) and a shadow-casting light: the
 * uniform buffer stores made over the run (glBufferData on GL_UNIFORM_BUFFER,
 * counted by -Wl,--wrap=glBufferData) must be a handful, not one a call (900
 * renders and 900 caster passes), and the frames read back at frames 1, 150
 * and 300 must equal a fresh renderer's picture of the same state, so the
 * slices that wrapped round the ring held the right blocks. The control
 * (T3_TEST_CONTROL=1) sets T3_TEST_UBO_ORPHAN=1, the old way (a new store each
 * write): the frames still match and the count fails. glFlush ends each frame,
 * as a swap would. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "three.h"

#define W 192
#define H 144
#define N 240
#define FRAMES 300

static unsigned ubo_stores;
void __real_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void __wrap_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
  if (target == GL_UNIFORM_BUFFER) ubo_stores++;
  __real_glBufferData(target, size, data, usage);
}

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

#define WALKERS 6
typedef struct world {
  t3_scene *s; t3_mesh *box[N]; t3_camera *cam[2];
  t3_gltf *man[WALKERS]; t3_animation_mixer *mix[WALKERS];
} world;

static void *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  void *p = malloc((size_t)n);
  if (p && fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); p = NULL; }
  fclose(f);
  *len = (size_t)n;
  return p;
}

static void world_new(world *w) {
  w->s = t3_scene_new();
  t3_geometry *g = t3_box_geometry_new(0.4f, 0.9f, 0.4f, 1, 1, 1);
  for (int i = 0; i < N; i++) {
    /* a material each: no auto-instancing, one draw (and one streamed block) a box */
    t3_material *m = t3_mesh_standard_material_new(0x303030u + (uint32_t)i * 0x010305u);
    t3_mesh *b = t3_mesh_new(g, m);
    b->base.cast_shadow = b->base.receive_shadow = true;
    t3_object_add(&w->s->base, b);
    w->box[i] = b;
    t3_release(b);
    t3_release(m);
  }
  t3_release(g);
  t3_geometry *pg = t3_plane_geometry_new(30, 30, 1, 1);
  t3_material *gm = t3_mesh_standard_material_new(0x808890);
  t3_mesh *ground = t3_mesh_new(pg, gm);
  t3_object_set_rotation(ground, -1.5707963f, 0, 0);
  ground->base.receive_shadow = true;
  t3_object_add(&w->s->base, ground);
  t3_release(ground);
  t3_release(pg);
  t3_release(gm);
  t3_light *a = t3_ambient_light_new(0x404040, 1), *d = t3_directional_light_new(0xffffff, 2.5f);
  t3_object_set_position(d, 6, 10, 4);
  d->base.cast_shadow = true;
  d->shadow->map_width = d->shadow->map_height = 512;
  t3_camera *sh = d->shadow->camera;
  sh->left = -14; sh->right = 14; sh->top = 14; sh->bottom = -14; sh->near = 1; sh->far = 40;
  t3_camera_update_projection_matrix(sh);
  t3_object_add(&w->s->base, a);
  t3_object_add(&w->s->base, d);
  t3_release(a);
  t3_release(d);
  size_t len = 0;
  void *glb = read_file("test/assets/CesiumMan.glb", &len);
  if (!glb) { fprintf(stderr, "test/assets/CesiumMan.glb missing\n"); exit(2); }
  for (int k = 0; k < WALKERS; k++) {
    w->man[k] = t3_gltf_parse(glb, len, NULL, NULL);
    t3_object *m = w->man[k]->scene;
    t3_object_set_position(m, (float)k * 2.2f - 5.5f, 0, 3.0f);
    t3_object_set_scale(m, 1.6f, 1.6f, 1.6f);
    t3_object_add(&w->s->base, m);
    w->mix[k] = t3_animation_mixer_new(m);
    t3_animation_action_play(t3_animation_mixer_clip_action(w->mix[k], w->man[k]->animations[0]));
    t3_animation_mixer_update(w->mix[k], 0.13f * (float)k); /* each at its own phase */
  }
  free(glb);
  for (int k = 0; k < 2; k++) {
    w->cam[k] = t3_perspective_camera_new(50, (float)W / H, 0.5f, 100);
    t3_object_set_position(w->cam[k], k ? -7.0f : 0.0f, 9, k ? 9.0f : 13.0f);
    t3_object_look_at(w->cam[k], 0, 0, 0);
  }
}

static void world_free(world *w) {
  t3_release(w->cam[0]);
  t3_release(w->cam[1]);
  t3_release(w->s);
}

/* the boxes at frame f (every box moves every frame: every block changes), the walkers a frame on */
static void pose(world *w, int f) {
  for (int k = 0; k < WALKERS; k++) t3_animation_mixer_update(w->mix[k], 1.0f / 60);
  for (int i = 0; i < N; i++) {
    float x = (float)(i % 20) * 0.8f - 7.6f, z = (float)(i / 20) * 0.8f - 4.4f;
    float y = 0.45f + 0.3f * (float)((f * 7 + i * 13) % 17) / 17.0f;
    t3_object_set_position(w->box[i], x, y, z);
    t3_object_set_rotation(w->box[i], 0, (float)((f + i) % 31) * 0.2f, 0);
  }
}

/* one frame: the two views side by side, then the first again over a corner */
static void draw_frame(t3_renderer *r, world *w) {
  t3_renderer_set_scissor_test(r, true);
  t3_renderer_set_viewport(r, 0, 0, W / 2, H);
  t3_renderer_set_scissor(r, 0, 0, W / 2, H);
  t3_renderer_render(r, w->s, w->cam[0]);
  t3_renderer_set_viewport(r, W / 2, 0, W / 2, H);
  t3_renderer_set_scissor(r, W / 2, 0, W / 2, H);
  t3_renderer_render(r, w->s, w->cam[1]);
  t3_renderer_set_viewport(r, 0, 0, W / 4, H / 3);
  t3_renderer_set_scissor(r, 0, 0, W / 4, H / 3);
  t3_renderer_render(r, w->s, w->cam[0]);
  t3_renderer_set_scissor_test(r, false);
  t3_renderer_set_viewport(r, 0, 0, W, H);
}

static t3_renderer *renderer(void) {
  t3_renderer *r = t3_renderer_new(W, H);
  t3_renderer_set_shadow_map(r, true, T3_PCF_SHADOW_MAP);
  return r;
}

int main(void) {
  if (getenv("T3_TEST_CONTROL")) setenv("T3_TEST_UBO_ORPHAN", "1", 1);
  int st = setup();
  if (st) return st;
  world w;
  world_new(&w);
  t3_renderer *r = renderer();
  static uint8_t got[W * H * 4], want[W * H * 4];
  int fail = 0, checked = 0;
  unsigned stores = 0;
  for (int f = 1; f <= FRAMES; f++) {
    pose(&w, f);
    unsigned before = ubo_stores;
    draw_frame(r, &w);
    stores += ubo_stores - before;
    if (f == 1 || f == FRAMES / 2 || f == FRAMES) {
      glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, got);
      /* the reference: a fresh renderer's one frame of the same state (its ring has not wrapped) */
      t3_renderer *ref = renderer();
      draw_frame(ref, &w);
      glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, want);
      t3_renderer_destroy(ref);
      int diff = 0;
      for (int i = 0; i < W * H * 4; i++) diff += got[i] != want[i];
      unsigned colours = 0;
      for (int i = 4; i < W * H * 4; i += 4) colours += memcmp(got + i, got, 4) != 0;
      printf("frame %d: %d bytes differ from a fresh renderer's, %u pixels unlike the first\n", f, diff, colours);
      if (diff) fail = 1;
      if (colours < (unsigned)(W * H / 4)) printf("frame %d: not a picture\n", f), fail = 1;
      checked++;
    }
    glFlush(); /* the end of a frame */
  }
  if (t3_renderer_info(r)->skipped) printf("%u draws skipped (a program missing)\n", t3_renderer_info(r)->skipped), fail = 1;
  const char *e = t3_renderer_last_error(r);
  if (e && *e) printf("renderer error: %s\n", e), fail = 1;
  printf("%u uniform buffer stores in %d frames of 3 renders (%d checked)\n", stores, FRAMES, checked);
  if (stores > 12) printf("FAIL: the per-draw blocks orphan their buffer (%u stores, at most 12 expected)\n", stores), fail = 1;
  t3_renderer_destroy(r);
  world_free(&w);
  return fail;
}
