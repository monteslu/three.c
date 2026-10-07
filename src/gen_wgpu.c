/* See gen_wgpu.h. */
#ifdef T3_WGPU
#include "gen_wgpu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SV(s) ((WGPUStringView){ (s), WGPU_STRLEN })
#define MAX_PIPES 32
#define MAX_BG 64

struct pipe { t3_wgpu_state st; WGPURenderPipeline p; };
struct bg { WGPUTextureView views[8]; WGPUSampler samplers[8]; uint32_t gen, epoch; WGPUBindGroup g; };
/* bumped when three.c releases a view: a cached bind group keyed by a view
 * pointer must not match a new view that reuses the address */
static uint32_t view_epoch;

struct gen_wgpu_prog {
  WGPUShaderModule vs, fs;
  WGPUShaderModule vs_flip;   /* made on first use: clip y negated */
  char *vtext;                /* the vertex source, for vs_flip */
  WGPUBindGroupLayout bgl[T3_GEN_MAX_GROUPS];
  bool present[T3_GEN_MAX_GROUPS];
  WGPUPipelineLayout layout;
  struct pipe pipes[MAX_PIPES]; int n_pipes;
  struct bg bgs[T3_GEN_MAX_GROUPS][MAX_BG]; int n_bgs[T3_GEN_MAX_GROUPS], next_bg[T3_GEN_MAX_GROUPS];
  /* per group: its textures (manifest order) and extra buffers */
  int n_tex[T3_GEN_MAX_GROUPS];
  const t3_bk_texture_layout *tex[T3_GEN_MAX_GROUPS][8];
};

struct target { WGPUTexture tex; WGPUTextureView view; int w, h; WGPUTextureFormat fmt; };
struct vbuf { WGPUBuffer b; size_t size; uint32_t version; bool uint; uint32_t serial; };
struct sampler_entry { uint32_t key; WGPUSampler s; };

struct t3_wgpu {
  WGPUDevice dev;
  WGPUQueue q;
  WGPUTextureFormat out_format;
  WGPUBuffer stream;
  size_t stream_size;
  uint32_t stream_gen;
  uint8_t *stage;
  size_t stage_n, stage_cap;
  WGPUCommandEncoder enc;
  WGPURenderPassEncoder pass;
  /* submits: one per render (r186's), or deferred until t3_wgpu_flush, or
   * an embedder's encoder (never submitted here); `written`: how much of the
   * stage is already in the stream buffer */
  bool defer;
  WGPUCommandEncoder ext;
  size_t written;
  uint32_t serial;    /* the open frame's number: a vertex buffer written in it is renamed, not overwritten */
  struct sampler_entry samplers[64];
  int n_samplers;
  /* r186's mipmap generator (WebGPUTexturePassUtils): one pipeline per format */
  WGPUShaderModule mip_module;
  WGPUSampler mip_sampler;
  WGPUBuffer mip_noflip, mip_flip;
  struct { WGPUTextureFormat fmt; WGPURenderPipeline p; } mip_pipes[8];
  int n_mip_pipes;
};

WGPUDevice t3_wgpu_device(t3_wgpu *w) { return w->dev; }
bool t3_wgpu_compat(t3_wgpu *w) { return !wgpuDeviceHasFeature(w->dev, WGPUFeatureName_CoreFeaturesAndLimits); }

t3_wgpu *t3_wgpu_new(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat out_format) {
  t3_wgpu *w = calloc(1, sizeof *w);
  T3_CHECK_ALLOC(w);
  w->dev = device;
  w->q = queue;
  w->out_format = out_format;
  return w;
}
void t3_wgpu_destroy(t3_wgpu *w) {
  if (!w) return;
  if (w->stream) wgpuBufferRelease(w->stream);
  for (int i = 0; i < w->n_samplers; i++) wgpuSamplerRelease(w->samplers[i].s);
  for (int i = 0; i < w->n_mip_pipes; i++) wgpuRenderPipelineRelease(w->mip_pipes[i].p);
  if (w->mip_module) wgpuShaderModuleRelease(w->mip_module);
  if (w->mip_sampler) wgpuSamplerRelease(w->mip_sampler);
  if (w->mip_noflip) wgpuBufferRelease(w->mip_noflip);
  if (w->mip_flip) wgpuBufferRelease(w->mip_flip);
  free(w->stage);
  free(w);
}

/* ── programs ───────────────────────────────────────────────────────── */
static WGPUShaderModule module(t3_wgpu *w, const char *code) {
  WGPUShaderSourceWGSL wgsl = { .chain = { .sType = WGPUSType_ShaderSourceWGSL }, .code = SV(code) };
  WGPUShaderModuleDescriptor d = { .nextInChain = &wgsl.chain };
  return wgpuDeviceCreateShaderModule(w->dev, &d);
}
static WGPUTextureSampleType sample_type(t3_bk_sample_type s) {
  switch (s) {
  case T3_BK_SAMPLE_UNFILTERABLE_FLOAT: return WGPUTextureSampleType_UnfilterableFloat;
  case T3_BK_SAMPLE_DEPTH: return WGPUTextureSampleType_Depth;
  case T3_BK_SAMPLE_SINT: return WGPUTextureSampleType_Sint;
  case T3_BK_SAMPLE_UINT: return WGPUTextureSampleType_Uint;
  default: return WGPUTextureSampleType_Float;
  }
}
static WGPUTextureViewDimension view_dim(t3_bk_tex_dim d) {
  switch (d) {
  case T3_BK_DIM_CUBE: return WGPUTextureViewDimension_Cube;
  case T3_BK_DIM_2D_ARRAY: return WGPUTextureViewDimension_2DArray;
  case T3_BK_DIM_3D: return WGPUTextureViewDimension_3D;
  default: return WGPUTextureViewDimension_2D;
  }
}

t3_gen_gl *t3_gen_wgpu_build(t3_wgpu *w, const t3_gen_program *src, t3_gen_params params, char *err, size_t errcap) {
  if (src->n_groups > T3_GEN_MAX_GROUPS) { snprintf(err, errcap, "%s: %d groups", src->state, src->n_groups); return NULL; }
  char *vt = t3_gen_apply_templates(src->vertex, src, params, err, errcap);
  if (!vt) return NULL;
  char *ft = t3_gen_apply_templates(src->fragment, src, params, err, errcap);
  if (!ft) { free(vt); return NULL; }
  t3_gen_gl *g = calloc(1, sizeof *g);
  struct gen_wgpu_prog *wg = calloc(1, sizeof *wg);
  T3_CHECK_ALLOC(g); T3_CHECK_ALLOC(wg);
  g->src = src;
  g->params = params;
  g->wg = wg;
  g->n_groups = src->n_groups;
  wg->vs = module(w, vt);
  wg->vtext = vt;
  wg->fs = module(w, ft);
  /* which groups the stages declare */
  for (int i = 0; i < src->n_groups; i++) {
    char tag[32];
    snprintf(tag, sizeof tag, "@group( %d )", i);
    wg->present[i] = strstr(vt, tag) || strstr(ft, tag);
  }
  free(ft);
  /* groups: the uniform struct (a dynamic offset into the frame stream), its
   * textures + samplers, its extra buffers (dynamic offsets too) */
  for (int i = 0; i < src->n_groups; i++) {
    const t3_bk_group_layout *L = src->groups[i];
    WGPUBindGroupLayoutEntry e[24];
    int n = 0;
    memset(e, 0, sizeof e);
    if (wg->present[i] && L->uniform_bytes) {
      g->bytes[i] = L->uniform_bytes;
      g->mirror[i] = calloc(1, L->uniform_bytes);
      T3_CHECK_ALLOC(g->mirror[i]);
      e[n].binding = L->uniform_binding;
      e[n].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
      e[n].buffer.type = WGPUBufferBindingType_Uniform;
      e[n].buffer.hasDynamicOffset = 1;
      e[n].buffer.minBindingSize = L->uniform_bytes;
      n++;
    }
    for (uint32_t t = 0; t < L->n_textures && t < 8; t++) {
      const t3_bk_texture_layout *T = &L->textures[t];
      wg->tex[i][wg->n_tex[i]++] = T;
      e[n].binding = T->binding;
      e[n].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
      e[n].texture.sampleType = sample_type(T->sample);
      e[n].texture.viewDimension = view_dim(T->dim);
      n++;
      if (T->has_sampler) {
        e[n].binding = T->sampler_binding;
        e[n].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[n].sampler.type = T->compare_sampler ? WGPUSamplerBindingType_Comparison
                          : T->sample == T3_BK_SAMPLE_UNFILTERABLE_FLOAT ? WGPUSamplerBindingType_NonFiltering
                          : WGPUSamplerBindingType_Filtering;
        n++;
      }
    }
    for (uint32_t k = 0; k < L->n_buffers && g->n_buffers < T3_GEN_MAX_BUFFERS; k++) {
      const t3_bk_buffer_layout *B = &L->buffers[k];
      uint32_t bytes = B->bytes;
      if (B->source && !strcmp(B->source, "boneMatrices")) bytes = (uint32_t)params.bones * 64;
      else if (B->source && !strcmp(B->source, "morphInfluences")) bytes = (uint32_t)params.morphs * 16;
      int bi = g->n_buffers++;
      g->buf[bi].bytes = bytes;
      g->buf[bi].source = B->source;
      g->buf[bi].point = (GLuint)i;          /* (WebGPU: the group it belongs to) */
      g->buf[bi].ubo = B->binding;           /* (WebGPU: its binding number) */
      e[n].binding = B->binding;
      e[n].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
      e[n].buffer.type = B->storage ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Uniform;
      e[n].buffer.hasDynamicOffset = 1;
      e[n].buffer.minBindingSize = bytes;
      n++;
    }
    WGPUBindGroupLayoutDescriptor d = { .entryCount = (size_t)n, .entries = e };
    wg->bgl[i] = wgpuDeviceCreateBindGroupLayout(w->dev, &d);
  }
  WGPUPipelineLayoutDescriptor pd = { .bindGroupLayoutCount = (size_t)src->n_groups, .bindGroupLayouts = wg->bgl };
  wg->layout = wgpuDeviceCreatePipelineLayout(w->dev, &pd);
  t3_gen_common(g);
  return g;
}

