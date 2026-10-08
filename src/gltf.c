/* GLTFLoader (three.js examples/jsm/loaders/GLTFLoader.js), over cgltf.
 *
 * Builds what three.js's loader builds, so a glTF renders and animates the
 * same: a Group per scene, Bone / Group / Object3D / Mesh / SkinnedMesh per
 * node with the same unique sanitized names, MeshStandardMaterial (or Basic
 * for KHR_materials_unlit) with the same parameters, cloned per variant
 * (skinning, morph targets, vertex colors, tangents, flat shading),
 * geometry bounds from the accessors' min / max (computeBounds), skins bound
 * with the mesh's then-identity world matrix, and AnimationClips whose
 * tracks address nodes by name.
 *
 * Images are decoded with stb_image; external files are fetched through the
 * caller's read callback. */
#include <math.h>
#include <stdio.h>
#include <string.h>

/* first, so cgltf's and stb_image's malloc / realloc / free are three.c's
 * allocator (t3_set_allocator) */
#include "internal.h"

#define CGLTF_IMPLEMENTATION
#include "../third_party/cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#include "../third_party/stb_image.h"

/* stb_image's inflate for a program table's packed shader text (gen_program.c),
 * under three.c's name: an embedder may build stb_image static */
char *t3__inflate(const void *z, int zlen, int raw, int *outlen) {
  return stbi_zlib_decode_malloc_guesssize_headerflag((const char *)z, zlen, raw, outlen, 1);
}

typedef struct {
  const cgltf_data *d;
  t3_gltf_read_fn read;
  void *ctx;
  /* per glTF index */
  t3_texture **textures;     /* decoded on first use */
  bool *texture_tried;
  t3_material **materials;
  t3_object **nodes;
  bool *is_bone, *mesh_skinned;
  int *mesh_uses;
  t3_object **mesh_objects;  /* first instance of each mesh */
  /* every name handed out, owned by the t3_gltf */
  char **names; int name_n, name_cap;
  t3_material *default_material;
  /* assignFinalMaterial's clone cache: (material, variant bits) -> clone */
  struct { t3_material *src; int bits; t3_material *clone; } *clones;
  int clone_n, clone_cap;
} loader;

/* ── names: createUniqueName (sanitizeNodeName + _N suffix) ───────── */
static const char *keep_name(loader *L, const char *s) {
  if (L->name_n == L->name_cap) {
    L->name_cap = L->name_cap ? L->name_cap * 2 : 64;
    L->names = realloc(L->names, (size_t)L->name_cap * sizeof *L->names);
    T3_CHECK_ALLOC(L->names);
  }
  size_t n = strlen(s) + 1;
  char *c = malloc(n);
  T3_CHECK_ALLOC(c);
  memcpy(c, s, n);
  L->names[L->name_n++] = c;
  return c;
}
static bool name_used(loader *L, const char *s) {
  for (int i = 0; i < L->name_n; i++)
    if (!strcmp(L->names[i], s)) return true;
  return false;
}
static const char *unique_name(loader *L, const char *original) {
  char base[200], cand[220];
  t3_property_binding_sanitize(original ? original : "", base, sizeof base);
  snprintf(cand, sizeof cand, "%s", base);
  for (int i = 1; name_used(L, cand); i++) snprintf(cand, sizeof cand, "%s_%d", base, i);
  return keep_name(L, cand);
}

/* ── accessors ───────────────────────────────────────────────────── */
static t3_attribute *float_attribute(const cgltf_accessor *a) {
  int nc = (int)cgltf_num_components(a->type);
  t3_attribute *at = t3_attribute_new(T3_FLOAT32, NULL, (int)a->count, nc);
  cgltf_accessor_unpack_floats(a, at->array, a->count * nc);
  return at;
}

static t3_attribute *index_attribute(const cgltf_accessor *a) {
  bool u32 = a->component_type == cgltf_component_type_r_32u;
  t3_attribute *at = t3_attribute_new(u32 ? T3_UINT32 : T3_UINT16, NULL, (int)a->count, 1);
  for (cgltf_size i = 0; i < a->count; i++) {
    cgltf_size v = cgltf_accessor_read_index(a, i);
    if (u32) ((uint32_t *)at->array)[i] = (uint32_t)v;
    else ((uint16_t *)at->array)[i] = (uint16_t)v;
  }
  return at;
}

/* getNormalizedComponentScale */
static float normalized_scale(cgltf_component_type t) {
  switch (t) {
  case cgltf_component_type_r_8: return 1.0f / 127;
  case cgltf_component_type_r_8u: return 1.0f / 255;
  case cgltf_component_type_r_16: return 1.0f / 32767;
  case cgltf_component_type_r_16u: return 1.0f / 65535;
  default: return 1;
  }
}

