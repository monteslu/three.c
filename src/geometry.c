/* BufferGeometry and three.js's geometry generators. Vertex order, index
 * order, groups and UVs follow three.js exactly, so a scene built here
 * rasterises the same triangles as the same scene in three.js. */
#include <math.h>
#include <string.h>

#include "internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

t3_geometry *t3_geometry_new(void) {
  t3_geometry *g = t3__alloc(sizeof *g, T3_KIND_GEOMETRY);
  g->id = t3__next_id();
  g->draw_count = -1;
  g->version = 1;
  return g;
}

void t3_geometry_set_attribute(t3_geometry *g, t3_attr_slot slot, t3_attribute *a) {
  t3_retain(a);
  t3_release(g->attributes[slot]);
  g->attributes[slot] = a;
  g->version++;
}

void t3_geometry_set_named_attribute(t3_geometry *g, const char *name, t3_attribute *a) {
  if (strlen(name) >= sizeof g->named[0].name) t3__fatal("attribute name too long");
  int i = 0;
  while (i < g->named_count && strcmp(g->named[i].name, name)) i++;
  if (i == g->named_count) {
    if (!a) return;
    g->named = realloc(g->named, (size_t)(g->named_count + 1) * sizeof *g->named);
    T3_CHECK_ALLOC(g->named);
    strcpy(g->named[i].name, name);
    g->named[i].attribute = NULL;
    g->named_count++;
  }
  t3_retain(a);
  t3_release(g->named[i].attribute);
  g->named[i].attribute = a;
  if (!a) {
    memmove(g->named + i, g->named + i + 1, (size_t)(g->named_count - i - 1) * sizeof *g->named);
    g->named_count--;
  }
  g->version++;
}

t3_attribute *t3_geometry_get_named_attribute(const t3_geometry *g, const char *name) {
  for (int i = 0; i < g->named_count; i++)
    if (!strcmp(g->named[i].name, name)) return g->named[i].attribute;
  return NULL;
}

void t3_geometry_set_index(t3_geometry *g, t3_attribute *index) {
  t3_retain(index);
  t3_release(g->index);
  g->index = index;
  g->version++;
}

void t3_geometry_add_group(t3_geometry *g, int start, int count, int material_index) {
  g->groups = realloc(g->groups, (g->group_count + 1) * sizeof *g->groups);
  T3_CHECK_ALLOC(g->groups);
  g->groups[g->group_count].start = start;
  g->groups[g->group_count].count = count;
  g->groups[g->group_count].material_index = material_index;
  g->group_count++;
}

void t3_geometry_set_morph_attributes(t3_geometry *g, t3_attribute **position, t3_attribute **normal, int count,
                                      bool relative) {
  for (int i = 0; i < g->morph_count; i++) {
    if (g->morph_position) t3_release(g->morph_position[i]);
    if (g->morph_normal) t3_release(g->morph_normal[i]);
  }
  free(g->morph_position);
  free(g->morph_normal);
  g->morph_position = g->morph_normal = NULL;
  g->morph_count = count;
  g->morph_relative = relative;
  if (position) {
    g->morph_position = calloc((size_t)(count ? count : 1), sizeof *g->morph_position);
    T3_CHECK_ALLOC(g->morph_position);
    for (int i = 0; i < count; i++) g->morph_position[i] = t3_retain(position[i]);
  }
  if (normal) {
    g->morph_normal = calloc((size_t)(count ? count : 1), sizeof *g->morph_normal);
    T3_CHECK_ALLOC(g->morph_normal);
    for (int i = 0; i < count; i++) g->morph_normal[i] = t3_retain(normal[i]);
  }
  g->version++;
}

void t3_geometry_compute_bounding_box(t3_geometry *g) {
  t3_attribute *p = g->attributes[T3_ATTR_POSITION];
  t3_box3 b = { { INFINITY, INFINITY, INFINITY }, { -INFINITY, -INFINITY, -INFINITY } };
  if (p) {
    const float *v = p->array;
    for (int i = 0; i < p->count; i++, v += p->item_size) {
      if (v[0] < b.min.x) b.min.x = v[0];
      if (v[1] < b.min.y) b.min.y = v[1];
      if (v[2] < b.min.z) b.min.z = v[2];
      if (v[0] > b.max.x) b.max.x = v[0];
      if (v[1] > b.max.y) b.max.y = v[1];
      if (v[2] > b.max.z) b.max.z = v[2];
    }
  }
  g->bounding_box = b;
  g->has_bounding_box = true;
}

void t3_geometry_compute_bounding_sphere(t3_geometry *g) {
  t3__bounds_epoch++;
  t3_attribute *p = g->attributes[T3_ATTR_POSITION];
  t3_sphere s = { { 0, 0, 0 }, 0 };
  if (p && p->count) {
    t3_geometry_compute_bounding_box(g);
    t3_box3 b = g->bounding_box;
    s.center = t3_vec3_scale(t3_vec3_add(b.min, b.max), 0.5f);
    float max_sq = 0;
    const float *v = p->array;
    for (int i = 0; i < p->count; i++, v += p->item_size) {
      float dx = v[0] - s.center.x, dy = v[1] - s.center.y, dz = v[2] - s.center.z;
      float d = dx * dx + dy * dy + dz * dz;
      if (d > max_sq) max_sq = d;
    }
    s.radius = sqrtf(max_sq);
  }
  g->bounding_sphere = s;
  g->has_bounding_sphere = true;
}

