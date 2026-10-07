/* The renderer: three.js r186's frame on OpenGL ES 3.0 (and, through
 * renderer_wgpu.inc, on WebGPU), drawing with r186's generated programs
 * (renderer_gen.inc).
 *
 * The frame follows three.js: update world matrices, build the render list
 * with frustum culling, sort opaque front-to-back and transparent
 * back-to-front (painterSortStable / reversePainterSortStable), set up the
 * lights in view space, draw.
 *
 * Where three.c departs from three.js it is to do less GL work for the same
 * pixels:
 *   - attribute locations are bound before link, so one VAO per geometry
 *     serves every program (three.js keys VAOs on geometry + program);
 *   - per-program uniform caches skip camera, light and material uploads
 *     that would write the value the program already holds. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backend.h"
#include "gen_program.h"
#include "gen_wgpu.h"
#include "simd.h"

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT /* EXT_texture_filter_anisotropic */
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

/* the per-object scene walk's helpers stay inlined into it (a bigger
 * renderer.c made the compiler outline them: 7% of a 2000-mesh frame in wasm) */
#define T3_HOT inline __attribute__((always_inline))

typedef struct {
  t3_object *object;
  t3_geometry *geometry;
  t3_material *material;
  const t3_group *group;
  float z;
  int render_order;
  int batch_count;   /* > 0: an auto-instanced batch of this many meshes */
  int batch_offset;  /* its first matrix in the renderer's instance staging */
  /* the generated path: the program, and the byte offset of this draw's
   * per-draw uniform block in the frame's streamed buffer (-1: none) */
  struct t3_gen_gl *gen_gl;
  int gen_off;
} render_item;

typedef struct {
  render_item *items;
  int n, cap;
} render_list;

struct batch_entry {
  t3_material *material; /* the first member's; the rest differ at most in color */
  t3_geometry *geometry;
  const t3_group *group;
  int render_order;
  bool receive_shadow;   /* one receiveShadow uniform per draw */
  int head, tail, count; /* member chain in mobj / mnext */
};

typedef struct {
  GLuint vao;
  GLuint batch_vao;  /* geometry + the renderer's batch instance buffer */
  int batch_off; /* instance offset its pointers were last set to */
  /* ... and the instance buffer they point into: renderers sharing a GL
   * context share geometries, and each has its own instance buffer, so a
   * batch VAO set up by one renderer must be re-pointed before another draws
   * through it (else that draw reads the first renderer's, smaller, buffer:
   * out of bounds on the GPU) */
  uint64_t batch_buf; /* renderer serial << 32 | buffer name */
  uint32_t geometry_version;
  bool instance_color_bound; /* an InstancedMesh's VAO: built with its instance colours */
  /* what each morph attribute slot of the plain VAO points at: 0 unknown,
   * -1 disabled, else morph target index + 1 (bind_morphs skips no-ops) */
  int16_t morph_slot[16];
  /* VAOs for programs with attributes of their own (ShaderMaterials), by
   * program, most recent first */
  struct { const void *program; GLuint vao; } pv[4];
  /* r186's morph texture (the generated path): a 2D array, one layer per target */
  GLuint morph_tex;
  uint32_t morph_version;
  int pv_n;
} geometry_gl;

static void delete_program_vaos(t3_renderer *r, geometry_gl *gg);

typedef struct {
  GLuint buffer;
  uint32_t version;
  GLsizeiptr size;
} attribute_gl;

typedef struct {
  GLuint tex;
  uint32_t version;
  float max_mip;      /* __maxMipLevel: log2(size) when mipmaps were generated */
  bool borrowed;      /* t3_texture_from_gl with owns_gl false: the embedder deletes it */
} texture_gl;

#define T3_MAX_LIGHTS 8 /* per type; more are ignored */
typedef struct {
  t3_color ambient;
  int num_dir, num_point, num_spot, num_hemi, num_dir_shadows, num_spot_shadows, num_point_shadows;
  t3_light *dir[T3_MAX_LIGHTS], *point[T3_MAX_LIGHTS], *spot[T3_MAX_LIGHTS], *hemi[T3_MAX_LIGHTS];
  /* view-space values for the current camera */
  t3_vec3 dir_direction[T3_MAX_LIGHTS];
  t3_vec3 point_position[T3_MAX_LIGHTS];
  t3_vec3 spot_position[T3_MAX_LIGHTS], spot_direction[T3_MAX_LIGHTS];
  t3_vec3 hemi_direction[T3_MAX_LIGHTS];
} light_state;

struct t3_renderer {
  uint32_t serial;         /* unique for the process: which renderer set a shared geometry's batch VAO */
  int width, height;
  t3_color clear_color;
  float clear_alpha;
  bool shadow_map_enabled;
  t3_shadow_map_type shadow_map_type;
  t3_color_space output_color_space;
  t3_tone_mapping tone_mapping;
  float tone_mapping_exposure;
  /* renderer.setRenderTarget (not retained): NULL draws to the screen */
  t3_render_target *target;
  const t3_texture *environment; /* the scene being rendered's environment */
  bool rendering_shadows;
  /* GPU timing: a ring of GL_TIME_ELAPSED_EXT queries in flight */
  bool gpu_timing, gq_active;
  GLuint gq[4];
  unsigned gq_frame[4];
  int gq_head, gq_count;
  t3_mesh *bg_sphere;     /* r186 Background.js's sphere */
  void *pm186;            /* r186's PMREM generator state (renderer_env.inc) */
  t3_scene *env_scene;    /* the scene being rendered (its environment intensity / rotation) */

  render_list opaque, transparent, scratch;
  /* transmission (r186): double-sided transmissive items, drawn back faces
   * first; and per render, the framebuffer copied once for each side at its
   * first transmissive draw ([0] front, [1] back: r186's two ViewportTextureNodes) */
  render_list backpass;
  bool vp_done[2];
  t3_render_target *vp_rt[2];   /* GL copies */
  void *wg_vp[2], *wg_vp_view[2];   /* WebGPU copies */
  t3_texture *vp_sampler;       /* the copies' filtering (FramebufferTexture: nearest, mipmapped minification) */
  /* per-frame batch table, filled while the scene is walked */
  struct batch_entry *batches;
  int batch_n, batch_cap;
  int *htab;               /* open addressing over batches, -1 = empty */
  int hcap;
  t3_object **mobj;        /* batch members, chained through mnext */
  /* last frame's uploads: an identical frame re-uses the buffers as they are */
  float *prev_inst;
  int prev_inst_n, prev_cap;

  int *mnext;
  int m_n, m_cap;

  /* Auto-instancing: opaque plain meshes that share geometry, material and
   * group are drawn as one instanced draw, with their world matrices as the
   * instance attribute. Same pixels as drawing them one by one (r186's
   * instanced program), a fraction of the draw calls. */
  bool auto_instancing;
  /* antialias: the canvas is multisampled; here a
   * multisampled framebuffer resolved into the default one after a render */
  int msaa_samples, msaa_w, msaa_h;
  GLuint msaa_fbo, msaa_rb[2];
  /* 0 untried, 1 the resolve blits straight to the default framebuffer, 2
   * it goes through a single-sample RGBA8 one (the default framebuffer's
   * format differs, e.g. BGRA, which a multisample blit refuses) */
  int msaa_path;
  /* GL_SAMPLES of the default framebuffer, -1 until asked: a canvas made with
   * antialias: true (or a multisampled EGL surface) already is what
   * antialias asks for, so it is drawn into directly */
  int fb0_samples;
  /* the framebuffer "the screen" means (t3_renderer_set_framebuffer): 0, or
   * an embedder's own target such as a 2D engine's canvas */
  GLuint screen_fbo;
  GLuint resolve_fbo, resolve_rb;
  t3_mat3 view_normal;
  float *inst;
  int inst_n, inst_cap;
  GLuint inst_vbo;
  GLsizeiptr inst_vbo_size;
  t3_light **lights;
  int light_count, light_cap;

  light_state ls;
  t3_mat4 proj_screen;
  t3_frustum frustum;
  const t3_camera *camera;

  /* renderer.setViewport / setScissor / autoClear */
  int view_x, view_y, view_w, view_h;
  int sc_x, sc_y, sc_w, sc_h;
  bool scissor_test, auto_clear;
  const t3_fog *fog;           /* the scene being rendered's fog, or NULL */
  t3_light **casters;          /* lights with castShadow, scene order (shadowsArray) */
  int caster_count, caster_cap;
  t3_mat4 shadow_proj_screen;
  t3_frustum shadow_frustum;

  /* GL state cache, kept across frames */
  bool fb_known, vp_known, clear_known;
  GLuint cur_fbo;
  int vp_x, vp_y, vp_w, vp_h;
  int cur_scissor; /* -1 unknown */
  int csc_x, csc_y, csc_w, csc_h;
  bool shadow_units_bound;
  float clear_v[4];
  GLuint cur_program, cur_vao;
  int cur_cull, cur_depth_test, cur_depth_write, cur_blend; /* -1 = unknown */
  bool blend_func_set;   /* glBlendFunc / glBlendEquation issued since the last reset */
  int cur_color_mask;     /* glColorMask, all four the same (1 until a material writes no colour) */

  unsigned stamp;        /* bumps per render() for camera + lights caches */
  t3_render_info info;
  char error[2048];
  bool has_error;

  /* the backend seam (backend.h): its GLES unit's lifetime and embedder slots */
  t3_backend *bk;
  /* t3_renderer_set_external_target: the embedder's target as described,
   * when one is set (its fbo is screen_fbo, its rect the viewport + scissor;
   * the clear of the next render follows load_color / load_depth) */
  t3_external_target ext_target;
  bool has_ext_target;
  t3_mat4 last_proj;     /* the projection matrix of the last render */
  /* the programs r186's node renderer generates (gen_program.h), built on
   * first use per table entry */
  struct gen_entry { const t3_gen_program *src; t3_gen_gl *gl; bool encode; t3_gen_params params; } *gen;
  /* this frame draws straight to the screen, its programs encoding sRGB
   * themselves (no transparency to blend in linear, no tone mapping) */
  bool gen_direct;
  /* WebGPU (T3_WGPU): the backend context and the frame's targets */
#ifdef T3_WGPU
  t3_wgpu *wgpu;
#else
  void *wgpu;
#endif
  void *wg_dfg, *wg_inst, *wg_quad, *wg_color, *wg_depth, *wg_out;
  int wg_out_format;
  bool exact_output;   /* t3_renderer_set_exact_output: always r186's output pass */
  int wg_pass_format, wg_depth_format, wg_samples;
  bool wg_flip;   /* the pass's target is stored bottom-up (t3_external_target.flip_y) */
#ifdef T3_WGPU
  t3_wgpu_pass wg_cur_pass;     /* the scene pass, restarted after a transmission copy */
#endif
  void *wg_cur_src; int wg_cur_src_format, wg_cur_w, wg_cur_h;
  void *wg_ext_out; int wg_ext_out_format;   /* an embedder's output view (t3_renderer_wgpu_set_output) */   /* the attachments of the pass being recorded */
  /* the generated programs' environment / background / PMREM values for the
   * draw being filled (renderer_gen.inc, renderer_env.inc) */
  struct gen_aux {
    const struct t3_texture *env;      /* the cube texture (classic) or cube-UV PMREM texture the draw samples */
    float env_intensity; t3_euler env_rotation;
    const struct t3_texture *bg;       /* the background texture (bg programs) */
    float bg_intensity, bg_blur; t3_euler bg_rotation;
    const struct t3_texture *pm_src;   /* a PMREM pass's source */
    float pm_rough, pm_mip, pm_sigma;
  } aux;
  struct gen_shadow { const t3_light *l; GLuint fbo, tex; int w, h; bool cube; void *wg; } gen_sh[16];
  int gen_sh_n;
  char *gen_missing;      /* table states the scenes asked for and the table lacks, one per line */
  /* every draw's per-draw block of the frame, packed at the uniform-buffer
   * offset alignment, uploaded once per frame (glBindBufferRange per draw) */
  uint8_t *gen_stage; size_t gen_stage_n, gen_stage_cap;
  GLuint gen_ubo; size_t gen_ubo_size;
  GLint gen_align;
  GLuint gen_bound_buf[T3_GEN_MAX_GROUPS]; int gen_bound_off[T3_GEN_MAX_GROUPS];
  int gen_n, gen_cap;
  /* r186 renders the scene into a linear (half float) target and a final pass
   * applies the output colour space (and tone mapping); on when the output
   * encoding is sRGB or a tone mapping is set, for the screen target only */
  GLuint out_fbo, out_tex, out_depth, out_vao, out_vbo;
  int out_w, out_h;
  bool out_on;
  GLuint dfg_tex;          /* r186's DFG LUT, made on first use */
};



static void rt_free(t3_render_target *rt);
static void pm186_free(t3_renderer *r);
static void tu_forget(GLuint tex);
static void tu_forget_all(void);
static int tu_active_unit(void);
static void tu_forget_active(void);

/* ── GL object lifetime ──────────────────────────────────────────── */
static void gl_release(uint32_t kind, void *thing) {
  switch (kind) {
  case T3_KIND_GEOMETRY: {
    t3_geometry *g = thing;
    geometry_gl *gg = g->_gl;
    delete_program_vaos(NULL, gg);
    glDeleteVertexArrays(1, &gg->vao);
    if (gg->batch_vao) glDeleteVertexArrays(1, &gg->batch_vao);
    if (gg->morph_tex) glDeleteTextures(1, &gg->morph_tex);
    free(gg);
    g->_gl = NULL;
    break;
  }
  case T3_KIND_ATTRIBUTE: {
    t3_attribute *a = thing;
    attribute_gl *ag = a->_gl;
    glDeleteBuffers(1, &ag->buffer);
    free(ag);
    a->_gl = NULL;
    break;
  }
  case T3_KIND_TEXTURE: {
    t3_texture *t = thing;
    texture_gl *tg = t->_gl;
    tu_forget(tg->tex);
    if (!tg->borrowed) glDeleteTextures(1, &tg->tex);
    free(tg);
    t->_gl = NULL;
    break;
  }
  case T3_KIND_RENDER_TARGET:
    rt_free(thing);
    break;
  default: break;
  }
}