/* computeBounds: the box from POSITION's min / max (grown by the morph
 * targets' largest displacement), the sphere around it */
static void compute_bounds(t3_geometry *g, const cgltf_primitive *pr) {
  const cgltf_accessor *pa = NULL;
  for (cgltf_size i = 0; i < pr->attributes_count; i++)
    if (pr->attributes[i].type == cgltf_attribute_type_position) pa = pr->attributes[i].data;
  if (!pa || !pa->has_min || !pa->has_max) return;
  float sc = pa->normalized ? normalized_scale(pa->component_type) : 1;
  t3_box3 box = { { pa->min[0] * sc, pa->min[1] * sc, pa->min[2] * sc }, { pa->max[0] * sc, pa->max[1] * sc, pa->max[2] * sc } };
  t3_vec3 md = { 0, 0, 0 };
  for (cgltf_size t = 0; t < pr->targets_count; t++)
    for (cgltf_size i = 0; i < pr->targets[t].attributes_count; i++) {
      const cgltf_attribute *at = &pr->targets[t].attributes[i];
      if (at->type != cgltf_attribute_type_position || !at->data->has_min || !at->data->has_max) continue;
      float s2 = at->data->normalized ? normalized_scale(at->data->component_type) : 1;
      float v[3];
      for (int k = 0; k < 3; k++) v[k] = fmaxf(fabsf(at->data->min[k]), fabsf(at->data->max[k])) * s2;
      md.x = fmaxf(md.x, v[0]); md.y = fmaxf(md.y, v[1]); md.z = fmaxf(md.z, v[2]);
    }
  box.min = t3_vec3_sub(box.min, md);
  box.max = t3_vec3_add(box.max, md);
  g->bounding_box = box;
  g->has_bounding_box = true;
  g->bounding_sphere.center = t3_vec3_scale(t3_vec3_add(box.min, box.max), 0.5f);
  g->bounding_sphere.radius = t3_vec3_distance(box.min, box.max) / 2;
  g->has_bounding_sphere = true;
}

/* toTrianglesDrawMode */
static void to_triangles(t3_geometry *g, cgltf_primitive_type mode) {
  int n;
  uint32_t *src;
  if (g->index) {
    n = g->index->count;
    src = malloc((size_t)(n ? n : 1) * sizeof *src);
    T3_CHECK_ALLOC(src);
    for (int i = 0; i < n; i++)
      src[i] = g->index->type == T3_UINT16 ? ((uint16_t *)g->index->array)[i] : ((uint32_t *)g->index->array)[i];
  } else {
    n = g->attributes[T3_ATTR_POSITION]->count;
    src = malloc((size_t)(n ? n : 1) * sizeof *src);
    T3_CHECK_ALLOC(src);
    for (int i = 0; i < n; i++) src[i] = (uint32_t)i;
  }
  int tri = n >= 3 ? n - 2 : 0;
  t3_attribute *ix = t3_attribute_new(T3_UINT32, NULL, tri * 3, 1);
  uint32_t *d = ix->array;
  for (int i = 0; i < tri; i++) {
    if (mode == cgltf_primitive_type_triangle_fan) { d[i * 3] = src[0]; d[i * 3 + 1] = src[i + 1]; d[i * 3 + 2] = src[i + 2]; }
    else if (i % 2 == 0) { d[i * 3] = src[i]; d[i * 3 + 1] = src[i + 1]; d[i * 3 + 2] = src[i + 2]; }
    else { d[i * 3] = src[i + 2]; d[i * 3 + 1] = src[i + 1]; d[i * 3 + 2] = src[i]; }
  }
  free(src);
  t3_geometry_set_index(g, ix);
  t3_release(ix);
}

