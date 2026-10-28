/* r186's generated programs, inside the renderer (included by renderer.c
 * ahead of draw_item: it uses the renderer's statics).
 *
 * A material is mapped to a table state (kind + features + the frame's light
 * counts), that state's program is built on first use, and each frame / draw
 * its uniform groups are filled through the manifest's op list
 * (gen_program.c). An item the table does not cover (or whose program has a
 * property with no op) is skipped: the state is recorded for
 * t3_renderer_generated_missing and the reason in the error text once. */

static void gen_draw_gl(t3_renderer *r, const render_item *it, const t3_camera *cam, struct t3_gen_gl *gl, const t3_material *state);
static void gen_set_aux(t3_renderer *r, const t3_material *m);
static void gen_background(t3_renderer *r, t3_scene *scene, const t3_camera *cam);
#ifdef T3_WGPU
static void wgpu_draw_prog(t3_renderer *r, const render_item *it, const t3_camera *cam, struct t3_gen_gl *gl, const t3_material *state);
#endif
static bool gen_note(t3_renderer *r, const char *what) {
  if (!r->has_error) {
    snprintf(r->error, sizeof r->error, "generated path: %.2000s", what);
    r->has_error = true;
  }
  return false;
}

/* a state's name in the generator's syntax (kind+features+lDPSH+sDPS) */
static void gen_state_name(char *name, size_t cap, t3_gen_kind kind, uint64_t f, const uint8_t want[4], const uint8_t *cast, uint16_t ext) {
  static const char *const kinds[] = { "basic", "lambert", "phong", "standard", "physical", "depth", "points", "psprite", "line", "sprite", "normal" };
  static const char *const feats[] = T3_GEN_FEATURE_NAMES;
  int n = snprintf(name, cap, "%s", kind < (int)(sizeof kinds / sizeof kinds[0]) ? kinds[kind] : "?");
  for (int b = 0; b < (int)(sizeof feats / sizeof feats[0]) && n < (int)cap - 16; b++)
    if (f & (1ull << b)) n += snprintf(name + n, cap - (size_t)n, "+%s", feats[b]);
  if (kind != T3_GEN_BASIC && kind != T3_GEN_NORMAL && kind <= T3_GEN_PHYSICAL)
    n += snprintf(name + n, cap - (size_t)n, "+l%d%d%d%d", want[0], want[1], want[2], want[3]);
  if (cast && (cast[0] != want[0] || cast[1] != want[1] || cast[2] != want[2]))
    n += snprintf(name + n, cap - (size_t)n, "+s%d%d%d", cast[0], cast[1], cast[2]);
  if (ext && n < (int)cap) snprintf(name + n, cap - (size_t)n, "+x%s", t3__extension_name(ext));
}
static bool gen_missing_has(const t3_renderer *r, const char *name) {
  size_t len = strlen(name);
  for (const char *p = r->gen_missing; p && (p = strstr(p, name)); p += len)
    if ((p == r->gen_missing || p[-1] == '\n') && (p[len] == '\n' || !p[len])) return true;
  return false;
}
/* a state the tables lack, remembered once: t3_renderer_generated_missing
 * lists them so a project can add them to its table (with the runtime
 * builder, the states it built, which a project table would save building) */
static void gen_record_missing_name(const t3_renderer *rc, const char *name) {
  t3_renderer *r = (t3_renderer *)rc;
  if (gen_missing_has(r, name)) return;
  size_t len = strlen(name);
  size_t old = r->gen_missing ? strlen(r->gen_missing) : 0;
  r->gen_missing = realloc(r->gen_missing, old + len + 2);
  T3_CHECK_ALLOC(r->gen_missing);
  memcpy(r->gen_missing + old, name, len);
  r->gen_missing[old + len] = '\n';
  r->gen_missing[old + len + 1] = 0;
#ifndef T3_WASMCART
  /* T3_MISSING_LOG=<file>: each missing state appended, once per renderer, for
   * tools/gen-project-table.mjs --missing <file> */
  const char *log = getenv("T3_MISSING_LOG");
  FILE *lf = log && *log ? fopen(log, "a") : NULL;
  if (lf) { fprintf(lf, "%s\n", name); fclose(lf); }
#endif
}
static void gen_record_missing(const t3_renderer *r, t3_gen_kind kind, uint64_t f, const uint8_t want[4], const uint8_t *cast) {
  char name[256];
  gen_state_name(name, sizeof name, kind, f, want, cast, 0);
  gen_record_missing_name(r, name);
}

/* the tables of the renderer's backend: the registered project tables, then
 * three.c's own (t3_gen_tables, gen_program.c) */
static int gen_tbls(const t3_renderer *r, const t3_gen_table *const **tables) {
#ifdef T3_WGPU
  if (r->wgpu) return t3_gen_tables(T3_GEN_WGPU, tables);
#endif
  (void)r;
  return t3_gen_tables(T3_GEN_GL, tables);
}
static const t3_gen_program *gen_by_name(const t3_renderer *r, const char *name) {
  const t3_gen_table *const *tables;
  int nt = gen_tbls(r, &tables);
  for (int t = 0; t < nt; t++)
    for (unsigned i = 0; i < tables[t]->count; i++)
      if (!strcmp(tables[t]->programs[i].state, name)) return &tables[t]->programs[i];
  return NULL;
}

/* WGSLNodeBuilder.isUnfilterable: a texture with both filters Nearest is read
 * with textureLoad and the wrap modes baked into the shader (GLSL is the same
 * either way) */
static bool tex_nearest(const t3_texture *t) { return t->mag_filter == T3_NEAREST && t->min_filter == T3_NEAREST; }

/* What picks an item's program: the table state (kind, features, the
 * frame's light vector and shadow casters) and what the object puts in the
 * program's template (bones, morph targets), with the output encoding. Equal
 * keys pick the same program, so a renderer looks programs up by key
 * (gen_pick) and searches the tables only for a key it has not seen. */
typedef struct {
  uint64_t f;
  uint8_t kind, shadow, enc, cast[3], want[4];
  uint16_t bones, morphs, morph_width;
  uint16_t ext;   /* the material's extension (t3__extension_id), 0 = none */
} gen_key;
struct gen_slot { gen_key key; t3_gen_gl *gl; bool used; };

