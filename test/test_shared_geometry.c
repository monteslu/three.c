/* Two renderers on one GL context drawing one shared geometry, both
 * auto-instanced. A geometry's batch VAO points into the instance buffer of
 * the renderer that set it up; another renderer drawing the same geometry at
 * the same batch offset used to skip re-pointing it, and so drew from the
 * first renderer's instance buffer: the wrong instances, and when the second
 * draw has more instances than the first buffer holds, a read past the end
 * of a GPU buffer (a GPU page fault on some drivers). This renders scene B
 * after scene A through two renderers sharing the box geometry and compares
 * B with B drawn by a renderer of its own with a geometry of its own.
 *
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_shared_geometry.c \
 *     build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o build/test_shared_geometry
 *   EGL_PLATFORM=surfaceless ./build/test_shared_geometry
 *
 * Exits 0 when B is the same both ways. */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "three.h"

#define W 96
#define H 64

static t3_scene *grid(t3_geometry *g, int n, float x0) {
  t3_scene *s = t3_scene_new();
  s->has_background = true;
  s->background = t3_color_hex(0x000000);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  for (int i = 0; i < n; i++) {
    t3_mesh *b = t3_mesh_new(g, m);
    t3_object_set_position(b, x0 + (float)(i % 8) * 1.5f - 5, (float)(i / 8) * 1.5f - 3, 0);
    t3_object_add(&s->base, b);
    t3_release(b);
  }
  t3_release(m);
  return s;
}

static void draw(t3_renderer *r, t3_render_target *rt, t3_scene *s, t3_camera *c, unsigned char *px) {
  t3_renderer_set_render_target(r, rt);
  t3_renderer_render(r, s, c);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
  t3_renderer_set_render_target(r, NULL);
}

int main(void) {
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

  t3_camera *cam = t3_orthographic_camera_new(-8, 8, 6, -6, 0.1f, 100);
  cam->base.position.z = 10;
  t3_geometry *shared = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_geometry *own = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_scene *a = grid(shared, 40, 0);   /* 40 instances at batch offset 0 */
  t3_scene *b = grid(shared, 20, 0.7f); /* 20 instances at batch offset 0, elsewhere */
  t3_scene *ref = grid(own, 20, 0.7f);
  t3_renderer *ra = t3_renderer_new(W, H), *rb = t3_renderer_new(W, H), *rr = t3_renderer_new(W, H);
  t3_render_target *ta = t3_render_target_new(W, H, NULL), *tb = t3_render_target_new(W, H, NULL),
                   *tr = t3_render_target_new(W, H, NULL);
  static unsigned char pa[W * H * 4], pb[W * H * 4], pr[W * H * 4];
  int bad = 0;
  for (int frame = 0; frame < 3; frame++) {
    draw(ra, ta, a, cam, pa);
    draw(rb, tb, b, cam, pb);
    draw(rr, tr, ref, cam, pr);
    int diff = 0;
    for (int i = 0; i < W * H * 4; i++) diff += pb[i] != pr[i];
    printf("frame %d: %d bytes of B differ from B drawn alone\n", frame, diff);
    bad += diff != 0;
  }
  /* B again after A grew: A's instance buffer reallocates (same name) */
  t3_scene *a2 = grid(shared, 64, 0);
  draw(ra, ta, a2, cam, pa);
  draw(rb, tb, b, cam, pb);
  int diff = 0;
  for (int i = 0; i < W * H * 4; i++) diff += pb[i] != pr[i];
  printf("after A grew: %d bytes differ\n", diff);
  bad += diff != 0;
  printf(bad ? "FAIL shared geometry across renderers\n" : "ok   shared geometry across renderers\n");
  return bad ? 1 : 0;
}