static t3_geometry *load_geometry(const cgltf_primitive *pr) {
  t3_geometry *g = t3_geometry_new();
  for (cgltf_size i = 0; i < pr->attributes_count; i++) {
    const cgltf_attribute *at = &pr->attributes[i];
    int slot = -1;
    switch (at->type) {
    case cgltf_attribute_type_position: slot = T3_ATTR_POSITION; break;
    case cgltf_attribute_type_normal: slot = T3_ATTR_NORMAL; break;
    case cgltf_attribute_type_tangent: slot = T3_ATTR_TANGENT; break;
    case cgltf_attribute_type_texcoord: slot = at->index == 0 ? T3_ATTR_UV : at->index == 1 ? T3_ATTR_UV1 : -1; break;
    case cgltf_attribute_type_color: slot = at->index == 0 ? T3_ATTR_COLOR : -1; break;
    case cgltf_attribute_type_joints: slot = at->index == 0 ? T3_ATTR_SKIN_INDEX : -1; break;
    case cgltf_attribute_type_weights: slot = at->index == 0 ? T3_ATTR_SKIN_WEIGHT : -1; break;
    default: break;
    }
    if (slot < 0) continue;
    t3_attribute *a = float_attribute(at->data);
    a->normalized = false; /* already dequantized */
    t3_geometry_set_attribute(g, (t3_attr_slot)slot, a);
    t3_release(a);
  }
  if (pr->indices) {
    t3_attribute *ix = index_attribute(pr->indices);
    t3_geometry_set_index(g, ix);
    t3_release(ix);
  }
  compute_bounds(g, pr);
  if (pr->targets_count) {
    bool hp = false, hn = false;
    for (cgltf_size t = 0; t < pr->targets_count; t++)
      for (cgltf_size i = 0; i < pr->targets[t].attributes_count; i++) {
        if (pr->targets[t].attributes[i].type == cgltf_attribute_type_position) hp = true;
        if (pr->targets[t].attributes[i].type == cgltf_attribute_type_normal) hn = true;
      }
    int n = (int)pr->targets_count;
    t3_attribute **ps = hp ? calloc((size_t)n, sizeof *ps) : NULL, **ns = hn ? calloc((size_t)n, sizeof *ns) : NULL;
    for (int t = 0; t < n; t++) {
      for (cgltf_size i = 0; i < pr->targets[t].attributes_count; i++) {
        const cgltf_attribute *at = &pr->targets[t].attributes[i];
        if (at->type == cgltf_attribute_type_position && ps) ps[t] = float_attribute(at->data);
        if (at->type == cgltf_attribute_type_normal && ns) ns[t] = float_attribute(at->data);
      }
      /* a target missing one attribute uses the base attribute, as GLTFLoader */
      if (ps && !ps[t]) ps[t] = t3_retain(g->attributes[T3_ATTR_POSITION]);
      if (ns && !ns[t]) ns[t] = t3_retain(g->attributes[T3_ATTR_NORMAL]);
    }
    if (hp || hn) t3_geometry_set_morph_attributes(g, ps, ns, n, true);
    for (int t = 0; t < n; t++) { if (ps) t3_release(ps[t]); if (ns) t3_release(ns[t]); }
    free(ps);
    free(ns);
  }
  if (pr->type == cgltf_primitive_type_triangle_strip || pr->type == cgltf_primitive_type_triangle_fan)
    to_triangles(g, pr->type);
  return g;
}

/* ── textures ────────────────────────────────────────────────────── */
static t3_filter gl_to_filter(cgltf_int f, t3_filter def) {
  switch (f) {
  case 9728: return T3_NEAREST;
  case 9729: return T3_LINEAR;
  case 9984: return T3_NEAREST_MIPMAP_NEAREST;
  case 9985: return T3_LINEAR_MIPMAP_NEAREST;
  case 9986: return T3_NEAREST_MIPMAP_LINEAR;
  case 9987: return T3_LINEAR_MIPMAP_LINEAR;
  default: return def;
  }
}
static t3_wrapping gl_to_wrap(cgltf_int w) {
  return w == 33071 ? T3_CLAMP_TO_EDGE : w == 33648 ? T3_MIRRORED_REPEAT : T3_REPEAT;
}

static t3_texture *load_texture(loader *L, const cgltf_texture *tx) {
  if (!tx || !tx->image) return NULL;
  cgltf_size ti = cgltf_texture_index(L->d, tx);
  if (L->texture_tried[ti]) return L->textures[ti];
  L->texture_tried[ti] = true;
  const cgltf_image *im = tx->image;
  const uint8_t *bytes = NULL;
  size_t len = 0;
  void *owned = NULL;
  if (im->buffer_view) {
    bytes = cgltf_buffer_view_data(im->buffer_view);
    len = im->buffer_view->size;
  } else if (im->uri && !strncmp(im->uri, "data:", 5)) {
    const char *comma = strchr(im->uri, ',');
    if (comma) {
      size_t b64 = strlen(comma + 1), out = b64 * 3 / 4;
      cgltf_options o = { 0 };
      while (b64 && comma[b64] == '=') { b64--; out--; }
      if (cgltf_load_buffer_base64(&o, out, comma + 1, &owned) == cgltf_result_success) { bytes = owned; len = out; }
    }
  } else if (im->uri && L->read) {
    owned = L->read(L->ctx, im->uri, &len);
    bytes = owned;
  }
  if (!bytes) return NULL;
  int w, h, c;
  uint8_t *px = stbi_load_from_memory(bytes, (int)len, &w, &h, &c, 4);
  free(owned);
  if (!px) return NULL;
  t3_texture *t = t3_texture_new(w, h, px);
  stbi_image_free(px);
  t->flip_y = false; /* texture.flipY = false */
  const cgltf_sampler *s = tx->sampler;
  t->mag_filter = s ? gl_to_filter(s->mag_filter, T3_LINEAR) : T3_LINEAR;
  t->min_filter = s ? gl_to_filter(s->min_filter, T3_LINEAR_MIPMAP_LINEAR) : T3_LINEAR_MIPMAP_LINEAR;
  t->wrap_s = s ? gl_to_wrap(s->wrap_s) : T3_REPEAT;
  t->wrap_t = s ? gl_to_wrap(s->wrap_t) : T3_REPEAT;
  L->textures[ti] = t;
  return t;
}

