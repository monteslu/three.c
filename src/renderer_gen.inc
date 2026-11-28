/* r187's generated programs, inside the renderer (included by renderer.c
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

/* reversedDepthBuffer: clip z in [0, 1] with 1 at the near plane, from the GL
 * matrix in p. The z row comes from near and far in double, as Matrix4
 * makePerspective builds it: taken as w - z from the rounded floats, -1 + f / (f - n)
 * cancels and most of the term is rounding error at a large far / near. A
 * projection the application edited keeps its edit, by the remap clip space does. */
static void gen_reversed_projection(const t3_camera *cam, float *p) {
  double n = cam->near, f = cam->far;
  if (cam->base.type == T3_PERSPECTIVE_CAMERA) {
    if (p[2] == 0 && p[6] == 0 && p[10] == (float)(-(f + n) / (f - n)) && p[14] == (float)(-2 * f * n / (f - n))) {
      p[10] = (float)(n / (f - n)); p[14] = (float)(f * n / (f - n));
      return;
    }
    for (int c = 0; c < 4; c++) p[c * 4 + 2] = 0.5f * p[c * 4 + 3] - 0.5f * p[c * 4 + 2];
    return;
  }
  p[10] = (float)(1 / (f - n)); p[14] = (float)(f / (f - n));
}
/* the kinds with a lighting model: their states carry a light vector and may receive shadows */
static bool gen_lit(t3_gen_kind kind) { return (kind >= T3_GEN_LAMBERT && kind <= T3_GEN_PHYSICAL) || kind == T3_GEN_TOON; }
/* a state's name in the generator's syntax (kind+features+lDPSH+sDPS) */
static void gen_state_name(char *name, size_t cap, t3_gen_kind kind, uint64_t f, const uint8_t want[4], const uint8_t *cast, uint8_t ies, uint16_t ext) {
  static const char *const kinds[T3_GEN_TOON + 1] = { "basic", "lambert", "phong", "standard", "physical", "depth", "points", "psprite", "line", "sprite", "normal",
                                                      [T3_GEN_TOON] = "toon" };
  static const char *const feats[] = T3_GEN_FEATURE_NAMES;
  int n = snprintf(name, cap, "%s", kind < (int)(sizeof kinds / sizeof kinds[0]) && kinds[kind] ? kinds[kind] : "?");
  for (int b = 0; b < (int)(sizeof feats / sizeof feats[0]) && n < (int)cap - 16; b++)
    if (f & (1ull << b)) n += snprintf(name + n, cap - (size_t)n, "+%s", feats[b]);
  if (gen_lit(kind))
    n += snprintf(name + n, cap - (size_t)n, "+l%d%d%d%d", want[0], want[1], want[2], want[3]);
  if (cast && (cast[0] != want[0] || cast[1] != want[1] || cast[2] != want[2]))
    n += snprintf(name + n, cap - (size_t)n, "+s%d%d%d", cast[0], cast[1], cast[2]);
  if (ies && n < (int)cap - 8) n += snprintf(name + n, cap - (size_t)n, "+ies%x", ies);
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
  if (r->missing_hook) r->missing_hook(r->missing_user, name, r->gen_cur_obj, r->gen_cur_mat);
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
  gen_state_name(name, sizeof name, kind, f, want, cast, 0, 0);
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
  uint8_t ies;    /* bit k: spot light k has an iesMap */
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
  case T3_MESH_TOON_MATERIAL: kind = T3_GEN_TOON; break;
  case T3_MESH_NORMAL_MATERIAL: kind = T3_GEN_NORMAL; break;
  case T3_POINTS_MATERIAL: kind = T3_GEN_POINTS; break;      /* r187 draws a Points object as 1-pixel GL points */
  case T3_LINE_BASIC_MATERIAL: kind = T3_GEN_LINE; break;
  case T3_SPRITE_MATERIAL: kind = T3_GEN_SPRITE; break;
  default: *why = "material kind"; return false;
  }
  bool lineish = o->type == T3_LINE || o->type == T3_LINE_SEGMENTS || o->type == T3_LINE_LOOP;
  if (kind == T3_GEN_POINTS ? o->type != T3_POINTS : kind == T3_GEN_LINE ? !lineish : kind == T3_GEN_SPRITE ? o->type != T3_SPRITE
      : (o->type != T3_MESH && o->type != T3_INSTANCED_MESH && o->type != T3_SKINNED_MESH && o->type != T3_BATCHED_MESH)) { *why = "object kind"; return false; }
  if (g->morph_count && !g->morph_position) { *why = "morph targets without positions"; return false; }
  if (o->type == T3_SKINNED_MESH) {
    const t3_skeleton *sk = ((const t3_skinned_mesh *)o)->skeleton;
    if (!sk || sk->bone_count < 1 || sk->bone_count > 256) { *why = "skeleton missing or over 256 bones (r187's bone-texture path)"; return false; }
  }
  /* (material / renderer clippingPlanes: r187's node renderer ignores them, its
   * clipping is ClippingGroup; the generated path does the same) */
  uint64_t f = 0;
  /* the environment: Standard / Physical through a PMREM (r187 EnvironmentNode:
   * a cube, an equirect or a PMREM texture; scene.environment when the
   * material has none); Basic / Lambert / Phong a CubeTexture sampled along
   * the reflection or refraction vector (BasicEnvironmentNode) */
  bool pbr = kind == T3_GEN_STANDARD || kind == T3_GEN_PHYSICAL;
  if (m->env_map) {
    const t3_texture *e = m->env_map;
    if (pbr) {
      if (!(e->is_cube || e->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING)) { *why = "envMap mapping"; return false; }
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
    if (!(e->is_cube || e->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING)) { *why = "scene.environment mapping"; return false; }
    f |= T3_GEN_SENV;
  }
  if (m->normal_map) { f |= T3_GEN_NMAP; if (g->attributes[T3_ATTR_TANGENT]) f |= T3_GEN_TAN; }
  if (m->ao_map) f |= T3_GEN_AO;
  if (m->emissive_map) f |= T3_GEN_EMAP;
  if (m->roughness_map) f |= T3_GEN_RMAP;
  if (m->metalness_map) f |= T3_GEN_MMAP;
  if (m->alpha_map) f |= T3_GEN_AMAP;
  if (m->light_map) f |= T3_GEN_LMAP;
  if (kind == T3_GEN_TOON && m->gradient_map) {
    /* three.js: a gradientMap takes NearestFilter for both filters (the program reads it that way) */
    if (!tex_nearest(m->gradient_map)) { *why = "a gradientMap that is not NearestFilter (min and mag)"; return false; }
    if (m->gradient_map->wrap_s != T3_CLAMP_TO_EDGE || m->gradient_map->wrap_t != T3_CLAMP_TO_EDGE) { *why = "gradientMap wrapping"; return false; }
    f |= T3_GEN_GMAP;
  }
  /* texture.channel (r187 RenderObject keys its programs on it) */
  if (m->map && m->map->channel) f |= T3_GEN_C1MAP;
  if (m->normal_map && m->normal_map->channel) f |= T3_GEN_C1NMAP;
  if (m->ao_map && m->ao_map->channel) f |= T3_GEN_C1AO;
  if (m->emissive_map && m->emissive_map->channel) f |= T3_GEN_C1EMAP;
  if (m->roughness_map && m->roughness_map->channel) f |= T3_GEN_C1RMAP;
  if (m->metalness_map && m->metalness_map->channel) f |= T3_GEN_C1MMAP;
  if (m->alpha_map && m->alpha_map->channel) f |= T3_GEN_C1AMAP;
  if (m->light_map && m->light_map->channel) f |= T3_GEN_C1LMAP;
  if (kind == T3_GEN_PHYSICAL) {
    /* r187 useClearcoat / useSheen / useDiffuseRoughness / useIridescence / useAnisotropy / useTransmission */
    if (m->clearcoat > 0) { f |= T3_GEN_CC; if (m->clearcoat_normal_map) f |= T3_GEN_CCN; }
    if (m->sheen > 0) f |= T3_GEN_SHEEN;
    if (m->diffuse_roughness > 0) f |= T3_GEN_EON;
    if (m->iridescence > 0) f |= T3_GEN_IRID;
    if (m->anisotropy > 0) f |= T3_GEN_ANISO;
    if (m->transmission > 0) f |= T3_GEN_TRANSM;
  }
  /* instanced draws take r187's instanced-attribute program whatever the count
   * (its uniform-buffer program bakes the count into the shader) */
  if (o->type == T3_INSTANCED_MESH || batched) f |= T3_GEN_INSTBIG;
  if (o->type == T3_INSTANCED_MESH && ((const t3_instanced_mesh *)o)->instance_color) f |= T3_GEN_ICOL;
  if (o->type == T3_SKINNED_MESH) f |= T3_GEN_SKIN;
  if (o->type == T3_BATCHED_MESH) f |= T3_GEN_BATCH;
  if (g->morph_count) { f |= T3_GEN_MORPH; if (g->morph_normal) f |= T3_GEN_MORPHN; }
  if (m->map) {
    f |= T3_GEN_MAP;
    if (m->map->wrap_s == T3_REPEAT && m->map->wrap_t == T3_REPEAT) f |= T3_GEN_REP;
    else if (m->map->wrap_s != T3_CLAMP_TO_EDGE || m->map->wrap_t != T3_CLAMP_TO_EDGE) { *why = "map wrapping"; return false; }
    /* (a depth map is nearest by default, which its state already implies) */
    if (m->map->format == T3_DEPTH_FORMAT || m->map->format == T3_DEPTH_STENCIL_FORMAT) {
      f |= T3_GEN_DMAP;
#ifdef T3_WGPU
      if (r->wgpu && t3_wgpu_compat(r->wgpu)) f |= T3_GEN_CM;   /* r187 reads it as texture_2d<f32> there */
#endif
    }
    else if (tex_nearest(m->map)) f |= T3_GEN_NEAREST;
  }
  if (m->vertex_colors) {
    /* rgb or rgba (glTF exporters write COLOR_0 as vec4 by default): the program reads the first three
     * components of the attribute, so an rgba one draws with its colour and an opaque alpha (r187's
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
  if (m->flat_shading && (kind == T3_GEN_NORMAL || gen_lit(kind))) f |= T3_GEN_FLAT;
  if (m->wireframe || m->premultiplied_alpha || m->dithering) { *why = "wireframe / premultiplied / dithering"; return false; }
  /* shadows: r187 keys the program on receiveShadow when a light casts; the
   * table's +shadow states have every light of the vector casting */
  bool shadow = false;
  uint8_t cast[3] = { 0, 0, 0 };
  if (r->shadow_map_enabled && o->receive_shadow && gen_lit(kind) &&
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
    if (r->reversed_depth && !r->forward_shadows) f |= T3_GEN_RDEPTH;   /* (only what reads a shadow map differs) */
    shadow = true;
  }
  /* lights: the exact vector when the table has it, else the smallest one that
   * covers the frame's: its extra lights get inert values. With shadows the
   * casters must match exactly (they come first in each type); extra
   * unshadowed lights are covered the same way. */
  uint8_t want[4] = { 0, 0, 0, 0 };
  if (gen_lit(kind)) {
    want[0] = (uint8_t)r->ls.num_dir; want[1] = (uint8_t)r->ls.num_point; want[2] = (uint8_t)r->ls.num_spot; want[3] = (uint8_t)r->ls.num_hemi;
  }
  /* IESSpotLight: the spot lights (by id) that carry an iesMap, the first eight of them */
  uint8_t ies = 0;
  if (gen_lit(kind))
    for (int i = 0; i < r->ls.num_spot && i < 8; i++) if (r->ls.spot[i]->ies_map) ies |= (uint8_t)(1u << i);
  memset(key, 0, sizeof *key);
  key->f = f;
  key->kind = (uint8_t)kind;
  key->shadow = shadow;
  if (shadow) memcpy(key->cast, cast, 3);
  memcpy(key->want, want, 4);
  key->ies = ies;
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

/* ── covering a state no table has (t3_renderer_set_cover) ──────────────
 * The nearest program of the same kind that can draw the item:
 *   exact (the same pixels): more lights of each kind (as for any draw), and
 *     fog the draw has none of, with inert values (a range fog from 1e30 to
 *     2e30, a density of 0: the factor is exactly 0);
 *   approximate (drawn without it): fog, the shadows received (and reversed
 *     depth with them) and the scene's environment map that the program lacks,
 *     and extension differences the embedder's cover_ext accepts.
 * Every other feature must match: a program with a map, skinning, alpha test or
 * a side the draw does not have would read what the draw does not bind. Exact
 * covers win, then the fewest approximations, then the fewest lights. */
static const uint64_t GEN_COVER_DROP = T3_GEN_FOG | T3_GEN_FOG2 | T3_GEN_SHADOW | T3_GEN_RDEPTH | T3_GEN_SENV;
static void gen_record_covered(const t3_renderer *rc, const char *want, const char *used, bool exact) {
  t3_renderer *r = (t3_renderer *)rc;
  char line[400];
  snprintf(line, sizeof line, "%s %s", want, used);
  size_t len = strlen(line);
  for (const char *p = r->gen_covered; p && (p = strstr(p, line)); p += len)
    if ((p == r->gen_covered || p[-1] == '\n') && (p[len] == '\n' || !p[len])) return;
  size_t old = r->gen_covered ? strlen(r->gen_covered) : 0;
  r->gen_covered = realloc(r->gen_covered, old + len + 2);
  T3_CHECK_ALLOC(r->gen_covered);
  memcpy(r->gen_covered + old, line, len);
  r->gen_covered[old + len] = '\n';
  r->gen_covered[old + len + 1] = 0;
  if (r->cover_hook) r->cover_hook(r->cover_user, want, used, exact, r->gen_cur_obj, r->gen_cur_mat);
#ifndef T3_WASMCART
  /* the state still belongs in the table: T3_MISSING_LOG lists it as a skipped one would be */
  const char *log = getenv("T3_MISSING_LOG");
  FILE *lf = log && *log ? fopen(log, "a") : NULL;
  if (lf) { fprintf(lf, "%s\n", want); fclose(lf); }
#endif
}
/* the extension's cover cost: 0 the same, > 0 approximate, < 0 cannot */
static int gen_cover_ext(const t3_renderer *r, uint16_t want, const char *have, bool caster) {
  const char *w = want ? t3__extension_name(want) : NULL;
  if (w ? have && !strcmp(w, have) : !have) return 0;
  if (!r->cover_ext) return -1;
  return r->cover_ext(r->cover_user, w ? w : "", have ? have : "", caster);
}
static const t3_gen_program *gen_cover_find(const t3_renderer *r, const gen_key *key, uint16_t *inert, bool *exact) {
  const uint64_t f = key->f;
  const t3_gen_program *best = NULL;
  long best_cost = -1;
  const t3_gen_table *const *tables;
  int nt = gen_tbls(r, &tables);
  for (int t = 0; t < nt; t++)
    for (unsigned i = 0; i < tables[t]->count; i++) {
      const t3_gen_program *p = &tables[t]->programs[i];
      if (p->kind != key->kind || p->ies != key->ies) continue;
      uint64_t lack = f & ~p->features, extra = p->features & ~f;
      if (lack & ~GEN_COVER_DROP) continue;
      if ((lack & T3_GEN_SHADOW) && (p->features & T3_GEN_RDEPTH)) continue;
      uint16_t in = 0;
      if (extra) {
        /* only fog the draw has none of: it is made inert */
        if ((extra != T3_GEN_FOG && extra != T3_GEN_FOG2) || (f & (T3_GEN_FOG | T3_GEN_FOG2))) continue;
        in = T3_GEN_INERT_FOG;
      }
      if (key->shadow && (p->features & T3_GEN_SHADOW) && memcmp(p->cast, key->cast, 3)) continue;
      int lights = 0;
      bool covers = true;
      for (int k = 0; k < 4; k++) { if (p->lights[k] < key->want[k]) covers = false; lights += p->lights[k]; }
      if (!covers) continue;
      int ec = gen_cover_ext(r, key->ext, p->extension, false);
      if (ec < 0) continue;
      /* (exact covers first, then the fewest approximations: a dropped feature counts as much as an extension's unit) */
      long approx = (long)__builtin_popcountll(lack & ~T3_GEN_RDEPTH) + ec;
      long cost = approx * 4096 + lights;
      if (!best || cost < best_cost) { best = p; best_cost = cost; *inert = in; *exact = approx == 0; }
    }
  return best;
}

/* the table state for a key, or NULL (with *why set). *inert: values the
 * program it returns must leave out (a covering program, t3_gen_params.inert) */
static const t3_gen_program *gen_state_find(const t3_renderer *r, const gen_key *key, const char **why, uint16_t *inert) {
  *inert = 0;
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
      if (p->ies != key->ies) continue;   /* (a program's extra spot lights are plain ones: inert) */
      bool covers = true;
      int cost = 0;
      for (int k = 0; k < 4; k++) { if (p->lights[k] < want[k]) covers = false; cost += p->lights[k]; }
      if (covers && cost < best_cost) { best = p; best_cost = cost; }
    }
  if (!best) {
    *why = "no table state for this feature / light combination";
    char name[256];
    gen_state_name(name, sizeof name, kind, f, want, shadow ? cast : NULL, key->ies, key->ext);
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
    bool exact = false;
    const t3_gen_program *cover = best || !r->cover_on ? NULL : gen_cover_find(r, key, inert, &exact);
    if (cover) {
      gen_record_covered(r, name, cover->state, exact);
      return cover;
    }
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
    char buf[400];
    if (!strncmp(gl->unsupported, "buffer ", 7)) snprintf(buf, sizeof buf, "program %s: %s", src->state, gl->unsupported);
    else snprintf(buf, sizeof buf, "program %s: no op for property %s", src->state, gl->unsupported);
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
    r->gen_cur_obj = o, r->gen_cur_mat = m; /* who asked, for the missing hook */
    uint16_t inert = 0;
    const t3_gen_program *src = gen_state_find(r, &key, &why, &inert);
    r->gen_cur_obj = NULL, r->gen_cur_mat = NULL;
    if (!src) { gen_note(r, why); r->info.skipped++; return NULL; }
    t3_gen_params prm = { 0, 0, 0, inert };
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
  case T3_GEN_TEX_GRADIENT: return m->gradient_map;
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
        if (r->reversed_depth && !r->wgpu) gen_reversed_projection(cam, (float *)b);
#ifdef T3_WGPU
        if (r->wgpu) {
          /* WebGPU clip z is [0, 1]: z' = 0.5 z + 0.5 w (three.js builds this
           * matrix for WebGPUCoordinateSystem) */
          float p[16];
          memcpy(p, cam->projection_matrix.e, 64);
          double n = cam->near, f = cam->far;
          if (cam->base.type == T3_PERSPECTIVE_CAMERA) {
            /* a projection the application edited (an oblique near plane:
             * r187's ReflectorNode replaces the third row) is not the one
             * near / far build: remap its z row as clip space does instead,
             * z' = 0.5 z + 0.5 w, which keeps the edit */
            if (p[2] == 0 && p[6] == 0 && p[10] == (float)(-(f + n) / (f - n)) && p[14] == (float)(-2 * f * n / (f - n))) {
              p[10] = (float)(-f / (f - n)); p[14] = (float)(-f * n / (f - n));
            } else {
              for (int c = 0; c < 4; c++) p[c * 4 + 2] = 0.5f * p[c * 4 + 2] + 0.5f * p[c * 4 + 3];
            }
          }
          else { p[10] = (float)(-1 / (f - n)); p[14] = (float)(-n / (f - n)); }
          if (r->reversed_depth) { memcpy(p, cam->projection_matrix.e, 64); gen_reversed_projection(cam, p); }
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
      /* the point shadow camera's range (far: the light's distance when it has one, as its passes draw) */
      case T3_GEN_SH_NEAR: putf(b, sh->camera->near); break;
      case T3_GEN_SH_FAR: putf(b, l->distance ? l->distance : sh->camera->far); break;
      case T3_GEN_SH_RADIUS: putf(b, sh->radius); break;
      case T3_GEN_SH_INTENSITY: putf(b, sh->intensity); break;
      case T3_GEN_SH_MAP_SIZE: { float v[2] = { (float)sh->map_width, (float)sh->map_height }; memcpy(b, v, 8); break; }
      case T3_GEN_SH_MATRIX: memcpy(b, sh->matrix.e, 64); break;
      }
      break;
    }
    /* a covering program's fog for a draw with none: a factor of exactly 0 (t3_renderer_set_cover) */
    case GOP_FOG_COLOR: if (gl->params.inert & T3_GEN_INERT_FOG) put3(b, &(t3_color){ 0, 0, 0 }, 0); else if (r->fog) put3(b, &r->fog->color, 1); break;
    case GOP_FOG_NEAR: if (gl->params.inert & T3_GEN_INERT_FOG) putf(b, 1e30f); else if (r->fog) putf(b, r->fog->near); break;
    case GOP_FOG_FAR: if (gl->params.inert & T3_GEN_INERT_FOG) putf(b, 2e30f); else if (r->fog) putf(b, r->fog->far); break;
    case GOP_FOG_DENSITY: if (gl->params.inert & T3_GEN_INERT_FOG) putf(b, 0); else if (r->fog) putf(b, r->fog->density); break;
    default: break;
    }
  }
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
    /* PMREMNode.updateFromTexture */
    case GOP_PMREM_MAXLOD: { const t3_texture *t = r->aux.env ? r->aux.env : r->aux.bg; putf(b, t ? (float)(t->mip_levels - 1) : 0); break; }
    case GOP_PMREM_SIZE: { const t3_texture *t = r->aux.env ? r->aux.env : r->aux.bg; putf(b, t ? (float)t->width : 0); break; }
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
    case GOP_PMREM_LODBIAS: putf(b, r->aux.pm_lod_bias); break;
    case GOP_PMREM_SOURCELOD: putf(b, r->aux.pm_source_lod); break;
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
    case GOP_MAT_DIFFUSE_ROUGHNESS: putf(b, m->diffuse_roughness); break;
    case GOP_MAT_IRID: putf(b, m->iridescence); break;
    case GOP_MAT_IRID_IOR: putf(b, m->iridescence_ior); break;
    case GOP_MAT_IRID_THICK_MAX: putf(b, m->iridescence_thickness_range[1]); break;
    case GOP_MAT_ANISO_VEC: {   /* r187 materialAnisotropyVector: anisotropy along anisotropyRotation */
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
const t3_camera *t3_extension_camera(const t3_extension_ctx *c) { return c->r->ext_cam; }
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
  if (gl->ext[i].buf >= 0) {
    /* in an extension buffer (an r187 uniformArray element) */
    if (n < 1 || (size_t)n * 4 > gl->ext[i].len) { *ok = false; return; }
    uint8_t *d = gl->buf[gl->ext[i].buf].mirror + gl->ext[i].off;
    if (memcmp(d, v, (size_t)n * 4)) { memcpy(d, v, (size_t)n * 4); gl->buf[gl->ext[i].buf].dirty = true; }
    return;
  }
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
    if (!ext_name_is(gl->ext[i].name, name) || (gl->ext[i].buf < 0 && !gl->mirror[gl->ext[i].group])) continue;
    ext_uniform_put(gl, i, v, n, &ok);
    if (!ok) return false;
    if (!found++) first = i;
  }
  gl->ext_ck[s].name = name;
  gl->ext_ck[s].idx = (int16_t)(found == 1 ? first : -1);
  if (!found && c->r->ext_miss_hook) c->r->ext_miss_hook(c->r->ext_miss_user, gl->src->state, name, false);
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
  if (c->r->ext_miss_hook) c->r->ext_miss_hook(c->r->ext_miss_user, gl->src->state, name, true);
  return false;
}
/* the renderer's entry for extension id (made on demand); NULL when out of memory */
static struct ext_hook *ext_hook_of(t3_renderer *r, uint16_t id) {
  if (id >= r->ext_hook_cap) {
    int cap = r->ext_hook_cap ? r->ext_hook_cap : 64;
    while (cap <= id) cap *= 2;
    struct ext_hook *grown = realloc(r->ext_hooks, (size_t)cap * sizeof *grown);
    if (!grown) return NULL;
    memset(grown + r->ext_hook_cap, 0, (size_t)(cap - r->ext_hook_cap) * sizeof *grown);
    r->ext_hooks = grown, r->ext_hook_cap = cap;
  }
  return &r->ext_hooks[id];
}
void t3_renderer_set_extension(t3_renderer *r, const char *name, t3_extension_fill fill, void *user) {
  uint16_t id = t3__extension_id(name);
  if (!id) return;
  struct ext_hook *h = ext_hook_of(r, id);
  if (!h) { gen_note(r, "t3_renderer_set_extension: out of memory"); return; }
  h->fill = fill;
  h->user = fill ? user : NULL;
}
void t3_renderer_set_extension_batching(t3_renderer *r, const char *name, bool on) {
  uint16_t id = t3__extension_id(name);
  if (!id) return;
  struct ext_hook *h = ext_hook_of(r, id);
  if (!h) { gen_note(r, "t3_renderer_set_extension_batching: out of memory"); return; }
  h->batch = on;
}
/* run the hook for a draw of an extension program: false (noted) when the
 * extension has no hook or the hook left a texture unset, and the draw is
 * skipped */