/* the table state for an item: false (with *why set) when no state fits */
static bool gen_state_key(const t3_renderer *r, const t3_material *m, const t3_object *o, const t3_geometry *g,
                          bool batched, const char **why, gen_key *key) {
  t3_gen_kind kind;
  switch (m->type) {
  case T3_MESH_BASIC_MATERIAL: kind = T3_GEN_BASIC; break;
  case T3_MESH_LAMBERT_MATERIAL: kind = T3_GEN_LAMBERT; break;
  case T3_MESH_PHONG_MATERIAL: kind = T3_GEN_PHONG; break;
  case T3_MESH_STANDARD_MATERIAL: kind = T3_GEN_STANDARD; break;
  case T3_MESH_PHYSICAL_MATERIAL: kind = T3_GEN_PHYSICAL; break;
  case T3_MESH_NORMAL_MATERIAL: kind = T3_GEN_NORMAL; break;
  case T3_POINTS_MATERIAL: kind = T3_GEN_POINTS; break;      /* r186 draws a Points object as 1-pixel GL points */
  case T3_LINE_BASIC_MATERIAL: kind = T3_GEN_LINE; break;
  case T3_SPRITE_MATERIAL: kind = T3_GEN_SPRITE; break;
  default: *why = "material kind"; return false;
  }
  bool lineish = o->type == T3_LINE || o->type == T3_LINE_SEGMENTS || o->type == T3_LINE_LOOP;
  if (kind == T3_GEN_POINTS ? o->type != T3_POINTS : kind == T3_GEN_LINE ? !lineish : kind == T3_GEN_SPRITE ? o->type != T3_SPRITE
      : (o->type != T3_MESH && o->type != T3_INSTANCED_MESH && o->type != T3_SKINNED_MESH)) { *why = "object kind"; return false; }
  if (g->morph_count && !g->morph_position) { *why = "morph targets without positions"; return false; }
  if (o->type == T3_SKINNED_MESH) {
    const t3_skeleton *sk = ((const t3_skinned_mesh *)o)->skeleton;
    if (!sk || sk->bone_count < 1 || sk->bone_count > 256) { *why = "skeleton missing or over 256 bones (r186's bone-texture path)"; return false; }
  }
  /* (material / renderer clippingPlanes: r186's node renderer ignores them, its
   * clipping is ClippingGroup; the generated path does the same) */
  uint64_t f = 0;
  /* the environment: Standard / Physical through a PMREM (r186 EnvironmentNode:
   * a cube, an equirect or a PMREM texture; scene.environment when the
   * material has none); Basic / Lambert / Phong a CubeTexture sampled along
   * the reflection or refraction vector (BasicEnvironmentNode) */
  bool pbr = kind == T3_GEN_STANDARD || kind == T3_GEN_PHYSICAL;
  if (m->env_map) {
    const t3_texture *e = m->env_map;
    if (pbr) {
      if (!(e->is_cube || e->mapping == T3_CUBE_UV_REFLECTION_MAPPING || e->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING)) { *why = "envMap mapping"; return false; }
      f |= T3_GEN_ENV;
    } else if (kind == T3_GEN_BASIC || kind == T3_GEN_LAMBERT || kind == T3_GEN_PHONG) {
      if (!e->is_cube) { *why = "a Basic / Lambert / Phong envMap that is not a CubeTexture"; return false; }
      f |= T3_GEN_CENV;
      if (e->mapping == T3_CUBE_REFRACTION_MAPPING) f |= T3_GEN_REFR;
      if (m->combine == T3_MIX_OPERATION) f |= T3_GEN_MIX;
      else if (m->combine == T3_ADD_OPERATION) f |= T3_GEN_ADD;
    } else { *why = "envMap on this material kind"; return false; }
  } else if (pbr && r->environment) {
    const t3_texture *e = r->environment;
    if (!(e->is_cube || e->mapping == T3_CUBE_UV_REFLECTION_MAPPING || e->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING)) { *why = "scene.environment mapping"; return false; }
    f |= T3_GEN_SENV;
  }
  if (m->normal_map) { f |= T3_GEN_NMAP; if (g->attributes[T3_ATTR_TANGENT]) f |= T3_GEN_TAN; }
  if (m->ao_map) f |= T3_GEN_AO;
  if (m->emissive_map) f |= T3_GEN_EMAP;
  if (m->roughness_map) f |= T3_GEN_RMAP;
  if (m->metalness_map) f |= T3_GEN_MMAP;
  if (m->alpha_map) f |= T3_GEN_AMAP;
  if (m->light_map) f |= T3_GEN_LMAP;
  if (kind == T3_GEN_PHYSICAL) {
    /* r186 useClearcoat / useSheen / useIridescence / useAnisotropy / useTransmission */
    if (m->clearcoat > 0) { f |= T3_GEN_CC; if (m->clearcoat_normal_map) f |= T3_GEN_CCN; }
    if (m->sheen > 0) f |= T3_GEN_SHEEN;
    if (m->iridescence > 0) f |= T3_GEN_IRID;
    if (m->anisotropy > 0) f |= T3_GEN_ANISO;
    if (m->transmission > 0) f |= T3_GEN_TRANSM;
  }
  /* instanced draws take r186's instanced-attribute program whatever the count
   * (its uniform-buffer program bakes the count into the shader) */
  if (o->type == T3_INSTANCED_MESH || batched) f |= T3_GEN_INSTBIG;
  if (o->type == T3_INSTANCED_MESH && ((const t3_instanced_mesh *)o)->instance_color) f |= T3_GEN_ICOL;
  if (o->type == T3_SKINNED_MESH) f |= T3_GEN_SKIN;
  if (g->morph_count) { f |= T3_GEN_MORPH; if (g->morph_normal) f |= T3_GEN_MORPHN; }
  if (m->map) {
    f |= T3_GEN_MAP;
    if (m->map->wrap_s == T3_REPEAT && m->map->wrap_t == T3_REPEAT) f |= T3_GEN_REP;
    else if (m->map->wrap_s != T3_CLAMP_TO_EDGE || m->map->wrap_t != T3_CLAMP_TO_EDGE) { *why = "map wrapping"; return false; }
    /* (a depth map is nearest by default, which its state already implies) */
    if (m->map->format == T3_DEPTH_FORMAT || m->map->format == T3_DEPTH_STENCIL_FORMAT) {
      f |= T3_GEN_DMAP;
#ifdef T3_WGPU
      if (r->wgpu && t3_wgpu_compat(r->wgpu)) f |= T3_GEN_CM;   /* r186 reads it as texture_2d<f32> there */
#endif
    }
    else if (tex_nearest(m->map)) f |= T3_GEN_NEAREST;
  }
  if (m->vertex_colors) {
    /* rgb or rgba (glTF exporters write COLOR_0 as vec4 by default): the program reads the first three
     * components of the attribute, so an rgba one draws with its colour and an opaque alpha (r186's
     * USE_COLOR_ALPHA program, which would multiply the alpha in, is not in the tables) */
    if (!g->attributes[T3_ATTR_COLOR] || (g->attributes[T3_ATTR_COLOR]->item_size != 3 && g->attributes[T3_ATTR_COLOR]->item_size != 4)) {
      *why = "vertex colors that are not rgb or rgba"; return false;
    }
    f |= T3_GEN_VCOL;
  }
  if (r->fog && m->fog) f |= r->fog->type == T3_FOG_LINEAR ? T3_GEN_FOG : T3_GEN_FOG2;
  if (m->transparent) f |= T3_GEN_TRANS;
  /* material.side is in the program (FrontFacingNode.negateOnBackSide) for the
   * mesh kinds; points, lines and sprites have no normals */
  if (kind != T3_GEN_POINTS && kind != T3_GEN_LINE && kind != T3_GEN_SPRITE && kind != T3_GEN_PSPRITE) {
    if (m->side == T3_DOUBLE_SIDE) f |= T3_GEN_DS;
    else if (m->side == T3_BACK_SIDE) f |= T3_GEN_BS;
  }
  if (m->alpha_test > 0) f |= T3_GEN_ATEST;
  if ((kind == T3_GEN_SPRITE) && !m->size_attenuation) f |= T3_GEN_NOATTEN;
  if (m->flat_shading && (kind == T3_GEN_NORMAL || (kind >= T3_GEN_LAMBERT && kind <= T3_GEN_PHYSICAL))) f |= T3_GEN_FLAT;
  if (m->wireframe || m->premultiplied_alpha || m->dithering) { *why = "wireframe / premultiplied / dithering"; return false; }
  /* shadows: r186 keys the program on receiveShadow when a light casts; the
   * table's +shadow states have every light of the vector casting */
  bool shadow = false;
  uint8_t cast[3] = { 0, 0, 0 };
  if (r->shadow_map_enabled && o->receive_shadow && kind >= T3_GEN_LAMBERT && kind <= T3_GEN_PHYSICAL &&
      (r->ls.num_dir_shadows || r->ls.num_spot_shadows || r->ls.num_point_shadows)) {
    /* per type, the casters must be the first lights by id (the table's sDPS) */
    t3_light *const *arr[3] = { r->ls.dir, r->ls.point, r->ls.spot };
    int num[3] = { r->ls.num_dir, r->ls.num_point, r->ls.num_spot };
    for (int t = 0; t < 3; t++) {
      bool plain_seen = false;
      for (int i = 0; i < num[t]; i++) {
        bool c = arr[t][i]->base.cast_shadow && arr[t][i]->shadow;
        if (c && plain_seen) { *why = "a shadow-casting light after a plain one of its type (by id)"; return false; }
        if (c) cast[t]++; else plain_seen = true;
      }
    }
    f |= T3_GEN_SHADOW;
    shadow = true;
  }
  /* lights: the exact vector when the table has it, else the smallest one that
   * covers the frame's: its extra lights get inert values. With shadows the
   * casters must match exactly (they come first in each type); extra
   * unshadowed lights are covered the same way. */
  uint8_t want[4] = { 0, 0, 0, 0 };
  if (kind >= T3_GEN_LAMBERT && kind <= T3_GEN_PHYSICAL) {
    want[0] = (uint8_t)r->ls.num_dir; want[1] = (uint8_t)r->ls.num_point; want[2] = (uint8_t)r->ls.num_spot; want[3] = (uint8_t)r->ls.num_hemi;
  }
  memset(key, 0, sizeof *key);
  key->f = f;
  key->kind = (uint8_t)kind;
  key->shadow = shadow;
  if (shadow) memcpy(key->cast, cast, 3);
  memcpy(key->want, want, 4);
  key->enc = r->gen_direct;
  key->ext = m->extension;
  if (o->type == T3_SKINNED_MESH) key->bones = (uint16_t)((const t3_skinned_mesh *)o)->skeleton->bone_count;
  if (g->morph_count) {
    int w = g->attributes[T3_ATTR_POSITION]->count * (g->morph_normal ? 2 : 1);
    key->morphs = (uint16_t)g->morph_count;
    key->morph_width = (uint16_t)(w > 4096 ? 4096 : w);
  }
  return true;
}

/* the table state for a key, or NULL (with *why set) */
static const t3_gen_program *gen_state_find(const t3_renderer *r, const gen_key *key, const char **why) {
  t3_gen_kind kind = (t3_gen_kind)key->kind;
  uint64_t f = key->f;
  bool shadow = key->shadow;
  const uint8_t *cast = key->cast, *want = key->want;
  const char *ext = t3__extension_name(key->ext);
  const t3_gen_program *best = NULL;
  int best_cost = 1 << 30;
  const t3_gen_table *const *tables;
  int nt = gen_tbls(r, &tables);
  for (int t = 0; t < nt; t++)
    for (unsigned i = 0; i < tables[t]->count; i++) {
      const t3_gen_program *p = &tables[t]->programs[i];
      if (p->kind != kind || p->features != f) continue;
      if (key->ext ? !p->extension || strcmp(p->extension, ext) : p->extension != NULL) continue;
      if (shadow && memcmp(p->cast, cast, 3)) continue;
      bool covers = true;
      int cost = 0;
      for (int k = 0; k < 4; k++) { if (p->lights[k] < want[k]) covers = false; cost += p->lights[k]; }
      if (covers && cost < best_cost) { best = p; best_cost = cost; }
    }
  if (!best) {
    *why = "no table state for this feature / light combination";
    char name[256];
    gen_state_name(name, sizeof name, kind, f, want, shadow ? cast : NULL, key->ext);
#ifdef T3_BUILDER
    /* build it now (once: a state that failed stays on the missing list) */
    if (!gen_missing_has(r, name)) {
      char err[1024], msg[1400];
      t3_gen_backend be = T3_GEN_GL;
#ifdef T3_WGPU
      if (r->wgpu) be = T3_GEN_WGPU;
#endif
      best = t3_builder_build(name, be, err, sizeof err);
      if (!best) { snprintf(msg, sizeof msg, "%s: the runtime builder failed: %s", name, err); gen_note((t3_renderer *)r, msg); }
    }
#endif
    gen_record_missing_name(r, name);
  }
  return best;
}

static t3_gen_gl *gen_program_p(t3_renderer *r, const t3_gen_program *src, bool enc, t3_gen_params prm);
/* Morph.js: the texture row is vertex count x texels per vertex (1 position,
 * 2 with normals), at most 4096 wide */
static void gen_morph_params(const t3_gen_program *src, const t3_geometry *g, t3_gen_params *prm) {
  if (!src->tpl[1] || !g->morph_count) return;
  int stride = g->morph_normal ? 2 : 1;
  int w = g->attributes[T3_ATTR_POSITION]->count * stride;
  prm->morphs = (uint16_t)g->morph_count;
  prm->morph_width = (uint16_t)(w > 4096 ? 4096 : w);
}
/* the geometry's morph texture: RGBA32F 2D array, layer per target, texel
 * j * stride (+1 for the normal) per vertex, as Morph.js lays it out */
