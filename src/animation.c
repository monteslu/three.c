/* Animation: KeyframeTrack (vector / quaternion / number),
 * AnimationClip, AnimationMixer + AnimationAction with LoopRepeat.
 *
 * One mixer drives clips on a root. Each action resolves its tracks once,
 * PropertyBinding style ("objName.property", searched depth first from the
 * root), to the mixer's PropertyMixer for that (object, property), which
 * every action animating it shares. An update evaluates each enabled action
 * with weight > 0 at its time and accumulates the result by weight, then
 * each PropertyMixer blends what is missing of a total weight of 1 from the
 * value it saved when it was first activated, and writes the property:
 * NormalAnimationBlendMode. Fades, warps and additive blending are not
 * ported. */
#include <math.h>
#include <string.h>

#include "internal.h"

typedef struct { uint32_t k; int r; void (*destroy)(void *); } misc_header;

/* ── tracks ──────────────────────────────────────────────────────── */
t3_keyframe_track *t3_keyframe_track_new_ex(t3_track_type type, const char *name, const float *times, int n,
                                            const float *values, int value_size, t3_interpolation interp) {
  t3_keyframe_track *t = calloc(1, sizeof *t);
  T3_CHECK_ALLOC(t);
  t->type = type;
  t->value_size = value_size;
  t->interpolation = interp;
  strncpy(t->name, name, sizeof t->name - 1);
  size_t nv = (size_t)n * value_size * (interp == T3_INTERPOLATE_CUBIC_SPLINE_GLTF ? 3 : 1);
  t->times = malloc((size_t)(n ? n : 1) * sizeof *t->times);
  t->values = malloc((nv ? nv : 1) * sizeof *t->values);
  T3_CHECK_ALLOC(t->times);
  T3_CHECK_ALLOC(t->values);
  memcpy(t->times, times, (size_t)n * sizeof *times);
  memcpy(t->values, values, nv * sizeof *values);
  t->count = n;
  return t;
}

t3_keyframe_track *t3_keyframe_track_new(t3_track_type type, const char *name, const float *times, int n,
                                         const float *values) {
  return t3_keyframe_track_new_ex(type, name, times, n, values,
                                  type == T3_TRACK_QUATERNION ? 4 : type == T3_TRACK_VECTOR ? 3 : 1, T3_INTERPOLATE_LINEAR);
}

/* PropertyBinding.sanitizeNodeName: whitespace to '_', drop [ ] . : / */
const char *t3_property_binding_sanitize(const char *name, char *out, size_t cap) {
  size_t n = 0;
  for (const char *p = name; *p && n + 1 < cap; p++) {
    char ch = *p;
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v') ch = '_';
    else if (ch == '[' || ch == ']' || ch == '.' || ch == ':' || ch == '/') continue;
    out[n++] = ch;
  }
  if (cap) out[n] = 0;
  return out;
}

static void track_free(t3_keyframe_track *t) {
  if (!t) return;
  free(t->times);
  free(t->values);
  free(t);
}

/* ── clips ───────────────────────────────────────────────────────── */
static void clip_destroy(void *p) {
  t3_animation_clip *c = p;
  for (int i = 0; i < c->track_count; i++) track_free(c->tracks[i]);
  free(c->tracks);
}

/* Takes ownership of the tracks. duration < 0: AnimationClip.resetDuration,
 * the last keyframe time of any track. */
t3_animation_clip *t3_animation_clip_new(const char *name, float duration, t3_keyframe_track **tracks, int n) {
  t3_animation_clip *c = t3__alloc(sizeof *c, T3_KIND_MISC);
  ((misc_header *)c)->destroy = clip_destroy;
  strncpy(c->name, name, sizeof c->name - 1);
  c->tracks = malloc((size_t)(n ? n : 1) * sizeof *c->tracks);
  T3_CHECK_ALLOC(c->tracks);
  memcpy(c->tracks, tracks, (size_t)n * sizeof *tracks);
  c->track_count = n;
  if (duration < 0) {
    duration = 0;
    for (int i = 0; i < n; i++)
      if (tracks[i]->count && tracks[i]->times[tracks[i]->count - 1] > duration)
        duration = tracks[i]->times[tracks[i]->count - 1];
  }
  c->duration = duration;
  return c;
}

/* ── bindings ────────────────────────────────────────────────────── */
static t3_object *find_by_name(t3_object *o, const char *name, size_t len) {
  if (o->name && strlen(o->name) == len && !strncmp(o->name, name, len)) return o;
  for (int i = 0; i < o->child_count; i++) {
    t3_object *f = find_by_name(o->children[i], name, len);
    if (f) return f;
  }
  return NULL;
}