static t3_vec3 vget(const float *a, int i) { return t3_v3(a[i * 3], a[i * 3 + 1], a[i * 3 + 2]); }

void t3_geometry_compute_vertex_normals(t3_geometry *g) {
  t3_attribute *p = g->attributes[T3_ATTR_POSITION];
  if (!p) return;
  t3_attribute *n = g->attributes[T3_ATTR_NORMAL];
  if (!n || n->count != p->count) {
    n = t3_attribute_new(T3_FLOAT32, NULL, p->count, 3);
    t3_geometry_set_attribute(g, T3_ATTR_NORMAL, n);
    t3_release(n);
  } else {
    memset(n->array, 0, (size_t)n->count * 3 * sizeof(float));
  }
  const float *pos = p->array;
  float *nor = n->array;
  int tri = g->index ? g->index->count / 3 : p->count / 3;
  for (int t = 0; t < tri; t++) {
    int ia, ib, ic;
    if (g->index) {
      if (g->index->type == T3_UINT16) {
        const uint16_t *ix = g->index->array;
        ia = ix[t * 3]; ib = ix[t * 3 + 1]; ic = ix[t * 3 + 2];
      } else {
        const uint32_t *ix = g->index->array;
        ia = (int)ix[t * 3]; ib = (int)ix[t * 3 + 1]; ic = (int)ix[t * 3 + 2];
      }
    } else {
      ia = t * 3; ib = t * 3 + 1; ic = t * 3 + 2;
    }
    t3_vec3 a = vget(pos, ia), b = vget(pos, ib), c = vget(pos, ic);
    t3_vec3 cb = t3_vec3_cross(t3_vec3_sub(c, b), t3_vec3_sub(a, b));
    int ids[3] = { ia, ib, ic };
    for (int k = 0; k < 3; k++) {
      if (g->index) {
        nor[ids[k] * 3] += cb.x; nor[ids[k] * 3 + 1] += cb.y; nor[ids[k] * 3 + 2] += cb.z;
      } else {
        nor[ids[k] * 3] = cb.x; nor[ids[k] * 3 + 1] = cb.y; nor[ids[k] * 3 + 2] = cb.z;
      }
    }
  }
  for (int i = 0; i < n->count; i++) {
    t3_vec3 v = t3_vec3_normalize(vget(nor, i));
    nor[i * 3] = v.x; nor[i * 3 + 1] = v.y; nor[i * 3 + 2] = v.z;
  }
  n->version++;
}

/* ── a growable vertex / index builder ───────────────────────────── */
typedef struct {
  float *pos, *nor, *uv;
  uint32_t *idx;
  int nv, ni, cv, ci;
} builder;

static void push_vertex(builder *b, float px, float py, float pz, float nx, float ny, float nz, float u, float v) {
  if (b->nv == b->cv) {
    b->cv = b->cv ? b->cv * 2 : 64;
    b->pos = realloc(b->pos, b->cv * 3 * sizeof(float));
    b->nor = realloc(b->nor, b->cv * 3 * sizeof(float));
    b->uv = realloc(b->uv, b->cv * 2 * sizeof(float));
    T3_CHECK_ALLOC(b->pos); T3_CHECK_ALLOC(b->nor); T3_CHECK_ALLOC(b->uv);
  }
  float *p = b->pos + b->nv * 3, *n = b->nor + b->nv * 3, *t = b->uv + b->nv * 2;
  p[0] = px; p[1] = py; p[2] = pz;
  n[0] = nx; n[1] = ny; n[2] = nz;
  t[0] = u; t[1] = v;
  b->nv++;
}

static void push_tri(builder *b, uint32_t a, uint32_t c, uint32_t d) {
  if (b->ni + 3 > b->ci) {
    b->ci = b->ci ? b->ci * 2 : 192;
    b->idx = realloc(b->idx, b->ci * sizeof(uint32_t));
    T3_CHECK_ALLOC(b->idx);
  }
  b->idx[b->ni++] = a; b->idx[b->ni++] = c; b->idx[b->ni++] = d;
}

/* BufferGeometry.setIndex picks Uint16 when the largest index fits (three.js:
 * arrayMax(index) > 65535 ? Uint32 : Uint16). */
static t3_geometry *builder_finish(builder *b, t3_geometry *g) {
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3); t3_geometry_set_attribute(g, T3_ATTR_POSITION, a); t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->nor, b->nv, 3); t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a); t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2); t3_geometry_set_attribute(g, T3_ATTR_UV, a); t3_release(a);
  if (b->ni) {
    uint32_t mx = 0;
    for (int i = 0; i < b->ni; i++) if (b->idx[i] > mx) mx = b->idx[i];
    if (mx > 65535) {
      a = t3_attribute_new(T3_UINT32, b->idx, b->ni, 1);
    } else {
      a = t3_attribute_new(T3_UINT16, NULL, b->ni, 1);
      uint16_t *d = a->array;
      for (int i = 0; i < b->ni; i++) d[i] = (uint16_t)b->idx[i];
    }
    t3_geometry_set_index(g, a);
    t3_release(a);
  }
  free(b->pos); free(b->nor); free(b->uv); free(b->idx);
  t3_geometry_compute_bounding_sphere(g);
  return g;
}