static float *gen_morph_data(const t3_geometry *g, int *pw, int *ph) {
  int n = g->attributes[T3_ATTR_POSITION]->count, stride = g->morph_normal ? 2 : 1;
  int w = n * stride, h = 1;
  if (w > 4096) { h = (w + 4095) / 4096; w = 4096; }
  *pw = w; *ph = h;
  size_t layer = (size_t)w * h * 4;
  float *data = calloc(layer * (size_t)g->morph_count, sizeof *data);
  T3_CHECK_ALLOC(data);
  for (int t = 0; t < g->morph_count; t++) {
    float *L = data + layer * (size_t)t;
    const t3_attribute *mp = g->morph_position[t], *mn = g->morph_normal ? g->morph_normal[t] : NULL;
    for (int j = 0; j < n; j++) {
      const float *p = (const float *)mp->array + (size_t)j * mp->item_size;
      float *d = L + (size_t)j * stride * 4;
      d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; d[3] = 0;
      if (mn) { const float *q = (const float *)mn->array + (size_t)j * mn->item_size; d[4] = q[0]; d[5] = q[1]; d[6] = q[2]; d[7] = 0; }
    }
  }
  return data;
}
static GLuint gen_morph_texture(t3_renderer *r, t3_geometry *g) {
  geometry_gl *gg = g->_gl;
  if (!gg) return 0;
  if (gg->morph_tex && gg->morph_version == g->version) return gg->morph_tex;
  int w, h;
  float *data = gen_morph_data(g, &w, &h);
  if (!gg->morph_tex) glGenTextures(1, &gg->morph_tex);
  glActiveTexture(GL_TEXTURE0 + 31);
  tu_forget_active();
  glBindTexture(0x8C1A /* GL_TEXTURE_2D_ARRAY */, gg->morph_tex);
  glTexImage3D(0x8C1A, 0, GL_RGBA32F, w, h, g->morph_count, 0, GL_RGBA, GL_FLOAT, data);
  glTexParameteri(0x8C1A, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(0x8C1A, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  free(data);
  gg->morph_version = g->version;
  (void)r;
  return gg->morph_tex;
}
static t3_gen_gl *gen_program_enc(t3_renderer *r, const t3_gen_program *src, bool enc) { return gen_program_p(r, src, enc, (t3_gen_params){ 0, 0, 0 }); }
static t3_gen_gl *gen_program_p(t3_renderer *r, const t3_gen_program *src, bool enc, t3_gen_params prm) {
  for (int i = 0; i < r->gen_n; i++)
    if (r->gen[i].src == src && r->gen[i].encode == enc && !memcmp(&r->gen[i].params, &prm, sizeof prm)) return r->gen[i].gl;
  char err[2048];
#ifdef T3_WGPU
  t3_gen_gl *gl = r->wgpu ? t3_gen_wgpu_build(r->wgpu, src, prm, err, sizeof err) : t3_gen_gl_build(src, enc, prm, err, sizeof err);
#else
  t3_gen_gl *gl = t3_gen_gl_build(src, enc, prm, err, sizeof err);
#endif
  if (!gl) {
    char buf[2200];
    snprintf(buf, sizeof buf, "program %s: %.2000s", src->state, err);
    gen_note(r, buf);
  } else if (!gl->complete) {
    char buf[200];
    snprintf(buf, sizeof buf, "program %s: no op for property %s", src->state, gl->unsupported);
    gen_note(r, buf);
  }
  if (r->gen_n == r->gen_cap) {
    r->gen_cap = r->gen_cap ? r->gen_cap * 2 : 16;
    r->gen = realloc(r->gen, (size_t)r->gen_cap * sizeof *r->gen);
    T3_CHECK_ALLOC(r->gen);
  }
  r->gen[r->gen_n].src = src;
  r->gen[r->gen_n].encode = enc;
  r->gen[r->gen_n].params = prm;
  r->gen[r->gen_n++].gl = gl;       /* a NULL gl is remembered: the build is not retried every draw */
  return gl;
}

/* the program an item takes, or NULL (the state is not in the table: the draw is skipped) */
static uint64_t gen_key_hash(const gen_key *k) {
  uint64_t w[4];
  memcpy(w, k, sizeof w);
  uint64_t h = w[0] * 0x9E3779B97F4A7C15ull;
  h = (h ^ (h >> 29) ^ w[1]) * 0xBF58476D1CE4E5B9ull;
  h = (h ^ (h >> 31) ^ w[2] ^ (w[3] << 17)) * 0x94D049BB133111EBull;
  return h ^ (h >> 32);
}
/* the key's slot in the renderer's lookup table: its own, or the empty one it would take */
static struct gen_slot *gen_slot_of(t3_renderer *r, const gen_key *k) {
  size_t mask = r->gen_slot_cap - 1;
  for (size_t i = (size_t)gen_key_hash(k) & mask;; i = (i + 1) & mask) {
    struct gen_slot *s = &r->gen_slots[i];
    if (!s->used || !memcmp(&s->key, k, sizeof *k)) return s;
  }
}
static void gen_slots_reset(t3_renderer *r, size_t cap) {
  free(r->gen_slots);
  r->gen_slots = calloc(cap, sizeof *r->gen_slots);
  T3_CHECK_ALLOC(r->gen_slots);
  r->gen_slot_cap = cap;
  r->gen_slot_n = 0;
}
/* the program an item takes, or NULL (the state is not in the table: the draw is skipped) */
static t3_gen_gl *gen_pick(t3_renderer *r, const t3_material *m, const t3_object *o, const t3_geometry *g, bool batched) {
  _Static_assert(sizeof(gen_key) == 32, "gen_key is hashed as four words");
  const char *why = "";
  gen_key key;
  if (!gen_state_key(r, m, o, g, batched, &why, &key)) { gen_note(r, why); r->info.skipped++; return NULL; }
  unsigned serial = t3_gen_tables_serial();
  if (!r->gen_slots || r->gen_slot_serial != serial) { gen_slots_reset(r, 64); r->gen_slot_serial = serial; }
  struct gen_slot *slot = gen_slot_of(r, &key);
  t3_gen_gl *gl;
  if (slot->used) gl = slot->gl;
  else {
    const t3_gen_program *src = gen_state_find(r, &key, &why);
    if (!src) { gen_note(r, why); r->info.skipped++; return NULL; }
    t3_gen_params prm = { 0, 0, 0 };
    if (src->tpl[0] && o->type == T3_SKINNED_MESH) prm.bones = (uint16_t)((const t3_skinned_mesh *)o)->skeleton->bone_count;
    gen_morph_params(src, g, &prm);
    gl = gen_program_p(r, src, r->gen_direct, prm);
    /* (a runtime build changes the tables: the next pick starts a fresh lookup table) */
    if (r->gen_slot_serial == t3_gen_tables_serial()) {
      if ((r->gen_slot_n + 1) * 2 > r->gen_slot_cap) {
        struct gen_slot *old = r->gen_slots;
        size_t n = r->gen_slot_cap;
        r->gen_slots = NULL;
        gen_slots_reset(r, n * 2);
        for (size_t i = 0; i < n; i++) if (old[i].used) { *gen_slot_of(r, &old[i].key) = old[i]; r->gen_slot_n++; }
        free(old);
        slot = gen_slot_of(r, &key);
      }
      slot->used = true;
      slot->key = key;
      slot->gl = gl;
      r->gen_slot_n++;
    }
  }
  if (!gl || !gl->complete) { r->info.skipped++; return NULL; }
  static int dbg = -1;
  if (dbg < 0) dbg = getenv("T3_GEN_DEBUG") != NULL;
  if (dbg && r->stamp != gl->render_stamp) fprintf(stderr, "[gen] %s lights %d/%d/%d/%d frame ops %d ops %d\n", gl->src->state, r->ls.num_dir, r->ls.num_point, r->ls.num_spot, r->ls.num_hemi, gl->n_frame_ops, gl->n_ops);
  return gl;
}

/* a material's texture by T3_GEN_TEX_* (NULL when unset or three.c has no such slot) */
static const t3_texture *gen_tex(const t3_material *m, int which) {
  switch (which) {
  case T3_GEN_TEX_MAP: return m->map;
  case T3_GEN_TEX_NORMAL: return m->normal_map;
  case T3_GEN_TEX_AO: return m->ao_map;
  case T3_GEN_TEX_EMISSIVE: return m->emissive_map;
  case T3_GEN_TEX_ROUGHNESS: return m->roughness_map;
  case T3_GEN_TEX_METALNESS: return m->metalness_map;
  case T3_GEN_TEX_ALPHA: return m->alpha_map;
  case T3_GEN_TEX_LIGHT: return m->light_map;
  case T3_GEN_TEX_CLEARCOAT_NORMAL: return m->clearcoat_normal_map;
  default: return NULL;
  }
}

static void put3(uint8_t *b, const t3_color *c, float k) { float v[3] = { c->r * k, c->g * k, c->b * k }; memcpy(b, v, 12); }
static void putv3(uint8_t *b, t3_vec3 v) { float f[3] = { v.x, v.y, v.z }; memcpy(b, f, 12); }
static void putf(uint8_t *b, float x) { memcpy(b, &x, 4); }

/* the camera, lights and fog; extra lights of a covering vector get inert values */
static void gen_fill_frame(t3_renderer *r, t3_gen_gl *gl, const t3_camera *cam) {
  const light_state *ls = &r->ls;
  for (int i = 0; i < gl->n_groups; i++) if (gl->frame_groups >> i & 1) gl->dirty[i] = true;
  for (int k = 0; k < gl->n_frame_ops; k++) {
    const struct t3_gen_op *op = &gl->ops[k];
    uint8_t *b = gl->mirror[op->group] + op->off;
    int i = op->idx;
    switch (op->op) {
    case GOP_CAM_PROJ:
      if (cam) {
        memcpy(b, cam->projection_matrix.e, 64);
#ifdef T3_WGPU
        if (r->wgpu) {
          /* WebGPU clip z is [0, 1]: z' = 0.5 z + 0.5 w (three.js builds this
           * matrix for WebGPUCoordinateSystem) */
          float p[16];
          memcpy(p, cam->projection_matrix.e, 64);
          double n = cam->near, f = cam->far;
          if (cam->base.type == T3_PERSPECTIVE_CAMERA) {
            /* a projection the application edited (an oblique near plane:
             * r186's ReflectorNode replaces the third row) is not the one
             * near / far build: remap its z row as clip space does instead,
             * z' = 0.5 z + 0.5 w, which keeps the edit */
            if (p[2] == 0 && p[6] == 0 && p[10] == (float)(-(f + n) / (f - n)) && p[14] == (float)(-2 * f * n / (f - n))) {
              p[10] = (float)(-f / (f - n)); p[14] = (float)(-f * n / (f - n));
            } else {
              for (int c = 0; c < 4; c++) p[c * 4 + 2] = 0.5f * p[c * 4 + 2] + 0.5f * p[c * 4 + 3];
            }
          }
          else { p[10] = (float)(-1 / (f - n)); p[14] = (float)(-n / (f - n)); }
          memcpy(b, p, 64);
        }
#endif
      }
      break;
    case GOP_CAM_VIEW: if (cam) memcpy(b, cam->matrix_world_inverse.e, 64); break;
    case GOP_AMBIENT: put3(b, &ls->ambient, 1); break;
    case GOP_DIR_COLOR: if (i < ls->num_dir) put3(b, &ls->dir[i]->color, ls->dir[i]->intensity); else put3(b, &(t3_color){0, 0, 0}, 0); break;
    case GOP_DIR_POS: putv3(b, i < ls->num_dir ? t3_mat4_get_position(&ls->dir[i]->base.matrix_world) : (t3_vec3){0, 1, 0}); break;
    case GOP_DIR_TARGET: putv3(b, i < ls->num_dir ? t3_mat4_get_position(&ls->dir[i]->target->matrix_world) : (t3_vec3){0, 0, 0}); break;
    case GOP_POINT_COLOR: if (i < ls->num_point) put3(b, &ls->point[i]->color, ls->point[i]->intensity); else put3(b, &(t3_color){0, 0, 0}, 0); break;
    case GOP_POINT_POS: putv3(b, i < ls->num_point ? ls->point_position[i] : (t3_vec3){0, 0, 0}); break;
    case GOP_POINT_DIST: putf(b, i < ls->num_point ? ls->point[i]->distance : 0); break;
    case GOP_POINT_DECAY: putf(b, i < ls->num_point ? ls->point[i]->decay : 1); break;
    case GOP_SPOT_COLOR: if (i < ls->num_spot) put3(b, &ls->spot[i]->color, ls->spot[i]->intensity); else put3(b, &(t3_color){0, 0, 0}, 0); break;
    case GOP_SPOT_POS: putv3(b, i < ls->num_spot ? ls->spot_position[i] : (t3_vec3){0, 0, 0}); break;
    case GOP_SPOT_WPOS: putv3(b, i < ls->num_spot ? t3_mat4_get_position(&ls->spot[i]->base.matrix_world) : (t3_vec3){0, 0, 0}); break;
    case GOP_SPOT_TARGET: putv3(b, i < ls->num_spot ? t3_mat4_get_position(&ls->spot[i]->target->matrix_world) : (t3_vec3){0, 0, -1}); break;
    case GOP_SPOT_DIST: putf(b, i < ls->num_spot ? ls->spot[i]->distance : 0); break;
    case GOP_SPOT_DECAY: putf(b, i < ls->num_spot ? ls->spot[i]->decay : 1); break;
    case GOP_SPOT_CONE: putf(b, i < ls->num_spot ? cosf(ls->spot[i]->angle) : 0); break;
    case GOP_SPOT_PENUMBRA: putf(b, i < ls->num_spot ? cosf(ls->spot[i]->angle * (1 - ls->spot[i]->penumbra)) : 0); break;
    case GOP_HEMI_SKY: if (i < ls->num_hemi) put3(b, &ls->hemi[i]->color, ls->hemi[i]->intensity); else put3(b, &(t3_color){0, 0, 0}, 0); break;
    case GOP_HEMI_GROUND: if (i < ls->num_hemi) put3(b, &ls->hemi[i]->ground_color, ls->hemi[i]->intensity); else put3(b, &(t3_color){0, 0, 0}, 0); break;
    case GOP_HEMI_POS: putv3(b, i < ls->num_hemi ? t3_mat4_get_position(&ls->hemi[i]->base.matrix_world) : (t3_vec3){0, 1, 0}); break;
    case GOP_VIEWPORT_RECT: { float v[4] = { (float)r->view_x, (float)r->view_y, (float)r->view_w, (float)r->view_h }; memcpy(b, v, 16); break; }
    case GOP_VIEWPORT_SIZE: { float v[2] = { (float)r->view_w, (float)r->view_h }; memcpy(b, v, 8); break; }
    case GOP_DRAWBUF_SIZE: { float v[2] = { (float)r->width, (float)r->height }; memcpy(b, v, 8); break; }
    case GOP_HALF_HEIGHT: putf(b, (float)r->height * 0.5f); break;
    case GOP_CAM_WORLD: if (cam) memcpy(b, cam->base.matrix_world.e, 64); break;
    case GOP_CAM_POS: if (cam) memcpy(b, cam->base.matrix_world.e + 12, 12); break;   /* the world position */
    case GOP_TONE_EXPOSURE: putf(b, r->tone_mapping_exposure); break;
    case GOP_SHADOW: {
      int type = op->sub >> 4, field = op->sub & 15;
      const t3_light *l = type == T3_GEN_LT_DIR ? (i < ls->num_dir ? ls->dir[i] : NULL)
                        : type == T3_GEN_LT_SPOT ? (i < ls->num_spot ? ls->spot[i] : NULL)
                        : (i < ls->num_point ? ls->point[i] : NULL);
      const t3_light_shadow *sh = l ? l->shadow : NULL;
      if (!sh) break;
      switch (field) {
      case T3_GEN_SH_BIAS: putf(b, sh->bias); break;
      case T3_GEN_SH_NORMAL_BIAS: putf(b, sh->normal_bias); break;
      case T3_GEN_SH_RADIUS: putf(b, sh->radius); break;
      case T3_GEN_SH_INTENSITY: putf(b, sh->intensity); break;
      case T3_GEN_SH_MAP_SIZE: { float v[2] = { (float)sh->map_width, (float)sh->map_height }; memcpy(b, v, 8); break; }
      case T3_GEN_SH_MATRIX: memcpy(b, sh->matrix.e, 64); break;
      }
      break;
    }
    case GOP_FOG_COLOR: if (r->fog) put3(b, &r->fog->color, 1); break;
    case GOP_FOG_NEAR: if (r->fog) putf(b, r->fog->near); break;
    case GOP_FOG_FAR: if (r->fog) putf(b, r->fog->far); break;
    case GOP_FOG_DENSITY: if (r->fog) putf(b, r->fog->density); break;
    default: break;
    }
  }
}

/* cube-UV sizes from a PMREM texture's height (PMREMNode _generateCubeUVSize) */
static void cubeuv_sizes(const t3_texture *t, float out[3]) {
  float h = t ? (float)t->height : 1024;
  float mm = log2f(h) - 2;
  float p = powf(2, mm);
  out[0] = 1.0f / (3 * (p > 112 ? p : 112)); out[1] = 1.0f / h; out[2] = mm;
}
/* the transposed rotation matrix of an euler (MaterialProperties envMapRotation) */
static void rotation_t(t3_euler e, float out[16]) {
  t3_mat4 m;
  t3_mat4_compose(&m, t3_v3(0, 0, 0), t3_quat_from_euler(e), t3_v3(1, 1, 1));
  for (int c = 0; c < 4; c++) for (int k = 0; k < 4; k++) out[c * 4 + k] = m.e[k * 4 + c];
}
static void gen_fill_draw(t3_renderer *r, t3_gen_gl *gl, const t3_material *m, const t3_object *o, bool batched) {
  /* a batch draws many meshes: their world matrices ride in the instance attribute */
  static const t3_mat4 id = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } };
  const t3_mat4 *world = batched ? &id : &o->matrix_world;
  for (int i = 0; i < gl->n_groups; i++) if (gl->draw_groups >> i & 1) gl->dirty[i] = true;
  for (int k = gl->n_frame_ops; k < gl->n_ops; k++) {
    const struct t3_gen_op *op = &gl->ops[k];
    uint8_t *b = gl->mirror[op->group] + op->off;
    switch (op->op) {
    case GOP_MODEL: memcpy(b, world->e, 64); break;
    case GOP_OBJ_POS: putv3(b, t3_mat4_get_position(world)); break;
    case GOP_MORPH_BASE: {
      const t3_mesh *mesh = (const t3_mesh *)o;
      float base = 1;
      if (!mesh->geometry->morph_relative && mesh->morph_influences)
        for (int k = 0; k < mesh->geometry->morph_count; k++) base -= mesh->morph_influences[k];
      putf(b, base);
      break;
    }
    case GOP_SKIN_BIND: if (o->type == T3_SKINNED_MESH) memcpy(b, ((const t3_skinned_mesh *)o)->bind_matrix.e, 64); break;
    case GOP_SKIN_BIND_INV: if (o->type == T3_SKINNED_MESH) memcpy(b, ((const t3_skinned_mesh *)o)->bind_matrix_inverse.e, 64); break;
    case GOP_NORMAL_MATRIX: {
      /* Matrix3.getNormalMatrix(matrixWorld), std140: three vec3 columns at 16 bytes */
      t3_mat3 nm;
      t3_mat3_normal_matrix(&nm, world);
      memcpy(b, nm.e, 12); memcpy(b + 16, nm.e + 3, 12); memcpy(b + 32, nm.e + 6, 12);
      break;
    }
    case GOP_MAT_COLOR: put3(b, &m->color, 1); break;
    case GOP_ENV_INTENSITY: putf(b, r->aux.env_intensity); break;
    case GOP_ENV_ROTATION: { float v[16]; rotation_t(r->aux.env_rotation, v); memcpy(b, v, 64); break; }
    case GOP_MAT_REFLECTIVITY: putf(b, m->reflectivity); break;
    case GOP_MAT_REFRACTION: putf(b, m->refraction_ratio); break;
    case GOP_CUBEUV_TW: case GOP_CUBEUV_TH: case GOP_CUBEUV_MAXMIP: {
      float c[3];
      cubeuv_sizes(r->aux.env ? r->aux.env : r->aux.bg, c);
      putf(b, c[op->op - GOP_CUBEUV_TW]);
      break;
    }
    case GOP_BG_INTENSITY: putf(b, r->aux.bg_intensity); break;
    case GOP_BG_BLUR: putf(b, r->aux.bg_blur); break;
    case GOP_BG_ROTATION: { float v[16]; rotation_t(r->aux.bg_rotation, v); memcpy(b, v, 64); break; }
    case GOP_BG_MATRIX: {
      float mt[9];
      if (r->aux.bg) uv_transform(r->aux.bg, mt); else { memset(mt, 0, sizeof mt); mt[0] = mt[4] = mt[8] = 1; }
      memcpy(b, mt, 12); memcpy(b + 16, mt + 3, 12); memcpy(b + 32, mt + 6, 12);
      break;
    }
    case GOP_BG_FLIPY: { uint32_t v = r->aux.bg && r->aux.bg->gpu_made; memcpy(b, &v, 4); break; }
    case GOP_PMREM_FLIPY: { uint32_t v = r->aux.pm_src && r->aux.pm_src->gpu_made; memcpy(b, &v, 4); break; }
    case GOP_ENV_FLIPY: { uint32_t v = r->aux.env && r->aux.env->gpu_made; memcpy(b, &v, 4); break; }
    case GOP_PMREM_ROUGH: putf(b, r->aux.pm_rough); break;
    case GOP_PMREM_MIP: putf(b, r->aux.pm_mip); break;
    case GOP_PMREM_SIGMA: putf(b, r->aux.pm_sigma); break;
    case GOP_MAT_OPACITY: putf(b, m->opacity); break;
    case GOP_MAT_EMISSIVE: put3(b, &m->emissive, 1); break;
    case GOP_MAT_EMISSIVE_I: putf(b, m->emissive_intensity); break;
    case GOP_MAT_ROUGH: putf(b, m->roughness); break;
    case GOP_MAT_METAL: putf(b, m->metalness); break;
    case GOP_MAT_SPECULAR: put3(b, &m->specular, 1); break;
    case GOP_MAT_SHININESS: putf(b, m->shininess); break;
    case GOP_MAT_IOR: putf(b, m->ior); break;
    case GOP_MAT_CLEARCOAT: putf(b, m->clearcoat); break;
    case GOP_MAT_CLEARCOAT_R: putf(b, m->clearcoat_roughness); break;
    case GOP_MAT_SPEC_INTENSITY: putf(b, m->specular_intensity); break;
    case GOP_MAT_SPEC_COLOR: put3(b, &m->specular_color, 1); break;
    case GOP_MAT_CC_NORMAL_SCALE: memcpy(b, &m->clearcoat_normal_scale, 8); break;
    case GOP_MAT_SHEEN: putf(b, m->sheen); break;
    case GOP_MAT_SHEEN_COLOR: put3(b, &m->sheen_color, 1); break;
    case GOP_MAT_SHEEN_R: putf(b, m->sheen_roughness); break;
    case GOP_MAT_IRID: putf(b, m->iridescence); break;
    case GOP_MAT_IRID_IOR: putf(b, m->iridescence_ior); break;
    case GOP_MAT_IRID_THICK_MAX: putf(b, m->iridescence_thickness_range[1]); break;
    case GOP_MAT_ANISO_VEC: {   /* r186 materialAnisotropyVector: anisotropy along anisotropyRotation */
      float v[2] = { m->anisotropy * cosf(m->anisotropy_rotation), m->anisotropy * sinf(m->anisotropy_rotation) };
      memcpy(b, v, 8);
      break;
    }
    case GOP_MAT_TRANSMISSION: putf(b, m->transmission); break;
    case GOP_MAT_THICKNESS: putf(b, m->thickness); break;
    case GOP_MAT_ATTEN_DIST: putf(b, m->attenuation_distance); break;
    case GOP_MAT_ATTEN_COLOR: put3(b, &m->attenuation_color, 1); break;
    case GOP_TEX_MATRIX: {
      const t3_texture *t = gen_tex(m, op->idx);
      if (t) {
        float mt[9];
        uv_transform(t, mt);
        memcpy(b, mt, 12); memcpy(b + 16, mt + 3, 12); memcpy(b + 32, mt + 6, 12);
      }
      break;
    }
    case GOP_TEX_FLIPY: {
      const t3_texture *t = op->idx < T3_GEN_TEX_COUNT ? gen_tex(m, op->idx) : NULL;
      uint32_t v = op->idx == T3_GEN_TEX_COUNT + 1 ? 1 : t && t->gpu_made;
      memcpy(b, &v, 4);
      break;
    }
    case GOP_MAT_ROTATION: putf(b, m->rotation); break;
    case GOP_MAT_ALPHATEST: putf(b, m->alpha_test); break;
    case GOP_MAT_SIZE: putf(b, m->size); break;
    case GOP_SPRITE_CENTER: if (o->type == T3_SPRITE) { float c[2] = { ((const t3_sprite *)o)->center.x, ((const t3_sprite *)o)->center.y }; memcpy(b, c, 8); } break;
    case GOP_NORMAL_SCALE: { float v[2] = { m->normal_scale.x, m->normal_scale.y }; memcpy(b, v, 8); break; }
    case GOP_AO_INTENSITY: putf(b, m->ao_map_intensity); break;
    case GOP_LIGHTMAP_INTENSITY: putf(b, m->light_map_intensity); break;
    default: break;
    }
  }
}

