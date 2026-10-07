/* The WebGPU side of r186's generated programs (src/gen/programs_wgpu.c):
 * shader modules from the captured WGSL, bind group layouts from the
 * manifest, pipelines per render state, and the frame's resources (vertex
 * and index buffers, textures, samplers, the streamed uniform data).
 *
 * The program object is the same t3_gen_gl the GLES path uses (ops, mirrors,
 * constants and template parameters are backend-neutral); `wg` holds the
 * WebGPU objects. Only built with T3_WGPU. */
#ifndef T3_GEN_WGPU_H
#define T3_GEN_WGPU_H
#ifdef T3_WGPU

#include <webgpu/webgpu.h>

#include "gen_program.h"

typedef struct t3_wgpu t3_wgpu;

t3_wgpu *t3_wgpu_new(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat out_format);
void t3_wgpu_destroy(t3_wgpu *w);

/* a program's WebGPU half: modules, bind group layouts, pipeline layout and
 * the pipelines made for it so far; NULL with err set on failure */
t3_gen_gl *t3_gen_wgpu_build(t3_wgpu *w, const t3_gen_program *src, t3_gen_params params, char *err, size_t errcap);
void t3_gen_wgpu_free(t3_gen_gl *g);

/* the render state a pipeline is made for */
typedef struct {
  uint8_t topology;       /* 0 triangles, 1 lines, 2 line strip, 3 points */
  uint8_t cull;           /* 0 none, 1 back, 2 front */
  uint8_t depth_test, depth_write, depth_func;   /* depth_func: three.js's constants */
  uint8_t blend;          /* t3 blending, 0 = off */
  uint8_t color_write;
  uint8_t instance;       /* 0 none, 1 InstancedMesh matrices (stride 64), 2 a batch (stride 80) */
  uint8_t strides[12];    /* the geometry's item size for each non-instance attribute, manifest order */
  uint8_t samples;        /* 0 or 1: single sampled, else the MSAA count */
  uint8_t flip_y;         /* clip y negated, front face CW: a target stored bottom-up (t3_external_target.flip_y) */
  WGPUTextureFormat color, depth;
} t3_wgpu_state;

/* one frame: begin a pass on a color + depth view (clear or load), draw,
 * end, and submit (or record on an external encoder) */
typedef struct {
  WGPUTextureView color, depth;
  WGPUTextureView resolve;    /* MSAA: where the colour resolves, else NULL */
  bool load_color, load_depth;
  float clear[4];
  bool depth_clear_set; float clear_depth;   /* else depth clears to 1 */
  bool stencil; uint32_t clear_stencil;     /* the depth view has a stencil aspect */
  int vx, vy, vw, vh;         /* viewport (top-left origin, as WebGPU) */
  bool scissor; int sx, sy, sw, sh;
} t3_wgpu_pass;
void t3_wgpu_begin_frame(t3_wgpu *w, size_t stream_estimate);
void t3_wgpu_begin_pass(t3_wgpu *w, const t3_wgpu_pass *p);
/* the frame's uniform stream: copy n bytes, get the dynamic offset */
uint32_t t3_wgpu_stream(t3_wgpu *w, const void *data, uint32_t n);
WGPUBuffer t3_wgpu_stream_buffer(t3_wgpu *w);
WGPURenderPassEncoder t3_wgpu_pass_encoder(t3_wgpu *w);
void t3_wgpu_end_pass(t3_wgpu *w);
void t3_wgpu_end_frame(t3_wgpu *w);    /* uploads the stream, submits (unless deferred / external) */
void t3_wgpu_flush(t3_wgpu *w);        /* submits an open deferred frame */
void t3_wgpu_set_defer(t3_wgpu *w, bool on);
void t3_wgpu_set_encoder(t3_wgpu *w, WGPUCommandEncoder enc);   /* NULL: back to our own */