/* ── materials ───────────────────────────────────────────────────── */
static t3_material *load_material(loader *L, const cgltf_material *md) {
  cgltf_size mi = cgltf_material_index(L->d, md);
  if (L->materials[mi]) return L->materials[mi];
  t3_material *m;
  if (md->unlit) {
    m = t3_material_new(T3_MESH_BASIC_MATERIAL);
  } else {
    m = t3_material_new(T3_MESH_STANDARD_MATERIAL);
    m->metalness = 1;
    m->roughness = 1;
  }
  m->color = t3_color_hex(0xffffff);
  m->opacity = 1;
  if (md->has_pbr_metallic_roughness) {
    const cgltf_pbr_metallic_roughness *pbr = &md->pbr_metallic_roughness;
    m->color.r = pbr->base_color_factor[0];
    m->color.g = pbr->base_color_factor[1];
    m->color.b = pbr->base_color_factor[2];
    m->opacity = pbr->base_color_factor[3];
    if (pbr->base_color_texture.texture) t3_material_set_texture(m, T3_MAP, load_texture(L, pbr->base_color_texture.texture));
    if (!md->unlit) {
      m->metalness = pbr->metallic_factor;
      m->roughness = pbr->roughness_factor;
      if (pbr->metallic_roughness_texture.texture) {
        t3_texture *t = load_texture(L, pbr->metallic_roughness_texture.texture);
        t3_material_set_texture(m, T3_METALNESS_MAP, t);
        t3_material_set_texture(m, T3_ROUGHNESS_MAP, t);
      }
    }
  }
  if (md->double_sided) m->side = T3_DOUBLE_SIDE;
  if (md->alpha_mode == cgltf_alpha_mode_blend) {
    m->transparent = true;
    m->depth_write = false;
  } else if (md->alpha_mode == cgltf_alpha_mode_mask) {
    m->alpha_test = md->alpha_cutoff;
  }
  if (!md->unlit) {
    if (md->normal_texture.texture) {
      t3_material_set_texture(m, T3_NORMAL_MAP, load_texture(L, md->normal_texture.texture));
      float sc = md->normal_texture.scale;
      m->normal_scale.x = m->normal_scale.y = sc;
    }
    if (md->occlusion_texture.texture) {
      t3_material_set_texture(m, T3_AO_MAP, load_texture(L, md->occlusion_texture.texture));
      m->ao_map_intensity = md->occlusion_texture.scale;
    }
    m->emissive.r = md->emissive_factor[0];
    m->emissive.g = md->emissive_factor[1];
    m->emissive.b = md->emissive_factor[2];
    if (md->emissive_texture.texture) t3_material_set_texture(m, T3_EMISSIVE_MAP, load_texture(L, md->emissive_texture.texture));
  }
  /* baseColorTexture and emissiveTexture are sRGB */
  if (m->map) m->map->color_space = T3_SRGB_COLOR_SPACE;
  if (m->emissive_map) m->emissive_map->color_space = T3_SRGB_COLOR_SPACE;
  L->materials[mi] = m;
  return m;
}

/* assignFinalMaterial: a clone per variant the mesh needs */
static t3_material *final_material(loader *L, t3_mesh *mesh, t3_material *m) {
  t3_geometry *g = mesh->geometry;
  /* r186 GLTFLoader.assignFinalMaterial: derivative tangents (a normal map
   * without a tangent attribute flips normalScale.y), vertex colours, flat
   * shading (no normals) */
  bool derivative = m->normal_map && !g->attributes[T3_ATTR_TANGENT], colors = g->attributes[T3_ATTR_COLOR] != NULL;
  bool flat = g->attributes[T3_ATTR_NORMAL] == NULL;
  int bits = derivative | colors << 1 | flat << 2;
  if (!bits) return m;
  for (int i = 0; i < L->clone_n; i++)
    if (L->clones[i].src == m && L->clones[i].bits == bits) return L->clones[i].clone;
  t3_material *c = t3_material_clone(m);
  if (colors) c->vertex_colors = true;
  if (flat) c->flat_shading = true;
  if (derivative) c->normal_scale.y *= -1;
  if (L->clone_n == L->clone_cap) {
    L->clone_cap = L->clone_cap ? L->clone_cap * 2 : 16;
    L->clones = realloc(L->clones, (size_t)L->clone_cap * sizeof *L->clones);
    T3_CHECK_ALLOC(L->clones);
  }
  L->clones[L->clone_n].src = m;
  L->clones[L->clone_n].bits = bits;
  L->clones[L->clone_n].clone = c;
  L->clone_n++;
  return c;
}