/* ── BoxGeometry ─────────────────────────────────────────────────── */
static void box_plane(builder *b, t3_geometry *g, int u, int v, int w, float udir, float vdir,
                      float width, float height, float depth, int gx, int gy, int mat, int *group_start) {
  float sw = width / gx, sh = height / gy, wh = width / 2, hh = height / 2, dh = depth / 2;
  int gx1 = gx + 1, gy1 = gy + 1, base = b->nv, count = 0;
  for (int iy = 0; iy < gy1; iy++) {
    float y = iy * sh - hh;
    for (int ix = 0; ix < gx1; ix++) {
      float x = ix * sw - wh, p[3], n[3];
      p[u] = x * udir; p[v] = y * vdir; p[w] = dh;
      n[u] = 0; n[v] = 0; n[w] = depth > 0 ? 1 : -1;
      push_vertex(b, p[0], p[1], p[2], n[0], n[1], n[2], (float)ix / gx, 1 - (float)iy / gy);
    }
  }
  for (int iy = 0; iy < gy; iy++)
    for (int ix = 0; ix < gx; ix++) {
      uint32_t a = base + ix + gx1 * iy, bb = base + ix + gx1 * (iy + 1);
      uint32_t c = base + (ix + 1) + gx1 * (iy + 1), d = base + (ix + 1) + gx1 * iy;
      push_tri(b, a, bb, d);
      push_tri(b, bb, c, d);
      count += 6;
    }
  t3_geometry_add_group(g, *group_start, count, mat);
  *group_start += count;
}

t3_geometry *t3_box_geometry_new(float w, float h, float d, int ws, int hs, int ds) {
  if (ws < 1) ws = 1;
  if (hs < 1) hs = 1;
  if (ds < 1) ds = 1;
  t3_geometry *g = t3_geometry_new();
  builder b = { 0 };
  int gs = 0;
  enum { X, Y, Z };
  box_plane(&b, g, Z, Y, X, -1, -1, d, h, w, ds, hs, 0, &gs);
  box_plane(&b, g, Z, Y, X, 1, -1, d, h, -w, ds, hs, 1, &gs);
  box_plane(&b, g, X, Z, Y, 1, 1, w, d, h, ws, ds, 2, &gs);
  box_plane(&b, g, X, Z, Y, 1, -1, w, d, -h, ws, ds, 3, &gs);
  box_plane(&b, g, X, Y, Z, 1, -1, w, h, d, ws, hs, 4, &gs);
  box_plane(&b, g, X, Y, Z, -1, -1, w, h, -d, ws, hs, 5, &gs);
  return builder_finish(&b, g);
}

/* ── PlaneGeometry ───────────────────────────────────────────────── */
t3_geometry *t3_plane_geometry_new(float w, float h, int gx, int gy) {
  if (gx < 1) gx = 1;
  if (gy < 1) gy = 1;
  t3_geometry *g = t3_geometry_new();
  builder b = { 0 };
  float sw = w / gx, sh = h / gy;
  int gx1 = gx + 1;
  for (int iy = 0; iy < gy + 1; iy++) {
    float y = iy * sh - h / 2;
    for (int ix = 0; ix < gx1; ix++)
      push_vertex(&b, ix * sw - w / 2, -y, 0, 0, 0, 1, (float)ix / gx, 1 - (float)iy / gy);
  }
  for (int iy = 0; iy < gy; iy++)
    for (int ix = 0; ix < gx; ix++) {
      uint32_t a = ix + gx1 * iy, bb = ix + gx1 * (iy + 1), c = (ix + 1) + gx1 * (iy + 1), d = (ix + 1) + gx1 * iy;
      push_tri(&b, a, bb, d);
      push_tri(&b, bb, c, d);
    }
  return builder_finish(&b, g);
}

/* ── SphereGeometry ──────────────────────────────────────────────── */
t3_geometry *t3_sphere_geometry_new_ex(float radius, int ws, int hs, float phi_start, float phi_len,
                                       float theta_start, float theta_len) {
  if (ws < 3) ws = 3;
  if (hs < 2) hs = 2;
  float theta_end = fminf(theta_start + theta_len, (float)M_PI);
  t3_geometry *g = t3_geometry_new();
  builder b = { 0 };
  uint32_t *grid = malloc((size_t)(hs + 1) * (ws + 1) * sizeof *grid);
  T3_CHECK_ALLOC(grid);
  uint32_t index = 0;
  for (int iy = 0; iy <= hs; iy++) {
    float v = (float)iy / hs, u_off = 0;
    if (iy == 0 && theta_start == 0) u_off = 0.5f / ws;
    else if (iy == hs && theta_end == (float)M_PI) u_off = -0.5f / ws;
    for (int ix = 0; ix <= ws; ix++) {
      float u = (float)ix / ws;
      float px = -radius * cosf(phi_start + u * phi_len) * sinf(theta_start + v * theta_len);
      float py = radius * cosf(theta_start + v * theta_len);
      float pz = radius * sinf(phi_start + u * phi_len) * sinf(theta_start + v * theta_len);
      t3_vec3 n = t3_vec3_normalize(t3_v3(px, py, pz));
      push_vertex(&b, px, py, pz, n.x, n.y, n.z, u + u_off, 1 - v);
      grid[iy * (ws + 1) + ix] = index++;
    }
  }
  for (int iy = 0; iy < hs; iy++)
    for (int ix = 0; ix < ws; ix++) {
      uint32_t a = grid[iy * (ws + 1) + ix + 1], bb = grid[iy * (ws + 1) + ix];
      uint32_t c = grid[(iy + 1) * (ws + 1) + ix], d = grid[(iy + 1) * (ws + 1) + ix + 1];
      if (iy != 0 || theta_start > 0) push_tri(&b, a, bb, d);
      if (iy != hs - 1 || theta_end < (float)M_PI) push_tri(&b, bb, c, d);
    }
  free(grid);
  return builder_finish(&b, g);
}

