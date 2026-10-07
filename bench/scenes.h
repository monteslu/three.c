#ifndef BENCH_SCENES_H
#define BENCH_SCENES_H

#include "three.h"

typedef struct {
  const char *name;
  void (*setup)(void);
  void (*frame)(void);
  double (*probe)(void); /* optional: a value to compare with the three.js side */
} bench_scene;

extern const bench_scene bench_scenes[];
t3_renderer *bench_renderer(void);
/* Host-provided: the bytes of an asset from three.lua's compare/assets (native
 * reads the file; the cart reads its .wasc). malloc'd; NULL if missing. */
char *bench_read_asset(const char *name, size_t *len);
/* Host-provided: the mouse in 1280x720 pixels (top-left origin), if the host
 * has one (the cart: wasmcart pointer 0; native: none). */
bool bench_pointer(int *x, int *y);

#endif