/* SkinnedMesh.normalizeSkinWeights */
static void normalize_skin_weights(t3_geometry *g) {
  t3_attribute *w = g->attributes[T3_ATTR_SKIN_WEIGHT];
  if (!w || w->item_size != 4) return;
  float *p = w->array;
  for (int i = 0; i < w->count; i++, p += 4) {
    float s = fabsf(p[0]) + fabsf(p[1]) + fabsf(p[2]) + fabsf(p[3]);
    if (s != 0) { float k = 1 / s; p[0] *= k; p[1] *= k; p[2] *= k; p[3] *= k; }
    else { p[0] = 1; p[1] = p[2] = p[3] = 0; }
  }
  w->version++;
}

/* loadMesh: one Mesh per primitive (SkinnedMesh when a skinned node uses the
 * mesh); several primitives make a Group */
static t3_object *load_mesh(loader *L, const cgltf_mesh *md, int mesh_index) {
  t3_object *meshes[64];
  int n = 0;
  for (cgltf_size i = 0; i < md->primitives_count && n < 64; i++) {
    const cgltf_primitive *pr = &md->primitives[i];
    if (pr->type != cgltf_primitive_type_triangles && pr->type != cgltf_primitive_type_triangle_strip &&
        pr->type != cgltf_primitive_type_triangle_fan) continue; /* points / lines: not yet */
    t3_geometry *g = load_geometry(pr);
    t3_material *mat = pr->material ? load_material(L, pr->material) : L->default_material;
    t3_mesh *mesh;
    if (L->mesh_skinned[mesh_index]) {
      t3_skinned_mesh *sm = t3_skinned_mesh_new(g, mat);
      /* weights not normalized by the accessor: normalizeSkinWeights */
      for (cgltf_size a = 0; a < pr->attributes_count; a++)
        if (pr->attributes[a].type == cgltf_attribute_type_weights && pr->attributes[a].index == 0 &&
            !pr->attributes[a].data->normalized) normalize_skin_weights(g);
      mesh = &sm->mesh;
    } else {
      mesh = t3_mesh_new(g, mat);
    }
    t3_release(g);
    if (g->morph_count) {
      t3_mesh_update_morph_targets(mesh);
      for (cgltf_size w = 0; w < md->weights_count && (int)w < mesh->morph_influence_count; w++)
        mesh->morph_influences[w] = md->weights[w];
    }
    char def[32];
    snprintf(def, sizeof def, "mesh_%d", mesh_index);
    mesh->base.name = unique_name(L, md->name ? md->name : def);
    t3_material *fm = final_material(L, mesh, mat);
    if (fm != mat) {
      t3_retain(fm);
      t3_release(mesh->materials[0]);
      mesh->materials[0] = fm;
    }
    meshes[n++] = &mesh->base;
  }
  if (n == 1) return meshes[0];
  t3_object *grp = t3_group_new();
  for (int i = 0; i < n; i++) {
    t3_object_add(grp, meshes[i]);
    t3_release(meshes[i]);
  }
  return grp;
}

/* Object3D.clone for a mesh object (and its child meshes): same geometry
 * and materials (_getNodeRef for a mesh used by several nodes) */
static t3_object *clone_mesh_object(loader *L, t3_object *src) {
  t3_object *o;
  if (src->type == T3_MESH || src->type == T3_SKINNED_MESH) {
    t3_mesh *sm = (t3_mesh *)src;
    t3_mesh *m;
    if (src->type == T3_SKINNED_MESH) m = &t3_skinned_mesh_new(sm->geometry, sm->materials[0])->mesh;
    else m = t3_mesh_new(sm->geometry, sm->materials[0]);
    if (sm->morph_influence_count) {
      t3_mesh_update_morph_targets(m);
      memcpy(m->morph_influences, sm->morph_influences, (size_t)sm->morph_influence_count * sizeof(float));
    }
    o = &m->base;
  } else {
    o = t3_group_new();
  }
  char nm[220];
  snprintf(nm, sizeof nm, "%s", src->name ? src->name : "");
  o->name = unique_name(L, nm);
  o->position = src->position; o->quaternion = src->quaternion; o->rotation = src->rotation; o->scale = src->scale;
  for (int i = 0; i < src->child_count; i++) {
    t3_object *c = clone_mesh_object(L, src->children[i]);
    t3_object_add(o, c);
    t3_release(c);
  }
  return o;
}

