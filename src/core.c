/* Reference counting, Object3D and its subclasses, materials, textures. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "internal.h"

void (*t3__gl_release)(uint32_t kind, void *thing);
void (*t3__wgpu_release)(uint32_t kind, void *thing);

/* ── the allocator (t3_set_allocator) ─────────────────────────────── */
static void *(*g_malloc)(size_t);
static void *(*g_realloc)(void *, size_t);
static void (*g_free)(void *);

void t3_set_allocator(void *(*malloc_fn)(size_t), void *(*realloc_fn)(void *, size_t), void (*free_fn)(void *)) {
  g_malloc = malloc_fn;
  g_realloc = realloc_fn;
  g_free = free_fn;
}
/* the parenthesised names are libc's (the macros in internal.h are
 * function-like, so they do not expand here) */
void *t3__malloc(size_t n) { return g_malloc ? g_malloc(n) : (malloc)(n); }
void *t3__realloc(void *p, size_t n) { return g_realloc ? g_realloc(p, n) : (realloc)(p, n); }
void t3__free(void *p) {
  if (!p) return;
  if (g_free) g_free(p);
  else (free)(p);
}
void *t3__calloc(size_t n, size_t size) {
  if (!g_malloc) return (calloc)(n, size);
  if (size && n > (size_t)-1 / size) return NULL;
  void *p = g_malloc(n * size);
  if (p) memset(p, 0, n * size);
  return p;
}
void *t3_malloc(size_t n) { return t3__malloc(n); }
void t3_free(void *p) { t3__free(p); }

static uint32_t g_next_id = 1;
uint32_t t3__next_id(void) { return g_next_id++; }

void t3__fatal(const char *what) {
  fprintf(stderr, "three.c: %s\n", what);
  abort();
}

void *t3__alloc(size_t size, uint32_t kind) {
  uint32_t *p = calloc(1, size);
  T3_CHECK_ALLOC(p);
  p[0] = kind;
  ((int *)p)[1] = 1;
  return p;
}

typedef struct { uint32_t kind; int refs; } header;

void *t3_retain(void *thing) {
  if (thing) ((header *)thing)->refs++;
  return thing;
}

static void object_free(t3_object *o);
static void geometry_free(t3_geometry *g);

void t3_release(void *thing) {
  header *h = thing;
  if (!h || --h->refs > 0) return;
  switch (h->kind) {
  case T3_KIND_OBJECT: object_free(thing); break;
  case T3_KIND_GEOMETRY: geometry_free(thing); break;
  case T3_KIND_MATERIAL: {
    t3_material *m = thing;
    t3_release(m->map);
    t3_release(m->normal_map); t3_release(m->ao_map); t3_release(m->emissive_map);
    t3_release(m->roughness_map); t3_release(m->metalness_map); t3_release(m->alpha_map);
    t3_release(m->env_map);
    t3_release(m->light_map);
    t3_release(m->clearcoat_normal_map);
    free(m);
    break;
  }
  case T3_KIND_TEXTURE: {
    t3_texture *t = thing;
    if (t3__gl_release && t->_gl) t3__gl_release(T3_KIND_TEXTURE, t);
    if (t3__wgpu_release && t->_wgpu) t3__wgpu_release(T3_KIND_TEXTURE, t);
    free(t->pixels);
    free(t);
    break;
  }
  case T3_KIND_ATTRIBUTE: {
    t3_attribute *a = thing;
    if (t3__gl_release && a->_gl) t3__gl_release(T3_KIND_ATTRIBUTE, a);
    if (t3__wgpu_release && a->_wgpu) t3__wgpu_release(T3_KIND_ATTRIBUTE, a);
    free(a->array);
    free(a);
    break;
  }
  case T3_KIND_RENDER_TARGET: {
    t3_render_target *rt = thing;
    if (t3__gl_release && rt->_gl) t3__gl_release(T3_KIND_RENDER_TARGET, rt);
    if (t3__wgpu_release && rt->_wgpu) t3__wgpu_release(T3_KIND_RENDER_TARGET, rt);
    t3_release(rt->texture);
    t3_release(rt->depth_texture);
    free(rt);
    break;
  }
  case T3_KIND_CURVE: {
    t3_curve *c = thing;
    free(c->points);
    free(c->lengths);
    free(c);
    break;
  }
  case T3_KIND_MISC: {
    /* header, refs, then a destructor the subsystem set */
    struct { uint32_t k; int r; void (*destroy)(void *); } *m = thing;
    if (m->destroy) m->destroy(thing);
    free(thing);
    break;
  }
  default: t3__fatal("t3_release: not a three.c object");
  }
}

/* ── attributes ──────────────────────────────────────────────────── */
static size_t attr_elem_size(t3_attr_type t) { return t == T3_UINT16 ? 2 : 4; }

t3_attribute *t3_attribute_new(t3_attr_type type, const void *data, int count, int item_size) {
  t3_attribute *a = t3__alloc(sizeof *a, T3_KIND_ATTRIBUTE);
  a->type = type;
  a->count = count;
  a->item_size = item_size;
  a->usage = T3_STATIC_DRAW;
  size_t n = (size_t)count * item_size * attr_elem_size(type);
  a->array = calloc(1, n ? n : 1);
  T3_CHECK_ALLOC(a->array);
  if (data) memcpy(a->array, data, n);
  return a;
}

/* ── textures ────────────────────────────────────────────────────── */
t3_texture *t3_texture_new(int w, int h, const uint8_t *rgba) {
  t3_texture *t = t3__alloc(sizeof *t, T3_KIND_TEXTURE);
  t->id = t3__next_id();
  t->width = w;
  t->height = h;
  t->pixels = malloc((size_t)w * h * 4);
  T3_CHECK_ALLOC(t->pixels);
  if (rgba) memcpy(t->pixels, rgba, (size_t)w * h * 4);
  else memset(t->pixels, 255, (size_t)w * h * 4);
  t->wrap_s = t->wrap_t = T3_CLAMP_TO_EDGE;
  t->mag_filter = T3_LINEAR;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->generate_mipmaps = true;
  t->flip_y = true;
  t->unpack_alignment = 4;
  t->anisotropy = 1;
  t->color_space = T3_NO_COLOR_SPACE;
  t->repeat.x = t->repeat.y = 1;
  t->type = T3_UNSIGNED_BYTE_TYPE;
  t->format = T3_RGBA_FORMAT;
  t->mapping = T3_UV_MAPPING;
  t->version = 1;
  return t;
}

t3_texture *t3_cube_texture_new(int size, const uint8_t *const faces[6]) {
  t3_texture *t = t3_texture_new(size, size, NULL);
  size_t face = (size_t)size * size * 4;
  free(t->pixels);
  t->pixels = malloc(face * 6);
  T3_CHECK_ALLOC(t->pixels);
  for (int i = 0; i < 6; i++) {
    if (faces && faces[i]) memcpy(t->pixels + face * i, faces[i], face);
    else memset(t->pixels + face * i, 255, face);
  }
  t->is_cube = true;
  t->needs_flip_env_map = true;
  t->flip_y = false;
  t->mapping = T3_CUBE_REFLECTION_MAPPING;
  return t;
}

static size_t texel_bytes(t3_texture_type type) {
  return type == T3_FLOAT_TYPE ? 16 : type == T3_HALF_FLOAT_TYPE ? 8 : 4;
}

t3_texture *t3_data_texture_new_typed(int w, int h, const void *rgba, t3_texture_type type) {
  t3_texture *t = t3_data_texture_new(w, h, NULL);
  t->type = type;
  size_t n = (size_t)w * h * texel_bytes(type);
  free(t->pixels);
  t->pixels = calloc(1, n ? n : 1);
  T3_CHECK_ALLOC(t->pixels);
  if (rgba) memcpy(t->pixels, rgba, n);
  return t;
}