/* "nodeName.property" -> object + property. A missing node binds to nothing
 * (three.js warns and skips the track). */
static void bind_track(t3_object *root, const t3_keyframe_track *t, t3_track_binding *b) {
  memset(b, 0, sizeof *b);
  const char *dot = strrchr(t->name, '.');
  const char *prop = dot ? dot + 1 : t->name;
  b->target = dot ? find_by_name(root, t->name, (size_t)(dot - t->name)) : root;
  if (!strcmp(prop, "position")) b->property = T3_PROP_POSITION;
  else if (!strcmp(prop, "quaternion")) b->property = T3_PROP_QUATERNION;
  else if (!strcmp(prop, "scale")) b->property = T3_PROP_SCALE;
  else if (!strcmp(prop, "renderOrder")) b->property = T3_PROP_RENDER_ORDER;
  else if (!strcmp(prop, "visible")) b->property = T3_PROP_VISIBLE;
  else if (!strcmp(prop, "morphTargetInfluences")) b->property = T3_PROP_MORPH_INFLUENCES;
  else b->target = NULL;
}

/* ── interpolation ───────────────────────────────────────────────── */
/* Interpolant.evaluate: the interval holding t, searched from the cached
 * index first (frames advance a little at a time), then linear or slerp. */
static void evaluate(const t3_keyframe_track *tr, int *cache, float t, float *out) {
  const float *pp = tr->times;
  int n = tr->count, vs = tr->value_size, i1 = *cache;
  bool cubic = tr->interpolation == T3_INTERPOLATE_CUBIC_SPLINE_GLTF;
  /* the value of key k: CUBICSPLINE keys are (in tangent, value, out tangent) */
#define KEY(k) (tr->values + (size_t)(k) * vs * (cubic ? 3 : 1) + (cubic ? vs : 0))
  if (n == 0) return;
  if (t <= pp[0]) { memcpy(out, KEY(0), (size_t)vs * sizeof *out); *cache = 0; return; }
  if (t >= pp[n - 1]) { memcpy(out, KEY(n - 1), (size_t)vs * sizeof *out); *cache = n; return; }
  if (i1 < 1 || i1 >= n || !(pp[i1 - 1] <= t && t < pp[i1])) {
    /* forward from the cache, else binary search */
    if (i1 >= 1 && i1 < n && t >= pp[i1] && i1 + 1 < n && t < pp[i1 + 1]) i1++;
    else {
      int lo = 0, hi = n;
      while (lo < hi) { int mid = (lo + hi) >> 1; if (t < pp[mid]) hi = mid; else lo = mid + 1; }
      i1 = lo;
    }
  }
  *cache = i1;
  float t0 = pp[i1 - 1], t1 = pp[i1];
  if (tr->interpolation == T3_INTERPOLATE_DISCRETE) {
    memcpy(out, KEY(i1 - 1), (size_t)vs * sizeof *out);
    return;
  }
  if (cubic) {
    /* GLTFCubicSplineInterpolant (GLTFLoader) */
    const float *v = tr->values;
    int s3 = vs * 3, o1 = i1 * s3, o0 = o1 - s3;
    float td = t1 - t0, p = (t - t0) / td, p2 = p * p, p3 = p2 * p;
    float s2c = -2 * p3 + 3 * p2, s3c = p3 - p2, s0c = 1 - s2c, s1c = s3c - p2 + p;
    for (int k = 0; k < vs; k++) {
      float P0 = v[o0 + k + vs], M0 = v[o0 + k + 2 * vs] * td, P1 = v[o1 + k + vs], M1 = v[o1 + k] * td;
      out[k] = s0c * P0 + s1c * M0 + s2c * P1 + s3c * M1;
    }
    return;
  }
  float alpha = (t - t0) / (t1 - t0);
  const float *a = KEY(i1 - 1), *b = KEY(i1);
  if (tr->type == T3_TRACK_QUATERNION) t3_quat_slerp_flat(out, a, b, alpha);
  else for (int k = 0; k < vs; k++) out[k] = a[k] * (1 - alpha) + b[k] * alpha;
#undef KEY
}

static void apply(const t3_track_binding *b, const float *v) {
  t3_object *o = b->target;
  if (!o) return;
  switch (b->property) {
  case T3_PROP_POSITION: o->position = t3_v3(v[0], v[1], v[2]); break;
  case T3_PROP_SCALE: o->scale = t3_v3(v[0], v[1], v[2]); break;
  case T3_PROP_QUATERNION: { t3_quat q = { v[0], v[1], v[2], v[3] }; t3_object_set_quaternion(o, q); break; }
  case T3_PROP_RENDER_ORDER: o->render_order = (int)v[0]; break;
  case T3_PROP_VISIBLE: o->visible = v[0] != 0; break;
  case T3_PROP_MORPH_INFLUENCES: break; /* needs the value size: see apply_morph */
  }
}

