/* 4-wide float SIMD for three.c's batch loops (culling, raycasting, ...),
 * written once with GCC/Clang vector extensions: SSE on x86-64, NEON on ARM,
 * simd128 on wasm (-msimd128), and plain scalar code anywhere else. Every
 * operation is the same IEEE single-precision operation the scalar code does,
 * lane by lane, so batch results equal the one-at-a-time results bit for
 * bit (no FMA contraction: keep -ffp-contract off for these, the default
 * outside GNU C on x86 and wasm). */
#ifndef T3_SIMD_H
#define T3_SIMD_H

#include <stdint.h>
#include <string.h>

typedef float t3v4 __attribute__((vector_size(16)));
typedef int32_t t3m4 __attribute__((vector_size(16))); /* lane masks: -1 or 0 */

static inline t3v4 v4_load(const float *p) { t3v4 v; memcpy(&v, p, sizeof v); return v; }
static inline void v4_store(float *p, t3v4 v) { memcpy(p, &v, sizeof v); }
static inline t3v4 v4_set1(float x) { t3v4 v = { x, x, x, x }; return v; }
/* m ? a : b, lane by lane */
static inline t3v4 v4_select(t3m4 m, t3v4 a, t3v4 b) {
  t3m4 r = (m & (t3m4)a) | (~m & (t3m4)b);
  return (t3v4)r;
}
/* bit i set when lane i of the mask is set */
static inline unsigned m4_bits(t3m4 m) {
  return (unsigned)((m[0] & 1) | (m[1] & 2) | (m[2] & 4) | (m[3] & 8));
}

#endif