/* ── extension programs: the hook registered for the program's extension
 * fills its "ext.<name>" uniforms and textures for each draw ── */
struct t3_extension_ctx { t3_renderer *r; t3_gen_gl *gl; const t3_material *m; const t3_object *o; };
const t3_object *t3_extension_object(const t3_extension_ctx *c) { return c->o; }
const t3_material *t3_extension_material(const t3_extension_ctx *c) { return c->m; }
/* bytes into a group's mirror, marking it dirty only when they change (a
 * shared group then uploads, or streams, only when its values do) */
static void ext_put(t3_gen_gl *gl, int group, uint16_t off, const void *v, size_t n) {
  uint8_t *d = gl->mirror[group] + off;
  if (memcmp(d, v, n)) { memcpy(d, v, n); gl->dirty[group] = true; }
}
/* "tint" against a manifest name, which may carry "#n" (another copy) */
static bool ext_name_is(const char *manifest, const char *name) {
  size_t n = strlen(name);
  return !strncmp(manifest, name, n) && (manifest[n] == 0 || manifest[n] == '#');
}
static void ext_uniform_put(t3_gen_gl *gl, int i, const float *v, int n, bool *ok) {
  if (gl->ext[i].mat3) {
    /* std140 / WGSL mat3x3: three columns at a 16-byte stride */
    if (n != 9) { *ok = false; return; }
    float cols[11] = { v[0], v[1], v[2], 0, v[3], v[4], v[5], 0, v[6], v[7], v[8] };
    ext_put(gl, gl->ext[i].group, gl->ext[i].off, cols, 44);
  } else {
    if (n < 1 || (size_t)n * 4 > gl->ext[i].len) { *ok = false; return; }
    ext_put(gl, gl->ext[i].group, gl->ext[i].off, v, (size_t)n * 4);
  }
}
/* a hook passes the same names every draw: the entry a name found is
 * remembered by the name's address (and checked against the name) */