void t3_gen_wgpu_free(t3_gen_gl *g) {
  if (!g || !g->wg) return;
  struct gen_wgpu_prog *wg = g->wg;
  for (int i = 0; i < wg->n_pipes; i++) wgpuRenderPipelineRelease(wg->pipes[i].p);
  for (int i = 0; i < T3_GEN_MAX_GROUPS; i++) {
    for (int k = 0; k < wg->n_bgs[i]; k++) wgpuBindGroupRelease(wg->bgs[i][k].g);
    if (wg->bgl[i]) wgpuBindGroupLayoutRelease(wg->bgl[i]);
    free(g->mirror[i]);
  }
  wgpuPipelineLayoutRelease(wg->layout);
  wgpuShaderModuleRelease(wg->vs);
  if (wg->vs_flip) wgpuShaderModuleRelease(wg->vs_flip);
  free(wg->vtext);
  wgpuShaderModuleRelease(wg->fs);
  free(g->ops);
  free(wg);
  free(g);
}

/* ── pipelines ──────────────────────────────────────────────────────── */
static WGPUCompareFunction compare(int f) {
  /* three.js depth functions: Never, Always, Less, LessEqual, Equal, GreaterEqual, Greater, NotEqual */
  static const WGPUCompareFunction m[8] = { WGPUCompareFunction_Never, WGPUCompareFunction_Always, WGPUCompareFunction_Less,
    WGPUCompareFunction_LessEqual, WGPUCompareFunction_Equal, WGPUCompareFunction_GreaterEqual, WGPUCompareFunction_Greater,
    WGPUCompareFunction_NotEqual };
  return f >= 0 && f < 8 ? m[f] : WGPUCompareFunction_LessEqual;
}
static WGPUVertexFormat float_format(int n) {
  return n == 1 ? WGPUVertexFormat_Float32 : n == 2 ? WGPUVertexFormat_Float32x2 : n == 3 ? WGPUVertexFormat_Float32x3 : WGPUVertexFormat_Float32x4;
}
static int attr_components(const char *type) {
  if (!strcmp(type, "vec2")) return 2;
  if (!strcmp(type, "vec3")) return 3;
  if (!strcmp(type, "vec4")) return 4;
  return 1;
}

WGPURenderPipeline t3_wgpu_pipeline(t3_wgpu *w, t3_gen_gl *g, const t3_wgpu_state *st) {
  struct gen_wgpu_prog *wg = g->wg;
  for (int i = 0; i < wg->n_pipes; i++) if (!memcmp(&wg->pipes[i].st, st, sizeof *st)) return wg->pipes[i].p;
  /* vertex buffers: one per geometry attribute (float32), the instance
   * matrix columns (nodeAttribute0..3) from one instance-step buffer */
  const t3_gen_program *src = g->src;
  WGPUVertexBufferLayout vb[16];
  WGPUVertexAttribute va[16];
  int nb = 0, na = 0, inst_buf = -1, icol_buf = -1;
  for (int a = 0; a < src->n_attributes && nb < 16; a++) {
    const t3_gen_attribute *A = &src->attributes[a];
    if (!strncmp(A->name, "nodeAttribute", 13)) {
      int col = A->name[13] - '0';
      if (inst_buf < 0) {
        inst_buf = nb++;
        vb[inst_buf] = (WGPUVertexBufferLayout){ .stepMode = WGPUVertexStepMode_Instance, .arrayStride = st->instance == 2 ? 80 : 64,
                                                 .attributeCount = 0, .attributes = &va[na] };
      }
      va[na++] = (WGPUVertexAttribute){ .format = WGPUVertexFormat_Float32x4, .offset = (uint64_t)col * 16, .shaderLocation = A->location };
      vb[inst_buf].attributeCount++;
      continue;
    }
    if (!strcmp(A->name, "instanceColor")) {   /* InstancedMesh.instanceColor: rgb float32 per instance */
      va[na] = (WGPUVertexAttribute){ .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = A->location };
      vb[nb++] = (WGPUVertexBufferLayout){ .stepMode = WGPUVertexStepMode_Instance, .arrayStride = 12, .attributeCount = 1, .attributes = &va[na] };
      na++;
      icol_buf = nb - 1;
      continue;
    }
    int c = attr_components(A->type[0] == 'u' ? A->type + 1 : A->type);
    /* a uvecN input (skinIndex) reads uint32 (t3_wgpu_attribute_uint) */
    static const WGPUVertexFormat uf[5] = { 0, WGPUVertexFormat_Uint32, WGPUVertexFormat_Uint32x2, WGPUVertexFormat_Uint32x3, WGPUVertexFormat_Uint32x4 };
    va[na] = (WGPUVertexAttribute){ .format = A->type[0] == 'u' ? uf[c] : float_format(c), .offset = 0, .shaderLocation = A->location };
    vb[nb++] = (WGPUVertexBufferLayout){ .stepMode = WGPUVertexStepMode_Vertex, .arrayStride = 0 /* set per geometry: see below */,
                                         .attributeCount = 1, .attributes = &va[na] };
    na++;
  }
  /* geometry attribute buffers are tightly packed float32 of the geometry's
   * item size, which the program's type may exceed (vec4 uv1 from a vec2):
   * the stride comes in through the state */
  for (int b = 0, k = 0; b < nb; b++) {
    if (b == inst_buf || b == icol_buf) continue;
    vb[b].arrayStride = (uint64_t)st->strides[k++] * 4;
  }
  WGPUBlendState blend = {
    .color = { .operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_SrcAlpha, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha },
    .alpha = { .operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_One, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha } };
  if (st->blend == 2) {   /* AdditiveBlending */
    blend.color = (WGPUBlendComponent){ WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_One };
    blend.alpha = (WGPUBlendComponent){ WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One };
  }
  WGPUColorTargetState ct = { .format = st->color, .blend = st->blend ? &blend : NULL,
                              .writeMask = st->color_write ? WGPUColorWriteMask_All : WGPUColorWriteMask_None };
  WGPUFragmentState fs = { .module = wg->fs, .entryPoint = SV("main"), .targetCount = st->color ? 1 : 0, .targets = &ct };
  WGPUDepthStencilState ds = { .format = st->depth, .depthWriteEnabled = st->depth_write ? WGPUOptionalBool_True : WGPUOptionalBool_False,
    .depthCompare = st->depth_test ? compare(st->depth_func) : WGPUCompareFunction_Always,
    .stencilFront = { .compare = WGPUCompareFunction_Always, .failOp = WGPUStencilOperation_Keep, .depthFailOp = WGPUStencilOperation_Keep, .passOp = WGPUStencilOperation_Keep },
    .stencilBack = { .compare = WGPUCompareFunction_Always, .failOp = WGPUStencilOperation_Keep, .depthFailOp = WGPUStencilOperation_Keep, .passOp = WGPUStencilOperation_Keep },
    .stencilReadMask = 0xFFFFFFFF, .stencilWriteMask = 0xFFFFFFFF };
  static const WGPUPrimitiveTopology topo[4] = { WGPUPrimitiveTopology_TriangleList, WGPUPrimitiveTopology_LineList,
                                                 WGPUPrimitiveTopology_LineStrip, WGPUPrimitiveTopology_PointList };
  WGPUShaderModule vs = wg->vs;
  if (st->flip_y) {
    if (!wg->vs_flip) {
      /* every r186 vertex program ends `return varyings;` with the clip
       * position in varyings.builtinClipSpace: negate its y just before */
      const char *ret = "\treturn varyings;";
      char *at = strstr(wg->vtext, ret);
      if (!at || strstr(at + 1, ret) || !strstr(wg->vtext, "builtinClipSpace")) return NULL;
      size_t pre = (size_t)(at - wg->vtext);
      const char *neg = "\tvaryings.builtinClipSpace.y = - varyings.builtinClipSpace.y;\n";
      char *t = malloc(strlen(wg->vtext) + strlen(neg) + 1);
      T3_CHECK_ALLOC(t);
      memcpy(t, wg->vtext, pre);
      strcpy(t + pre, neg);
      strcat(t, at);
      wg->vs_flip = module(w, t);
      free(t);
    }
    vs = wg->vs_flip;
  }
  WGPURenderPipelineDescriptor d = {
    .layout = wg->layout,
    .vertex = { .module = vs, .entryPoint = SV("main"), .bufferCount = (size_t)nb, .buffers = vb },
    .primitive = { .topology = topo[st->topology & 3], .frontFace = st->flip_y ? WGPUFrontFace_CW : WGPUFrontFace_CCW,
                   .cullMode = st->cull == 1 ? WGPUCullMode_Back : st->cull == 2 ? WGPUCullMode_Front : WGPUCullMode_None },
    .depthStencil = st->depth ? &ds : NULL,
    .multisample = { .count = st->samples > 1 ? st->samples : 1, .mask = 0xFFFFFFFF },
    .fragment = &fs,
  };
  WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(w->dev, &d);
  if (wg->n_pipes < MAX_PIPES) wg->pipes[wg->n_pipes++] = (struct pipe){ *st, p };
  return p;
}