static bool gen_fill_ext(t3_renderer *r, t3_gen_gl *gl, const t3_material *m, const t3_object *o) {
  if (!gl->src->extension) return true;
  const struct ext_hook *h = gl->ext_id < r->ext_hook_cap ? &r->ext_hooks[gl->ext_id] : NULL;
  char buf[200];
  if (!h || !h->fill) {
    snprintf(buf, sizeof buf, "program %s: no hook for extension %s (t3_renderer_set_extension)", gl->src->state, gl->src->extension);
    return gen_note(r, buf);
  }
  for (int t = 0; t < gl->n_ext_tex; t++) gl->ext_tex[t].tex = NULL;
  struct t3_extension_ctx c = { r, gl, m, o };
  h->fill(&c, h->user);
  for (int t = 0; t < gl->n_ext_tex; t++) {
    const t3_texture *tx = gl->ext_tex[t].tex;
    if (!tx) {
      snprintf(buf, sizeof buf, "program %s: extension %s left texture %s unset", gl->src->state, gl->src->extension, gl->ext_tex[t].name);
      return gen_note(r, buf);
    }
    /* r187 TextureNode flipY: a texture the GPU drew is stored flipped */
    for (int k = 0; k < gl->ext_tex[t].n_flip; k++)
      if (gl->mirror[gl->ext_tex[t].flip_group[k]]) {
        uint32_t v = tx->gpu_made;
        ext_put(gl, gl->ext_tex[t].flip_group[k], gl->ext_tex[t].flip_off[k], &v, 4);
      }
  }
  return true;
}

