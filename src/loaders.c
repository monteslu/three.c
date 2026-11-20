/* BufferGeometryLoader.parse for the JSON BufferGeometry format:
 *
 *   { "data": { "attributes": { "<name>": { "itemSize": n, "type": "...",
 *               "array": [ ... ], "normalized": bool }, ... },
 *               "index": { "type": "...", "array": [ ... ] } } }
 *
 * A small purpose-built JSON reader: it walks the document once, keeps the
 * attributes three.c has slots for (position, normal, uv, color, tangent,
 * uv2) and the index, and skips everything else. Loading the bytes is the
 * host's job; this only parses. */
#include <stdlib.h>
#include <string.h>

#include "internal.h"

typedef struct { const char *p, *end; bool err; } js;

static void ws(js *j) {
  while (j->p < j->end && (*j->p == ' ' || *j->p == '\n' || *j->p == '\r' || *j->p == '\t')) j->p++;
}
static bool lit(js *j, char c) {
  ws(j);
  if (j->p < j->end && *j->p == c) { j->p++; return true; }
  return false;
}
/* a string's contents into buf (truncated); escapes copied as-is */
static bool str(js *j, char *buf, size_t cap) {
  if (!lit(j, '"')) { j->err = true; return false; }
  size_t n = 0;
  while (j->p < j->end && *j->p != '"') {
    if (*j->p == '\\' && j->p + 1 < j->end) j->p++;
    if (n + 1 < cap) buf[n++] = *j->p;
    j->p++;
  }
  if (cap) buf[n] = 0;
  if (j->p >= j->end) { j->err = true; return false; }
  j->p++;
  return true;
}
static double num(js *j) {
  ws(j);
  char *e;
  double v = strtod(j->p, &e);
  if (e == j->p) j->err = true;
  j->p = e;
  return v;
}
static void skip(js *j);
static void skip_container(js *j, char open, char close) {
  lit(j, open);
  int depth = 1;
  bool in_str = false;
  while (j->p < j->end && depth) {
    char c = *j->p++;
    if (in_str) {
      if (c == '\\') j->p++;
      else if (c == '"') in_str = false;
    } else if (c == '"') in_str = true;
    else if (c == open) depth++;
    else if (c == close) depth--;
  }
}
static void skip(js *j) {
  ws(j);
  if (j->p >= j->end) { j->err = true; return; }
  char c = *j->p;
  if (c == '{') skip_container(j, '{', '}');
  else if (c == '[') skip_container(j, '[', ']');
  else if (c == '"') { char t[1]; str(j, t, 1); }
  else while (j->p < j->end && *j->p != ',' && *j->p != '}' && *j->p != ']') j->p++;
}

/* a numeric array into a growable float/uint buffer */
static double *num_array(js *j, int *count) {
  int n = 0, cap = 1024;
  double *v = malloc((size_t)cap * sizeof *v);
  T3_CHECK_ALLOC(v);
  if (!lit(j, '[')) { j->err = true; *count = 0; return v; }
  if (lit(j, ']')) { *count = 0; return v; }
  do {
    if (n == cap) {
      cap *= 2;
      v = realloc(v, (size_t)cap * sizeof *v);
      T3_CHECK_ALLOC(v);
    }
    v[n++] = num(j);
  } while (!j->err && lit(j, ','));
  if (!lit(j, ']')) j->err = true;
  *count = n;
  return v;
}

/* { "itemSize", "type", "array", "normalized" } */
static t3_attribute *read_attribute(js *j, bool is_index) {
  int item_size = 1, n = 0;
  bool normalized = false, u32 = false;
  double *arr = NULL;
  char key[32], type[32] = "Float32Array";
  if (!lit(j, '{')) { j->err = true; return NULL; }
  if (!lit(j, '}')) do {
    if (!str(j, key, sizeof key) || !lit(j, ':')) break;
    if (!strcmp(key, "itemSize")) item_size = (int)num(j);
    else if (!strcmp(key, "type")) str(j, type, sizeof type);
    else if (!strcmp(key, "normalized")) { ws(j); normalized = j->p < j->end && *j->p == 't'; skip(j); }
    else if (!strcmp(key, "array")) { free(arr); arr = num_array(j, &n); }
    else skip(j);
  } while (!j->err && lit(j, ','));
  if (!j->err && !lit(j, '}')) j->err = true;
  if (j->err || !arr) { free(arr); return NULL; }
  t3_attribute *a;
  if (is_index) {
    u32 = !strcmp(type, "Uint32Array");
    a = t3_attribute_new(u32 ? T3_UINT32 : T3_UINT16, NULL, n, 1);
    for (int i = 0; i < n; i++) {
      if (u32) ((uint32_t *)a->array)[i] = (uint32_t)arr[i];
      else ((uint16_t *)a->array)[i] = (uint16_t)arr[i];
    }
  } else {
    a = t3_attribute_new(T3_FLOAT32, NULL, n / item_size, item_size);
    for (int i = 0; i < n; i++) ((float *)a->array)[i] = (float)arr[i];
    a->normalized = normalized;
  }
  free(arr);
  return a;
}