/* ── bind groups ────────────────────────────────────────────────────── */
WGPUBindGroup t3_wgpu_bind_group(t3_wgpu *w, t3_gen_gl *g, int group, WGPUTextureView const *views, WGPUSampler const *samplers) {
  struct gen_wgpu_prog *wg = g->wg;
  int nt = wg->n_tex[group];
  for (int k = 0; k < wg->n_bgs[group]; k++) {
    struct bg *b = &wg->bgs[group][k];
    if (b->gen == w->stream_gen && b->epoch == view_epoch && !memcmp(b->views, views, sizeof(WGPUTextureView) * (size_t)nt) &&
        !memcmp(b->samplers, samplers, sizeof(WGPUSampler) * (size_t)nt))
      return b->g;
  }
  const t3_bk_group_layout *L = g->src->groups[group];
  WGPUBindGroupEntry e[24];
  int n = 0;
  memset(e, 0, sizeof e);
  if (g->bytes[group]) e[n++] = (WGPUBindGroupEntry){ .binding = L->uniform_binding, .buffer = w->stream, .offset = 0, .size = g->bytes[group] };
  for (int t = 0; t < nt; t++) {
    const t3_bk_texture_layout *T = wg->tex[group][t];
    e[n++] = (WGPUBindGroupEntry){ .binding = T->binding, .textureView = views[t] };
    if (T->has_sampler) e[n++] = (WGPUBindGroupEntry){ .binding = T->sampler_binding, .sampler = samplers[t] };
  }
  for (int bi = 0; bi < g->n_buffers; bi++)
    if ((int)g->buf[bi].point == group)
      e[n++] = (WGPUBindGroupEntry){ .binding = g->buf[bi].ubo, .buffer = w->stream, .offset = 0, .size = g->buf[bi].bytes };
  WGPUBindGroupDescriptor d = { .layout = wg->bgl[group], .entryCount = (size_t)n, .entries = e };
  WGPUBindGroup bg = wgpuDeviceCreateBindGroup(w->dev, &d);
  int slot;
  if (wg->n_bgs[group] < MAX_BG) slot = wg->n_bgs[group]++;
  else { slot = wg->next_bg[group]; wg->next_bg[group] = (slot + 1) % MAX_BG; wgpuBindGroupRelease(wg->bgs[group][slot].g); }
  struct bg *b = &wg->bgs[group][slot];
  memset(b, 0, sizeof *b);
  memcpy(b->views, views, sizeof(WGPUTextureView) * (size_t)nt);
  memcpy(b->samplers, samplers, sizeof(WGPUSampler) * (size_t)nt);
  b->gen = w->stream_gen;
  b->epoch = view_epoch;
  b->g = bg;
  return bg;
}

