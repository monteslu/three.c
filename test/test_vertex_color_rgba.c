/* Vertex colours from an rgba color attribute (glTF exporters write COLOR_0
 * as vec4 by default): the draw uses the rgb part and an opaque alpha, the
 * same picture as an rgb attribute with those colours. The control: a two
 * component colour attribute is still refused (the draw is skipped, counted in
 * t3_renderer_info, and the reason is in t3_renderer_last_error).
 *
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_vertex_color_rgba.c \
 *     build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o build/test_vertex_color_rgba
 *   EGL_PLATFORM=surfaceless ./build/test_vertex_color_rgba
 *
 * Exits 0 when every check holds. */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "three.h"
#define W 32
#define H 32

static unsigned char px[3][W * H * 4];

/* a quad filling the view, vertex colour items of item_size components (rgb (1, 0.5, 0)
 * plus alpha 0.25 when there is a fourth; 0.8 and 0.2 for two), drawn over a blue clear */
static void draw(t3_renderer *r, int item_size, unsigned char *out) {
  t3_scene *s = t3_scene_new();
  t3_geometry *g = t3_plane_geometry_new(4, 4, 1, 1);
  float c[4 * 4];
  const float rgba[4] = { 1, 0.5f, 0, 0.25f }, two[2] = { 0.8f, 0.2f };
  for (int v = 0; v < 4; v++)
    for (int k = 0; k < item_size; k++) c[v * item_size + k] = item_size == 2 ? two[k] : rgba[k];
  t3_attribute *a = t3_attribute_new(T3_FLOAT32, c, 4, item_size);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, a);
  t3_release(a);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  t3_mesh *mesh = t3_mesh_new(g, m);
  t3_object_add(&s->base, mesh);
  t3_camera *cam = t3_perspective_camera_new(50, (float)W / H, 0.1f, 100);
  t3_object_set_position(cam, 0, 0, 3);
  t3_renderer_set_clear_color(r, 0x0000ff, 1);
  t3_renderer_render(r, s, cam);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
  t3_release(mesh);
  t3_release(g);
  t3_release(m);
  t3_release(cam);
  t3_release(s);
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
  EGLint cxa[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cxa);
  if (!eglMakeCurrent(d, surf, surf, ctx)) return fprintf(stderr, "makeCurrent failed\n"), 2;

  int fails = 0;
  for (int i = 0; i < 2; i++) {   /* rgb, then rgba: drawn, nothing skipped */
    t3_renderer *r = t3_renderer_new(W, H);
    draw(r, 3 + i, px[i]);
    unsigned skipped = t3_renderer_info(r)->skipped;
    if (skipped) { fprintf(stderr, "FAIL %s: %u draws skipped (%s)\n", i ? "rgba" : "rgb", skipped, t3_renderer_last_error(r)); fails++; }
    t3_renderer_destroy(r);
  }
  t3_renderer *r = t3_renderer_new(W, H);
  draw(r, 2, px[2]);
  const char *err = t3_renderer_last_error(r);
  bool refused = t3_renderer_info(r)->skipped > 0 && err && strstr(err, "vertex colors");
  t3_renderer_destroy(r);

  const unsigned char *mid0 = px[0] + ((H / 2) * W + W / 2) * 4, *mid1 = px[1] + ((H / 2) * W + W / 2) * 4,
                      *mid2 = px[2] + ((H / 2) * W + W / 2) * 4;
  /* rgb (1, 0.5, 0) in sRGB output: 255, 188, 0 */
  if (mid0[0] < 250 || mid0[1] < 180 || mid0[1] > 196 || mid0[2] > 4) { fprintf(stderr, "FAIL rgb draw: centre %d %d %d\n", mid0[0], mid0[1], mid0[2]); fails++; }
  if (memcmp(px[0], px[1], sizeof px[0])) {
    fprintf(stderr, "FAIL rgba draw differs from rgb: centre %d %d %d, rgb %d %d %d\n", mid1[0], mid1[1], mid1[2], mid0[0], mid0[1], mid0[2]);
    fails++;
  }
  /* the control: two components draw nothing (the blue clear) and the state is named */
  if (!refused) { fprintf(stderr, "FAIL control: a two component colour attribute was not refused\n"); fails++; }
  if (mid2[0] > 4 || mid2[2] < 250) { fprintf(stderr, "FAIL control: centre %d %d %d, want the blue clear\n", mid2[0], mid2[1], mid2[2]); fails++; }
  if (fails) return 1;
  printf("test_vertex_color_rgba: rgba centre %d %d %d = rgb; two components refused, ok\n", mid1[0], mid1[1], mid1[2]);
  return 0;
}
