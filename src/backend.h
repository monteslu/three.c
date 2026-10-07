/* The backend seam: what the renderer asks of the GPU API. Private to the
 * library. The GLES unit (src/backend_gles.c) implements the lifetime,
 * frame-level and embedder slots; the renderer still draws with its own GL
 * (renderer.c, renderer_gen.inc) and with webgpu.h directly on WebGPU
 * (gen_wgpu.c, renderer_wgpu.inc), so the resource and draw slots below are
 * a contract the GLES unit leaves NULL and the renderer never calls.
 *
 * Shaped by what the GENERATED programs need (tools/gen-programs.mjs: the
 * programs three.js r186 generates for each material state, with a manifest
 * of bind groups, uniform structs, typed textures, samplers and vertex
 * attributes): uniform groups uploaded as whole blocks, textures and samplers
 * bound by manifest name, one pipeline per (program, render state). It is not
 * a general RHI: an embedder's own passes go straight to GL or webgpu.h. */
#ifndef T3_BACKEND_H
#define T3_BACKEND_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "three.h"

typedef struct t3_backend t3_backend;
/* opaque, backend-owned */
typedef struct t3_bk_buffer t3_bk_buffer;       /* vertex, index, uniform, storage */
typedef struct t3_bk_texture t3_bk_texture;     /* 2D, 2D array, cube, 3D, depth */
typedef struct t3_bk_sampler t3_bk_sampler;
typedef struct t3_bk_target t3_bk_target;       /* colour attachments + depth; the canvas is one */
typedef struct t3_bk_program t3_bk_program;     /* a generated program: shader stages + its binding layout (the manifest) */
typedef struct t3_bk_pipeline t3_bk_pipeline;   /* program + vertex layout + blend/depth/cull/stencil state */
typedef struct t3_bk_bindgroup t3_bk_bindgroup; /* one manifest group bound: uniform buffer, textures, samplers */
typedef struct t3_bk_query t3_bk_query;         /* timestamp / occlusion, optional */

typedef enum { T3_BK_BUFFER_VERTEX, T3_BK_BUFFER_INDEX, T3_BK_BUFFER_UNIFORM, T3_BK_BUFFER_STORAGE } t3_bk_buffer_kind;
typedef enum { T3_BK_STAGE_VERTEX = 1, T3_BK_STAGE_FRAGMENT = 2, T3_BK_STAGE_COMPUTE = 4 } t3_bk_stage;
typedef enum { T3_BK_QUERY_TIMESTAMP, T3_BK_QUERY_OCCLUSION } t3_bk_query_kind;

/* A texture binding is TYPED: an extension group (an embedder's clustered
 * light data: RG32UI records, R32UI index lists read with texelFetch) must be
 * able to say "uint, 2d, no sampler". The generator records r186's own
 * classification per binding. */
typedef enum { T3_BK_SAMPLE_FLOAT, T3_BK_SAMPLE_UNFILTERABLE_FLOAT, T3_BK_SAMPLE_DEPTH, T3_BK_SAMPLE_SINT, T3_BK_SAMPLE_UINT } t3_bk_sample_type;
typedef enum { T3_BK_DIM_2D, T3_BK_DIM_2D_ARRAY, T3_BK_DIM_CUBE, T3_BK_DIM_3D } t3_bk_tex_dim;
typedef struct {
  const char *name;          /* binding name in the shader ("nodeUniform1", "clusterRecords") */
  uint32_t binding;          /* binding number inside the group */
  t3_bk_sample_type sample;  /* uint / sint: texelFetch only, no sampler (WGSL texture_2d<u32>, GLSL usampler2D) */
  t3_bk_tex_dim dim;
  bool has_sampler;          /* a sampler binding accompanies it (WebGPU); false for integer and storage textures */
  bool compare_sampler;      /* ... and it is a comparison sampler (shadow maps) */
  bool storage;              /* storage texture (written), not sampled */
  const char *source;        /* what the renderer binds here: "map", "normalMap", ..., "dfg_lut" (r186's LUT, shipped with the programs), or an extension's own name */
  uint32_t sampler_binding;  /* its sampler's binding number in the group (WebGPU; has_sampler) */
} t3_bk_texture_layout;
typedef struct {
  const char *name;          /* its name in the shader (NodeBuffer_<id>; GLES binds the block by it) */
  uint32_t binding;          /* binding number inside the group (WebGPU) */
  uint32_t bytes;
  bool storage;              /* storage buffer, else uniform */
  const char *source;        /* "instanceMatrix", "morphInfluences", ... or NULL when the capture could not name it */
} t3_bk_buffer_layout;
typedef struct {
  const char *name;          /* "render", "object", or an extension group's */
  uint32_t index;            /* bind group index (WebGPU) / uniform block binding (GLES) */
  uint32_t uniform_bytes;    /* the group's uniform struct size (std140 on GLES, WGSL layout on WebGPU; the generator emits both) */
  uint32_t uniform_binding;  /* its binding number inside the group (WebGPU; GLES binds the block by name at link) */
  const t3_bk_texture_layout *textures; uint32_t n_textures;   /* in binding order */
  /* extra uniform / storage buffers in the group beside the struct (r186
   * keeps instance matrices, morph influences, bone matrices in them) */
  const t3_bk_buffer_layout *buffers; uint32_t n_buffers;
} t3_bk_group_layout;

