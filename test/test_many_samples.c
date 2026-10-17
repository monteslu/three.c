/* An extension texture sampled at many places (test/extensions/many.mjs: 24
 * samples, as a splat terrain samples its layer arrays) draws: its GL program
 * has a flipY flag per sample, and each must find a slot. Before 0.186.16 a
 * texture had room for 16 flags, so the program was refused as incomplete
 * and every mesh carrying it was silently not drawn (Terrain3D's near
 * patches). Checks: the draw happens (one call, none skipped, no error) and
 * the plane shows white texels where it covers the frame; the control, the
 * same scene without the plane, must leave those texels at the clear colour.
 *
 * Built and run by test/run.sh on GLES (build/test/exttab: standard+xmany). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "three.h"
#include "gen/programs.h"

#define W 64
#define H 64

extern const t3_gen_table exttab_gl;

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

static t3_texture *many_map;
static void fill(t3_extension_ctx *c, void *user) {
  (void)user;
  t3_extension_set_texture(c, "manyMap", many_map);
}

int main(void) {
  int st = setup();
  if (st) return st;
  t3_register_program_table(&exttab_gl, NULL);
  uint8_t white[4 * 4 * 4];
  memset(white, 255, sizeof white);
  many_map = t3_data_texture_new(2, 2, white);
  t3_scene *scene = t3_scene_new();
  t3_camera *cam = t3_perspective_camera_new(50, 1, 0.1f, 10);
  t3_object_set_position(cam, 0, 0, 2);
  t3_material *mat = t3_mesh_standard_material_new(0xffffff);
  mat->emissive = (t3_color){ 1, 1, 1 }; /* unlit: the plane is white wherever it is drawn */
  int failures = 0;
  if (!t3_material_set_extension(mat, "many")) { fprintf(stderr, "FAIL: set_extension(many)\n"); failures++; }
  t3_geometry *g = t3_plane_geometry_new(2, 2, 1, 1);
  t3_mesh *plane = t3_mesh_new(g, mat);
  t3_object_add(&scene->base, plane);
  t3_renderer *r = t3_renderer_new(W, H);
  t3_renderer_set_clear_color(r, 0x000000, 1);
  t3_renderer_set_extension(r, "many", fill, NULL);
  static uint8_t px[W * H * 4];
  t3_renderer_render(r, scene, cam);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
  const t3_render_info *info = t3_renderer_info(r);
  const char *err = t3_renderer_last_error(r);
  int lit = 0;
  for (int i = 0; i < W * H; i++) lit += px[i * 4] > 200;
  if (info->skipped || info->calls < 1) { fprintf(stderr, "FAIL: %u draws, %u skipped (%s)\n", info->calls, info->skipped, err ? err : ""); failures++; }
  if (lit < W * H / 4) { fprintf(stderr, "FAIL: the plane covers %d white texels of %d\n", lit, W * H); failures++; }
  /* the control: no plane, no white */
  t3_object_remove(&scene->base, plane);
  t3_renderer_render(r, scene, cam);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
  int lit0 = 0;
  for (int i = 0; i < W * H; i++) lit0 += px[i * 4] > 200;
  if (lit0) { fprintf(stderr, "FAIL CONTROL: %d white texels with no plane: the check sees nothing\n", lit0); failures++; }
  printf("test_many_samples: a texture sampled 24 times draws (%d white texels; %d without the plane)%s\n", lit, lit0, failures ? "" : ", ok");
  return failures ? 1 : 0;
}