static int slot_of(const char *name) {
  static const char *const names[] = { "position", "normal", "uv", "color", "tangent", "uv2" };
  for (int i = 0; i < 6; i++)
    if (!strcmp(name, names[i])) return i;
  return -1;
}

t3_geometry *t3_buffer_geometry_loader_parse(const char *text, size_t len) {
  js j = { text, text + len, false };
  t3_geometry *g = t3_geometry_new();
  char key[64];
  if (!lit(&j, '{')) goto fail;
  if (!lit(&j, '}')) do {
    if (!str(&j, key, sizeof key) || !lit(&j, ':')) goto fail;
    if (strcmp(key, "data")) { skip(&j); continue; }
    if (!lit(&j, '{')) goto fail;
    if (!lit(&j, '}')) do {
      if (!str(&j, key, sizeof key) || !lit(&j, ':')) goto fail;
      if (!strcmp(key, "attributes")) {
        if (!lit(&j, '{')) goto fail;
        if (!lit(&j, '}')) do {
          char name[64];
          if (!str(&j, name, sizeof name) || !lit(&j, ':')) goto fail;
          int slot = slot_of(name);
          if (slot < 0) { skip(&j); continue; }
          t3_attribute *a = read_attribute(&j, false);
          if (!a) goto fail;
          t3_geometry_set_attribute(g, (t3_attr_slot)slot, a);
          t3_release(a);
        } while (lit(&j, ','));
        if (!lit(&j, '}')) goto fail;
      } else if (!strcmp(key, "index")) {
        t3_attribute *a = read_attribute(&j, true);
        if (!a) goto fail;
        t3_geometry_set_index(g, a);
        t3_release(a);
      } else {
        skip(&j);
      }
    } while (!j.err && lit(&j, ','));
    if (!lit(&j, '}')) goto fail;
  } while (!j.err && lit(&j, ','));
  if (j.err || !g->attributes[T3_ATTR_POSITION]) goto fail;
  return g;
fail:
  t3_release(g);
  return NULL;
}

/* ── IESLoader (three.js examples/jsm/loaders/IESLoader.js): an IES photometric
 * file (IESNA LM-63) as the texture an IESSpotLight takes. Follows the loader
 * step for step: every number after the TILT line is read in file order
 * (commas and any white space separate them), the candela values are scaled
 * by multiplier x ballast factor x ballast-lamp factor and normalized to the
 * brightest one, the horizontal angles are shifted to start at 0 (a range
 * that ends between 180 and 360 gets the 360 entry some files leave out) and
 * the texture is 180 texels of vertical angle by 1 row (one horizontal angle)
 * or 360 rows (one a degree, the measured range mirrored around the cycle). */
#include <math.h>

typedef struct { const char *p, *end; bool err; } ies_in;
static double ies_num(ies_in *in) {
  while (in->p < in->end && (*in->p == ' ' || *in->p == '\t' || *in->p == '\r' || *in->p == '\n' || *in->p == ',')) in->p++;
  if (in->p >= in->end) { in->err = true; return 0; }
  char buf[48];
  size_t n = 0;
  while (in->p < in->end && n < sizeof buf - 1 && !(*in->p == ' ' || *in->p == '\t' || *in->p == '\r' || *in->p == '\n' || *in->p == ',')) buf[n++] = *in->p++;
  buf[n] = 0;
  char *e;
  double v = strtod(buf, &e);
  if (e == buf) in->err = true;
  return v;
}
static uint16_t ies_half(double d) {
  /* DataUtils.toHalfFloat: clamped to the half range, the mantissa truncated (its table lookup) */
  float f = (float)(d > 65504 ? 65504 : d < -65504 ? -65504 : d);
  uint32_t x;
  memcpy(&x, &f, 4);
  uint32_t sign = (x >> 16) & 0x8000u, mant = x & 0x7fffffu;
  int e = (int)((x >> 23) & 0xff) - 127;
  if (e < -24) return (uint16_t)sign;
  if (e < -14) return (uint16_t)(sign | ((mant | 0x800000u) >> (-e - 1)));
  if (e <= 15) return (uint16_t)(sign | ((uint32_t)(e + 15) << 10) | (mant >> 13));
  return (uint16_t)(sign | 0x7c00u);
}