/* r187's precomputed DFG LUT as a texture (16 x 16 RG16F, linear, clamp) */
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

/* ── shadows (r187 ShadowNode): a depth texture per casting light, drawn
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
/* a 2D depth texture, or for a point light a cube one (r187 CubeDepthTexture) */
static void gen_shadow_storage(t3_renderer *r, struct gen_shadow *s, int w, int h, bool cube) {
  if (s->tex && s->w == w && s->h == h && s->cube == cube) return;
  if (s->tex && s->cube != cube) { glDeleteTextures(1, &s->tex); s->tex = 0; }
  if (!s->tex) glGenTextures(1, &s->tex);
  if (!s->fbo) glGenFramebuffers(1, &s->fbo);
  s->cube = cube;
  GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
  bool rev = r->reversed_depth && !r->forward_shadows;
  GLenum dfmt = rev ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT24, dtype = rev ? GL_FLOAT : GL_UNSIGNED_INT;
  if (cube) {
    tu_bind_cube(31, s->tex, true);
    for (int f = 0; f < 6; f++)
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, dfmt, w, w, 0, GL_DEPTH_COMPONENT, dtype, NULL);
  } else {
    tu_bind(31, s->tex, true);
    glTexImage2D(GL_TEXTURE_2D, 0, dfmt, w, h, 0, GL_DEPTH_COMPONENT, dtype, NULL);
  }
  bool pcf = r->shadow_map_type != T3_BASIC_SHADOW_MAP;
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, pcf ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, pcf ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, 0x884C /* GL_TEXTURE_COMPARE_MODE */, 0x884E /* GL_COMPARE_REF_TO_TEXTURE */);
  glTexParameteri(target, 0x884D /* GL_TEXTURE_COMPARE_FUNC */, rev ? GL_GEQUAL : GL_LEQUAL);
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