t3_geometry *t3_sphere_geometry_new(float radius, int ws, int hs) {
  return t3_sphere_geometry_new_ex(radius, ws, hs, 0, (float)(M_PI * 2), 0, (float)M_PI);
}

/* ── CylinderGeometry ────────────────────────────────────────────── */
static void cylinder_cap(builder *b, t3_geometry *g, bool top, float rt, float rb, float hh, int rs,
                         float ts, float tl, int *group_start) {
  uint32_t center_start = b->nv;
  float radius = top ? rt : rb, sign = top ? 1.0f : -1.0f;
  int count = 0;
  for (int x = 1; x <= rs; x++) push_vertex(b, 0, hh * sign, 0, 0, sign, 0, 0.5f, 0.5f);
  uint32_t center_end = b->nv;
  for (int x = 0; x <= rs; x++) {
    float u = (float)x / rs, th = u * tl + ts, c = cosf(th), s = sinf(th);
    push_vertex(b, radius * s, hh * sign, radius * c, 0, sign, 0, c * 0.5f + 0.5f, s * 0.5f * sign + 0.5f);
  }
  for (int x = 0; x < rs; x++) {
    uint32_t c = center_start + x, i = center_end + x;
    if (top) push_tri(b, i, i + 1, c);
    else push_tri(b, i + 1, i, c);
    count += 3;
  }
  t3_geometry_add_group(g, *group_start, count, top ? 1 : 2);
  *group_start += count;
}

t3_geometry *t3_cylinder_geometry_new(float rt, float rb, float h, int rs, int hs, bool open_ended) {
  if (rs < 3) rs = 3;
  if (hs < 1) hs = 1;
  const float ts = 0, tl = (float)(M_PI * 2);
  t3_geometry *g = t3_geometry_new();
  builder b = { 0 };
  float hh = h / 2, slope = (rb - rt) / h;
  uint32_t *rows = malloc((size_t)(hs + 1) * (rs + 1) * sizeof *rows);
  T3_CHECK_ALLOC(rows);
  int gs = 0, count = 0;
  for (int y = 0; y <= hs; y++) {
    float v = (float)y / hs, radius = v * (rb - rt) + rt;
    for (int x = 0; x <= rs; x++) {
      float u = (float)x / rs, th = u * tl + ts, s = sinf(th), c = cosf(th);
      t3_vec3 n = t3_vec3_normalize(t3_v3(s, slope, c));
      rows[y * (rs + 1) + x] = b.nv;
      push_vertex(&b, radius * s, -v * h + hh, radius * c, n.x, n.y, n.z, u, 1 - v);
    }
  }
  for (int x = 0; x < rs; x++)
    for (int y = 0; y < hs; y++) {
      uint32_t a = rows[y * (rs + 1) + x], bb = rows[(y + 1) * (rs + 1) + x];
      uint32_t c = rows[(y + 1) * (rs + 1) + x + 1], d = rows[y * (rs + 1) + x + 1];
      push_tri(&b, a, bb, d);
      push_tri(&b, bb, c, d);
      count += 6;
    }
  free(rows);
  t3_geometry_add_group(g, gs, count, 0);
  gs += count;
  if (!open_ended) {
    if (rt > 0) cylinder_cap(&b, g, true, rt, rb, hh, rs, ts, tl, &gs);
    if (rb > 0) cylinder_cap(&b, g, false, rt, rb, hh, rs, ts, tl, &gs);
  }
  return builder_finish(&b, g);
}

/* ── TorusGeometry ───────────────────────────────────────────────── */
t3_geometry *t3_torus_geometry_new(float radius, float tube, int rs, int ts, float arc) {
  t3_geometry *g = t3_geometry_new();
  builder b = { 0 };
  for (int j = 0; j <= rs; j++)
    for (int i = 0; i <= ts; i++) {
      float u = (float)i / ts * arc, v = (float)j / rs * (float)(M_PI * 2);
      float px = (radius + tube * cosf(v)) * cosf(u), py = (radius + tube * cosf(v)) * sinf(u), pz = tube * sinf(v);
      t3_vec3 n = t3_vec3_normalize(t3_v3(px - radius * cosf(u), py - radius * sinf(u), pz));
      push_vertex(&b, px, py, pz, n.x, n.y, n.z, (float)i / ts, (float)j / rs);
    }
  for (int j = 1; j <= rs; j++)
    for (int i = 1; i <= ts; i++) {
      uint32_t a = (ts + 1) * j + i - 1, bb = (ts + 1) * (j - 1) + i - 1;
      uint32_t c = (ts + 1) * (j - 1) + i, d = (ts + 1) * j + i;
      push_tri(&b, a, bb, d);
      push_tri(&b, bb, c, d);
    }
  return builder_finish(&b, g);
}

