/* r186's environments for the generated renderer (included by renderer.c
 * after renderer_wgpu.inc): its PMREM generator (PMREMGenerator.js: a cube
 * map or an equirect to cube-UV, a scene rendered to cube-UV, the GGX filter
 * between LODs, the blur), PMREMNode's per-texture cache, and the scene
 * background (Background.js: a sphere drawn first, BackSide, at the far
 * plane). Every pass is r186's own generated program (tools/gen-programs.mjs
 * kinds pmrem*, bg*), drawn through the same paths as a mesh on either
 * backend. */

#define PM_LOD_MIN 4
#define PM_EXTRA_LODS 6
#define PM_MAX_LODS (10 - PM_LOD_MIN + 1 + PM_EXTRA_LODS)

struct pm186 {
  int lod_max, cube_size, n;
  int size_lods[PM_MAX_LODS];
  t3_mesh *lod[PM_MAX_LODS];
  t3_render_target *ping;
  t3_material *mat;        /* the passes' render state: no depth, no blending */
  t3_camera *flat;         /* _flatCamera */
  struct { const t3_texture *src; uint32_t version; t3_render_target *rt; } cache[16];
  int cache_n;
};

static void pm_free_meshes(struct pm186 *p) {
  for (int i = 0; i < p->n; i++) { t3_release(p->lod[i]); p->lod[i] = NULL; }
  p->n = 0;
}
static void pm186_free(t3_renderer *r) {
  struct pm186 *p = r->pm186;
  if (!p) return;
  pm_free_meshes(p);
  t3_release(p->ping);
  t3_release(p->mat);
  t3_release(p->flat);
  for (int i = 0; i < p->cache_n; i++) t3_release(p->cache[i].rt);
  free(p);
  r->pm186 = NULL;
}
static struct pm186 *pm_get(t3_renderer *r) {
  struct pm186 *p = r->pm186;
  if (p) return p;
  p = calloc(1, sizeof *p);
  T3_CHECK_ALLOC(p);
  p->mat = t3_mesh_basic_material_new(0xffffff);
  p->mat->depth_test = p->mat->depth_write = false;
  p->mat->blending = T3_NO_BLENDING;
  p->mat->side = T3_DOUBLE_SIDE;
  p->flat = t3_orthographic_camera_new(-1, 1, 1, -1, 0, 1);
  r->pm186 = p;
  return p;
}

/* _createPlanes: per LOD, the six faces as quads in a 3 x 2 grid of NDC with
 * each vertex's sampling direction (outputDirection, carried in the normal slot) */
static void pm_planes(struct pm186 *p) {
  static const int face_lib[6] = { 3, 1, 5, 0, 4, 2 };
  pm_free_meshes(p);
  int lod = p->lod_max;
  int total = p->lod_max - PM_LOD_MIN + 1 + PM_EXTRA_LODS;
  for (int i = 0; i < total && i < PM_MAX_LODS; i++) {
    int size = 1 << lod;
    p->size_lods[i] = size;
    double texel = 1.0 / (size - 2);
    double mn = -texel, mx = 1 + texel;
    double uv1[12] = { mn, mn, mx, mn, mx, mx, mn, mn, mx, mx, mn, mx };
    float pos[3 * 6 * 6], dir[3 * 6 * 6];
    for (int face = 0; face < 6; face++) {
      float x = (float)((face % 3) * 2.0 / 3.0 - 1), y = face > 2 ? 0.0f : -1.0f;
      float c[18] = { x, y, 0, x + 2.0f / 3, y, 0, x + 2.0f / 3, y + 1, 0, x, y, 0, x + 2.0f / 3, y + 1, 0, x, y + 1, 0 };
      int fi = face_lib[face];
      memcpy(pos + 18 * fi, c, sizeof c);
      for (int v = 0; v < 6; v++) {
        float u = (float)(uv1[v * 2] * 2 - 1), w = (float)(uv1[v * 2 + 1] * 2 - 1);
        float d[3];
        switch (fi) {
        case 0: d[0] = 1; d[1] = w; d[2] = u; break;
        case 1: d[0] = -u; d[1] = 1; d[2] = -w; break;
        case 2: d[0] = -u; d[1] = w; d[2] = 1; break;
        case 3: d[0] = -1; d[1] = w; d[2] = -u; break;
        case 4: d[0] = -u; d[1] = -1; d[2] = w; break;
        default: d[0] = u; d[1] = w; d[2] = -1; break;
        }
        memcpy(dir + (fi * 6 + v) * 3, d, sizeof d);
      }
    }
    t3_geometry *g = t3_geometry_new();
    t3_attribute *ap = t3_attribute_new(T3_FLOAT32, pos, 36, 3), *ad = t3_attribute_new(T3_FLOAT32, dir, 36, 3);
    t3_geometry_set_attribute(g, T3_ATTR_POSITION, ap);
    t3_geometry_set_attribute(g, T3_ATTR_NORMAL, ad);
    t3_release(ap); t3_release(ad);
    p->lod[i] = t3_mesh_new(g, p->mat);
    t3_release(g);
    p->n = i + 1;
    if (lod > PM_LOD_MIN) lod--;
  }
}

