/* Extension names and hooks have room for an application that composes many
 * (an engine that joins parts into one name per combination): 300 distinct
 * names, one of 63 characters, each with a hook and a batching flag on one
 * renderer; a name read back is the name set; 64 characters and a bad
 * character are refused. Nothing is drawn.
 * The control (T3_TEST_CONTROL=1) asks for what three.c 0.187.9 had room for,
 * 64 names of at most 31 characters, to hold: it must fail.
 *
 * cc -std=c99 -O2 -I include -I src test/test_extension_names.c build/native/libthree.a -lEGL -lGLESv2 -lz -lm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>

#include "three.h"

static int gl_setup(void) {
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(d, NULL, NULL)) return 2;
  EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
  EGLConfig cfg;
  EGLint n = 0;
  if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) return 2;
  EGLint sa[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
  EGLSurface surf = eglCreatePbufferSurface(d, cfg, sa);
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint xa[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xa);
  return ctx && eglMakeCurrent(d, surf, surf, ctx) ? 0 : 2;
}

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
static void fill(t3_extension_ctx *c, void *u) { (void)c; (void)u; }

int main(void) {
  bool control = getenv("T3_TEST_CONTROL") != NULL;
  int count = 300;
  size_t longest = 63;
  char name[80];
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  int ok = 0;
  for (int i = 0; i < count; i++) {
    snprintf(name, sizeof name, "part%d_cluster_csm_gi", i);
    if (t3_material_set_extension(m, name) && !strcmp(t3_material_get_extension(m), name)) ok++;
  }
  memset(name, 'a', longest);
  name[longest] = 0;
  bool long_ok = t3_material_set_extension(m, name) && strlen(t3_material_get_extension(m)) == longest;
  if (control) {
    /* what 64 names of 31 characters allowed */
    CHECK(ok <= 64, "%d names were taken, the old limit was 64", ok);
    CHECK(!long_ok, "a %zu character name was taken, the old limit was 31", longest);
    return failures ? 1 : 0;
  }
  CHECK(ok == count, "%d of %d names taken", ok, count);
  CHECK(long_ok, "a %zu character name refused", longest);
  name[longest] = 'a', name[longest + 1] = 0;
  CHECK(!t3_material_set_extension(m, name), "a 64 character name taken");
  CHECK(!t3_material_set_extension(m, "Bad-Name"), "a name with bad characters taken");
  CHECK(t3_material_set_extension(m, NULL) && !t3_material_get_extension(m), "NULL does not clear the extension");
  /* the first name is still where it was after the list grew */
  CHECK(t3_material_set_extension(m, "part0_cluster_csm_gi") && !strcmp(t3_material_get_extension(m), "part0_cluster_csm_gi"), "the first name moved");
  /* a renderer takes a hook and a batching flag for every one (it had room for 64) */
  if (gl_setup()) return fprintf(stderr, "no GLES context\n"), 2;
  t3_renderer *r = t3_renderer_new(16, 16);
  for (int i = 0; i < count; i++) {
    snprintf(name, sizeof name, "part%d_cluster_csm_gi", i);
    t3_renderer_set_extension(r, name, fill, NULL);
    t3_renderer_set_extension_batching(r, name, i & 1);
  }
  const char *err = t3_renderer_last_error(r);
  CHECK(!err || !strstr(err, "extension"), "the renderer reports: %s", err);
  t3_renderer_destroy(r);
  t3_release(m);
  if (!failures) puts("test_extension_names: ok");
  return failures ? 1 : 0;
}