/* the caster program for an object (r187 draws the shadow pass with an
 * override NodeMaterial: kind depth, plus the object's deformations) */
static t3_gen_gl *gen_caster_program_b(t3_renderer *r, const t3_object *o, const t3_geometry *g, const t3_material *m, bool batched);
/* (batched: an auto-instanced group of meshes, drawn with the InstancedMesh caster) */
/* a caster state with an extension no table has (t3_renderer_set_cover): the depth program of the same deformations
 * whose extension cover_ext accepts at the lowest cost (none: the plain caster, if it accepts that), remembered by name */
static const t3_gen_program *gen_cover_caster(t3_renderer *r, const char *name, const char *base, uint16_t ext) {
  for (int i = 0; i < r->cover_memo_n; i++)
    if (!strcmp(r->cover_memo[i].name, name)) return (const t3_gen_program *)r->cover_memo[i].src;
  const t3_gen_program *best = NULL;
  int best_cost = 1 << 30;
  size_t bl = strlen(base);
  const t3_gen_table *const *tables;
  int nt = gen_tbls(r, &tables);
  for (int t = 0; t < nt; t++)
    for (unsigned i = 0; i < tables[t]->count; i++) {
      const t3_gen_program *p = &tables[t]->programs[i];
      /* the same deformations: the base name, alone or with an extension */
      if (p->kind != T3_GEN_DEPTH || strncmp(p->state, base, bl) || (p->state[bl] && strncmp(p->state + bl, "+x", 2))) continue;
      int c = gen_cover_ext(r, ext, p->extension, true);
      if (c >= 0 && c < best_cost) { best = p; best_cost = c; }
    }
  if (best) gen_record_covered(r, name, best->state, best_cost == 0);
  if (r->cover_memo_n == r->cover_memo_cap) {
    r->cover_memo_cap = r->cover_memo_cap ? r->cover_memo_cap * 2 : 16;
    r->cover_memo = realloc(r->cover_memo, (size_t)r->cover_memo_cap * sizeof *r->cover_memo);
    T3_CHECK_ALLOC(r->cover_memo);
  }
  snprintf(r->cover_memo[r->cover_memo_n].name, sizeof r->cover_memo[0].name, "%s", name);
  r->cover_memo[r->cover_memo_n++].src = best;
  return best;
}
void t3_renderer_set_cover(t3_renderer *r, bool on, t3_cover_extension_fn ext, t3_cover_hook hook, void *user) {
  if (r->cover_on == on && r->cover_ext == ext && r->cover_hook == hook && r->cover_user == user) return;   /* (an embedder may set it every frame) */
  r->cover_on = on; r->cover_ext = ext; r->cover_hook = hook; r->cover_user = user;
  r->cover_memo_n = 0;
  /* (the lookups made so far keep no answer the setting changes) */
  free(r->gen_slots);
  r->gen_slots = NULL;
  r->gen_slot_cap = r->gen_slot_n = 0;
}
static t3_gen_gl *gen_caster_program_b(t3_renderer *r, const t3_object *o, const t3_geometry *g, const t3_material *m, bool batched) {
  if (g->morph_count && (g->morph_normal || o->type != T3_MESH)) return NULL;
  uint64_t f = 0;
  if (g->morph_count) f |= T3_GEN_MORPH;
  if (o->type == T3_INSTANCED_MESH || batched) f |= T3_GEN_INSTBIG;
  if (o->type == T3_SKINNED_MESH) f |= T3_GEN_SKIN;
  /* (the caster program does not depend on the lights: look it up by name) */
  if (o->type == T3_BATCHED_MESH) f |= T3_GEN_BATCH;
  const char *base = f & T3_GEN_BATCH ? "depth+batch" : f & T3_GEN_INSTBIG ? "depth+instbig" : f & T3_GEN_SKIN ? "depth+skin" : f & T3_GEN_MORPH ? "depth+morph" : "depth";
  const t3_gen_program *src = NULL;
  r->gen_cur_obj = o, r->gen_cur_mat = m; /* who asked, for the missing hook */
  if (m->extension) {
    /* r187 draws the shadow pass with the material's positionNode (and its
     * colorNode's alpha): an extension material casts with depth+x<name>.
     * A table without it is listed as missing, and the plain caster draws. */
    char name[96];
    snprintf(name, sizeof name, "%s+x%s", base, t3__extension_name(m->extension));
    src = gen_by_name(r, name);
    if (!src && r->cover_on) src = gen_cover_caster(r, name, base, m->extension);
    if (!src) gen_record_missing_name(r, name);
  }
  if (!src) src = gen_by_name(r, base);
  if (!src) gen_record_missing(r, T3_GEN_DEPTH, f, (const uint8_t[4]){ 0, 0, 0, 0 }, NULL);
  r->gen_cur_obj = NULL, r->gen_cur_mat = NULL;
  if (!src) return NULL;
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
/* ── the per-draw block ring (GLES) ──
 * Every render call's per-draw blocks (gen_prepare) and every caster pass's
 * (gen_casters) go to one uniform buffer as slices, one after the other,
 * wrapping at the end. Nothing is orphaned (glBufferData(NULL) a call made the
 * driver find a new store and rebind it, every render call): a slice is
 * written unsynchronized once the fence put after the draws that read the
 * slices before it has signalled, frames later in practice. A slice that would
 * land on one still in use, or one that does not fit, makes a new, larger
 * store (the old one is freed by GL once its draws are done). In a cart the
 * host's GL has no unsynchronized map: slices go up with glBufferSubData,
 * which GL orders against the draws that read them, and no fence is kept. */
#define GL_RING_MIN ((size_t)256 << 10)
#define GL_RING_REGS 64
#define GL_RING_MAX ((size_t)16 << 20)   /* grown past this, a slice in use is waited for instead */
/* T3_TEST_UBO_ORPHAN=1 (test/test_ubo_ring.c's control): every write orphans the store, as before the ring */
static bool gl_ring_orphan(void) {
  static int on = -1;
  if (on < 0) on = getenv("T3_TEST_UBO_ORPHAN") != NULL;
  return on;
}
/* T3_TEST_RING_STREAM=1 (a control): the ring's store is a GL_STREAM_DRAW buffer, as in v0.187.23 */
static bool gl_ring_stream(void) {
  static int on = -1;
  if (on < 0) on = getenv("T3_TEST_RING_STREAM") != NULL;
  return on;
}
/* T3_TEST_RING_SLICE_EACH=1 (a control): each draw writes its own bone matrices and morph influences to the draw
 * ring, one ring write a draw, as in v0.187.23 */
static bool gl_ring_slice_each(void) {
  static int on = -1;
  if (on < 0) on = getenv("T3_TEST_RING_SLICE_EACH") != NULL;
  return on;
}
static void gl_ring_drop_fences(struct gl_ring *g) {
#ifndef T3_WASMCART
  for (int i = 0; i < g->nr; i++) glDeleteSync((GLsync)g->reg[(g->r0 + i) % GL_RING_REGS].fence);
#endif
  g->r0 = g->nr = 0;
}
static void gl_ring_free(struct gl_ring *g) {
  gl_ring_drop_fences(g);
  if (g->buf) glDeleteBuffers(1, &g->buf);
  memset(g, 0, sizeof *g);
}
/* the slices written since the last fence: every draw that reads them has been issued */
static void gl_ring_fence(struct gl_ring *g) {
  if (!g->pend) return;
#ifndef T3_WASMCART
  if (g->nr == GL_RING_REGS) return;   /* (kept pending: a slice that would meet it makes a new store) */
  struct gl_ring_reg *e = &g->reg[(g->r0 + g->nr) % GL_RING_REGS];
  e->lo = g->pend_lo;
  e->hi = g->pend_hi;
  e->fence = (void *)glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  g->nr++;
#endif
  g->pend = false;
}
static bool gl_ring_busy(const struct gl_ring *g, size_t lo, size_t hi) {
#ifndef T3_WASMCART
  if (g->pend && lo < g->pend_hi && g->pend_lo < hi) return true;
  for (int i = 0; i < g->nr; i++) {
    const struct gl_ring_reg *e = &g->reg[(g->r0 + i) % GL_RING_REGS];
    if (lo < e->hi && e->lo < hi) return true;
  }
#else
  (void)g; (void)lo; (void)hi;
#endif
  return false;
}
static void gl_ring_grow(struct gl_ring *g, size_t size) {
  gl_ring_drop_fences(g);
  if (g->buf) glDeleteBuffers(1, &g->buf);
  glGenBuffers(1, &g->buf);
  glBindBuffer(GL_UNIFORM_BUFFER, g->buf);
  /* DYNAMIC, not STREAM: radeonsi keeps a stream buffer in system memory (GTT), so on a discrete card every draw's
   * block and bone matrices were read over PCIe; a dynamic one is in VRAM the CPU can write. T3_TEST_RING_STREAM=1
   * (a control) makes it a stream buffer again. */
  glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)size, NULL, gl_ring_stream() ? GL_STREAM_DRAW : GL_DYNAMIC_DRAW);
  g->size = size;
  g->head = 0;
  g->pend = false;
  g->grows++;
}
/* at the size limit: the slices in the way waited for, oldest first (flushed) */
static void gl_ring_wait(struct gl_ring *g, size_t lo, size_t hi) {
#ifndef T3_WASMCART
  if (g->pend && lo < g->pend_hi && g->pend_lo < hi) {
    if (g->nr == GL_RING_REGS) {   /* (no room for its fence: the oldest waited for first) */
      struct gl_ring_reg *e = &g->reg[g->r0];
      glClientWaitSync((GLsync)e->fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000u);
      glDeleteSync((GLsync)e->fence);
      g->r0 = (g->r0 + 1) % GL_RING_REGS;
      g->nr--;
    }
    gl_ring_fence(g);
  }
  while (g->nr && gl_ring_busy(g, lo, hi)) {
    struct gl_ring_reg *e = &g->reg[g->r0];
    glClientWaitSync((GLsync)e->fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000u);
    glDeleteSync((GLsync)e->fence);
    g->r0 = (g->r0 + 1) % GL_RING_REGS;
    g->nr--;
  }
#else
  (void)g; (void)lo; (void)hi;
#endif
}
/* a slice of n bytes at the alignment, written with data; returns its offset.
 * *moved: the ring's buffer is a new one (bindings to the old name are stale) */
