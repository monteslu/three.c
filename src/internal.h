/* Shared between three.c's translation units; not part of the API. */
#ifndef T3_INTERNAL_H
#define T3_INTERNAL_H

#include <stdlib.h>

#include "three.h"

/* Every allocation three.c makes goes through the embedder's allocator
 * (t3_set_allocator), cgltf's and stb_image's included: these replace the
 * libc names in every translation unit that includes this header. */
void *t3__malloc(size_t n);
void *t3__calloc(size_t n, size_t size);
void *t3__realloc(void *p, size_t n);
void t3__free(void *p);
#define malloc(n) t3__malloc(n)
#define calloc(n, size) t3__calloc(n, size)
#define realloc(p, n) t3__realloc(p, n)
#define free(p) t3__free(p)

enum {
  T3_KIND_OBJECT = 0x74334f42,     /* 't3OB' */
  T3_KIND_GEOMETRY = 0x74334745,
  T3_KIND_MATERIAL = 0x74334d41,
  T3_KIND_TEXTURE = 0x74335458,
  T3_KIND_ATTRIBUTE = 0x74334154,
  T3_KIND_CURVE = 0x74334355,
  T3_KIND_MISC = 0x74334d53,       /* raycasters, clips, mixers: free() + their own cleanup */
  T3_KIND_RENDER_TARGET = 0x74335254,
};

uint32_t t3__next_id(void);
void *t3__alloc(size_t size, uint32_t kind);

/* Set by the renderer so releasing a geometry / texture / attribute frees its
 * GL objects (three.js's 'dispose' event). */
extern void (*t3__gl_release)(uint32_t kind, void *thing);

void t3__fatal(const char *what);

/* Bumped when any world matrix is recomputed or the scene graph changes
 * (t3__world_epoch), and when any geometry bounding sphere is computed
 * (t3__bounds_epoch): caches built from world-space bounds key on them. */
extern uint32_t t3__world_epoch, t3__bounds_epoch;
/* bumped when the scene graph's shape changes (an add or a remove) */
extern uint32_t t3__graph_epoch;
/* A mesh's world-space bounding sphere (cx, cy, cz, r): the geometry's
 * sphere through matrix_world, as Mesh.raycast and Frustum.intersectsObject
 * compute it; cached until the world matrix or the geometry's bounds change. */
const float *t3__world_sphere(t3_object *o, t3_geometry *g);
#define T3_CHECK_ALLOC(p) do { if (!(p)) t3__fatal("out of memory"); } while (0)

#endif