t3_renderer *t3_renderer_new(int width, int height) {
  t3_renderer *r = calloc(1, sizeof *r);
  T3_CHECK_ALLOC(r);
  static uint32_t serials;
  r->serial = ++serials;
  r->clear_alpha = 1;
  r->output_color_space = T3_SRGB_COLOR_SPACE;   /* r186: outputColorSpace = SRGBColorSpace */
  r->tone_mapping_exposure = 1;
  r->shadow_map_type = T3_PCF_SHADOW_MAP;
  t3__gl_release = gl_release;
  r->cur_cull = r->cur_depth_test = r->cur_depth_write = r->cur_blend = -1;
  r->cur_color_mask = 1;
  r->auto_instancing = true;
  r->auto_clear = true;
  r->fb0_samples = -1;
  r->cur_scissor = -1;
  r->info.gpu_timer_supported = -1;
  r->info.gpu_ms = -1;
  r->bk = t3_backend_gles_new();
  T3_CHECK_ALLOC(r->bk);
  t3_mat4_identity(&r->last_proj);
  t3_renderer_set_size(r, width, height);
  return r;
}

void t3_renderer_destroy(t3_renderer *r) {
  if (!r) return;
  if (r->info.gpu_timer_supported == 1) glDeleteQueries(4, r->gq);
  t3_release(r->bg_sphere);
  for (int i = 0; i < 2; i++) t3_release(r->vp_rt[i]);
  t3_release(r->vp_sampler);
  free(r->backpass.items);
  pm186_free(r);
#ifdef T3_WGPU
  for (int i = 0; i < r->gen_n; i++) if (r->wgpu) t3_gen_wgpu_free(r->gen[i].gl); else t3_gen_gl_free(r->gen[i].gl);
  t3_wgpu_destroy(r->wgpu);
#else
  for (int i = 0; i < r->gen_n; i++) t3_gen_gl_free(r->gen[i].gl);
#endif
  free(r->gen);
  if (r->out_fbo) { glDeleteFramebuffers(1, &r->out_fbo); glDeleteTextures(1, &r->out_tex); glDeleteRenderbuffers(1, &r->out_depth); }
  if (r->out_vao) { glDeleteVertexArrays(1, &r->out_vao); glDeleteBuffers(1, &r->out_vbo); }
  if (r->dfg_tex) glDeleteTextures(1, &r->dfg_tex);
  if (r->gen_ubo) glDeleteBuffers(1, &r->gen_ubo);
  for (int i = 0; i < r->gen_sh_n; i++) { glDeleteTextures(1, &r->gen_sh[i].tex); glDeleteFramebuffers(1, &r->gen_sh[i].fbo); }
  free(r->gen_missing);
  free(r->gen_stage);
  free(r->opaque.items);
  free(r->transparent.items);
  free(r->casters);
  free(r->scratch.items);
  free(r->batches); free(r->htab); free(r->mobj); free(r->mnext);
  free(r->prev_inst);
  free(r->inst);
  if (r->inst_vbo) glDeleteBuffers(1, &r->inst_vbo);
  free(r->lights);
  if (r->msaa_fbo) { glDeleteFramebuffers(1, &r->msaa_fbo); glDeleteRenderbuffers(2, r->msaa_rb); }
  if (r->resolve_fbo) { glDeleteFramebuffers(1, &r->resolve_fbo); glDeleteRenderbuffers(1, &r->resolve_rb); }
  if (r->bk) r->bk->destroy(r->bk);
  free(r);
}

void t3_renderer_set_size(t3_renderer *r, int w, int h) {
  r->width = w;
  r->height = h;
  /* renderer.setSize resets the viewport to the whole surface */
  r->view_x = r->view_y = 0; r->view_w = w; r->view_h = h;
  r->sc_x = r->sc_y = 0; r->sc_w = w; r->sc_h = h;
}

void t3_renderer_set_viewport(t3_renderer *r, int x, int y, int w, int h) {
  r->view_x = x; r->view_y = y; r->view_w = w; r->view_h = h;
}
void t3_renderer_set_scissor(t3_renderer *r, int x, int y, int w, int h) {
  r->sc_x = x; r->sc_y = y; r->sc_w = w; r->sc_h = h;
}
void t3_renderer_set_scissor_test(t3_renderer *r, bool on) { r->scissor_test = on; }
void t3_renderer_get_viewport(const t3_renderer *r, int *x, int *y, int *w, int *h) {
  *x = r->view_x; *y = r->view_y; *w = r->view_w; *h = r->view_h;
}
bool t3_renderer_get_scissor(const t3_renderer *r, int *x, int *y, int *w, int *h) {
  *x = r->sc_x; *y = r->sc_y; *w = r->sc_w; *h = r->sc_h;
  return r->scissor_test;
}
void t3_renderer_get_size(const t3_renderer *r, int *w, int *h) { *w = r->width; *h = r->height; }
bool t3_renderer_get_shadow_map(const t3_renderer *r, t3_shadow_map_type *type) {
  *type = r->shadow_map_type;
  return r->shadow_map_enabled;
}
int t3_renderer_get_antialias(const t3_renderer *r) { return r->msaa_samples; }
void t3_renderer_set_auto_clear(t3_renderer *r, bool on) { r->auto_clear = on; }
void t3_renderer_set_antialias(t3_renderer *r, int samples) { r->msaa_samples = samples; }
void t3_renderer_set_framebuffer(t3_renderer *r, unsigned fbo) {
  r->has_ext_target = false; /* the GL-era call: a plain fbo, the renderer's own clear rules */
  if (fbo == r->screen_fbo) return;
  r->screen_fbo = fbo;
  r->fb0_samples = -1;
  r->fb_known = false;
}

/* ── the backend-neutral embedder hooks (three.h) ─────────────────── */
const char *t3_renderer_generated_missing(const t3_renderer *r) { return r->gen_missing ? r->gen_missing : ""; }
t3_clip_z t3_renderer_clip_z(const t3_renderer *r) { return r->bk->clip_z(r->bk); }
const float *t3_renderer_last_projection(const t3_renderer *r) { return r->last_proj.e; }
void t3_renderer_set_exact_output(t3_renderer *r, bool on) { r->exact_output = on; }
void t3_renderer_set_external_encoder(t3_renderer *r, uintptr_t enc) {
#ifdef T3_WGPU
  if (r->wgpu) { t3_wgpu_set_encoder(r->wgpu, (WGPUCommandEncoder)enc); return; }
#endif
  r->bk->set_external_encoder(r->bk, enc);
}
void t3_renderer_set_external_target(t3_renderer *r, const t3_external_target *t) {
  if (!t) {
    t3_renderer_set_framebuffer(r, 0);
    r->view_x = r->view_y = 0; r->view_w = r->width; r->view_h = r->height;
    r->sc_x = r->sc_y = 0; r->sc_w = r->width; r->sc_h = r->height;
    r->scissor_test = false;
    return;
  }
  t3_renderer_set_framebuffer(r, (unsigned)t->color);
  r->ext_target = *t;
  r->has_ext_target = true;
  r->view_x = t->x; r->view_y = t->y; r->view_w = t->w; r->view_h = t->h;
  r->sc_x = t->x; r->sc_y = t->y; r->sc_w = t->w; r->sc_h = t->h;
  r->scissor_test = true;
}

/* render through the renderer's own multisampled framebuffer? */
static bool own_msaa(t3_renderer *r) {
  if (!r->msaa_samples) return false;
  if (r->fb0_samples < 0) {
    GLint n = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, r->screen_fbo);
    r->fb_known = true;
    r->cur_fbo = r->screen_fbo;
    glGetIntegerv(GL_SAMPLES, &n);
    r->fb0_samples = n;
  }
  return r->fb0_samples == 0;
}

/* the multisampled color + depth/stencil target, (re)made at the drawing size */
static GLuint msaa_target(t3_renderer *r) {
  if (r->msaa_fbo && r->msaa_w == r->width && r->msaa_h == r->height) return r->msaa_fbo;
  if (!r->msaa_fbo) { glGenFramebuffers(1, &r->msaa_fbo); glGenRenderbuffers(2, r->msaa_rb); }
  glBindRenderbuffer(GL_RENDERBUFFER, r->msaa_rb[0]);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, r->msaa_samples, GL_RGBA8, r->width, r->height);
  glBindRenderbuffer(GL_RENDERBUFFER, r->msaa_rb[1]);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, r->msaa_samples, GL_DEPTH24_STENCIL8, r->width, r->height);
  glBindFramebuffer(GL_FRAMEBUFFER, r->msaa_fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, r->msaa_rb[0]);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, r->msaa_rb[1]);
  r->fb_known = true;
  r->cur_fbo = r->msaa_fbo;
  if (r->resolve_fbo) {
    glBindRenderbuffer(GL_RENDERBUFFER, r->resolve_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, r->width, r->height);
    glBindFramebuffer(GL_FRAMEBUFFER, r->msaa_fbo);
  }
  r->msaa_w = r->width; r->msaa_h = r->height;
  return r->msaa_fbo;
}

static void msaa_resolve(t3_renderer *r) {
  int x0 = r->view_x, y0 = r->view_y, x1 = r->view_x + r->view_w, y1 = r->view_y + r->view_h;
  if (r->msaa_path != 2) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->screen_fbo);
    glBlitFramebuffer(x0, y0, x1, y1, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (!r->msaa_path) {
      while (glGetError() != GL_NO_ERROR) r->msaa_path = 2;
      if (!r->msaa_path) r->msaa_path = 1;
    }
  }
  if (r->msaa_path == 2) {
    if (!r->resolve_fbo) {
      glGenFramebuffers(1, &r->resolve_fbo);
      glGenRenderbuffers(1, &r->resolve_rb);
      glBindRenderbuffer(GL_RENDERBUFFER, r->resolve_rb);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, r->width, r->height);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->resolve_fbo);
      glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, r->resolve_rb);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->resolve_fbo);
    glBlitFramebuffer(x0, y0, x1, y1, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->resolve_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->screen_fbo);
    glBlitFramebuffer(x0, y0, x1, y1, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, r->msaa_fbo);
}

static void gl_viewport(t3_renderer *r, int x, int y, int w, int h) {
  if (r->vp_known && r->vp_x == x && r->vp_y == y && r->vp_w == w && r->vp_h == h) return;
  glViewport(x, y, w, h);
  r->vp_known = true;
  r->vp_x = x; r->vp_y = y; r->vp_w = w; r->vp_h = h;
}
static void gl_fbo(t3_renderer *r, GLuint fbo) {
  if (r->fb_known && r->cur_fbo == fbo) return;
  /* hosts unbind a texture attached to the framebuffer being drawn into
   * (WebGL: no feedback loops), so the output target's texture, which the
   * output pass samples, is forgotten as its framebuffer is bound */
  if (fbo && fbo == r->out_fbo) tu_forget(r->out_tex);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  r->fb_known = true;
  r->cur_fbo = fbo;
}
static void gl_clear_color(t3_renderer *r, float cr, float cg, float cb, float ca) {
  float cv[4] = { cr, cg, cb, ca };
  if (r->clear_known && !memcmp(cv, r->clear_v, sizeof cv)) return;
  glClearColor(cr, cg, cb, ca);
  memcpy(r->clear_v, cv, sizeof cv);
  r->clear_known = true;
}
/* the main pass's scissor: renderer.setScissorTest / setScissor */
static void gl_scissor_box(t3_renderer *r, bool on, int x, int y, int w, int h) {
  if (r->cur_scissor != (int)on) {
    if (on) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    r->cur_scissor = on;
  }
  if (on && (r->csc_x != x || r->csc_y != y || r->csc_w != w || r->csc_h != h)) {
    glScissor(x, y, w, h);
    r->csc_x = x; r->csc_y = y; r->csc_w = w; r->csc_h = h;
  }
}

static GLuint target_fbo(t3_renderer *r);
/* setViewport / setScissor measure y from the TOP of the drawing buffer (the
 * WebGPU convention, kept by r186's WebGL backend); an embedder's external
 * target from the bottom, as GL */
static GLuint gen_out_target(t3_renderer *r);
static int view_gl_y(const t3_renderer *r, int y, int h) { return !r->has_ext_target ? r->height - y - h : y; }
/* the current target's framebuffer, viewport and scissor: the render
 * target's own (setRenderTarget copies them), else the renderer's */
static void bind_target(t3_renderer *r) {
  gl_fbo(r, target_fbo(r));
  const t3_render_target *rt = r->target;
  if (rt) {
    /* r186's WebGL backend: a target's viewport and scissor are measured from the top too */
    gl_viewport(r, rt->viewport[0], rt->height - rt->viewport[1] - rt->viewport[3], rt->viewport[2], rt->viewport[3]);
    gl_scissor_box(r, rt->scissor_test, rt->scissor[0], rt->height - rt->scissor[1] - rt->scissor[3], rt->scissor[2], rt->scissor[3]);
  } else if (rt) {
    gl_viewport(r, rt->viewport[0], rt->viewport[1], rt->viewport[2], rt->viewport[3]);
    gl_scissor_box(r, rt->scissor_test, rt->scissor[0], rt->scissor[1], rt->scissor[2], rt->scissor[3]);
  } else {
    gl_viewport(r, r->view_x, view_gl_y(r, r->view_y, r->view_h), r->view_w, r->view_h);
    gl_scissor_box(r, r->scissor_test, r->sc_x, view_gl_y(r, r->sc_y, r->sc_h), r->sc_w, r->sc_h);
  }
}

static bool wgpu_clear(t3_renderer *r, bool color, bool depth);
void t3_renderer_clear(t3_renderer *r, bool color, bool depth, bool stencil) {
  if (wgpu_clear(r, color, depth || stencil)) return;
  if (!r->target && !r->has_ext_target && (r->output_color_space == T3_SRGB_COLOR_SPACE || r->tone_mapping)) {
    /* r186 clears its linear framebuffer target; the next render may draw
     * straight to the screen (sRGB encoded) or through that target: clear both
     * so a render with autoClear off finds the clear either way */
    GLbitfield bits = (color ? GL_COLOR_BUFFER_BIT : 0) | (depth ? GL_DEPTH_BUFFER_BIT : 0) | (stencil ? GL_STENCIL_BUFFER_BIT : 0);
    if (!bits) return;
    if (depth && r->cur_depth_write != 1) { glDepthMask(GL_TRUE); r->cur_depth_write = 1; }
    gl_fbo(r, r->screen_fbo);
    gl_viewport(r, r->view_x, view_gl_y(r, r->view_y, r->view_h), r->view_w, r->view_h);
    gl_scissor_box(r, r->scissor_test, r->sc_x, view_gl_y(r, r->sc_y, r->sc_h), r->sc_w, r->sc_h);
    gl_clear_color(r, t3_linear_to_srgb(r->clear_color.r), t3_linear_to_srgb(r->clear_color.g), t3_linear_to_srgb(r->clear_color.b), r->clear_alpha);
    glClear(bits);
    gl_fbo(r, gen_out_target(r));
    gl_clear_color(r, r->clear_color.r, r->clear_color.g, r->clear_color.b, r->clear_alpha);
    glClear(bits);
    return;
  }
  bind_target(r);
  gl_clear_color(r, r->clear_color.r, r->clear_color.g, r->clear_color.b, r->clear_alpha);
  if (depth && r->cur_depth_write != 1) { glDepthMask(GL_TRUE); r->cur_depth_write = 1; }
  GLbitfield bits = (color ? GL_COLOR_BUFFER_BIT : 0) | (depth ? GL_DEPTH_BUFFER_BIT : 0) | (stencil ? GL_STENCIL_BUFFER_BIT : 0);
  if (bits) glClear(bits);
}