static size_t gl_ring_write(struct gl_ring *g, const void *data, size_t n, size_t align, bool *moved) {
  *moved = false;
#ifndef T3_WASMCART
  while (g->nr) {   /* the slices whose draws are done */
    struct gl_ring_reg *e = &g->reg[g->r0];
    GLenum st = glClientWaitSync((GLsync)e->fence, 0, 0);
    if (st != GL_ALREADY_SIGNALED && st != GL_CONDITION_SATISFIED) break;
    glDeleteSync((GLsync)e->fence);
    g->r0 = (g->r0 + 1) % GL_RING_REGS;
    g->nr--;
  }
#endif
  if (g->pend && g->pend_hi - g->pend_lo >= g->size / 8) gl_ring_fence(g);
  size_t off = (g->head + align - 1) / align * align;
  if (!g->buf || n * 4 > g->size) {
    size_t sz = GL_RING_MIN;
    while (sz < n * 8) sz *= 2;
    if (g->size * 2 > sz) sz = g->size * 2;
    gl_ring_grow(g, sz);
    *moved = true;
    off = 0;
  } else {
    if (off + n > g->size) {   /* wrap: the slices before the end are fenced first */
      gl_ring_fence(g);
      off = 0;
    }
    if (gl_ring_busy(g, off, off + n)) {
      if (g->size < GL_RING_MAX) {
        gl_ring_grow(g, g->size * 2);
        *moved = true;
        off = 0;
      } else
        gl_ring_wait(g, off, off + n);   /* (an embedder that never lets the GPU catch up: no endless growth) */
    }
  }
  if (gl_ring_orphan()) {   /* (the test control: the old way, a new store each write) */
    glBindBuffer(GL_UNIFORM_BUFFER, g->buf);
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)g->size, NULL, GL_STREAM_DRAW);
    off = 0;
  }
  glBindBuffer(GL_UNIFORM_BUFFER, g->buf);