/* ── the frame ──────────────────────────────────────────────────────── */
void t3_wgpu_flush(t3_wgpu *w) {
  if (!w->enc) return;
  w->serial++;
  if (w->enc != w->ext) {
    WGPUCommandBufferDescriptor cd = { 0 };
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(w->enc, &cd);
    wgpuQueueSubmit(w->q, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(w->enc);
  }
  w->enc = NULL;
}
void t3_wgpu_set_defer(t3_wgpu *w, bool on) { if (!on) t3_wgpu_flush(w); w->defer = on; }
void t3_wgpu_set_encoder(t3_wgpu *w, WGPUCommandEncoder enc) { t3_wgpu_flush(w); w->ext = enc; }
void t3_wgpu_begin_frame(t3_wgpu *w, size_t estimate) {
  if (w->enc) {
    /* a frame is open (deferred or the embedder's encoder): keep streaming
     * into it while the rest of the buffer holds this render */
    if (w->stage_n + estimate + 256 <= w->stream_size) return;
    t3_wgpu_flush(w);
  }
  w->stage_n = 0;
  w->written = 0;
  /* the stream buffer must exist (and be big enough) before bind groups refer
   * to it: a new buffer is a new generation of bind groups */
  size_t want = estimate < 65536 ? 65536 : estimate;
  if (!w->stream || w->stream_size < want) {
    if (w->stream) wgpuBufferRelease(w->stream);
    w->stream_size = want * 2;
    WGPUBufferDescriptor d = { .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, .size = w->stream_size };
    w->stream = wgpuDeviceCreateBuffer(w->dev, &d);
    w->stream_gen++;
  }
  if (w->stage_cap < w->stream_size) {
    w->stage_cap = w->stream_size;
    w->stage = realloc(w->stage, w->stage_cap);
    T3_CHECK_ALLOC(w->stage);
  }
  if (w->ext) w->enc = w->ext;
  else { WGPUCommandEncoderDescriptor ed = { 0 }; w->enc = wgpuDeviceCreateCommandEncoder(w->dev, &ed); }
}
uint32_t t3_wgpu_stream(t3_wgpu *w, const void *data, uint32_t n) {
  size_t off = (w->stage_n + 255) & ~(size_t)255;
  if (off + n > w->stream_size) return 0;   /* (the estimate was too small: drawn with stale data; begin_frame grows it next frame) */
  memcpy(w->stage + off, data, n);
  w->stage_n = off + n;
  return (uint32_t)off;
}
WGPUBuffer t3_wgpu_stream_buffer(t3_wgpu *w) { return w->stream; }
void t3_wgpu_begin_pass(t3_wgpu *w, const t3_wgpu_pass *p) {
  WGPURenderPassColorAttachment ca = { .view = p->color, .resolveTarget = p->resolve, .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
    .loadOp = p->load_color ? WGPULoadOp_Load : WGPULoadOp_Clear, .storeOp = WGPUStoreOp_Store,
    .clearValue = { p->clear[0], p->clear[1], p->clear[2], p->clear[3] } };
  WGPURenderPassDepthStencilAttachment da = { .view = p->depth, .depthLoadOp = p->load_depth ? WGPULoadOp_Load : WGPULoadOp_Clear,
    .depthStoreOp = WGPUStoreOp_Store, .depthClearValue = p->depth_clear_set ? p->clear_depth : 1.0f };
  if (p->stencil) {
    da.stencilLoadOp = p->load_depth ? WGPULoadOp_Load : WGPULoadOp_Clear;
    da.stencilStoreOp = WGPUStoreOp_Store;
    da.stencilClearValue = p->clear_stencil;
  }
  WGPURenderPassDescriptor d = { .colorAttachmentCount = p->color ? 1 : 0, .colorAttachments = &ca, .depthStencilAttachment = p->depth ? &da : NULL };
  w->pass = wgpuCommandEncoderBeginRenderPass(w->enc, &d);
  if (p->vw > 0) wgpuRenderPassEncoderSetViewport(w->pass, (float)p->vx, (float)p->vy, (float)p->vw, (float)p->vh, 0, 1);
  if (p->scissor) wgpuRenderPassEncoderSetScissorRect(w->pass, (uint32_t)p->sx, (uint32_t)p->sy, (uint32_t)p->sw, (uint32_t)p->sh);
}
WGPURenderPassEncoder t3_wgpu_pass_encoder(t3_wgpu *w) { return w->pass; }
void t3_wgpu_end_pass(t3_wgpu *w) {
  wgpuRenderPassEncoderEnd(w->pass);
  wgpuRenderPassEncoderRelease(w->pass);
  w->pass = NULL;
}
void t3_wgpu_end_frame(t3_wgpu *w) {
  /* this render's part of the stream (queue writes land before the submit) */
  size_t end = (w->stage_n + 3) & ~(size_t)3;
  if (end > w->written) wgpuQueueWriteBuffer(w->q, w->stream, w->written, w->stage + w->written, end - w->written);
  w->written = end;
  if (!w->defer && !w->ext) t3_wgpu_flush(w);
}

/* ── mipmaps: r186's WebGPUTexturePassUtils, a pass per level drawing the
 * level above through a linear-min sampler over one big triangle ────── */
static const char mip_wgsl[] =
  "struct VarysStruct { @builtin( position ) Position: vec4f, @location( 0 ) vTex : vec2f };\n"
  "@group( 0 ) @binding ( 2 ) var<uniform> flipY: u32;\n"
  "@vertex fn mainVS( @builtin( vertex_index ) vertexIndex : u32 ) -> VarysStruct {\n"
  "  var Varys : VarysStruct;\n"
  "  var pos = array( vec2f( -1, -1 ), vec2f( -1, 3 ), vec2f( 3, -1 ) );\n"
  "  let p = pos[ vertexIndex ];\n"
  "  let mult = select( vec2f( 0.5, -0.5 ), vec2f( 0.5, 0.5 ), flipY != 0 );\n"
  "  Varys.vTex = p * mult + vec2f( 0.5 );\n"
  "  Varys.Position = vec4f( p, 0, 1 );\n"
  "  return Varys;\n"
  "}\n"
  "@group( 0 ) @binding( 0 ) var imgSampler : sampler;\n"
  "@group( 0 ) @binding( 1 ) var img2d : texture_2d<f32>;\n"
  "@fragment fn main_2d( Varys: VarysStruct ) -> @location( 0 ) vec4<f32> { return textureSample( img2d, imgSampler, Varys.vTex ); }\n";

static WGPURenderPipeline mip_pipeline(t3_wgpu *w, WGPUTextureFormat fmt) {
  for (int i = 0; i < w->n_mip_pipes; i++) if (w->mip_pipes[i].fmt == fmt) return w->mip_pipes[i].p;
  if (!w->mip_module) {
    w->mip_module = module(w, mip_wgsl);
    WGPUSamplerDescriptor sd = { .addressModeU = WGPUAddressMode_ClampToEdge, .addressModeV = WGPUAddressMode_ClampToEdge,
      .addressModeW = WGPUAddressMode_ClampToEdge, .magFilter = WGPUFilterMode_Nearest, .minFilter = WGPUFilterMode_Linear,
      .mipmapFilter = WGPUMipmapFilterMode_Nearest, .lodMinClamp = 0, .lodMaxClamp = 32, .maxAnisotropy = 1 };
    w->mip_sampler = wgpuDeviceCreateSampler(w->dev, &sd);
    WGPUBufferDescriptor bd = { .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, .size = 16 };
    w->mip_noflip = wgpuDeviceCreateBuffer(w->dev, &bd);
    w->mip_flip = wgpuDeviceCreateBuffer(w->dev, &bd);
    uint32_t zero[4] = { 0 }, one[4] = { 1 };
    wgpuQueueWriteBuffer(w->q, w->mip_noflip, 0, zero, 16);
    wgpuQueueWriteBuffer(w->q, w->mip_flip, 0, one, 16);
  }
  WGPUColorTargetState ct = { .format = fmt, .writeMask = WGPUColorWriteMask_All };
  WGPUFragmentState fs = { .module = w->mip_module, .entryPoint = SV("main_2d"), .targetCount = 1, .targets = &ct };
  WGPURenderPipelineDescriptor d = {
    .vertex = { .module = w->mip_module, .entryPoint = SV("mainVS") },
    .primitive = { .topology = WGPUPrimitiveTopology_TriangleList, .cullMode = WGPUCullMode_None, .frontFace = WGPUFrontFace_CCW },
    .multisample = { .count = 1, .mask = 0xFFFFFFFF },
    .fragment = &fs };
  WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(w->dev, &d);
  if (w->n_mip_pipes < 8) { w->mip_pipes[w->n_mip_pipes].fmt = fmt; w->mip_pipes[w->n_mip_pipes++].p = p; }
  return p;
}

static void generate_mipmaps_layers(t3_wgpu *w, WGPUTexture tex, WGPUTextureFormat fmt, uint32_t levels, uint32_t layers);
static void generate_mipmaps(t3_wgpu *w, WGPUTexture tex, WGPUTextureFormat fmt, uint32_t levels) { generate_mipmaps_layers(w, tex, fmt, levels, 1); }
/* (a cube's faces: each layer's chain from its own level above, as r186's
 * 2d-array mipmap pipeline does) */
static void generate_mipmaps_layers(t3_wgpu *w, WGPUTexture tex, WGPUTextureFormat fmt, uint32_t levels, uint32_t layers) {
  WGPURenderPipeline p = mip_pipeline(w, fmt);
  WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(p, 0);
  /* into the open frame when there is one (its draws into the texture come first) */
  bool own = !w->enc || w->pass;
  WGPUCommandEncoder enc = own ? wgpuDeviceCreateCommandEncoder(w->dev, NULL) : w->enc;
  for (uint32_t lv = 1; lv < levels; lv++) for (uint32_t layer = 0; layer < layers; layer++) {
    WGPUTextureViewDescriptor sv = { .format = fmt, .dimension = WGPUTextureViewDimension_2D, .baseMipLevel = lv - 1, .mipLevelCount = 1,
                                     .baseArrayLayer = layer, .arrayLayerCount = 1, .aspect = WGPUTextureAspect_All };
    WGPUTextureView src = wgpuTextureCreateView(tex, &sv);
    WGPUTextureViewDescriptor dv = sv;
    dv.baseMipLevel = lv;
    WGPUTextureView dst = wgpuTextureCreateView(tex, &dv);
    WGPUBindGroupEntry e[3] = { { .binding = 0, .sampler = w->mip_sampler }, { .binding = 1, .textureView = src },
                                { .binding = 2, .buffer = w->mip_noflip, .size = 4 } };
    WGPUBindGroupDescriptor bd = { .layout = bgl, .entryCount = 3, .entries = e };
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(w->dev, &bd);
    WGPURenderPassColorAttachment ca = { .view = dst, .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED, .loadOp = WGPULoadOp_Clear, .storeOp = WGPUStoreOp_Store };
    WGPURenderPassDescriptor pd = { .colorAttachmentCount = 1, .colorAttachments = &ca };
    WGPURenderPassEncoder pe = wgpuCommandEncoderBeginRenderPass(enc, &pd);
    wgpuRenderPassEncoderSetPipeline(pe, p);
    wgpuRenderPassEncoderSetBindGroup(pe, 0, bg, 0, NULL);
    wgpuRenderPassEncoderDraw(pe, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pe);
    wgpuRenderPassEncoderRelease(pe);
    wgpuBindGroupRelease(bg);
    wgpuTextureViewRelease(src);
    wgpuTextureViewRelease(dst);
  }
  if (own) {
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, NULL);
    wgpuQueueSubmit(w->q, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);
  }
  wgpuBindGroupLayoutRelease(bgl);
}