/* ── CircleGeometry ──────────────────────────────────────────────── */
t3_geometry *t3_circle_geometry_new(float radius, int segments) {
  if (segments < 3) segments = 3;
  t3_geometry *g = t3_geometry_new();
  builder b = { 0 };
  push_vertex(&b, 0, 0, 0, 0, 0, 1, 0.5f, 0.5f);
  for (int s = 0; s <= segments; s++) {
    float seg = (float)s / segments * (float)(M_PI * 2);
    float x = radius * cosf(seg), y = radius * sinf(seg);
    push_vertex(&b, x, y, 0, 0, 0, 1, (x / radius + 1) / 2, (y / radius + 1) / 2);
  }
  for (int i = 1; i <= segments; i++) push_tri(&b, i, i + 1, 0);
  return builder_finish(&b, g);
}

/* ── ConeGeometry ────────────────────────────────────────────────── */
t3_geometry *t3_cone_geometry_new(float radius, float h, int rs, int hs, bool open_ended) {
  return t3_cylinder_geometry_new(0, radius, h, rs, hs, open_ended);
}

/* ── RingGeometry ────────────────────────────────────────────────── */
t3_geometry *t3_ring_geometry_new(float inner, float outer, int ts, int ps) {
  if (ts < 3) ts = 3;
  if (ps < 1) ps = 1;
  const float tl = (float)(M_PI * 2);
  t3_geometry *g = t3_geometry_new();
  builder b = { 0 };
  float radius = inner, step = (outer - inner) / ps;
  for (int j = 0; j <= ps; j++) {
    for (int i = 0; i <= ts; i++) {
      float seg = (float)i / ts * tl;
      float x = radius * cosf(seg), y = radius * sinf(seg);
      push_vertex(&b, x, y, 0, 0, 0, 1, (x / outer + 1) / 2, (y / outer + 1) / 2);
    }
    radius += step;
  }
  for (int j = 0; j < ps; j++) {
    int level = j * (ts + 1);
    for (int i = 0; i < ts; i++) {
      uint32_t seg = i + level, a = seg, bb = seg + ts + 1, c = seg + ts + 2, d = seg + 1;
      push_tri(&b, a, bb, d);
      push_tri(&b, bb, c, d);
    }
  }
  return builder_finish(&b, g);
}

/* ── PolyhedronGeometry / IcosahedronGeometry ────────────────────── */
typedef struct { float *v; int n, cap; } fbuf;
static void fpush(fbuf *b, float x) {
  if (b->n == b->cap) {
    b->cap = b->cap ? b->cap * 2 : 256;
    b->v = realloc(b->v, (size_t)b->cap * sizeof *b->v);
    T3_CHECK_ALLOC(b->v);
  }
  b->v[b->n++] = x;
}
static void fpush3(fbuf *b, t3_vec3 v) { fpush(b, v.x); fpush(b, v.y); fpush(b, v.z); }

static void subdivide_face(fbuf *vb, t3_vec3 a, t3_vec3 b, t3_vec3 c, int detail) {
  int cols = detail + 1;
  t3_vec3 *v = malloc((size_t)(cols + 1) * (cols + 1) * sizeof *v);
  T3_CHECK_ALLOC(v);
#define V(i, j) v[(i) * (cols + 1) + (j)]
  for (int i = 0; i <= cols; i++) {
    t3_vec3 aj = t3_vec3_lerp(a, c, (float)i / cols), bj = t3_vec3_lerp(b, c, (float)i / cols);
    int rows = cols - i;
    for (int j = 0; j <= rows; j++)
      V(i, j) = (j == 0 && i == cols) ? aj : t3_vec3_lerp(aj, bj, (float)j / rows);
  }
  for (int i = 0; i < cols; i++)
    for (int j = 0; j < 2 * (cols - i) - 1; j++) {
      int k = j / 2;
      if (j % 2 == 0) { fpush3(vb, V(i, k + 1)); fpush3(vb, V(i + 1, k)); fpush3(vb, V(i, k)); }
      else { fpush3(vb, V(i, k + 1)); fpush3(vb, V(i + 1, k + 1)); fpush3(vb, V(i + 1, k)); }
    }
#undef V
  free(v);
}

static float azimuth(t3_vec3 v) { return atan2f(v.z, -v.x); }
static float inclination(t3_vec3 v) { return atan2f(-v.y, sqrtf(v.x * v.x + v.z * v.z)); }