/* loadNode */
static t3_object *load_node(loader *L, const cgltf_node *nd) {
  cgltf_size ni = cgltf_node_index(L->d, nd);
  if (L->nodes[ni]) return L->nodes[ni];
  const char *node_name = nd->name ? unique_name(L, nd->name) : NULL;
  t3_object *mesh_obj = NULL;
  if (nd->mesh) {
    cgltf_size mi = cgltf_mesh_index(L->d, nd->mesh);
    if (L->mesh_uses[mi]++ == 0) {
      mesh_obj = load_mesh(L, nd->mesh, (int)mi);
      L->mesh_objects[mi] = mesh_obj;
    } else {
      mesh_obj = clone_mesh_object(L, L->mesh_objects[mi]);
    }
    if (nd->weights_count)
      for (int c = -1; c < mesh_obj->child_count; c++) {
        t3_object *o = c < 0 ? mesh_obj : mesh_obj->children[c];
        if (o->type != T3_MESH && o->type != T3_SKINNED_MESH) continue;
        t3_mesh *m = (t3_mesh *)o;
        for (cgltf_size w = 0; w < nd->weights_count && (int)w < m->morph_influence_count; w++)
          m->morph_influences[w] = nd->weights[w];
      }
  }
  t3_object *node;
  if (L->is_bone[ni]) {
    node = t3_bone_new();
    if (mesh_obj) { t3_object_add(node, mesh_obj); t3_release(mesh_obj); }
  } else if (mesh_obj) {
    node = mesh_obj;
  } else {
    node = t3_object_new();
  }
  if (nd->name) node->name = node_name;
  if (nd->has_matrix) {
    t3_mat4 m;
    memcpy(m.e, nd->matrix, sizeof m.e);
    t3_vec3 p, s;
    t3_quat q;
    t3_mat4_decompose(&m, &p, &q, &s);
    node->position = p;
    node->scale = s;
    t3_object_set_quaternion(node, q);
  } else {
    if (nd->has_translation) node->position = t3_v3(nd->translation[0], nd->translation[1], nd->translation[2]);
    if (nd->has_rotation) {
      t3_quat q = { nd->rotation[0], nd->rotation[1], nd->rotation[2], nd->rotation[3] };
      t3_object_set_quaternion(node, q);
    }
    if (nd->has_scale) node->scale = t3_v3(nd->scale[0], nd->scale[1], nd->scale[2]);
  }
  L->nodes[ni] = node;
  return node;
}

/* buildNodeHierachy: bind skins before the node joins its parent (so the
 * mesh's world matrix, the bind matrix, is still the identity) */
static void build_hierarchy(loader *L, const cgltf_node *nd, t3_object *parent) {
  t3_object *node = load_node(L, nd);
  if (nd->skin) {
    const cgltf_skin *sk = nd->skin;
    int n = (int)sk->joints_count;
    t3_object **bones = calloc((size_t)(n ? n : 1), sizeof *bones);
    t3_mat4 *inv = calloc((size_t)(n ? n : 1), sizeof *inv);
    T3_CHECK_ALLOC(bones);
    T3_CHECK_ALLOC(inv);
    for (int j = 0; j < n; j++) {
      bones[j] = load_node(L, sk->joints[j]);
      t3_mat4_identity(&inv[j]);
      if (sk->inverse_bind_matrices) cgltf_accessor_read_float(sk->inverse_bind_matrices, (cgltf_size)j, inv[j].e, 16);
    }
    for (int c = -1; c < node->child_count; c++) {
      t3_object *o = c < 0 ? node : node->children[c];
      if (o->type != T3_SKINNED_MESH) continue;
      t3_skeleton *s = t3_skeleton_new(bones, n, inv);
      t3_skinned_mesh_bind((t3_skinned_mesh *)o, s, &o->matrix_world);
      t3_release(s);
    }
    free(bones);
    free(inv);
  }
  t3_object_add(parent, node);
  for (cgltf_size i = 0; i < nd->children_count; i++) build_hierarchy(L, nd->children[i], node);
}

