/* A project's own program table: three.c built with only its core table
 * (T3_TABLE=core ./build.sh) plus a table tools/gen-project-table.mjs made
 * (registered with t3_register_program_table) must draw exactly what the full
 * table draws. test/run.sh builds this three ways:
 *   -DFULL     the full table, nothing registered: the reference picture
 *   -DPROJECT  the core table and the project table: the same picture
 *   (neither)  the core table alone: the control, its draws are missing
 * and writes each frame to build/test/project-<mode>.rgba. */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <string.h>

#include "three.h"

#ifdef PROJECT
struct t3_gen_table;
extern const struct t3_gen_table testproj_gl;
#endif

#define W 64
#define H 64

int main(int argc, char **argv) {
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
#ifdef PROJECT
  t3_register_program_table(&testproj_gl, NULL);
#endif
  t3_renderer *r = t3_renderer_new(W, H);
  t3_scene *s = t3_scene_new();
  t3_camera *cam = t3_perspective_camera_new(50, 1, 0.1f, 20);
  t3_object_set_position(cam, 0, 0.5f, 4);
  t3_object_look_at(cam, 0, 0, 0);
  uint8_t px4[4 * 4 * 4];
  for (int i = 0; i < 16; i++) { px4[i * 4] = (uint8_t)(i * 16); px4[i * 4 + 1] = 200; px4[i * 4 + 2] = (uint8_t)(255 - i * 16); px4[i * 4 + 3] = 255; }
  t3_texture *tex = t3_data_texture_new(4, 4, px4);
  t3_geometry *g = t3_sphere_geometry_new(1, 24, 12);
  t3_material *m = t3_mesh_standard_material_new(0xffffff);   /* standard+map+l1100 */
  m->roughness = 0.4f;
  t3_material_set_map(m, tex);
  t3_mesh *ball = t3_mesh_new(g, m);
  t3_object_add(&s->base, ball);
  t3_light *sun = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(sun, 2, 3, 4);
  t3_object_add(&s->base, sun);
  t3_light *lamp = t3_point_light_new(0xff8040, 10, 0, 2);
  t3_object_set_position(lamp, -2, 1, 2);
  t3_object_add(&s->base, lamp);
  t3_renderer_render(r, s, cam);
  static uint8_t px[W * H * 4];
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
  const char *missing = t3_renderer_generated_missing(r);
  const char *mode = argc > 1 ? argv[1] : "x";
  char fn[256];
  snprintf(fn, sizeof fn, "build/test/project-%s.rgba", mode);
  FILE *f = fopen(fn, "wb");
  if (f) { fwrite(px, 1, sizeof px, f); fclose(f); }
  printf("%s: missing states: %s\n", mode, *missing ? missing : "(none)");
  int bad = 0;
#if defined(FULL) || defined(PROJECT)
  bad = *missing != 0;   /* every draw has a program */
#else
  bad = *missing == 0;   /* the control: the core table alone cannot draw a material */
#endif
  t3_release(ball); t3_release(g); t3_release(m); t3_release(tex); t3_release(sun); t3_release(lamp); t3_release(cam); t3_release(s);
  t3_renderer_destroy(r);
  return bad;
}