/* renderer.resetState: forget every cached GL state, for callers that
 * draw with GL themselves between renders. */
void t3_renderer_gl_state(const t3_renderer *r, t3_gl_state *s) {
  s->fbo = r->fb_known ? (int)r->cur_fbo : -1;
  s->vp_known = r->vp_known;
  s->vp_x = r->vp_x; s->vp_y = r->vp_y; s->vp_w = r->vp_w; s->vp_h = r->vp_h;
  s->active_unit = tu_active_unit();
  s->program = (int)r->cur_program;
  s->vao = (int)r->cur_vao;
  s->blend = r->cur_blend;
  s->blend_func_set = r->blend_func_set;
  s->depth_test = r->cur_depth_test;
  s->depth_write = r->cur_depth_write;
  s->cull = r->cur_cull;
  s->scissor = r->cur_scissor;
  s->clear_set = r->clear_known;
  memcpy(s->clear, r->clear_v, sizeof s->clear);
}

void t3_renderer_forget(t3_renderer *r, unsigned what) {
  if (what & T3_FORGET_FBO) r->fb_known = false;
  if (what & T3_FORGET_VIEWPORT) r->vp_known = false;
  if (what & T3_FORGET_PROGRAM) r->cur_program = 0;
  if (what & T3_FORGET_VAO) r->cur_vao = 0;
  if (what & T3_FORGET_TEXTURES) { tu_forget_all(); r->shadow_units_bound = false; }
  else if (what & T3_FORGET_UNIT) tu_forget_active();
  if (what & T3_FORGET_CAPS) {
    r->cur_cull = r->cur_depth_test = r->cur_depth_write = r->cur_blend = -1;
    r->blend_func_set = false;
  }
  if (what & T3_FORGET_SCISSOR) r->cur_scissor = -1;
  if (what & T3_FORGET_CLEAR_COLOR) r->clear_known = false;
}

bool t3_renderer_clears_target(const t3_renderer *r) {
  return r->auto_clear && !r->scissor_test && r->view_x == 0 && r->view_y == 0 && r->view_w == r->width &&
         r->view_h == r->height;
}

void t3_renderer_reset_bindings(t3_renderer *r) {
  r->blend_func_set = false;
  for (int i = 0; i < T3_GEN_MAX_GROUPS; i++) r->gen_bound_buf[i] = 0;
  r->fb_known = r->vp_known = r->clear_known = false;
  r->cur_scissor = -1;
  r->shadow_units_bound = false;
  tu_forget_all();
  r->cur_program = r->cur_vao = 0;
  r->cur_cull = r->cur_depth_test = r->cur_depth_write = r->cur_blend = -1;
}

void t3_renderer_reset_state(t3_renderer *r) {
  t3_renderer_reset_bindings(r);
  for (int i = 0; i < r->gen_n; i++) if (r->gen[i].gl) r->gen[i].gl->render_stamp = 0;
}

t3_color t3_renderer_get_clear_color(const t3_renderer *r, float *alpha) {
  if (alpha) *alpha = r->clear_alpha;
  return r->clear_color;
}
bool t3_renderer_get_auto_clear(const t3_renderer *r) { return r->auto_clear; }
t3_tone_mapping t3_renderer_get_tone_mapping(const t3_renderer *r, float *exposure) {
  if (exposure) *exposure = r->tone_mapping_exposure;
  return r->tone_mapping;
}
t3_color_space t3_renderer_get_output_color_space(const t3_renderer *r) { return r->output_color_space; }

void t3_renderer_set_clear_color(t3_renderer *r, uint32_t hex, float alpha) {
  r->clear_color = t3_color_hex(hex);
  r->clear_alpha = alpha;
}

void t3_renderer_set_shadow_map(t3_renderer *r, bool enabled, t3_shadow_map_type type) {
  r->shadow_map_enabled = enabled;
  r->shadow_map_type = type;
}

void t3_renderer_set_output_color_space(t3_renderer *r, t3_color_space e) { r->output_color_space = e; }
void t3_renderer_set_tone_mapping(t3_renderer *r, t3_tone_mapping tm, float exposure) {
  r->tone_mapping = tm;
  r->tone_mapping_exposure = exposure;
}
void t3_renderer_set_render_target(t3_renderer *r, t3_render_target *rt) { r->target = rt; }
t3_render_target *t3_renderer_get_render_target(const t3_renderer *r) { return r->target; }
void t3_renderer_set_auto_instancing(t3_renderer *r, bool on) { r->auto_instancing = on; }
const t3_render_info *t3_renderer_info(t3_renderer *r) { return &r->info; }
void t3_renderer_set_gpu_timing(t3_renderer *r, bool on) { r->gpu_timing = on; }

/* ── GPU timing (EXT_disjoint_timer_query) ───────────────────────── */
#define T3_TIME_ELAPSED 0x88BF
#define T3_GPU_DISJOINT 0x8FBB

/* results that are in, oldest first, without waiting; then a query for this
 * render if a slot is free */
static void gpu_timer_begin(t3_renderer *r) {
  if (r->info.gpu_timer_supported < 0) {
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    r->info.gpu_timer_supported = ext && strstr(ext, "disjoint_timer_query") ? 1 : 0;
    if (r->info.gpu_timer_supported) {
      while (glGetError() != GL_NO_ERROR) {}
      glGenQueries(4, r->gq);
      glBeginQuery(T3_TIME_ELAPSED, r->gq[0]);
      bool ok = glGetError() == GL_NO_ERROR;
      if (ok) glEndQuery(T3_TIME_ELAPSED);
      while (glGetError() != GL_NO_ERROR) {}
      /* (that first query's result is dropped: it was not queued) */
      if (!ok) {
        /* listed but not enabled (a WebGL host that never called getExtension) */
        glDeleteQueries(4, r->gq);
        r->info.gpu_timer_supported = 0;
      }
    }
  }
  if (r->info.gpu_timer_supported != 1) return;
  while (r->gq_count) {
    GLuint q = r->gq[r->gq_head];
    GLuint avail = 0;
    glGetQueryObjectuiv(q, GL_QUERY_RESULT_AVAILABLE, &avail);
    if (!avail) break;
    GLuint ns = 0;
    glGetQueryObjectuiv(q, GL_QUERY_RESULT, &ns);
    GLint disjoint = 0;
    glGetIntegerv(T3_GPU_DISJOINT, &disjoint);
    if (!disjoint) {
      r->info.gpu_ms = ns / 1e6;
      r->info.gpu_frame = r->gq_frame[r->gq_head];
    }
    r->gq_head = (r->gq_head + 1) & 3;
    r->gq_count--;
  }
  if (r->gq_count == 4) return;
  int slot = (r->gq_head + r->gq_count) & 3;
  glBeginQuery(T3_TIME_ELAPSED, r->gq[slot]);
  r->gq_frame[slot] = r->info.frame;
  r->gq_count++;
  r->gq_active = true;
}
static void gpu_timer_end(t3_renderer *r) {
  if (!r->gq_active) return;
  glEndQuery(T3_TIME_ELAPSED);
  r->gq_active = false;
}
const char *t3_renderer_last_error(t3_renderer *r) { return r->has_error ? r->error : NULL; }

/* ── uploads ─────────────────────────────────────────────────────── */
static GLuint attribute_buffer(t3_attribute *a, GLenum target) {
  attribute_gl *ag = a->_gl;
  GLsizeiptr size = (GLsizeiptr)a->count * a->item_size * (a->type == T3_UINT16 ? 2 : 4);
  if (!ag) {
    ag = calloc(1, sizeof *ag);
    T3_CHECK_ALLOC(ag);
    glGenBuffers(1, &ag->buffer);
    a->_gl = ag;
  }
  if (ag->version != a->version || !ag->size) {
    glBindBuffer(target, ag->buffer);
    if (ag->size == size) glBufferSubData(target, 0, size, a->array);
    else glBufferData(target, size, a->array, a->usage);
    ag->size = size;
    ag->version = a->version;
  }
  return ag->buffer;
}

static void bind_float_attr(t3_attribute *a, GLuint index) {
  GLuint buf = attribute_buffer(a, GL_ARRAY_BUFFER);
  glBindBuffer(GL_ARRAY_BUFFER, buf);
  glEnableVertexAttribArray(index);
  glVertexAttribPointer(index, a->item_size, GL_FLOAT, a->normalized, 0, 0);
}

static void bind_vao(t3_renderer *r, GLuint vao) {
  if (r->cur_vao != vao) {
    glBindVertexArray(vao);
    r->cur_vao = vao;
  }
}

/* The VAO for a plain mesh lives on the geometry. An instanced mesh needs its
 * own, since the per-instance buffers belong to the mesh; it is kept in the
 * mesh's user-invisible slot below. */
static GLuint build_vao(t3_renderer *r, t3_geometry *g, t3_instanced_mesh *im) {
  GLuint vao;
  glGenVertexArrays(1, &vao);
  bind_vao(r, vao);
  static const GLuint locs[T3_ATTR_COUNT] = { A_POSITION, A_NORMAL, A_UV, A_COLOR, A_TANGENT, A_UV1 };
  for (int s = T3_ATTR_POSITION; s <= T3_ATTR_UV1; s++)
    if (g->attributes[s]) bind_float_attr(g->attributes[s], locs[s]);
  if (g->attributes[T3_ATTR_SKIN_INDEX]) bind_float_attr(g->attributes[T3_ATTR_SKIN_INDEX], A_SKIN_INDEX);
  if (g->attributes[T3_ATTR_SKIN_WEIGHT]) bind_float_attr(g->attributes[T3_ATTR_SKIN_WEIGHT], A_SKIN_WEIGHT);
  if (g->index) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, attribute_buffer(g->index, GL_ELEMENT_ARRAY_BUFFER));
  if (im) {
    glBindBuffer(GL_ARRAY_BUFFER, attribute_buffer(im->instance_matrix, GL_ARRAY_BUFFER));
    for (int c = 0; c < 4; c++) {
      glEnableVertexAttribArray(A_INSTANCE_MATRIX + c);
      glVertexAttribPointer(A_INSTANCE_MATRIX + c, 4, GL_FLOAT, GL_FALSE, 64, (void *)(intptr_t)(c * 16));
      glVertexAttribDivisor(A_INSTANCE_MATRIX + c, 1);
    }
    if (im->instance_color) {
      glBindBuffer(GL_ARRAY_BUFFER, attribute_buffer(im->instance_color, GL_ARRAY_BUFFER));
      glEnableVertexAttribArray(A_INSTANCE_COLOR);
      glVertexAttribPointer(A_INSTANCE_COLOR, 3, GL_FLOAT, GL_FALSE, 12, NULL);
      glVertexAttribDivisor(A_INSTANCE_COLOR, 1);
    }
  }
  return vao;
}

static void refresh_attributes(t3_geometry *g) {
  /* re-upload any attribute whose version moved; buffers stay bound to the
   * VAO, so only the data changes */
  for (int s = 0; s < T3_ATTR_COUNT; s++) {
    t3_attribute *a = g->attributes[s];
    if (a && a->_gl && ((attribute_gl *)a->_gl)->version != a->version) attribute_buffer(a, GL_ARRAY_BUFFER);
  }
  for (int i = 0; i < g->named_count; i++) {
    t3_attribute *a = g->named[i].attribute;
    if (a->_gl && ((attribute_gl *)a->_gl)->version != a->version) attribute_buffer(a, GL_ARRAY_BUFFER);
  }
  t3_attribute *ix = g->index;
  if (ix && ix->_gl && ((attribute_gl *)ix->_gl)->version != ix->version) {
    /* ELEMENT_ARRAY_BUFFER binding is VAO state: the caller has the VAO bound */
    attribute_buffer(ix, GL_ELEMENT_ARRAY_BUFFER);
  }
}

static void use_geometry_bind(t3_renderer *r, t3_object *o, t3_geometry *g, bool bind);
static void delete_program_vaos(t3_renderer *r, geometry_gl *gg) {
  for (int i = 0; i < gg->pv_n; i++) {
    if (r && r->cur_vao == gg->pv[i].vao) r->cur_vao = 0;
    glDeleteVertexArrays(1, &gg->pv[i].vao);
  }
  gg->pv_n = 0;
}

static void use_plain_geometry(t3_renderer *r, t3_geometry *g, bool bind);
static void use_geometry_bind(t3_renderer *r, t3_object *o, t3_geometry *g, bool bind);
static void use_geometry(t3_renderer *r, t3_object *o, t3_geometry *g) { use_geometry_bind(r, o, g, true); }

static void use_geometry_bind(t3_renderer *r, t3_object *o, t3_geometry *g, bool bind) {
  if (o->type != T3_INSTANCED_MESH) {
    use_plain_geometry(r, g, bind);
    return;
  }
  {
    t3_instanced_mesh *im = (t3_instanced_mesh *)o;
    geometry_gl *gg = im->_gl;
    if (gg && (gg->geometry_version != g->version || (im->instance_color != NULL) != gg->instance_color_bound)) {
      delete_program_vaos(r, gg);
      glDeleteVertexArrays(1, &gg->vao);
      if (r->cur_vao == gg->vao) r->cur_vao = 0;
      free(gg);
      gg = im->_gl = NULL;
    }
    if (!gg) {
      gg = calloc(1, sizeof *gg);
      T3_CHECK_ALLOC(gg);
      gg->vao = build_vao(r, g, im);
      gg->geometry_version = g->version;
      gg->instance_color_bound = im->instance_color != NULL;
      im->_gl = gg;
    }
    bind_vao(r, gg->vao);
    refresh_attributes(g);
    attribute_gl *mg = im->instance_matrix->_gl;
    if (mg->version != im->instance_matrix->version) attribute_buffer(im->instance_matrix, GL_ARRAY_BUFFER);
    if (im->instance_color && ((attribute_gl *)im->instance_color->_gl)->version != im->instance_color->version)
      attribute_buffer(im->instance_color, GL_ARRAY_BUFFER);
  }
}