t3_geometry *t3_polyhedron_geometry_new(const float *vertices, const int *indices, int index_count, float radius,
                                        int detail) {
  fbuf vb = { 0 }, ub = { 0 };
  for (int i = 0; i < index_count; i += 3) {
    const float *a = vertices + indices[i] * 3, *b = vertices + indices[i + 1] * 3, *c = vertices + indices[i + 2] * 3;
    subdivide_face(&vb, t3_v3(a[0], a[1], a[2]), t3_v3(b[0], b[1], b[2]), t3_v3(c[0], c[1], c[2]), detail);
  }
  for (int i = 0; i < vb.n; i += 3) {
    t3_vec3 p = t3_vec3_scale(t3_vec3_normalize(t3_v3(vb.v[i], vb.v[i + 1], vb.v[i + 2])), radius);
    vb.v[i] = p.x; vb.v[i + 1] = p.y; vb.v[i + 2] = p.z;
  }
  const float pi = (float)M_PI;
  for (int i = 0; i < vb.n; i += 3) {
    t3_vec3 p = t3_v3(vb.v[i], vb.v[i + 1], vb.v[i + 2]);
    fpush(&ub, azimuth(p) / 2 / pi + 0.5f);
    fpush(&ub, 1 - (inclination(p) / pi + 0.5f));
  }
  /* correctUVs */
  for (int i = 0, j = 0; i < vb.n; i += 9, j += 6) {
    t3_vec3 a = t3_v3(vb.v[i], vb.v[i + 1], vb.v[i + 2]), b = t3_v3(vb.v[i + 3], vb.v[i + 4], vb.v[i + 5]);
    t3_vec3 c = t3_v3(vb.v[i + 6], vb.v[i + 7], vb.v[i + 8]);
    t3_vec3 cen = t3_vec3_scale(t3_vec3_add(t3_vec3_add(a, b), c), 1.0f / 3);
    float azi = azimuth(cen);
    t3_vec3 pts[3] = { a, b, c };
    for (int k = 0; k < 3; k++) {
      int s = j + k * 2;
      if (azi < 0 && ub.v[s] == 1) ub.v[s] = ub.v[s] - 1;
      if (pts[k].x == 0 && pts[k].z == 0) ub.v[s] = azi / 2 / pi + 0.5f;
    }
  }
  /* correctSeam */
  for (int i = 0; i < ub.n; i += 6) {
    float x0 = ub.v[i], x1 = ub.v[i + 2], x2 = ub.v[i + 4];
    float mx = fmaxf(x0, fmaxf(x1, x2)), mn = fminf(x0, fminf(x1, x2));
    if (mx > 0.9f && mn < 0.1f) {
      if (x0 < 0.2f) ub.v[i] += 1;
      if (x1 < 0.2f) ub.v[i + 2] += 1;
      if (x2 < 0.2f) ub.v[i + 4] += 1;
    }
  }
  int nv = vb.n / 3;
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, vb.v, nv, 3); t3_geometry_set_attribute(g, T3_ATTR_POSITION, a); t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, vb.v, nv, 3); t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a); t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, ub.v, nv, 2); t3_geometry_set_attribute(g, T3_ATTR_UV, a); t3_release(a);
  free(vb.v);
  free(ub.v);
  if (detail == 0) t3_geometry_compute_vertex_normals(g);
  else t3_geometry_normalize_normals(g);
  t3_geometry_compute_bounding_sphere(g);
  return g;
}

t3_geometry *t3_icosahedron_geometry_new(float radius, int detail) {
  const float t = (1 + sqrtf(5)) / 2;
  const float v[] = { -1, t, 0, 1, t, 0, -1, -t, 0, 1, -t, 0, 0, -1, t, 0, 1, t,
                      0, -1, -t, 0, 1, -t, t, 0, -1, t, 0, 1, -t, 0, -1, -t, 0, 1 };
  const int ix[] = { 0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                     3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1 };
  return t3_polyhedron_geometry_new(v, ix, 60, radius, detail);
}

t3_geometry *t3_octahedron_geometry_new(float radius, int detail) {
  const float v[] = { 1, 0, 0, -1, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 1, 0, 0, -1 };
  const int ix[] = { 0, 2, 4, 0, 4, 3, 0, 3, 5, 0, 5, 2, 1, 2, 5, 1, 5, 3, 1, 3, 4, 1, 4, 2 };
  return t3_polyhedron_geometry_new(v, ix, 24, radius, detail);
}

t3_geometry *t3_tetrahedron_geometry_new(float radius, int detail) {
  const float v[] = { 1, 1, 1, -1, -1, 1, -1, 1, -1, 1, -1, -1 };
  const int ix[] = { 2, 1, 0, 0, 3, 2, 1, 3, 0, 2, 3, 1 };
  return t3_polyhedron_geometry_new(v, ix, 12, radius, detail);
}

