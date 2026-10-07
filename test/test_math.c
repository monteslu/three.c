/* t3_sincos against libm, and the vector t3_mat4_multiply against the
 * scalar definition. Exits nonzero on any mismatch beyond the stated bound. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "three.h"

static int ulps(float a, float b) {
  int ia, ib;
  memcpy(&ia, &a, 4);
  memcpy(&ib, &b, 4);
  if ((ia < 0) != (ib < 0)) return a == b ? 0 : 1 << 30;
  return abs(ia - ib);
}

int main(void) {
  int worst = 0, n = 0, off = 0;
  float worst_x = 0;
  for (double x = -1000; x <= 1000; x += 0.000731) {
    float s, c, fx = (float)x;
    t3_sincos(fx, &s, &c);
    /* the correctly rounded reference, from double libm */
    float rs = (float)sin((double)fx), rc = (float)cos((double)fx);
    int u = ulps(s, rs), v = ulps(c, rc);
    if (u > worst) { worst = u; worst_x = fx; }
    if (v > worst) { worst = v; worst_x = fx; }
    if (u || v) off++;
    n++;
  }
  printf("sincos: %d args, %d not correctly rounded, worst %d ulp at %g\n", n, off, worst, worst_x);
  int fail = worst > 1;

  /* control: a sincos that is wrong must be caught */
  float s, c;
  t3_sincos(1.0f, &s, &c);
  if (ulps(s, (float)sin(1.0f + 1e-6)) <= 1) { printf("control failed: differ cannot see 1e-6\n"); fail = 1; }

  srand(1);
  int mism = 0;
  for (int t = 0; t < 10000; t++) {
    t3_mat4 a, b, r;
    for (int i = 0; i < 16; i++) { a.e[i] = rand() / (float)RAND_MAX - 0.5f; b.e[i] = rand() / (float)RAND_MAX - 0.5f; }
    t3_mat4_multiply(&r, &a, &b);
    for (int col = 0; col < 4; col++)
      for (int i = 0; i < 4; i++) {
        float want = a.e[i] * b.e[col * 4] + a.e[4 + i] * b.e[col * 4 + 1] + a.e[8 + i] * b.e[col * 4 + 2] + a.e[12 + i] * b.e[col * 4 + 3];
        if (memcmp(&want, &r.e[col * 4 + i], 4)) mism++;
      }
  }
  printf("mat4_multiply: %d of 160000 elements differ from scalar\n", mism);
  if (mism) fail = 1;
  printf(fail ? "FAIL\n" : "PASS\n");
  return fail;
}
