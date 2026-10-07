/* The OpenGL ES 3 backend (src/backend.h): the slots the renderer routes
 * through (lifetime, finish, the embedder's encoder / target hooks, the
 * clip-space convention). The renderer's GL for programs, geometry, textures
 * and draws lives in renderer.c and renderer_gen.inc; those slots are NULL
 * here, and the renderer never calls a NULL slot. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "gl.h"
#include "internal.h"

typedef struct {
  t3_backend base;
  uintptr_t external_encoder;   /* accepted, ignored: GL has no encoder */
  char renderer_string[256];
} gles_backend;

static void gles_destroy(t3_backend *b) { free(b); }
static const char *gles_name(t3_backend *b) { (void)b; return "gles3"; }
static const char *gles_renderer_string(t3_backend *b) {
  gles_backend *g = (gles_backend *)b;
  if (!g->renderer_string[0]) {
    const char *s = (const char *)glGetString(GL_RENDERER);
    snprintf(g->renderer_string, sizeof g->renderer_string, "%s", s ? s : "unknown");
  }
  return g->renderer_string;
}
static t3_clip_z gles_clip_z(t3_backend *b) { (void)b; return T3_CLIP_Z_NEG1_1; }
static void gles_finish(t3_backend *b) { (void)b; glFinish(); }
static void gles_reset_bindings(t3_backend *b) { (void)b; /* the renderer's own state cache is reset by t3_renderer_reset_bindings */ }
static void gles_set_external_encoder(t3_backend *b, uintptr_t enc) { ((gles_backend *)b)->external_encoder = enc; }
static uintptr_t gles_external_pass(t3_backend *b) { (void)b; return 0; }

t3_backend *t3_backend_gles_new(void) {
  gles_backend *g = calloc(1, sizeof *g);
  if (!g) return NULL;
  g->base.destroy = gles_destroy;
  g->base.name = gles_name;
  g->base.renderer_string = gles_renderer_string;
  g->base.clip_z = gles_clip_z;
  g->base.finish = gles_finish;
  g->base.reset_bindings = gles_reset_bindings;
  g->base.set_external_encoder = gles_set_external_encoder;
  g->base.external_pass = gles_external_pass;
  /* set_external_target, texture_native / adopt / swap: the renderer's own
   * GLES paths (t3_renderer_set_framebuffer, t3_texture_from_gl / adopt_gl /
   * swap_gl) serve these */
  return &g->base;
}
