/* Raycaster: setFromCamera, intersectObject(s) against meshes, with
 * Mesh.raycast's bounding-sphere early out, the ray in object space, and
 * Ray.intersectTriangle honouring the material's side. Results are sorted
 * by distance and owned by the raycaster (valid until its next query). */
#include <math.h>
#include <string.h>

#include "internal.h"
#include "simd.h"

typedef struct { uint32_t k; int r; void (*destroy)(void *); } misc_header;

/* The candidates of one intersectObjects call, flattened: the meshes in
 * traversal order and their world bounding spheres as arrays, padded to a
 * multiple of 4 with spheres no ray reaches. Rebuilt only when the query's
 * roots change or any world matrix / bounds / the scene graph changed, so the
 * rays of a frame share one build. */
typedef struct {
  t3_object *const *roots; int root_n; bool recursive;
  uint32_t world_epoch, bounds_epoch, graph_epoch, layers;
  t3_mesh **mesh;
  float *cx, *cy, *cz, *r;
  int n, cap;
  /* the objects left to the embedder, in traversal order */
  t3_object **other; int other_n, other_cap;
} batch;
static void batch_free(batch *b) {
  if (!b) return;
  free(b->mesh); free(b->cx); free(b->cy); free(b->cz); free(b->r); free(b->other);
  free(b);
}

static void raycaster_destroy(void *p) {
  t3_raycaster *rc = p;
  free(rc->hits);
  batch_free(rc->_batch);
}

t3_raycaster *t3_raycaster_new(void) {
  t3_raycaster *rc = t3__alloc(sizeof *rc, T3_KIND_MISC);
  ((misc_header *)rc)->destroy = raycaster_destroy;
  rc->ray.direction = t3_v3(0, 0, -1);
  rc->near = 0;
  rc->far = INFINITY;
  rc->layers = 1;
  return rc;
}

void t3_raycaster_set(t3_raycaster *rc, t3_vec3 origin, t3_vec3 direction) {
  rc->ray.origin = origin;
  rc->ray.direction = direction;
}

/* Raycaster.setFromCamera: NDC (x, y) through the camera. */
void t3_raycaster_set_from_camera(t3_raycaster *rc, float x, float y, const t3_camera *cam) {
  if (cam->base.type == T3_PERSPECTIVE_CAMERA) {
    rc->ray.origin = t3_mat4_get_position(&cam->base.matrix_world);
    /* Vector3(x, y, 0.5).unproject(camera) */
    t3_vec3 p = t3_vec3_apply_mat4(t3_v3(x, y, 0.5f), &cam->projection_matrix_inverse);
    p = t3_vec3_apply_mat4(p, &cam->base.matrix_world);
    rc->ray.direction = t3_vec3_normalize(t3_vec3_sub(p, rc->ray.origin));
  } else {
    float z = (cam->near + cam->far) / (cam->near - cam->far);
    t3_vec3 p = t3_vec3_apply_mat4(t3_v3(x, y, z), &cam->projection_matrix_inverse);
    rc->ray.origin = t3_vec3_apply_mat4(p, &cam->base.matrix_world);
    rc->ray.direction = t3_vec3_transform_direction(t3_v3(0, 0, -1), &cam->base.matrix_world);
  }
}

/* Ray.intersectTriangle: distance along the ray, or -1. */
static float ray_triangle(const t3_ray *r, t3_vec3 a, t3_vec3 b, t3_vec3 c, bool backface_culling) {
  t3_vec3 e1 = t3_vec3_sub(b, a), e2 = t3_vec3_sub(c, a), n = t3_vec3_cross(e1, e2);
  float ddn = t3_vec3_dot(r->direction, n), sign;
  if (ddn > 0) {
    if (backface_culling) return -1;
    sign = 1;
  } else if (ddn < 0) {
    sign = -1;
    ddn = -ddn;
  } else {
    return -1;
  }
  t3_vec3 diff = t3_vec3_sub(r->origin, a);
  float ddqxe2 = sign * t3_vec3_dot(r->direction, t3_vec3_cross(diff, e2));
  if (ddqxe2 < 0) return -1;
  float dde1xq = sign * t3_vec3_dot(r->direction, t3_vec3_cross(e1, diff));
  if (dde1xq < 0) return -1;
  if (ddqxe2 + dde1xq > ddn) return -1;
  float qdn = -sign * t3_vec3_dot(diff, n);
  if (qdn < 0) return -1;
  return qdn / ddn;
}