t3_texture *t3_ies_texture_parse(const char *text, size_t len) {
  if (!text) return NULL;
  /* the line with TILT: NONE, INCLUDE (tilt data follows) or a file name (not read, as three.js) */
  const char *end = text + len, *line = text, *tilt = NULL, *eol = NULL;
  while (line < end && !tilt) {
    eol = memchr(line, '\n', (size_t)(end - line));
    if (!eol) eol = end;
    for (const char *q = line; q + 4 <= eol; q++) if (!memcmp(q, "TILT", 4)) { tilt = q; break; }
    if (!tilt) line = eol < end ? eol + 1 : end;
  }
  if (!tilt) return NULL;
  bool none = false, include = false;
  for (const char *q = tilt; q + 4 <= eol; q++) if (!memcmp(q, "NONE", 4)) none = true;
  for (const char *q = tilt; q + 7 <= eol; q++) if (!memcmp(q, "INCLUDE", 7)) include = true;
  ies_in in = { eol < end ? eol + 1 : end, end, false };
  if (!none && include) {
    ies_num(&in);                         /* lamp to luminaire geometry */
    int n = (int)ies_num(&in);
    if (n < 0 || n > 4096) return NULL;
    for (int i = 0; i < 2 * n; i++) ies_num(&in);   /* angles, multiplying factors: unused by the loader */
  }
  double v[13];
  for (int i = 0; i < 13; i++) v[i] = ies_num(&in);   /* 10 lamp values, 3 factors */
  int nv = (int)v[3], nh = (int)v[4];
  if (in.err || nv < 2 || nh < 1 || nv > 4096 || nh > 4096) return NULL;
  double mul = v[2] * v[10] * v[11];
  double *ver = malloc(sizeof *ver * (size_t)nv), *hor = malloc(sizeof *hor * (size_t)(nh + 1));
  double *cd = malloc(sizeof *cd * (size_t)(nh + 1) * (size_t)nv);
  T3_CHECK_ALLOC(ver); T3_CHECK_ALLOC(hor); T3_CHECK_ALLOC(cd);
  for (int i = 0; i < nv; i++) ver[i] = ies_num(&in);
  for (int i = 0; i < nh; i++) hor[i] = ies_num(&in);
  double max = -1, base;
  for (int i = 0; i < nh * nv; i++) { cd[i] = ies_num(&in) * mul; if (cd[i] > max) max = cd[i]; }
  if (in.err) { free(ver); free(hor); free(cd); return NULL; }
  base = hor[0];
  for (int i = 0; i < nh; i++) hor[i] -= base;
  if (hor[nh - 1] > 180 && hor[nh - 1] < 360) {
    hor[nh] = 360;
    memcpy(cd + (size_t)nh * nv, cd, sizeof *cd * (size_t)nv);
    nh++;
  }
  if (max > 0) for (int i = 0; i < nh * nv; i++) cd[i] /= max;
  int width = nh == 1 ? 1 : 360, height = 180;   /* (the loader's names: its texture is `height` wide and `width` rows) */
  uint16_t *px = malloc(sizeof *px * 4 * (size_t)width * height);
  T3_CHECK_ALLOC(px);
  double end_theta = hor[nh - 1];
  for (int i = 0; i < width * height; i++) {
    int theta_i = i % width, phi_i = i / width;
    double theta = theta_i;
    if (end_theta != 0 && theta > end_theta) {
      theta = fmod(theta, end_theta * 2);
      if (theta > end_theta) theta = end_theta * 2 - theta;
    }
    double phi = phi_i * 180.0 / (height - 1);
    /* interpolateCandelaValues( phi, theta ) */
    int ti = 0, pi = 0;
    double st = 0, et = 0, sp = 0, ep = 0;
    for (int k = 0; k < nh - 1; k++) if (theta < hor[k + 1] || k == nh - 2) { ti = k; st = hor[k]; et = hor[k + 1]; break; }
    for (int k = 0; k < nv - 1; k++) if (phi < ver[k + 1] || k == nv - 2) { pi = k; sp = ver[k]; ep = ver[k + 1]; break; }
    double dt = et - st, dp = ep - sp, val = 0;
    if (dp != 0) {
      double t1 = dt == 0 ? 0 : (theta - st) / dt, t2 = (phi - sp) / dp;
      int nt = dt == 0 ? ti : ti + 1;
      double a0 = cd[(size_t)ti * nv + pi], a1 = cd[(size_t)nt * nv + pi], b0 = cd[(size_t)ti * nv + pi + 1], b1 = cd[(size_t)nt * nv + pi + 1];
      double v1 = a0 + (a1 - a0) * t1, v2 = b0 + (b1 - b0) * t1;
      val = v1 + (v2 - v1) * t2;
    }
    uint16_t *o = px + 4 * ((size_t)theta_i * height + phi_i);   /* texel (phi, theta) */
    o[0] = o[1] = o[2] = ies_half(val);
    o[3] = 0x3c00;
  }
  /* (three.js makes it RedFormat; three.c's textures are RGBA, the program reads red) */
  t3_texture *t = t3_data_texture_new_typed(height, width, px, T3_HALF_FLOAT_TYPE);
  t->min_filter = t->mag_filter = T3_LINEAR;
  t->wrap_t = T3_REPEAT;
  free(px); free(ver); free(hor); free(cd);
  return t;
}
