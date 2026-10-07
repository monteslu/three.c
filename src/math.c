/* Vector3 / Quaternion / Euler / Matrix4 / Frustum, following three.js
 * line for line where the result is observable (compose, lookAt,
 * makePerspective, setFromEuler, setFromRotationMatrix), so values match the
 * oracle bit-for-bit as far as float vs double allows. */
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif
#include <math.h>
#include <string.h>

#include "three.h"

/* sin and cos of one float argument together, in double internally.
 * libm's sinf/cosf each redo the range reduction (musl's __rem_pio2f was 8%
 * of a frame in the browser profile); here it is one multiply-subtract,
 * exact enough in double for |x| < 2^20, then musl's __sindf / __cosdf
 * kernels. Results are the correctly rounded float in all but rare ties. */
static void sincos_f(float xf, float *sp, float *cp) {
  double x = xf;
  if (!(x > -1048576.0 && x < 1048576.0)) { *sp = sinf(xf); *cp = cosf(xf); return; }
  static const double invpio2 = 6.36619772367581382433e-01, pio2_1 = 1.57079631090164184570e+00,
                      pio2_1t = 1.58932547735281966916e-08;
  /* round-to-nearest-even: one f64.nearest / roundsd */
  double k = __builtin_rint(x * invpio2);
  double y = x - k * pio2_1 - k * pio2_1t;
  /* musl __sindf / __cosdf, |y| <= ~pi/4 */
  static const double S1 = -0x15555554cbac77.0p-55, S2 = 0x111110896efbb2.0p-59, S3 = -0x1a00f9e2cae774.0p-65,
                      S4 = 0x16cd878c3b46a7.0p-71;
  static const double C0 = -0x1ffffffd0c5e81.0p-54, C1 = 0x155553e1053a42.0p-57, C2 = -0x16c087e80f1e27.0p-62,
                      C3 = 0x199342e0ee5069.0p-68;
  double z = y * y, w = z * z;
  double r = S3 + z * S4, sn = z * y;
  double sv = (y + sn * (S1 + z * S2)) + sn * w * r;
  r = C2 + z * C3;
  double cv = ((1.0 + z * C0) + w * C1) + (w * z) * r;
  switch ((int)k & 3) {
  case 0: *sp = (float)sv; *cp = (float)cv; break;
  case 1: *sp = (float)cv; *cp = (float)-sv; break;
  case 2: *sp = (float)-sv; *cp = (float)-cv; break;
  default: *sp = (float)-cv; *cp = (float)sv; break;
  }
}

void t3_sincos(float x, float *s, float *c) { sincos_f(x, s, c); }

/* ColorManagement.enabled (true): colours set from hex are sRGB and stored in
 * the linear working space */