static void push_hit(t3_raycaster *rc, t3_intersection h) {
  if (rc->hit_count == rc->hit_cap) {
    rc->hit_cap = rc->hit_cap ? rc->hit_cap * 2 : 64;
    rc->hits = realloc(rc->hits, (size_t)rc->hit_cap * sizeof *rc->hits);
    T3_CHECK_ALLOC(rc->hits);
  }
  rc->hits[rc->hit_count++] = h;
}

static uint32_t index_at(const t3_attribute *ix, int i) {
  return ix->type == T3_UINT16 ? ((const uint16_t *)ix->array)[i] : ((const uint32_t *)ix->array)[i];
}

static bool raycastable(const t3_mesh *m) {
  const t3_geometry *g = m->geometry;
  return g && m->material_count && g->attributes[T3_ATTR_POSITION];
}

/* Mesh.raycast past its bounding-sphere test: the triangles */
static void mesh_raycast_triangles(t3_raycaster *rc, t3_mesh *m) {
  t3_geometry *g = m->geometry;
  const t3_mat4 *mw = &m->base.matrix_world;
  /* the ray in object space */
  t3_mat4 inv;
  t3_mat4_invert(&inv, mw);
  t3_ray lr;
  lr.origin = t3_vec3_apply_mat4(rc->ray.origin, &inv);
  lr.direction = t3_vec3_normalize(t3_vec3_sub(t3_vec3_apply_mat4(t3_vec3_add(rc->ray.direction, rc->ray.origin), &inv), lr.origin));
  const float *pos = g->attributes[T3_ATTR_POSITION]->array;
  int total = g->index ? g->index->count : g->attributes[T3_ATTR_POSITION]->count;
  int dstart = g->draw_start > 0 ? g->draw_start : 0;
  int dend = g->draw_count < 0 ? total : dstart + g->draw_count;
  if (dend > total) dend = total;
  /* with groups and a material array, each group is tested with its
   * own material (and reports its index); otherwise the draw range */
  int ng = (g->group_count && m->material_count > 1) ? g->group_count : 1;
  for (int gi = 0; gi < ng; gi++) {
  int start = dstart, end = dend, mi = 0;
  const t3_material *mat = m->materials[0];
  if (ng > 1 || (g->group_count && m->material_count > 1)) {
    const t3_group *gr = &g->groups[gi];
    mi = gr->material_index;
    if (mi < 0 || mi >= m->material_count) continue;
    mat = m->materials[mi];
    if (gr->start > start) start = gr->start;
    if (gr->start + gr->count < end) end = gr->start + gr->count;
  }
  if (!mat) continue;
  for (int i = start; i + 2 < end; i += 3) {
    uint32_t ia = g->index ? index_at(g->index, i) : (uint32_t)i;
    uint32_t ib = g->index ? index_at(g->index, i + 1) : (uint32_t)i + 1;
    uint32_t ic = g->index ? index_at(g->index, i + 2) : (uint32_t)i + 2;
    t3_vec3 a = t3_v3(pos[ia * 3], pos[ia * 3 + 1], pos[ia * 3 + 2]);
    t3_vec3 b = t3_v3(pos[ib * 3], pos[ib * 3 + 1], pos[ib * 3 + 2]);
    t3_vec3 c = t3_v3(pos[ic * 3], pos[ic * 3 + 1], pos[ic * 3 + 2]);
    float t = mat->side == T3_BACK_SIDE ? ray_triangle(&lr, c, b, a, true)
                                        : ray_triangle(&lr, a, b, c, mat->side != T3_DOUBLE_SIDE);
    if (t < 0) continue;
    t3_vec3 local = t3_vec3_add(lr.origin, t3_vec3_scale(lr.direction, t));
    t3_vec3 world = t3_vec3_apply_mat4(local, mw);
    float dist = t3_vec3_distance(rc->ray.origin, world);
    if (dist < rc->near || dist > rc->far) continue;
    t3_intersection h = { dist, world, &m->base, (int)(i / 3), ia, ib, ic, mi };
    push_hit(rc, h);
  }
  }
}