/* The geometry's own GL state (buffers + plain VAO), created or refreshed;
 * the plain VAO is bound only when asked. */
static void use_plain_geometry(t3_renderer *r, t3_geometry *g, bool bind) {
  geometry_gl *gg = g->_gl;
  if (gg && gg->geometry_version != g->version) {
    delete_program_vaos(r, gg);
    glDeleteVertexArrays(1, &gg->vao);
    if (gg->batch_vao) glDeleteVertexArrays(1, &gg->batch_vao);
    if (r->cur_vao == gg->vao || r->cur_vao == gg->batch_vao) r->cur_vao = 0;
    free(gg);
    gg = g->_gl = NULL;
  }
  if (!gg) {
    gg = calloc(1, sizeof *gg);
    T3_CHECK_ALLOC(gg);
    gg->vao = build_vao(r, g, NULL);
    gg->geometry_version = g->version;
    g->_gl = gg;
  }
  /* A batch draw does not need the plain VAO bound, except to re-upload an
   * index buffer: that binds ELEMENT_ARRAY_BUFFER, which is VAO state, and
   * would otherwise land in whichever VAO is current. */
  t3_attribute *ix = g->index;
  if (bind || (ix && ix->_gl && ((attribute_gl *)ix->_gl)->version != ix->version)) bind_vao(r, gg->vao);
  refresh_attributes(g);
}

static void flip_rows(uint8_t *dst, const uint8_t *src, int w, int h, size_t texel) {
  size_t row = (size_t)w * texel;
  for (int y = 0; y < h; y++) memcpy(dst + (size_t)(h - 1 - y) * row, src + (size_t)y * row, row);
}

/* WebGLUtils.convert / getInternalFormat (WebGL2) for the formats three.c
 * keeps: RGBA (RGB uploads as RGBA) and depth */
static GLenum gl_type(t3_texture_type t) {
  switch (t) {
  case T3_FLOAT_TYPE: return GL_FLOAT;
  case T3_HALF_FLOAT_TYPE: return GL_HALF_FLOAT;
  case T3_UNSIGNED_SHORT_TYPE: return GL_UNSIGNED_SHORT;
  case T3_UNSIGNED_INT_TYPE: return GL_UNSIGNED_INT;
  case T3_UNSIGNED_INT_248_TYPE: return GL_UNSIGNED_INT_24_8;
  default: return GL_UNSIGNED_BYTE;
  }
}
static bool is_depth_format(t3_texture_format f) { return f == T3_DEPTH_FORMAT || f == T3_DEPTH_STENCIL_FORMAT; }
static GLenum gl_format(const t3_texture *t) {
  return t->format == T3_DEPTH_FORMAT ? GL_DEPTH_COMPONENT : t->format == T3_DEPTH_STENCIL_FORMAT ? GL_DEPTH_STENCIL : GL_RGBA;
}
static GLenum gl_internal_format(const t3_texture *t) {
  if (is_depth_format(t->format)) {
    /* uploadTexture's DepthTexture branch (WebGL2 wants a sized format) */
    switch (t->type) {
    case T3_FLOAT_TYPE: return GL_DEPTH_COMPONENT32F;
    case T3_UNSIGNED_INT_TYPE: return GL_DEPTH_COMPONENT24;
    case T3_UNSIGNED_INT_248_TYPE: return GL_DEPTH24_STENCIL8;
    default: return GL_DEPTH_COMPONENT16;
    }
  }
  /* r186 (colour management on): an sRGB texture is stored sRGB and GL
   * decodes on sampling (WebGL backend: SRGB8_ALPHA8); the generated programs
   * do no decode of their own */
  if (t->color_space == T3_SRGB_COLOR_SPACE && t3_get_color_management() && t->type != T3_FLOAT_TYPE && t->type != T3_HALF_FLOAT_TYPE)
    return 0x8C43; /* GL_SRGB8_ALPHA8 */
  return t->type == T3_FLOAT_TYPE ? GL_RGBA32F : t->type == T3_HALF_FLOAT_TYPE ? GL_RGBA16F : GL_RGBA8;
}
static size_t texel_size(const t3_texture *t) {
  return t->type == T3_FLOAT_TYPE ? 16 : t->type == T3_HALF_FLOAT_TYPE ? 8 : 4;
}

static GLenum gl_wrap(t3_wrapping w) {
  return w == T3_REPEAT ? GL_REPEAT : w == T3_MIRRORED_REPEAT ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE;
}
static GLenum gl_filter(t3_filter f) {
  switch (f) {
  case T3_NEAREST: return GL_NEAREST;
  case T3_NEAREST_MIPMAP_NEAREST: return GL_NEAREST_MIPMAP_NEAREST;
  case T3_NEAREST_MIPMAP_LINEAR: return GL_NEAREST_MIPMAP_LINEAR;
  case T3_LINEAR: return GL_LINEAR;
  case T3_LINEAR_MIPMAP_NEAREST: return GL_LINEAR_MIPMAP_NEAREST;
  default: return GL_LINEAR_MIPMAP_LINEAR;
  }
}

/* ── texture units ───────────────────────────────────────────────────
 * Which texture is bound to each unit, and the active unit: a bind that
 * would change nothing is skipped (WebGLState.bindTexture does the same).
 * Binding state belongs to the GL context, which renderers share, so this is
 * file-wide. A deleted texture's name can come back from glGenTextures, so
 * deleting forgets it; a texture attached to the framebuffer being drawn into
 * is unbound by WebGL hosts (no feedback loops), so binding such a
 * framebuffer forgets it too; t3_renderer_reset_state forgets everything. */
static GLuint tu_tex[32];
static uint32_t tu_valid;
static int tu_active = -1;

static GLuint tu_cube[32];
static uint32_t tu_cube_valid;
static void tu_forget_all(void) { tu_valid = tu_cube_valid = 0; tu_active = -1; }
static void tu_forget_active(void) { tu_active = -1; }
static int tu_active_unit(void) { return tu_active; }
static void tu_forget(GLuint tex) {
  for (int u = 0; u < 32; u++) {
    if ((tu_valid >> u & 1) && tu_tex[u] == tex) tu_valid &= ~(1u << u);
    if ((tu_cube_valid >> u & 1) && tu_cube[u] == tex) tu_cube_valid &= ~(1u << u);
  }
}
static void tu_activate(int unit) {
  if (tu_active == unit) return;
  glActiveTexture(GL_TEXTURE0 + unit);
  tu_active = unit;
}
/* tex on unit; with select, the unit is also left active (to upload to it) */
static void tu_bind(int unit, GLuint tex, bool select) {
  if ((tu_valid >> unit & 1) && tu_tex[unit] == tex) {
    if (select) tu_activate(unit);
    return;
  }
  tu_activate(unit);
  glBindTexture(GL_TEXTURE_2D, tex);
  tu_tex[unit] = tex;
  tu_valid |= 1u << unit;
}

static texture_gl *texture_gl_of(t3_texture *t) {
  texture_gl *tg = t->_gl;
  if (!tg) {
    tg = calloc(1, sizeof *tg);
    T3_CHECK_ALLOC(tg);
    glGenTextures(1, &tg->tex);
    t->_gl = tg;
  }
  return tg;
}

static void gl_release(uint32_t kind, void *thing);

void t3_texture_adopt_gl(t3_texture *t, unsigned gl_name, int w, int h, int levels, bool owns_gl) {
  /* releasing a texture frees its GL object through this hook, set by the
   * first renderer: an adopted texture may come before any renderer */
  t3__gl_release = gl_release;
  if (t->_gl) gl_release(T3_KIND_TEXTURE, t);
  free(t->pixels);
  t->pixels = NULL; /* never uploaded */
  t->width = w;
  t->height = h;
  t->flip_y = false;
  texture_gl *tg = calloc(1, sizeof *tg);
  T3_CHECK_ALLOC(tg);
  tg->tex = gl_name;
  tg->version = t->version;
  tg->max_mip = levels > 1 ? (float)(levels - 1) : 0;
  tg->borrowed = !owns_gl;
  t->_gl = tg;
  /* the embedder bound it (on some unit) while filling it */
  tu_forget_all();
}

t3_texture *t3_texture_from_gl(unsigned gl_name, int w, int h, int levels, bool is_cube, bool owns_gl) {
  t3_texture *t = t3_texture_new(1, 1, NULL);
  t->generate_mipmaps = false;
  if (is_cube) {
    t->is_cube = t->needs_flip_env_map = true;
    t->mapping = T3_CUBE_REFLECTION_MAPPING;
  }
  t3_texture_adopt_gl(t, gl_name, w, h, levels, owns_gl);
  return t;
}

void t3_texture_swap_gl(t3_texture *a, t3_texture *b) {
  void *g = a->_gl;
  int w = a->width, h = a->height;
  uint8_t *px = a->pixels;
  a->_gl = b->_gl; a->width = b->width; a->height = b->height; a->pixels = b->pixels;
  b->_gl = g; b->width = w; b->height = h; b->pixels = px;
  /* each record matches its new owner's version: nothing re-uploads */
  if (a->_gl) ((texture_gl *)a->_gl)->version = a->version;
  if (b->_gl) ((texture_gl *)b->_gl)->version = b->version;
}

static bool needs_mipmaps(const t3_texture *t) {
  /* textureNeedsGenerateMipmaps: generateMipmaps and a mipmap min filter */
  return t->generate_mipmaps && t->min_filter != T3_NEAREST && t->min_filter != T3_LINEAR;
}

/* setTextureParameters, on the texture bound to target of the active unit */
static void texture_params(GLenum target, const t3_texture *t) {
  glTexParameteri(target, GL_TEXTURE_WRAP_S, gl_wrap(t->wrap_s));
  glTexParameteri(target, GL_TEXTURE_WRAP_T, gl_wrap(t->wrap_t));
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, t->mag_filter == T3_NEAREST ? GL_NEAREST : GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, gl_filter(t->min_filter));
  if (t->anisotropy > 1) {
    /* min(texture.anisotropy, capabilities.getMaxAnisotropy()) */
    GLfloat mx = 1;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &mx);
    glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, t->anisotropy < mx ? t->anisotropy : mx);
  }
}

/* tex on unit's cube map target (the unit's 2D binding is its own) */
static void tu_bind_cube(int unit, GLuint tex, bool select) {
  if ((tu_cube_valid >> unit & 1) && tu_cube[unit] == tex) {
    if (select) tu_activate(unit);
    return;
  }
  tu_activate(unit);
  glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
  tu_cube[unit] = tex;
  tu_cube_valid |= 1u << unit;
}

/* a CubeTexture (uploadCubeTexture): six faces, no flipY, then mipmaps */
static void bind_cube_texture(t3_texture *t, int unit) {
  texture_gl *tg = texture_gl_of(t);
  bool upload = tg->version != t->version && t->pixels;
  tu_bind_cube(unit, tg->tex, upload);
  if (!upload) return;
  size_t face = (size_t)t->width * t->height * texel_size(t);
  int align = t->unpack_alignment ? t->unpack_alignment : 4;
  glPixelStorei(GL_UNPACK_ALIGNMENT, align);
  for (int i = 0; i < 6; i++)
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, gl_internal_format(t), t->width, t->height, 0, gl_format(t),
                 gl_type(t->type), t->pixels + face * i);
  if (align != 4) glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  texture_params(GL_TEXTURE_CUBE_MAP, t);
  tg->max_mip = 0;
  if (needs_mipmaps(t)) {
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    tg->max_mip = log2f((float)(t->width > t->height ? t->width : t->height));
  }
  tg->version = t->version;
}

static void bind_texture(t3_texture *t, int unit) {
  texture_gl *tg = texture_gl_of(t);
  /* a render target's (or depth) texture has no texels to upload: the GPU
   * writes it, and its storage and parameters are set up with the target */
  bool upload = tg->version != t->version && t->pixels;
  tu_bind(unit, tg->tex, upload);
  if (upload) {
    const uint8_t *px = t->pixels;
    uint8_t *tmp = NULL;
    size_t ts = texel_size(t);
    if (t->flip_y) {
      /* GLES has no UNPACK_FLIP_Y; WebGL's flipY puts row 0 at the bottom */
      tmp = malloc((size_t)t->width * t->height * ts);
      T3_CHECK_ALLOC(tmp);
      flip_rows(tmp, px, t->width, t->height, ts);
      px = tmp;
    }
    int align = t->unpack_alignment ? t->unpack_alignment : 4;
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    glTexImage2D(GL_TEXTURE_2D, 0, gl_internal_format(t), t->width, t->height, 0, gl_format(t), gl_type(t->type), px);
    /* leave GL's default behind: an embedder's own uploads assume it */
    if (align != 4) glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    free(tmp);
    texture_params(GL_TEXTURE_2D, t);
    if (needs_mipmaps(t)) glGenerateMipmap(GL_TEXTURE_2D);
  }
  tg->version = t->version;
}

/* ── render targets (WebGLTextures.setupRenderTarget) ────────────── */
typedef struct {
  GLuint fbo, depth_rb;              /* the target (its texture) and its depth renderbuffer */
  GLuint msaa_fbo, msaa_color, msaa_depth; /* a multisampled target: drawn here, blitted to fbo */
  int w, h;
} rt_gl;

static void rt_free(t3_render_target *rt) {
  rt_gl *g = rt->_gl;
  if (!g) return;
  glDeleteFramebuffers(1, &g->fbo);
  if (g->depth_rb) glDeleteRenderbuffers(1, &g->depth_rb);
  if (g->msaa_fbo) {
    glDeleteFramebuffers(1, &g->msaa_fbo);
    glDeleteRenderbuffers(1, &g->msaa_color);
    if (g->msaa_depth) glDeleteRenderbuffers(1, &g->msaa_depth);
  }
  free(g);
  rt->_gl = NULL;
}

#define T3_SCRATCH_UNIT 31 /* uploads that are not draws (render target setup, mipmaps) */