/* ── resources ──────────────────────────────────────────────────────── */
/* a geometry attribute as float32 (vertex) or as uint16 / uint32 (index) */
static WGPUBuffer attribute_buf(t3_wgpu *w, t3_attribute *a, bool index, uint32_t *index_format, bool as_uint);
WGPUBuffer t3_wgpu_attribute(t3_wgpu *w, t3_attribute *a, bool index, uint32_t *index_format) { return attribute_buf(w, a, index, index_format, false); }
WGPUBuffer t3_wgpu_attribute_uint(t3_wgpu *w, t3_attribute *a) { return attribute_buf(w, a, false, NULL, true); }
static WGPUBuffer attribute_buf(t3_wgpu *w, t3_attribute *a, bool index, uint32_t *index_format, bool as_uint) {
  struct vbuf *v = a->_wgpu;
  size_t n = (size_t)a->count * a->item_size;
  size_t bytes = index ? n * (a->type == T3_UINT16 ? 2 : 4) : n * 4;
  size_t padded = (bytes + 3) & ~(size_t)3;
  if (index_format) *index_format = a->type == T3_UINT16 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
  if (v && v->version == a->version && v->size >= padded && v->uint == as_uint) return v->b;
  if (!v) { v = calloc(1, sizeof *v); T3_CHECK_ALLOC(v); a->_wgpu = v; }
  if (v->b && w->enc && v->serial == w->serial) { wgpuBufferRelease(v->b); v->b = NULL; }   /* (in use by the open frame: renamed) */
  if (!v->b || v->size < padded) {
    if (v->b) wgpuBufferRelease(v->b);
    WGPUBufferDescriptor d = { .usage = (index ? WGPUBufferUsage_Index : WGPUBufferUsage_Vertex) | WGPUBufferUsage_CopyDst, .size = padded };
    v->b = wgpuDeviceCreateBuffer(w->dev, &d);
    v->size = padded;
  }
  void *tmp = NULL;
  const void *data = a->array;
  if (as_uint) {
    uint32_t *u = malloc(n * sizeof *u);
    T3_CHECK_ALLOC(u);
    for (size_t i = 0; i < n; i++) u[i] = a->type == T3_UINT16 ? ((const uint16_t *)a->array)[i] : a->type == T3_UINT32 ? ((const uint32_t *)a->array)[i]
                                         : (uint32_t)((const float *)a->array)[i];
    data = tmp = u;
  } else if (!index && a->type != T3_FLOAT32) {
    float *f = malloc(n * sizeof *f);
    T3_CHECK_ALLOC(f);
    for (size_t i = 0; i < n; i++) f[i] = a->type == T3_UINT16 ? (float)((const uint16_t *)a->array)[i] : (float)((const uint32_t *)a->array)[i];
    data = tmp = f;
  } else if (padded != bytes) {
    tmp = calloc(1, padded);
    T3_CHECK_ALLOC(tmp);
    memcpy(tmp, a->array, bytes);
    data = tmp;
  }
  wgpuQueueWriteBuffer(w->q, v->b, 0, data, padded);
  v->serial = w->serial;
  free(tmp);
  v->version = a->version;
  v->uint = as_uint;
  return v->b;
}
WGPUBuffer t3_wgpu_floats(t3_wgpu *w, void **slot, const float *data, size_t n) {
  struct vbuf *v = *slot;
  size_t bytes = n * 4;
  if (!v) { v = calloc(1, sizeof *v); T3_CHECK_ALLOC(v); *slot = v; }
  if (!bytes) return v->b;
  /* rewritten while a deferred frame still reads it: a new buffer instead */
  bool busy = v->b && w->enc && v->serial == w->serial;
  if (!v->b || v->size < bytes || busy) {
    if (v->b) wgpuBufferRelease(v->b);
    size_t sz = v->size > bytes * 2 ? v->size : bytes * 2;
    WGPUBufferDescriptor d = { .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, .size = sz };
    v->b = wgpuDeviceCreateBuffer(w->dev, &d);
    v->size = sz;
  }
  wgpuQueueWriteBuffer(w->q, v->b, 0, data, bytes);
  v->serial = w->serial;
  return v->b;
}

static float srgb_dec(uint8_t c) { float v = c / 255.0f; return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f); }
static uint8_t srgb_enc(float v) { v = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1 / 2.4f) - 0.055f; v = v < 0 ? 0 : v > 1 ? 1 : v; return (uint8_t)(v * 255 + 0.5f); }
static void cube_mips_cpu(t3_wgpu *w, WGPUTexture tex, const uint8_t *faces, int fw, int fh, uint32_t levels, bool srgb) {
  for (int f = 0; f < 6; f++) {
    const uint8_t *prev = faces + (size_t)fw * fh * 4 * f;
    uint8_t *own = NULL;
    int pw = fw, ph = fh;
    for (uint32_t lv = 1; lv < levels; lv++) {
      int nw = pw > 1 ? pw / 2 : 1, nh = ph > 1 ? ph / 2 : 1;
      uint8_t *next = malloc((size_t)nw * nh * 4);
      T3_CHECK_ALLOC(next);
      for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) for (int c = 0; c < 4; c++) {
        int x0 = x * 2 < pw ? x * 2 : pw - 1, x1 = x * 2 + 1 < pw ? x * 2 + 1 : pw - 1;
        int y0 = y * 2 < ph ? y * 2 : ph - 1, y1 = y * 2 + 1 < ph ? y * 2 + 1 : ph - 1;
        uint8_t s[4] = { prev[(y0 * pw + x0) * 4 + c], prev[(y0 * pw + x1) * 4 + c], prev[(y1 * pw + x0) * 4 + c], prev[(y1 * pw + x1) * 4 + c] };
        if (srgb && c < 3) next[(y * nw + x) * 4 + c] = srgb_enc((srgb_dec(s[0]) + srgb_dec(s[1]) + srgb_dec(s[2]) + srgb_dec(s[3])) * 0.25f);
        else next[(y * nw + x) * 4 + c] = (uint8_t)((s[0] + s[1] + s[2] + s[3] + 2) / 4);
      }
      WGPUTexelCopyTextureInfo dst = { .texture = tex, .mipLevel = lv, .origin = { 0, 0, (uint32_t)f }, .aspect = WGPUTextureAspect_All };
      WGPUTexelCopyBufferLayout lay = { .bytesPerRow = (uint32_t)nw * 4, .rowsPerImage = (uint32_t)nh };
      WGPUExtent3D ext = { (uint32_t)nw, (uint32_t)nh, 1 };
      wgpuQueueWriteTexture(w->q, &dst, next, (size_t)nw * nh * 4, &lay, &ext);
      free(own);
      own = next; prev = next; pw = nw; ph = nh;
    }
    free(own);
  }
}

struct dmap { WGPUTexture t; WGPUTextureView sample, faces[6]; int w, h; bool cube; uint32_t version; };
struct tex { WGPUTexture t; WGPUTextureView v, v0; uint32_t version; int w, h; WGPUTextureFormat fmt; uint32_t levels;
             bool borrowed_t, borrowed_v; /* t3_texture_adopt_wgpu: the embedder's texture / view, not released here */ };
WGPUTextureView t3_wgpu_texture(t3_wgpu *w, t3_texture *t) {
  struct tex *x = t->_wgpu;
  if (x && x->version == t->version) return x->v;
  if (!t->pixels) return x ? x->v : NULL;
  bool srgb = t->color_space == T3_SRGB_COLOR_SPACE;
  bool half = t->type == T3_HALF_FLOAT_TYPE, flt = t->type == T3_FLOAT_TYPE;
  WGPUTextureFormat fmt = flt ? WGPUTextureFormat_RGBA32Float : half ? WGPUTextureFormat_RGBA16Float
                        : srgb ? WGPUTextureFormat_RGBA8UnormSrgb : WGPUTextureFormat_RGBA8Unorm;
  /* Textures.needsMipmaps / getMipLevels: generateMipmaps alone decides */
  uint32_t levels = 1;
  if (t->generate_mipmaps) for (int m = t->width > t->height ? t->width : t->height; m > 1; m >>= 1) levels++;
  uint32_t layers = t->is_cube ? 6 : 1;
  /* a cube's mip chain is made on the CPU (a 2 x 2 box, in linear for sRGB):
   * compatibility mode samples a cube only as a whole cube, so the GPU pass
   * that reads one face per level cannot run there; 8-bit cubes only */
  if (t->is_cube && (flt || half)) levels = 1;
  if (!x || x->w != t->width || x->h != t->height || x->fmt != fmt || x->levels != levels) {
    if (!x) { x = calloc(1, sizeof *x); T3_CHECK_ALLOC(x); t->_wgpu = x; }
    else { wgpuTextureViewRelease(x->v); wgpuTextureRelease(x->t); }
    /* (compatibility mode: a texture is bound with one view dimension, named at creation) */
    WGPUTextureBindingViewDimension bvd = { .chain = { .sType = WGPUSType_TextureBindingViewDimension },
                                            .textureBindingViewDimension = WGPUTextureViewDimension_Cube };
    WGPUTextureDescriptor d = { .nextInChain = t->is_cube ? &bvd.chain : NULL,
      .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | (levels > 1 && !t->is_cube ? WGPUTextureUsage_RenderAttachment : 0),
      .dimension = WGPUTextureDimension_2D,
      .size = { (uint32_t)t->width, (uint32_t)t->height, layers }, .format = fmt, .mipLevelCount = levels, .sampleCount = 1 };
    x->t = wgpuDeviceCreateTexture(w->dev, &d);
    WGPUTextureViewDescriptor vd = { .format = fmt, .dimension = t->is_cube ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_2D,
                                     .mipLevelCount = levels, .arrayLayerCount = layers, .aspect = WGPUTextureAspect_All };
    x->v = wgpuTextureCreateView(x->t, &vd);
    x->w = t->width; x->h = t->height; x->fmt = fmt; x->levels = levels;
  }
  size_t texel = flt ? 16 : half ? 8 : 4, row = (size_t)t->width * texel;
  /* t3 rows run top to bottom; r186 uploads a flipY texture flipped (GL
   * convention: v = 0 at the bottom), so row 0 of the GPU texture is the
   * image's bottom row then */
  uint8_t *rows = NULL;
  const uint8_t *src = t->pixels;
  if (t->flip_y) {
    rows = malloc(row * (size_t)t->height);
    T3_CHECK_ALLOC(rows);
    for (int y = 0; y < t->height; y++) memcpy(rows + row * (size_t)y, t->pixels + row * (size_t)(t->height - 1 - y), row);
    src = rows;
  }
  WGPUTexelCopyTextureInfo dst = { .texture = x->t, .mipLevel = 0, .aspect = WGPUTextureAspect_All };
  WGPUTexelCopyBufferLayout lay = { .offset = 0, .bytesPerRow = (uint32_t)row, .rowsPerImage = (uint32_t)t->height };
  WGPUExtent3D ext = { (uint32_t)t->width, (uint32_t)t->height, layers };   /* (a cube's six faces follow one another) */
  wgpuQueueWriteTexture(w->q, &dst, src, row * (size_t)t->height * layers, &lay, &ext);
  free(rows);
  if (levels > 1 && t->is_cube) cube_mips_cpu(w, x->t, src, t->width, t->height, levels, srgb);
  else if (levels > 1) generate_mipmaps_layers(w, x->t, fmt, levels, layers);
  x->version = t->version;
  return x->v;
}
/* ── render targets ─────────────────────────────────────────────────── */
struct rtx { WGPUTexture msaa_c, msaa_d, depth; WGPUTextureView msaa_cv, msaa_dv, depth_v; int w, h; };

