/* r187's environments for the generated renderer (included by renderer.c
 * after renderer_wgpu.inc): its PMREM generator (PMREMGenerator.js: a cube
 * map, an equirect or a scene into a mipmapped source cube, the blur, then a
 * cube render target's GGX-filtered levels), PMREMNode's per-texture cache, and the scene
 * background (Background.js: a sphere drawn first, BackSide, at the far
 * plane). Every pass is r187's own generated program (tools/gen-programs.mjs
 * kinds pmrem*, bg*), drawn through the same paths as a mesh on either
 * backend. */

/* PMREMGenerator's constants */
#define PM_MIN_SIZE 256
#define PM_LOD_MIN 3
#define PM_GGX_SAMPLES 256
#define PM_INTEGRATION_SIZE 16
#define PM_INTEGRATION_LEVELS 3

struct pm187 {
  int cube_size;
  t3_render_target *source;     /* _sourceTarget: the cube the levels are filtered from, mipmapped */
  t3_mesh *box;                 /* _boxMesh */
  t3_camera *faces[6];          /* _cubeCamera's cameras, this backend's */
  struct { const t3_texture *src; uint32_t version; t3_render_target *rt; } cache[16];
  int cache_n;
};

static void pm187_free(t3_renderer *r) {
  struct pm187 *p = r->pm187;
  if (!p) return;
  t3_release(p->source);
  t3_release(p->box);
  for (int i = 0; i < 6; i++) t3_release(p->faces[i]);
  for (int i = 0; i < p->cache_n; i++) t3_release(p->cache[i].rt);
  free(p);
  r->pm187 = NULL;
}
/* CubeCamera.updateCoordinateSystem: what each face looks at, its up */
static void pm_look(t3_camera *c, bool webgpu, int face, t3_vec3 at) {
  static const float gl_dir[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
  static const float gl_up[6][3] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };
  static const float wg_dir[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
  static const float wg_up[6][3] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 } };
  const float *d = webgpu ? wg_dir[face] : gl_dir[face], *u = webgpu ? wg_up[face] : gl_up[face];
  c->base.position = at;
  c->base.up = t3_v3(u[0], u[1], u[2]);
  t3_object_look_at(c, at.x + d[0], at.y + d[1], at.z + d[2]);
  t3_object_update_matrix_world(c, false);
}
static struct pm187 *pm_get(t3_renderer *r) {
  struct pm187 *p = r->pm187;
  if (p) return p;
  p = calloc(1, sizeof *p);
  T3_CHECK_ALLOC(p);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->depth_test = m->depth_write = false;
  m->blending = T3_NO_BLENDING;
  m->side = T3_BACK_SIDE;
  t3_geometry *g = t3_box_geometry_new(5, 5, 5, 1, 1, 1);
  p->box = t3_mesh_new(g, m);
  t3_release(g); t3_release(m);
  t3_object_update_matrix_world(p->box, false);
  for (int i = 0; i < 6; i++) {
    p->faces[i] = t3_perspective_camera_new(-90, 1, 1, 10);
    pm_look(p->faces[i], r->wgpu != NULL, i, t3_v3(0, 0, 0));
  }
  r->pm187 = p;
  return p;
}

/* _createRenderTarget: a half-float cube, linear; the PMREM's levels allocated
 * by hand (no mipmap generation), the source's generated */
static t3_render_target *pm_target(int size, bool generate_mipmaps, bool depth) {
  t3_render_target_options o = t3_render_target_options_default();
  o.type = T3_HALF_FLOAT_TYPE;
  o.mag_filter = T3_LINEAR;
  o.min_filter = T3_LINEAR_MIPMAP_LINEAR;
  o.generate_mipmaps = generate_mipmaps;
  o.depth_buffer = depth;
  return t3_cube_render_target_new(size, &o);
}
static void pm_set_size(struct pm187 *p, int cube_size) {
  int s = 1;
  while (s * 2 <= cube_size) s *= 2;
  p->cube_size = s > PM_MIN_SIZE ? s : PM_MIN_SIZE;
}
/* log2(size) - LOD_MIN + 1 levels */
static int pm_levels(int size) {
  int n = 1;
  for (int s = size; s > 1 << PM_LOD_MIN; s >>= 1) n++;
  return n;
}
/* the target to draw into: reuse when it is this size's PMREM, else a new one (a new reference either way) */
static t3_render_target *pm_allocate(struct pm187 *p, t3_render_target *reuse) {
  if (reuse && reuse->is_cube && reuse->width == p->cube_size && reuse->texture->mip_levels == pm_levels(p->cube_size)) return t3_retain(reuse);
  t3_render_target *rt = pm_target(p->cube_size, false, false);
  rt->texture->mip_levels = pm_levels(p->cube_size);
  return rt;
}
static t3_render_target *pm_source(struct pm187 *p, bool depth) {
  t3_render_target *s = p->source;
  if (!s || s->width != p->cube_size || (depth && !s->depth_buffer)) {
    t3_release(s);
    p->source = pm_target(p->cube_size, true, depth);
  }
  return p->source;
}