/* a target's depth renderbuffer: r186 gives every target a depth texture of
 * UnsignedIntType (UnsignedInt248Type with stencil), so 24 bit unless a
 * depth texture asks for another type */
static void rt_depth_storage(const t3_render_target *rt, int samples) {
  GLenum fmt = rt->stencil_buffer ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
  if (!rt->stencil_buffer && rt->depth_texture) {
    if (rt->depth_texture->type == T3_FLOAT_TYPE) fmt = GL_DEPTH_COMPONENT32F;
    else if (rt->depth_texture->type == T3_UNSIGNED_SHORT_TYPE) fmt = GL_DEPTH_COMPONENT16;
  }
  if (samples) glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, fmt, rt->width, rt->height);
  else glRenderbufferStorage(GL_RENDERBUFFER, fmt, rt->width, rt->height);
}

static void gl_fbo(t3_renderer *r, GLuint fbo);
static void gl_clear_color(t3_renderer *r, float cr, float cg, float cb, float ca);
static void rt_initial_clear(t3_renderer *r, GLuint fbo, const t3_render_target *rt) {
  gl_fbo(r, fbo);
  if (r->cur_scissor != 0) { glDisable(GL_SCISSOR_TEST); r->cur_scissor = 0; }
  gl_clear_color(r, 0, 0, 0, 0);
  if (r->cur_depth_write != 1) { glDepthMask(GL_TRUE); r->cur_depth_write = 1; }
  glClear(GL_COLOR_BUFFER_BIT | (rt->depth_buffer ? GL_DEPTH_BUFFER_BIT : 0) | (rt->stencil_buffer ? GL_STENCIL_BUFFER_BIT : 0));
}

static rt_gl *rt_setup(t3_renderer *r, t3_render_target *rt) {
  rt_gl *g = rt->_gl;
  if (g && g->w == rt->width && g->h == rt->height) return g;
  if (g) rt_free(rt);
  g = calloc(1, sizeof *g);
  T3_CHECK_ALLOC(g);
  rt->_gl = g;
  g->w = rt->width;
  g->h = rt->height;
  t3_texture *t = rt->texture;
  GLenum ifmt = gl_internal_format(t);
  glGenFramebuffers(1, &g->fbo);
  if (rt->samples) {
    GLint mx = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &mx);
    int samples = rt->samples < mx ? rt->samples : mx;
    glGenFramebuffers(1, &g->msaa_fbo);
    glGenRenderbuffers(1, &g->msaa_color);
    glBindRenderbuffer(GL_RENDERBUFFER, g->msaa_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, ifmt, rt->width, rt->height);
    glBindFramebuffer(GL_FRAMEBUFFER, g->msaa_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g->msaa_color);
    if (rt->depth_buffer) {
      glGenRenderbuffers(1, &g->msaa_depth);
      glBindRenderbuffer(GL_RENDERBUFFER, g->msaa_depth);
      rt_depth_storage(rt, samples);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, rt->stencil_buffer ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, g->msaa_depth);
    }
  }
  /* the color texture */
  texture_gl *tg = texture_gl_of(t);
  tu_bind(T3_SCRATCH_UNIT, tg->tex, true);
  texture_params(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, ifmt, rt->width, rt->height, 0, gl_format(t), gl_type(t->type), NULL);
  glBindFramebuffer(GL_FRAMEBUFFER, g->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tg->tex, 0);
  if (needs_mipmaps(t)) glGenerateMipmap(GL_TEXTURE_2D);
  tg->version = t->version;
  /* setupDepthRenderbuffer: a depth texture, else a renderbuffer */
  if (rt->depth_buffer) {
    t3_texture *dt = rt->depth_texture;
    if (dt) {
      texture_gl *dg = texture_gl_of(dt);
      dt->width = rt->width;
      dt->height = rt->height;
      tu_bind(T3_SCRATCH_UNIT, dg->tex, true);
      glTexImage2D(GL_TEXTURE_2D, 0, gl_internal_format(dt), rt->width, rt->height, 0, gl_format(dt), gl_type(dt->type), NULL);
      texture_params(GL_TEXTURE_2D, dt);
      dg->version = dt->version;
      glFramebufferTexture2D(GL_FRAMEBUFFER, dt->format == T3_DEPTH_STENCIL_FORMAT ? GL_DEPTH_STENCIL_ATTACHMENT
                             : GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dg->tex, 0);
    } else {
      glGenRenderbuffers(1, &g->depth_rb);
      glBindRenderbuffer(GL_RENDERBUFFER, g->depth_rb);
      rt_depth_storage(rt, 0);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, rt->stencil_buffer ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, g->depth_rb);
    }
  }
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  r->fb_known = true;
  r->cur_fbo = g->fbo;
  /* WebGL hands out new storage cleared (colour 0, depth 1, stencil 0); GLES
   * leaves it undefined, so clear it once, as a WebGL host would */
  rt_initial_clear(r, g->fbo, rt);
  if (g->msaa_fbo) rt_initial_clear(r, g->msaa_fbo, rt);
  gl_fbo(r, g->fbo);
  return g;
}

/* after a render into rt: its mipmaps, then the multisample resolve */
static void rt_finish(t3_renderer *r, t3_render_target *rt) {
  rt_gl *g = rt->_gl;
  if (!g) return;
  t3_texture *t = rt->texture;
  /* resolve first: the mipmaps are built from the resolved image */
  if (rt->samples) {
    GLbitfield mask = GL_COLOR_BUFFER_BIT | (rt->depth_buffer ? GL_DEPTH_BUFFER_BIT : 0) |
                      (rt->stencil_buffer ? GL_STENCIL_BUFFER_BIT : 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g->msaa_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g->fbo);
    glBlitFramebuffer(0, 0, rt->width, rt->height, 0, 0, rt->width, rt->height, mask, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, g->msaa_fbo);
    r->fb_known = true;
    r->cur_fbo = g->msaa_fbo;
  }
  if (needs_mipmaps(t)) {
    tu_bind(T3_SCRATCH_UNIT, ((texture_gl *)t->_gl)->tex, true);
    glGenerateMipmap(GL_TEXTURE_2D);
  }
}

static GLuint target_fbo(t3_renderer *r) {
  t3_render_target *rt = r->target;
  if (!rt && !r->gen_direct && (r->output_color_space == T3_SRGB_COLOR_SPACE || r->tone_mapping)) return gen_out_target(r);
  if (!rt) return own_msaa(r) ? msaa_target(r) : r->screen_fbo;
  rt_gl *g = rt_setup(r, rt);
  GLuint fbo = rt->samples ? g->msaa_fbo : g->fbo;
  if (!r->fb_known || r->cur_fbo != fbo) {
    /* hosts unbind a texture attached to the framebuffer being drawn into */
    if (rt->texture->_gl) tu_forget(((texture_gl *)rt->texture->_gl)->tex);
    if (rt->depth_texture && rt->depth_texture->_gl) tu_forget(((texture_gl *)rt->depth_texture->_gl)->tex);
  }
  return fbo;
}

/* ── lights (WebGLLights.setup / setupView) ──────────────────────── */
/* r186 orders a program's lights by object id (LightsNode sortLights) */
static void lights_by_id(t3_light **a, int n) {
  for (int i = 1; i < n; i++) {
    t3_light *x = a[i];
    int j = i - 1;
    while (j >= 0 && a[j]->base.id > x->base.id) { a[j + 1] = a[j]; j--; }
    a[j + 1] = x;
  }
}
static void setup_lights(t3_renderer *r, const t3_camera *cam) {
  light_state *ls = &r->ls;
  memset(ls, 0, sizeof *ls);
  /* shadow casters first (a stable sort), so a type's first numXShadows
   * lights are its casters; lights_by_id then orders each type as r186's
   * LightsNode does */
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < r->light_count; i++) {
      t3_light *l = r->lights[i];
      bool casts = l->base.cast_shadow && l->shadow;
      if (pass == 0 ? !casts : casts) continue;
      if (casts) {
        if (l->base.type == T3_DIRECTIONAL_LIGHT) ls->num_dir_shadows++;
        else if (l->base.type == T3_SPOT_LIGHT) ls->num_spot_shadows++;
        else if (l->base.type == T3_POINT_LIGHT) ls->num_point_shadows++;
      }
      switch (l->base.type) {
      case T3_AMBIENT_LIGHT:
        ls->ambient.r += l->color.r * l->intensity;
        ls->ambient.g += l->color.g * l->intensity;
        ls->ambient.b += l->color.b * l->intensity;
        break;
      case T3_DIRECTIONAL_LIGHT:
        if (ls->num_dir < T3_MAX_LIGHTS) ls->dir[ls->num_dir++] = l;
        break;
      case T3_POINT_LIGHT:
        if (ls->num_point < T3_MAX_LIGHTS) ls->point[ls->num_point++] = l;
        break;
      case T3_SPOT_LIGHT:
        if (ls->num_spot < T3_MAX_LIGHTS) ls->spot[ls->num_spot++] = l;
        break;
      case T3_HEMISPHERE_LIGHT:
        if (ls->num_hemi < T3_MAX_LIGHTS) ls->hemi[ls->num_hemi++] = l;
        break;
      default: break;
      }
    }
  lights_by_id(ls->dir, ls->num_dir); lights_by_id(ls->point, ls->num_point);
  lights_by_id(ls->spot, ls->num_spot); lights_by_id(ls->hemi, ls->num_hemi);
  const t3_mat4 *view = &cam->matrix_world_inverse;
  for (int i = 0; i < ls->num_dir; i++) {
    t3_light *l = ls->dir[i];
    t3_vec3 d = t3_vec3_sub(t3_mat4_get_position(&l->base.matrix_world), t3_mat4_get_position(&l->target->matrix_world));
    ls->dir_direction[i] = t3_vec3_transform_direction(d, view);
  }
  for (int i = 0; i < ls->num_point; i++)
    ls->point_position[i] = t3_vec3_apply_mat4(t3_mat4_get_position(&ls->point[i]->base.matrix_world), view);
  for (int i = 0; i < ls->num_spot; i++) {
    t3_light *l = ls->spot[i];
    t3_vec3 p = t3_mat4_get_position(&l->base.matrix_world);
    ls->spot_position[i] = t3_vec3_apply_mat4(p, view);
    ls->spot_direction[i] = t3_vec3_transform_direction(t3_vec3_sub(p, t3_mat4_get_position(&l->target->matrix_world)), view);
  }
  for (int i = 0; i < ls->num_hemi; i++)
    ls->hemi_direction[i] = t3_vec3_transform_direction(t3_mat4_get_position(&ls->hemi[i]->base.matrix_world), view);
}

/* Texture.updateMatrix: setUvTransform(offset, repeat, rotation, center) */
static void uv_transform(const t3_texture *t, float mt[9]) {
  float c = cosf(t->rotation), s = sinf(t->rotation), sx = t->repeat.x, sy = t->repeat.y;
  float cx = t->center.x, cy = t->center.y, tx = t->offset.x, ty = t->offset.y;
  mt[0] = sx * c; mt[1] = -sy * s; mt[2] = 0;
  mt[3] = sx * s; mt[4] = sy * c; mt[5] = 0;
  mt[6] = -sx * (c * cx + s * cy) + cx + tx; mt[7] = -sy * (-s * cx + c * cy) + cy + ty; mt[8] = 1;
}

/* ── GL state ────────────────────────────────────────────────────── */

/* three.js's *Factor (200..210) and *Equation (100..104) constants */
static GLenum gl_factor(int f) {
  static const GLenum map[] = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA,
                                GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR,
                                GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA_SATURATE };
  return f >= 200 && f <= 210 ? map[f - 200] : GL_ONE;
}
static GLenum gl_equation(int e) {
  static const GLenum map[] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX };
  return e >= 100 && e <= 104 ? map[e - 100] : GL_FUNC_ADD;
}
/* NormalBlending on an opaque material is NoBlending, as three.js */
static int blend_key(const t3_material *m) {
  int b = m->blending;
  if (b <= 0 || (b == 1 && !m->transparent)) return 0;
  if (b != 5) return b | (m->premultiplied_alpha ? 8 : 0);
  int sa = m->blend_src_alpha < 0 ? 0 : m->blend_src_alpha - 199;
  int da = m->blend_dst_alpha < 0 ? 0 : m->blend_dst_alpha - 199;
  int ea = m->blend_equation_alpha < 0 ? 0 : m->blend_equation_alpha - 99;
  return 5 | (m->blend_src - 199) << 4 | (m->blend_dst - 199) << 8 | (m->blend_equation - 99) << 12 | sa << 15 |
         da << 19 | ea << 23;
}
/* WebGLState.setBlending */
static void apply_blending(const t3_material *m) {
  int b = m->blending;
  if (b == 5) {
    glBlendEquationSeparate(gl_equation(m->blend_equation),
                            gl_equation(m->blend_equation_alpha < 0 ? m->blend_equation : m->blend_equation_alpha));
    glBlendFuncSeparate(gl_factor(m->blend_src), gl_factor(m->blend_dst),
                        gl_factor(m->blend_src_alpha < 0 ? m->blend_src : m->blend_src_alpha),
                        gl_factor(m->blend_dst_alpha < 0 ? m->blend_dst : m->blend_dst_alpha));
    return;
  }
  glBlendEquation(GL_FUNC_ADD);
  if (m->premultiplied_alpha) {
    switch (b) {
    case 2: glBlendFunc(GL_ONE, GL_ONE); break;
    case 3: glBlendFuncSeparate(GL_ZERO, GL_ZERO, GL_ONE_MINUS_SRC_COLOR, GL_ONE_MINUS_SRC_ALPHA); break;
    case 4: glBlendFuncSeparate(GL_ZERO, GL_SRC_COLOR, GL_ZERO, GL_SRC_ALPHA); break;
    default: glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
    }
  } else {
    switch (b) {
    case 2: glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
    case 3: glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR); break;
    case 4: glBlendFunc(GL_ZERO, GL_SRC_COLOR); break;
    default: glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
    }
  }
}
static GLenum gl_depth_func(int f) {
  static const GLenum map[] = { GL_NEVER, GL_ALWAYS, GL_LESS, GL_LEQUAL, GL_EQUAL, GL_GEQUAL, GL_GREATER, GL_NOTEQUAL };
  return f >= 0 && f <= 7 ? map[f] : GL_LEQUAL;
}

static void set_color_mask(t3_renderer *r, int on) {
  if (r->cur_color_mask == on) return;
  glColorMask(on, on, on, on);
  r->cur_color_mask = on;
}