static bool color_management = true;
void t3_set_color_management(bool on) { color_management = on; }
bool t3_get_color_management(void) { return color_management; }
float t3_srgb_to_linear(float c) { return c < 0.04045f ? c * 0.0773993808f : (float)pow((double)c * 0.9478672986 + 0.0521327014, 2.4); }
float t3_linear_to_srgb(float c) { return c < 0.0031308f ? c * 12.92f : (float)(1.055 * pow((double)c, 0.41666) - 0.055); }
t3_color t3_color_hex(uint32_t hex) {
  t3_color c = { ((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f };
  if (color_management) { c.r = t3_srgb_to_linear(c.r); c.g = t3_srgb_to_linear(c.g); c.b = t3_srgb_to_linear(c.b); }
  return c;
}

/* Color.setHSL */
static float hue2rgb(float p, float q, float t) {
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  if (t < 1.0f / 6) return p + (q - p) * 6 * t;
  if (t < 1.0f / 2) return q;
  if (t < 2.0f / 3) return p + (q - p) * 6 * (2.0f / 3 - t);
  return p;
}
t3_color t3_color_hsl(float h, float s, float l) {
  h = h - floorf(h); /* euclideanModulo( h, 1 ) */
  s = s < 0 ? 0 : s > 1 ? 1 : s;
  l = l < 0 ? 0 : l > 1 ? 1 : l;
  if (s == 0) { t3_color c = { l, l, l }; return c; }
  float p = l <= 0.5f ? l * (1 + s) : l + s - l * s, q = 2 * l - p;
  t3_color c = { hue2rgb(q, p, h + 1.0f / 3), hue2rgb(q, p, h), hue2rgb(q, p, h - 1.0f / 3) };
  return c;
}

t3_vec3 t3_vec3_add(t3_vec3 a, t3_vec3 b) { return t3_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
t3_vec3 t3_vec3_sub(t3_vec3 a, t3_vec3 b) { return t3_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
t3_vec3 t3_vec3_scale(t3_vec3 a, float s) { return t3_v3(a.x * s, a.y * s, a.z * s); }
float t3_vec3_dot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
t3_vec3 t3_vec3_cross(t3_vec3 a, t3_vec3 b) {
  return t3_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
float t3_vec3_length(t3_vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
float t3_vec3_length_sq(t3_vec3 a) { return a.x * a.x + a.y * a.y + a.z * a.z; }
float t3_vec3_distance_sq(t3_vec3 a, t3_vec3 b) { return t3_vec3_length_sq(t3_vec3_sub(a, b)); }
float t3_vec3_distance(t3_vec3 a, t3_vec3 b) { return sqrtf(t3_vec3_distance_sq(a, b)); }
t3_vec3 t3_vec3_lerp(t3_vec3 a, t3_vec3 b, float t) {
  return t3_v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}
t3_vec3 t3_vec3_normalize(t3_vec3 a) {
  float l = t3_vec3_length(a);
  return t3_vec3_scale(a, 1.0f / (l ? l : 1.0f));
}

t3_vec3 t3_vec3_apply_mat4(t3_vec3 v, const t3_mat4 *m) {
  const float *e = m->e;
  float w = 1.0f / (e[3] * v.x + e[7] * v.y + e[11] * v.z + e[15]);
  return t3_v3((e[0] * v.x + e[4] * v.y + e[8] * v.z + e[12]) * w,
               (e[1] * v.x + e[5] * v.y + e[9] * v.z + e[13]) * w,
               (e[2] * v.x + e[6] * v.y + e[10] * v.z + e[14]) * w);
}

t3_vec3 t3_vec3_apply_quat(t3_vec3 v, t3_quat q) {
  float ix = q.w * v.x + q.y * v.z - q.z * v.y;
  float iy = q.w * v.y + q.z * v.x - q.x * v.z;
  float iz = q.w * v.z + q.x * v.y - q.y * v.x;
  float iw = -q.x * v.x - q.y * v.y - q.z * v.z;
  return t3_v3(ix * q.w + iw * -q.x + iy * -q.z - iz * -q.y,
               iy * q.w + iw * -q.y + iz * -q.x - ix * -q.z,
               iz * q.w + iw * -q.z + ix * -q.y - iy * -q.x);
}

t3_vec3 t3_vec3_transform_direction(t3_vec3 v, const t3_mat4 *m) {
  const float *e = m->e;
  return t3_vec3_normalize(t3_v3(e[0] * v.x + e[4] * v.y + e[8] * v.z,
                                 e[1] * v.x + e[5] * v.y + e[9] * v.z,
                                 e[2] * v.x + e[6] * v.y + e[10] * v.z));
}

t3_vec3 t3_mat4_get_position(const t3_mat4 *m) { return t3_v3(m->e[12], m->e[13], m->e[14]); }

/* ── quaternion / euler ──────────────────────────────────────────── */
t3_quat t3_quat_identity(void) { t3_quat q = { 0, 0, 0, 1 }; return q; }

t3_quat t3_quat_from_euler(t3_euler e) {
  float c1, c2, c3, s1, s2, s3;
  sincos_f(e.x / 2, &s1, &c1);
  sincos_f(e.y / 2, &s2, &c2);
  sincos_f(e.z / 2, &s3, &c3);
  t3_quat q;
  switch (e.order) {
  default:
  case T3_XYZ:
    q.x = s1 * c2 * c3 + c1 * s2 * s3; q.y = c1 * s2 * c3 - s1 * c2 * s3;
    q.z = c1 * c2 * s3 + s1 * s2 * c3; q.w = c1 * c2 * c3 - s1 * s2 * s3; break;
  case T3_YXZ:
    q.x = s1 * c2 * c3 + c1 * s2 * s3; q.y = c1 * s2 * c3 - s1 * c2 * s3;
    q.z = c1 * c2 * s3 - s1 * s2 * c3; q.w = c1 * c2 * c3 + s1 * s2 * s3; break;
  case T3_ZXY:
    q.x = s1 * c2 * c3 - c1 * s2 * s3; q.y = c1 * s2 * c3 + s1 * c2 * s3;
    q.z = c1 * c2 * s3 + s1 * s2 * c3; q.w = c1 * c2 * c3 - s1 * s2 * s3; break;
  case T3_ZYX:
    q.x = s1 * c2 * c3 - c1 * s2 * s3; q.y = c1 * s2 * c3 + s1 * c2 * s3;
    q.z = c1 * c2 * s3 - s1 * s2 * c3; q.w = c1 * c2 * c3 + s1 * s2 * s3; break;
  case T3_YZX:
    q.x = s1 * c2 * c3 + c1 * s2 * s3; q.y = c1 * s2 * c3 + s1 * c2 * s3;
    q.z = c1 * c2 * s3 - s1 * s2 * c3; q.w = c1 * c2 * c3 - s1 * s2 * s3; break;
  case T3_XZY:
    q.x = s1 * c2 * c3 - c1 * s2 * s3; q.y = c1 * s2 * c3 - s1 * c2 * s3;
    q.z = c1 * c2 * s3 + s1 * s2 * c3; q.w = c1 * c2 * c3 + s1 * s2 * s3; break;
  }
  return q;
}

t3_quat t3_quat_from_axis_angle(t3_vec3 a, float angle) {
  float s = sinf(angle / 2);
  t3_quat q = { a.x * s, a.y * s, a.z * s, cosf(angle / 2) };
  return q;
}

t3_quat t3_quat_from_rotation_matrix(const t3_mat4 *m) {
  const float *te = m->e;
  float m11 = te[0], m12 = te[4], m13 = te[8], m21 = te[1], m22 = te[5], m23 = te[9],
        m31 = te[2], m32 = te[6], m33 = te[10], trace = m11 + m22 + m33, s;
  t3_quat q;
  if (trace > 0) {
    s = 0.5f / sqrtf(trace + 1.0f);
    q.w = 0.25f / s; q.x = (m32 - m23) * s; q.y = (m13 - m31) * s; q.z = (m21 - m12) * s;
  } else if (m11 > m22 && m11 > m33) {
    s = 2.0f * sqrtf(1.0f + m11 - m22 - m33);
    q.w = (m32 - m23) / s; q.x = 0.25f * s; q.y = (m12 + m21) / s; q.z = (m13 + m31) / s;
  } else if (m22 > m33) {
    s = 2.0f * sqrtf(1.0f + m22 - m11 - m33);
    q.w = (m13 - m31) / s; q.x = (m12 + m21) / s; q.y = 0.25f * s; q.z = (m23 + m32) / s;
  } else {
    s = 2.0f * sqrtf(1.0f + m33 - m11 - m22);
    q.w = (m21 - m12) / s; q.x = (m13 + m31) / s; q.y = (m23 + m32) / s; q.z = 0.25f * s;
  }
  return q;
}

t3_quat t3_quat_multiply(t3_quat a, t3_quat b) {
  t3_quat q = { a.x * b.w + a.w * b.x + a.y * b.z - a.z * b.y,
                a.y * b.w + a.w * b.y + a.z * b.x - a.x * b.z,
                a.z * b.w + a.w * b.z + a.x * b.y - a.y * b.x,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
  return q;
}

/* Quaternion.slerp */
t3_quat t3_quat_slerp(t3_quat a, t3_quat b, float t) {
  if (t == 0) return a;
  if (t == 1) return b;
  float x = a.x, y = a.y, z = a.z, w = a.w;
  float cos_ht = w * b.w + x * b.x + y * b.y + z * b.z;
  t3_quat r;
  if (cos_ht < 0) { r.w = -b.w; r.x = -b.x; r.y = -b.y; r.z = -b.z; cos_ht = -cos_ht; }
  else r = b;
  if (cos_ht >= 1.0f) return a;
  float sqr_sin = 1.0f - cos_ht * cos_ht;
  if (sqr_sin <= 1.1920929e-7f) {
    float s = 1 - t;
    t3_quat l = { s * x + t * r.x, s * y + t * r.y, s * z + t * r.z, s * w + t * r.w };
    return t3_quat_normalize(l);
  }
  float sin_ht = sqrtf(sqr_sin), half = atan2f(sin_ht, cos_ht);
  float ra = sinf((1 - t) * half) / sin_ht, rb = sinf(t * half) / sin_ht;
  t3_quat o = { x * ra + r.x * rb, y * ra + r.y * rb, z * ra + r.z * rb, w * ra + r.w * rb };
  return o;
}

/* Quaternion.slerpFlat, the animation system's interpolant. */
void t3_quat_slerp_flat(float *dst, const float *src0, const float *src1, float t) {
  float x0 = src0[0], y0 = src0[1], z0 = src0[2], w0 = src0[3];
  const float x1 = src1[0], y1 = src1[1], z1 = src1[2], w1 = src1[3];
  if (t == 0) { dst[0] = x0; dst[1] = y0; dst[2] = z0; dst[3] = w0; return; }
  if (t == 1) { dst[0] = x1; dst[1] = y1; dst[2] = z1; dst[3] = w1; return; }
  if (w0 != w1 || x0 != x1 || y0 != y1 || z0 != z1) {
    float s = 1 - t;
    const float cs = x0 * x1 + y0 * y1 + z0 * z1 + w0 * w1, dir = cs >= 0 ? 1.0f : -1.0f, sqr_sin = 1 - cs * cs;
    if (sqr_sin > 2.220446049250313e-16f) {
      const float sn = sqrtf(sqr_sin), len = atan2f(sn, cs * dir);
      s = sinf(s * len) / sn;
      t = sinf(t * len) / sn;
    }
    const float tdir = t * dir;
    x0 = x0 * s + x1 * tdir; y0 = y0 * s + y1 * tdir; z0 = z0 * s + z1 * tdir; w0 = w0 * s + w1 * tdir;
    if (s == 1 - t) {
      const float f = 1 / sqrtf(x0 * x0 + y0 * y0 + z0 * z0 + w0 * w0);
      x0 *= f; y0 *= f; z0 *= f; w0 *= f;
    }
  }
  dst[0] = x0; dst[1] = y0; dst[2] = z0; dst[3] = w0;
}

t3_quat t3_quat_normalize(t3_quat q) {
  float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (l == 0) return t3_quat_identity();
  l = 1 / l;
  t3_quat r = { q.x * l, q.y * l, q.z * l, q.w * l };
  return r;
}

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

t3_euler t3_euler_from_quat(t3_quat q, t3_euler_order order) {
  t3_mat4 m;
  t3_mat4_compose(&m, t3_v3(0, 0, 0), q, t3_v3(1, 1, 1));
  const float *te = m.e;
  float m11 = te[0], m12 = te[4], m13 = te[8], m21 = te[1], m22 = te[5], m23 = te[9],
        m31 = te[2], m32 = te[6], m33 = te[10];
  t3_euler e = { 0, 0, 0, order };
  switch (order) {
  default:
  case T3_XYZ:
    e.y = asinf(clampf(m13, -1, 1));
    if (fabsf(m13) < 0.9999999f) { e.x = atan2f(-m23, m33); e.z = atan2f(-m12, m11); }
    else { e.x = atan2f(m32, m22); e.z = 0; }
    break;
  case T3_YXZ:
    e.x = asinf(-clampf(m23, -1, 1));
    if (fabsf(m23) < 0.9999999f) { e.y = atan2f(m13, m33); e.z = atan2f(m21, m22); }
    else { e.y = atan2f(-m31, m11); e.z = 0; }
    break;
  case T3_ZXY:
    e.x = asinf(clampf(m32, -1, 1));
    if (fabsf(m32) < 0.9999999f) { e.y = atan2f(-m31, m33); e.z = atan2f(-m12, m22); }
    else { e.y = 0; e.z = atan2f(m21, m11); }
    break;
  case T3_ZYX:
    e.y = asinf(-clampf(m31, -1, 1));
    if (fabsf(m31) < 0.9999999f) { e.x = atan2f(m32, m33); e.z = atan2f(m21, m11); }
    else { e.x = 0; e.z = atan2f(-m12, m22); }
    break;
  case T3_YZX:
    e.z = asinf(clampf(m21, -1, 1));
    if (fabsf(m21) < 0.9999999f) { e.x = atan2f(-m23, m22); e.y = atan2f(-m31, m11); }
    else { e.x = 0; e.y = atan2f(m13, m33); }
    break;
  case T3_XZY:
    e.z = asinf(-clampf(m12, -1, 1));
    if (fabsf(m12) < 0.9999999f) { e.x = atan2f(m32, m22); e.y = atan2f(m13, m11); }
    else { e.x = atan2f(-m23, m33); e.y = 0; }
    break;
  }
  return e;
}

/* ── matrix4 ─────────────────────────────────────────────────────── */
void t3_mat4_identity(t3_mat4 *m) {
  memset(m, 0, sizeof *m);
  m->e[0] = m->e[5] = m->e[10] = m->e[15] = 1;
}

/* Column c of the product is a's columns weighted by b's column c: four
 * lanes at once with GCC/Clang vector types (SSE, NEON or wasm simd128 when
 * the target has them, scalar code otherwise). Same operation order as the
 * scalar form, so the same bits. */
typedef float v4 __attribute__((vector_size(16)));
static inline v4 ld4(const float *p) { v4 v; memcpy(&v, p, sizeof v); return v; }
static inline void st4(float *p, v4 v) { memcpy(p, &v, sizeof v); }

#ifdef T3_F64_MATH
/* three.js's arithmetic: the products in double precision, rounded to float
 * once when stored (Matrix4.elements is a Float32Array). Same operation
 * order as three.js's multiplyMatrices, so the same bits. Used where three.c
 * runs behind a JavaScript-exact API (three.lua). */
void t3_mat4_multiply(t3_mat4 *out, const t3_mat4 *a, const t3_mat4 *b) {
  const float *ae = a->e, *be = b->e;
  float r[16];
  for (int c = 0; c < 4; c++)
    for (int i = 0; i < 4; i++)
      r[c * 4 + i] = (float)((double)ae[i] * be[c * 4] + (double)ae[4 + i] * be[c * 4 + 1] +
                             (double)ae[8 + i] * be[c * 4 + 2] + (double)ae[12 + i] * be[c * 4 + 3]);
  memcpy(out->e, r, sizeof r);
}
#else
void t3_mat4_multiply(t3_mat4 *out, const t3_mat4 *a, const t3_mat4 *b) {
  const float *be = b->e;
  v4 a0 = ld4(a->e), a1 = ld4(a->e + 4), a2 = ld4(a->e + 8), a3 = ld4(a->e + 12);
  v4 r[4];
  for (int c = 0; c < 4; c++)
    r[c] = a0 * be[c * 4] + a1 * be[c * 4 + 1] + a2 * be[c * 4 + 2] + a3 * be[c * 4 + 3];
  for (int c = 0; c < 4; c++) st4(out->e + c * 4, r[c]);
}
#endif

/* Matrix4.compose in double precision from double inputs (three.js's
 * numbers), rounded once into the float elements. */
void t3_mat4_compose_d(t3_mat4 *m, const double p[3], const double q[4], const double s[3]) {
  float *te = m->e;
  double x = q[0], y = q[1], z = q[2], w = q[3];
  double x2 = x + x, y2 = y + y, z2 = z + z;
  double xx = x * x2, xy = x * y2, xz = x * z2, yy = y * y2, yz = y * z2, zz = z * z2;
  double wx = w * x2, wy = w * y2, wz = w * z2;
  te[0] = (float)((1 - (yy + zz)) * s[0]); te[1] = (float)((xy + wz) * s[0]); te[2] = (float)((xz - wy) * s[0]); te[3] = 0;
  te[4] = (float)((xy - wz) * s[1]); te[5] = (float)((1 - (xx + zz)) * s[1]); te[6] = (float)((yz + wx) * s[1]); te[7] = 0;
  te[8] = (float)((xz + wy) * s[2]); te[9] = (float)((yz - wx) * s[2]); te[10] = (float)((1 - (xx + yy)) * s[2]); te[11] = 0;
  te[12] = (float)p[0]; te[13] = (float)p[1]; te[14] = (float)p[2]; te[15] = 1;
}

void t3_mat4d_compose(double te[16], const double p[3], const double q[4], const double s[3]) {
  double x = q[0], y = q[1], z = q[2], w = q[3];
  double x2 = x + x, y2 = y + y, z2 = z + z;
  double xx = x * x2, xy = x * y2, xz = x * z2, yy = y * y2, yz = y * z2, zz = z * z2;
  double wx = w * x2, wy = w * y2, wz = w * z2;
  te[0] = (1 - (yy + zz)) * s[0]; te[1] = (xy + wz) * s[0]; te[2] = (xz - wy) * s[0]; te[3] = 0;
  te[4] = (xy - wz) * s[1]; te[5] = (1 - (xx + zz)) * s[1]; te[6] = (yz + wx) * s[1]; te[7] = 0;
  te[8] = (xz + wy) * s[2]; te[9] = (yz - wx) * s[2]; te[10] = (1 - (xx + yy)) * s[2]; te[11] = 0;
  te[12] = p[0]; te[13] = p[1]; te[14] = p[2]; te[15] = 1;
}

/* three.js multiplyMatrices, the same operation order */
void t3_mat4d_multiply(double out[16], const double ae[16], const double be[16]) {
  double r[16];
#ifdef __wasm_simd128__
  /* two rows per f64x2: the same multiplies and adds in the same order, lane
   * by lane (no fused multiply-add), so the doubles are identical */
  v128_t a0 = wasm_v128_load(ae), a1 = wasm_v128_load(ae + 2), a2 = wasm_v128_load(ae + 4), a3 = wasm_v128_load(ae + 6);
  v128_t a4 = wasm_v128_load(ae + 8), a5 = wasm_v128_load(ae + 10), a6 = wasm_v128_load(ae + 12), a7 = wasm_v128_load(ae + 14);
  for (int c = 0; c < 4; c++) {
    v128_t b0 = wasm_f64x2_splat(be[c * 4]), b1 = wasm_f64x2_splat(be[c * 4 + 1]);
    v128_t b2 = wasm_f64x2_splat(be[c * 4 + 2]), b3 = wasm_f64x2_splat(be[c * 4 + 3]);
    v128_t lo = wasm_f64x2_add(wasm_f64x2_add(wasm_f64x2_add(wasm_f64x2_mul(a0, b0), wasm_f64x2_mul(a2, b1)),
                                              wasm_f64x2_mul(a4, b2)), wasm_f64x2_mul(a6, b3));
    v128_t hi = wasm_f64x2_add(wasm_f64x2_add(wasm_f64x2_add(wasm_f64x2_mul(a1, b0), wasm_f64x2_mul(a3, b1)),
                                              wasm_f64x2_mul(a5, b2)), wasm_f64x2_mul(a7, b3));
    wasm_v128_store(r + c * 4, lo);
    wasm_v128_store(r + c * 4 + 2, hi);
  }
#else
  for (int c = 0; c < 4; c++)
    for (int i = 0; i < 4; i++)
      r[c * 4 + i] = ae[i] * be[c * 4] + ae[4 + i] * be[c * 4 + 1] + ae[8 + i] * be[c * 4 + 2] + ae[12 + i] * be[c * 4 + 3];
#endif
  memcpy(out, r, sizeof r);
}

/* three.js Matrix4.invert, the same expressions */
void t3_mat4d_invert(double out[16], const double te[16]) {
  double n11 = te[0], n21 = te[1], n31 = te[2], n41 = te[3];
  double n12 = te[4], n22 = te[5], n32 = te[6], n42 = te[7];
  double n13 = te[8], n23 = te[9], n33 = te[10], n43 = te[11];
  double n14 = te[12], n24 = te[13], n34 = te[14], n44 = te[15];
  double t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
  double t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
  double t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
  double t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;
  double det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
  double r[16];
  if (det == 0) { memset(out, 0, sizeof r); return; }
  double di = 1 / det;
  r[0] = t11 * di;
  r[1] = (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * di;
  r[2] = (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * di;
  r[3] = (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * di;
  r[4] = t12 * di;
  r[5] = (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * di;
  r[6] = (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * di;
  r[7] = (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * di;
  r[8] = t13 * di;
  r[9] = (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * di;
  r[10] = (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * di;
  r[11] = (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * di;
  r[12] = t14 * di;
  r[13] = (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * di;
  r[14] = (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * di;
  r[15] = (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * di;
  memcpy(out, r, sizeof r);
}

void t3_mat4_compose(t3_mat4 *m, t3_vec3 p, t3_quat q, t3_vec3 s) {
  float *te = m->e;
  float x = q.x, y = q.y, z = q.z, w = q.w;
  float x2 = x + x, y2 = y + y, z2 = z + z;
  float xx = x * x2, xy = x * y2, xz = x * z2, yy = y * y2, yz = y * z2, zz = z * z2;
  float wx = w * x2, wy = w * y2, wz = w * z2;
  te[0] = (1 - (yy + zz)) * s.x; te[1] = (xy + wz) * s.x; te[2] = (xz - wy) * s.x; te[3] = 0;
  te[4] = (xy - wz) * s.y; te[5] = (1 - (xx + zz)) * s.y; te[6] = (yz + wx) * s.y; te[7] = 0;
  te[8] = (xz + wy) * s.z; te[9] = (yz - wx) * s.z; te[10] = (1 - (xx + yy)) * s.z; te[11] = 0;
  te[12] = p.x; te[13] = p.y; te[14] = p.z; te[15] = 1;
}

static float det4(const float *te) {
  /* Matrix4.determinant */
  float n11 = te[0], n12 = te[4], n13 = te[8], n14 = te[12];
  float n21 = te[1], n22 = te[5], n23 = te[9], n24 = te[13];
  float n31 = te[2], n32 = te[6], n33 = te[10], n34 = te[14];
  float n41 = te[3], n42 = te[7], n43 = te[11], n44 = te[15];
  return n41 * (+n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34) +
         n42 * (+n11 * n23 * n34 - n11 * n24 * n33 + n14 * n21 * n33 - n13 * n21 * n34 + n13 * n24 * n31 - n14 * n23 * n31) +
         n43 * (+n11 * n24 * n32 - n11 * n22 * n34 - n14 * n21 * n32 + n12 * n21 * n34 + n14 * n22 * n31 - n12 * n24 * n31) +
         n44 * (-n13 * n22 * n31 - n11 * n23 * n32 + n11 * n22 * n33 + n13 * n21 * n32 - n12 * n21 * n33 + n12 * n23 * n31);
}

void t3_mat4_decompose(const t3_mat4 *m, t3_vec3 *p, t3_quat *q, t3_vec3 *s) {
  const float *te = m->e;
  float sx = t3_vec3_length(t3_v3(te[0], te[1], te[2]));
  float sy = t3_vec3_length(t3_v3(te[4], te[5], te[6]));
  float sz = t3_vec3_length(t3_v3(te[8], te[9], te[10]));
  if (det4(te) < 0) sx = -sx;
  if (p) *p = t3_v3(te[12], te[13], te[14]);
  if (q) {
    t3_mat4 r = *m;
    float isx = 1 / sx, isy = 1 / sy, isz = 1 / sz;
    r.e[0] *= isx; r.e[1] *= isx; r.e[2] *= isx;
    r.e[4] *= isy; r.e[5] *= isy; r.e[6] *= isy;
    r.e[8] *= isz; r.e[9] *= isz; r.e[10] *= isz;
    *q = t3_quat_from_rotation_matrix(&r);
  }
  if (s) *s = t3_v3(sx, sy, sz);
}

/* (in double, as three.js's Matrix4.invert, rounded once at the end) */
void t3_mat4_invert(t3_mat4 *out, const t3_mat4 *m) {
  const float *te = m->e;
  double n11 = te[0], n21 = te[1], n31 = te[2], n41 = te[3];
  double n12 = te[4], n22 = te[5], n32 = te[6], n42 = te[7];
  double n13 = te[8], n23 = te[9], n33 = te[10], n43 = te[11];
  double n14 = te[12], n24 = te[13], n34 = te[14], n44 = te[15];
  double t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
  double t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
  double t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
  double t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;
  double det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
  double r[16];
  if (det == 0) { memset(out, 0, sizeof *out); return; }
  double di = 1 / det;
  r[0] = t11 * di;
  r[1] = (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * di;
  r[2] = (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * di;
  r[3] = (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * di;
  r[4] = t12 * di;
  r[5] = (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * di;
  r[6] = (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * di;
  r[7] = (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * di;
  r[8] = t13 * di;
  r[9] = (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * di;
  r[10] = (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * di;
  r[11] = (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * di;
  r[12] = t14 * di;
  r[13] = (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * di;
  r[14] = (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * di;
  r[15] = (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * di;
  for (int k = 0; k < 16; k++) out->e[k] = (float)r[k];
}

/* three.js Matrix4.lookAt: rotation only; translation untouched. */
void t3_mat4_look_at(t3_mat4 *m, t3_vec3 eye, t3_vec3 target, t3_vec3 up) {
  float *te = m->e;
  t3_vec3 z = t3_vec3_sub(eye, target);
  if (t3_vec3_dot(z, z) == 0) z.z = 1;
  z = t3_vec3_normalize(z);
  t3_vec3 x = t3_vec3_cross(up, z);
  if (t3_vec3_dot(x, x) == 0) {
    if (fabsf(up.z) == 1) z.x += 0.0001f; else z.z += 0.0001f;
    z = t3_vec3_normalize(z);
    x = t3_vec3_cross(up, z);
  }
  x = t3_vec3_normalize(x);
  t3_vec3 y = t3_vec3_cross(z, x);
  te[0] = x.x; te[4] = y.x; te[8] = z.x;
  te[1] = x.y; te[5] = y.y; te[9] = z.y;
  te[2] = x.z; te[6] = y.z; te[10] = z.z;
}

void t3_mat4_make_perspective(t3_mat4 *m, float left, float right, float top, float bottom, float near, float far) {
  float *te = m->e;
  double x = 2.0 * near / ((double)right - left), y = 2.0 * near / ((double)top - bottom);
  double a = ((double)right + left) / ((double)right - left), b = ((double)top + bottom) / ((double)top - bottom);
  double c = -((double)far + near) / ((double)far - near), d = -2.0 * far * near / ((double)far - near);
  te[0] = (float)x; te[4] = 0; te[8] = (float)a; te[12] = 0;
  te[1] = 0; te[5] = (float)y; te[9] = (float)b; te[13] = 0;
  te[2] = 0; te[6] = 0; te[10] = (float)c; te[14] = (float)d;
  te[3] = 0; te[7] = 0; te[11] = -1; te[15] = 0;
}

void t3_mat4_make_orthographic(t3_mat4 *m, float left, float right, float top, float bottom, float near, float far) {
  float *te = m->e;
  float w = 1.0f / (right - left), h = 1.0f / (top - bottom), p = 1.0f / (far - near);
  float x = (right + left) * w, y = (top + bottom) * h, z = (far + near) * p;
  te[0] = 2 * w; te[4] = 0; te[8] = 0; te[12] = -x;
  te[1] = 0; te[5] = 2 * h; te[9] = 0; te[13] = -y;
  te[2] = 0; te[6] = 0; te[10] = -2 * p; te[14] = -z;
  te[3] = 0; te[7] = 0; te[11] = 0; te[15] = 1;
}

float t3_mat4_max_scale_on_axis(const t3_mat4 *m) {
  const float *te = m->e;
  float a = te[0] * te[0] + te[1] * te[1] + te[2] * te[2];
  float b = te[4] * te[4] + te[5] * te[5] + te[6] * te[6];
  float c = te[8] * te[8] + te[9] * te[9] + te[10] * te[10];
  float mx = a > b ? a : b;
  return sqrtf(mx > c ? mx : c);
}

/* Matrix3.getNormalMatrix: inverse transpose of the upper 3x3. */
void t3_mat3_normal_matrix(t3_mat3 *out, const t3_mat4 *m) {
  const float *e = m->e;
  float n11 = e[0], n21 = e[1], n31 = e[2], n12 = e[4], n22 = e[5], n32 = e[6], n13 = e[8], n23 = e[9], n33 = e[10];
  float t11 = n33 * n22 - n32 * n23, t12 = n32 * n13 - n33 * n12, t13 = n23 * n12 - n22 * n13;
  float det = n11 * t11 + n21 * t12 + n31 * t13;
  float *te = out->e;
  if (det == 0) { memset(out, 0, sizeof *out); return; }
  float di = 1 / det;
  /* invert into te, then transpose */
  float i0 = t11 * di, i1 = (n31 * n23 - n33 * n21) * di, i2 = (n32 * n21 - n31 * n22) * di;
  float i3 = t12 * di, i4 = (n33 * n11 - n31 * n13) * di, i5 = (n31 * n12 - n32 * n11) * di;
  float i6 = t13 * di, i7 = (n21 * n13 - n23 * n11) * di, i8 = (n22 * n11 - n21 * n12) * di;
  te[0] = i0; te[1] = i3; te[2] = i6;
  te[3] = i1; te[4] = i4; te[5] = i7;
  te[6] = i2; te[7] = i5; te[8] = i8;
}

/* ── frustum ─────────────────────────────────────────────────────── */
static t3_plane plane_norm(float a, float b, float c, float d) {
  float l = sqrtf(a * a + b * b + c * c);
  float il = l ? 1 / l : 1;
  t3_plane p = { { a * il, b * il, c * il }, d * il };
  return p;
}

void t3_frustum_from_matrix(t3_frustum *f, const t3_mat4 *m) {
  const float *me = m->e;
  float me0 = me[0], me1 = me[1], me2 = me[2], me3 = me[3];
  float me4 = me[4], me5 = me[5], me6 = me[6], me7 = me[7];
  float me8 = me[8], me9 = me[9], me10 = me[10], me11 = me[11];
  float me12 = me[12], me13 = me[13], me14 = me[14], me15 = me[15];
  f->planes[0] = plane_norm(me3 - me0, me7 - me4, me11 - me8, me15 - me12);
  f->planes[1] = plane_norm(me3 + me0, me7 + me4, me11 + me8, me15 + me12);
  f->planes[2] = plane_norm(me3 + me1, me7 + me5, me11 + me9, me15 + me13);
  f->planes[3] = plane_norm(me3 - me1, me7 - me5, me11 - me9, me15 - me13);
  f->planes[4] = plane_norm(me3 - me2, me7 - me6, me11 - me10, me15 - me14);
  f->planes[5] = plane_norm(me3 + me2, me7 + me6, me11 + me10, me15 + me14);
}

bool t3_frustum_intersects_sphere(const t3_frustum *f, t3_sphere s) {
  float neg = -s.radius;
  for (int i = 0; i < 6; i++)
    if (t3_vec3_dot(f->planes[i].normal, s.center) + f->planes[i].constant < neg) return false;
  return true;
}
