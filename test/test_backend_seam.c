/* The backend-neutral embedder hooks on the GLES backend
 * (t3_renderer_set_external_target, t3_renderer_render_depth_to,
 * t3_renderer_clip_z, t3_renderer_last_projection): a descriptor's clear
 * follows its load flags, its rect is the viewport and scissor, pixels
 * outside the rect survive, a depth-only pass LOADs the caller's depth
 * unless told to clear, and NULL puts the renderer back on the screen.
 *
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_backend_seam.c \
 *     build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o build/test_backend_seam
 *   EGL_PLATFORM=surfaceless ./build/test_backend_seam
 *
 * Exits 0 when every check holds. */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "three.h"
#define W 64
#define H 48

static int fails;
static unsigned char px[W * H * 4];
static void read_px(void) { glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); }
static const unsigned char *at(int x, int y) { return px + (y * W + x) * 4; }
static void expect(const char *what, int x, int y, int r, int g, int b) {
  const unsigned char *p = at(x, y);
  if (abs(p[0] - r) > 2 || abs(p[1] - g) > 2 || abs(p[2] - b) > 2) {
    fprintf(stderr, "FAIL %s at (%d,%d): got %d %d %d, want %d %d %d\n", what, x, y, p[0], p[1], p[2], r, g, b);
    fails++;
  }
}
static void fill(GLuint fbo, float r, float g, float b, float depth) {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glDisable(GL_SCISSOR_TEST);
  glDepthMask(GL_TRUE);
  glClearColor(r, g, b, 1);
  glClearDepthf(depth);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glClearDepthf(1);
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

  /* the embedder's target: RGBA8 colour + depth */
  GLuint fbo, tex, rb;
  glGenFramebuffers(1, &fbo);
  glGenTextures(1, &tex);
  glGenRenderbuffers(1, &rb);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glBindRenderbuffer(GL_RENDERBUFFER, rb);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return fprintf(stderr, "fbo incomplete\n"), 2;

  /* a red cube filling the middle of the view */
  t3_renderer *r = t3_renderer_new(W, H);
  t3_scene *s = t3_scene_new();
  t3_geometry *g = t3_box_geometry_new(2, 2, 2, 1, 1, 1);
  t3_material *m = t3_mesh_basic_material_new(0xff0000);
  t3_mesh *cube = t3_mesh_new(g, m);
  ((t3_object *)cube)->cast_shadow = true;
  t3_object_add(&s->base, cube);
  t3_camera *cam = t3_perspective_camera_new(50, (float)W / H, 0.1f, 100);
  t3_object_set_position(cam, 0, 0, 4);

  if (t3_renderer_clip_z(r) != T3_CLIP_Z_NEG1_1) { fprintf(stderr, "FAIL clip_z: GLES must report [-1,1]\n"); fails++; }

  /* 1: a descriptor that clears both: its clear colour outside the cube, red inside */
  t3_external_target t = { 0 };
  t.color = fbo; t.x = 0; t.y = 0; t.w = W; t.h = H;
  t.clear_rgba[0] = 0; t.clear_rgba[1] = 0; t.clear_rgba[2] = 1; t.clear_rgba[3] = 1; t.clear_depth = 1;
  fill(fbo, 0, 1, 0, 1);
  t3_renderer_reset_bindings(r);
  t3_renderer_set_external_target(r, &t);
  t3_renderer_render(r, s, cam);
  read_px();
  expect("clear both: corner takes the descriptor's clear colour", 1, 1, 0, 0, 255);
  expect("clear both: the cube draws", W / 2, H / 2, 255, 0, 0);
  const float *lp = t3_renderer_last_projection(r);
  if (memcmp(lp, cam->projection_matrix.e, sizeof(float) * 16)) { fprintf(stderr, "FAIL last_projection is not the camera's projection\n"); fails++; }

  /* 2: load_color keeps what was there outside the cube (depth still cleared, so the cube draws) */
  fill(fbo, 0, 1, 0, 1);
  t.load_color = true;
  t3_renderer_reset_bindings(r);
  t3_renderer_set_external_target(r, &t);
  t3_renderer_render(r, s, cam);
  read_px();
  expect("load_color: corner keeps the earlier green", 1, 1, 0, 255, 0);
  expect("load_color: the cube draws", W / 2, H / 2, 255, 0, 0);

  /* 3: load_depth with the depth at 0 (nearest): nothing passes the depth test, the cube is gone */
  fill(fbo, 0, 1, 0, 0);
  t.load_color = true; t.load_depth = true;
  t3_renderer_reset_bindings(r);
  t3_renderer_set_external_target(r, &t);
  t3_renderer_render(r, s, cam);
  read_px();
  expect("load_depth: the loaded depth hides the cube", W / 2, H / 2, 0, 255, 0);

  /* 4: a sub-rect: cleared and drawn only inside, untouched outside */
  fill(fbo, 0, 1, 0, 1);
  t.load_color = false; t.load_depth = false;
  t.x = W / 2; t.y = 0; t.w = W / 2; t.h = H;
  t3_renderer_reset_bindings(r);
  t3_renderer_set_external_target(r, &t);
  t3_renderer_render(r, s, cam);
  read_px();
  expect("rect: outside the rect is untouched", 1, 1, 0, 255, 0);
  expect("rect: inside the rect is cleared", W - 2, 1, 0, 0, 255);
  expect("rect: the cube draws inside the rect (its viewport is the rect)", W / 2 + W / 4, H / 2, 255, 0, 0);
  int vx, vy, vw, vh;
  t3_renderer_get_viewport(r, &vx, &vy, &vw, &vh);
  if (vx != W / 2 || vy != 0 || vw != W / 2 || vh != H) { fprintf(stderr, "FAIL rect: viewport not the rect\n"); fails++; }

  /* 5: NULL: back on the screen, whole-surface viewport, no scissor */
  t3_renderer_set_external_target(r, NULL);
  t3_renderer_get_viewport(r, &vx, &vy, &vw, &vh);
  if (vx || vy || vw != W || vh != H || t3_renderer_get_scissor(r, &vx, &vy, &vw, &vh)) { fprintf(stderr, "FAIL NULL: viewport / scissor not reset\n"); fails++; }
  if (!t3_renderer_clears_target(r)) { fprintf(stderr, "FAIL NULL: the renderer no longer clears its whole target\n"); fails++; }

  /* 6: render_depth_to: the depth pass into a rect of the caller's target. The
   * depth material writes packed depth as colour, so the cube's pixels change
   * from green; outside the rect nothing changes. With load_depth and depth
   * at 0 nothing passes; with a clear, it draws. */
  t3_external_target dt = { 0 };
  dt.color = fbo; dt.x = 0; dt.y = 0; dt.w = W / 2; dt.h = H; dt.load_depth = true; dt.clear_depth = 1;
  fill(fbo, 0, 1, 0, 0);
  t3_renderer_reset_bindings(r);
  t3_renderer_render_depth_to(r, s, cam, &dt);
  read_px();
  expect("depth_to + load_depth: nothing passes against depth 0", W / 4, H / 2, 0, 255, 0);
  dt.load_depth = false;
  t3_renderer_reset_bindings(r);
  t3_renderer_render_depth_to(r, s, cam, &dt);
  read_px();
  const unsigned char *p = at(W / 4, H / 2);
  if (p[0] == 0 && p[1] == 255 && p[2] == 0) { fprintf(stderr, "FAIL depth_to + clear: the caster did not draw in the rect\n"); fails++; }
  expect("depth_to: outside the rect is untouched", W - 2, H / 2, 0, 255, 0);

  /* the GL-era call still works as it did: a plain fbo, the renderer's clear colour */
  t3_renderer_set_clear_color(r, 0x000000, 1);
  fill(fbo, 0, 1, 0, 1);
  t3_renderer_reset_bindings(r);
  t3_renderer_set_framebuffer(r, fbo);
  t3_renderer_render(r, s, cam);
  read_px();
  expect("set_framebuffer: autoClear to the renderer's clear colour", 1, 1, 0, 0, 0);
  expect("set_framebuffer: the cube draws", W / 2, H / 2, 255, 0, 0);

  t3_release(cam); t3_release(cube); t3_release(m); t3_release(g); t3_release(s);
  t3_renderer_destroy(r);
  printf(fails ? "test_backend_seam: %d FAILED\n" : "test_backend_seam: ok\n", fails);
  return fails ? 1 : 0;
}