/* a texture whose texels the GPU writes: no pixels, never uploaded */
static t3_texture *gpu_texture_new(int w, int h) {
  t3_texture *t = t3_texture_new(1, 1, NULL);
  free(t->pixels);
  t->pixels = NULL;
  t->width = w;
  t->height = h;
  t->gpu_made = true;
  return t;
}

t3_texture *t3_depth_texture_new(int w, int h, t3_texture_type type) {
  t3_texture *t = gpu_texture_new(w, h);
  t->format = type == T3_UNSIGNED_INT_248_TYPE ? T3_DEPTH_STENCIL_FORMAT : T3_DEPTH_FORMAT;
  t->type = type ? type : T3_UNSIGNED_INT_TYPE;
  t->mag_filter = t->min_filter = T3_NEAREST;
  t->flip_y = false;
  t->generate_mipmaps = false;
  return t;
}

t3_render_target_options t3_render_target_options_default(void) {
  t3_render_target_options o;
  memset(&o, 0, sizeof o);
  o.wrap_s = o.wrap_t = T3_CLAMP_TO_EDGE;
  o.mag_filter = o.min_filter = T3_LINEAR;
  o.format = T3_RGBA_FORMAT;
  o.type = T3_UNSIGNED_BYTE_TYPE;
  o.anisotropy = 1;
  o.color_space = T3_NO_COLOR_SPACE;
  o.depth_buffer = true;
  return o;
}

t3_render_target *t3_render_target_new(int w, int h, const t3_render_target_options *opt) {
  t3_render_target_options d = t3_render_target_options_default();
  if (!opt) opt = &d;
  t3_render_target *rt = t3__alloc(sizeof *rt, T3_KIND_RENDER_TARGET);
  rt->width = w;
  rt->height = h;
  t3_texture *t = gpu_texture_new(w, h);
  t->wrap_s = opt->wrap_s; t->wrap_t = opt->wrap_t;
  t->mag_filter = opt->mag_filter; t->min_filter = opt->min_filter;
  t->format = opt->format;
  t->type = opt->type;
  t->anisotropy = opt->anisotropy;
  t->color_space = opt->color_space;
  t->generate_mipmaps = opt->generate_mipmaps;
  rt->texture = t;
  rt->depth_buffer = opt->depth_buffer;
  rt->stencil_buffer = opt->stencil_buffer;
  rt->depth_texture = t3_retain(opt->depth_texture);
  rt->samples = opt->samples;
  rt->viewport[2] = rt->scissor[2] = w;
  rt->viewport[3] = rt->scissor[3] = h;
  return rt;
}

void t3_render_target_set_size(t3_render_target *rt, int w, int h) {
  if (rt->width != w || rt->height != h) {
    rt->width = w;
    rt->height = h;
    rt->texture->width = w;
    rt->texture->height = h;
    if (t3__gl_release && rt->_gl) t3__gl_release(T3_KIND_RENDER_TARGET, rt); /* dispose() */
  }
  rt->viewport[0] = rt->viewport[1] = rt->scissor[0] = rt->scissor[1] = 0;
  rt->viewport[2] = rt->scissor[2] = w;
  rt->viewport[3] = rt->scissor[3] = h;
}

t3_texture *t3_data_texture_new(int w, int h, const uint8_t *rgba) {
  t3_texture *t = t3_texture_new(w, h, rgba);
  t->mag_filter = T3_NEAREST;
  t->min_filter = T3_NEAREST;
  t->generate_mipmaps = false;
  t->flip_y = false;
  t->unpack_alignment = 1;
  return t;
}

/* ── materials ───────────────────────────────────────────────────── */
t3_material *t3_material_new(t3_material_type type) {
  t3_material *m = t3__alloc(sizeof *m, T3_KIND_MATERIAL);
  m->id = t3__next_id();
  m->type = type;
  m->color = t3_color_hex(0xffffff);
  m->emissive = t3_color_hex(0x000000);
  m->specular = t3_color_hex(0x111111);
  m->opacity = 1;
  m->shininess = 30;
  m->roughness = 1;
  m->metalness = 0;
  m->emissive_intensity = 1;
  m->depth_test = m->depth_write = m->visible = m->fog = m->tone_mapped = true;
  m->blending = 1;
  m->blend_src = 204; m->blend_dst = 205; m->blend_equation = 100;
  m->blend_src_alpha = m->blend_dst_alpha = m->blend_equation_alpha = -1;
  m->depth_func = 3;
  m->color_write = true;
  m->size = 1;
  m->env_map_intensity = 1;
  m->reflectivity = 1;
  m->refraction_ratio = 0.98f;
  m->size_attenuation = true;
  m->shadow_side = -1;
  m->normal_scale.x = m->normal_scale.y = 1;
  m->ao_map_intensity = 1;
  m->light_map_intensity = 1;
  m->ior = 1.5f;
  m->specular_intensity = 1;
  m->specular_color = (t3_color){ 1, 1, 1 };
  m->clearcoat_normal_scale.x = m->clearcoat_normal_scale.y = 1;
  m->sheen_roughness = 1;
  m->iridescence_ior = 1.3f;
  m->iridescence_thickness_range[0] = 100; m->iridescence_thickness_range[1] = 400;
  m->attenuation_distance = INFINITY;
  m->attenuation_color = (t3_color){ 1, 1, 1 };
  m->version = 1;
  return m;
}

static t3_material *colored(t3_material_type type, uint32_t hex) {
  t3_material *m = t3_material_new(type);
  m->color = t3_color_hex(hex);
  return m;
}
t3_material *t3_mesh_basic_material_new(uint32_t c) { return colored(T3_MESH_BASIC_MATERIAL, c); }
t3_material *t3_line_basic_material_new(uint32_t c) { return colored(T3_LINE_BASIC_MATERIAL, c); }
t3_material *t3_mesh_lambert_material_new(uint32_t c) { return colored(T3_MESH_LAMBERT_MATERIAL, c); }
t3_material *t3_mesh_phong_material_new(uint32_t c) { return colored(T3_MESH_PHONG_MATERIAL, c); }
t3_material *t3_mesh_standard_material_new(uint32_t c) { return colored(T3_MESH_STANDARD_MATERIAL, c); }
t3_material *t3_mesh_physical_material_new(uint32_t c) { return colored(T3_MESH_PHYSICAL_MATERIAL, c); }
t3_material *t3_mesh_normal_material_new(void) { return t3_material_new(T3_MESH_NORMAL_MATERIAL); }

void t3_material_set_map(t3_material *m, t3_texture *map) { t3_material_set_texture(m, T3_MAP, map); }

void t3_material_set_texture(t3_material *m, t3_map_slot slot, t3_texture *t) {
  t3_texture **p = slot == T3_MAP ? &m->map : slot == T3_NORMAL_MAP ? &m->normal_map : slot == T3_AO_MAP ? &m->ao_map
                 : slot == T3_EMISSIVE_MAP ? &m->emissive_map : slot == T3_ROUGHNESS_MAP ? &m->roughness_map
                 : slot == T3_METALNESS_MAP ? &m->metalness_map : slot == T3_ENV_MAP ? &m->env_map
                 : slot == T3_LIGHT_MAP ? &m->light_map : slot == T3_CLEARCOAT_NORMAL_MAP ? &m->clearcoat_normal_map : &m->alpha_map;
  t3_retain(t);
  t3_release(*p);
  *p = t;
  m->version++;
}

