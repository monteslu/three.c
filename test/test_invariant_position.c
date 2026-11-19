/* Every WebGPU vertex program three.c builds declares its clip position @invariant (gen_program.h
 * t3_gen_wgsl_invariant_position, applied by gen_wgpu.c t3_gen_wgpu_build): a depth prepass and the colour pass draw
 * the same vertices through two pipelines, and on Metal a position computed two ways failed about half the pixels of
 * distant ground (holes the background showed through). Checks each vertex program of the base WebGPU table as the
 * build makes it, and that text without the declaration is refused (fail fast, never a program built without it).
 * T3_TEST_CONTROL=1 checks the programs as the table holds them, without the rewrite: it must fail. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gen_program.h"

static int count(const char *s, const char *what) {
  int n = 0;
  for (const char *p = s; (p = strstr(p, what)); p++) n++;
  return n;
}

int main(void) {
  const bool control = getenv("T3_TEST_CONTROL") && atoi(getenv("T3_TEST_CONTROL"));
  char err[256];
  int bad = 0, n = 0;
  for (unsigned i = 0; i < t3_gen_base_wgpu.count; i++) {
    const t3_gen_program *p = &t3_gen_base_wgpu.programs[i];
    char *v = t3_gen_source(p, false, err, sizeof err);
    if (!v) { fprintf(stderr, "FAIL %s\n", err); bad++; continue; }
    if (!strstr(v, "@vertex")) { free(v); continue; }
    char *b = control ? v : t3_gen_wgsl_invariant_position(v, p->state, err, sizeof err);
    if (!b) { fprintf(stderr, "FAIL %s\n", err); bad++; free(v); continue; }
    n++;
    if (count(b, "@invariant @builtin( position ) builtinClipSpace") != 1 || count(b, "@builtin( position )") != 1) {
      if (bad < 5) fprintf(stderr, "FAIL %s: the position is not declared @invariant exactly once\n", p->state);
      bad++;
    }
    if (b != v) free(b);
    free(v);
  }
  /* GL: every vertex program of the base GL table gets invariant gl_Position once, before its main */
  int ngl = 0;
  for (unsigned i = 0; i < t3_gen_base_gl.count; i++) {
    const t3_gen_program *p = &t3_gen_base_gl.programs[i];
    char *v = t3_gen_source(p, false, err, sizeof err);
    if (!v) { fprintf(stderr, "FAIL %s\n", err); bad++; continue; }
    char *b = control ? v : t3_gen_glsl_invariant_position(v, p->state, err, sizeof err);
    if (!b) { fprintf(stderr, "FAIL %s\n", err); bad++; free(v); continue; }
    ngl++;
    const char *inv = strstr(b, "invariant gl_Position;"), *m = strstr(b, "\nvoid main(");
    if (count(b, "invariant gl_Position;") != 1 || !m || inv > m) {
      if (bad < 5) fprintf(stderr, "FAIL %s (GL): gl_Position is not declared invariant once before main\n", p->state);
      bad++;
    }
    if (b != v) free(b);
    free(v);
  }
  printf("%d GL vertex programs\n", ngl);
  char *g0 = t3_gen_glsl_invariant_position("#version 300 es\nint f(){return 1;}\n", "nomain", err, sizeof err);
  if (g0 || !strstr(err, "nomain")) { fprintf(stderr, "FAIL a GL program with no main was not refused\n"); bad++; free(g0); }
  /* a vertex program that does not declare the position where r187 does is refused, naming the program */
  const char *none = "@vertex fn main() -> @builtin( position ) vec4<f32> { return vec4<f32>(0.0); }";
  char *r = t3_gen_wgsl_invariant_position(none, "none", err, sizeof err);
  if (r || !strstr(err, "none")) { fprintf(stderr, "FAIL a program with no builtinClipSpace was not refused\n"); bad++; free(r); }
  printf("%d WebGPU vertex programs, %d without an invariant position\n", n, bad);
  return n > 0 && bad == 0 ? 0 : 1;
}
