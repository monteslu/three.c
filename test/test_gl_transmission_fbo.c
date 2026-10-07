/* Transmission when the embedder owns the framebuffer: three.c draws into the
 * caller's RGBA16F FBO (t3_renderer_set_framebuffer), so the backdrop the glass
 * copies comes from that FBO (not framebuffer 0), at its type. A glass block in
 * front of red / green stripes must show the stripes; the control (the same
 * block, transmission off) must not.
 *
 *   EGL_PLATFORM=surfaceless ./build/test/test_gl_transmission_fbo   (test/run.sh builds it)
 *
 * Exits 0 when the glass shows the backdrop and the control does not. */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>

#include "three.h"

#define W 64
#define H 64

static int coloured_centre(float transmission, GLuint fbo) {
  t3_renderer *r = t3_renderer_new(W, H);
  t3_renderer_set_output_color_space(r, T3_LINEAR_SRGB_COLOR_SPACE);
  t3_renderer_set_framebuffer(r, fbo);
  t3_scene *s = t3_scene_new();
  t3_geometry *bar = t3_box_geometry_new(0.3f, 4, 0.2f, 1, 1, 1);
  for (int i = 0; i < 8; i++) {
    t3_material *m = t3_mesh_basic_material_new(i % 2 ? 0xe04040 : 0x40c060);
    t3_mesh *b = t3_mesh_new(bar, m);
    t3_object_set_position(b, -2.1f + i * 0.6f, (float)(i % 3) * 0.3f, -2);
    t3_object_add(&s->base, b);
    t3_release(b); t3_release(m);
  }
  t3_geometry *g = t3_box_geometry_new(1.4f, 1.4f, 1.4f, 1, 1, 1);
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->metalness = 0; m->roughness = 0.1f; m->transmission = transmission; m->thickness = 1; m->ior = 1.5f;
  m->transparent = transmission == 0;   /* (the control still draws in the transparent pass) */
  t3_mesh *glass = t3_mesh_new(g, m);
  t3_object_set_rotation(glass, 0.5f, 0.7f, 0);
  t3_object_set_position(glass, 0.2f, 0.3f, 0);
  t3_object_add(&s->base, glass);
  t3_light *sun = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(sun, 2, 3, 4);
  t3_object_add(&s->base, sun);
  t3_camera *cam = t3_perspective_camera_new(50, 1, 0.1f, 20);
  t3_object_set_position(cam, 0, 0, 4);
  t3_renderer_render(r, s, cam);
  float px[W * H * 4];
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, px);
  int n = 0;
  for (int y = H / 2 - 4; y < H / 2 + 4; y++)
    for (int x = W / 2 - 4; x < W / 2 + 4; x++) {
      const float *p = px + (y * W + x) * 4;
      if (p[1] > p[0] + 0.08f || p[0] > p[1] + 0.08f) n++;
    }
  const char *err = t3_renderer_last_error(r);
  if (err) fprintf(stderr, "renderer: %s\n", err);
  t3_release(glass); t3_release(g); t3_release(m); t3_release(bar); t3_release(sun); t3_release(cam); t3_release(s);
  t3_renderer_destroy(r);
  return n;
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
  /* the embedder's scene FBO: RGBA16F colour + depth */
  GLuint fbo, tex, depth;
  glGenFramebuffers(1, &fbo); glGenTextures(1, &tex); glGenRenderbuffers(1, &depth);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, W, H, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glBindRenderbuffer(GL_RENDERBUFFER, depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, W, H);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return fprintf(stderr, "fbo incomplete\n"), 2;
  int glass = coloured_centre(1, fbo), control = coloured_centre(0, fbo);
  printf("glass: %d coloured texels at the centre, control (no transmission): %d\n", glass, control);
  int ok = glass > 8 && control <= 8;
  puts(ok ? "test_gl_transmission_fbo: ok" : "test_gl_transmission_fbo: FAILED");
  return ok ? 0 : 1;
}