t3_material *t3_material_clone(const t3_material *src) {
  t3_material *m = t3_material_new(src->type);
  uint32_t id = m->id;
  memcpy((char *)m + 2 * sizeof(int), (const char *)src + 2 * sizeof(int), sizeof *m - 2 * sizeof(int));
  m->id = id;
  m->version = 1;
  t3_retain(m->map); t3_retain(m->normal_map); t3_retain(m->ao_map); t3_retain(m->emissive_map);
  t3_retain(m->roughness_map); t3_retain(m->metalness_map); t3_retain(m->alpha_map);
  t3_retain(m->env_map);
  t3_retain(m->light_map);
  t3_retain(m->clearcoat_normal_map);
  m->version = 1;
  return m;
}


bool t3_string_replace(char **s, const char *find, const char *with) {
  char *at = *s && *find ? strstr(*s, find) : NULL;
  if (!at) return false;
  size_t a = (size_t)(at - *s), fl = strlen(find), wl = strlen(with), rest = strlen(at + fl);
  char *d = malloc(a + wl + rest + 1);
  T3_CHECK_ALLOC(d);
  memcpy(d, *s, a);
  memcpy(d + a, with, wl);
  memcpy(d + a + wl, at + fl, rest + 1);
  free(*s);
  *s = d;
  return true;
}

/* ── Object3D ────────────────────────────────────────────────────── */
static void object_init(t3_object *o, t3_object_type type) {
  o->id = t3__next_id();
  o->type = type;
  o->scale = t3_v3(1, 1, 1);
  o->up = t3_v3(0, 1, 0);
  o->rotation.order = T3_XYZ;
  o->quaternion = t3_quat_identity();
  t3_mat4_identity(&o->matrix);
  t3_mat4_identity(&o->matrix_world);
  o->matrix_auto_update = true;
  o->visible = true;
  o->frustum_culled = true;
  o->layers = 1;
  o->_md[0] = o->_md[5] = o->_md[10] = o->_md[15] = 1;
  o->_mwd[0] = o->_mwd[5] = o->_mwd[10] = o->_mwd[15] = 1;
  if (type == T3_PERSPECTIVE_CAMERA || type == T3_ORTHOGRAPHIC_CAMERA) {
    double *wi = ((t3_camera *)o)->_mwid;
    wi[0] = wi[5] = wi[10] = wi[15] = 1;
  }
}

static void *object_alloc(size_t size, t3_object_type type) {
  t3_object *o = t3__alloc(size, T3_KIND_OBJECT);
  object_init(o, type);
  return o;
}

t3_object *t3_object_new(void) { return object_alloc(sizeof(t3_object), T3_OBJECT3D); }
t3_object *t3_group_new(void) { return object_alloc(sizeof(t3_object), T3_GROUP); }

void t3_scene_set_background_texture(t3_scene *s, t3_texture *t) {
  t3_retain(t);
  t3_release(s->background_texture);
  s->background_texture = t;
}
void t3_scene_set_environment(t3_scene *s, t3_texture *t) {
  t3_retain(t);
  t3_release(s->environment);
  s->environment = t;
}

t3_scene *t3_scene_new(void) {
  t3_scene *s = object_alloc(sizeof *s, T3_SCENE);
  s->auto_update = true;
  s->background_intensity = s->environment_intensity = 1;
  return s;
}

t3_mesh *t3_mesh_new_multi(t3_geometry *g, t3_material **ms, int count) {
  t3_mesh *m = object_alloc(sizeof *m, T3_MESH);
  m->geometry = t3_retain(g);
  m->materials = calloc(count ? count : 1, sizeof *m->materials);
  T3_CHECK_ALLOC(m->materials);
  for (int i = 0; i < count; i++) m->materials[i] = t3_retain(ms[i]);
  m->material_count = count;
  return m;
}

t3_mesh *t3_mesh_new(t3_geometry *g, t3_material *mat) { return t3_mesh_new_multi(g, &mat, 1); }

static t3_mesh *line_new(t3_geometry *g, t3_material *mat, t3_object_type type) {
  t3_mesh *m = t3_mesh_new_multi(g, &mat, 1);
  m->base.type = type;
  return m;
}
t3_mesh *t3_line_new(t3_geometry *g, t3_material *m) { return line_new(g, m, T3_LINE); }
t3_mesh *t3_line_segments_new(t3_geometry *g, t3_material *m) { return line_new(g, m, T3_LINE_SEGMENTS); }
t3_mesh *t3_line_loop_new(t3_geometry *g, t3_material *m) { return line_new(g, m, T3_LINE_LOOP); }
t3_mesh *t3_points_new(t3_geometry *g, t3_material *m) { return line_new(g, m, T3_POINTS); }

t3_material *t3_points_material_new(uint32_t c) { return colored(T3_POINTS_MATERIAL, c); }
t3_material *t3_sprite_material_new(uint32_t c) {
  t3_material *m = colored(T3_SPRITE_MATERIAL, c);
  m->transparent = true;
  return m;
}

/* Sprite's module-level quad: position + uv, indexed (made once, kept) */
static t3_geometry *sprite_geometry(void) {
  static t3_geometry *g;
  if (g) return g;
  static const float pos[12] = { -0.5f, -0.5f, 0, 0.5f, -0.5f, 0, 0.5f, 0.5f, 0, -0.5f, 0.5f, 0 };
  static const float uv[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
  static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
  g = t3_geometry_new();
  t3_attribute *p = t3_attribute_new(T3_FLOAT32, pos, 4, 3), *u = t3_attribute_new(T3_FLOAT32, uv, 4, 2);
  t3_attribute *i = t3_attribute_new(T3_UINT16, idx, 6, 1);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, p);
  t3_geometry_set_attribute(g, T3_ATTR_UV, u);
  t3_geometry_set_index(g, i);
  t3_release(p); t3_release(u); t3_release(i);
  return g;
}

t3_sprite *t3_sprite_new(t3_material *mat) {
  t3_sprite *sp = object_alloc(sizeof *sp, T3_SPRITE);
  sp->mesh.geometry = t3_retain(sprite_geometry());
  sp->mesh.materials = calloc(1, sizeof *sp->mesh.materials);
  T3_CHECK_ALLOC(sp->mesh.materials);
  sp->mesh.materials[0] = mat ? t3_retain(mat) : t3_sprite_material_new(0xffffff);
  sp->mesh.material_count = 1;
  sp->center.x = sp->center.y = 0.5f;
  return sp;
}

static void lod_destroy(t3_object *o) { free(((t3_lod *)o)->levels); }

t3_lod *t3_lod_new(void) {
  t3_lod *l = object_alloc(sizeof *l, T3_LOD);
  l->auto_update = true;
  l->base.on_destroy = lod_destroy;
  return l;
}

void t3_lod_add_level(t3_lod *lod, void *object, float distance) {
  distance = fabsf(distance);
  int at = 0;
  while (at < lod->level_count && !(distance < lod->levels[at].distance)) at++;
  if (lod->level_count == lod->_level_cap) {
    lod->_level_cap = lod->_level_cap ? lod->_level_cap * 2 : 4;
    lod->levels = realloc(lod->levels, (size_t)lod->_level_cap * sizeof *lod->levels);
    T3_CHECK_ALLOC(lod->levels);
  }
  memmove(lod->levels + at + 1, lod->levels + at, (size_t)(lod->level_count - at) * sizeof *lod->levels);
  lod->levels[at].object = object;
  lod->levels[at].distance = distance;
  lod->level_count++;
  t3_object_add(&lod->base, object);
}

