/* Image bytes on stdin -> "w h\n" + RGBA8 rows on stdout (stb_image), for
 * the Node reference renders (tools/ref-r186.mjs builds it on first use into
 * build/native/): Node has no image decoder. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "../third_party/stb_image.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  size_t cap = 1 << 20, n = 0, got;
  unsigned char *buf = malloc(cap);
  while (buf && (got = fread(buf + n, 1, cap - n, stdin)) > 0) {
    n += got;
    if (n == cap) buf = realloc(buf, cap *= 2);
  }
  int w, h, c;
  unsigned char *px = buf ? stbi_load_from_memory(buf, (int)n, &w, &h, &c, 4) : NULL;
  if (!px) { fprintf(stderr, "imgdecode: %s\n", stbi_failure_reason()); return 1; }
  printf("%d %d\n", w, h);
  fwrite(px, 1, (size_t)w * h * 4, stdout);
  return 0;
}