/* WebGPUTextureUtils.getFormat for a render target's textures */
static WGPUTextureFormat rt_color_format(const t3_texture *t) {
  if (t->type == T3_FLOAT_TYPE) return WGPUTextureFormat_RGBA32Float;
  if (t->type == T3_HALF_FLOAT_TYPE) return WGPUTextureFormat_RGBA16Float;
  return t->color_space == T3_SRGB_COLOR_SPACE ? WGPUTextureFormat_RGBA8UnormSrgb : WGPUTextureFormat_RGBA8Unorm;
}
static WGPUTextureFormat rt_depth_format(const t3_render_target *rt) {
  const t3_texture *d = rt->depth_texture;
  if (!d) return rt->stencil_buffer ? WGPUTextureFormat_Depth24PlusStencil8 : WGPUTextureFormat_Depth24Plus;
  if (d->format == T3_DEPTH_STENCIL_FORMAT || d->type == T3_UNSIGNED_INT_248_TYPE) return WGPUTextureFormat_Depth24PlusStencil8;
  if (d->type == T3_FLOAT_TYPE) return WGPUTextureFormat_Depth32Float;
  if (d->type == T3_UNSIGNED_SHORT_TYPE) return WGPUTextureFormat_Depth16Unorm;
  return WGPUTextureFormat_Depth24Plus;
}
static WGPUTexture make_tex(t3_wgpu *w, int width, int height, WGPUTextureFormat fmt, uint32_t levels, uint32_t samples, WGPUTextureUsage usage) {
  WGPUTextureDescriptor d = { .usage = usage, .dimension = WGPUTextureDimension_2D, .size = { (uint32_t)width, (uint32_t)height, 1 },
                              .format = fmt, .mipLevelCount = levels, .sampleCount = samples };
  return wgpuDeviceCreateTexture(w->dev, &d);
}
static WGPUTextureView level_view(WGPUTexture t, WGPUTextureFormat fmt, uint32_t level, uint32_t count) {
  WGPUTextureViewDescriptor v = { .format = fmt, .dimension = WGPUTextureViewDimension_2D, .baseMipLevel = level, .mipLevelCount = count,
                                  .baseArrayLayer = 0, .arrayLayerCount = 1, .aspect = WGPUTextureAspect_All };
  return wgpuTextureCreateView(t, &v);
}
static void tex_drop(struct tex *x) {
  if (!x) return;
  if (x->v0) wgpuTextureViewRelease(x->v0);
  if (x->v && !x->borrowed_v) wgpuTextureViewRelease(x->v);
  if (x->t && !x->borrowed_t) wgpuTextureRelease(x->t);
  memset(x, 0, sizeof *x);
  view_epoch++;
}

/* an embedder's texture as a t3_texture's image */
void t3_wgpu_adopt(t3_texture *t, WGPUTexture tex, WGPUTextureView view, int levels, bool owns) {
  struct tex *x = t->_wgpu;
  if (x) tex_drop(x);
  else { x = calloc(1, sizeof *x); T3_CHECK_ALLOC(x); t->_wgpu = x; }
  x->t = tex;
  x->borrowed_t = !owns;
  if (view) { x->v = view; x->borrowed_v = !owns; }
  else {
    WGPUTextureViewDescriptor vd = { .format = WGPUTextureFormat_Undefined, .dimension = t->is_cube ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_2D,
                                     .mipLevelCount = (uint32_t)(levels > 0 ? levels : 1), .arrayLayerCount = t->is_cube ? 6 : 1, .aspect = WGPUTextureAspect_All };
    x->v = wgpuTextureCreateView(tex, &vd);
  }
  x->version = t->version;
  x->w = t->width; x->h = t->height; x->levels = (uint32_t)(levels > 0 ? levels : 1);
}