t3_object *t3_lod_get_object_for_distance(const t3_lod *lod, float distance) {
  if (!lod->level_count) return NULL;
  int i = 1;
  for (; i < lod->level_count; i++)
    if (distance < lod->levels[i].distance) break;
  return lod->levels[i - 1].object;
}

void t3_lod_update(t3_lod *lod, const t3_camera *camera) {
  int n = lod->level_count;
  if (n <= 1) return;
  t3_vec3 a = t3_mat4_get_position(&camera->base.matrix_world), b = t3_mat4_get_position(&lod->base.matrix_world);
  float distance = t3_vec3_distance(a, b) / camera->zoom;
  lod->levels[0].object->visible = true;
  int i = 1;
  for (; i < n; i++) {
    if (distance >= lod->levels[i].distance) {
      lod->levels[i - 1].object->visible = false;
      lod->levels[i].object->visible = true;
    } else {
      break;
    }
  }
  lod->current_level = i - 1;
  for (; i < n; i++) lod->levels[i].object->visible = false;
}

/* ── SkeletonHelper ──────────────────────────────────────────────── */
typedef struct {
  t3_mesh mesh;
  t3_object *root;          /* not retained, as three.js */
  t3_object **bones; int bone_count;
} skeleton_helper;

static void bone_list(t3_object *o, t3_object ***list, int *n, int *cap) {
  if (o->type == T3_BONE) {
    if (*n == *cap) {
      *cap = *cap ? *cap * 2 : 32;
      *list = realloc(*list, (size_t)*cap * sizeof **list);
      T3_CHECK_ALLOC(*list);
    }
    (*list)[(*n)++] = o;
  }
  for (int i = 0; i < o->child_count; i++) bone_list(o->children[i], list, n, cap);
}

static void skeleton_helper_update(t3_object *o) {
  skeleton_helper *h = (skeleton_helper *)o;
  t3_attribute *position = h->mesh.geometry->attributes[T3_ATTR_POSITION];
  float *p = position->array;
  t3_mat4 inv, m;
  t3_mat4_invert(&inv, &h->root->matrix_world);
  for (int i = 0, j = 0; i < h->bone_count; i++) {
    t3_object *bone = h->bones[i];
    if (!bone->parent || bone->parent->type != T3_BONE) continue;
    t3_mat4_multiply(&m, &inv, &bone->matrix_world);
    t3_vec3 v = t3_mat4_get_position(&m);
    p[j * 3] = v.x; p[j * 3 + 1] = v.y; p[j * 3 + 2] = v.z;
    t3_mat4_multiply(&m, &inv, &bone->parent->matrix_world);
    v = t3_mat4_get_position(&m);
    p[j * 3 + 3] = v.x; p[j * 3 + 4] = v.y; p[j * 3 + 5] = v.z;
    j += 2;
  }
  position->version++;
}

static void skeleton_helper_destroy(t3_object *o) { free(((skeleton_helper *)o)->bones); }

t3_mesh *t3_skeleton_helper_new(t3_object *object) {
  skeleton_helper *h = object_alloc(sizeof *h, T3_LINE_SEGMENTS);
  int cap = 0, pairs = 0;
  bone_list(object, &h->bones, &h->bone_count, &cap);
  for (int i = 0; i < h->bone_count; i++)
    if (h->bones[i]->parent && h->bones[i]->parent->type == T3_BONE) pairs++;
  t3_attribute *pos = t3_attribute_new(T3_FLOAT32, NULL, pairs * 2, 3), *col = t3_attribute_new(T3_FLOAT32, NULL, pairs * 2, 3);
  memset(pos->array, 0, (size_t)pairs * 6 * sizeof(float));
  float *c = col->array;
  for (int i = 0; i < pairs; i++) {
    const float cc[6] = { 0, 0, 1, 0, 1, 0 }; /* color1 (0, 0, 1), color2 (0, 1, 0) */
    memcpy(c + i * 6, cc, sizeof cc);
  }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, pos);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, col);
  t3_release(pos); t3_release(col);
  t3_material *m = t3_line_basic_material_new(0xffffff);
  m->vertex_colors = true;
  m->depth_test = m->depth_write = false;
  m->transparent = true;
  h->mesh.geometry = g;
  h->mesh.materials = calloc(1, sizeof *h->mesh.materials);
  T3_CHECK_ALLOC(h->mesh.materials);
  h->mesh.materials[0] = m;
  h->mesh.material_count = 1;
  h->root = object;
  /* this.matrix = object.matrixWorld; matrixAutoUpdate = false */
  h->mesh.base.matrix = object->matrix_world;
  h->mesh.base.matrix_auto_update = false;
  h->mesh.base.on_update_matrix_world = skeleton_helper_update;
  h->mesh.base.on_destroy = skeleton_helper_destroy;
  return &h->mesh;
}

t3_mesh *t3_grid_helper_new(float size, int divisions, uint32_t color1, uint32_t color2) {
  t3_color c1 = t3_color_hex(color1), c2 = t3_color_hex(color2);
  float center = divisions / 2.0f, step = size / divisions, half = size / 2;
  int n = (divisions + 1) * 4;
  t3_attribute *pos = t3_attribute_new(T3_FLOAT32, NULL, n, 3), *col = t3_attribute_new(T3_FLOAT32, NULL, n, 3);
  float *v = pos->array, *c = col->array;
  float k = -half;
  for (int i = 0; i <= divisions; i++, k += step) {
    const float p[12] = { -half, 0, k, half, 0, k, k, 0, -half, k, 0, half };
    memcpy(v + i * 12, p, sizeof p);
    t3_color cc = i == center ? c1 : c2;
    for (int j = 0; j < 4; j++) { c[i * 12 + j * 3] = cc.r; c[i * 12 + j * 3 + 1] = cc.g; c[i * 12 + j * 3 + 2] = cc.b; }
  }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, pos);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, col);
  t3_release(pos); t3_release(col);
  t3_material *m = t3_line_basic_material_new(0xffffff);
  m->vertex_colors = true;
  t3_mesh *grid = t3_line_segments_new(g, m);
  t3_release(g); t3_release(m);
  return grid;
}

t3_instanced_mesh *t3_instanced_mesh_new(t3_geometry *g, t3_material *mat, int count) {
  t3_instanced_mesh *im = object_alloc(sizeof *im, T3_INSTANCED_MESH);
  im->mesh.geometry = t3_retain(g);
  im->mesh.materials = calloc(1, sizeof *im->mesh.materials);
  T3_CHECK_ALLOC(im->mesh.materials);
  im->mesh.materials[0] = t3_retain(mat);
  im->mesh.material_count = 1;
  im->count = count;
  im->instance_matrix = t3_attribute_new(T3_FLOAT32, NULL, count, 16);
  im->instance_matrix->usage = T3_DYNAMIC_DRAW;
  t3_mat4 id;
  t3_mat4_identity(&id);
  for (int i = 0; i < count; i++) memcpy((float *)im->instance_matrix->array + i * 16, id.e, sizeof id.e);
  /* three.js InstancedMesh: frustumCulled stays true but is tested
   * against the geometry's sphere, which hides instances placed elsewhere;
   * matching that is a footgun nobody wants, so instanced meshes skip it. */
  im->mesh.base.frustum_culled = false;
  return im;
}

void t3_instanced_mesh_set_matrix_at(t3_instanced_mesh *im, int i, const t3_mat4 *m) {
  memcpy((float *)im->instance_matrix->array + i * 16, m->e, sizeof m->e);
}

void t3_instanced_mesh_get_matrix_at(t3_instanced_mesh *im, int i, t3_mat4 *m) {
  memcpy(m->e, (float *)im->instance_matrix->array + i * 16, sizeof m->e);
}