static unsigned ext_slot(const char *name, unsigned n) { return (unsigned)(((uintptr_t)name >> 3) ^ ((uintptr_t)name >> 9)) & (n - 1); }
bool t3_extension_set_uniform(t3_extension_ctx *c, const char *name, const float *v, int n) {
  t3_gen_gl *gl = c->gl;
  bool ok = true;
  unsigned s = ext_slot(name, 32);
  int k = gl->ext_ck[s].name == name ? gl->ext_ck[s].idx : -1;
  if (k >= 0 && k < gl->n_ext && ext_name_is(gl->ext[k].name, name)) {
    ext_uniform_put(gl, k, v, n, &ok);
    return ok;
  }
  int found = 0, first = -1;
  for (int i = 0; i < gl->n_ext; i++) {
    if (!ext_name_is(gl->ext[i].name, name) || !gl->mirror[gl->ext[i].group]) continue;
    ext_uniform_put(gl, i, v, n, &ok);
    if (!ok) return false;
    if (!found++) first = i;
  }
  gl->ext_ck[s].name = name;
  gl->ext_ck[s].idx = (int16_t)(found == 1 ? first : -1);
  return found > 0;
}
bool t3_extension_set_texture(t3_extension_ctx *c, const char *name, t3_texture *t) {
  t3_gen_gl *gl = c->gl;
  unsigned s = ext_slot(name, 16);
  int k = gl->ext_tck[s].name == name ? gl->ext_tck[s].idx : -1;
  if (k >= 0 && k < gl->n_ext_tex && !strcmp(gl->ext_tex[k].name, name)) { gl->ext_tex[k].tex = t; return true; }
  for (int i = 0; i < gl->n_ext_tex; i++)
    if (!strcmp(gl->ext_tex[i].name, name)) {
      gl->ext_tex[i].tex = t;
      gl->ext_tck[s].name = name;
      gl->ext_tck[s].idx = (int16_t)i;
      return true;
    }
  return false;
}
void t3_renderer_set_extension(t3_renderer *r, const char *name, t3_extension_fill fill, void *user) {
  uint16_t id = t3__extension_id(name);
  if (!id) return;
  int h = 0;
  while (h < r->ext_hook_n && r->ext_hooks[h].id != id) h++;
  if (!fill) {
    if (h < r->ext_hook_n) r->ext_hooks[h] = r->ext_hooks[--r->ext_hook_n];
    return;
  }
  if (h == r->ext_hook_n) {
    if (h == (int)(sizeof r->ext_hooks / sizeof r->ext_hooks[0])) { gen_note(r, "t3_renderer_set_extension: more extensions than the renderer has room for"); return; }
    r->ext_hook_n++;
  }
  r->ext_hooks[h].id = id;
  r->ext_hooks[h].fill = fill;
  r->ext_hooks[h].user = user;
}
void t3_renderer_set_extension_batching(t3_renderer *r, const char *name, bool on) {
  uint16_t id = t3__extension_id(name);
  if (!id) return;
  int i = 0;
  while (i < r->ext_batch_n && r->ext_batch[i] != id) i++;
  if (!on) {
    if (i < r->ext_batch_n) r->ext_batch[i] = r->ext_batch[--r->ext_batch_n];
    return;
  }
  if (i < r->ext_batch_n) return;
  if (r->ext_batch_n == (int)(sizeof r->ext_batch / sizeof r->ext_batch[0])) { gen_note(r, "t3_renderer_set_extension_batching: too many extensions"); return; }
  r->ext_batch[r->ext_batch_n++] = id;
}
/* run the hook for a draw of an extension program: false (noted) when the
 * extension has no hook or the hook left a texture unset, and the draw is
 * skipped */