/* WebGLState.setMaterial */
static void set_material_state(t3_renderer *r, const t3_material *m) {
  int cull = m->side == T3_DOUBLE_SIDE ? 0 : m->side == T3_BACK_SIDE ? 2 : 1;
  if (cull != r->cur_cull) {
    if (!cull) glDisable(GL_CULL_FACE);
    else {
      glEnable(GL_CULL_FACE);
      glCullFace(cull == 1 ? GL_BACK : GL_FRONT);
    }
    r->cur_cull = cull;
  }
  /* depth test on, with the material's function, as one cached value
   * (1 + depthFunc), 0 = off */
  int dt = m->depth_test ? 1 + m->depth_func : 0;
  if (dt != r->cur_depth_test) {
    if (dt) {
      if (r->cur_depth_test <= 0) glEnable(GL_DEPTH_TEST);
      glDepthFunc(gl_depth_func(m->depth_func));
    } else {
      glDisable(GL_DEPTH_TEST);
    }
    r->cur_depth_test = dt;
  }
  int dw = m->depth_write;
  if (dw != r->cur_depth_write) {
    glDepthMask(dw ? GL_TRUE : GL_FALSE);
    r->cur_depth_write = dw;
  }
  int bl = blend_key(m);
  if (bl != r->cur_blend) {
    if (bl) {
      if (r->cur_blend <= 0) glEnable(GL_BLEND);
      apply_blending(m);
      r->blend_func_set = true;
    } else {
      glDisable(GL_BLEND);
    }
    r->cur_blend = bl;
  }
  set_color_mask(r, m->color_write);
}

/* ── render list ─────────────────────────────────────────────────── */
static void list_push(render_list *l, render_item it) {
  if (l->n == l->cap) {
    l->cap = l->cap ? l->cap * 2 : 256;
    l->items = realloc(l->items, l->cap * sizeof *l->items);
    T3_CHECK_ALLOC(l->items);
  }
  l->items[l->n++] = it;
}
/* the materials of the back-face pass, set to one side for a pass (r186
 * mutates material.side the same way) */
static void backpass_side(t3_renderer *r, t3_side side) {
  for (int i = 0; i < r->backpass.n; i++) ((t3_material *)r->backpass.items[i].material)->side = side;
}

/* Frustum.intersectsObject: the geometry's bounding sphere through the world
 * matrix. World matrices are affine, so the centre needs no divide
 * (three.js's applyMatrix4 divides by w = 1). */
T3_HOT static bool intersects_frustum(t3_renderer *r, t3_object *o, t3_geometry *g) {
  /* the world sphere is cached on the object (raycasting shares it) */
  const float *ws = t3__world_sphere(o, g);
  const float cx = ws[0], cy = ws[1], cz = ws[2], neg = -ws[3];
  const t3_plane *p = r->frustum.planes;
  for (int i = 0; i < 6; i++)
    if (p[i].normal.x * cx + p[i].normal.y * cy + p[i].normal.z * cz + p[i].constant < neg) return false;
  return true;
}

T3_HOT static bool batchable_matrix(const t3_mat4 *m);
static void batch_add(t3_renderer *r, t3_object *o, t3_geometry *g, t3_material *m, const t3_group *grp);

/* The projected depth three.js sorts by: the object's origin through
 * projection * view. */
static float item_z(const t3_renderer *r, const t3_object *o) {
  const float *e = o->matrix_world.e;
  return t3_vec3_apply_mat4(t3_v3(e[12], e[13], e[14]), &r->proj_screen).z;
}

/* Opaque items get their z later (build_batches): a batched mesh never needs
 * one, and that was a perspective divide per mesh per frame. */
T3_HOT static void push_mesh_item(t3_renderer *r, t3_object *o, t3_geometry *g, t3_material *m, const t3_group *grp,
                           bool can_batch) {
  if (!m || !m->visible) return;
  /* r186 RenderList: transmission goes with the transparent objects */
  bool tr = m->transparent || (m->type == T3_MESH_PHYSICAL_MATERIAL && m->transmission > 0);
  if (can_batch && !tr) { batch_add(r, o, g, m, grp); return; }
  render_item it = { o, g, m, grp, item_z(r, o), o->render_order, 0, 0 };
  list_push(tr ? &r->transparent : &r->opaque, it);
}

/* projectObject for one object (not its children). in_frustum: the frustum
 * test's answer if the caller has it (1 / 0), or -1 to test here. */
/* an LOD's level pick, out of the per-object loop's way */
__attribute__((noinline)) static void project_lod(t3_renderer *r, t3_object *o) {
  if (((t3_lod *)o)->auto_update) t3_lod_update((t3_lod *)o, r->camera);
}

T3_HOT static void project_one(t3_renderer *r, t3_object *o, int in_frustum) {
  switch (o->type) {
  case T3_AMBIENT_LIGHT: case T3_HEMISPHERE_LIGHT: case T3_DIRECTIONAL_LIGHT:
  case T3_POINT_LIGHT: case T3_SPOT_LIGHT:
    if (r->light_count == r->light_cap) {
      r->light_cap = r->light_cap ? r->light_cap * 2 : 8;
      r->lights = realloc(r->lights, r->light_cap * sizeof *r->lights);
      T3_CHECK_ALLOC(r->lights);
    }
    r->lights[r->light_count++] = (t3_light *)o;
    if (o->cast_shadow && ((t3_light *)o)->shadow) {
      if (r->caster_count == r->caster_cap) {
        r->caster_cap = r->caster_cap ? r->caster_cap * 2 : 4;
        r->casters = realloc(r->casters, (size_t)r->caster_cap * sizeof *r->casters);
        T3_CHECK_ALLOC(r->casters);
      }
      r->casters[r->caster_count++] = (t3_light *)o;
    }
    break;
  case T3_MESH: case T3_INSTANCED_MESH: case T3_SKINNED_MESH: {
    t3_mesh *m = (t3_mesh *)o;
    t3_geometry *g = m->geometry;
    if (!g || !g->attributes[T3_ATTR_POSITION]) break;
    if (o->type == T3_SKINNED_MESH) {
      /* WebGLRenderer.projectObject: each skeleton updates once a frame,
       * before culling */
      t3_skeleton *sk = ((t3_skinned_mesh *)o)->skeleton;
      if (sk && sk->frame != r->info.frame) {
        t3_skeleton_update(sk);
        sk->frame = r->info.frame;
      }
    }
    if (o->frustum_culled && !(in_frustum >= 0 ? in_frustum : intersects_frustum(r, o, g))) break;
    /* a morphed mesh draws on its own: its attributes and influences are its own */
    bool can_batch = r->auto_instancing && o->type == T3_MESH && !g->morph_count && batchable_matrix(&o->matrix_world);
    if (m->material_count > 1 && g->group_count) {
      for (int i = 0; i < g->group_count; i++) {
        int mi = g->groups[i].material_index;
        if (mi >= 0 && mi < m->material_count) push_mesh_item(r, o, g, m->materials[mi], &g->groups[i], can_batch);
      }
    } else if (m->material_count) {
      push_mesh_item(r, o, g, m->materials[0], NULL, can_batch);
    }
    break;
  }
  case T3_LOD:
    /* projectObject: an LOD picks its level before its children are seen */
    project_lod(r, o);
    break;
  case T3_LINE: case T3_LINE_SEGMENTS: case T3_LINE_LOOP: case T3_POINTS: case T3_SPRITE: {
    t3_mesh *m = (t3_mesh *)o;
    t3_geometry *g = m->geometry;
    if (!g || !g->attributes[T3_ATTR_POSITION] ||
        (o->frustum_culled && !(in_frustum >= 0 ? in_frustum : intersects_frustum(r, o, g)))) break;
    if (m->material_count) push_mesh_item(r, o, g, m->materials[0], NULL, false);
    break;
  }
  default: break;
  }
}

/* ── the scene, flattened (projectObject's traversal, batched culling) ──
 * The scene graph in traversal order, each entry with the index past its
 * subtree (an invisible object skips its children), and the world bounding
 * spheres of the entries that are meshes or lines, as arrays. Rebuilt only
 * when the scene graph or a world matrix / bounds changed (the world
 * epochs), so a still scene keeps it; the frustum test then runs over the
 * arrays 4 spheres at a time. visible, frustumCulled and materials are read
 * live each frame; an entry whose geometry was swapped since the build is
 * tested the scalar way. */
typedef struct {
  t3_object *root;
  uint32_t world_epoch, bounds_epoch, graph_epoch;
  t3_object **obj; int *end, *sph; const t3_geometry **geo; int n, cap;
  float *cx, *cy, *cz, *rad; uint8_t *vis; int ns, scap;
  bool spheres_valid; /* the arrays match the current world */
} scene_flat;

static scene_flat g_flat; /* one per process: rendering another scene rebuilds it */

static bool sphere_kind(const t3_object *o) {
  switch (o->type) {
  case T3_MESH: case T3_INSTANCED_MESH: case T3_SKINNED_MESH: case T3_LINE: case T3_LINE_SEGMENTS: case T3_LINE_LOOP:
  case T3_POINTS: case T3_SPRITE: {
    const t3_geometry *g = ((const t3_mesh *)o)->geometry;
    return g && g->attributes[T3_ATTR_POSITION];
  }
  default: return false;
  }
}

static void flat_add(scene_flat *f, t3_object *o) {
  if (f->n == f->cap) {
    f->cap = f->cap ? f->cap * 2 : 256;
    f->obj = realloc(f->obj, (size_t)f->cap * sizeof *f->obj);
    f->end = realloc(f->end, (size_t)f->cap * sizeof *f->end);
    f->sph = realloc(f->sph, (size_t)f->cap * sizeof *f->sph);
    f->geo = realloc(f->geo, (size_t)f->cap * sizeof *f->geo);
    T3_CHECK_ALLOC(f->obj); T3_CHECK_ALLOC(f->end); T3_CHECK_ALLOC(f->sph); T3_CHECK_ALLOC(f->geo);
  }
  int i = f->n++;
  f->obj[i] = o;
  f->sph[i] = -1;
  f->geo[i] = NULL;
  if (sphere_kind(o)) {
    if (f->ns + 4 > f->scap) {
      f->scap = f->scap ? f->scap * 2 : 256;
      f->cx = realloc(f->cx, (size_t)f->scap * sizeof(float));
      f->cy = realloc(f->cy, (size_t)f->scap * sizeof(float));
      f->cz = realloc(f->cz, (size_t)f->scap * sizeof(float));
      f->rad = realloc(f->rad, (size_t)f->scap * sizeof(float));
      f->vis = realloc(f->vis, (size_t)f->scap);
      T3_CHECK_ALLOC(f->cx); T3_CHECK_ALLOC(f->cy); T3_CHECK_ALLOC(f->cz); T3_CHECK_ALLOC(f->rad); T3_CHECK_ALLOC(f->vis);
    }
    t3_geometry *g = ((t3_mesh *)o)->geometry;
    const float *ws = t3__world_sphere(o, g);
    int k = f->ns++;
    f->cx[k] = ws[0]; f->cy[k] = ws[1]; f->cz[k] = ws[2]; f->rad[k] = ws[3];
    f->sph[i] = k;
    f->geo[i] = g;
  }
  for (int c = 0; c < o->child_count; c++) flat_add(f, o->children[c]);
  f->end[i] = f->n;
}

static scene_flat *flatten_scene(t3_object *root) {
  scene_flat *f = &g_flat;
  bool same_world = f->world_epoch == t3__world_epoch && f->bounds_epoch == t3__bounds_epoch;
  if (f->root == root && same_world && f->obj && f->spheres_valid) return f;
  if (f->root == root && f->graph_epoch == t3__graph_epoch && f->obj) {
    f->world_epoch = t3__world_epoch;
    f->bounds_epoch = t3__bounds_epoch;
    /* Things moved since the last frame: refreshing every sphere costs more
     * than culling each object as it comes (on its cached sphere), so the
     * arrays wait until the scene holds still for a frame. */
    if (!same_world) { f->spheres_valid = false; return f; }
    for (int i = 0; i < f->n; i++) {
      int k = f->sph[i];
      if (k < 0) continue;
      t3_object *o = f->obj[i];
      t3_geometry *g = ((t3_mesh *)o)->geometry;
      if (!g || !g->attributes[T3_ATTR_POSITION]) { f->geo[i] = NULL; continue; }
      const float *ws = t3__world_sphere(o, g);
      f->cx[k] = ws[0]; f->cy[k] = ws[1]; f->cz[k] = ws[2]; f->rad[k] = ws[3];
      f->geo[i] = g;
    }
    f->spheres_valid = true;
    return f;
  }
  f->n = f->ns = 0;
  flat_add(f, root);
  f->root = root;
  f->graph_epoch = t3__graph_epoch;
  f->spheres_valid = true;
  f->world_epoch = t3__world_epoch;
  f->bounds_epoch = t3__bounds_epoch;
  return f;
}

/* Frustum.intersectsObject for every sphere, 4 at a time: the scalar test's
 * float operations lane by lane (per plane, n.x*cx + n.y*cy + n.z*cz + c < -r
 * rejects), so the same answers */
static void cull_flat(t3_renderer *r, scene_flat *f) {
  int n = f->ns;
  while (n & 3) { f->cx[n] = f->cy[n] = f->cz[n] = f->rad[n] = 0; n++; }
  const t3_plane *pl = r->frustum.planes;
  for (int i = 0; i < n; i += 4) {
    t3v4 cx = v4_load(f->cx + i), cy = v4_load(f->cy + i), cz = v4_load(f->cz + i);
    t3v4 neg = -v4_load(f->rad + i);
    t3m4 out = { 0, 0, 0, 0 };
    for (int p = 0; p < 6; p++) {
      t3v4 d = v4_set1(pl[p].normal.x) * cx + v4_set1(pl[p].normal.y) * cy + v4_set1(pl[p].normal.z) * cz +
               v4_set1(pl[p].constant);
      out |= d < neg;
    }
    unsigned rejected = m4_bits(out);
    for (int k = 0; k < 4; k++) f->vis[i + k] = !(rejected >> k & 1);
  }
}