void t3_instanced_mesh_set_color_at(t3_instanced_mesh *im, int i, t3_color c) {
  if (!im->instance_color) {
    im->instance_color = t3_attribute_new(T3_FLOAT32, NULL, im->instance_matrix->count, 3);
    im->instance_color->usage = T3_DYNAMIC_DRAW;
  }
  float *p = (float *)im->instance_color->array + i * 3;
  p[0] = c.r; p[1] = c.g; p[2] = c.b;
}

t3_skinned_mesh *t3_skinned_mesh_new(t3_geometry *g, t3_material *mat) {
  t3_skinned_mesh *sm = object_alloc(sizeof *sm, T3_SKINNED_MESH);
  sm->mesh.geometry = t3_retain(g);
  sm->mesh.materials = calloc(1, sizeof *sm->mesh.materials);
  T3_CHECK_ALLOC(sm->mesh.materials);
  sm->mesh.materials[0] = t3_retain(mat);
  sm->mesh.material_count = 1;
  t3_mat4_identity(&sm->bind_matrix);
  t3_mat4_identity(&sm->bind_matrix_inverse);
  return sm;
}

t3_object *t3_bone_new(void) { return object_alloc(sizeof(t3_object), T3_BONE); }

static void skeleton_destroy(void *p) {
  t3_skeleton *sk = p;
  if (t3__gl_release && sk->_gl) t3__gl_release(T3_KIND_MISC, sk);
  for (int i = 0; i < sk->bone_count; i++) t3_release(sk->bones[i]);
  free(sk->bones);
  free(sk->bone_inverses);
  free(sk->bone_matrices);
}

t3_skeleton *t3_skeleton_new(t3_object *const *bones, int n, const t3_mat4 *inverses) {
  t3_skeleton *sk = t3__alloc(sizeof *sk, T3_KIND_MISC);
  sk->_destroy = skeleton_destroy;
  sk->bones = calloc((size_t)(n ? n : 1), sizeof *sk->bones);
  sk->bone_inverses = calloc((size_t)(n ? n : 1), sizeof *sk->bone_inverses);
  T3_CHECK_ALLOC(sk->bones);
  T3_CHECK_ALLOC(sk->bone_inverses);
  for (int i = 0; i < n; i++) {
    sk->bones[i] = t3_retain(bones[i]);
    if (inverses) sk->bone_inverses[i] = inverses[i];
    else if (bones[i]) t3_mat4_invert(&sk->bone_inverses[i], &bones[i]->matrix_world);
    else t3_mat4_identity(&sk->bone_inverses[i]);
  }
  sk->bone_count = n;
  /* Skeleton.computeBoneTexture: a power-of-two square, at least 4x4 */
  float size = sqrtf((float)n * 4);
  int s2 = 1;
  while (s2 < size) s2 <<= 1;
  if (s2 < 4) s2 = 4;
  sk->bone_texture_size = s2;
  sk->bone_matrices = calloc((size_t)s2 * s2 * 4, sizeof(float));
  T3_CHECK_ALLOC(sk->bone_matrices);
  sk->frame = (unsigned)-1;
  return sk;
}

void t3_skeleton_update(t3_skeleton *sk) {
  for (int i = 0; i < sk->bone_count; i++) {
    t3_mat4 m;
    if (sk->bones[i]) t3_mat4_multiply(&m, &sk->bones[i]->matrix_world, &sk->bone_inverses[i]);
    else m = sk->bone_inverses[i];
    memcpy(sk->bone_matrices + i * 16, m.e, sizeof m.e);
  }
  sk->version++;
}

void t3_skinned_mesh_bind(t3_skinned_mesh *sm, t3_skeleton *sk, const t3_mat4 *bind_matrix) {
  t3_retain(sk);
  t3_release(sm->skeleton);
  sm->skeleton = sk;
  if (!bind_matrix) {
    t3_object_update_matrix_world(sm, true);
    sm->bind_matrix = sm->mesh.base.matrix_world;
  } else {
    sm->bind_matrix = *bind_matrix;
  }
  t3_mat4_invert(&sm->bind_matrix_inverse, &sm->bind_matrix);
}

void t3_mesh_update_morph_targets(t3_mesh *m) {
  int n = m->geometry ? m->geometry->morph_count : 0;
  if (n == m->morph_influence_count) return;
  m->morph_influences = realloc(m->morph_influences, (size_t)(n ? n : 1) * sizeof *m->morph_influences);
  T3_CHECK_ALLOC(m->morph_influences);
  for (int i = m->morph_influence_count; i < n; i++) m->morph_influences[i] = 0;
  m->morph_influence_count = n;
}

t3_object *t3_object_get_by_name(t3_object *o, const char *name) {
  if (o->name && !strcmp(o->name, name)) return o;
  for (int i = 0; i < o->child_count; i++) {
    t3_object *f = t3_object_get_by_name(o->children[i], name);
    if (f) return f;
  }
  return NULL;
}

t3_camera *t3_perspective_camera_new(float fov, float aspect, float near, float far) {
  t3_camera *c = object_alloc(sizeof *c, T3_PERSPECTIVE_CAMERA);
  c->fov = fov; c->aspect = aspect; c->near = near; c->far = far; c->zoom = 1;
  t3_mat4_identity(&c->matrix_world_inverse);
  t3_camera_update_projection_matrix(c);
  return c;
}

t3_camera *t3_orthographic_camera_new(float left, float right, float top, float bottom, float near, float far) {
  t3_camera *c = object_alloc(sizeof *c, T3_ORTHOGRAPHIC_CAMERA);
  c->left = left; c->right = right; c->top = top; c->bottom = bottom;
  c->near = near; c->far = far; c->zoom = 1;
  t3_mat4_identity(&c->matrix_world_inverse);
  t3_camera_update_projection_matrix(c);
  return c;
}

void t3_camera_update_projection_matrix(t3_camera *c) {
  if (c->base.type == T3_PERSPECTIVE_CAMERA) {
    /* (in double, as three.js; one rounding per element) */
    double top = c->near * tan(3.141592653589793 / 180 * 0.5 * c->fov) / c->zoom;
    double height = 2 * top, width = c->aspect * height, left = -0.5 * width;
    double n = c->near, f = c->far, r = left + width, bt = top - height;
    float *te = c->projection_matrix.e;
    memset(te, 0, 64);
    te[0] = (float)(2 * n / (r - left)); te[8] = (float)((r + left) / (r - left));
    te[5] = (float)(2 * n / (top - bt)); te[9] = (float)((top + bt) / (top - bt));
    te[10] = (float)(-(f + n) / (f - n)); te[14] = (float)(-2 * f * n / (f - n)); te[11] = -1;
  } else {
    float dx = (c->right - c->left) / (2 * c->zoom), dy = (c->top - c->bottom) / (2 * c->zoom);
    float cx = (c->right + c->left) / 2, cy = (c->top + c->bottom) / 2;
    t3_mat4_make_orthographic(&c->projection_matrix, cx - dx, cx + dx, cy + dy, cy - dy, c->near, c->far);
  }
  t3_mat4_invert(&c->projection_matrix_inverse, &c->projection_matrix);
}

static t3_light *light_new(t3_object_type type, uint32_t color, float intensity) {
  t3_light *l = object_alloc(sizeof *l, type);
  l->color = t3_color_hex(color);
  l->intensity = intensity;
  return l;
}

static t3_light_shadow *shadow_new(t3_camera *cam) {
  t3_light_shadow *s = calloc(1, sizeof *s);
  T3_CHECK_ALLOC(s);
  s->camera = cam;
  s->map_width = s->map_height = 512;
  s->radius = 1;
  s->focus = 1;
  s->intensity = 1;
  return s;
}