static void apply_morph(t3_object *o, const float *v, int n) {
  if (o->type != T3_MESH && o->type != T3_SKINNED_MESH) {
    for (int i = 0; i < o->child_count; i++) apply_morph(o->children[i], v, n);
    return;
  }
  t3_mesh *m = (t3_mesh *)o;
  for (int i = 0; i < n && i < m->morph_influence_count; i++) m->morph_influences[i] = v[i];
}

/* ── PropertyMixer ───────────────────────────────────────────────── */
struct t3_property_mixer {
  t3_track_binding binding;
  int value_size;
  /* buffer: incoming | accu | original, value_size floats each */
  float buf[3 * 16];
  float cumulative_weight;
  bool saved, used; /* used: a playing action binds it (useCount > 0) */
};

/* binding.getValue */
static void get_value(const t3_track_binding *b, float *v, int n) {
  t3_object *o = b->target;
  switch (b->property) {
  case T3_PROP_POSITION: v[0] = o->position.x; v[1] = o->position.y; v[2] = o->position.z; break;
  case T3_PROP_SCALE: v[0] = o->scale.x; v[1] = o->scale.y; v[2] = o->scale.z; break;
  case T3_PROP_QUATERNION: v[0] = o->quaternion.x; v[1] = o->quaternion.y; v[2] = o->quaternion.z; v[3] = o->quaternion.w; break;
  case T3_PROP_RENDER_ORDER: v[0] = (float)o->render_order; break;
  case T3_PROP_VISIBLE: v[0] = o->visible; break;
  case T3_PROP_MORPH_INFLUENCES: {
    /* the first mesh a node track fans out to */
    while (o && o->type != T3_MESH && o->type != T3_SKINNED_MESH) o = o->child_count ? o->children[0] : NULL;
    const t3_mesh *m = (const t3_mesh *)o;
    for (int i = 0; i < n; i++) v[i] = m && i < m->morph_influence_count ? m->morph_influences[i] : 0;
    break;
  }
  }
}

/* _lerp / _slerp / _select: dst := mix(dst, src, t) */
static void mix_region(const t3_property_mixer *p, float *dst, const float *src, float t) {
  if (p->binding.property == T3_PROP_QUATERNION) t3_quat_slerp_flat(dst, dst, src, t);
  else if (p->binding.property == T3_PROP_VISIBLE || p->binding.property == T3_PROP_RENDER_ORDER) {
    if (t >= 0.5f) memcpy(dst, src, (size_t)p->value_size * sizeof *dst);
  } else {
    for (int i = 0; i < p->value_size; i++) dst[i] = dst[i] * (1 - t) + src[i] * t;
  }
}

/* accumulate( accuIndex, weight ): the incoming value into the accumulator */
static void accumulate(t3_property_mixer *p, float weight) {
  int n = p->value_size;
  float *in = p->buf, *accu = p->buf + n;
  if (p->cumulative_weight == 0) {
    memcpy(accu, in, (size_t)n * sizeof *accu);
    p->cumulative_weight = weight;
  } else {
    p->cumulative_weight += weight;
    mix_region(p, accu, in, weight / p->cumulative_weight);
  }
}

/* ── mixer + actions ─────────────────────────────────────────────── */
static void mixer_destroy(void *p) {
  t3_animation_mixer *m = p;
  for (int i = 0; i < m->action_count; i++) {
    t3_release(m->actions[i]->clip);
    free(m->actions[i]->props);
    free(m->actions[i]->cache);
    free(m->actions[i]);
  }
  free(m->actions);
  free(m->props);
  t3_release(m->root);
}

t3_animation_mixer *t3_animation_mixer_new(t3_object *root) {
  t3_animation_mixer *m = t3__alloc(sizeof *m, T3_KIND_MISC);
  ((misc_header *)m)->destroy = mixer_destroy;
  m->root = t3_retain(root);
  m->time_scale = 1;
  return m;
}

/* the mixer's PropertyMixer for a binding, made on first use */
static int property_mixer(t3_animation_mixer *m, const t3_track_binding *b, int value_size) {
  for (int i = 0; i < m->prop_count; i++)
    if (m->props[i].binding.target == b->target && m->props[i].binding.property == b->property) return i;
  m->props = realloc(m->props, (size_t)(m->prop_count + 1) * sizeof *m->props);
  T3_CHECK_ALLOC(m->props);
  t3_property_mixer *p = &m->props[m->prop_count];
  memset(p, 0, sizeof *p);
  p->binding = *b;
  p->value_size = value_size;
  return m->prop_count++;
}