static bool gen_fill_ext(t3_renderer *r, t3_gen_gl *gl, const t3_material *m, const t3_object *o) {
  if (!gl->src->extension) return true;
  int h = 0;
  while (h < r->ext_hook_n && r->ext_hooks[h].id != gl->ext_id) h++;
  char buf[200];
  if (h == r->ext_hook_n) {
    snprintf(buf, sizeof buf, "program %s: no hook for extension %s (t3_renderer_set_extension)", gl->src->state, gl->src->extension);
    return gen_note(r, buf);
  }
  for (int t = 0; t < gl->n_ext_tex; t++) gl->ext_tex[t].tex = NULL;
  struct t3_extension_ctx c = { r, gl, m, o };
  r->ext_hooks[h].fill(&c, r->ext_hooks[h].user);
  for (int t = 0; t < gl->n_ext_tex; t++) {
    const t3_texture *tx = gl->ext_tex[t].tex;
    if (!tx) {
      snprintf(buf, sizeof buf, "program %s: extension %s left texture %s unset", gl->src->state, gl->src->extension, gl->ext_tex[t].name);
      return gen_note(r, buf);
    }
    /* r186 TextureNode flipY: a texture the GPU drew is stored flipped */
    for (int k = 0; k < gl->ext_tex[t].n_flip; k++)
      if (gl->mirror[gl->ext_tex[t].flip_group[k]]) {
        uint32_t v = tx->gpu_made;
        ext_put(gl, gl->ext_tex[t].flip_group[k], gl->ext_tex[t].flip_off[k], &v, 4);
      }
  }
  return true;
}

/* r186's precomputed DFG LUT as a texture (16 x 16 RG16F, linear, clamp) */
static GLuint gen_dfg_texture(t3_renderer *r) {
  if (r->dfg_tex) return r->dfg_tex;
  glGenTextures(1, &r->dfg_tex);
  tu_bind(31, r->dfg_tex, true);
  glTexImage2D(GL_TEXTURE_2D, 0, 0x822F /* GL_RG16F */, T3_GEN_DFG_LUT_SIZE, T3_GEN_DFG_LUT_SIZE, 0, 0x8227 /* GL_RG */,
               0x140B /* GL_HALF_FLOAT */, t3_gen_dfg_lut);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return r->dfg_tex;
}

/* ── shadows (r186 ShadowNode): a depth texture per casting light, drawn
 * with the table's caster programs (kind depth) from the light's shadow
 * camera, sampled by receivers with a LEQUAL comparison (hardware PCF:
 * linear filtering for PCFShadowMap) ── */
static struct gen_shadow *gen_shadow_of(t3_renderer *r, const t3_light *l, bool make) {
  for (int i = 0; i < r->gen_sh_n; i++)
    if (r->gen_sh[i].l == l && r->gen_sh[i].light_id == l->base.id) return &r->gen_sh[i];
  if (!make) return NULL;
  /* Scene changes destroy lights, but their depth storage can be reused.
   * Keep every caster in this render, including ones not processed yet.
   * Compare ids too: allocators can reuse a destroyed light's address. */
  for (int i = 0; i < r->gen_sh_n; i++) {
    struct gen_shadow *s = &r->gen_sh[i];
    bool live = false;
    for (int j = 0; j < r->caster_count; j++)
      if (s->l == r->casters[j] && s->light_id == r->casters[j]->base.id) { live = true; break; }
    if (live) continue;
    s->l = l;
    s->light_id = l->base.id;
    s->rendered = false;
    return s;
  }
  if (r->gen_sh_n == (int)(sizeof r->gen_sh / sizeof r->gen_sh[0])) return NULL;
  struct gen_shadow *s = &r->gen_sh[r->gen_sh_n++];
  memset(s, 0, sizeof *s);
  s->l = l;
  s->light_id = l->base.id;
  return s;
}
/* a 2D depth texture, or for a point light a cube one (r186 CubeDepthTexture) */
static void gen_shadow_storage(t3_renderer *r, struct gen_shadow *s, int w, int h, bool cube) {
  if (s->tex && s->w == w && s->h == h && s->cube == cube) return;
  if (s->tex && s->cube != cube) { glDeleteTextures(1, &s->tex); s->tex = 0; }
  if (!s->tex) glGenTextures(1, &s->tex);
  if (!s->fbo) glGenFramebuffers(1, &s->fbo);
  s->cube = cube;
  GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
  if (cube) {
    tu_bind_cube(31, s->tex, true);
    for (int f = 0; f < 6; f++)
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_DEPTH_COMPONENT24, w, w, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
  } else {
    tu_bind(31, s->tex, true);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
  }
  bool pcf = r->shadow_map_type != T3_BASIC_SHADOW_MAP;
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, pcf ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, pcf ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, 0x884C /* GL_TEXTURE_COMPARE_MODE */, 0x884E /* GL_COMPARE_REF_TO_TEXTURE */);
  glTexParameteri(target, 0x884D /* GL_TEXTURE_COMPARE_FUNC */, GL_LEQUAL);
  glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
  if (cube) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X, s->tex, 0);
  else glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s->tex, 0);
  GLenum none = GL_NONE;
  glDrawBuffers(1, &none);
#ifndef T3_WASMCART   /* (the cart ABI's gl module has no glReadBuffer; a depth-only FBO needs none on WebGL2) */
  glReadBuffer(GL_NONE);
#endif
  r->fb_known = true;
  r->cur_fbo = s->fbo;
  s->w = w; s->h = h;
}
/* the light a shadow texture source names (T3_SRC_SHADOW_*, its index), or NULL */
static const t3_light *gen_shadow_light(const t3_renderer *r, uint8_t kind, int idx) {
  const light_state *ls = &r->ls;
  return kind == T3_SRC_SHADOW_DIR ? (idx < ls->num_dir ? ls->dir[idx] : NULL)
       : kind == T3_SRC_SHADOW_SPOT ? (idx < ls->num_spot ? ls->spot[idx] : NULL)
       : kind == T3_SRC_SHADOW_POINT ? (idx < ls->num_point ? ls->point[idx] : NULL) : NULL;
}
/* "directional[0]" -> that light's shadow texture on unit */
static void gen_bind_shadow(t3_renderer *r, uint8_t kind, int idx, int unit) {
  const t3_light *l = gen_shadow_light(r, kind, idx);
  struct gen_shadow *s = l ? gen_shadow_of(r, l, false) : NULL;
  if (s && s->tex) { if (s->cube) tu_bind_cube(unit, s->tex, true); else tu_bind(unit, s->tex, true); }
}

/* the caster program for an object (r186 draws the shadow pass with an
 * override NodeMaterial: kind depth, plus the object's deformations) */
static t3_gen_gl *gen_caster_program(t3_renderer *r, const t3_object *o, const t3_geometry *g, const t3_material *m) {
  if (g->morph_count && (g->morph_normal || o->type != T3_MESH)) return NULL;
  uint64_t f = 0;
  if (g->morph_count) f |= T3_GEN_MORPH;
  if (o->type == T3_INSTANCED_MESH) f |= T3_GEN_INSTBIG;
  if (o->type == T3_SKINNED_MESH) f |= T3_GEN_SKIN;
  /* (the caster program does not depend on the lights: look it up by name) */
  const char *base = f & T3_GEN_INSTBIG ? "depth+instbig" : f & T3_GEN_SKIN ? "depth+skin" : f & T3_GEN_MORPH ? "depth+morph" : "depth";
  const t3_gen_program *src = NULL;
  if (m->extension) {
    /* r186 draws the shadow pass with the material's positionNode (and its
     * colorNode's alpha): an extension material casts with depth+x<name>.
     * A table without it is listed as missing, and the plain caster draws. */
    char name[96];
    snprintf(name, sizeof name, "%s+x%s", base, t3__extension_name(m->extension));
    src = gen_by_name(r, name);
    if (!src) gen_record_missing_name(r, name);
  }
  if (!src) src = gen_by_name(r, base);
  if (!src) { gen_record_missing(r, T3_GEN_DEPTH, f, (const uint8_t[4]){ 0, 0, 0, 0 }, NULL); return NULL; }
  t3_gen_params prm = { 0, 0, 0 };
  if (src->tpl[0] && o->type == T3_SKINNED_MESH) prm.bones = (uint16_t)((const t3_skinned_mesh *)o)->skeleton->bone_count;
  gen_morph_params(src, g, &prm);
  t3_gen_gl *gl = gen_program_p(r, src, false, prm);
  return gl && gl->complete ? gl : NULL;
}
/* T3_GEN_DUMP (debug: every draw block on stderr), read once rather than per item and per draw */
static bool gen_dump(void) {
  static int on = -1;
  if (on < 0) on = getenv("T3_GEN_DUMP") != NULL;
  return on;
}
static void gen_collect_casters(t3_renderer *r, t3_object *o, const t3_camera *sc);
/* the caster draws: on GLES collected, their per-draw blocks packed and
 * uploaded with one call, then drawn (one upload a shadow map, not one a
 * draw); WebGPU draws as it walks (its blocks go to the frame stream) */
