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

/* a state the table lacks, in the generator's syntax (kind+features+lDPSH),
 * remembered once: t3_renderer_generated_missing lists them so a project can
 * add them to tools/gen-states.txt and regenerate */
static void gen_record_missing(const t3_renderer *rc, t3_gen_kind kind, uint64_t f, const uint8_t want[4], const uint8_t *cast) {
  t3_renderer *r = (t3_renderer *)rc;
  static const char *const kinds[] = { "basic", "lambert", "phong", "standard", "physical", "depth", "points", "psprite", "line", "sprite", "normal" };
  static const char *const feats[] = T3_GEN_FEATURE_NAMES;
  char name[256];
  int n = snprintf(name, sizeof name, "%s", kind < (int)(sizeof kinds / sizeof kinds[0]) ? kinds[kind] : "?");
  for (int b = 0; b < (int)(sizeof feats / sizeof feats[0]) && n < (int)sizeof name - 16; b++)
    if (f & (1ull << b)) n += snprintf(name + n, sizeof name - (size_t)n, "+%s", feats[b]);
  if (kind != T3_GEN_BASIC && kind != T3_GEN_NORMAL && kind <= T3_GEN_PHYSICAL)
    n += snprintf(name + n, sizeof name - (size_t)n, "+l%d%d%d%d", want[0], want[1], want[2], want[3]);
  if (cast && (cast[0] != want[0] || cast[1] != want[1] || cast[2] != want[2]))
    snprintf(name + n, sizeof name - (size_t)n, "+s%d%d%d", cast[0], cast[1], cast[2]);
  size_t len = strlen(name);
  if (r->gen_missing && strstr(r->gen_missing, name)) {
    /* already listed (as a whole line) */
    const char *p = r->gen_missing;
    while ((p = strstr(p, name))) { if ((p == r->gen_missing || p[-1] == '\n') && (p[len] == '\n' || !p[len])) return; p += len; }
  }
  size_t old = r->gen_missing ? strlen(r->gen_missing) : 0;
  r->gen_missing = realloc(r->gen_missing, old + len + 2);
  T3_CHECK_ALLOC(r->gen_missing);
  memcpy(r->gen_missing + old, name, len);
  r->gen_missing[old + len] = '\n';
  r->gen_missing[old + len + 1] = 0;
}

/* the table of the renderer's backend */
static const t3_gen_program *gen_tbl(const t3_renderer *r, unsigned *n) {
#ifdef T3_WGPU
  if (r->wgpu) { *n = t3_gen_program_count_wgpu; return t3_gen_programs_wgpu(); }
#endif
  (void)r;
  *n = t3_gen_program_count_gl;
  return t3_gen_programs_gl();
}
static const t3_gen_program *gen_by_name(const t3_renderer *r, const char *name) {
#ifdef T3_WGPU
  if (r->wgpu) return t3_gen_program_by_name_wgpu(name);
#endif
  (void)r;
  return t3_gen_program_by_name_gl(name);
}

/* WGSLNodeBuilder.isUnfilterable: a texture with both filters Nearest is read
 * with textureLoad and the wrap modes baked into the shader (GLSL is the same
 * either way) */
static bool tex_nearest(const t3_texture *t) { return t->mag_filter == T3_NEAREST && t->min_filter == T3_NEAREST; }

