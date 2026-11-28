/* t3_renderer_render_objects: a few meshes of a scene drawn without walking it. A scene of 200 boxes (a material
 * each, unlit), an instanced mesh of 64 boxes and three skinned CesiumMan walkers (Khronos glTF sample, CC-BY-4.0),
 * every one posed anew each frame; a set of them (every seventh box, the instanced mesh, two walkers' meshes) is drawn
 * two ways and must give the same picture byte for byte, at frames 1, 40 and 80:
 *   - the scene with everything outside the set hidden, through t3_renderer_render;
 *   - the whole scene left visible, through t3_renderer_render_objects with the set (world matrices updated by the
 *     caller first, as render_objects does not run the scene's auto update).
 * The picture must be one (a share of pixels unlike the clear colour). The control (T3_TEST_CONTROL=1) adds one box
 * outside the set to render_objects' list and must fail. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "three.h"

#define W 192
#define H 144
#define N 200
#define WALKERS 3
#define FRAMES 80

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

static t3_scene *s;
static t3_mesh *box[N];
static t3_instanced_mesh *swarm;
static t3_gltf *man[WALKERS];
static t3_animation_mixer *mix[WALKERS];
static t3_camera *cam;
/* the set drawn, and every drawable mesh of the scene */
static t3_object *set[N + 16], *all[N + 64];
static int nset, nall;

static void collect(t3_object *o, void *u) {
  (void)u;
  if (o->type == T3_SKINNED_MESH) all[nall++] = o;
}

static void world(void) {
  s = t3_scene_new();
  t3_geometry *g = t3_box_geometry_new(0.4f, 0.9f, 0.4f, 1, 1, 1);
  for (int i = 0; i < N; i++) {
    t3_material *m = t3_mesh_basic_material_new(0x303030u + (uint32_t)i * 0x010305u);
    box[i] = t3_mesh_new(g, m);
    t3_object_add(&s->base, box[i]);
    t3_release(box[i]);
    t3_release(m);
    all[nall++] = &box[i]->base;
  }
  t3_material *im = t3_mesh_basic_material_new(0xc08040);
  swarm = t3_instanced_mesh_new(g, im, 64);
  t3_object_add(&s->base, swarm);
  t3_release(swarm);
  t3_release(im);
  all[nall++] = &swarm->mesh.base;
  t3_release(g);
  size_t len = 0;
  void *glb = read_file("test/assets/CesiumMan.glb", &len);
  if (!glb) { fprintf(stderr, "test/assets/CesiumMan.glb missing\n"); exit(2); }
  for (int k = 0; k < WALKERS; k++) {
    man[k] = t3_gltf_parse(glb, len, NULL, NULL);
    t3_object *m = man[k]->scene;
    t3_object_set_position(m, (float)k * 2.5f - 2.5f, 0, 3.0f);
    t3_object_set_scale(m, 1.6f, 1.6f, 1.6f);
    t3_object_add(&s->base, m);
    mix[k] = t3_animation_mixer_new(m);
    t3_animation_action_play(t3_animation_mixer_clip_action(mix[k], man[k]->animations[0]));
    t3_animation_mixer_update(mix[k], 0.17f * (float)k);
    int before = nall;
    t3_object_traverse(m, collect, NULL);
    for (int i = before; i < nall && k < 2; i++) set[nset++] = all[i];   /* two walkers' meshes in the set */
  }
  free(glb);
  for (int i = 0; i < N; i += 7) set[nset++] = &box[i]->base;
  set[nset++] = &swarm->mesh.base;
  cam = t3_perspective_camera_new(50, (float)W / H, 0.5f, 100);
  t3_object_set_position(cam, 0, 7, 13);
  t3_object_look_at(cam, 0, 0, 0);
}

static void pose(int f) {
  for (int k = 0; k < WALKERS; k++) t3_animation_mixer_update(mix[k], 1.0f / 60);
  for (int i = 0; i < N; i++) {
    float x = (float)(i % 20) * 0.8f - 7.6f, z = (float)(i / 20) * 0.8f - 4.4f;
    t3_object_set_position(box[i], x, 0.45f + 0.3f * (float)((f * 7 + i * 13) % 17) / 17.0f, z);
    t3_object_set_rotation(box[i], 0, (float)((f + i) % 31) * 0.2f, 0);
  }
  for (int i = 0; i < 64; i++) {
    t3_mat4 m;
    t3_mat4_compose(&m, (t3_vec3){ (float)(i % 8) * 0.9f - 3.2f, 2.5f + 0.4f * (float)((f + i) % 9) / 9.0f, (float)(i / 8) * 0.6f - 6.0f },
                    (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 0.5f, 0.5f, 0.5f });
    t3_instanced_mesh_set_matrix_at(swarm, i, &m);
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
  static uint8_t a[W * H * 4], b[W * H * 4];
  int fail = 0;
  for (int f = 1; f <= FRAMES; f++) {
    pose(f);
    if (f != 1 && f != FRAMES / 2 && f != FRAMES) continue;
    /* the whole-scene way: everything outside the set hidden */
    for (int i = 0; i < nall; i++) all[i]->visible = in_set(all[i]);
    t3_renderer_render(r, s, cam);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, a);
    for (int i = 0; i < nall; i++) all[i]->visible = true;
    /* the set alone */
    t3_object *list[N + 17];
    int n = 0;
    for (int i = 0; i < nset; i++) list[n++] = set[i];
    if (control) list[n++] = &box[1]->base;   /* (a box outside the set) */
    t3_renderer_render_objects(r, s, cam, list, n);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, b);
    int diff = 0;
    unsigned drawn = 0;
    for (int i = 0; i < W * H * 4; i++) diff += a[i] != b[i];
    for (int i = 0; i < W * H * 4; i += 4) drawn += memcmp(a + i, a, 4) != 0;
    printf("frame %d: %d of %d meshes drawn, %d bytes differ, %u pixels unlike the first\n", f, nset, nall, diff, drawn);
    if (diff) fail = 1;
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