static void gen_casters(t3_renderer *r, t3_object *o, const t3_camera *sc) {
#ifdef T3_WGPU
  if (r->wgpu) { gen_collect_casters(r, o, sc); return; }
#endif
  r->cast_n = 0;
  gen_collect_casters(r, o, sc);
  if (!r->cast_n) return;
  if (!r->gen_align) {
    glGetIntegerv(0x8A34 /* GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT */, &r->gen_align);
    if (r->gen_align <= 0) r->gen_align = 256;
  }
  r->cast_stage_n = 0;
  t3_gen_gl *prev_gl = NULL;
  int prev_off = -1;
  for (int i = 0; i < r->cast_n; i++) {
    struct cast_item *c = &r->cast[i];
    t3_gen_gl *gl = c->gl;
    c->it.gen_gl = gl;
    c->it.gen_off = -1;
    if (gl->draw_group < 0) continue;
    gen_set_aux(r, c->it.material);
    gen_fill_draw(r, gl, c->it.material, c->it.object, false);
    gen_fill_ext(r, gl, c->it.material, c->it.object);   /* (a failure skips the draw, in gen_draw_gl) */
    uint32_t n = gl->bytes[gl->draw_group];
    const uint8_t *blk = gl->mirror[gl->draw_group];
    if (gl == prev_gl && prev_off >= 0 && !memcmp(r->cast_stage + prev_off, blk, n)) { c->it.gen_off = prev_off; continue; }
    size_t off = (r->cast_stage_n + (size_t)r->gen_align - 1) / (size_t)r->gen_align * (size_t)r->gen_align;
    if (off + n > r->cast_stage_cap) {
      r->cast_stage_cap = (off + n) * 2 + 4096;
      r->cast_stage = realloc(r->cast_stage, r->cast_stage_cap);
      T3_CHECK_ALLOC(r->cast_stage);
    }
    memcpy(r->cast_stage + off, blk, n);
    r->cast_stage_n = off + n;
    c->it.gen_off = (int)off;
    prev_gl = gl;
    prev_off = (int)off;
  }
  if (r->cast_stage_n) {
    if (!r->cast_ubo) glGenBuffers(1, &r->cast_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, r->cast_ubo);
    if (r->cast_stage_n > r->cast_ubo_size) r->cast_ubo_size = r->cast_stage_n * 2;
    /* orphaned each time: the GPU may still read the last pass's blocks */
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)r->cast_ubo_size, NULL, GL_STREAM_DRAW);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)r->cast_stage_n, r->cast_stage);
  }
  GLuint keep = r->gen_stream_ubo;
  r->gen_stream_ubo = r->cast_ubo;
  /* the caster's state: the material with r186's shadow overrides, copied
   * once for a run of casters sharing a material */
  t3_material state;
  const t3_material *state_of = NULL;
  for (int i = 0; i < r->cast_n; i++) {
    struct cast_item *c = &r->cast[i];
    const t3_material *src = c->it.material;
    if (src != state_of) {
      static const t3_side flip[3] = { T3_BACK_SIDE, T3_FRONT_SIDE, T3_DOUBLE_SIDE };
      state = *src;
      state.side = src->shadow_side >= 0 ? (t3_side)src->shadow_side : flip[src->side];
      state.transparent = false;
      state.blending = T3_NO_BLENDING;
      state.depth_test = state.depth_write = true;
      state.color_write = true;
      state_of = src;
    }
    gen_draw_gl(r, &c->it, sc, c->gl, &state);
  }
  r->gen_stream_ubo = keep;
  /* the main lists bind gen_ubo again (the slots now hold cast_ubo ranges) */
  for (int i = 0; i < T3_GEN_MAX_GROUPS; i++) if (r->gen_bound_buf[i] == r->cast_ubo) r->gen_bound_buf[i] = 0;
}
static void gen_collect_casters(t3_renderer *r, t3_object *o, const t3_camera *sc) {
  if (!o->visible) return;
  if ((o->type == T3_MESH || o->type == T3_INSTANCED_MESH || o->type == T3_SKINNED_MESH) && o->cast_shadow) {
    t3_mesh *mesh = (t3_mesh *)o;
    t3_geometry *g = mesh->geometry;
    if (g && g->attributes[T3_ATTR_POSITION] && (!o->frustum_culled || in_shadow_frustum(r, o, g))) {
      for (int k = 0; k < (mesh->material_count > 1 && g->group_count ? g->group_count : 1); k++) {
        const t3_group *grp = mesh->material_count > 1 && g->group_count ? &g->groups[k] : NULL;
        int mi = grp ? grp->material_index : 0;
        if (mi < 0 || mi >= mesh->material_count || !mesh->materials[mi]->visible) continue;
        const t3_material *src = mesh->materials[mi];
        t3_gen_gl *gl = gen_caster_program(r, o, g, src);
        if (!gl) continue;
        render_item it = { o, g, (t3_material *)src, grp, 0, 0, 0, 0 };
        it.gen_off = -1;
#ifdef T3_WGPU
        if (r->wgpu) {
          /* overrideMaterial.side = material.shadowSide ?? shadowSide[material.side] */
          static const t3_side flip[3] = { T3_BACK_SIDE, T3_FRONT_SIDE, T3_DOUBLE_SIDE };
          t3_material state = *src;
          state.side = src->shadow_side >= 0 ? (t3_side)src->shadow_side : flip[src->side];
          state.transparent = false;
          state.blending = T3_NO_BLENDING;
          state.depth_test = state.depth_write = true;
          state.color_write = true;
          wgpu_draw_prog(r, &it, sc, gl, &state);
          continue;
        }
#endif
        if (r->cast_n == r->cast_cap) {
          r->cast_cap = r->cast_cap ? r->cast_cap * 2 : 64;
          r->cast = realloc(r->cast, sizeof *r->cast * (size_t)r->cast_cap);
          T3_CHECK_ALLOC(r->cast);
        }
        r->cast[r->cast_n++] = (struct cast_item){ it, gl };
      }
    }
  }
  for (int i = 0; i < o->child_count; i++) gen_collect_casters(r, o->children[i], sc);
}
static void gen_render_shadow_maps(t3_renderer *r, t3_scene *scene) {
  if (!r->shadow_map_enabled || !r->caster_count) return;
  r->rendering_shadows = true;
  if (r->cur_blend != 0) { glDisable(GL_BLEND); r->cur_blend = 0; }
  if (r->cur_scissor != 0) { glDisable(GL_SCISSOR_TEST); r->cur_scissor = 0; }
  for (int i = 0; i < r->caster_count; i++) {
    t3_light *l = r->casters[i];
    t3_light_shadow *sh = l->shadow;
    struct gen_shadow *s = gen_shadow_of(r, l, true);
    if (!s) continue;
    bool point = l->base.type == T3_POINT_LIGHT;
    if (shadow_keep(r, s, sh->map_width, sh->map_height, point)) continue;
    s->rendered = true;
    gen_shadow_storage(r, s, sh->map_width, sh->map_height, point);
    gl_fbo(r, s->fbo);
    tu_forget(s->tex);
    for (int face = 0; face < (point ? 6 : 1); face++) {
      if (point) {
        /* PointShadowNode.renderShadow (WebGL coordinate system): a 90 degree
         * camera per cube face, shadow matrix = translation to the light */
        static const float dirs[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
        static const float ups[6][3] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 } };
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, s->tex, 0);
        gl_viewport(r, 0, 0, sh->map_width, sh->map_width);
        t3_camera *c = sh->camera;
        float far = l->distance ? l->distance : c->far;
        if (far != c->far) { c->far = far; t3_camera_update_projection_matrix(c); }
        t3_vec3 lp = t3_mat4_get_position(&l->base.matrix_world);
        c->base.position = lp;
        c->base.up = t3_v3(ups[face][0], ups[face][1], ups[face][2]);
        t3_object_look_at(c, lp.x + dirs[face][0], lp.y + dirs[face][1], lp.z + dirs[face][2]);
        t3_object_update_matrix_world(c, false);
        t3_mat4_identity(&sh->matrix);
        sh->matrix.e[12] = -lp.x; sh->matrix.e[13] = -lp.y; sh->matrix.e[14] = -lp.z;
        t3_mat4_multiply(&r->shadow_proj_screen, &c->projection_matrix, &c->matrix_world_inverse);
        t3_frustum_from_matrix(&r->shadow_frustum, &r->shadow_proj_screen);
      } else {
        gl_viewport(r, 0, 0, sh->map_width, sh->map_height);
        shadow_update_matrices(r, l);
      }
      if (r->cur_depth_write != 1) { glDepthMask(GL_TRUE); r->cur_depth_write = 1; }
      glClear(GL_DEPTH_BUFFER_BIT);
      for (int k = 0; k < r->gen_n; k++) if (r->gen[k].gl) r->gen[k].gl->render_stamp = 0;   /* frame data = this light's camera */
      gen_casters(r, &scene->base, l->shadow->camera);
    }
  }
  for (int k = 0; k < r->gen_n; k++) if (r->gen[k].gl) r->gen[k].gl->render_stamp = 0;   /* the main camera next */
  r->shadow_units_bound = false;
  r->rendering_shadows = false;
}

static void gen_bind_textures_g(t3_renderer *r, t3_gen_gl *gl, const t3_material *m, t3_geometry *cur_geometry);
static void gen_bind_textures_g(t3_renderer *r, t3_gen_gl *gl, const t3_material *m, t3_geometry *cur_geometry) {
  for (int i = 0; i < gl->n_textures; i++) {
    uint8_t arg = gl->tex[i].arg;
    switch (gl->tex[i].kind) {
    case T3_SRC_MAT: {
      const t3_texture *tx = gen_tex(m, arg);
      if (tx) bind_texture((t3_texture *)tx, i);
      break;
    }
    case T3_SRC_DFG: tu_bind(i, gen_dfg_texture(r), true); break;
    case T3_SRC_VIEWPORT: {   /* transmission's framebuffer copy, this side's */
      t3_render_target *v = r->vp_rt[m && m->side == T3_BACK_SIDE];
      if (v) {
        tu_bind(i, ((texture_gl *)v->texture->_gl)->tex, true);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      }
      break;
    }
    case T3_SRC_SHADOW_DIR: case T3_SRC_SHADOW_SPOT: case T3_SRC_SHADOW_POINT: gen_bind_shadow(r, gl->tex[i].kind, arg, i); break;
    case T3_SRC_EXT: {   /* (gen_fill_ext has checked it is set) */
      t3_texture *tx = gl->ext_tex[arg].tex;
      if (tx->is_cube) bind_cube_texture(tx, i); else bind_texture(tx, i);
      break;
    }
    case T3_SRC_ENV: case T3_SRC_BG: case T3_SRC_PMREM: {
      const t3_texture *aux = gl->tex[i].kind == T3_SRC_ENV ? r->aux.env : gl->tex[i].kind == T3_SRC_BG ? r->aux.bg : r->aux.pm_src;
      if (aux) { if (aux->is_cube) bind_cube_texture((t3_texture *)aux, i); else bind_texture((t3_texture *)aux, i); }
      break;
    }
    case T3_SRC_MORPH: {
      GLuint mt = cur_geometry ? gen_morph_texture(r, cur_geometry) : 0;
      glActiveTexture(GL_TEXTURE0 + (GLenum)i);
      tu_forget_active();
      glBindTexture(0x8C1A, mt);
      break;
    }
    default: break;
    }
  }
}


/* Before the draws: each generated item's program and per-draw block, packed
 * into one staging buffer at the uniform-buffer offset alignment and uploaded
 * with one call. A block equal to the previous one of the same program reuses
 * its offset. */