static void project_scene(t3_renderer *r, t3_object *root, uint32_t camera_layers) {
  scene_flat *f = flatten_scene(root);
  bool batched = f->spheres_valid;
  if (batched) cull_flat(r, f);
  for (int i = 0; i < f->n;) {
    t3_object *o = f->obj[i];
    if (!o->visible) { i = f->end[i]; continue; }
    /* object.layers.test(camera.layers): this object only; its children
     * are still projected, as three.js's projectObject */
    if (!(o->layers & camera_layers)) { i++; continue; }
    int k = f->sph[i];
    int in_frustum = batched && k >= 0 && ((t3_mesh *)o)->geometry == f->geo[i] ? f->vis[k] : -1;
    project_one(r, o, in_frustum);
    i++;
  }
}

/* r186 RenderList painterSortStable: groupOrder, renderOrder, z, id */
static int painter_sort(const void *pa, const void *pb) {
  const render_item *a = pa, *b = pb;
  if (a->render_order != b->render_order) return a->render_order - b->render_order;
  if (a->z != b->z) return a->z < b->z ? -1 : 1;
  return a->object->id < b->object->id ? -1 : a->object->id > b->object->id;
}

/* reversePainterSortStable: groupOrder, renderOrder, z (far first), id */
static int reverse_painter_sort(const void *pa, const void *pb) {
  const render_item *a = pa, *b = pb;
  if (a->render_order != b->render_order) return a->render_order - b->render_order;
  if (a->z != b->z) return b->z < a->z ? -1 : 1;
  return a->object->id < b->object->id ? -1 : a->object->id > b->object->id;
}

/* ── auto-instancing ─────────────────────────────────────────────── */
/* r186's instanced program transforms the normal by mat3(instanceMatrix)
 * divided by the squared column lengths (transformNormal): exact for
 * rotation x any axis scale, wrong under shear, so only shear-free,
 * non-mirrored world matrices are batched. */
T3_HOT static bool batchable_matrix(const t3_mat4 *m) {
  const float *e = m->e;
#define D3(a, b) (e[a] * e[b] + e[(a) + 1] * e[(b) + 1] + e[(a) + 2] * e[(b) + 2])
  float l0 = D3(0, 0), l1 = D3(4, 4), l2 = D3(8, 8), d01 = D3(0, 4), d02 = D3(0, 8), d12 = D3(4, 8);
#undef D3
  const float tol = 1e-6f;
  if (d01 * d01 > tol * l0 * l1 || d02 * d02 > tol * l0 * l2 || d12 * d12 > tol * l1 * l2) return false;
  /* (c0 x c1) . c2 > 0: not mirrored */
  return (e[1] * e[6] - e[2] * e[5]) * e[8] + (e[2] * e[4] - e[0] * e[6]) * e[9] + (e[0] * e[5] - e[1] * e[4]) * e[10] > 0;
}

/* One interleaved instance record: the world matrix, then the color (rgb +
 * pad). A separate tightly packed color buffer cost ~0.5 us per frame more
 * in Chromium's GPU process on a three-mesh scene. */
#define INST_FLOATS 20
#define INST_STRIDE (INST_FLOATS * 4)

static void batch_reserve(t3_renderer *r, int n) {
  if (r->inst_n + n <= r->inst_cap) return;
  while (r->inst_cap < r->inst_n + n) r->inst_cap = r->inst_cap ? r->inst_cap * 2 : 1024;
  r->inst = realloc(r->inst, (size_t)r->inst_cap * INST_FLOATS * sizeof(float));
  T3_CHECK_ALLOC(r->inst);
}

static uint32_t batch_hash(t3_material *m, const t3_geometry *g, const t3_group *grp, int ro) {
  uintptr_t h = (uintptr_t)m * 0x9E3779B1u;   /* a batch shares one material */
  h ^= (uintptr_t)g * 0x85EBCA77u;
  h ^= (uintptr_t)grp * 0xC2B2AE3Du;
  h ^= (uintptr_t)(unsigned)ro * 0x27D4EB2Fu;
  return (uint32_t)(h ^ (h >> 15) ^ (h >> 7));
}

static void batch_table_reset(t3_renderer *r, int expect) {
  int cap = 64;
  while (cap < 4 * expect) cap <<= 1;
  if (cap != r->hcap) {
    r->hcap = cap;
    r->htab = realloc(r->htab, (size_t)cap * sizeof *r->htab);
    T3_CHECK_ALLOC(r->htab);
  }
  memset(r->htab, 0xff, (size_t)r->hcap * sizeof *r->htab);
  r->batch_n = 0;
  r->m_n = 0;
}

static void batch_rehash(t3_renderer *r) {
  r->hcap *= 2;
  r->htab = realloc(r->htab, (size_t)r->hcap * sizeof *r->htab);
  T3_CHECK_ALLOC(r->htab);
  memset(r->htab, 0xff, (size_t)r->hcap * sizeof *r->htab);
  for (int i = 0; i < r->batch_n; i++) {
    struct batch_entry *e = &r->batches[i];
    uint32_t h = batch_hash(e->material, e->geometry, e->group, e->render_order) & (uint32_t)(r->hcap - 1);
    while (r->htab[h] >= 0) h = (h + 1) & (uint32_t)(r->hcap - 1);
    r->htab[h] = i;
  }
}

/* Add a batchable opaque mesh to the batch for its (material, geometry,
 * group, renderOrder). */
static void batch_add(t3_renderer *r, t3_object *o, t3_geometry *g, t3_material *m, const t3_group *grp) {
  if (r->m_n == r->m_cap) {
    r->m_cap = r->m_cap ? r->m_cap * 2 : 1024;
    r->mobj = realloc(r->mobj, (size_t)r->m_cap * sizeof *r->mobj);
    r->mnext = realloc(r->mnext, (size_t)r->m_cap * sizeof *r->mnext);
    T3_CHECK_ALLOC(r->mobj); T3_CHECK_ALLOC(r->mnext);
  }
  int mi = r->m_n++;
  r->mobj[mi] = o;
  r->mnext[mi] = -1;
  uint32_t mask = (uint32_t)(r->hcap - 1), h = batch_hash(m, g, grp, o->render_order) & mask;
  for (;;) {
    int bi = r->htab[h];
    if (bi < 0) break;
    struct batch_entry *e = &r->batches[bi];
    if (e->geometry == g && e->group == grp && e->render_order == o->render_order &&
        e->receive_shadow == o->receive_shadow &&
        e->material == m) {
      r->mnext[e->tail] = mi;
      e->tail = mi;
      e->count++;
      return;
    }
    h = (h + 1) & mask;
  }
  if (r->batch_n == r->batch_cap) {
    r->batch_cap = r->batch_cap ? r->batch_cap * 2 : 64;
    r->batches = realloc(r->batches, (size_t)r->batch_cap * sizeof *r->batches);
    T3_CHECK_ALLOC(r->batches);
  }
  struct batch_entry *e = &r->batches[r->batch_n];
  e->material = m; e->geometry = g; e->group = grp; e->render_order = o->render_order;
  e->receive_shadow = o->receive_shadow;
  e->head = e->tail = mi;
  e->count = 1;
  r->htab[h] = r->batch_n++;
  if (2 * r->batch_n > r->hcap) batch_rehash(r);
}

/* Turn the batch table into render items: a single member becomes an
 * ordinary item; two or more become one instanced item whose world matrices
 * go to the instance buffer. Instance order is scene order, which cannot
 * change an opaque depth-tested result; a batch sorts by its first member's
 * z. */
/* Upload n floats per instance to vbo unless they equal last frame's (then
 * the buffer already holds them). */
static void upload_if_changed(GLuint *vbo, const float *data, int n_floats, float **prev, int *prev_n, int *prev_cap) {
  if (*prev && *prev_n == n_floats && !memcmp(*prev, data, (size_t)n_floats * sizeof(float))) return;
  if (!*vbo) glGenBuffers(1, vbo);
  glBindBuffer(GL_ARRAY_BUFFER, *vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n_floats * 4, data, GL_DYNAMIC_DRAW);
  if (*prev_cap < n_floats) {
    *prev_cap = n_floats;
    *prev = realloc(*prev, (size_t)n_floats * sizeof(float));
    T3_CHECK_ALLOC(*prev);
  }
  memcpy(*prev, data, (size_t)n_floats * sizeof(float));
  *prev_n = n_floats;
}

static void finish_batches(t3_renderer *r) {
  r->inst_n = 0;
  for (int bi = 0; bi < r->batch_n; bi++) {
    struct batch_entry *e = &r->batches[bi];
    t3_object *first = r->mobj[e->head];
    render_item it = { first, e->geometry, e->material, e->group, item_z(r, first), e->render_order, 0, 0 };
    if (e->count == 1) {
      /* a lone mesh: an instanced draw of one costs more than a plain draw */
      list_push(&r->opaque, it);
      continue;
    }
    /* a batch of meshes sharing geometry and material is one draw of
     * r186's instanced program, their world matrices the instance attribute;
     * a frame where nothing moved re-uses the instance buffer untouched */
    it.batch_count = e->count;
    it.batch_offset = r->inst_n;
    batch_reserve(r, e->count);
    float *dst = r->inst + (size_t)r->inst_n * INST_FLOATS;
    for (int k = e->head; k >= 0; k = r->mnext[k], dst += INST_FLOATS) {
      memcpy(dst, r->mobj[k]->matrix_world.e, 16 * sizeof(float));
      dst[16] = dst[17] = dst[18] = 1; dst[19] = 0;
    }
    r->inst_n += e->count;
    list_push(&r->opaque, it);
  }
  if (r->inst_n)
    if (!r->wgpu) upload_if_changed(&r->inst_vbo, r->inst, r->inst_n * INST_FLOATS, &r->prev_inst, &r->prev_inst_n, &r->prev_cap);
}

static void use_batch_geometry(t3_renderer *r, t3_object *o, t3_geometry *g, int offset) {

  use_geometry_bind(r, o, g, false); /* creates / refreshes the plain VAO and buffers */
  geometry_gl *gg = g->_gl;
  GLuint *vao = &gg->batch_vao;
  int *off = &gg->batch_off;
  uint64_t *buf = &gg->batch_buf;
  /* keyed by renderer too: a destroyed renderer's buffer name can come back */
  uint64_t key = (uint64_t)r->serial << 32 | r->inst_vbo;
  if (!*vao) {
    *vao = build_vao(r, g, NULL);
    for (int c = 0; c < 4; c++) {
      glEnableVertexAttribArray(A_INSTANCE_MATRIX + c);
      glVertexAttribDivisor(A_INSTANCE_MATRIX + c, 1);
    }
    *off = -1;
  }
  bind_vao(r, *vao);
  /* The pointers are VAO state and the buffers keep their names across
   * re-uploads, so a batch at the same offset as last time needs none. */
  if (*off != offset || *buf != key) {
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
    intptr_t base = (intptr_t)offset * INST_STRIDE;
    for (int c = 0; c < 4; c++)
      glVertexAttribPointer(A_INSTANCE_MATRIX + c, 4, GL_FLOAT, GL_FALSE, INST_STRIDE, (void *)(base + c * 16));
    *off = offset;
    *buf = key;
  }
}

/* LightShadow.updateMatrices (and SpotLightShadow's): a directional or spot
 * light's shadow camera and matrix (point lights: renderer_gen.inc) */
static void shadow_update_matrices(t3_renderer *r, t3_light *l) {
  t3_light_shadow *sh = l->shadow;
  t3_camera *c = sh->camera;
  t3_vec3 lp = t3_mat4_get_position(&l->base.matrix_world);
  if (l->base.type == T3_SPOT_LIGHT) {
    float fov = 180.0f / 3.14159265358979f * 2 * l->angle * sh->focus;
    float aspect = (float)sh->map_width / sh->map_height, far = l->distance ? l->distance : c->far;
    if (fov != c->fov || aspect != c->aspect || far != c->far) {
      c->fov = fov; c->aspect = aspect; c->far = far;
      t3_camera_update_projection_matrix(c);
    }
  }
  t3_vec3 tp = t3_mat4_get_position(&l->target->matrix_world);
  c->base.position = lp;
  t3_object_look_at(c, tp.x, tp.y, tp.z);
  t3_object_update_matrix_world(c, false);
  t3_mat4 bias = { { 0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0.5f, 0, 0.5f, 0.5f, 0.5f, 1 } }, tmp;
  t3_mat4_multiply(&tmp, &bias, &c->projection_matrix);
  t3_mat4_multiply(&sh->matrix, &tmp, &c->matrix_world_inverse);
  t3_mat4_multiply(&r->shadow_proj_screen, &c->projection_matrix, &c->matrix_world_inverse);
  t3_frustum_from_matrix(&r->shadow_frustum, &r->shadow_proj_screen);
}

static bool in_shadow_frustum(t3_renderer *r, t3_object *o, t3_geometry *g) {
  if (!g->has_bounding_sphere) t3_geometry_compute_bounding_sphere(g);
  t3_sphere s = g->bounding_sphere;
  s.center = t3_vec3_apply_mat4(s.center, &o->matrix_world);
  s.radius *= t3_mat4_max_scale_on_axis(&o->matrix_world);
  return t3_frustum_intersects_sphere(&r->shadow_frustum, s);
}

/* A stable merge sort: musl's qsort (smoothsort) cost more than the rest of
 * a 2000-mesh frame's sorting work. Sorts through r->scratch. */
static void sort_items(render_list *l, render_list *tmp, int (*cmp)(const void *, const void *)) {
  int n = l->n;
  if (n < 2) return;
  if (tmp->cap < n) {
    tmp->cap = n;
    tmp->items = realloc(tmp->items, (size_t)n * sizeof *tmp->items);
    T3_CHECK_ALLOC(tmp->items);
  }
  /* insertion-sort runs of 16, then merge passes */
  const int RUN = 16;
  render_item *a = l->items, *b = tmp->items;
  for (int s0 = 0; s0 < n; s0 += RUN) {
    int e = s0 + RUN < n ? s0 + RUN : n;
    for (int i = s0 + 1; i < e; i++) {
      render_item x = a[i];
      int j = i - 1;
      while (j >= s0 && cmp(&a[j], &x) > 0) { a[j + 1] = a[j]; j--; }
      a[j + 1] = x;
    }
  }
  for (int w = RUN; w < n; w *= 2) {
    for (int s0 = 0; s0 < n; s0 += 2 * w) {
      int m = s0 + w < n ? s0 + w : n, e = s0 + 2 * w < n ? s0 + 2 * w : n, i = s0, j = m, k = s0;
      while (i < m && j < e) b[k++] = cmp(&a[j], &a[i]) < 0 ? a[j++] : a[i++];
      while (i < m) b[k++] = a[i++];
      while (j < e) b[k++] = a[j++];
    }
    render_item *t = a; a = b; b = t;
  }
  if (a != l->items) {
    memcpy(l->items, a, (size_t)n * sizeof *a);
  }
}

