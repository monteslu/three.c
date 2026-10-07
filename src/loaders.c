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