t3_light *t3_ambient_light_new(uint32_t color, float intensity) { return light_new(T3_AMBIENT_LIGHT, color, intensity); }

t3_light *t3_hemisphere_light_new(uint32_t sky, uint32_t ground, float intensity) {
  t3_light *l = light_new(T3_HEMISPHERE_LIGHT, sky, intensity);
  l->ground_color = t3_color_hex(ground);
  l->base.position = t3_v3(0, 1, 0); /* Object3D.DefaultUp */
  t3_object_update_matrix(l);
  return l;
}

t3_light *t3_directional_light_new(uint32_t color, float intensity) {
  t3_light *l = light_new(T3_DIRECTIONAL_LIGHT, color, intensity);
  l->base.position = t3_v3(0, 1, 0);
  t3_object_update_matrix(l);
  l->target = t3_object_new();
  l->shadow = shadow_new(t3_orthographic_camera_new(-5, 5, 5, -5, 0.5f, 500));
  return l;
}

t3_light *t3_point_light_new(uint32_t color, float intensity, float distance, float decay) {
  t3_light *l = light_new(T3_POINT_LIGHT, color, intensity);
  l->distance = distance;
  l->decay = decay;
  l->shadow = shadow_new(t3_perspective_camera_new(90, 1, 0.5f, 500));
  return l;
}

t3_light *t3_spot_light_new(uint32_t color, float intensity, float distance, float angle, float penumbra, float decay) {
  t3_light *l = light_new(T3_SPOT_LIGHT, color, intensity);
  l->distance = distance;
  l->angle = angle;
  l->penumbra = penumbra;
  l->decay = decay;
  l->base.position = t3_v3(0, 1, 0);
  t3_object_update_matrix(l);
  l->target = t3_object_new();
  l->shadow = shadow_new(t3_perspective_camera_new(50, 1, 0.5f, 500));
  return l;
}

static void object_free(t3_object *o) {
  if (o->on_destroy) o->on_destroy(o);
  for (int i = 0; i < o->child_count; i++) {
    o->children[i]->parent = NULL;
    t3_release(o->children[i]);
  }
  free(o->children);
  switch (o->type) {
  case T3_SKINNED_MESH: case T3_INSTANCED_MESH:
  case T3_MESH: case T3_LINE: case T3_LINE_SEGMENTS: case T3_LINE_LOOP: case T3_POINTS: case T3_SPRITE: {
    /* a SkinnedMesh is not an InstancedMesh: each frees only its own fields */
    if (o->type == T3_SKINNED_MESH) t3_release(((t3_skinned_mesh *)o)->skeleton);
    if (o->type == T3_INSTANCED_MESH) {
      t3_instanced_mesh *im = (t3_instanced_mesh *)o;
      if (t3__gl_release && (im->_gl || im->_cull)) t3__gl_release(T3_KIND_OBJECT, im);
      t3_release(im->instance_matrix);
      t3_release(im->instance_color);
    }
    t3_mesh *m = (t3_mesh *)o;
    t3_release(m->geometry);
    for (int i = 0; i < m->material_count; i++) t3_release(m->materials[i]);
    free(m->materials);
    free(m->morph_influences);
    break;
  }
  case T3_AMBIENT_LIGHT: case T3_HEMISPHERE_LIGHT: case T3_DIRECTIONAL_LIGHT:
  case T3_POINT_LIGHT: case T3_SPOT_LIGHT: {
    t3_light *l = (t3_light *)o;
    t3_release(l->target);
    if (l->shadow) {
      if (t3__gl_release && l->shadow->_gl) t3__gl_release(0, l->shadow);
      t3_release(l->shadow->camera);
      free(l->shadow);
    }
    break;
  }
  case T3_SCENE:
    t3_release(((t3_scene *)o)->background_texture);
    t3_release(((t3_scene *)o)->environment);
    break;
  default: break;
  }
  free(o);
}

void t3_object_remove(t3_object *parent, void *child) {
  t3_object *c = child;
  for (int i = 0; i < parent->child_count; i++) {
    if (parent->children[i] != c) continue;
    memmove(parent->children + i, parent->children + i + 1, (parent->child_count - i - 1) * sizeof c);
    parent->child_count--;
    c->parent = NULL;
    c->_world_computed = false;
    c->matrix_world_needs_update = true; /* its world no longer includes the parent */
    t3__world_epoch++;
    t3__graph_epoch++;
    t3_release(c);
    return;
  }
}

void t3_object_add(t3_object *parent, void *child) {
  t3_object *c = child;
  if (c == parent) return;
  t3_retain(c);
  if (c->parent) t3_object_remove(c->parent, c);
  if (parent->child_count == parent->_child_cap) {
    parent->_child_cap = parent->_child_cap ? parent->_child_cap * 2 : 4;
    parent->children = realloc(parent->children, parent->_child_cap * sizeof c);
    T3_CHECK_ALLOC(parent->children);
  }
  parent->children[parent->child_count++] = c;
  c->parent = parent;
  c->_world_computed = false;
  c->matrix_world_needs_update = true; /* its world now includes the parent */
  t3__world_epoch++;
  t3__graph_epoch++;
}

void t3_object_set_position(void *p, float x, float y, float z) { T3_OBJ(p)->position = t3_v3(x, y, z); }

void t3_object_set_rotation(void *p, float x, float y, float z) {
  t3_object *o = p;
  o->rotation.x = x; o->rotation.y = y; o->rotation.z = z;
  o->quaternion = t3_quat_from_euler(o->rotation);
}

void t3_object_rotation_changed(void *p) {
  t3_object *o = p;
  o->quaternion = t3_quat_from_euler(o->rotation);
}

void t3_object_set_quaternion(void *p, t3_quat q) {
  t3_object *o = p;
  o->quaternion = q;
  o->rotation = t3_euler_from_quat(q, o->rotation.order);
}

void t3_object_set_scale(void *p, float x, float y, float z) { T3_OBJ(p)->scale = t3_v3(x, y, z); }
/* layers and the raycast override decide what a raycast's candidate list
 * holds, so a change invalidates it */
void t3_object_set_layers(void *p, uint32_t mask) {
  if (T3_OBJ(p)->layers == mask) return;
  T3_OBJ(p)->layers = mask;
  t3__graph_epoch++;
}
void t3_object_set_raycast_custom(void *p, bool custom) {
  if (T3_OBJ(p)->raycast_custom == custom) return;
  T3_OBJ(p)->raycast_custom = custom;
  t3__graph_epoch++;
}

static bool is_camera_or_light(const t3_object *o) {
  return o->type == T3_PERSPECTIVE_CAMERA || o->type == T3_ORTHOGRAPHIC_CAMERA ||
         o->type == T3_DIRECTIONAL_LIGHT || o->type == T3_SPOT_LIGHT || o->type == T3_POINT_LIGHT ||
         o->type == T3_AMBIENT_LIGHT || o->type == T3_HEMISPHERE_LIGHT;
}

/* Object3D.lookAt */
void t3_object_look_at(void *p, float x, float y, float z) {
  t3_object *o = p;
  t3_vec3 target = t3_v3(x, y, z);
  t3_object_update_world_matrix(o, true, false);
  t3_vec3 pos = t3_mat4_get_position(&o->matrix_world);
  t3_mat4 m;
  t3_mat4_identity(&m);
  if (is_camera_or_light(o)) t3_mat4_look_at(&m, pos, target, o->up);
  else t3_mat4_look_at(&m, target, pos, o->up);
  t3_quat q = t3_quat_from_rotation_matrix(&m);
  if (o->parent) {
    t3_quat pq;
    t3_mat4_decompose(&o->parent->matrix_world, NULL, &pq, NULL);
    pq.x = -pq.x; pq.y = -pq.y; pq.z = -pq.z; /* invert (unit) */
    q = t3_quat_multiply(pq, q);
  }
  t3_object_set_quaternion(o, q);
}