static void gen_prepare_list(t3_renderer *r, render_list *l, const t3_camera *cam) {
  t3_gen_gl *prev_gl = NULL;
  int prev_off = -1;
  for (int i = 0; i < l->n; i++) {
    render_item *it = &l->items[i];
    it->gen_gl = NULL;
    it->gen_off = -1;
    t3_gen_gl *gl = gen_pick(r, it->material, it->object, it->geometry, it->batch_count > 0);
    if (!gl) continue;
    it->gen_gl = gl;
    if (gl->draw_group < 0) continue;
    gen_set_aux(r, it->material);
    gen_fill_draw(r, gl, it->material, it->object, it->batch_count > 0);
    gen_fill_ext(r, gl, it->material, it->object);   /* (a failure skips the draw, in gen_draw_gl) */
    uint32_t n = gl->bytes[gl->draw_group];
    if (gen_dump()) {
      fprintf(stderr, "[gen] %s draw block:", gl->src->state);
      for (uint32_t q = 0; q < n / 4; q++) fprintf(stderr, " %g", ((float *)gl->mirror[gl->draw_group])[q]);
      fprintf(stderr, "\n");
    }
    const uint8_t *blk = gl->mirror[gl->draw_group];
    if (gl == prev_gl && prev_off >= 0 && !memcmp(r->gen_stage + prev_off, blk, n)) { it->gen_off = prev_off; continue; }
    size_t off = (r->gen_stage_n + (size_t)r->gen_align - 1) / (size_t)r->gen_align * (size_t)r->gen_align;
    if (off + n > r->gen_stage_cap) {
      r->gen_stage_cap = (off + n) * 2 + 4096;
      r->gen_stage = realloc(r->gen_stage, r->gen_stage_cap);
      T3_CHECK_ALLOC(r->gen_stage);
    }
    memcpy(r->gen_stage + off, blk, n);
    r->gen_stage_n = off + n;
    it->gen_off = (int)off;
    prev_gl = gl;
    prev_off = (int)off;
  }
  (void)cam;
}
static void gen_prepare(t3_renderer *r, const t3_camera *cam) {
  if (!r->gen_align) {
    glGetIntegerv(0x8A34 /* GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT */, &r->gen_align);
    if (r->gen_align <= 0) r->gen_align = 256;
  }
  r->gen_stage_n = 0;
  gen_prepare_list(r, &r->opaque, cam);
  gen_prepare_list(r, &r->transparent, cam);
  if (r->backpass.n) {   /* the back-face pass's programs (BackSide) */
    backpass_side(r, T3_BACK_SIDE);
    gen_prepare_list(r, &r->backpass, cam);
    backpass_side(r, T3_FRONT_SIDE);
  }
  for (int i = 0; i < T3_GEN_MAX_GROUPS; i++) r->gen_bound_buf[i] = 0;
  if (!r->gen_stage_n) return;
  if (!r->gen_ubo) glGenBuffers(1, &r->gen_ubo);
  r->gen_stream_ubo = r->gen_ubo;
  glBindBuffer(GL_UNIFORM_BUFFER, r->gen_ubo);
  if (r->gen_stage_n > r->gen_ubo_size) {
    r->gen_ubo_size = r->gen_stage_n * 2;
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)r->gen_ubo_size, NULL, GL_STREAM_DRAW);
  } else {
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)r->gen_ubo_size, NULL, GL_STREAM_DRAW);   /* orphan: the GPU may still read last frame's */
  }
  glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)r->gen_stage_n, r->gen_stage);
}

static void gen_draw(t3_renderer *r, const render_item *it, const t3_camera *cam) {
  t3_gen_gl *gl = it->gen_gl ? it->gen_gl : gen_pick(r, it->material, it->object, it->geometry, it->batch_count > 0);
  if (gl) gen_draw_gl(r, it, cam, gl, it->material);
}
/* state: the material whose render state (side, depth, blending) applies:
 * the item's own, or for a shadow caster a copy with r186's shadow side */
/* transmission's framebuffer copy on GLES: what has been drawn so far (the
 * current framebuffer, resolved when multisampled) into a mipmapped texture
 * of the framebuffer's type, as r186 copyFramebufferToTexture + mipmaps */
static void gl_viewport_copy(t3_renderer *r, int which) {
  if (r->vp_done[which]) return;
  r->vp_done[which] = true;
  int w = r->target ? r->target->width : r->width, h = r->target ? r->target->height : r->height;
  /* r186: the framebuffer's type (an embedder's FBO (set_framebuffer /
   * external target) by what its colour attachment holds) */
  t3_texture_type type = r->target ? r->target->texture->type : T3_UNSIGNED_BYTE_TYPE;
  if (!r->target && r->cur_fbo) {
    if (r->cur_fbo == r->screen_fbo && r->screen_fbo_type) type = (t3_texture_type)r->screen_fbo_type;
    else {
      GLint ct = 0, bits = 0;
      glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &ct);
      glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &bits);
      if (ct == GL_FLOAT) type = bits > 16 ? T3_FLOAT_TYPE : T3_HALF_FLOAT_TYPE;
      if (r->cur_fbo == r->screen_fbo) r->screen_fbo_type = (int)type;
    }
  }
  t3_render_target *v = r->vp_rt[which];
  if (!v || v->width != w || v->height != h || v->texture->type != type) {
    t3_release(v);
    t3_render_target_options o = t3_render_target_options_default();
    o.type = type;
    o.mag_filter = T3_NEAREST;
    o.min_filter = T3_LINEAR_MIPMAP_LINEAR;
    o.generate_mipmaps = true;
    o.depth_buffer = false;
    v = r->vp_rt[which] = t3_render_target_new(w, h, &o);
  }
  GLuint src = r->cur_fbo;
  int scissor = r->cur_scissor;
  rt_gl *g = rt_setup(r, v);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g->fbo);
  glDisable(GL_SCISSOR_TEST);   /* (the blit honours the scissor) */
  glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  if (scissor == 1) glEnable(GL_SCISSOR_TEST);
  r->cur_scissor = scissor;
  glBindFramebuffer(GL_FRAMEBUFFER, src);
  r->fb_known = true;
  r->cur_fbo = src;
  tu_bind(T3_SCRATCH_UNIT, ((texture_gl *)v->texture->_gl)->tex, true);
  glGenerateMipmap(GL_TEXTURE_2D);
}

static void gen_draw_gl(t3_renderer *r, const render_item *it, const t3_camera *cam, t3_gen_gl *gl, const t3_material *state) {
  t3_object *o = it->object;
  const t3_material *m = it->material;
  t3_geometry *g = it->geometry;
  if (gl->src->features & T3_GEN_TRANSM) gl_viewport_copy(r, state->side == T3_BACK_SIDE);
  if (r->cur_program != gl->program) {
    glUseProgram(gl->program);
    r->cur_program = gl->program;
  }
  if (gl->render_stamp != r->stamp) {
    gen_fill_frame(r, gl, cam);
    gl->render_stamp = r->stamp;
  }
  gen_set_aux(r, m);
  bool streamed = it->gen_gl && gl->draw_group >= 0 && it->gen_off >= 0;
  if (!streamed) gen_fill_draw(r, gl, m, o, it->batch_count > 0);
  /* (again when streamed: the block went out with the frame's, but the
   * extension's own groups and textures are bound now) */
  if (!gen_fill_ext(r, gl, m, o)) { r->info.skipped++; return; }
  if (gen_dump()) {
    for (int gi = 0; gi < gl->n_groups; gi++) if (gl->mirror[gi]) {
      fprintf(stderr, "[gen] group %d (%s, %u B):", gi, gl->src->groups[gi]->name, gl->bytes[gi]);
      for (uint32_t q = 0; q < gl->bytes[gi] / 4; q++) fprintf(stderr, " %g", ((float *)gl->mirror[gi])[q]);
      fprintf(stderr, "\n");
    }
  }
  t3_gen_gl_flush(gl);
  for (int i = 0; i < gl->n_groups; i++) {
    if (!gl->ubo[i] || i == gl->draw_group) continue;
    if (r->gen_bound_buf[i] != gl->ubo[i] || r->gen_bound_off[i] != -1) {
      glBindBufferBase(GL_UNIFORM_BUFFER, (GLuint)i, gl->ubo[i]);
      r->gen_bound_buf[i] = gl->ubo[i];
      r->gen_bound_off[i] = -1;
    }
  }
  if (streamed) {
    int dg = gl->draw_group;
    if (r->gen_bound_buf[dg] != r->gen_stream_ubo || r->gen_bound_off[dg] != it->gen_off) {
      glBindBufferRange(GL_UNIFORM_BUFFER, (GLuint)dg, r->gen_stream_ubo, it->gen_off, (GLsizeiptr)gl->bytes[dg]);
      r->gen_bound_buf[dg] = r->gen_stream_ubo;
      r->gen_bound_off[dg] = it->gen_off;
    }
  } else if (gl->draw_group >= 0) {
    /* not prepared (drawn outside render's lists): upload the block itself */
    int dg = gl->draw_group;
    glBindBuffer(GL_UNIFORM_BUFFER, gl->ubo[dg]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)gl->bytes[dg], gl->mirror[dg]);
    glBindBufferBase(GL_UNIFORM_BUFFER, (GLuint)dg, gl->ubo[dg]);
    r->gen_bound_buf[dg] = gl->ubo[dg];
    r->gen_bound_off[dg] = -1;
  }
  /* the extra buffers: bone matrices (morph influences: later), per draw */
  for (int i = 0; i < gl->n_buffers; i++) {
    if (gl->buf[i].kind == T3_SRC_MORPH_INFLUENCES) {
      const t3_mesh *mesh = (const t3_mesh *)o;
      float v[4 * 64] = { 0 };
      int nm = g->morph_count < 64 ? g->morph_count : 64;
      for (int k = 0; k < nm; k++) v[k * 4] = mesh->morph_influences ? mesh->morph_influences[k] : 0;
      glBindBuffer(GL_UNIFORM_BUFFER, gl->buf[i].ubo);
      glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)gl->buf[i].bytes, v);
    }
    if (gl->buf[i].kind == T3_SRC_BONES && o->type == T3_SKINNED_MESH) {
      const t3_skeleton *sk = ((const t3_skinned_mesh *)o)->skeleton;
      glBindBuffer(GL_UNIFORM_BUFFER, gl->buf[i].ubo);
      glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)gl->buf[i].bytes, sk->bone_matrices);
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, gl->buf[i].point, gl->buf[i].ubo);
  }
  gen_bind_textures_g(r, gl, m, g);
  set_material_state(r, state);
  if (it->batch_count) use_batch_geometry(r, o, g, it->batch_offset);
  else use_geometry(r, o, g);
  issue_draw(r, it, o, g);
}

