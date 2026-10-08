/* An InstancedMesh that casts a shadow and gains instance colours after its
 * first render. The shadow caster pass draws it (casters ignore colour), and
 * the instanced VAO path once dereferenced the colour attribute's GL buffer,
 * which nothing had created: a crash in the caster pass.
 *
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_instance_color.c \
 *     build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o build/test_instance_color
 *   EGL_PLATFORM=surfaceless ./build/test_instance_color
 *
 * Then InstancedMeshes drawn and released in a loop: three.c's live
 * allocations (counted through t3_set_allocator) come back to the same number
 * each time. An InstancedMesh's own VAO record (use_geometry_bind) was never
 * freed when the mesh was.
 *
 * Exits 0 when every render returns and nothing leaks. */
#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>

#include "three.h"

#define W 96
#define H 64

static long live;
static void *cmalloc(size_t n) { void *p = malloc(n); if (p) live++; return p; }
static void *crealloc(void *p, size_t n) { void *q = realloc(p, n); if (!p && q) live++; return q; }
static void cfree(void *p) { if (p) live--; free(p); }

/* an InstancedMesh drawn (shadow pass too) and released: the live count while it is in the scene */
static long cycle(t3_renderer *r, t3_scene *s, t3_camera *cam, t3_geometry *g, t3_material *m) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, 4);
  im->mesh.base.cast_shadow = true;
  t3_instanced_mesh_set_color_at(im, 1, t3_color_hex(0x40ff80));
  t3_object_add(&s->base, im);
  t3_renderer_render(r, s, cam);
  long during = live;
  t3_object_remove(&s->base, im);
  t3_release(im);
  return during;
}

int main(void) {
  t3_set_allocator(cmalloc, crealloc, cfree);
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

  t3_renderer *r = t3_renderer_new(W, H);
  t3_renderer_set_shadow_map(r, true, T3_PCF_SHADOW_MAP);
  t3_scene *s = t3_scene_new();
  t3_camera *cam = t3_perspective_camera_new(50, (float)W / H, 0.1f, 100);
  t3_object_set_position(cam, 0, 4, 10);
  t3_object_look_at(cam, 0, 0, 0);
  t3_light *sun = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(sun, 3, 6, 2);
  sun->base.cast_shadow = true;
  t3_object_add(&s->base, sun);
  t3_release(sun);

  t3_geometry *g = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, 8);
  im->mesh.base.cast_shadow = true;
  for (int i = 0; i < 8; i++) {
    t3_mat4 mx;
    t3_mat4_identity(&mx);
    mx.e[12] = (float)i * 1.5f - 5;
    t3_instanced_mesh_set_matrix_at(im, i, &mx);
  }
  t3_object_add(&s->base, im);

  t3_renderer_render(r, s, cam);
  for (int i = 0; i < 8; i++) t3_instanced_mesh_set_color_at(im, i, t3_color_hex(0xff8040));
  t3_renderer_render(r, s, cam);
  t3_renderer_render(r, s, cam);

  t3_object_remove(&s->base, im);
  t3_release(im);
  cycle(r, s, cam, g, m);   /* warm: its program and caches */
  long base = live;
  for (int i = 0; i < 4; i++) {
    long during = cycle(r, s, cam, g, m);
    /* the control: the mesh's own allocations were counted while it lived */
    if (during <= base) return fprintf(stderr, "test_instance_color: %ld live during a cycle, %ld after: the count sees nothing\n", during, base), 1;
    if (live != base) return fprintf(stderr, "test_instance_color: cycle %d leaks %ld allocations\n", i, live - base), 1;
  }
  t3_release(g);
  t3_release(m);
  t3_release(s);
  t3_release(cam);
  t3_renderer_destroy(r);
  puts("test_instance_color: ok");
  return 0;
}