#ifdef T3_F64_MATH
static void round16(float *f, const double *d) { for (int i = 0; i < 16; i++) f[i] = (float)d[i]; }
#endif

void t3_object_update_matrix(void *p) {
  t3_object *o = p;
#ifdef T3_F64_MATH
  double pos[3] = { o->position.x, o->position.y, o->position.z };
  double q[4] = { o->quaternion.x, o->quaternion.y, o->quaternion.z, o->quaternion.w };
  double s[3] = { o->scale.x, o->scale.y, o->scale.z };
  t3_mat4d_compose(o->_md, pos, q, s);
  round16(o->matrix.e, o->_md);
#else
  t3_mat4_compose(&o->matrix, o->position, o->quaternion, o->scale);
#endif
  o->matrix_world_needs_update = true;
}

#ifdef T3_F64_MATH
/* the double local matrix, re-derived when the float one was written
 * directly (it no longer rounds to it) */
static const double *local_d(t3_object *o) {
  for (int i = 0; i < 16; i++)
    if ((float)o->_md[i] != o->matrix.e[i]) {
      for (int k = 0; k < 16; k++) o->_md[k] = o->matrix.e[k];
      break;
    }
  return o->_md;
}
/* matrixWorld in double from the parent's, and its float rounding */
static void world_d(t3_object *o, double out[16]) {
  const double *m = local_d(o);
  if (!o->parent) memcpy(out, m, sizeof o->_md);
  else t3_mat4d_multiply(out, o->parent->_mwd, m);
}
#endif

static void camera_world_changed(t3_object *o) {
  if (o->type == T3_SKINNED_MESH) {
    t3_skinned_mesh *sm = (t3_skinned_mesh *)o;
    if (sm->bind_mode == T3_BIND_ATTACHED) t3_mat4_invert(&sm->bind_matrix_inverse, &o->matrix_world);
    else t3_mat4_invert(&sm->bind_matrix_inverse, &sm->bind_matrix);
    return;
  }
  if (o->type == T3_PERSPECTIVE_CAMERA || o->type == T3_ORTHOGRAPHIC_CAMERA) {
    t3_camera *c = (t3_camera *)o;
#ifdef T3_F64_MATH
    t3_mat4d_invert(c->_mwid, o->_mwd);
    round16(c->matrix_world_inverse.e, c->_mwid);
#else
    t3_mat4_invert(&c->matrix_world_inverse, &o->matrix_world);
#endif
  }
}