static void batch_reserve(batch *b) {
  if (b->n + 4 > b->cap) {
    b->cap = b->cap ? b->cap * 2 : 256;
    b->mesh = realloc(b->mesh, (size_t)b->cap * sizeof *b->mesh);
    b->cx = realloc(b->cx, (size_t)b->cap * sizeof(float));
    b->cy = realloc(b->cy, (size_t)b->cap * sizeof(float));
    b->cz = realloc(b->cz, (size_t)b->cap * sizeof(float));
    b->r = realloc(b->r, (size_t)b->cap * sizeof(float));
    T3_CHECK_ALLOC(b->mesh); T3_CHECK_ALLOC(b->cx); T3_CHECK_ALLOC(b->cy); T3_CHECK_ALLOC(b->cz); T3_CHECK_ALLOC(b->r);
  }
}

static void batch_push(batch *b, t3_mesh *m) {
  batch_reserve(b);
  const float *s = t3__world_sphere(&m->base, m->geometry);
  b->mesh[b->n] = m;
  b->cx[b->n] = s[0]; b->cy[b->n] = s[1]; b->cz[b->n] = s[2]; b->r[b->n] = s[3];
  b->n++;
}

static void other_push(batch *b, t3_object *o) {
  if (b->other_n == b->other_cap) {
    b->other_cap = b->other_cap ? b->other_cap * 2 : 16;
    b->other = realloc(b->other, (size_t)b->other_cap * sizeof *b->other);
    T3_CHECK_ALLOC(b->other);
  }
  b->other[b->other_n++] = o;
}

/* intersectObject's traversal order (the object, then its children); an
 * object outside the raycaster's layers is skipped, its children are not */
static void batch_collect(batch *b, t3_object *o, bool recursive, uint32_t layers) {
  if (o->layers & layers) {
    if (o->raycast_custom) other_push(b, o);
    else if (o->type == T3_MESH) { if (raycastable((t3_mesh *)o)) batch_push(b, (t3_mesh *)o); }
    else if (o->type == T3_INSTANCED_MESH || o->type == T3_SKINNED_MESH || o->type == T3_LINE ||
             o->type == T3_LINE_SEGMENTS || o->type == T3_LINE_LOOP) other_push(b, o);
  }
  if (recursive)
    for (int i = 0; i < o->child_count; i++) batch_collect(b, o->children[i], true, layers);
}

static batch *batch_for(t3_raycaster *rc, t3_object *const *roots, int n, bool recursive) {
  batch *b = rc->_batch;
  if (!b) {
    b = rc->_batch = calloc(1, sizeof *b);
    T3_CHECK_ALLOC(b);
  }
  if (b->roots == roots && b->root_n == n && b->recursive == recursive && b->world_epoch == t3__world_epoch &&
      b->bounds_epoch == t3__bounds_epoch && b->graph_epoch == t3__graph_epoch && b->layers == rc->layers && b->mesh)
    return b;
  b->n = 0;
  b->other_n = 0;
  for (int i = 0; i < n; i++) batch_collect(b, roots[i], recursive, rc->layers);
  /* pad to 4 lanes with NaN spheres: no comparison with them is true */
  batch_reserve(b);
  while (b->n & 3) {
    b->mesh[b->n] = NULL;
    b->cx[b->n] = b->cy[b->n] = b->cz[b->n] = b->r[b->n] = NAN;
    b->n++;
  }
  b->roots = roots; b->root_n = n; b->recursive = recursive;
  b->world_epoch = t3__world_epoch; b->bounds_epoch = t3__bounds_epoch;
  b->graph_epoch = t3__graph_epoch; b->layers = rc->layers;
  return b;
}