#ifndef T3_WASMCART
  void *p = glMapBufferRange(GL_UNIFORM_BUFFER, (GLintptr)off, (GLsizeiptr)n,
                             GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
  if (p) {
    memcpy(p, data, n);
    glUnmapBuffer(GL_UNIFORM_BUFFER);
  } else
#endif
    glBufferSubData(GL_UNIFORM_BUFFER, (GLintptr)off, (GLsizeiptr)n, data);
  g->head = off + n;
  if (!g->pend) g->pend_lo = off, g->pend = true;
  g->pend_hi = off + n;
  return off;
}
/* a draw's extra buffer i as its program reads it: a skinned mesh's bone matrices, or the morph influences (written
 * into mv, 4 x 64 floats); NULL for any other buffer (an extension's own) */
static const void *gen_buf_slice(const t3_gen_gl *gl, int i, const t3_object *o, const t3_geometry *g, float *mv) {
  if (!gl->buf[i].bytes) return NULL;
  if (gl->buf[i].kind == T3_SRC_MORPH_INFLUENCES) {
    if (gl->buf[i].bytes > sizeof(float) * 4 * 64) return NULL;
    const t3_mesh *mesh = (const t3_mesh *)o;
    memset(mv, 0, sizeof(float) * 4 * 64);
    int nm = g->morph_count < 64 ? g->morph_count : 64;
    for (int k = 0; k < nm; k++) mv[k * 4] = mesh->morph_influences ? mesh->morph_influences[k] : 0;
    return mv;
  }
  if (gl->buf[i].kind == T3_SRC_BONES && o->type == T3_SKINNED_MESH) return ((const t3_skinned_mesh *)o)->skeleton->bone_matrices;
  return NULL;
}
/* GLES: a draw's bone matrices and morph influences packed into the stage its per-draw blocks go to, each slice at
 * the uniform-buffer offset alignment, so they go up with the blocks in one write (one ring write a draw cost a map,
 * an unmap and a fence poll each: with Mesa's glthread every one of them waits for the driver thread). Returns the
 * offset + 1 of the first slice (0: none). Slices equal to the last draw's (a skinned mesh's groups, one after
 * another) reuse its offset. */
static int gen_stage_bufs(t3_renderer *r, uint8_t **stage, size_t *stage_n, size_t *cap, const t3_gen_gl *gl,
                          const t3_object *o, const t3_geometry *g, int *prev_at, size_t *prev_n) {
  if (!gl->n_buffers) return 0;
  size_t a = (size_t)r->gen_align, start = (*stage_n + a - 1) / a * a, end = start;
  float mv[4 * 64];
  int slices = 0;
  for (int i = 0; i < gl->n_buffers; i++) {
    const void *src = gen_buf_slice(gl, i, o, g, mv);
    if (!src) continue;
    size_t off = (end + a - 1) / a * a, n = gl->buf[i].bytes;
    if (off + n > *cap) {
      *cap = (off + n) * 2 + 4096;
      *stage = realloc(*stage, *cap);
      T3_CHECK_ALLOC(*stage);
    }
    memcpy(*stage + off, src, n);
    end = off + n;
    slices++;
  }
  if (!slices) return 0;
  size_t len = end - start;
  if (*prev_at && *prev_n == len && !memcmp(*stage + (*prev_at - 1), *stage + start, len)) return *prev_at;   /* (not kept) */
  *stage_n = end;
  *prev_at = (int)start + 1;
  *prev_n = len;
  return *prev_at;
}
static void gen_collect_casters(t3_renderer *r, t3_object *o, const t3_camera *sc);
/* the caster draws: on GLES collected, their per-draw blocks packed and
 * uploaded with one call, then drawn (one upload a shadow map, not one a
 * draw); WebGPU draws as it walks (its blocks go to the frame stream) */