/* _createRenderTarget: RGBA half float, linear, no mipmaps, cube-UV mapping */
static t3_render_target *pm_target(int w, int h, bool depth) {
  t3_render_target_options o = t3_render_target_options_default();
  o.type = T3_HALF_FLOAT_TYPE;
  o.mag_filter = o.min_filter = T3_LINEAR;
  o.generate_mipmaps = false;
  o.depth_buffer = depth;
  t3_render_target *rt = t3_render_target_new(w, h, &o);
  rt->texture->mapping = T3_CUBE_UV_REFLECTION_MAPPING;
  rt->scissor_test = true;
  return rt;
}
static void pm_set_size(struct pm186 *p, int cube_size) {
  int lm = 0;
  while ((2 << lm) <= cube_size) lm++;
  if (lm < PM_LOD_MIN) lm = PM_LOD_MIN;   /* (r186 would make an empty LOD list below 16) */
  if (lm > 10) lm = 10;                    /* the table's programs go to z10 (1024) */
  p->lod_max = lm;
  p->cube_size = 1 << lm;
}
static t3_render_target *pm_allocate(struct pm186 *p, bool depth) {
  int w = 3 * (p->cube_size > 112 ? p->cube_size : 112), h = 4 * p->cube_size;
  return pm_target(w, h, depth);
}
static void pm_init(t3_renderer *r, struct pm186 *p, t3_render_target *rt) {
  (void)r;
  if (!p->ping || p->ping->width != rt->width || p->ping->height != rt->height || !p->n || p->size_lods[0] != p->cube_size) {
    t3_release(p->ping);
    p->ping = pm_target(rt->width, rt->height, false);
    pm_planes(p);
  }
}

static void bind_target(t3_renderer *r);
/* one pass: program `name` over LOD mesh `lod` into rt's viewport (x, y, w,
 * h: top-left origin, as r186 sets it on either backend); clear: r186's
 * autoClear for the pass (the whole target on WebGPU, the scissored region
 * on GL, as each backend clears) */