t3_geometry *t3_dodecahedron_geometry_new(float radius, int detail) {
  const float t = (1 + sqrtf(5)) / 2, r = 1 / t;
  const float v[] = { -1, -1, -1, -1, -1, 1, -1, 1, -1, -1, 1, 1, 1, -1, -1, 1, -1, 1, 1, 1, -1, 1, 1, 1,
                      0, -r, -t, 0, -r, t, 0, r, -t, 0, r, t,
                      -r, -t, 0, -r, t, 0, r, -t, 0, r, t, 0,
                      -t, 0, -r, t, 0, -r, -t, 0, r, t, 0, r };
  const int ix[] = { 3, 11, 7, 3, 7, 15, 3, 15, 13, 7, 19, 17, 7, 17, 6, 7, 6, 15, 17, 4, 8, 17, 8, 10, 17, 10, 6,
                     8, 0, 16, 8, 16, 2, 8, 2, 10, 0, 12, 1, 0, 1, 18, 0, 18, 16, 6, 10, 2, 6, 2, 13, 6, 13, 15,
                     2, 16, 18, 2, 18, 3, 2, 3, 13, 18, 1, 9, 18, 9, 11, 18, 11, 3, 4, 14, 12, 4, 12, 0, 4, 0, 8,
                     11, 9, 5, 11, 5, 19, 11, 19, 7, 19, 5, 14, 19, 14, 4, 19, 4, 17, 1, 12, 14, 1, 14, 5, 1, 5, 9 };
  return t3_polyhedron_geometry_new(v, ix, 108, radius, detail);
}

/* ── LatheGeometry ───────────────────────────────────────────────── */
t3_geometry *t3_lathe_geometry_new(const t3_vec2 *points, int count, int segments, float phi_start, float phi_length) {
  if (phi_length < 0) phi_length = 0;
  if (phi_length > (float)(M_PI * 2)) phi_length = (float)(M_PI * 2);
  t3_geometry *g = t3_geometry_new();
  builder b = { 0 };
  const float inv = 1.0f / segments;
  for (int i = 0; i <= segments; i++) {
    float phi = phi_start + i * inv * phi_length, sn = sinf(phi), cs = cosf(phi);
    for (int j = 0; j < count; j++)
      push_vertex(&b, points[j].x * sn, points[j].y, points[j].x * cs, 0, 0, 0, (float)i / segments,
                  (float)j / (count - 1));
  }
  for (int i = 0; i < segments; i++)
    for (int j = 0; j < count - 1; j++) {
      uint32_t base = j + i * count, a = base, bb = base + count, c = base + count + 1, d = base + 1;
      push_tri(&b, a, bb, d);
      push_tri(&b, bb, c, d);
    }
  builder_finish(&b, g);
  t3_geometry_compute_vertex_normals(g);
  if (phi_length == (float)(M_PI * 2)) {
    /* closed: average the normals along the seam, whose vertices coincide */
    float *n = g->attributes[T3_ATTR_NORMAL]->array;
    int base = segments * count * 3;
    for (int i = 0, j = 0; i < count; i++, j += 3) {
      t3_vec3 m = t3_vec3_normalize(t3_v3(n[j] + n[base + j], n[j + 1] + n[base + j + 1], n[j + 2] + n[base + j + 2]));
      n[j] = n[base + j] = m.x; n[j + 1] = n[base + j + 1] = m.y; n[j + 2] = n[base + j + 2] = m.z;
    }
  }
  return g;
}

void t3_geometry_normalize_normals(t3_geometry *g) {
  t3_attribute *n = g->attributes[T3_ATTR_NORMAL];
  if (!n) return;
  float *p = n->array;
  for (int i = 0; i < n->count; i++) {
    t3_vec3 v = t3_vec3_normalize(t3_v3(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]));
    p[i * 3] = v.x; p[i * 3 + 1] = v.y; p[i * 3 + 2] = v.z;
  }
  n->version++;
}

/* ── TorusKnotGeometry ───────────────────────────────────────────── */
static t3_vec3 knot_point(float u, float p, float q, float radius) {
  float cu = cosf(u), su = sinf(u), qp = q / p * u, cs = cosf(qp);
  return t3_v3(radius * (2 + cs) * 0.5f * cu, radius * (2 + cs) * su * 0.5f, radius * sinf(qp) * 0.5f);
}

t3_geometry *t3_torus_knot_geometry_new(float radius, float tube, int ts, int rs, float p, float q) {
  t3_geometry *g = t3_geometry_new();
  builder b = { 0 };
  for (int i = 0; i <= ts; i++) {
    float u = (float)i / ts * p * (float)M_PI * 2;
    t3_vec3 p1 = knot_point(u, p, q, radius), p2 = knot_point(u + 0.01f, p, q, radius);
    t3_vec3 T = t3_vec3_sub(p2, p1), N = t3_vec3_add(p2, p1);
    t3_vec3 B = t3_vec3_cross(T, N);
    N = t3_vec3_cross(B, T);
    B = t3_vec3_normalize(B);
    N = t3_vec3_normalize(N);
    for (int j = 0; j <= rs; j++) {
      float v = (float)j / rs * (float)M_PI * 2, cx = -tube * cosf(v), cy = tube * sinf(v);
      t3_vec3 vx = t3_v3(p1.x + (cx * N.x + cy * B.x), p1.y + (cx * N.y + cy * B.y), p1.z + (cx * N.z + cy * B.z));
      t3_vec3 n = t3_vec3_normalize(t3_vec3_sub(vx, p1));
      push_vertex(&b, vx.x, vx.y, vx.z, n.x, n.y, n.z, (float)i / ts, (float)j / rs);
    }
  }
  for (int j = 1; j <= ts; j++)
    for (int i = 1; i <= rs; i++) {
      uint32_t a = (rs + 1) * (j - 1) + (i - 1), bb = (rs + 1) * j + (i - 1), c = (rs + 1) * j + i, d = (rs + 1) * (j - 1) + i;
      push_tri(&b, a, bb, d);
      push_tri(&b, bb, c, d);
    }
  return builder_finish(&b, g);
}