/* ── animations ──────────────────────────────────────────────────── */
static void collect_morph_meshes(t3_object *o, t3_object **out, int *n, int cap) {
  if ((o->type == T3_MESH || o->type == T3_SKINNED_MESH) && ((t3_mesh *)o)->morph_influence_count && *n < cap)
    out[(*n)++] = o;
  for (int i = 0; i < o->child_count; i++) collect_morph_meshes(o->children[i], out, n, cap);
}

static t3_animation_clip *load_animation(loader *L, const cgltf_animation *an, int index) {
  int cap = (int)an->channels_count * 4 + 4, nt = 0;
  t3_keyframe_track **tracks = calloc((size_t)cap, sizeof *tracks);
  T3_CHECK_ALLOC(tracks);
  for (cgltf_size c = 0; c < an->channels_count; c++) {
    const cgltf_animation_channel *ch = &an->channels[c];
    if (!ch->target_node || !ch->sampler) continue;
    t3_object *node = L->nodes[cgltf_node_index(L->d, ch->target_node)];
    if (!node) continue;
    const cgltf_animation_sampler *sp = ch->sampler;
    int nk = (int)sp->input->count;
    float *times = malloc((size_t)(nk ? nk : 1) * sizeof *times);
    cgltf_accessor_unpack_floats(sp->input, times, (cgltf_size)nk);
    int nc = (int)cgltf_num_components(sp->output->type);
    int nv = (int)sp->output->count * nc;
    float *vals = malloc((size_t)(nv ? nv : 1) * sizeof *vals);
    cgltf_accessor_unpack_floats(sp->output, vals, (cgltf_size)nv);
    t3_interpolation interp = sp->interpolation == cgltf_interpolation_type_step ? T3_INTERPOLATE_DISCRETE
                            : sp->interpolation == cgltf_interpolation_type_cubic_spline ? T3_INTERPOLATE_CUBIC_SPLINE_GLTF
                            : T3_INTERPOLATE_LINEAR;
    int per_key = (interp == T3_INTERPOLATE_CUBIC_SPLINE_GLTF ? nv / 3 : nv) / (nk ? nk : 1);
    const char *path = NULL;
    t3_track_type tt = T3_TRACK_VECTOR;
    switch (ch->target_path) {
    case cgltf_animation_path_type_translation: path = "position"; break;
    case cgltf_animation_path_type_rotation: path = "quaternion"; tt = T3_TRACK_QUATERNION; break;
    case cgltf_animation_path_type_scale: path = "scale"; break;
    case cgltf_animation_path_type_weights: path = "morphTargetInfluences"; tt = T3_TRACK_NUMBER; break;
    default: break;
    }
    if (path) {
      t3_object *targets[32];
      int ntg = 0;
      if (ch->target_path == cgltf_animation_path_type_weights) collect_morph_meshes(node, targets, &ntg, 32);
      else targets[ntg++] = node;
      for (int k = 0; k < ntg && nt < cap; k++) {
        char nm[256];
        snprintf(nm, sizeof nm, "%s.%s", targets[k]->name ? targets[k]->name : "", path);
        tracks[nt++] = t3_keyframe_track_new_ex(tt, nm, times, nk, vals, per_key, interp);
      }
    }
    free(times);
    free(vals);
  }
  char def[32];
  snprintf(def, sizeof def, "animation_%d", index);
  t3_animation_clip *clip = t3_animation_clip_new(an->name ? an->name : def, -1, tracks, nt);
  free(tracks);
  return clip;
}

/* objects need a name to be animated by name: three.js falls back to the
 * uuid; give every unnamed node a unique one */
static void name_unnamed(loader *L, t3_object *o) {
  if (!o->name || !*o->name) {
    char nm[48];
    snprintf(nm, sizeof nm, "t3_node_%u", o->id);
    o->name = keep_name(L, nm);
  }
  for (int i = 0; i < o->child_count; i++) name_unnamed(L, o->children[i]);
}

static void gltf_destroy(void *p) {
  t3_gltf *g = p;
  t3_release(g->scene);
  for (int i = 0; i < g->animation_count; i++) t3_release(g->animations[i]);
  free(g->animations);
  for (int i = 0; i < g->name_count; i++) free(g->names[i]);
  free(g->names);
}