static void pm_pass(t3_renderer *r, struct pm186 *p, t3_render_target *rt, int x, int y, int w, int h, const char *name, int lod, bool clear) {
  const t3_gen_program *src = gen_by_name(r, name);
  if (!src) { char b[96]; snprintf(b, sizeof b, "PMREM: no program %s in the table", name); gen_note(r, b); return; }
  t3_gen_gl *gl = gen_program_p(r, src, false, (t3_gen_params){ 0, 0, 0 });
  if (!gl || !gl->complete) return;
  t3_render_target *saved = r->target;
  r->target = rt;
  rt->viewport[0] = rt->scissor[0] = x; rt->viewport[1] = rt->scissor[1] = y;
  rt->viewport[2] = rt->scissor[2] = w; rt->viewport[3] = rt->scissor[3] = h;
  rt->scissor_test = true;
  t3_mesh *mesh = p->lod[lod];
  render_item it;
  memset(&it, 0, sizeof it);
  it.object = &mesh->base;
  it.geometry = mesh->geometry;
  it.material = p->mat;
  it.gen_off = -1;
  gl->render_stamp = 0;
#ifdef T3_WGPU
  if (r->wgpu) {
    t3_wgpu *wg = r->wgpu;
    t3_wgpu_begin_frame(wg, 4096);
    wgpu_dest D;
    wgpu_destination(r, &D);
    t3_color cc = r->clear_color;
    t3_wgpu_pass P = { .color = D.color, .resolve = D.resolve, .depth = D.depth, .load_color = !clear, .load_depth = true,
                       .clear = { cc.r, cc.g, cc.b, r->clear_alpha }, .vx = x, .vy = y, .vw = w, .vh = h,
                       .scissor = true, .sx = x, .sy = y, .sw = w, .sh = h };
    t3_wgpu_begin_pass(wg, &P);
    wgpu_draw_prog(r, &it, p->flat, gl, p->mat);
    t3_wgpu_end_pass(wg);
    t3_wgpu_end_frame(wg);
    r->target = saved;
    return;
  }
#endif
  bind_target(r);
  if (clear) {
    gl_clear_color(r, r->clear_color.r, r->clear_color.g, r->clear_color.b, r->clear_alpha);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  gen_draw_gl(r, &it, p->flat, gl, p->mat);
  r->target = saved;
}

/* _textureToCubeUV + _applyPMREM (GGX between LODs) */
static void pm_apply(t3_renderer *r, struct pm186 *p, t3_render_target *rt) {
  char name[32];
  snprintf(name, sizeof name, "pmremggx+z%d", p->lod_max);
  for (int i = 1; i < p->n; i++) {
    int in = i - 1, out = i;
    double target = (double)out / (p->n - 1), source = (double)in / (p->n - 1);
    double incr = sqrt(target * target - source * source);
    double adjusted = incr * (target * 1.25);
    int size = p->size_lods[out];
    int x = 3 * size * (out > p->lod_max - PM_LOD_MIN ? out - p->lod_max + PM_LOD_MIN : 0);
    int y = 4 * (p->cube_size - size);
    r->aux.pm_src = rt->texture;
    r->aux.pm_rough = (float)adjusted;
    r->aux.pm_mip = (float)(p->lod_max - in);
    pm_pass(r, p, p->ping, x, y, 3 * size, 2 * size, name, out, false);
    r->aux.pm_src = p->ping->texture;
    r->aux.pm_rough = 0;
    r->aux.pm_mip = (float)(p->lod_max - out);
    pm_pass(r, p, rt, x, y, 3 * size, 2 * size, name, out, false);
  }
  r->aux.pm_src = NULL;
}
static void pm_blur_pass(t3_renderer *r, struct pm186 *p, t3_render_target *in, t3_render_target *out, int lod_in, int lod_out, float sigma) {
  char name[32];
  snprintf(name, sizeof name, "pmremblur+z%d", p->lod_max);
  r->aux.pm_src = in->texture;
  r->aux.pm_sigma = sigma;
  r->aux.pm_mip = (float)(p->lod_max - lod_in);
  int size = p->size_lods[lod_out];
  int x = 3 * size * (lod_out > p->lod_max - PM_LOD_MIN ? lod_out - p->lod_max + PM_LOD_MIN : 0);
  int y = 4 * (p->cube_size - size);
  pm_pass(r, p, out, x, y, 3 * size, 2 * size, name, lod_out, false);
  r->aux.pm_src = NULL;
}

/* fromCubemap / fromEquirectangular (into reuse when it is the right size) */
static t3_render_target *pm186_from_texture(t3_renderer *r, t3_texture *t, t3_render_target *reuse) {
  struct pm186 *p = pm_get(r);
  bool cube = t->is_cube;
  pm_set_size(p, cube ? t->width : t->width / 4);
  t3_render_target *rt = reuse;
  int w = 3 * (p->cube_size > 112 ? p->cube_size : 112), h = 4 * p->cube_size;
  if (!rt || rt->width != w || rt->height != h) rt = pm_allocate(p, false);
  else t3_retain(rt);
  pm_init(r, p, rt);
  r->aux.pm_src = t;
  /* the conversion pass renders with autoClear on */
  pm_pass(r, p, rt, 0, 0, 3 * p->cube_size, 2 * p->cube_size, cube ? "pmremcube" : "pmremequi", 0, r->auto_clear);
  r->aux.pm_src = NULL;
  pm_apply(r, p, rt);
  rt->viewport[0] = rt->viewport[1] = rt->scissor[0] = rt->scissor[1] = 0;
  rt->viewport[2] = rt->scissor[2] = rt->width; rt->viewport[3] = rt->scissor[3] = rt->height;
  rt->scissor_test = false;
  return rt;
}

/* fromScene: the scene drawn six times (a 90 degree camera per face) into the
 * cube-UV layout, an optional blur, then the GGX LODs */
static t3_render_target *pm186_from_scene(t3_renderer *r, t3_scene *scene, float sigma, float near, float far, int size) {
  struct pm186 *p = pm_get(r);
  pm_set_size(p, size);
  t3_render_target *rt = pm_allocate(p, true);
  pm_init(r, p, rt);
  static const float up_sign[6] = { 1, 1, 1, 1, -1, 1 }, fwd[6] = { 1, -1, 1, -1, 1, -1 };
  t3_camera *cc = t3_perspective_camera_new(90, 1, near, far);
  t3_render_target *saved = r->target;
  bool auto_clear = r->auto_clear;
  t3_color clear = r->clear_color;
  float clear_a = r->clear_alpha;
  bool had_bg = scene->has_background;
  /* the background box in the scene's colour (or the clear colour) = a clear to it */
  t3_color bgc = had_bg ? scene->background : r->clear_color;
  r->target = rt;
  rt->viewport[0] = rt->viewport[1] = 0; rt->viewport[2] = rt->width; rt->viewport[3] = rt->height;
  rt->scissor[0] = rt->scissor[1] = 0; rt->scissor[2] = rt->width; rt->scissor[3] = rt->height;
  r->clear_color = bgc; r->clear_alpha = 1;
  t3_renderer_clear(r, true, true, true);
  r->clear_color = clear; r->clear_alpha = clear_a;
  r->auto_clear = false;
  scene->has_background = false;
  for (int i = 0; i < 6; i++) {
    int col = i % 3;
    cc->base.position = t3_v3(0, 0, 0);
    if (col == 0) { cc->base.up = t3_v3(0, up_sign[i], 0); t3_object_look_at(cc, fwd[i], 0, 0); }
    else if (col == 1) { cc->base.up = t3_v3(0, 0, up_sign[i]); t3_object_look_at(cc, 0, fwd[i], 0); }
    else { cc->base.up = t3_v3(0, up_sign[i], 0); t3_object_look_at(cc, 0, 0, fwd[i]); }
    t3_object_update_matrix_world(cc, false);
    int s = p->cube_size;
    rt->viewport[0] = rt->scissor[0] = col * s; rt->viewport[1] = rt->scissor[1] = i > 2 ? s : 0;
    rt->viewport[2] = rt->scissor[2] = s; rt->viewport[3] = rt->scissor[3] = s;
    rt->scissor_test = true;
    t3_renderer_render(r, scene, cc);
  }
  scene->has_background = had_bg;
  r->auto_clear = auto_clear;
  r->target = saved;
  t3_release(cc);
  if (sigma > 0) {
    float bs = (sigma < 3.14159265f ? sigma : 3.14159265f) / 1.41421356f;
    pm_blur_pass(r, p, rt, p->ping, 0, 0, bs);
    pm_blur_pass(r, p, p->ping, rt, 0, 0, bs);
  }
  pm_apply(r, p, rt);
  rt->viewport[0] = rt->viewport[1] = rt->scissor[0] = rt->scissor[1] = 0;
  rt->viewport[2] = rt->scissor[2] = rt->width; rt->viewport[3] = rt->scissor[3] = rt->height;
  rt->scissor_test = false;
  return rt;
}

/* PMREMNode: a cube or equirect texture's PMREM, made once per version */
static const t3_texture *pm186_for(t3_renderer *r, const t3_texture *t) {
  if (!t) return NULL;
  if (t->mapping == T3_CUBE_UV_REFLECTION_MAPPING) return t;
  struct pm186 *p = pm_get(r);
  for (int i = 0; i < p->cache_n; i++)
    if (p->cache[i].src == t) {
      if (p->cache[i].version != t->version) {
        t3_render_target *n = pm186_from_texture(r, (t3_texture *)t, p->cache[i].rt);
        t3_release(p->cache[i].rt);
        p->cache[i].rt = n;
        p->cache[i].version = t->version;
      }
      return p->cache[i].rt->texture;
    }
  if (p->cache_n == (int)(sizeof p->cache / sizeof p->cache[0])) { t3_release(p->cache[0].rt); memmove(p->cache, p->cache + 1, sizeof p->cache[0] * (size_t)--p->cache_n); }
  t3_render_target *rt = pm186_from_texture(r, (t3_texture *)t, NULL);
  p->cache[p->cache_n].src = t;
  p->cache[p->cache_n].version = t->version;
  p->cache[p->cache_n++].rt = rt;
  return rt->texture;
}
/* (only looks: NULL when the PMREM has not been made) */
static const t3_texture *pm186_cached(t3_renderer *r, const t3_texture *t) {
  if (!t) return NULL;
  if (t->mapping == T3_CUBE_UV_REFLECTION_MAPPING) return t;
  struct pm186 *p = r->pm186;
  if (p) for (int i = 0; i < p->cache_n; i++) if (p->cache[i].src == t) return p->cache[i].rt->texture;
  return NULL;
}

/* the env values of a draw (its material, else the scene's environment) */
static void gen_set_aux(t3_renderer *r, const t3_material *m) {
  r->aux.env = NULL;
  r->aux.env_intensity = 1;
  memset(&r->aux.env_rotation, 0, sizeof r->aux.env_rotation);
  if (!m) return;
  bool pbr = m->type == T3_MESH_STANDARD_MATERIAL || m->type == T3_MESH_PHYSICAL_MATERIAL;
  if (m->env_map) {
    r->aux.env = pbr ? pm186_cached(r, m->env_map) : m->env_map;
    r->aux.env_intensity = m->env_map_intensity;
    r->aux.env_rotation = m->env_map_rotation;
  } else if (pbr && r->environment) {
    r->aux.env = pm186_cached(r, r->environment);
    r->aux.env_intensity = r->env_scene ? r->env_scene->environment_intensity : 1;
    if (r->env_scene) r->aux.env_rotation = r->env_scene->environment_rotation;
  }
}

/* before a render's draws: every PMREM the frame samples, made now (a PMREM
 * pass in the middle of a frame would interrupt it) */
static void gen_env_prepare(t3_renderer *r, t3_scene *scene) {
  r->env_scene = scene;
  render_list *ls[2] = { &r->opaque, &r->transparent };
  for (int l = 0; l < 2; l++)
    for (int i = 0; i < ls[l]->n; i++) {
      const t3_material *m = ls[l]->items[i].material;
      if (m->env_map && (m->type == T3_MESH_STANDARD_MATERIAL || m->type == T3_MESH_PHYSICAL_MATERIAL)) pm186_for(r, m->env_map);
    }
  if (r->environment) pm186_for(r, r->environment);
  const t3_texture *bg = scene->background_texture;
  if (bg && (bg->mapping == T3_CUBE_UV_REFLECTION_MAPPING || ((bg->is_cube || bg->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING) && scene->background_blurriness > 0)))
    pm186_for(r, bg);
}

/* Background.js: the scene's background texture on a sphere, drawn first */
static void gen_background(t3_renderer *r, t3_scene *scene, const t3_camera *cam) {
  const t3_texture *bg = scene->background_texture;
  if (!bg) return;
  const char *name;
  const t3_texture *use = bg;
  if (bg->mapping == T3_CUBE_UV_REFLECTION_MAPPING || ((bg->is_cube || bg->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING) && scene->background_blurriness > 0)) {
    name = "bgpmrem";
    use = pm186_cached(r, bg);
  } else if (bg->is_cube) name = "bgcube";
  else if (bg->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING) { gen_note(r, "background: an equirect texture without blurriness (r186 converts it to a cube target)"); return; }
  else name = "bg2d";
  if (!use) return;
  const t3_gen_program *src = gen_by_name(r, name);
  t3_gen_gl *gl = src ? gen_program_p(r, src, false, (t3_gen_params){ 0, 0, 0 }) : NULL;
  if (!gl || !gl->complete) { gen_note(r, "background: no program"); return; }
  if (!r->bg_sphere) {
    t3_geometry *g = t3_sphere_geometry_new(1, 32, 32);
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->side = T3_BACK_SIDE;
    m->depth_test = m->depth_write = false;
    m->blending = T3_NO_BLENDING;
    r->bg_sphere = t3_mesh_new(g, m);
    t3_release(g); t3_release(m);
  }
  t3_mesh *mesh = r->bg_sphere;
  r->aux.bg = use;
  r->aux.bg_intensity = scene->background_intensity;
  r->aux.bg_blur = scene->background_blurriness;
  r->aux.bg_rotation = scene->background_rotation;
  render_item it;
  memset(&it, 0, sizeof it);
  it.object = &mesh->base;
  it.geometry = mesh->geometry;
  it.material = mesh->materials[0];
  it.gen_off = -1;
#ifdef T3_WGPU
  if (r->wgpu) { wgpu_draw_prog(r, &it, cam, gl, it.material); r->aux.bg = NULL; return; }
#endif
  gen_draw_gl(r, &it, cam, gl, it.material);
  r->aux.bg = NULL;
}

/* ── PMREMGenerator (three.h) ── */
struct t3_pmrem_generator { t3_renderer *r; };
t3_pmrem_generator *t3_pmrem_generator_new(t3_renderer *r) {
  t3_pmrem_generator *g = calloc(1, sizeof *g);
  T3_CHECK_ALLOC(g);
  g->r = r;
  return g;
}
void t3_pmrem_generator_destroy(t3_pmrem_generator *g) { free(g); }
t3_render_target *t3_pmrem_from_cubemap(t3_pmrem_generator *g, t3_texture *cube) { return pm186_from_texture(g->r, cube, NULL); }
t3_render_target *t3_pmrem_from_equirectangular(t3_pmrem_generator *g, t3_texture *e) { return pm186_from_texture(g->r, e, NULL); }
t3_render_target *t3_pmrem_from_scene(t3_pmrem_generator *g, t3_scene *scene, float sigma, float near, float far) {
  return pm186_from_scene(g->r, scene, sigma, near, far, 256);
}

/* ── depth passes for embedders (three.h): the shadow pass's caster programs
 * from any camera into the bound framebuffer / an embedder's target ── */
static void depth_skeletons(t3_object *o, void *ctx) {
  t3_renderer *r = ctx;
  if (o->type != T3_SKINNED_MESH || !o->visible) return;
  t3_skeleton *sk = ((t3_skinned_mesh *)o)->skeleton;
  if (sk && sk->frame != r->info.frame + 1 && sk->frame != r->info.frame) {
    t3_skeleton_update(sk);
    sk->frame = r->info.frame + 1;
  }
}
void t3_renderer_render_depth(t3_renderer *r, t3_scene *scene, t3_camera *cam) {
  if (r->wgpu) { gen_note(r, "render_depth: on WebGPU use t3_renderer_render_depth_to (a pass needs its depth view)"); return; }
  if (scene->auto_update) t3_object_update_matrix_world(scene, false);
  if (!cam->base.parent) t3_object_update_matrix_world(cam, false);
  t3_object_traverse(scene, depth_skeletons, r);
  r->rendering_shadows = true;
  if (r->cur_blend != 0) { glDisable(GL_BLEND); r->cur_blend = 0; }
  if (r->cur_depth_write != 1) { glDepthMask(GL_TRUE); r->cur_depth_write = 1; }
  t3_mat4_multiply(&r->shadow_proj_screen, &cam->projection_matrix, &cam->matrix_world_inverse);
  t3_frustum_from_matrix(&r->shadow_frustum, &r->shadow_proj_screen);
  for (int k = 0; k < r->gen_n; k++) if (r->gen[k].gl) r->gen[k].gl->render_stamp = 0;   /* frame data = this camera */
  gen_casters(r, &scene->base, cam);
  for (int k = 0; k < r->gen_n; k++) if (r->gen[k].gl) r->gen[k].gl->render_stamp = 0;
  r->rendering_shadows = false;
}
void t3_renderer_render_depth_to(t3_renderer *r, t3_scene *scene, t3_camera *cam, const t3_external_target *t) {
#ifdef T3_WGPU
  if (r->wgpu) {
    /* one depth-only pass on the caller's depth view (and its encoder, when
     * set), the rect as viewport and scissor */
    t3_wgpu *w = r->wgpu;
    if (scene->auto_update) t3_object_update_matrix_world(scene, false);
    if (!cam->base.parent) t3_object_update_matrix_world(cam, false);
    t3_object_traverse(scene, depth_skeletons, r);
    int pf = r->wg_pass_format, df = r->wg_depth_format, ns = r->wg_samples;
    bool flip = r->wg_flip;
    r->wg_pass_format = 0; r->wg_depth_format = t->depth_format; r->wg_samples = t->samples > 1 ? t->samples : 1; r->wg_flip = t->flip_y;
    r->rendering_shadows = true;
    t3_mat4_multiply(&r->shadow_proj_screen, &cam->projection_matrix, &cam->matrix_world_inverse);
    t3_frustum_from_matrix(&r->shadow_frustum, &r->shadow_proj_screen);
    r->stamp++;
    t3_wgpu_begin_frame(w, 64 * 1024);
    t3_wgpu_pass P = { .depth = (WGPUTextureView)t->depth, .load_depth = t->load_depth, .depth_clear_set = true, .clear_depth = t->clear_depth,
                       .stencil = depth_has_stencil(t->depth_format), .clear_stencil = (uint32_t)t->clear_stencil,
                       .vx = t->x, .vy = t->y, .vw = t->w, .vh = t->h, .scissor = true, .sx = t->x, .sy = t->y, .sw = t->w, .sh = t->h };
    t3_wgpu_begin_pass(w, &P);
    for (int k = 0; k < r->gen_n; k++) if (r->gen[k].gl) r->gen[k].gl->render_stamp = 0;   /* frame data = this camera */
    gen_casters(r, &scene->base, cam);
    t3_wgpu_end_pass(w);
    t3_wgpu_end_frame(w);
    for (int k = 0; k < r->gen_n; k++) if (r->gen[k].gl) r->gen[k].gl->render_stamp = 0;
    r->rendering_shadows = false;
    r->wg_pass_format = pf; r->wg_depth_format = df; r->wg_samples = ns; r->wg_flip = flip;
    return;
  }
#endif
  gl_fbo(r, (GLuint)t->color);
  gl_viewport(r, t->x, t->y, t->w, t->h);
  gl_scissor_box(r, true, t->x, t->y, t->w, t->h);
  if (!t->load_depth) {
    if (r->cur_depth_write != 1) { glDepthMask(GL_TRUE); r->cur_depth_write = 1; }
    glClearDepthf(t->clear_depth);
    glClear(GL_DEPTH_BUFFER_BIT);
    glClearDepthf(1);
  }
  t3_renderer_render_depth(r, scene, cam);
}