static void bind_target(t3_renderer *r);
/* one box drawn with program name into face and level of rt, with a face's
 * camera (_renderCube) */
static void pm_pass(t3_renderer *r, struct pm187 *p, t3_render_target *rt, int face, int lod, const char *name) {
  const t3_gen_program *src = gen_by_name(r, name);
  if (!src) { char b[96]; snprintf(b, sizeof b, "PMREM: no program %s in the table", name); gen_note(r, b); return; }
  t3_gen_gl *gl = gen_program_p(r, src, false, (t3_gen_params){ 0, 0, 0 });
  if (!gl || !gl->complete) return;
  t3_render_target *saved = r->target;
  t3_renderer_set_render_target_ex(r, rt, face, lod);
  t3_mesh *mesh = p->box;
  t3_material *mat = mesh->materials[0];
  t3_camera *cam = p->faces[face];
  render_item it;
  memset(&it, 0, sizeof it);
  it.object = &mesh->base;
  it.geometry = mesh->geometry;
  it.material = mat;
  it.gen_off = -1;
  /* a stamp of its own: before the renderer's first render the stamp is 0,
   * every program's too, and the box would draw with no camera */
  r->stamp++;
#ifdef T3_WGPU
  if (r->wgpu) {
    t3_wgpu *wg = r->wgpu;
    t3_wgpu_begin_frame(wg, 4096);
    wgpu_dest D;
    wgpu_destination(r, &D);
    int s = rt->width >> lod;
    t3_color cc = r->clear_color;
    t3_wgpu_pass P = { .color = D.color, .resolve = D.resolve, .depth = D.depth, .load_color = false, .load_depth = false,
                       .clear = { cc.r, cc.g, cc.b, r->clear_alpha }, .vw = s, .vh = s };
    t3_wgpu_begin_pass(wg, &P);
    wgpu_draw_prog(r, &it, cam, gl, mat);
    t3_wgpu_end_pass(wg);
    t3_wgpu_end_frame(wg);
    r->target = saved;
    return;
  }
#endif
  bind_target(r);
  gen_draw_gl(r, &it, cam, gl, mat);
  r->target = saved;
}
/* all six faces of a level */
static void pm_cube(t3_renderer *r, struct pm187 *p, t3_render_target *rt, int lod, const char *name) {
  for (int f = 0; f < 6; f++) pm_pass(r, p, rt, f, lod, name);
}
/* the source's mip chain, as the last face of a render into it builds it */
static void pm_source_mips(t3_renderer *r, t3_render_target *s) {
  s->active_face = 5; s->active_mip = 0;
#ifdef T3_WGPU
  if (r->wgpu) { t3_wgpu_render_target_done(r->wgpu, s); return; }
#endif
  t3_render_target *saved = r->target;
  r->target = s;
  bind_target(r);
  rt_finish(r, s);
  r->target = saved;
}

/* _applyPMREM: every level from the source, GGX sampled on the sharp ones and
 * integrated over a small source mip on the rough ones */
static void pm_apply(t3_renderer *r, struct pm187 *p, t3_render_target *rt) {
  int size = p->cube_size, max_lod = rt->texture->mip_levels - 1;
  float log2_size = log2f((float)size);
  r->aux.pm_src = p->source->texture;
  r->aux.pm_source_lod = log2f((float)size / PM_INTEGRATION_SIZE);
  for (int lod = 0; lod <= max_lod; lod++) {
    double rough = max_lod > 0 ? 1 - sqrt(1 - (double)lod / max_lod) : 0;   /* lodToRoughness */
    r->aux.pm_rough = (float)rough;
    if (lod > max_lod - PM_INTEGRATION_LEVELS) pm_cube(r, p, rt, lod, "pmremintegration");
    else {
      /* for V = N, pdf( L ) = D( H ) / 4: the source texel's solid angle matched to 1 / ( GGX_SAMPLES * pdf ), a half-mip bias */
      r->aux.pm_lod_bias = rough > 0 ? (float)(log2_size + 0.5 * log2(6 / (PM_GGX_SAMPLES * pow(rough, 4))) + 0.5) : 0;
      pm_cube(r, p, rt, lod, "pmremggx");
    }
  }
  r->aux.pm_src = NULL;
  rt->active_face = rt->active_mip = 0;
  rt->viewport[2] = rt->viewport[3] = rt->scissor[2] = rt->scissor[3] = rt->width;
}