/* the table state for an item, or NULL (with *why set) */
static const t3_gen_program *gen_state(const t3_renderer *r, const t3_material *m, const t3_object *o, const t3_geometry *g,
                                       bool batched, const char **why) {
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
  default: *why = "material kind"; return NULL;
  }
  bool lineish = o->type == T3_LINE || o->type == T3_LINE_SEGMENTS || o->type == T3_LINE_LOOP;
  if (kind == T3_GEN_POINTS ? o->type != T3_POINTS : kind == T3_GEN_LINE ? !lineish : kind == T3_GEN_SPRITE ? o->type != T3_SPRITE
      : (o->type != T3_MESH && o->type != T3_INSTANCED_MESH && o->type != T3_SKINNED_MESH)) { *why = "object kind"; return NULL; }
  if (g->morph_count && !g->morph_position) { *why = "morph targets without positions"; return NULL; }
  if (o->type == T3_SKINNED_MESH) {
    const t3_skeleton *sk = ((const t3_skinned_mesh *)o)->skeleton;
    if (!sk || sk->bone_count < 1 || sk->bone_count > 256) { *why = "skeleton missing or over 256 bones (r186's bone-texture path)"; return NULL; }
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
      if (!(e->is_cube || e->mapping == T3_CUBE_UV_REFLECTION_MAPPING || e->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING)) { *why = "envMap mapping"; return NULL; }
      f |= T3_GEN_ENV;
    } else if (kind == T3_GEN_BASIC || kind == T3_GEN_LAMBERT || kind == T3_GEN_PHONG) {
      if (!e->is_cube) { *why = "a Basic / Lambert / Phong envMap that is not a CubeTexture"; return NULL; }
      f |= T3_GEN_CENV;
      if (e->mapping == T3_CUBE_REFRACTION_MAPPING) f |= T3_GEN_REFR;
      if (m->combine == T3_MIX_OPERATION) f |= T3_GEN_MIX;
      else if (m->combine == T3_ADD_OPERATION) f |= T3_GEN_ADD;
    } else { *why = "envMap on this material kind"; return NULL; }
  } else if (pbr && r->environment) {
    const t3_texture *e = r->environment;
    if (!(e->is_cube || e->mapping == T3_CUBE_UV_REFLECTION_MAPPING || e->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING)) { *why = "scene.environment mapping"; return NULL; }
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
    else if (m->map->wrap_s != T3_CLAMP_TO_EDGE || m->map->wrap_t != T3_CLAMP_TO_EDGE) { *why = "map wrapping"; return NULL; }
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
    if (!g->attributes[T3_ATTR_COLOR] || g->attributes[T3_ATTR_COLOR]->item_size != 3) { *why = "vertex colors that are not rgb"; return NULL; }
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
  if (m->wireframe || m->premultiplied_alpha || m->dithering) { *why = "wireframe / premultiplied / dithering"; return NULL; }
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
        if (c && plain_seen) { *why = "a shadow-casting light after a plain one of its type (by id)"; return NULL; }
        if (c) cast[t]++; else plain_seen = true;
      }
    }
    f |= T3_GEN_SHADOW;
    shadow = true;
  }
  /* lights: the exact vector when the table has it, else the smallest one that covers the frame's */
  uint8_t want[4] = { 0, 0, 0, 0 };
  if (kind >= T3_GEN_LAMBERT && kind <= T3_GEN_PHYSICAL) {
    want[0] = (uint8_t)r->ls.num_dir; want[1] = (uint8_t)r->ls.num_point; want[2] = (uint8_t)r->ls.num_spot; want[3] = (uint8_t)r->ls.num_hemi;
  }
  const t3_gen_program *best = NULL;
  int best_cost = 1 << 30;
  unsigned n_all;
  const t3_gen_program *all = gen_tbl(r, &n_all);
  for (unsigned i = 0; i < n_all; i++) {
    const t3_gen_program *p = &all[i];
    if (p->kind != kind || p->features != f || p->extension) continue;
    if (shadow && memcmp(p->cast, cast, 3)) continue;
    bool covers = true;
    int cost = 0;
    for (int k = 0; k < 4; k++) { if (p->lights[k] < want[k] || (shadow && p->lights[k] != want[k])) covers = false; cost += p->lights[k]; }
    if (covers && cost < best_cost) { best = p; best_cost = cost; }
  }
  if (!best) {
    *why = "no table state for this feature / light combination";
    gen_record_missing(r, kind, f, want, shadow ? cast : NULL);
    return NULL;
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
static t3_gen_gl *gen_pick(t3_renderer *r, const t3_material *m, const t3_object *o, const t3_geometry *g, bool batched) {
  const char *why = "";
  const t3_gen_program *src = gen_state(r, m, o, g, batched, &why);
  if (!src) { gen_note(r, why); r->info.skipped++; return NULL; }
  t3_gen_params prm = { 0, 0, 0 };
  if (src->tpl[0] && o->type == T3_SKINNED_MESH) prm.bones = (uint16_t)((const t3_skinned_mesh *)o)->skeleton->bone_count;
  gen_morph_params(src, g, &prm);
  t3_gen_gl *gl = gen_program_p(r, src, r->gen_direct, prm);
  if (!gl || !gl->complete) { r->info.skipped++; return NULL; }
  static int dbg = -1;
  if (dbg < 0) dbg = getenv("T3_GEN_DEBUG") != NULL;
  if (dbg && r->stamp != gl->render_stamp) fprintf(stderr, "[gen] %s lights %d/%d/%d/%d frame ops %d ops %d\n", src->state, r->ls.num_dir, r->ls.num_point, r->ls.num_spot, r->ls.num_hemi, gl->n_frame_ops, gl->n_ops);
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
  for (int k = 0; k < gl->n_frame_ops; k++) {
    const struct t3_gen_op *op = &gl->ops[k];
    uint8_t *b = t3_gen_gl_op(gl, op);
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
          if (cam->base.type == T3_PERSPECTIVE_CAMERA) { p[10] = (float)(-f / (f - n)); p[14] = (float)(-f * n / (f - n)); }
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
  t3_mat4 id;
  t3_mat4_identity(&id);
  const t3_mat4 *world = batched ? &id : &o->matrix_world;
  for (int k = gl->n_frame_ops; k < gl->n_ops; k++) {
    const struct t3_gen_op *op = &gl->ops[k];
    uint8_t *b = t3_gen_gl_op(gl, op);
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
  for (int i = 0; i < r->gen_sh_n; i++) if (r->gen_sh[i].l == l) return &r->gen_sh[i];
  if (!make || r->gen_sh_n == (int)(sizeof r->gen_sh / sizeof r->gen_sh[0])) return NULL;
  struct gen_shadow *s = &r->gen_sh[r->gen_sh_n++];
  memset(s, 0, sizeof *s);
  s->l = l;
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
/* "directional[0]" -> that light's shadow texture on unit */
static void gen_bind_shadow(t3_renderer *r, const char *name, int unit) {
  const light_state *ls = &r->ls;
  int idx = 0;
  const char *br = strchr(name, '[');
  if (br) idx = atoi(br + 1);
  const t3_light *l = !strncmp(name, "directional", 11) ? (idx < ls->num_dir ? ls->dir[idx] : NULL)
                    : !strncmp(name, "spot", 4) ? (idx < ls->num_spot ? ls->spot[idx] : NULL)
                    : !strncmp(name, "point", 5) ? (idx < ls->num_point ? ls->point[idx] : NULL) : NULL;
  struct gen_shadow *s = l ? gen_shadow_of(r, l, false) : NULL;
  if (s && s->tex) { if (s->cube) tu_bind_cube(unit, s->tex, true); else tu_bind(unit, s->tex, true); }
}

/* the caster program for an object (r186 draws the shadow pass with an
 * override NodeMaterial: kind depth, plus the object's deformations) */
static t3_gen_gl *gen_caster_program(t3_renderer *r, const t3_object *o, const t3_geometry *g) {
  if (g->morph_count && (g->morph_normal || o->type != T3_MESH)) return NULL;
  uint64_t f = 0;
  if (g->morph_count) f |= T3_GEN_MORPH;
  if (o->type == T3_INSTANCED_MESH) f |= T3_GEN_INSTBIG;
  if (o->type == T3_SKINNED_MESH) f |= T3_GEN_SKIN;
  /* (the caster program does not depend on the lights: look it up by name) */
  const t3_gen_program *src = gen_by_name(r, f & T3_GEN_INSTBIG ? "depth+instbig" : f & T3_GEN_SKIN ? "depth+skin" : f & T3_GEN_MORPH ? "depth+morph" : "depth");
  if (!src) { gen_record_missing(r, T3_GEN_DEPTH, f, (const uint8_t[4]){ 0, 0, 0, 0 }, NULL); return NULL; }
  t3_gen_params prm = { 0, 0, 0 };
  if (src->tpl[0] && o->type == T3_SKINNED_MESH) prm.bones = (uint16_t)((const t3_skinned_mesh *)o)->skeleton->bone_count;
  gen_morph_params(src, g, &prm);
  t3_gen_gl *gl = gen_program_p(r, src, false, prm);
  return gl && gl->complete ? gl : NULL;
}
static void gen_casters(t3_renderer *r, t3_object *o, const t3_camera *sc) {
  if (!o->visible) return;
  if ((o->type == T3_MESH || o->type == T3_INSTANCED_MESH || o->type == T3_SKINNED_MESH) && o->cast_shadow) {
    t3_mesh *mesh = (t3_mesh *)o;
    t3_geometry *g = mesh->geometry;
    if (g && g->attributes[T3_ATTR_POSITION] && (!o->frustum_culled || in_shadow_frustum(r, o, g))) {
      t3_gen_gl *gl = gen_caster_program(r, o, g);
      for (int k = 0; gl && k < (mesh->material_count > 1 && g->group_count ? g->group_count : 1); k++) {
        const t3_group *grp = mesh->material_count > 1 && g->group_count ? &g->groups[k] : NULL;
        int mi = grp ? grp->material_index : 0;
        if (mi < 0 || mi >= mesh->material_count || !mesh->materials[mi]->visible) continue;
        const t3_material *src = mesh->materials[mi];
        /* overrideMaterial.side = material.shadowSide ?? shadowSide[material.side] */
        static const t3_side flip[3] = { T3_BACK_SIDE, T3_FRONT_SIDE, T3_DOUBLE_SIDE };
        t3_material state = *src;
        state.side = src->shadow_side >= 0 ? (t3_side)src->shadow_side : flip[src->side];
        state.transparent = false;
        state.blending = T3_NO_BLENDING;
        state.depth_test = state.depth_write = true;
        state.color_write = true;
        render_item it = { o, g, (t3_material *)src, grp, 0, 0, 0, 0 };
        it.gen_off = -1;
#ifdef T3_WGPU
        if (r->wgpu) { wgpu_draw_prog(r, &it, sc, gl, &state); continue; }
#endif
        gen_draw_gl(r, &it, sc, gl, &state);
      }
    }
  }
  for (int i = 0; i < o->child_count; i++) gen_casters(r, o->children[i], sc);
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
    const char *src = gl->tex[i].layout->source;
    if (!strcmp(src, "dfg_lut")) { tu_bind(i, gen_dfg_texture(r), true); continue; }
    if (!strcmp(src, "viewport")) {   /* transmission's framebuffer copy, this side's */
      t3_render_target *v = r->vp_rt[m && m->side == T3_BACK_SIDE];
      if (v) {
        tu_bind(i, ((texture_gl *)v->texture->_gl)->tex, true);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      }
      continue;
    }
    if (!strncmp(src, "shadow:", 7)) { gen_bind_shadow(r, src + 7, i); continue; }
    {
      const t3_texture *aux = !strcmp(src, "envMap") || !strcmp(src, "environment") ? r->aux.env
                            : !strcmp(src, "background") ? r->aux.bg : !strcmp(src, "pmrem.source") ? r->aux.pm_src : NULL;
      if (aux) { if (aux->is_cube) bind_cube_texture((t3_texture *)aux, i); else bind_texture((t3_texture *)aux, i); continue; }
    }
    if (!strcmp(src, "morphTexture")) {
      GLuint mt = cur_geometry ? gen_morph_texture(r, cur_geometry) : 0;
      glActiveTexture(GL_TEXTURE0 + (GLenum)i);
      tu_forget_active();
      glBindTexture(0x8C1A, mt);
      continue;
    }
    for (int t = 0; t < T3_GEN_TEX_COUNT; t++)
      if (!strcmp(src, t3_gen_tex_names[t])) {
        const t3_texture *tx = gen_tex(m, t);
        if (tx) bind_texture((t3_texture *)tx, i);
        break;
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
    uint32_t n = gl->bytes[gl->draw_group];
    if (getenv("T3_GEN_DUMP")) {
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
    GLint ct = 0, bits = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &ct);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &bits);
    if (ct == GL_FLOAT) type = bits > 16 ? T3_FLOAT_TYPE : T3_HALF_FLOAT_TYPE;
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
  if (getenv("T3_GEN_DUMP")) {
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
    if (r->gen_bound_buf[dg] != r->gen_ubo || r->gen_bound_off[dg] != it->gen_off) {
      glBindBufferRange(GL_UNIFORM_BUFFER, (GLuint)dg, r->gen_ubo, it->gen_off, (GLsizeiptr)gl->bytes[dg]);
      r->gen_bound_buf[dg] = r->gen_ubo;
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
    const char *srcn = gl->buf[i].source;
    if (srcn && !strcmp(srcn, "morphInfluences")) {
      const t3_mesh *mesh = (const t3_mesh *)o;
      float v[4 * 64] = { 0 };
      int nm = g->morph_count < 64 ? g->morph_count : 64;
      for (int k = 0; k < nm; k++) v[k * 4] = mesh->morph_influences ? mesh->morph_influences[k] : 0;
      glBindBuffer(GL_UNIFORM_BUFFER, gl->buf[i].ubo);
      glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)gl->buf[i].bytes, v);
    }
    if (srcn && !strcmp(srcn, "boneMatrices") && o->type == T3_SKINNED_MESH) {
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