/* Ray.intersectsSphere (distanceSqToPoint(center) <= r^2) for 4 spheres at
 * a time: the scalar code's float operations lane by lane, so the same
 * answers. distanceSqToPoint: behind the origin, |o - c|^2; else
 * |dir * d + o - c|^2, d = dot(c - o, dir). */
static void batch_raycast(t3_raycaster *rc, const batch *b) {
  const t3v4 ox = v4_set1(rc->ray.origin.x), oy = v4_set1(rc->ray.origin.y), oz = v4_set1(rc->ray.origin.z);
  const t3v4 dx = v4_set1(rc->ray.direction.x), dy = v4_set1(rc->ray.direction.y), dz = v4_set1(rc->ray.direction.z);
  const t3v4 zero = v4_set1(0);
  for (int i = 0; i < b->n; i += 4) {
    t3v4 cx = v4_load(b->cx + i), cy = v4_load(b->cy + i), cz = v4_load(b->cz + i), r = v4_load(b->r + i);
    /* d = dot(c - o, dir) */
    t3v4 d = (cx - ox) * dx + (cy - oy) * dy + (cz - oz) * dz;
    /* behind the origin: |o - c|^2; else |dir * d + o - c|^2 */
    t3v4 bx = ox - cx, by = oy - cy, bz = oz - cz;
    t3v4 behind = bx * bx + by * by + bz * bz;
    t3v4 px = dx * d + ox - cx, py = dy * d + oy - cy, pz = dz * d + oz - cz;
    t3v4 ahead = px * px + py * py + pz * pz;
    t3v4 dist2 = v4_select(d < zero, behind, ahead);
    unsigned hit = m4_bits(dist2 <= r * r);
    while (hit) {
      int k = __builtin_ctz(hit);
      hit &= hit - 1;
      mesh_raycast_triangles(rc, b->mesh[i + k]);
    }
  }
}

static int hit_cmp(const void *pa, const void *pb) {
  const t3_intersection *a = pa, *b = pb;
  return a->distance < b->distance ? -1 : a->distance > b->distance;
}

int t3_raycaster_intersect_objects_ex(t3_raycaster *rc, t3_object *const *objects, int n, bool recursive,
                                      const t3_intersection **out, void (*other)(t3_object *, void *), void *ctx) {
  rc->hit_count = 0;
  batch *b = batch_for(rc, objects, n, recursive);
  batch_raycast(rc, b);
  if (other)
    for (int i = 0; i < b->other_n; i++) other(b->other[i], ctx);
  if (rc->hit_count > 1) qsort(rc->hits, rc->hit_count, sizeof *rc->hits, hit_cmp);
  if (out) *out = rc->hits;
  return rc->hit_count;
}

int t3_raycaster_intersect_objects(t3_raycaster *rc, t3_object *const *objects, int n, bool recursive,
                                   const t3_intersection **out) {
  rc->hit_count = 0;
  batch_raycast(rc, batch_for(rc, objects, n, recursive));
  if (rc->hit_count > 1) qsort(rc->hits, rc->hit_count, sizeof *rc->hits, hit_cmp);
  if (out) *out = rc->hits;
  return rc->hit_count;
}

int t3_raycaster_intersect_object(t3_raycaster *rc, t3_object *o, bool recursive, const t3_intersection **out) {
  return t3_raycaster_intersect_objects(rc, &o, 1, recursive, out);
}