static void gen_cast_batches(t3_renderer *r, const t3_camera *sc);
static void gen_casters(t3_renderer *r, t3_object *o, const t3_camera *sc) {
  r->ccand_n = 0;
#ifdef T3_WGPU
  if (r->wgpu) { gen_collect_casters(r, o, sc); gen_cast_batches(r, sc); return; }
#endif
  r->cast_n = 0;
  gen_collect_casters(r, o, sc);
  gen_cast_batches(r, sc);
  if (!r->cast_n) return;
  if (!r->gen_align) {
    glGetIntegerv(0x8A34 /* GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT */, &r->gen_align);
    if (r->gen_align <= 0) r->gen_align = 256;
  }
  r->cast_stage_n = 0;
  r->ext_cam = sc;
  t3_gen_gl *prev_gl = NULL;
  int prev_off = -1, prev_at = 0;
  size_t prev_bn = 0;
  for (int i = 0; i < r->cast_n; i++) {
    struct cast_item *c = &r->cast[i];
    t3_gen_gl *gl = c->gl;
    c->it.gen_gl = gl;
    c->it.gen_off = -1;
    c->it.buf_at = gen_stage_bufs(r, &r->cast_stage, &r->cast_stage_n, &r->cast_stage_cap, gl, c->it.object, c->it.geometry, &prev_at, &prev_bn);
    if (gl->draw_group < 0) continue;
    gen_set_aux(r, c->it.material);
    gen_fill_draw(r, gl, c->it.material, c->it.object, c->it.batch_count > 0);
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
  size_t keep_base = r->gen_stream_base, cast_base = 0;
  if (r->cast_stage_n) {
    bool moved;
    cast_base = gl_ring_write(&r->ubo_ring, r->cast_stage, r->cast_stage_n, (size_t)r->gen_align, &moved);
    /* (a new store: every binding to the old name, the main lists' too, is stale) */
    if (moved) for (int i = 0; i < T3_GEN_MAX_GROUPS; i++) r->gen_bound_buf[i] = 0;
  }
  GLuint keep = r->gen_stream_ubo;
  r->gen_stream_ubo = r->ubo_ring.buf;
  r->gen_stream_base = cast_base;
  r->batch_vbo = r->cast_inst_vbo;   /* (batched casters read the pass's own instance stream) */
  /* the caster's state: the material with r187's shadow overrides, copied
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
  /* (the main lists' slice: its draws bind their ranges again, by offset) */
  r->gen_stream_ubo = keep ? r->ubo_ring.buf : 0;
  r->gen_stream_base = keep_base;
  r->batch_vbo = 0;
}
static void gen_cast_one(t3_renderer *r, t3_object *o, t3_geometry *g, const t3_material *src, const t3_group *grp,
                         const t3_camera *sc, int count, int offset);
static void gen_collect_casters(t3_renderer *r, t3_object *o, const t3_camera *sc) {
  if (!o->visible) return;
  if ((o->type == T3_MESH || o->type == T3_INSTANCED_MESH || o->type == T3_SKINNED_MESH || o->type == T3_BATCHED_MESH) && o->cast_shadow) {
    t3_mesh *mesh = (t3_mesh *)o;
    t3_geometry *g = mesh->geometry;
    if (g && g->attributes[T3_ATTR_POSITION] && (!o->frustum_culled || in_shadow_frustum(r, o, g))) {
      for (int k = 0; k < (mesh->material_count > 1 && g->group_count ? g->group_count : 1); k++) {
        const t3_group *grp = mesh->material_count > 1 && g->group_count ? &g->groups[k] : NULL;
        int mi = grp ? grp->material_index : 0;
        if (mi < 0 || mi >= mesh->material_count || !mesh->materials[mi]->visible) continue;
        const t3_material *src = mesh->materials[mi];
        /* a mesh render's lists would auto-instance (renderer.c push_mesh_item): grouped after the walk */
        bool tr = src->transparent || (src->type == T3_MESH_PHYSICAL_MATERIAL && src->transmission > 0);
        if (r->auto_instancing && o->type == T3_MESH && !g->morph_count && !tr && (!src->extension || ext_batches(r, src->extension)) &&
            batchable_matrix(&o->matrix_world) && !r->cast_batch_off) {
          if (r->ccand_n == r->ccand_cap) {
            r->ccand_cap = r->ccand_cap ? r->ccand_cap * 2 : 256;
            r->ccand = realloc(r->ccand, sizeof *r->ccand * (size_t)r->ccand_cap);
            T3_CHECK_ALLOC(r->ccand);
          }
          r->ccand[r->ccand_n] = (struct cast_cand){ o, g, (t3_material *)src, grp, r->ccand_n };
          r->ccand_n++;
          continue;
        }
        gen_cast_one(r, o, g, src, grp, sc, 0, 0);
      }
    }
  }
  for (int i = 0; i < o->child_count; i++) gen_collect_casters(r, o->children[i], sc);
}
/* one caster draw (count > 0: an auto-instanced group, its matrices at offset in the pass's instance stream): drawn
 * now on WebGPU, collected on GLES */
static void gen_cast_one(t3_renderer *r, t3_object *o, t3_geometry *g, const t3_material *src, const t3_group *grp,
                         const t3_camera *sc, int count, int offset) {
  t3_gen_gl *gl = gen_caster_program_b(r, o, g, src, count > 0);
  if (!gl) return;
  render_item it = { o, g, (t3_material *)src, grp, 0, 0, count, offset };
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
    return;
  }
#endif
  if (r->cast_n == r->cast_cap) {
    r->cast_cap = r->cast_cap ? r->cast_cap * 2 : 64;
    r->cast = realloc(r->cast, sizeof *r->cast * (size_t)r->cast_cap);
    T3_CHECK_ALLOC(r->cast);
  }
  r->cast[r->cast_n++] = (struct cast_item){ it, gl };
}
static int cast_cand_cmp(const void *a, const void *b) {
  const struct cast_cand *x = a, *y = b;
  if (x->m != y->m) return (uintptr_t)x->m < (uintptr_t)y->m ? -1 : 1;
  if (x->g != y->g) return (uintptr_t)x->g < (uintptr_t)y->g ? -1 : 1;
  if (x->grp != y->grp) return (uintptr_t)x->grp < (uintptr_t)y->grp ? -1 : 1;
  return x->i - y->i;
}
/* the deferred meshes of a caster pass: a run sharing material, geometry and group is one instanced draw (its world
 * matrices the instance attribute, as render's batches), a lone one an ordinary caster; a run whose instanced caster
 * the tables lack draws one by one */
