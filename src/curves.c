/* Curves (extras/curves): CatmullRomCurve3, CubicBezierCurve3,
 * EllipseCurve, with Curve's arc-length cache, getPointAt and getLength.
 * 2D curves return their point with z = 0. */
#include <math.h>
#include <string.h>

#include "internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

t3_curve *t3_catmull_rom_curve3_new(const t3_vec3 *points, int n, bool closed, t3_curve_kind kind, float tension) {
  t3_curve *c = t3__alloc(sizeof *c, T3_KIND_CURVE);
  c->kind = kind;
  c->closed = closed;
  c->tension = tension;
  c->points = malloc((size_t)(n ? n : 1) * sizeof *c->points);
  T3_CHECK_ALLOC(c->points);
  memcpy(c->points, points, (size_t)n * sizeof *points);
  c->point_count = n;
  c->arc_length_divisions = 200;
  return c;
}

t3_curve *t3_cubic_bezier_curve3_new(t3_vec3 v0, t3_vec3 v1, t3_vec3 v2, t3_vec3 v3) {
  t3_curve *c = t3__alloc(sizeof *c, T3_KIND_CURVE);
  c->kind = T3_CUBIC_BEZIER_CURVE3;
  c->v[0] = v0; c->v[1] = v1; c->v[2] = v2; c->v[3] = v3;
  c->arc_length_divisions = 200;
  return c;
}

t3_curve *t3_ellipse_curve_new(float ax, float ay, float xr, float yr, float start, float end, bool clockwise,
                               float rotation) {
  t3_curve *c = t3__alloc(sizeof *c, T3_KIND_CURVE);
  c->kind = T3_ELLIPSE_CURVE;
  c->ax = ax; c->ay = ay; c->xr = xr; c->yr = yr;
  c->start = start; c->end = end; c->clockwise = clockwise; c->rotation = rotation;
  c->arc_length_divisions = 200;
  return c;
}

/* CubicPoly: p(s) = c0 + c1 s + c2 s^2 + c3 s^3 */
typedef struct { float c0, c1, c2, c3; } cubic_poly;

static void poly_init(cubic_poly *p, float x0, float x1, float t0, float t1) {
  p->c0 = x0;
  p->c1 = t0;
  p->c2 = -3 * x0 + 3 * x1 - 2 * t0 - t1;
  p->c3 = 2 * x0 - 2 * x1 + t0 + t1;
}
static void poly_nonuniform(cubic_poly *p, float x0, float x1, float x2, float x3, float dt0, float dt1, float dt2) {
  float t1 = (x1 - x0) / dt0 - (x2 - x0) / (dt0 + dt1) + (x2 - x1) / dt1;
  float t2 = (x2 - x1) / dt1 - (x3 - x1) / (dt1 + dt2) + (x3 - x2) / dt2;
  t1 *= dt1;
  t2 *= dt1;
  poly_init(p, x1, x2, t1, t2);
}
static void poly_catmull(cubic_poly *p, float x0, float x1, float x2, float x3, float tension) {
  poly_init(p, x1, x2, tension * (x2 - x0), tension * (x3 - x1));
}
static float poly_calc(const cubic_poly *p, float t) {
  float t2 = t * t, t3 = t2 * t;
  return p->c0 + p->c1 * t + p->c2 * t2 + p->c3 * t3;
}

static t3_vec3 catmull_point(const t3_curve *c, float t) {
  const t3_vec3 *pts = c->points;
  int l = c->point_count;
  float p = (l - (c->closed ? 0 : 1)) * t;
  int ip = (int)floorf(p);
  float weight = p - ip;
  if (c->closed) ip += ip > 0 ? 0 : ((int)floorf((float)abs(ip) / l) + 1) * l;
  else if (weight == 0 && ip == l - 1) { ip = l - 2; weight = 1; }
  t3_vec3 p0, p3;
  if (c->closed || ip > 0) p0 = pts[(ip - 1) % l];
  else p0 = t3_vec3_add(t3_vec3_sub(pts[0], pts[1]), pts[0]);
  t3_vec3 p1 = pts[ip % l], p2 = pts[(ip + 1) % l];
  if (c->closed || ip + 2 < l) p3 = pts[(ip + 2) % l];
  else p3 = t3_vec3_add(t3_vec3_sub(pts[l - 1], pts[l - 2]), pts[l - 1]);
  cubic_poly px, py, pz;
  if (c->kind == T3_CENTRIPETAL || c->kind == T3_CHORDAL) {
    float pw = c->kind == T3_CHORDAL ? 0.5f : 0.25f;
    float dt0 = powf(t3_vec3_distance_sq(p0, p1), pw);
    float dt1 = powf(t3_vec3_distance_sq(p1, p2), pw);
    float dt2 = powf(t3_vec3_distance_sq(p2, p3), pw);
    if (dt1 < 1e-4f) dt1 = 1.0f;
    if (dt0 < 1e-4f) dt0 = dt1;
    if (dt2 < 1e-4f) dt2 = dt1;
    poly_nonuniform(&px, p0.x, p1.x, p2.x, p3.x, dt0, dt1, dt2);
    poly_nonuniform(&py, p0.y, p1.y, p2.y, p3.y, dt0, dt1, dt2);
    poly_nonuniform(&pz, p0.z, p1.z, p2.z, p3.z, dt0, dt1, dt2);
  } else {
    poly_catmull(&px, p0.x, p1.x, p2.x, p3.x, c->tension);
    poly_catmull(&py, p0.y, p1.y, p2.y, p3.y, c->tension);
    poly_catmull(&pz, p0.z, p1.z, p2.z, p3.z, c->tension);
  }
  return t3_v3(poly_calc(&px, weight), poly_calc(&py, weight), poly_calc(&pz, weight));
}