/* the WebGPU objects of a three.c object being freed (t3__wgpu_release) */
void t3_wgpu_release(uint32_t kind, void *thing) {
  switch (kind) {
  case T3_KIND_TEXTURE: {
    t3_texture *t = thing;
    tex_drop(t->_wgpu);
    free(t->_wgpu);
    t->_wgpu = NULL;
    break;
  }
  case T3_KIND_ATTRIBUTE: {
    t3_attribute *a = thing;
    struct vbuf *v = a->_wgpu;
    if (v && v->b) wgpuBufferRelease(v->b);
    free(v);
    a->_wgpu = NULL;
    break;
  }
  case T3_KIND_RENDER_TARGET:
    t3_wgpu_render_target_free(thing);   /* (its texture goes with the texture) */
    view_epoch++;
    break;
  case T3_KIND_GEOMETRY: {
    t3_geometry *g = thing;
    struct dmap *x = g->_wgpu;   /* the morph texture */
    if (x) {
      if (x->sample) wgpuTextureViewRelease(x->sample);
      if (x->t) wgpuTextureRelease(x->t);
      free(x);
      view_epoch++;
    }
    g->_wgpu = NULL;
    break;
  }
  default: break;
  }
}
void t3_wgpu_render_target_free(t3_render_target *rt) {
  struct rtx *x = rt->_wgpu;
  if (!x) return;
  if (x->msaa_cv) wgpuTextureViewRelease(x->msaa_cv);
  if (x->msaa_c) wgpuTextureRelease(x->msaa_c);
  if (x->msaa_dv) wgpuTextureViewRelease(x->msaa_dv);
  if (x->msaa_d) wgpuTextureRelease(x->msaa_d);
  if (x->depth_v) wgpuTextureViewRelease(x->depth_v);
  if (x->depth) wgpuTextureRelease(x->depth);
  free(x);
  rt->_wgpu = NULL;
}
void t3_wgpu_render_target(t3_wgpu *w, t3_render_target *rt, t3_wgpu_rt *out) {
  t3_texture *t = rt->texture;
  WGPUTextureFormat cf = rt_color_format(t), df = rt_depth_format(rt);
  uint32_t levels = 1;
  if (t->generate_mipmaps) for (int m = rt->width > rt->height ? rt->width : rt->height; m > 1; m >>= 1) levels++;
  int samples = rt->samples > 1 ? 4 : 1;   /* WebGPU: 1 or 4 */
  struct tex *tx = t->_wgpu;
  struct rtx *x = rt->_wgpu;
  if (!x || x->w != rt->width || x->h != rt->height || !tx || tx->fmt != cf || tx->levels != levels) {
    t3_wgpu_render_target_free(rt);
    x = calloc(1, sizeof *x);
    T3_CHECK_ALLOC(x);
    rt->_wgpu = x;
    x->w = rt->width; x->h = rt->height;
    if (!tx) { tx = calloc(1, sizeof *tx); T3_CHECK_ALLOC(tx); t->_wgpu = tx; }
    else tex_drop(tx);
    tx->t = make_tex(w, rt->width, rt->height, cf, levels, 1, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
                     WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
    tx->v = wgpuTextureCreateView(tx->t, NULL);
    tx->v0 = level_view(tx->t, cf, 0, 1);
    tx->w = rt->width; tx->h = rt->height; tx->fmt = cf; tx->levels = levels; tx->version = t->version;
    if (samples > 1) {
      x->msaa_c = make_tex(w, rt->width, rt->height, cf, 1, 4, WGPUTextureUsage_RenderAttachment);
      x->msaa_cv = wgpuTextureCreateView(x->msaa_c, NULL);
    }
    if (rt->depth_buffer) {
      t3_texture *dt = rt->depth_texture;
      if (dt && samples == 1) {
        struct tex *dx = dt->_wgpu;
        if (!dx) { dx = calloc(1, sizeof *dx); T3_CHECK_ALLOC(dx); dt->_wgpu = dx; }
        else tex_drop(dx);
        dt->width = rt->width; dt->height = rt->height;
        dx->t = make_tex(w, rt->width, rt->height, df, 1, 1, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc);
        WGPUTextureViewDescriptor dv = { .format = df, .dimension = WGPUTextureViewDimension_2D, .mipLevelCount = 1, .arrayLayerCount = 1,
                                         .aspect = WGPUTextureAspect_DepthOnly };
        dx->v = wgpuTextureCreateView(dx->t, &dv);
        dx->v0 = wgpuTextureCreateView(dx->t, NULL);
        dx->w = rt->width; dx->h = rt->height; dx->fmt = df; dx->levels = 1; dx->version = dt->version;
      } else {
        x->depth = make_tex(w, rt->width, rt->height, df, 1, (uint32_t)samples, WGPUTextureUsage_RenderAttachment);
        x->depth_v = wgpuTextureCreateView(x->depth, NULL);
      }
    }
  }
  out->samples = samples;
  out->color_format = cf;
  out->depth_format = rt->depth_buffer ? df : WGPUTextureFormat_Undefined;
  out->color = samples > 1 ? x->msaa_cv : tx->v0;
  out->resolve = samples > 1 ? tx->v0 : NULL;
  out->depth = !rt->depth_buffer ? NULL : x->depth_v ? x->depth_v : ((struct tex *)rt->depth_texture->_wgpu)->v0;
}
void t3_wgpu_render_target_done(t3_wgpu *w, t3_render_target *rt) {
  struct tex *tx = rt->texture->_wgpu;
  if (tx && tx->levels > 1) generate_mipmaps(w, tx->t, tx->fmt, tx->levels);
}

/* ── shadow maps and morph arrays ────────────────────────────────────── */
WGPUTextureView t3_wgpu_depth_map(t3_wgpu *w, void **slot, int width, int height, bool cube, WGPUTextureView *face_views) {
  struct dmap *x = *slot;
  if (!x || x->w != width || x->h != height || x->cube != cube) {
    if (x) {
      wgpuTextureViewRelease(x->sample);
      for (int f = 0; f < (x->cube ? 6 : 1); f++) wgpuTextureViewRelease(x->faces[f]);
      wgpuTextureRelease(x->t);
    } else { x = calloc(1, sizeof *x); T3_CHECK_ALLOC(x); *slot = x; }
    WGPUTextureBindingViewDimension bvd = { .chain = { .sType = WGPUSType_TextureBindingViewDimension },
                                            .textureBindingViewDimension = WGPUTextureViewDimension_Cube };
    WGPUTextureDescriptor d = { .nextInChain = cube ? &bvd.chain : NULL, .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
      .dimension = WGPUTextureDimension_2D,
      .size = { (uint32_t)width, (uint32_t)height, cube ? 6u : 1u }, .format = WGPUTextureFormat_Depth24Plus, .mipLevelCount = 1, .sampleCount = 1 };
    x->t = wgpuDeviceCreateTexture(w->dev, &d);
    WGPUTextureViewDescriptor sv = { .format = WGPUTextureFormat_Depth24Plus, .dimension = cube ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_2D,
      .mipLevelCount = 1, .arrayLayerCount = cube ? 6 : 1, .aspect = WGPUTextureAspect_DepthOnly };
    x->sample = wgpuTextureCreateView(x->t, &sv);
    for (int f = 0; f < (cube ? 6 : 1); f++) {
      WGPUTextureViewDescriptor fv = { .format = WGPUTextureFormat_Depth24Plus, .dimension = WGPUTextureViewDimension_2D, .mipLevelCount = 1,
        .baseArrayLayer = (uint32_t)f, .arrayLayerCount = 1, .aspect = WGPUTextureAspect_All };
      x->faces[f] = wgpuTextureCreateView(x->t, &fv);
    }
    x->w = width; x->h = height; x->cube = cube;
  }
  if (face_views) for (int f = 0; f < (cube ? 6 : 1); f++) face_views[f] = x->faces[f];
  return x->sample;
}
WGPUSampler t3_wgpu_compare_sampler(t3_wgpu *w, bool linear) {
  uint32_t key = 0x80000000u | (uint32_t)linear;
  for (int i = 0; i < w->n_samplers; i++) if (w->samplers[i].key == key) return w->samplers[i].s;
  WGPUFilterMode fm = linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
  WGPUSamplerDescriptor d = { .addressModeU = WGPUAddressMode_ClampToEdge, .addressModeV = WGPUAddressMode_ClampToEdge, .addressModeW = WGPUAddressMode_ClampToEdge,
    .magFilter = fm, .minFilter = fm, .mipmapFilter = WGPUMipmapFilterMode_Nearest, .lodMinClamp = 0, .lodMaxClamp = 32,
    .compare = WGPUCompareFunction_LessEqual, .maxAnisotropy = 1 };
  WGPUSampler s = wgpuDeviceCreateSampler(w->dev, &d);
  if (w->n_samplers < 64) w->samplers[w->n_samplers++] = (struct sampler_entry){ key, s };
  return s;
}
WGPUTextureView t3_wgpu_array_current(void *slot, uint32_t version) {
  struct dmap *x = slot;
  return x && x->version == version ? x->sample : NULL;
}
WGPUTextureView t3_wgpu_array_texture(t3_wgpu *w, void **slot, uint32_t version, int width, int height, int layers, const float *data) {
  struct dmap *x = *slot;
  if (x && x->version == version && x->w == width && x->h == height) return x->sample;
  if (x) { wgpuTextureViewRelease(x->sample); wgpuTextureRelease(x->t); }
  else { x = calloc(1, sizeof *x); T3_CHECK_ALLOC(x); *slot = x; }
  WGPUTextureDescriptor d = { .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, .dimension = WGPUTextureDimension_2D,
    .size = { (uint32_t)width, (uint32_t)height, (uint32_t)layers }, .format = WGPUTextureFormat_RGBA32Float, .mipLevelCount = 1, .sampleCount = 1 };
  x->t = wgpuDeviceCreateTexture(w->dev, &d);
  WGPUTextureViewDescriptor v = { .format = WGPUTextureFormat_RGBA32Float, .dimension = WGPUTextureViewDimension_2DArray,
    .mipLevelCount = 1, .arrayLayerCount = (uint32_t)layers, .aspect = WGPUTextureAspect_All };
  x->sample = wgpuTextureCreateView(x->t, &v);
  WGPUTexelCopyTextureInfo dst = { .texture = x->t, .aspect = WGPUTextureAspect_All };
  WGPUTexelCopyBufferLayout lay = { .bytesPerRow = (uint32_t)width * 16, .rowsPerImage = (uint32_t)height };
  WGPUExtent3D ext = { (uint32_t)width, (uint32_t)height, (uint32_t)layers };
  wgpuQueueWriteTexture(w->q, &dst, data, (size_t)width * height * layers * 16, &lay, &ext);
  x->w = width; x->h = height; x->version = version;
  return x->sample;
}

WGPUTextureView t3_wgpu_data_texture(t3_wgpu *w, void **slot, int width, int height, WGPUTextureFormat fmt, const void *data, uint32_t bpr) {
  struct tex *x = *slot;
  if (x) return x->v;
  x = calloc(1, sizeof *x);
  T3_CHECK_ALLOC(x);
  *slot = x;
  WGPUTextureDescriptor d = { .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, .dimension = WGPUTextureDimension_2D,
    .size = { (uint32_t)width, (uint32_t)height, 1 }, .format = fmt, .mipLevelCount = 1, .sampleCount = 1 };
  x->t = wgpuDeviceCreateTexture(w->dev, &d);
  x->v = wgpuTextureCreateView(x->t, NULL);
  WGPUTexelCopyTextureInfo dst = { .texture = x->t, .aspect = WGPUTextureAspect_All };
  WGPUTexelCopyBufferLayout lay = { .bytesPerRow = bpr, .rowsPerImage = (uint32_t)height };
  WGPUExtent3D ext = { (uint32_t)width, (uint32_t)height, 1 };
  wgpuQueueWriteTexture(w->q, &dst, data, (size_t)bpr * (size_t)height, &lay, &ext);
  return x->v;
}
WGPUSampler t3_wgpu_sampler(t3_wgpu *w, const t3_texture *t, bool cmp) {
  int ws = t ? t->wrap_s : T3_CLAMP_TO_EDGE, wt = t ? t->wrap_t : T3_CLAMP_TO_EDGE;
  int mag = t ? t->mag_filter : T3_LINEAR, min = t ? t->min_filter : T3_LINEAR;
  uint32_t key = (uint32_t)(ws - 1000) | (uint32_t)(wt - 1000) << 2 | (uint32_t)(mag - 1003) << 4 | (uint32_t)(min - 1003) << 7 | (uint32_t)cmp << 10;
  for (int i = 0; i < w->n_samplers; i++) if (w->samplers[i].key == key) return w->samplers[i].s;
  static const WGPUAddressMode am[3] = { WGPUAddressMode_Repeat, WGPUAddressMode_ClampToEdge, WGPUAddressMode_MirrorRepeat };
  bool mag_lin = mag != T3_NEAREST, min_lin = min == T3_LINEAR || min == T3_LINEAR_MIPMAP_NEAREST || min == T3_LINEAR_MIPMAP_LINEAR;
  bool mip_lin = min == T3_NEAREST_MIPMAP_LINEAR || min == T3_LINEAR_MIPMAP_LINEAR;
  WGPUSamplerDescriptor d = { .addressModeU = am[(ws - 1000) % 3], .addressModeV = am[(wt - 1000) % 3], .addressModeW = WGPUAddressMode_ClampToEdge,
    .magFilter = mag_lin ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest, .minFilter = min_lin ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest,
    .mipmapFilter = mip_lin ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest, .lodMinClamp = 0, .lodMaxClamp = 32,
    .compare = cmp ? WGPUCompareFunction_LessEqual : WGPUCompareFunction_Undefined, .maxAnisotropy = 1 };
  WGPUSampler s = wgpuDeviceCreateSampler(w->dev, &d);
  if (w->n_samplers < 64) w->samplers[w->n_samplers++] = (struct sampler_entry){ key, s };
  return s;
}
/* r186 ViewportTextureNode (transmission): a mipmapped copy of src, a view of
 * the colour attachment just drawn into (no pass open), at its format.
 * Level 0 is a same-size blit with the mipmap pipeline, then the chain. */
struct vpcopy { WGPUTexture t; WGPUTextureView v; int w, h; WGPUTextureFormat fmt; uint32_t levels; };
WGPUTextureView t3_wgpu_viewport_copy(t3_wgpu *w, void **slot, WGPUTextureView src, WGPUTextureFormat fmt, int width, int height, bool flip) {
  struct vpcopy *x = *slot;
  if (!x || x->w != width || x->h != height || x->fmt != fmt) {
    if (x) { wgpuTextureViewRelease(x->v); wgpuTextureRelease(x->t); view_epoch++; }
    else { x = calloc(1, sizeof *x); T3_CHECK_ALLOC(x); *slot = x; }
    uint32_t levels = 1;
    for (int m = width > height ? width : height; m > 1; m >>= 1) levels++;
    WGPUTextureDescriptor d = { .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment, .dimension = WGPUTextureDimension_2D,
      .size = { (uint32_t)width, (uint32_t)height, 1 }, .format = fmt, .mipLevelCount = levels, .sampleCount = 1 };
    x->t = wgpuDeviceCreateTexture(w->dev, &d);
    x->v = wgpuTextureCreateView(x->t, NULL);
    x->w = width; x->h = height; x->fmt = fmt; x->levels = levels;
  }
  WGPURenderPipeline p = mip_pipeline(w, fmt);
  WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(p, 0);
  WGPUTextureViewDescriptor dv = { .format = fmt, .dimension = WGPUTextureViewDimension_2D, .baseMipLevel = 0, .mipLevelCount = 1,
                                   .baseArrayLayer = 0, .arrayLayerCount = 1, .aspect = WGPUTextureAspect_All };
  WGPUTextureView dst = wgpuTextureCreateView(x->t, &dv);
  WGPUBindGroupEntry e[3] = { { .binding = 0, .sampler = w->mip_sampler }, { .binding = 1, .textureView = src },
                              { .binding = 2, .buffer = flip ? w->mip_flip : w->mip_noflip, .size = 4 } };
  WGPUBindGroupDescriptor bd = { .layout = bgl, .entryCount = 3, .entries = e };
  WGPUBindGroup bg = wgpuDeviceCreateBindGroup(w->dev, &bd);
  WGPURenderPassColorAttachment ca = { .view = dst, .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED, .loadOp = WGPULoadOp_Clear, .storeOp = WGPUStoreOp_Store };
  WGPURenderPassDescriptor pd = { .colorAttachmentCount = 1, .colorAttachments = &ca };
  WGPURenderPassEncoder pe = wgpuCommandEncoderBeginRenderPass(w->enc, &pd);
  wgpuRenderPassEncoderSetPipeline(pe, p);
  wgpuRenderPassEncoderSetBindGroup(pe, 0, bg, 0, NULL);
  wgpuRenderPassEncoderDraw(pe, 3, 1, 0, 0);
  wgpuRenderPassEncoderEnd(pe);
  wgpuRenderPassEncoderRelease(pe);
  wgpuBindGroupRelease(bg);
  wgpuTextureViewRelease(dst);
  wgpuBindGroupLayoutRelease(bgl);
  if (x->levels > 1) generate_mipmaps(w, x->t, fmt, x->levels);
  return x->v;
}
WGPUTextureView t3_wgpu_target(t3_wgpu *w, void **slot, int width, int height, WGPUTextureFormat fmt, bool sampled) {
  struct target *x = *slot;
  if (x && x->w == width && x->h == height && x->fmt == fmt) return x->view;
  if (!x) { x = calloc(1, sizeof *x); T3_CHECK_ALLOC(x); *slot = x; }
  else { wgpuTextureViewRelease(x->view); wgpuTextureRelease(x->tex); }
  WGPUTextureDescriptor d = { .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | (sampled ? WGPUTextureUsage_TextureBinding : 0),
    .dimension = WGPUTextureDimension_2D, .size = { (uint32_t)width, (uint32_t)height, 1 }, .format = fmt, .mipLevelCount = 1, .sampleCount = 1 };
  x->tex = wgpuDeviceCreateTexture(w->dev, &d);
  x->view = wgpuTextureCreateView(x->tex, NULL);
  x->w = width; x->h = height; x->fmt = fmt;
  return x->view;
}

static void map_done(WGPUMapAsyncStatus status, WGPUStringView msg, void *u1, void *u2) {
  (void)msg; (void)u2;
  *(int *)u1 = status == WGPUMapAsyncStatus_Success ? 1 : -1;
}
bool t3_wgpu_read_rgba8(t3_wgpu *w, void *slot, int width, int height, uint8_t *out) {
  struct target *x = slot;
  if (!x) return false;
#if defined(__EMSCRIPTEN__) || defined(T3_WASMCART)
  (void)width; (void)height; (void)out;
  return false;   /* (a cart cannot block on mapAsync) */
#else
  t3_wgpu_flush(w);
  uint32_t bpr = ((uint32_t)width * 4 + 255) & ~255u;
  WGPUBufferDescriptor bd = { .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, .size = (uint64_t)bpr * (uint64_t)height };
  WGPUBuffer buf = wgpuDeviceCreateBuffer(w->dev, &bd);
  WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(w->dev, NULL);
  WGPUTexelCopyTextureInfo src = { .texture = x->tex, .aspect = WGPUTextureAspect_All };
  WGPUTexelCopyBufferInfo dst = { .layout = { .bytesPerRow = bpr, .rowsPerImage = (uint32_t)height }, .buffer = buf };
  WGPUExtent3D ext = { (uint32_t)width, (uint32_t)height, 1 };
  wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
  WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, NULL);
  wgpuQueueSubmit(w->q, 1, &cb);
  wgpuCommandBufferRelease(cb);
  wgpuCommandEncoderRelease(enc);
  int done = 0;
  WGPUBufferMapCallbackInfo ci = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = map_done, .userdata1 = &done };
  wgpuBufferMapAsync(buf, WGPUMapMode_Read, 0, (size_t)bpr * (size_t)height, ci);
  WGPUInstance inst = wgpuAdapterGetInstance(wgpuDeviceGetAdapter(w->dev));
  while (!done) wgpuInstanceProcessEvents(inst);
  if (done > 0) {
    const uint8_t *p = wgpuBufferGetConstMappedRange(buf, 0, (size_t)bpr * (size_t)height);
    for (int y = 0; y < height; y++) memcpy(out + (size_t)y * (size_t)width * 4, p + (size_t)y * bpr, (size_t)width * 4);
    wgpuBufferUnmap(buf);
  }
  wgpuBufferRelease(buf);
  return done > 0;
#endif
}
#endif