unsigned t3_texture_gl_name(const t3_texture *t) {
  return t && t->_gl ? ((const texture_gl *)t->_gl)->tex : 0;
}

/* ── draw ────────────────────────────────────────────────────────── */
/* the draw call of an item: draw range x group range, as
 * three.js's renderBufferDirect */
static void issue_draw(t3_renderer *r, const render_item *it, t3_object *o, t3_geometry *g) {
  int total = g->index ? g->index->count : g->attributes[T3_ATTR_POSITION]->count;
  int start = g->draw_start, end = g->draw_count < 0 ? total : g->draw_start + g->draw_count;
  if (it->group) {
    if (it->group->start > start) start = it->group->start;
    if (it->group->start + it->group->count < end) end = it->group->start + it->group->count;
  }
  if (start < 0) start = 0;
  if (end > total) end = total;
  int count = end - start;
  if (count <= 0) return;
  bool instanced = o->type == T3_INSTANCED_MESH || it->batch_count;
  int instances = it->batch_count ? it->batch_count : o->type == T3_INSTANCED_MESH ? ((t3_instanced_mesh *)o)->count : 1;
  /* renderBufferDirect: LINES for LineSegments, LINE_LOOP, LINE_STRIP for Line */
  GLenum mode = o->type == T3_LINE_SEGMENTS ? GL_LINES : o->type == T3_LINE_LOOP ? GL_LINE_LOOP
              : o->type == T3_LINE ? GL_LINE_STRIP : o->type == T3_POINTS ? GL_POINTS : GL_TRIANGLES;
  if (g->index) {
    GLenum t = g->index->type == T3_UINT16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    void *off = (void *)(intptr_t)(start * (t == GL_UNSIGNED_SHORT ? 2 : 4));
    if (instanced) glDrawElementsInstanced(mode, count, t, off, instances);
    else glDrawElements(mode, count, t, off);
  } else {
    if (instanced) glDrawArraysInstanced(mode, start, count, instances);
    else glDrawArrays(mode, start, count);
  }
  r->info.calls++;
  if (mode == GL_TRIANGLES) r->info.triangles += (unsigned)(count / 3 * instances);
}

#include "renderer_gen.inc"
#include "renderer_wgpu.inc"
#include "renderer_env.inc"

/* ── r186's output pass: the scene is drawn into a linear target, this pass
 * writes it to the screen with the output colour space applied ───────── */
static GLuint gen_out_target(t3_renderer *r) {
  int w = r->width, h = r->height;
  r->out_on = true;
  if (r->out_fbo && r->out_w == w && r->out_h == h) return r->out_fbo;
  if (!r->out_fbo) { glGenFramebuffers(1, &r->out_fbo); glGenTextures(1, &r->out_tex); glGenRenderbuffers(1, &r->out_depth); }
  tu_bind(31, r->out_tex, true);
  glTexImage2D(GL_TEXTURE_2D, 0, 0x881A /* GL_RGBA16F */, w, h, 0, GL_RGBA, 0x140B /* GL_HALF_FLOAT */, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindRenderbuffer(GL_RENDERBUFFER, r->out_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, r->out_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->out_tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, r->out_depth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    snprintf(r->error, sizeof r->error, "output target: RGBA16F framebuffer incomplete");
    r->has_error = true;
  }
  r->fb_known = true;
  r->cur_fbo = r->out_fbo;
  r->out_w = w; r->out_h = h;
  return r->out_fbo;
}

/* the output pass is r186's own program for the frame's tone mapping and
 * output colour space (tools/gen-programs.mjs kind "output"), drawn over its
 * QuadMesh triangle */
static void gen_output_pass(t3_renderer *r) {
  static const char *const tm[] = { NULL, "tmlinear", "tmreinhard", "tmcineon", "tmaces", NULL, "tmagx", "tmneutral" };
  char name[64] = "output";
  if (r->tone_mapping > 0 && r->tone_mapping < 8 && tm[r->tone_mapping]) {
    strcat(name, "+"); strcat(name, tm[r->tone_mapping]);
    if (r->output_color_space != T3_SRGB_COLOR_SPACE) strcat(name, "+lin");
  }
  const t3_gen_program *src = t3_gen_program_by_name_gl(name);
  t3_gen_gl *gl = src ? gen_program_enc(r, src, false) : NULL;
  if (!gl || !gl->complete) { gen_note(r, "no output pass program"); return; }
  if (!r->out_vao) {
    static const float tri[9] = { -1, 3, 0, -1, -1, 0, 3, -1, 0 };   /* QuadMesh's triangle */
    glGenVertexArrays(1, &r->out_vao);
    glGenBuffers(1, &r->out_vbo);
    bind_vao(r, r->out_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->out_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(A_POSITION);
    glVertexAttribPointer(A_POSITION, 3, GL_FLOAT, GL_FALSE, 0, 0);
  }
  gl_fbo(r, r->screen_fbo);
  gl_viewport(r, r->view_x, view_gl_y(r, r->view_y, r->view_h), r->view_w, r->view_h);
  gl_scissor_box(r, r->scissor_test, r->sc_x, view_gl_y(r, r->sc_y, r->sc_h), r->sc_w, r->sc_h);
  if (r->cur_depth_test != 0) { glDisable(GL_DEPTH_TEST); r->cur_depth_test = 0; }
  if (r->cur_blend != 0) { glDisable(GL_BLEND); r->cur_blend = 0; }
  if (r->cur_cull != 0) { glDisable(GL_CULL_FACE); r->cur_cull = 0; }
  set_color_mask(r, 1);
  if (r->cur_program != gl->program) { glUseProgram(gl->program); r->cur_program = gl->program; }
  gen_fill_frame(r, gl, NULL);
  /* its one per-draw value: the framebuffer texture's flip flag */
  for (int k = gl->n_frame_ops; k < gl->n_ops; k++)
    if (gl->ops[k].op == GOP_TEX_FLIPY) { uint32_t one = 1; memcpy(t3_gen_gl_op(gl, &gl->ops[k]), &one, 4); }
  t3_gen_gl_flush(gl);
  for (int i = 0; i < gl->n_groups; i++) {
    if (!gl->ubo[i]) continue;
    if (i == gl->draw_group) {
      glBindBuffer(GL_UNIFORM_BUFFER, gl->ubo[i]);
      glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)gl->bytes[i], gl->mirror[i]);
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, (GLuint)i, gl->ubo[i]);
    r->gen_bound_buf[i] = gl->ubo[i];
    r->gen_bound_off[i] = -1;
  }
  for (int i = 0; i < gl->n_textures; i++)
    if (!strcmp(gl->tex[i].layout->source, "output")) tu_bind(i, r->out_tex, true);
  bind_vao(r, r->out_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  r->info.calls++;
  r->info.triangles++;
}

static void draw_item(t3_renderer *r, const render_item *it, const t3_camera *cam) {
  gen_draw(r, it, cam);
}

/* renderer.compile( scene, camera ): every visible object's program for the
 * scene's lights and fog, built now instead of on its first draw */
static void compile_lights(t3_object *o, void *ctx) {
  t3_renderer *r = ctx;
  if (o->visible && (o->type == T3_AMBIENT_LIGHT || o->type == T3_HEMISPHERE_LIGHT || o->type == T3_DIRECTIONAL_LIGHT ||
                     o->type == T3_POINT_LIGHT || o->type == T3_SPOT_LIGHT)) project_one(r, o, 1);
}
static void compile_object(t3_object *o, void *ctx) {
  t3_renderer *r = ctx;
  switch (o->type) {
  case T3_MESH: case T3_INSTANCED_MESH: case T3_SKINNED_MESH: case T3_LINE: case T3_LINE_SEGMENTS: case T3_LINE_LOOP:
  case T3_POINTS: case T3_SPRITE: {
    t3_mesh *m = (t3_mesh *)o;
    if (!m->geometry) break;
    for (int i = 0; i < m->material_count; i++) gen_pick(r, m->materials[i], o, m->geometry, false);
    break;
  }
  default: break;
  }
}
void t3_renderer_compile(t3_renderer *r, t3_scene *scene, t3_camera *cam) {
  r->camera = cam;
  r->environment = scene->environment;
  r->light_count = r->caster_count = 0;
  r->fog = scene->fog.type != T3_FOG_NONE ? &scene->fog : NULL;
  t3_object_traverse(scene, compile_lights, r);
  setup_lights(r, cam);
  t3_object_traverse(scene, compile_object, r);
}

void t3_renderer_render(t3_renderer *r, t3_scene *scene, t3_camera *cam) {
  r->stamp++;
  r->info.frame++;
  r->info.calls = r->info.triangles = r->info.skipped = 0;

  r->camera = cam;
  r->environment = scene->environment;
  if (r->gpu_timing) gpu_timer_begin(r);
  if (scene->auto_update) t3_object_update_matrix_world(scene, false);
  if (!cam->base.parent) t3_object_update_matrix_world(cam, false);

  t3_mat4_multiply(&r->proj_screen, &cam->projection_matrix, &cam->matrix_world_inverse);
  t3_mat3_normal_matrix(&r->view_normal, &cam->matrix_world_inverse);
  t3_frustum_from_matrix(&r->frustum, &r->proj_screen);
  r->last_proj = cam->projection_matrix;

  r->opaque.n = r->transparent.n = 0;
  r->light_count = 0;
  r->caster_count = 0;
  r->fog = scene->fog.type != T3_FOG_NONE ? &scene->fog : NULL;
  batch_table_reset(r, r->m_n);
  project_scene(r, &scene->base, cam->base.layers);
  setup_lights(r, cam);

  finish_batches(r);
  sort_items(&r->opaque, &r->scratch, painter_sort);
  sort_items(&r->transparent, &r->scratch, reverse_painter_sort);
  /* r186 _renderTransparents: double-sided transmissive objects draw their back
   * faces first (material.side set to BackSide, then FrontSide for the
   * transparent list, then back to DoubleSide) */
  r->backpass.n = 0;
  r->vp_done[0] = r->vp_done[1] = false;
  for (int i = 0; i < r->transparent.n; i++) {
    const t3_material *m = r->transparent.items[i].material;
    if (m->type == T3_MESH_PHYSICAL_MATERIAL && m->transmission > 0 && m->side == T3_DOUBLE_SIDE) list_push(&r->backpass, r->transparent.items[i]);
  }

  r->info.programs = (unsigned)r->gen_n;
  gen_env_prepare(r, scene);
#ifdef T3_WGPU
  if (r->wgpu) {
    r->gen_direct = false;
    wgpu_render(r, scene, cam);
    return;
  }
#endif
  gen_render_shadow_maps(r, scene);

  /* r186 draws into a linear target and encodes in a final pass; with nothing
   * blended and no tone mapping the result is the same with the encode done in
   * each program, straight into the screen, without the extra target and pass */
  r->gen_direct = !r->exact_output && !r->target && r->output_color_space == T3_SRGB_COLOR_SPACE && !r->tone_mapping &&
                  r->transparent.n == 0 && !scene->background_texture;

  /* GL state persists across frames, as in three.js's WebGLState: a call
   * that would set what is already set is skipped. Code that touches GL
   * behind the renderer's back calls t3_renderer_reset_state. */
  bind_target(r);
  /* a scene background colour clears to it, else autoClear clears
   * to the clear color */
  bool bg_color = scene->has_background && !scene->background_texture;
  if (r->has_ext_target && !r->target) {
    /* an embedder's target: clear only what it does not LOAD, to its values
     * (a scene background colour still wins for the colour) */
    const t3_external_target *et = &r->ext_target;
    GLbitfield bits = (!et->load_color ? GL_COLOR_BUFFER_BIT : 0) | (!et->load_depth ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT : 0);
    if (bg_color) bits |= GL_COLOR_BUFFER_BIT;
    if (bits) {
      if (bg_color) gl_clear_color(r, scene->background.r, scene->background.g, scene->background.b, 1);
      else gl_clear_color(r, et->clear_rgba[0], et->clear_rgba[1], et->clear_rgba[2], et->clear_rgba[3]);
      if (r->cur_depth_write != 1) { glDepthMask(GL_TRUE); r->cur_depth_write = 1; }
      if (bits & GL_DEPTH_BUFFER_BIT) { glClearDepthf(et->clear_depth); glClearStencil(et->clear_stencil); }
      glClear(bits);
      if (bits & GL_DEPTH_BUFFER_BIT) { glClearDepthf(1); glClearStencil(0); }
    }
  } else if (bg_color || r->auto_clear) {
    t3_color cc = bg_color ? scene->background : r->clear_color;
    if (r->gen_direct) { cc.r = t3_linear_to_srgb(cc.r); cc.g = t3_linear_to_srgb(cc.g); cc.b = t3_linear_to_srgb(cc.b); }
    gl_clear_color(r, cc.r, cc.g, cc.b, bg_color ? 1 : r->clear_alpha);
    if (r->cur_depth_write != 1) { glDepthMask(GL_TRUE); r->cur_depth_write = 1; }
    GLbitfield bits = (r->auto_clear || bg_color ? GL_COLOR_BUFFER_BIT : 0) |
                      (r->auto_clear ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT : 0);
    glClear(bits);
  }
  backpass_side(r, T3_FRONT_SIDE);   /* (after the shadow maps, which see DoubleSide as r186's do) */
  gen_prepare(r, cam);
  gen_background(r, scene, cam);

  for (int i = 0; i < r->opaque.n; i++) draw_item(r, &r->opaque.items[i], cam);
  if (r->backpass.n) {
    backpass_side(r, T3_BACK_SIDE);
    for (int i = 0; i < r->backpass.n; i++) draw_item(r, &r->backpass.items[i], cam);
    backpass_side(r, T3_FRONT_SIDE);
  }
  for (int i = 0; i < r->transparent.n; i++) draw_item(r, &r->transparent.items[i], cam);
  backpass_side(r, T3_DOUBLE_SIDE);

  if (r->target) {
    rt_finish(r, r->target);
  } else if (r->out_on && r->cur_fbo == r->out_fbo) {
    gen_output_pass(r);
  } else if (own_msaa(r)) {
    /* resolve what this render drew (the scissor still applies to the blit) */
    msaa_resolve(r);
  }
  set_color_mask(r, 1); /* colour writes back on for the next clear */
  gpu_timer_end(r);
}