/* fromCubemap / fromEquirectangular (into reuse when it is the right size) */
static t3_render_target *pm187_from_texture(t3_renderer *r, t3_texture *t, t3_render_target *reuse) {
  struct pm187 *p = pm_get(r);
  bool cube = t->is_cube;
  pm_set_size(p, cube ? t->width : t->width / 4);
  t3_render_target *rt = pm_allocate(p, reuse);
  t3_render_target *s = pm_source(p, false);
  r->aux.pm_src = t;
  pm_cube(r, p, s, 0, cube ? "pmremcube" : "pmremequi");
  r->aux.pm_src = NULL;
  pm_source_mips(r, s);
  pm_apply(r, p, rt);
  return rt;
}

/* fromScene: the scene drawn into the source's six faces (a -90 degree camera
 * per face, at position), an optional blur, then the levels. reuse: r187's
 * options.renderTarget */
static t3_render_target *pm187_from_scene(t3_renderer *r, t3_scene *scene, float sigma, float near, float far, int size, t3_vec3 at,
                                          t3_render_target *reuse) {
  struct pm187 *p = pm_get(r);
  pm_set_size(p, size);
  t3_render_target *rt = pm_allocate(p, reuse);
  t3_render_target *s = pm_source(p, true);
  t3_camera *cc = t3_perspective_camera_new(-90, 1, near, far);
  t3_render_target *saved = r->target;
  bool auto_clear = r->auto_clear, had_bg = scene->has_background;
  t3_color bg = scene->background;
  /* every face cleared, to the scene's background or the clear colour */
  r->auto_clear = true;
  if (!had_bg && !scene->background_texture) { scene->has_background = true; scene->background = r->clear_color; }
  for (int i = 0; i < 6; i++) {
    pm_look(cc, r->wgpu != NULL, i, at);
    t3_renderer_set_render_target_ex(r, s, i, 0);
    t3_renderer_render(r, scene, cc);
  }
  scene->has_background = had_bg;
  scene->background = bg;
  r->auto_clear = auto_clear;
  r->target = saved;
  t3_release(cc);
  if (sigma > 0) {
    /* the variance split between two passes, sigma clamped to the sphere's largest angle (_blur) */
    r->aux.pm_sigma = (sigma < 3.14159265f ? sigma : 3.14159265f) / 1.41421356f;
    r->aux.pm_src = s->texture;
    pm_cube(r, p, rt, 0, "pmremblur");
    r->aux.pm_src = rt->texture;
    pm_cube(r, p, s, 0, "pmremblur");
    r->aux.pm_src = NULL;
    pm_source_mips(r, s);
  }
  pm_apply(r, p, rt);
  return rt;
}

/* PMREMNode: a cube or equirect texture's PMREM, made once per version */
static const t3_texture *pm187_for(t3_renderer *r, const t3_texture *t) {
  if (!t) return NULL;
  if (t->mip_levels > 0) return t;
  struct pm187 *p = pm_get(r);
  for (int i = 0; i < p->cache_n; i++)
    if (p->cache[i].src == t) {
      if (p->cache[i].version != t->version) {
        t3_render_target *n = pm187_from_texture(r, (t3_texture *)t, p->cache[i].rt);
        t3_release(p->cache[i].rt);
        p->cache[i].rt = n;
        p->cache[i].version = t->version;
      }
      return p->cache[i].rt->texture;
    }
  if (p->cache_n == (int)(sizeof p->cache / sizeof p->cache[0])) { t3_release(p->cache[0].rt); memmove(p->cache, p->cache + 1, sizeof p->cache[0] * (size_t)--p->cache_n); }
  t3_render_target *rt = pm187_from_texture(r, (t3_texture *)t, NULL);
  p->cache[p->cache_n].src = t;
  p->cache[p->cache_n].version = t->version;
  p->cache[p->cache_n++].rt = rt;
  return rt->texture;
}
/* (only looks: NULL when the PMREM has not been made) */
static const t3_texture *pm187_cached(t3_renderer *r, const t3_texture *t) {
  if (!t) return NULL;
  if (t->mip_levels > 0) return t;
  struct pm187 *p = r->pm187;
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
    r->aux.env = pbr ? pm187_cached(r, m->env_map) : m->env_map;
    r->aux.env_intensity = m->env_map_intensity;
    r->aux.env_rotation = m->env_map_rotation;
  } else if (pbr && r->environment) {
    r->aux.env = pm187_cached(r, r->environment);
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
      if (m->env_map && (m->type == T3_MESH_STANDARD_MATERIAL || m->type == T3_MESH_PHYSICAL_MATERIAL)) pm187_for(r, m->env_map);
    }
  if (r->environment) pm187_for(r, r->environment);
  const t3_texture *bg = scene->background_texture;
  if (bg && (bg->mip_levels > 0 || ((bg->is_cube || bg->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING) && scene->background_blurriness > 0)))
    pm187_for(r, bg);
}