typedef struct {
  const char *vertex, *fragment, *compute;     /* generated source for THIS backend (GLSL ES 3.00 / WGSL) */
  const t3_bk_group_layout *groups; uint32_t n_groups;
  const char *const *attributes; uint32_t n_attributes;  /* vertex attribute names, in location order */
  const char *state_name;                      /* "standard+map+fog", for errors and caches */
} t3_bk_program_desc;

typedef struct {
  int topology;              /* triangles, lines, points */
  bool depth_test, depth_write; int depth_func;
  int cull;                  /* none / front / back */
  bool blend; int blend_src, blend_dst, blend_eq, blend_src_a, blend_dst_a, blend_eq_a;
  bool color_mask[4];
  int color_format, depth_format, samples;   /* of the target the pipeline draws into */
  struct { const char *name; int location; int components; int type; bool normalized; uint32_t stride, offset; bool instanced; } attrs[16];
  int n_attrs;
} t3_bk_pipeline_desc;

struct t3_backend {
  /* ── lifetime ── */
  void (*destroy)(t3_backend *b);
  const char *(*name)(t3_backend *b);                /* "gles3" / "webgpu" */
  const char *(*renderer_string)(t3_backend *b);     /* GL_RENDERER / adapter description */
  t3_clip_z (*clip_z)(t3_backend *b);                /* [-1,1] on GLES, [0,1] on WebGPU; r186 builds projections per backend and so does the renderer */

  /* ── resources (NULL in the GLES unit) ── */
  t3_bk_buffer *(*buffer_create)(t3_backend *b, t3_bk_buffer_kind kind, size_t bytes, const void *data, bool dynamic);
  void (*buffer_write)(t3_backend *b, t3_bk_buffer *buf, size_t offset, const void *data, size_t bytes);   /* queue.writeBuffer / glBufferSubData */
  void (*buffer_destroy)(t3_backend *b, t3_bk_buffer *buf);
  t3_bk_texture *(*texture_create)(t3_backend *b, const t3_texture *t);          /* from a t3_texture's description + pixels */
  void (*texture_update)(t3_backend *b, t3_bk_texture *tex, const t3_texture *t); /* new pixels, same size */
  void (*texture_destroy)(t3_backend *b, t3_bk_texture *tex);
  t3_bk_sampler *(*sampler_get)(t3_backend *b, t3_wrapping wrap_s, t3_wrapping wrap_t, t3_filter mag, t3_filter min, float anisotropy, bool compare);   /* cached by state */
  t3_bk_target *(*target_create)(t3_backend *b, int w, int h, int color_format, int depth_format, int samples, int layers, bool cube);
  void (*target_destroy)(t3_backend *b, t3_bk_target *t);
  t3_bk_target *(*canvas_target)(t3_backend *b);     /* the one the host presents; size from the host */
  t3_bk_program *(*program_create)(t3_backend *b, const t3_bk_program_desc *d, char *err, size_t errlen);
  void (*program_destroy)(t3_backend *b, t3_bk_program *p);
  t3_bk_pipeline *(*pipeline_get)(t3_backend *b, t3_bk_program *p, const t3_bk_pipeline_desc *d);   /* cached by (program, state) */
  t3_bk_bindgroup *(*bindgroup_create)(t3_backend *b, t3_bk_program *p, uint32_t group, t3_bk_buffer *uniforms,
                                       t3_bk_texture *const *textures, t3_bk_sampler *const *samplers);
  void (*bindgroup_destroy)(t3_backend *b, t3_bk_bindgroup *g);