static void gen_cast_batches(t3_renderer *r, const t3_camera *sc) {
  if (!r->ccand_n) return;
  qsort(r->ccand, (size_t)r->ccand_n, sizeof *r->ccand, cast_cand_cmp);
  r->cinst_n = 0;
  /* the matrices first (one upload a pass), then the draws */
  for (int pass = 0; pass < 2; pass++) {
    int off = 0;
    for (int i = 0; i < r->ccand_n;) {
      int j = i + 1;
      const struct cast_cand *c = &r->ccand[i];
      while (j < r->ccand_n && r->ccand[j].m == c->m && r->ccand[j].g == c->g && r->ccand[j].grp == c->grp) j++;
      int n = j - i;
      bool inst = n > 1 && gen_caster_program_b(r, c->o, c->g, c->m, true);
      if (pass == 0 && inst) {
        if (r->cinst_n + n > r->cinst_cap) {
          while (r->cinst_cap < r->cinst_n + n) r->cinst_cap = r->cinst_cap ? r->cinst_cap * 2 : 1024;
          r->cinst = realloc(r->cinst, (size_t)r->cinst_cap * INST_FLOATS * sizeof(float));
          T3_CHECK_ALLOC(r->cinst);
        }
        for (int k = i; k < j; k++) {
          float *dst = r->cinst + (size_t)r->cinst_n++ * INST_FLOATS;
          memcpy(dst, r->ccand[k].o->matrix_world.e, 16 * sizeof(float));
          dst[16] = dst[17] = dst[18] = 1; dst[19] = 0;
        }
      }
      if (pass == 1) {
        if (inst) gen_cast_one(r, c->o, c->g, c->m, c->grp, sc, n, off), off += n;
        else for (int k = i; k < j; k++) gen_cast_one(r, r->ccand[k].o, r->ccand[k].g, r->ccand[k].m, r->ccand[k].grp, sc, 0, 0);
      }
      i = j;
    }
    if (pass == 0 && r->cinst_n) {
#ifdef T3_WGPU
      if (r->wgpu) { t3_wgpu_floats(r->wgpu, &r->wg_cast_inst, r->cinst, (size_t)r->cinst_n * INST_FLOATS); r->wg_inst_slot = &r->wg_cast_inst; continue; }
#endif
      if (!r->cast_inst_vbo) glGenBuffers(1, &r->cast_inst_vbo);
      glBindBuffer(GL_ARRAY_BUFFER, r->cast_inst_vbo);
      /* (a new store each pass: the GPU may still read the last pass's matrices) */
      glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)r->cinst_n * INST_STRIDE, r->cinst, GL_STREAM_DRAW);
    }
  }
#ifdef T3_WGPU
  r->wg_inst_slot = NULL;
#endif
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
    case T3_SRC_BATCH_INDIRECT: case T3_SRC_BATCH_MATRICES:
      if (r->gen_draw_obj && r->gen_draw_obj->type == T3_BATCHED_MESH) {
        const t3_batched_mesh *bm = (const t3_batched_mesh *)r->gen_draw_obj;
        bind_texture(gl->tex[i].kind == T3_SRC_BATCH_INDIRECT ? bm->indirect_texture : bm->matrices_texture, i);
      }
      break;
    case T3_SRC_IES: if (arg < r->ls.num_spot && r->ls.spot[arg]->ies_map) bind_texture(r->ls.spot[arg]->ies_map, i); break;
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
  r->ext_cam = cam;
  t3_gen_gl *prev_gl = NULL;
  int prev_off = -1, prev_at = 0;
  size_t prev_bn = 0;
  for (int i = 0; i < l->n; i++) {
    render_item *it = &l->items[i];
    it->gen_gl = NULL;
    it->gen_off = -1;
    it->buf_at = 0;
    t3_gen_gl *gl = gen_pick(r, it->material, it->object, it->geometry, it->batch_count > 0);
    if (!gl) continue;
    it->gen_gl = gl;
    it->buf_at = gen_stage_bufs(r, &r->gen_stage, &r->gen_stage_n, &r->gen_stage_cap, gl, it->object, it->geometry, &prev_at, &prev_bn);
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
  bool moved;   /* (the bindings were all forgotten above) */
  r->gen_stream_base = gl_ring_write(&r->ubo_ring, r->gen_stage, r->gen_stage_n, (size_t)r->gen_align, &moved);
  r->gen_stream_ubo = r->ubo_ring.buf;
}

static void gen_draw(t3_renderer *r, const render_item *it, const t3_camera *cam) {
  t3_gen_gl *gl = it->gen_gl ? it->gen_gl : gen_pick(r, it->material, it->object, it->geometry, it->batch_count > 0);
  if (gl) gen_draw_gl(r, it, cam, gl, it->material);
}
/* state: the material whose render state (side, depth, blending) applies:
 * the item's own, or for a shadow caster a copy with r187's shadow side */
/* transmission's framebuffer copy on GLES: what has been drawn so far (the
 * current framebuffer, resolved when multisampled) into a mipmapped texture
 * of the framebuffer's type, as r187 copyFramebufferToTexture + mipmaps */
static void gl_viewport_copy(t3_renderer *r, int which) {
  if (r->vp_done[which]) return;
  r->vp_done[which] = true;
  int w = r->target ? r->target->width : r->width, h = r->target ? r->target->height : r->height;
  /* r187: the framebuffer's type (an embedder's FBO (set_framebuffer /
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
  r->ext_cam = cam;
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
    int at = (int)(r->gen_stream_base + (size_t)it->gen_off);   /* (the slice's offset in the ring + the block's in it) */
    if (r->gen_bound_buf[dg] != r->gen_stream_ubo || r->gen_bound_off[dg] != at) {
      glBindBufferRange(GL_UNIFORM_BUFFER, (GLuint)dg, r->gen_stream_ubo, at, (GLsizeiptr)gl->bytes[dg]);
      r->gen_bound_buf[dg] = r->gen_stream_ubo;
      r->gen_bound_off[dg] = at;
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
  /* the extra buffers: bone matrices and morph influences, per draw, each a slice of the draw ring bound by range
   * (not written into the program's one buffer, which the draw before may still be reading: some drivers then wait
   * for it, between every pair of draws) */
  /* (staged with the per-draw blocks: the slices follow each other from buf_at in the stream this draw binds from) */
  size_t staged = it->buf_at && it->gen_gl == gl && r->gen_stream_ubo && !gl_ring_slice_each() ? r->gen_stream_base + (size_t)(it->buf_at - 1) : 0;
  for (int i = 0; i < gl->n_buffers; i++) {
    float mv[4 * 64];
    const void *slice = gen_buf_slice(gl, i, o, g, mv);
    if (slice && staged) {
      size_t a = (size_t)r->gen_align, at = (staged + a - 1) / a * a;
      glBindBufferRange(GL_UNIFORM_BUFFER, gl->buf[i].point, r->gen_stream_ubo, (GLintptr)at, (GLsizeiptr)gl->buf[i].bytes);
      staged = at + gl->buf[i].bytes;
      continue;
    }
    if (slice) {
      bool moved;
      if (!r->gen_align) {
        glGetIntegerv(0x8A34 /* GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT */, &r->gen_align);
        if (r->gen_align <= 0) r->gen_align = 256;
      }
      size_t off = gl_ring_write(&r->draw_ring, slice, gl->buf[i].bytes, (size_t)r->gen_align, &moved);
      glBindBufferRange(GL_UNIFORM_BUFFER, gl->buf[i].point, r->draw_ring.buf, (GLintptr)off, (GLsizeiptr)gl->buf[i].bytes);
      continue;
    }
    if (gl->buf[i].mirror && gl->buf[i].dirty) {
      /* an extension's buffer, as its hook left it for this draw */
      glBindBuffer(GL_UNIFORM_BUFFER, gl->buf[i].ubo);
      glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)gl->buf[i].bytes, gl->buf[i].mirror);
      gl->buf[i].dirty = false;
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, gl->buf[i].point, gl->buf[i].ubo);
  }
  r->gen_draw_obj = o;
  gen_bind_textures_g(r, gl, m, g);
  set_material_state(r, state);
  if (it->batch_count) use_batch_geometry(r, o, g, it->batch_offset);
  else use_geometry(r, o, g);
  issue_draw(r, it, o, g);
}