static bool mat4_is_identity(const t3_mat4 *m) {
  static const float id[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  return !memcmp(m->e, id, sizeof id);
}

/* Object3D.updateMatrixWorld. A parent whose world matrix is exactly the
 * identity (a scene, an untransformed group) passes its children's local
 * matrices through as their world matrices: identity * M is M bit for bit,
 * so skipping the multiply changes nothing but the cost. */
uint32_t t3__world_epoch = 1, t3__bounds_epoch = 1, t3__graph_epoch = 1;
/* bumped for every world matrix recomputed, cameras included: an embedder
 * keeping copies of world matrices knows from it whether any changed */
uint32_t t3__world_recomputes = 0;
uint32_t t3_world_recompute_count(void) { return t3__world_recomputes; }

const float *t3__world_sphere(t3_object *o, t3_geometry *g) {
  if (!g->has_bounding_sphere) t3_geometry_compute_bounding_sphere(g);
  if (o->_ws_world == o->_world_version && o->_ws_geometry == g && o->_ws_bounds == t3__bounds_epoch && o->_world_version)
    return o->_ws;
  const float *e = o->matrix_world.e;
  const t3_vec3 c = g->bounding_sphere.center;
  o->_ws[0] = e[0] * c.x + e[4] * c.y + e[8] * c.z + e[12];
  o->_ws[1] = e[1] * c.x + e[5] * c.y + e[9] * c.z + e[13];
  o->_ws[2] = e[2] * c.x + e[6] * c.y + e[10] * c.z + e[14];
  o->_ws[3] = g->bounding_sphere.radius * t3_mat4_max_scale_on_axis(&o->matrix_world);
  o->_ws_world = o->_world_version;
  o->_ws_geometry = g;
  o->_ws_bounds = t3__bounds_epoch;
  return o->_ws;
}

/* updateMatrix, skipped when position, quaternion and scale are bitwise what
 * the last compose used: the same inputs make the same matrix, so nothing
 * below needs recomputing unless a parent moved (force). three.js recomposes
 * every object on every updateMatrixWorld; the results are identical. */
static inline uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static void update_matrix_if_changed(t3_object *o) {
  float in[10] = { o->position.x, o->position.y, o->position.z, o->quaternion.x, o->quaternion.y, o->quaternion.z,
                   o->quaternion.w, o->scale.x, o->scale.y, o->scale.z };
  /* bitwise, inline: a libc memcmp call per object cost more than the compare */
  uint32_t diff = 0;
  for (int i = 0; i < 10; i++) diff |= fbits(in[i]) ^ fbits(o->_composed[i]);
  if (o->_composed_valid && !diff) return;
  for (int i = 0; i < 10; i++) o->_composed[i] = in[i];
  o->_composed_valid = true;
  t3_object_update_matrix(o);
}

/* a mesh's world sphere, computed while its new world matrix is in cache
 * (culling and raycasting read it this frame); others wait for first use */
static void world_sphere_now(t3_object *o) {
  switch (o->type) {
  case T3_MESH: case T3_INSTANCED_MESH: case T3_SKINNED_MESH: case T3_LINE: case T3_LINE_SEGMENTS: case T3_LINE_LOOP:
  case T3_POINTS: case T3_SPRITE: {
    t3_geometry *g = ((t3_mesh *)o)->geometry;
    if (g && g->has_bounding_sphere) t3__world_sphere(o, g);
    break;
  }
  default: break;
  }
}

/* the world matrix changed (whoever computed it): versions, epochs, the
 * cached world sphere, a camera's inverse */
static void world_changed(t3_object *o) {
  o->_world_version++;
  t3__world_recomputes++;
  world_sphere_now(o);
  if (o->child_count || (o->type != T3_PERSPECTIVE_CAMERA && o->type != T3_ORTHOGRAPHIC_CAMERA)) t3__world_epoch++;
  camera_world_changed(o);
}

static void set_world_external(t3_object *o) {
  o->matrix_world_external = true;
  o->matrix_world_needs_update = false;
  world_changed(o);
  /* children compute from it on the next update */
  for (int i = 0; i < o->child_count; i++) o->children[i]->matrix_world_needs_update = true;
}

void t3_object_set_matrix_world(void *p, const t3_mat4 *m) {
  t3_object *o = p;
  o->matrix_world = *m;
  for (int i = 0; i < 16; i++) o->_mwd[i] = m->e[i];
  set_world_external(o);
}

void t3_object_set_matrix_world_d(void *p, const double e[16]) {
  t3_object *o = p;
  for (int i = 0; i < 16; i++) { o->_mwd[i] = e[i]; o->matrix_world.e[i] = (float)e[i]; }
  set_world_external(o);
}

static void update_world(t3_object *o, bool force, bool parent_identity) {
  if (o->on_update_matrix_world) o->on_update_matrix_world(o);
  if (o->matrix_world_external) {
    /* the embedder's: its children recompute only if they (or it, through
     * set_matrix_world) changed */
    o->matrix_world_needs_update = false;
    if (!o->child_count) return;
    bool ident = mat4_is_identity(&o->matrix_world);
    for (int i = 0; i < o->child_count; i++) update_world(o->children[i], false, ident);
    return;
  }
  if (o->matrix_auto_update) update_matrix_if_changed(o);
  /* force (three.js recomputes every world matrix below) is skipped where
   * it would compute the same matrix: the local one unchanged
   * (matrix_world_needs_update clear) under an unchanged parent world */
  uint32_t pwv = o->parent ? o->parent->_world_version : 0;
  if (force && !o->matrix_world_needs_update && o->_world_computed && o->_parent_wv == pwv) force = false;
  if (o->matrix_world_needs_update || force) {
    o->_parent_wv = pwv;
    o->_world_computed = true;
#ifdef T3_F64_MATH
    (void)parent_identity;
    world_d(o, o->_mwd);
    round16(o->matrix_world.e, o->_mwd);
#else
    if (!o->parent || parent_identity) o->matrix_world = o->matrix;
    else t3_mat4_multiply(&o->matrix_world, &o->parent->matrix_world, &o->matrix);
#endif
    o->_world_version++;
    t3__world_recomputes++;
    world_sphere_now(o);
    /* a camera with nothing attached moves no mesh: caches of world-space
     * bounds (raycasting, culling) stay valid when only the view changes */
    if (o->child_count || (o->type != T3_PERSPECTIVE_CAMERA && o->type != T3_ORTHOGRAPHIC_CAMERA)) t3__world_epoch++;
    camera_world_changed(o);
    o->matrix_world_needs_update = false;
    force = true;
  }
  if (!o->child_count) return;
#ifdef T3_F64_MATH
  bool ident = false;   /* (the double path multiplies regardless) */
#else
  bool ident = mat4_is_identity(&o->matrix_world);
#endif
  for (int i = 0; i < o->child_count; i++) update_world(o->children[i], force, ident);
}

void t3_object_update_matrix_world(void *p, bool force) {
  t3_object *o = p;
  update_world(o, force, o->parent && mat4_is_identity(&o->parent->matrix_world));
}

void t3_object_set_trs_d(void *p, const double pos[3], const double q[4], const double s[3]) {
  t3_object *o = p;
  o->position = t3_v3((float)pos[0], (float)pos[1], (float)pos[2]);
  o->quaternion.x = (float)q[0]; o->quaternion.y = (float)q[1]; o->quaternion.z = (float)q[2]; o->quaternion.w = (float)q[3];
  o->scale = t3_v3((float)s[0], (float)s[1], (float)s[2]);
  float in[10] = { o->position.x, o->position.y, o->position.z, o->quaternion.x, o->quaternion.y, o->quaternion.z,
                   o->quaternion.w, o->scale.x, o->scale.y, o->scale.z };
  uint32_t diff = 0;
  for (int i = 0; i < 10; i++) diff |= fbits(in[i]) ^ fbits(o->_composed[i]);
  if (o->_composed_valid && !diff) return;
  for (int i = 0; i < 10; i++) o->_composed[i] = in[i];
  o->_composed_valid = true;
#ifdef T3_F64_MATH
  t3_mat4d_compose(o->_md, pos, q, s);
  round16(o->matrix.e, o->_md);
#else
  t3_mat4_compose_d(&o->matrix, pos, q, s);
#endif
  o->matrix_world_needs_update = true;
}

void t3_object_set_matrix_d(void *p, const double e[16]) {
  t3_object *o = p;
  memcpy(o->_md, e, sizeof o->_md);
  for (int i = 0; i < 16; i++) o->matrix.e[i] = (float)e[i];
  o->matrix_world_needs_update = true;
}

void t3_object_update_world_matrix(void *p, bool update_parents, bool update_children) {
  t3_object *o = p;
  if (update_parents && o->parent) t3_object_update_world_matrix(o->parent, true, false);
  if (o->matrix_world_external) {
    if (update_children)
      for (int i = 0; i < o->child_count; i++) t3_object_update_world_matrix(o->children[i], false, true);
    return;
  }
  /* recomposed only when the transform changed (the same matrix either way,
   * and it keeps one set through t3_object_set_trs_d) */
  if (o->matrix_auto_update) update_matrix_if_changed(o);
#ifdef T3_F64_MATH
  /* the local matrix unchanged (flag clear, and its float copy still the
   * rounding of the double one, so a direct write to matrix is seen) under an
   * unchanged parent world: the world matrix it would compute is the one it
   * has (the forced walk's rule) */
  uint32_t pwv = o->parent ? o->parent->_world_version : 0;
  if (!o->matrix_world_needs_update && o->_world_computed && o->_parent_wv == pwv) {
    bool same = true;
    for (int i = 0; i < 16; i++) same &= (float)o->_md[i] == o->matrix.e[i];
    if (same) {
      if (update_children)
        for (int i = 0; i < o->child_count; i++) t3_object_update_world_matrix(o->children[i], false, true);
      return;
    }
  }
  double wd[16];
  world_d(o, wd);
  bool differs = memcmp(wd, o->_mwd, sizeof wd) != 0;
  t3_mat4 w;
  if (differs) { memcpy(o->_mwd, wd, sizeof wd); round16(w.e, wd); }
#else
  t3_mat4 w;
  if (!o->parent) w = o->matrix;
  else t3_mat4_multiply(&w, &o->parent->matrix_world, &o->matrix);
  bool differs = memcmp(&w, &o->matrix_world, sizeof w) != 0;
#endif
  /* the same matrix as before (the usual case: getWorldPosition on a still
   * object) changes nothing that depends on it */
  o->_parent_wv = o->parent ? o->parent->_world_version : 0;
  o->_world_computed = true;
  if (differs) {
    o->matrix_world = w;
    o->_world_version++;
    t3__world_recomputes++;
    if (o->child_count || (o->type != T3_PERSPECTIVE_CAMERA && o->type != T3_ORTHOGRAPHIC_CAMERA)) t3__world_epoch++;
    camera_world_changed(o);
  }
  if (update_children)
    for (int i = 0; i < o->child_count; i++) t3_object_update_world_matrix(o->children[i], false, true);
}

t3_vec3 t3_object_get_world_position(void *p) {
  t3_object *o = p;
  t3_object_update_world_matrix(o, true, false);
  return t3_mat4_get_position(&o->matrix_world);
}

void t3_object_traverse(void *p, void (*fn)(t3_object *, void *), void *ctx) {
  t3_object *o = p;
  fn(o, ctx);
  for (int i = 0; i < o->child_count; i++) t3_object_traverse(o->children[i], fn, ctx);
}

/* ── geometry lifetime (builders live in geometry.c) ─────────────── */
static void geometry_free(t3_geometry *g) {
  if (t3__gl_release && g->_gl) t3__gl_release(T3_KIND_GEOMETRY, g);
  if (t3__wgpu_release && g->_wgpu) t3__wgpu_release(T3_KIND_GEOMETRY, g);
  for (int i = 0; i < T3_ATTR_COUNT; i++) t3_release(g->attributes[i]);
  t3_release(g->index);
  for (int i = 0; i < g->morph_count; i++) {
    if (g->morph_position) t3_release(g->morph_position[i]);
    if (g->morph_normal) t3_release(g->morph_normal[i]);
  }
  free(g->morph_position);
  free(g->morph_normal);
  for (int i = 0; i < g->named_count; i++) t3_release(g->named[i].attribute);
  free(g->named);
  free(g->groups);
  free(g);
}
