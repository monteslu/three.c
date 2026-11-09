/* t3_renderer_set_missing_hook: a draw whose program state no table has
 * calls the hook once, with that draw's object and material, and the state
 * t3_renderer_generated_missing lists. test/run.sh builds it twice:
 *   (default)  three.c's core table alone: the sphere's standard material is
 *              in no table, so the hook names the sphere and its material,
 *              once over two frames
 *   -DFULL     the full table: every draw has a program, the hook never runs
 *              (and the core build's count of 1 is not something the hook
 *              does whatever the tables say) */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <string.h>

#include "three.h"

static int calls;
static const t3_object *seen_obj;
static const t3_material *seen_mat;
static char seen_state[256];
static void hook(void *user, const char *state, const t3_object *o, const t3_material *m) {
  (void)user;
  calls++;
  seen_obj = o, seen_mat = m;
  snprintf(seen_state, sizeof seen_state, "%s", state);
}

int main(void) {
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(d, NULL, NULL)) return fprintf(stderr, "eglInitialize failed\n"), 2;
  EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
  EGLConfig cfg;
  EGLint n = 0;
  if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) return fprintf(stderr, "no config\n"), 2;
  EGLint sa[] = { EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE };
  EGLSurface surf = eglCreatePbufferSurface(d, cfg, sa);
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint xa[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xa);
  if (!ctx || !eglMakeCurrent(d, surf, surf, ctx)) return fprintf(stderr, "no context\n"), 2;
  t3_renderer *r = t3_renderer_new(32, 32);
  t3_renderer_set_missing_hook(r, hook, NULL);
  t3_scene *s = t3_scene_new();
  t3_camera *cam = t3_perspective_camera_new(50, 1, 0.1f, 20);
  t3_object_set_position(cam, 0, 0.5f, 4);
  t3_object_look_at(cam, 0, 0, 0);
  t3_geometry *g = t3_sphere_geometry_new(1, 16, 8);
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  t3_mesh *ball = t3_mesh_new(g, m);
  t3_object_add(&s->base, ball);
  t3_light *sun = t3_directional_light_new(0xffffff, 2);
  t3_object_add(&s->base, sun);
  t3_renderer_render(r, s, cam);
  t3_renderer_render(r, s, cam); /* the same state again: no second call */
  const char *missing = t3_renderer_generated_missing(r);
  int bad;
#ifdef FULL
  bad = calls != 0 || *missing;
  printf("full table: %d hook call(s), missing \"%s\"\n", calls, missing);
#else
  char want[260];
  snprintf(want, sizeof want, "%s\n", seen_state);
  bad = calls != 1 || seen_obj != (const t3_object *)ball || seen_mat != m || strcmp(missing, want) != 0;
  printf("core table: %d hook call(s), state %s, object %s, material %s\n", calls, seen_state,
         seen_obj == (const t3_object *)ball ? "the sphere" : "WRONG", seen_mat == m ? "its material" : "WRONG");
#endif
  t3_release(ball); t3_release(g); t3_release(m); t3_release(sun); t3_release(cam); t3_release(s);
  t3_renderer_destroy(r);
  return bad;
}