/* the pipeline for a program in a state, made on first use */
WGPURenderPipeline t3_wgpu_pipeline(t3_wgpu *w, t3_gen_gl *g, const t3_wgpu_state *st);
/* a group's bind group for this draw: its uniform struct and buffers come
 * from the stream (dynamic offsets), its textures and samplers given in
 * manifest order */
WGPUBindGroup t3_wgpu_bind_group(t3_wgpu *w, t3_gen_gl *g, int group, WGPUTextureView const *views, WGPUSampler const *samplers);

/* resources made from three.c objects, cached on them */
WGPUBuffer t3_wgpu_attribute(t3_wgpu *w, t3_attribute *a, bool index, uint32_t *index_format);
WGPUBuffer t3_wgpu_attribute_uint(t3_wgpu *w, t3_attribute *a);   /* as uint32 (a uvecN shader input) */
WGPUBuffer t3_wgpu_floats(t3_wgpu *w, void **slot, const float *data, size_t n_floats);   /* a vertex buffer of floats, reused */
WGPUTextureView t3_wgpu_texture(t3_wgpu *w, t3_texture *t);
WGPUTextureView t3_wgpu_data_texture(t3_wgpu *w, void **slot, int width, int height, WGPUTextureFormat fmt, const void *data, uint32_t bytes_per_row);
WGPUSampler t3_wgpu_sampler(t3_wgpu *w, const t3_texture *t, bool compare);
/* t3_texture_adopt_wgpu; and releasing a freed object's WebGPU side */
void t3_wgpu_adopt(t3_texture *t, WGPUTexture tex, WGPUTextureView view, int levels, bool owns);
void t3_wgpu_release(uint32_t kind, void *thing);
/* a render target texture of a format, recreated when the size changes */
WGPUTextureView t3_wgpu_target(t3_wgpu *w, void **slot, int width, int height, WGPUTextureFormat fmt, bool sampled);

/* a render target's attachments (made, or remade after a resize): colour
 * (the multisampled one when samples > 1, with `resolve` the target's own
 * texture), depth (its DepthTexture or an internal buffer; NULL without a
 * depth buffer) */
typedef struct {
  WGPUTextureView color, resolve, depth;
  WGPUTextureFormat color_format, depth_format;
  int samples;
} t3_wgpu_rt;
void t3_wgpu_render_target(t3_wgpu *w, t3_render_target *rt, t3_wgpu_rt *out);
/* after a render into rt: its texture's mipmaps */
void t3_wgpu_render_target_done(t3_wgpu *w, t3_render_target *rt);
void t3_wgpu_render_target_free(t3_render_target *rt);

/* a shadow map: a Depth24Plus texture (6 layers for a point light's cube)
 * kept in *slot, its view for sampling (2D or cube) and one attachment view
 * per face */
WGPUTextureView t3_wgpu_depth_map(t3_wgpu *w, void **slot, int width, int height, bool cube, WGPUTextureView *face_views);
/* r186's shadow sampler: LessEqual comparison, linear (PCF) or nearest */
WGPUSampler t3_wgpu_compare_sampler(t3_wgpu *w, bool linear);
/* an RGBA32Float 2D array (morph targets), remade when version changes */
WGPUTextureView t3_wgpu_array_texture(t3_wgpu *w, void **slot, uint32_t version, int width, int height, int layers, const float *data);
/* the array in *slot if it is still that version, else NULL */
WGPUTextureView t3_wgpu_array_current(void *slot, uint32_t version);

/* read a view's texture back (RGBA8), blocking: for checks and the bench */
bool t3_wgpu_read_rgba8(t3_wgpu *w, void *texture_slot, int width, int height, uint8_t *out);
WGPUDevice t3_wgpu_device(t3_wgpu *w);
/* the device lacks core-features-and-limits (WebGPU compatibility mode, as wasmcart gives carts) */
bool t3_wgpu_compat(t3_wgpu *w);

#endif
#endif