static float bezier(float t, float p0, float p1, float p2, float p3) {
  float k = 1 - t;
  return k * k * k * p0 + 3 * k * k * t * p1 + 3 * (1 - t) * t * t * p2 + t * t * t * p3;
}

static t3_vec3 ellipse_point(const t3_curve *c, float t) {
  const float two_pi = (float)(M_PI * 2);
  float delta = c->end - c->start;
  bool same = fabsf(delta) < 1.1920929e-7f;
  while (delta < 0) delta += two_pi;
  while (delta > two_pi) delta -= two_pi;
  if (delta < 1.1920929e-7f) delta = same ? 0 : two_pi;
  if (c->clockwise && !same) delta = delta == two_pi ? -two_pi : delta - two_pi;
  float angle = c->start + t * delta;
  float x = c->ax + c->xr * cosf(angle), y = c->ay + c->yr * sinf(angle);
  if (c->rotation != 0) {
    float cs = cosf(c->rotation), sn = sinf(c->rotation), tx = x - c->ax, ty = y - c->ay;
    x = tx * cs - ty * sn + c->ax;
    y = tx * sn + ty * cs + c->ay;
  }
  return t3_v3(x, y, 0);
}

t3_vec3 t3_curve_get_point(const t3_curve *c, float t) {
  switch (c->kind) {
  case T3_CUBIC_BEZIER_CURVE3:
    return t3_v3(bezier(t, c->v[0].x, c->v[1].x, c->v[2].x, c->v[3].x),
                 bezier(t, c->v[0].y, c->v[1].y, c->v[2].y, c->v[3].y),
                 bezier(t, c->v[0].z, c->v[1].z, c->v[2].z, c->v[3].z));
  case T3_ELLIPSE_CURVE: return ellipse_point(c, t);
  default: return catmull_point(c, t);
  }
}

/* Curve.getLengths: cumulative chord lengths at arc_length_divisions + 1
 * samples, cached until t3_curve_update_arc_lengths. */
const float *t3_curve_get_lengths(t3_curve *c, int *count) {
  int div = c->arc_length_divisions;
  if (c->lengths && c->lengths_n == div + 1 && !c->needs_update) {
    if (count) *count = c->lengths_n;
    return c->lengths;
  }
  c->needs_update = false;
  if (c->lengths_n != div + 1) {
    free(c->lengths);
    c->lengths = malloc((size_t)(div + 1) * sizeof *c->lengths);
    T3_CHECK_ALLOC(c->lengths);
    c->lengths_n = div + 1;
  }
  t3_vec3 last = t3_curve_get_point(c, 0);
  float sum = 0;
  c->lengths[0] = 0;
  for (int p = 1; p <= div; p++) {
    t3_vec3 cur = t3_curve_get_point(c, (float)p / div);
    sum += t3_vec3_distance(cur, last);
    c->lengths[p] = sum;
    last = cur;
  }
  if (count) *count = c->lengths_n;
  return c->lengths;
}

void t3_curve_update_arc_lengths(t3_curve *c) {
  c->needs_update = true;
  t3_curve_get_lengths(c, NULL);
}

float t3_curve_get_length(t3_curve *c) {
  int n;
  const float *l = t3_curve_get_lengths(c, &n);
  return l[n - 1];
}

/* Curve.getUtoTmapping */
float t3_curve_u_to_t(t3_curve *c, float u) {
  int il;
  const float *al = t3_curve_get_lengths(c, &il);
  float target = u * al[il - 1];
  int low = 0, high = il - 1, i = 0;
  while (low <= high) {
    i = low + (high - low) / 2;
    float cmp = al[i] - target;
    if (cmp < 0) low = i + 1;
    else if (cmp > 0) high = i - 1;
    else { high = i; break; }
  }
  i = high;
  if (al[i] == target) return (float)i / (il - 1);
  float before = al[i], after = al[i + 1];
  return (i + (target - before) / (after - before)) / (il - 1);
}

t3_vec3 t3_curve_get_point_at(t3_curve *c, float u) { return t3_curve_get_point(c, t3_curve_u_to_t(c, u)); }