t3_gltf *t3_gltf_parse(const void *data, size_t len, t3_gltf_read_fn read, void *ctx) {
  cgltf_options opt = { 0 };
  cgltf_data *d = NULL;
  if (cgltf_parse(&opt, data, len, &d) != cgltf_result_success) return NULL;
  /* buffers: GLB's own, data URIs, or the caller's files */
  for (cgltf_size i = 0; i < d->buffers_count; i++) {
    cgltf_buffer *b = &d->buffers[i];
    if (b->data) continue;
    if (!b->uri && d->bin) { b->data = (void *)d->bin; b->data_free_method = cgltf_data_free_method_none; continue; }
    if (b->uri && !strncmp(b->uri, "data:", 5)) {
      const char *comma = strchr(b->uri, ',');
      if (comma && cgltf_load_buffer_base64(&opt, b->size, comma + 1, &b->data) == cgltf_result_success)
        b->data_free_method = cgltf_data_free_method_memory_free;
    } else if (b->uri && read) {
      size_t n = 0;
      b->data = read(ctx, b->uri, &n);
      b->data_free_method = cgltf_data_free_method_memory_free;
    }
    if (!b->data) { cgltf_free(d); return NULL; }
  }
  loader L = { 0 };
  L.d = d;
  L.read = read;
  L.ctx = ctx;
  L.textures = calloc(d->textures_count + 1, sizeof *L.textures);
  L.texture_tried = calloc(d->textures_count + 1, sizeof *L.texture_tried);
  L.materials = calloc(d->materials_count + 1, sizeof *L.materials);
  L.nodes = calloc(d->nodes_count + 1, sizeof *L.nodes);
  L.is_bone = calloc(d->nodes_count + 1, sizeof *L.is_bone);
  L.mesh_skinned = calloc(d->meshes_count + 1, sizeof *L.mesh_skinned);
  L.mesh_uses = calloc(d->meshes_count + 1, sizeof *L.mesh_uses);
  L.mesh_objects = calloc(d->meshes_count + 1, sizeof *L.mesh_objects);
  /* createDefaultMaterial */
  L.default_material = t3_mesh_standard_material_new(0xffffff);
  L.default_material->metalness = 1;
  L.default_material->roughness = 1;
  /* _markDefs: joints are bones; a mesh a skinned node uses is skinned */
  for (cgltf_size i = 0; i < d->skins_count; i++)
    for (cgltf_size j = 0; j < d->skins[i].joints_count; j++) L.is_bone[cgltf_node_index(d, d->skins[i].joints[j])] = true;
  for (cgltf_size i = 0; i < d->nodes_count; i++)
    if (d->nodes[i].mesh && d->nodes[i].skin) L.mesh_skinned[cgltf_mesh_index(d, d->nodes[i].mesh)] = true;

  t3_gltf *g = t3__alloc(sizeof *g, T3_KIND_MISC);
  g->_destroy = gltf_destroy;
  g->scene = t3_group_new();
  const cgltf_scene *sc = d->scene ? d->scene : d->scenes_count ? &d->scenes[0] : NULL;
  if (sc) {
    if (sc->name) g->scene->name = unique_name(&L, sc->name);
    for (cgltf_size i = 0; i < sc->nodes_count; i++) build_hierarchy(&L, sc->nodes[i], g->scene);
  }
  /* nodes outside the scene but referenced (e.g. joints) still load above */
  name_unnamed(&L, g->scene);
  g->animation_count = (int)d->animations_count;
  g->animations = calloc((size_t)(g->animation_count ? g->animation_count : 1), sizeof *g->animations);
  for (int i = 0; i < g->animation_count; i++) g->animations[i] = load_animation(&L, &d->animations[i], i);

  /* the scene graph holds what it uses; drop the loader's references */
  for (cgltf_size i = 0; i < d->textures_count; i++) t3_release(L.textures[i]);
  for (cgltf_size i = 0; i < d->materials_count; i++) t3_release(L.materials[i]);
  for (int i = 0; i < L.clone_n; i++) t3_release(L.clones[i].clone);
  t3_release(L.default_material);
  /* placed nodes are held by their parents (bones also by skeletons) */
  for (cgltf_size i = 0; i < d->nodes_count; i++) t3_release(L.nodes[i]);
  g->names = L.names;
  g->name_count = L.name_n;
  free(L.textures); free(L.texture_tried); free(L.materials); free(L.nodes); free(L.is_bone);
  free(L.mesh_skinned); free(L.mesh_uses); free(L.mesh_objects); free(L.clones);
  cgltf_free(d);
  return g;
}

/* ── TextureLoader: an image file's bytes into a Texture ─────────── */
t3_texture *t3_texture_load_memory(const void *data, size_t len) {
  int w, h, c;
  uint8_t *px = stbi_load_from_memory(data, (int)len, &w, &h, &c, 4);
  if (!px) return NULL;
  t3_texture *t = t3_texture_new(w, h, px);
  stbi_image_free(px);
  return t;
}

t3_texture *t3_texture_load(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  void *buf = malloc(n > 0 ? (size_t)n : 1);
  t3_texture *t = NULL;
  if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) t = t3_texture_load_memory(buf, (size_t)n);
  free(buf);
  fclose(f);
  return t;
}