  /* ── a frame (NULL in the GLES unit, except finish) ── */
  void (*begin_pass)(t3_backend *b, t3_bk_target *t, int layer, int level, bool clear_color, const float rgba[4],
                     bool clear_depth, float depth, bool clear_stencil, int stencil);
  void (*set_viewport)(t3_backend *b, int x, int y, int w, int h);
  void (*set_scissor)(t3_backend *b, bool on, int x, int y, int w, int h);
  void (*set_pipeline)(t3_backend *b, t3_bk_pipeline *p);
  void (*set_bindgroup)(t3_backend *b, uint32_t index, t3_bk_bindgroup *g, uint32_t dynamic_offset);
  void (*set_vertex_buffer)(t3_backend *b, int slot, t3_bk_buffer *buf, size_t offset);
  void (*set_index_buffer)(t3_backend *b, t3_bk_buffer *buf, int index_type);
  void (*draw)(t3_backend *b, uint32_t vertices, uint32_t instances, uint32_t first, uint32_t first_instance);
  void (*draw_indexed)(t3_backend *b, uint32_t indices, uint32_t instances, uint32_t first_index, int32_t base_vertex, uint32_t first_instance);
  void (*end_pass)(t3_backend *b);
  void (*resolve)(t3_backend *b, t3_bk_target *from, t3_bk_target *to);   /* MSAA resolve / blit */
  void (*copy_target_to_texture)(t3_backend *b, t3_bk_target *from, t3_bk_texture *to, int level);
  void (*generate_mipmaps)(t3_backend *b, t3_bk_texture *tex);
  void (*finish)(t3_backend *b);                     /* glFinish / onSubmittedWorkDone */
  bool (*read_pixels)(t3_backend *b, t3_bk_target *t, int x, int y, int w, int h, void *rgba8);   /* synchronous on GLES, a copy + map on WebGPU */
  t3_bk_query *(*query_begin)(t3_backend *b, t3_bk_query_kind kind);   /* NULL when unsupported */
  void (*query_end)(t3_backend *b, t3_bk_query *q);
  bool (*query_result)(t3_backend *b, t3_bk_query *q, uint64_t *out);  /* false while pending */

  /* ── the embedder's escape hatches ──
   * GLES: what three.h exposes today, unchanged. WebGPU: counterparts that
   * take and hand out webgpu.h objects. */
  void (*reset_bindings)(t3_backend *b);                              /* GLES: forget cached GL state after foreign GL calls; WebGPU: no-op */
  uintptr_t (*texture_native)(t3_backend *b, t3_bk_texture *t);       /* GLES: GLuint; WebGPU: WGPUTexture */
  uintptr_t (*texture_native_view)(t3_backend *b, t3_bk_texture *t);  /* WebGPU: WGPUTextureView; GLES: 0 */
  t3_bk_texture *(*texture_adopt)(t3_backend *b, uintptr_t native, int w, int h, int levels, bool cube, bool owns);  /* t3_texture_from_gl / adopt_gl */
  void (*texture_swap)(t3_backend *b, t3_bk_texture *a, t3_bk_texture *c);
  /* An embedder's frame encoder (WebGPU): the renderer records its passes
   * on it in the embedder's pass order (sky, opaque = the scene, effects,
   * post, one submit before the overlay) and never submits. 0 = the renderer
   * owns encoding and submit. GLES: no-op. */
  void (*set_external_encoder)(t3_backend *b, uintptr_t wgpu_command_encoder);
  /* An embedder's target, the whole attachment set and a rect (three.h
   * t3_external_target). NULL = back to the canvas. Depth-only (color == 0)
   * is the shadow-atlas case: the renderer's depth pass draws into a sub-rect
   * of the embedder's depth texture with LOAD so the other tiles survive. */
  void (*set_external_target)(t3_backend *b, const t3_external_target *t);
  uintptr_t (*external_pass)(t3_backend *b);                          /* WebGPU: the WGPURenderPassEncoder in flight, for an embedder drawing into the renderer's pass; GLES: 0 */
  /* Extension groups declared by a program's manifest are ordinary bind
   * groups filled by their owner each frame through this hook (generator-time
   * extensions: external TSL nodes + support-table rows). */
  void (*extension_fill)(t3_backend *b, const char *group_name, t3_bk_buffer *uniforms,
                         t3_bk_texture **textures, t3_bk_sampler **samplers, void *user);
};

/* constructors, one per backend compiled in */
t3_backend *t3_backend_gles_new(void);                 /* on the current GL context */
/* core webgpu.h handles; the platform shim (surface, present, async) lives outside */
t3_backend *t3_backend_wgpu_new(void *wgpu_device, void *wgpu_queue, void *surface_or_view);

#endif