t3_animation_action *t3_animation_mixer_clip_action(t3_animation_mixer *m, t3_animation_clip *clip) {
  for (int i = 0; i < m->action_count; i++)
    if (m->actions[i]->clip == clip) return m->actions[i];
  t3_animation_action *a = calloc(1, sizeof *a);
  T3_CHECK_ALLOC(a);
  a->clip = t3_retain(clip);
  a->time_scale = 1;
  a->weight = 1;
  a->enabled = true;
  a->loop_count = -1;
  a->props = malloc((size_t)(clip->track_count ? clip->track_count : 1) * sizeof *a->props);
  a->cache = calloc((size_t)(clip->track_count ? clip->track_count : 1), sizeof *a->cache);
  T3_CHECK_ALLOC(a->props);
  T3_CHECK_ALLOC(a->cache);
  for (int i = 0; i < clip->track_count; i++) {
    t3_track_binding b;
    bind_track(m->root, clip->tracks[i], &b);
    /* more morph targets than one PropertyMixer holds: skipped */
    a->props[i] = b.target && clip->tracks[i]->value_size <= 16 ? property_mixer(m, &b, clip->tracks[i]->value_size) : -1;
  }
  m->actions = realloc(m->actions, (size_t)(m->action_count + 1) * sizeof *m->actions);
  T3_CHECK_ALLOC(m->actions);
  m->actions[m->action_count++] = a;
  return a;
}

void t3_animation_action_play(t3_animation_action *a) { a->playing = true; }
void t3_animation_action_stop(t3_animation_action *a) { a->playing = false; a->time = 0; a->loop_count = -1; }
void t3_animation_action_set_effective_weight(t3_animation_action *a, float weight) { a->weight = weight; }
void t3_animation_action_set_effective_time_scale(t3_animation_action *a, float ts) { a->time_scale = ts; }
float t3_animation_action_get_effective_weight(const t3_animation_action *a) { return a->enabled ? a->weight : 0; }

/* AnimationAction._updateTime for LoopRepeat with infinite repetitions. */
static float update_time(t3_animation_action *a, float dt) {
  float duration = a->clip->duration, time = a->time + dt;
  if (dt == 0) return a->time;
  if (a->loop_count == -1 && dt >= 0) a->loop_count = 0;
  if (duration > 0 && (time >= duration || time < 0)) {
    float loop_delta = floorf(time / duration);
    time -= duration * loop_delta;
    a->loop_count += (int)fabsf(loop_delta);
  }
  a->time = time;
  return time;
}

void t3_animation_mixer_update(t3_animation_mixer *m, float dt) {
  dt *= m->time_scale;
  m->time += dt;
  /* activating an action saves the original state of the properties it binds
   * (PropertyMixer.saveOriginalState, when its first user activates) */
  for (int i = 0; i < m->prop_count; i++) m->props[i].used = false;
  for (int i = 0; i < m->action_count; i++) {
    t3_animation_action *a = m->actions[i];
    if (!a->playing) continue;
    for (int k = 0; k < a->clip->track_count; k++) {
      if (a->props[k] < 0) continue;
      t3_property_mixer *p = &m->props[a->props[k]];
      p->used = true;
      if (p->saved) continue;
      float *orig = p->buf + 2 * p->value_size;
      get_value(&p->binding, orig, p->value_size);
      memcpy(p->buf + p->value_size, orig, (size_t)p->value_size * sizeof *orig);
      p->saved = true;
    }
  }
  /* run the active actions (_update) */
  for (int i = 0; i < m->action_count; i++) {
    t3_animation_action *a = m->actions[i];
    if (!a->playing || !a->enabled) continue;
    float t = update_time(a, dt * (a->paused ? 0 : a->time_scale));
    float weight = a->weight;
    if (weight <= 0) continue;
    for (int k = 0; k < a->clip->track_count; k++) {
      if (a->props[k] < 0) continue;
      t3_property_mixer *p = &m->props[a->props[k]];
      evaluate(a->clip->tracks[k], &a->cache[k], t, p->buf);
      accumulate(p, weight);
    }
  }
  /* update the scene graph: every binding an active action uses (apply) */
  for (int i = 0; i < m->prop_count; i++) {
    t3_property_mixer *p = &m->props[i];
    if (!p->used) continue;
    int n = p->value_size;
    float *accu = p->buf + n;
    if (p->cumulative_weight < 1) mix_region(p, accu, p->buf + 2 * n, 1 - p->cumulative_weight);
    p->cumulative_weight = 0;
    if (p->binding.property == T3_PROP_MORPH_INFLUENCES) apply_morph(p->binding.target, accu, n);
    else apply(&p->binding, accu);
  }
}