/* ── BufferGeometry.toNonIndexed ─────────────────────────────────── */
t3_geometry *t3_geometry_to_non_indexed(const t3_geometry *g) {
  if (!g->index) return t3_retain((void *)g);
  t3_geometry *o = t3_geometry_new();
  const t3_attribute *ix = g->index;
  for (int s = 0; s < T3_ATTR_COUNT; s++) {
    const t3_attribute *a = g->attributes[s];
    if (!a) continue;
    t3_attribute *na = t3_attribute_new(a->type, NULL, ix->count, a->item_size);
    na->normalized = a->normalized;
    size_t es = a->type == T3_UINT16 ? 2 : 4, isz = es * (size_t)a->item_size;
    for (int i = 0; i < ix->count; i++) {
      uint32_t k = ix->type == T3_UINT16 ? ((const uint16_t *)ix->array)[i] : ((const uint32_t *)ix->array)[i];
      memcpy((char *)na->array + i * isz, (const char *)a->array + k * isz, isz);
    }
    t3_geometry_set_attribute(o, (t3_attr_slot)s, na);
    t3_release(na);
  }
  for (int i = 0; i < g->group_count; i++) t3_geometry_add_group(o, g->groups[i].start, g->groups[i].count, g->groups[i].material_index);
  return o;
}

/* ── BufferGeometry.applyMatrix4 / scale ─────────────────────────── */
void t3_geometry_apply_matrix4(t3_geometry *g, const t3_mat4 *m) {
  const float *e = m->e;
  t3_attribute *a = g->attributes[T3_ATTR_POSITION];
  if (a) {
    float *p = a->array;
    for (int i = 0; i < a->count; i++, p += 3) {
      float x = p[0], y = p[1], z = p[2];
      float w = 1 / (e[3] * x + e[7] * y + e[11] * z + e[15]);
      p[0] = (e[0] * x + e[4] * y + e[8] * z + e[12]) * w;
      p[1] = (e[1] * x + e[5] * y + e[9] * z + e[13]) * w;
      p[2] = (e[2] * x + e[6] * y + e[10] * z + e[14]) * w;
    }
    a->version++;
  }
  a = g->attributes[T3_ATTR_NORMAL];
  if (a) {
    /* applyNormalMatrix: the normal matrix, then normalize */
    t3_mat3 nm;
    t3_mat3_normal_matrix(&nm, m);
    const float *n = nm.e;
    float *p = a->array;
    for (int i = 0; i < a->count; i++, p += 3) {
      t3_vec3 v = t3_vec3_normalize(t3_v3(n[0] * p[0] + n[3] * p[1] + n[6] * p[2], n[1] * p[0] + n[4] * p[1] + n[7] * p[2],
                                          n[2] * p[0] + n[5] * p[1] + n[8] * p[2]));
      p[0] = v.x; p[1] = v.y; p[2] = v.z;
    }
    a->version++;
  }
  a = g->attributes[T3_ATTR_TANGENT];
  if (a) {
    /* transformDirection on xyz */
    float *p = a->array;
    for (int i = 0; i < a->count; i++, p += a->item_size) {
      t3_vec3 v = t3_vec3_normalize(t3_v3(e[0] * p[0] + e[4] * p[1] + e[8] * p[2], e[1] * p[0] + e[5] * p[1] + e[9] * p[2],
                                          e[2] * p[0] + e[6] * p[1] + e[10] * p[2]));
      p[0] = v.x; p[1] = v.y; p[2] = v.z;
    }
    a->version++;
  }
  if (g->has_bounding_box) t3_geometry_compute_bounding_box(g);
  if (g->has_bounding_sphere) t3_geometry_compute_bounding_sphere(g);
}

void t3_geometry_scale(t3_geometry *g, float x, float y, float z) {
  t3_mat4 m = { { x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1 } };
  t3_geometry_apply_matrix4(g, &m);
}

/* BufferGeometry.rotateX / rotateY / rotateZ / translate */
void t3_geometry_rotate_x(t3_geometry *g, float angle) {
  float c = cosf(angle), s = sinf(angle);
  t3_mat4 m = { { 1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1 } };
  t3_geometry_apply_matrix4(g, &m);
}
void t3_geometry_rotate_y(t3_geometry *g, float angle) {
  float c = cosf(angle), s = sinf(angle);
  t3_mat4 m = { { c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1 } };
  t3_geometry_apply_matrix4(g, &m);
}
void t3_geometry_rotate_z(t3_geometry *g, float angle) {
  float c = cosf(angle), s = sinf(angle);
  t3_mat4 m = { { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } };
  t3_geometry_apply_matrix4(g, &m);
}
void t3_geometry_translate(t3_geometry *g, float x, float y, float z) {
  t3_mat4 m = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1 } };
  t3_geometry_apply_matrix4(g, &m);
}