/* Background.js: the scene's background texture on a sphere, drawn first */
static void gen_background(t3_renderer *r, t3_scene *scene, const t3_camera *cam) {
  const t3_texture *bg = scene->background_texture;
  if (!bg) return;
  const char *name;
  const t3_texture *use = bg;
  if (bg->mip_levels > 0 || ((bg->is_cube || bg->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING) && scene->background_blurriness > 0)) {
    name = "bgpmrem";
    use = pm187_cached(r, bg);
  } else if (bg->is_cube) name = "bgcube";
  else if (bg->mapping == T3_EQUIRECTANGULAR_REFLECTION_MAPPING) { gen_note(r, "background: an equirect texture without blurriness (r187 converts it to a cube target)"); return; }
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
t3_render_target *t3_pmrem_from_cubemap(t3_pmrem_generator *g, t3_texture *cube) { return pm187_from_texture(g->r, cube, NULL); }
t3_render_target *t3_pmrem_from_equirectangular(t3_pmrem_generator *g, t3_texture *e) { return pm187_from_texture(g->r, e, NULL); }
t3_render_target *t3_pmrem_from_cubemap_into(t3_pmrem_generator *g, t3_texture *cube, t3_render_target *rt) {
  return pm187_from_texture(g->r, cube, rt);
}
t3_render_target *t3_pmrem_from_equirectangular_into(t3_pmrem_generator *g, t3_texture *e, t3_render_target *rt) {
  return pm187_from_texture(g->r, e, rt);
}
t3_render_target *t3_pmrem_from_scene(t3_pmrem_generator *g, t3_scene *scene, float sigma, float near, float far) {
  return pm187_from_scene(g->r, scene, sigma, near, far, 256, t3_v3(0, 0, 0), NULL);
}
t3_render_target *t3_pmrem_from_scene_ex(t3_pmrem_generator *g, t3_scene *scene, float sigma, float near, float far,
                                         const t3_pmrem_scene_options *o) {
  if (!o) return t3_pmrem_from_scene(g, scene, sigma, near, far);
  return pm187_from_scene(g->r, scene, sigma, near, far, o->size > 0 ? o->size : 256, o->position, o->render_target);
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
  draw_epoch++;
  if (r->wgpu) { gen_note(r, "render_depth: on WebGPU use t3_renderer_render_depth_to (a pass needs its depth view)"); return; }
  if (scene->auto_update) t3_object_update_matrix_world(scene, false);
  if (!cam->base.parent) t3_object_update_matrix_world(cam, false);
  t3_object_traverse(scene, depth_skeletons, r);
  r->rendering_shadows = true;
  if (r->cur_blend != 0) { glDisable(GL_BLEND); r->cur_blend = 0; }
  if (r->cur_depth_write != 1) { glDepthMask(GL_TRUE); r->cur_depth_write = 1; }
  t3_mat4_multiply(&r->shadow_proj_screen, &cam->projection_matrix, &cam->matrix_world_inverse);
  t3_frustum_from_matrix(&r->shadow_frustum, &r->shadow_proj_screen);
  /* a stamp of its own, as the WebGPU path: before the renderer's first render the
   * stamp is 0, every program's too, and the casters drew with no camera */
  r->stamp++;
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
    glClearDepthf(r->reversed_depth ? 0 : 1);
  }
  t3_renderer_render_depth(r, scene, cam);
}
