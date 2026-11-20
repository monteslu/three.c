/* t3_ies_texture_parse against three.js's IESLoader: parses the IES file
 * given as argv[1] and writes the texture's red channel (half floats, row by
 * row) to argv[2], with its width and height as two leading uint32.
 * test/test_ies.mjs compares that with IESLoader.parse of the same text,
 * texel for texel, for a symmetric profile, a mirrored 0-180 one and a 0-270
 * one (the loader's missing-360 rule); its control (another profile) must
 * differ. No GL.
 *
 *   cc -std=c99 -O2 -I include test/test_ies.c build/native/libthree.a -lz -lm -o build/test/test_ies */
#include <stdio.h>
#include <stdlib.h>
#include "three.h"

int main(int argc, char **argv) {
  if (argc < 3) return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 2;
  static char text[1 << 16];
  size_t n = fread(text, 1, sizeof text, f);
  fclose(f);
  t3_texture *t = t3_ies_texture_parse(text, n);
  if (!t) return fprintf(stderr, "not parsed\n"), 1;
  if (t->type != T3_HALF_FLOAT_TYPE || t->min_filter != T3_LINEAR || t->mag_filter != T3_LINEAR || t->wrap_t != T3_REPEAT || t->wrap_s != T3_CLAMP_TO_EDGE)
    return fprintf(stderr, "texture settings differ from IESLoader's\n"), 1;
  FILE *o = fopen(argv[2], "wb");
  if (!o) return 2;
  uint32_t wh[2] = { (uint32_t)t->width, (uint32_t)t->height };
  fwrite(wh, 4, 2, o);
  const uint16_t *px = (const uint16_t *)t->pixels;
  for (int i = 0; i < t->width * t->height; i++) fwrite(px + i * 4, 2, 1, o);
  fclose(o);
  t3_release(t);
  /* not IES text: refused */
  if (t3_ies_texture_parse("hello", 5) || t3_ies_texture_parse("TILT=NONE\n1 2 3", 15)) return fprintf(stderr, "junk parsed\n"), 1;
  return 0;
}
