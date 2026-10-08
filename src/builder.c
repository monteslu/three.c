/* The runtime program builder (T3_BUILDER=1): a state no table has is
 * captured from three.js r186 itself, on first use, by running the same
 * capture tools/gen-programs.mjs does (tools/builder-entry.mjs) in an embedded
 * QuickJS, on the mock GPU (tools/mock-gpu.mjs with tools/gpu-answers.json).
 * The result is the program the emitter would have written into a table
 * (tools/program-model.mjs), built on the heap and kept for the process.
 *
 * Each capture gets a fresh JS runtime: three.js numbers its nodes per
 * process, and the numbers end up in the program text. A capture whose
 * uniforms the primary world cannot explain runs again in the probe world
 * (another fresh runtime), exactly as the offline tool does. */
#include "gen_program.h"
#include "quickjs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the embedded JS (build/builder/builder_js.c, tools/embed-builder.mjs):
 * each file NUL-terminated (QuickJS's parser needs it), len without the NUL */
typedef struct { const char *name; const unsigned char *data; size_t len; } t3_builder_file;
extern const t3_builder_file t3_builder_files[];
extern const unsigned t3_builder_n_files;

static const t3_builder_file *bfile(const char *name) {
  for (unsigned i = 0; i < t3_builder_n_files; i++) if (!strcmp(t3_builder_files[i].name, name)) return &t3_builder_files[i];
  return NULL;
}
/* every module is one of the embedded files, by its base name */
static char *bnormalize(JSContext *ctx, const char *base, const char *name, void *op) {
  (void)base; (void)op;
  const char *s = strrchr(name, '/');
  return js_strdup(ctx, s ? s + 1 : name);
}
static JSModuleDef *bload(JSContext *ctx, const char *name, void *op) {
  (void)op;
  const t3_builder_file *f = bfile(name);
  if (!f) { JS_ThrowReferenceError(ctx, "the builder has no module %s", name); return NULL; }
  JSValue fn = JS_Eval(ctx, (const char *)f->data, f->len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(fn)) return NULL;
  JSModuleDef *m = JS_VALUE_GET_PTR(fn);
  JS_FreeValue(ctx, fn);
  return m;
}

typedef struct { JSRuntime *rt; JSContext *ctx; } bsession;

static void berr(JSContext *ctx, char *err, size_t cap, const char *what) {
  JSValue e = JS_GetException(ctx);
  const char *s = JS_ToCString(ctx, e);
  JSValue st = JS_IsError(e) ? JS_GetPropertyStr(ctx, e, "stack") : JS_UNDEFINED;
  const char *t = JS_IsString(st) ? JS_ToCString(ctx, st) : NULL;
  snprintf(err, cap, "%s: %s%s%s", what, s ? s : "?", t ? "\n" : "", t ? t : "");
  if (t) JS_FreeCString(ctx, t);
  if (s) JS_FreeCString(ctx, s);
  JS_FreeValue(ctx, st);
  JS_FreeValue(ctx, e);
}

/* run the job queue, then three.js's timers, until the promise settles;
 * consumes p, returns its value (JS_EXCEPTION when it rejected) */
static JSValue bawait(bsession *s, JSValue p) {
  JSContext *c;
  JSValue gl = JS_GetGlobalObject(s->ctx);
  JSValue tick = JS_GetPropertyStr(s->ctx, gl, "__t3_tick");
  JS_FreeValue(s->ctx, gl);
  for (;;) {
    int r;
    while ((r = JS_ExecutePendingJob(s->rt, &c)) > 0) {}
    if (r < 0) { JS_FreeValue(s->ctx, tick); JS_FreeValue(s->ctx, p); return JS_EXCEPTION; }
    JSPromiseStateEnum st = JS_PromiseState(s->ctx, p);
    if (st == JS_PROMISE_FULFILLED || st == JS_PROMISE_NOT_A_PROMISE) {
      JSValue v = st == JS_PROMISE_FULFILLED ? JS_PromiseResult(s->ctx, p) : JS_DupValue(s->ctx, p);
      JS_FreeValue(s->ctx, tick); JS_FreeValue(s->ctx, p);
      return v;
    }
    if (st == JS_PROMISE_REJECTED) {
      JS_Throw(s->ctx, JS_PromiseResult(s->ctx, p));
      JS_FreeValue(s->ctx, tick); JS_FreeValue(s->ctx, p);
      return JS_EXCEPTION;
    }
    JSValue more = JS_Call(s->ctx, tick, JS_UNDEFINED, 0, NULL);
    if (JS_IsException(more)) { JS_FreeValue(s->ctx, tick); JS_FreeValue(s->ctx, p); return JS_EXCEPTION; }
    bool any = JS_ToBool(s->ctx, more);
    JS_FreeValue(s->ctx, more);
    if (!any) {
      JS_FreeValue(s->ctx, tick); JS_FreeValue(s->ctx, p);
      JS_ThrowInternalError(s->ctx, "the capture stopped with nothing left to run");
      return JS_EXCEPTION;
    }
  }
}

static void bclose(bsession *s) {
  if (s->ctx) JS_FreeContext(s->ctx);
  if (s->rt) JS_FreeRuntime(s->rt);
  s->ctx = NULL; s->rt = NULL;
}
static bool bopen(bsession *s, char *err, size_t cap) {
  s->rt = JS_NewRuntime();
  s->ctx = s->rt ? JS_NewContext(s->rt) : NULL;
  if (!s->ctx) { snprintf(err, cap, "QuickJS: out of memory"); bclose(s); return false; }
  /* (QuickJS's default 1 MB stack bound holds the capture with room to spare:
   * the thread that renders needs a little over that) */
  JS_SetModuleLoaderFunc(s->rt, bnormalize, bload, NULL);
  const t3_builder_file *ans = bfile("gpu-answers.json"), *entry = bfile("builder-entry.mjs");
  if (!ans || !entry) { snprintf(err, cap, "the builder's JS is not embedded"); bclose(s); return false; }
  JSValue gl = JS_GetGlobalObject(s->ctx);
  JS_SetPropertyStr(s->ctx, gl, "__t3_answers", JS_NewStringLen(s->ctx, (const char *)ans->data, ans->len));
  JS_FreeValue(s->ctx, gl);
  JSValue v = bawait(s, JS_Eval(s->ctx, (const char *)entry->data, entry->len, "builder-entry.mjs", JS_EVAL_TYPE_MODULE));
  if (JS_IsException(v)) { berr(s->ctx, err, cap, "loading the builder"); bclose(s); return false; }
  JS_FreeValue(s->ctx, v);
  return true;
}
static JSValue bcall(bsession *s, const char *fn, int argc, JSValue *argv) {
  JSValue gl = JS_GetGlobalObject(s->ctx);
  JSValue f = JS_GetPropertyStr(s->ctx, gl, fn);
  JSValue v = JS_Call(s->ctx, f, JS_UNDEFINED, argc, argv);
  JS_FreeValue(s->ctx, f);
  JS_FreeValue(s->ctx, gl);
  return v;
}
/* one capture in its own runtime: the JSON text (malloc'd), or NULL */
static char *bcapture(const char *state, const char *be, bool probe, char *err, size_t cap) {
  bsession s = { 0 };
  if (!bopen(&s, err, cap)) return NULL;
  JSValue a[3] = { JS_NewString(s.ctx, state), JS_NewString(s.ctx, be), JS_NewBool(s.ctx, probe) };
  JSValue v = bawait(&s, bcall(&s, "__t3_capture", 3, a));
  for (int i = 0; i < 3; i++) JS_FreeValue(s.ctx, a[i]);
  char *out = NULL;
  if (JS_IsException(v)) berr(s.ctx, err, cap, probe ? "the probe capture" : "the capture");
  else {
    size_t n;
    const char *t = JS_ToCStringLen(s.ctx, &n, v);
    if (t) { out = malloc(n + 1); T3_CHECK_ALLOC(out); memcpy(out, t, n + 1); JS_FreeCString(s.ctx, t); }
  }
  JS_FreeValue(s.ctx, v);
  bclose(&s);
  return out;
}

/* ── the model object as a t3_gen_program ─────────────────────────── */
static JSValue P(JSContext *c, JSValueConst o, const char *k) { return JS_GetPropertyStr(c, o, k); }
static int64_t I(JSContext *c, JSValueConst o, const char *k) {
  JSValue v = P(c, o, k);
  int64_t n = 0;
  if (JS_IsBool(v)) n = JS_ToBool(c, v);
  else JS_ToInt64(c, &n, v);
  JS_FreeValue(c, v);
  return n;
}
static char *dupjs(JSContext *c, JSValueConst v) {
  if (JS_IsNull(v) || JS_IsUndefined(v)) return NULL;
  size_t n;
  const char *t = JS_ToCStringLen(c, &n, v);
  if (!t) return NULL;
  char *s = malloc(n + 1);
  T3_CHECK_ALLOC(s);
  memcpy(s, t, n + 1);
  JS_FreeCString(c, t);
  return s;
}
static char *S(JSContext *c, JSValueConst o, const char *k) { JSValue v = P(c, o, k); char *s = dupjs(c, v); JS_FreeValue(c, v); return s; }
static int64_t LEN(JSContext *c, JSValueConst a) { int64_t n = 0; JS_GetLength(c, a, &n); return n; }
static void *ZALLOC(size_t n) { void *p = calloc(n ? n : 1, 1); T3_CHECK_ALLOC(p); return p; }

static t3_gen_program *bconvert(JSContext *c, JSValueConst m, t3_gen_backend be) {
  t3_gen_program *p = ZALLOC(sizeof *p);
  p->state = S(c, m, "state");
  p->kind = (t3_gen_kind)I(c, m, "kind");
  p->backend = be;
  p->extension = S(c, m, "extension");
  p->vertex = S(c, m, "vertex");
  p->fragment = S(c, m, "fragment");
  JSValue a = P(c, m, "features");
  for (int64_t i = 0, n = LEN(c, a); i < n; i++) { JSValue v = JS_GetPropertyUint32(c, a, (uint32_t)i); int32_t b = 0; JS_ToInt32(c, &b, v); p->features |= 1ull << b; JS_FreeValue(c, v); }
  JS_FreeValue(c, a);
  static const char *const arr3[] = { "lights", "tpl", "cast" };
  for (int k = 0; k < 3; k++) {
    a = P(c, m, arr3[k]);
    for (uint32_t i = 0; i < (k ? 3u : 4u); i++) {
      JSValue v = JS_GetPropertyUint32(c, a, i);
      int32_t x = 0;
      JS_ToInt32(c, &x, v);
      JS_FreeValue(c, v);
      if (k == 0) p->lights[i] = (uint8_t)x; else if (k == 1) p->tpl[i] = (uint16_t)x; else p->cast[i] = (uint8_t)x;
    }
    JS_FreeValue(c, a);
  }
  /* groups */
  a = P(c, m, "groups");
  p->n_groups = (uint8_t)LEN(c, a);
  const t3_bk_group_layout **gs = ZALLOC(sizeof *gs * p->n_groups);
  for (uint32_t gi = 0; gi < p->n_groups; gi++) {
    JSValue g = JS_GetPropertyUint32(c, a, gi);
    t3_bk_group_layout *gl = ZALLOC(sizeof *gl);
    gl->name = S(c, g, "name");
    gl->index = (uint32_t)I(c, g, "index");
    gl->uniform_bytes = (uint32_t)I(c, g, "uniformBytes");
    gl->uniform_binding = (uint32_t)I(c, g, "uniformBinding");
    JSValue ts = P(c, g, "textures");
    gl->n_textures = (uint32_t)LEN(c, ts);
    if (gl->n_textures) {
      t3_bk_texture_layout *t = ZALLOC(sizeof *t * gl->n_textures);
      for (uint32_t i = 0; i < gl->n_textures; i++) {
        JSValue x = JS_GetPropertyUint32(c, ts, i);
        t[i] = (t3_bk_texture_layout){ S(c, x, "name"), (uint32_t)I(c, x, "binding"), (t3_bk_sample_type)I(c, x, "sample"), (t3_bk_tex_dim)I(c, x, "dim"),
          I(c, x, "hasSampler") != 0, I(c, x, "compare") != 0, I(c, x, "storage") != 0, S(c, x, "source"), (uint32_t)I(c, x, "samplerBinding") };
        JS_FreeValue(c, x);
      }
      gl->textures = t;
    }
    JS_FreeValue(c, ts);
    JSValue bs = P(c, g, "buffers");
    gl->n_buffers = (uint32_t)LEN(c, bs);
    if (gl->n_buffers) {
      t3_bk_buffer_layout *b = ZALLOC(sizeof *b * gl->n_buffers);
      for (uint32_t i = 0; i < gl->n_buffers; i++) {
        JSValue x = JS_GetPropertyUint32(c, bs, i);
        b[i] = (t3_bk_buffer_layout){ S(c, x, "name"), (uint32_t)I(c, x, "binding"), (uint32_t)I(c, x, "bytes"), I(c, x, "storage") != 0, S(c, x, "source") };
        JS_FreeValue(c, x);
      }
      gl->buffers = b;
    }
    JS_FreeValue(c, bs);
    gs[gi] = gl;
    JS_FreeValue(c, g);
  }
  p->groups = gs;
  JS_FreeValue(c, a);
  /* properties */
  a = P(c, m, "properties");
  p->n_properties = (uint16_t)LEN(c, a);
  t3_gen_property *pr = ZALLOC(sizeof *pr * p->n_properties);
  for (uint32_t i = 0; i < p->n_properties; i++) {
    JSValue x = JS_GetPropertyUint32(c, a, i);
    pr[i] = (t3_gen_property){ S(c, x, "path"), (uint8_t)I(c, x, "group"), (uint16_t)I(c, x, "offset"), (uint16_t)I(c, x, "length"), I(c, x, "mat3") != 0 };
    JS_FreeValue(c, x);
  }
  p->properties = pr;
  JS_FreeValue(c, a);
  /* attributes */
  a = P(c, m, "attributes");
  p->n_attributes = (uint8_t)LEN(c, a);
  t3_gen_attribute *at = ZALLOC(sizeof *at * p->n_attributes);
  for (uint32_t i = 0; i < p->n_attributes; i++) {
    JSValue x = JS_GetPropertyUint32(c, a, i);
    at[i] = (t3_gen_attribute){ S(c, x, "name"), S(c, x, "type"), (uint8_t)I(c, x, "location"), I(c, x, "instanced") != 0 };
    JS_FreeValue(c, x);
  }
  p->attributes = at;
  JS_FreeValue(c, a);
  /* constants: hex bytes */
  a = P(c, m, "constants");
  p->n_constants = (uint8_t)LEN(c, a);
  if (p->n_constants) {
    t3_gen_constant *k = ZALLOC(sizeof *k * p->n_constants);
    for (uint32_t i = 0; i < p->n_constants; i++) {
      JSValue x = JS_GetPropertyUint32(c, a, i);
      char *hex = S(c, x, "bytes");
      size_t n = hex ? strlen(hex) / 2 : 0;
      uint8_t *bytes = ZALLOC(n);
      for (size_t j = 0; j < n; j++) { unsigned v = 0; sscanf(hex + 2 * j, "%2x", &v); bytes[j] = (uint8_t)v; }
      free(hex);
      k[i] = (t3_gen_constant){ (uint8_t)I(c, x, "group"), (uint16_t)I(c, x, "offset"), (uint16_t)n, bytes };
      JS_FreeValue(c, x);
    }
    p->constants = k;
  }
  JS_FreeValue(c, a);
  return p;
}

const t3_gen_program *t3_builder_build(const char *state, t3_gen_backend backend, char *err, size_t cap) {
  const char *be = backend == T3_GEN_WGPU ? "wgpu" : "gl";
  char *primary = bcapture(state, be, false, err, cap);
  if (!primary) return NULL;
  bsession s = { 0 };
  char *probe = NULL;
  t3_gen_program *p = NULL;
  const t3_gen_program *kept = NULL;
  if (!bopen(&s, err, cap)) goto done;
  JSValue a[4] = { JS_NewString(s.ctx, primary), JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED };
  JSValue need = bcall(&s, "__t3_needs_probe", 1, a);
  JS_FreeValue(s.ctx, a[0]);
  if (JS_IsException(need)) { berr(s.ctx, err, cap, "the capture"); goto done; }
  bool want_probe = JS_ToBool(s.ctx, need);
  JS_FreeValue(s.ctx, need);
  if (want_probe && !(probe = bcapture(state, be, true, err, cap))) goto done;
  a[0] = JS_NewString(s.ctx, state); a[1] = JS_NewString(s.ctx, be); a[2] = JS_NewString(s.ctx, primary);
  a[3] = probe ? JS_NewString(s.ctx, probe) : JS_NULL;
  JSValue m = bcall(&s, "__t3_model", 4, a);
  for (int i = 0; i < 4; i++) JS_FreeValue(s.ctx, a[i]);
  if (JS_IsException(m)) { berr(s.ctx, err, cap, "the program model"); goto done; }
  p = bconvert(s.ctx, m, backend);
  JS_FreeValue(s.ctx, m);
  kept = t3_gen_add_built(p);
done:
  bclose(&s);
  free(primary);
  free(probe);
  return kept;
}
