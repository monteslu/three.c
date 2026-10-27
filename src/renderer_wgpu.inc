/* The WebGPU frame (included by renderer.c after renderer_gen.inc; built
 * with T3_WGPU). The CPU side of a render is the one the GLES path runs
 * (scene walk, lists, sorting, batching, lights, the generated programs'
 * fills); here the draws are recorded on a WebGPU pass into r186's linear
 * RGBA16F target, and r186's own output pass writes the output texture. */
#ifdef T3_WGPU

static WGPUTextureView wgpu_dfg(t3_renderer *r) {
  return t3_wgpu_data_texture(r->wgpu, &r->wg_dfg, T3_GEN_DFG_LUT_SIZE, T3_GEN_DFG_LUT_SIZE, WGPUTextureFormat_RG16Float, t3_gen_dfg_lut, T3_GEN_DFG_LUT_SIZE * 4);
}

/* bind every group of gl for this draw: stream its uniform struct and extra
 * buffers, bind its textures; dynamic offsets in binding order */
static t3_texture *vp_sampler(t3_renderer *r) {
  if (!r->vp_sampler) {
    r->vp_sampler = t3_texture_new(1, 1, NULL);
    r->vp_sampler->mag_filter = T3_NEAREST;
    r->vp_sampler->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  }
  return r->vp_sampler;
}
static bool wgpu_bind_groups(t3_renderer *r, t3_gen_gl *gl, const t3_material *m, const t3_object *o, const t3_geometry *g,
                             WGPUTextureView output_view) {
  t3_wgpu *w = r->wgpu;
  for (int gi = 0; gi < gl->n_groups; gi++) {
    const t3_bk_group_layout *L = gl->src->groups[gi];
    struct dynoff { uint32_t binding, off; } dyn[8];
    int nd = 0;
    if (gl->bytes[gi]) {
      /* a block no op wrote since it was streamed (the frame's camera and
       * lights, between draws) keeps its offset: the bind group set is skipped */
      dyn[nd++] = (struct dynoff){ L->uniform_binding, t3_wgpu_stream_keep(w, &gl->wtok[gi], &gl->woff[gi], gl->mirror[gi], gl->bytes[gi], gl->dirty[gi]) };
      gl->dirty[gi] = false;
    }
    for (int bi = 0; bi < gl->n_buffers; bi++) {
      if ((int)gl->buf[bi].point != gi) continue;
      const void *data = NULL;
      float tmp[4 * 256];
      if (gl->buf[bi].kind == T3_SRC_BONES && o && o->type == T3_SKINNED_MESH) data = ((const t3_skinned_mesh *)o)->skeleton->bone_matrices;
      else {
        /* (morph influences, or zeros for a buffer with no source here) */
        uint32_t bytes = gl->buf[bi].bytes < sizeof tmp ? gl->buf[bi].bytes : (uint32_t)sizeof tmp;
        memset(tmp, 0, bytes);
        if (gl->buf[bi].kind == T3_SRC_MORPH_INFLUENCES && o) {
          const t3_mesh *mesh = (const t3_mesh *)o;
          for (int k = 0; k < g->morph_count && k < 256; k++) tmp[k * 4] = mesh->morph_influences ? mesh->morph_influences[k] : 0;
        }
        data = tmp;
      }
      dyn[nd++] = (struct dynoff){ gl->buf[bi].ubo, t3_wgpu_stream(w, data, gl->buf[bi].bytes) };
    }
    for (int a = 1; a < nd; a++) for (int b = a; b > 0 && dyn[b - 1].binding > dyn[b].binding; b--) { struct dynoff t = dyn[b]; dyn[b] = dyn[b - 1]; dyn[b - 1] = t; }
    WGPUTextureView views[T3_GEN_MAX_TEXTURES];   /* (the first nt are set: what the bind group reads and compares) */
    WGPUSampler samplers[T3_GEN_MAX_TEXTURES];
    int nt = 0;
    for (uint32_t t = 0; t < L->n_textures && t < T3_GEN_MAX_TEXTURES; t++) {
      uint8_t kind = gl->wtex_kind[gi][t], arg = gl->wtex_arg[gi][t];
      switch (kind) {
      case T3_SRC_DFG: views[nt] = wgpu_dfg(r); samplers[nt] = t3_wgpu_sampler(w, NULL, false); break;
      case T3_SRC_OUTPUT: views[nt] = output_view; samplers[nt] = t3_wgpu_sampler(w, NULL, false); break;
      case T3_SRC_SHADOW_DIR: case T3_SRC_SHADOW_SPOT: case T3_SRC_SHADOW_POINT: case T3_SRC_SHADOW_NONE: {
        const t3_light *l = gen_shadow_light(r, kind, arg);
        struct gen_shadow *sh = l ? gen_shadow_of(r, l, false) : NULL;
        if (!sh || !sh->wg) return false;
        views[nt] = t3_wgpu_depth_map(w, &sh->wg, sh->w, sh->h, sh->cube, NULL);
        samplers[nt] = t3_wgpu_compare_sampler(w, r->shadow_map_type != T3_BASIC_SHADOW_MAP);
        break;
      }
      case T3_SRC_ENV: case T3_SRC_BG: case T3_SRC_PMREM: {
        const t3_texture *tx = kind == T3_SRC_BG ? r->aux.bg : kind == T3_SRC_PMREM ? r->aux.pm_src : r->aux.env;
        if (!tx) return false;
        views[nt] = t3_wgpu_texture(w, (t3_texture *)tx);
        samplers[nt] = t3_wgpu_sampler(w, tx, false);
        if (!views[nt]) return false;
        break;
      }
      case T3_SRC_VIEWPORT:   /* transmission's framebuffer copy, this side's */
        views[nt] = (WGPUTextureView)r->wg_vp_view[m && m->side == T3_BACK_SIDE];
        samplers[nt] = t3_wgpu_sampler(w, vp_sampler(r), false);
        if (!views[nt]) return false;
        break;
      case T3_SRC_MORPH: {
        if (!g || !g->morph_count) return false;
        void **slot = &((t3_geometry *)g)->_wgpu;
        views[nt] = t3_wgpu_array_current(*slot, g->version);
        if (!views[nt]) {
          int mw, mh;
          float *data = gen_morph_data(g, &mw, &mh);
          views[nt] = t3_wgpu_array_texture(w, slot, g->version, mw, mh, g->morph_count, data);
          free(data);
        }
        samplers[nt] = NULL;
        break;
      }
      case T3_SRC_EXT: {   /* (gen_fill_ext has checked it is set) */
        t3_texture *tx = gl->ext_tex[arg].tex;
        views[nt] = t3_wgpu_texture(w, tx);
        /* a DepthTexture the extension samples with compare (r186's
         * texture(depth).compare(ref), a comparison sampler in the layout):
         * a LessEqual comparison sampler, linear unless the texture is nearest */
        samplers[nt] = L->textures[t].compare_sampler ? t3_wgpu_compare_sampler(w, tx->mag_filter != T3_NEAREST)
                                                      : t3_wgpu_sampler(w, tx, false);
        if (!views[nt]) return false;
        break;
      }
      case T3_SRC_MAT: {
        const t3_texture *tx = m ? gen_tex(m, arg) : NULL;
        if (!tx) return false;
        views[nt] = t3_wgpu_texture(w, (t3_texture *)tx);
        samplers[nt] = t3_wgpu_sampler(w, tx, false);
        if (!views[nt]) return false;
        break;
      }
      default: return false;
      }
      nt++;
    }
    WGPUBindGroup bg = t3_wgpu_bind_group(w, gl, gi, views, samplers);
    uint32_t offs[8];
    for (int k = 0; k < nd; k++) offs[k] = dyn[k].off;
    t3_wgpu_set_bind_group(w, gi, bg, nd, offs);
  }
  return true;
}

/* one draw with program gl; state: the material whose render state (side,
 * depth, blending) applies (a shadow caster's is a copy with r186's shadow
 * side) */
/* transmission's framebuffer copy on WebGPU: the pass ends, the colour
 * attachment drawn so far is copied (with its mipmaps), and the pass goes on
 * with LOAD, as r186 WebGPUBackend.copyFramebufferToTexture */
static void wgpu_viewport_copy(t3_renderer *r, int which) {
  if (r->vp_done[which]) return;
  r->vp_done[which] = true;
  if (!r->wg_cur_src) { gen_note(r, "transmission: no colour attachment to copy (a depth-only target)"); return; }
  t3_wgpu *w = r->wgpu;
  t3_wgpu_end_pass(w);
  r->wg_vp_view[which] = t3_wgpu_viewport_copy(w, &r->wg_vp[which], (WGPUTextureView)r->wg_cur_src, (WGPUTextureFormat)r->wg_cur_src_format,
                                               r->wg_cur_w, r->wg_cur_h, r->wg_flip);
  t3_wgpu_pass P = r->wg_cur_pass;
  P.load_color = P.load_depth = true;
  t3_wgpu_begin_pass(w, &P);
}

static void wgpu_draw_prog(t3_renderer *r, const render_item *it, const t3_camera *cam, t3_gen_gl *gl, const t3_material *state) {
  if (gl->src->features & T3_GEN_TRANSM) wgpu_viewport_copy(r, state->side == T3_BACK_SIDE);
  t3_object *o = it->object;
  const t3_material *m = it->material;
  t3_geometry *g = it->geometry;
  bool batched = it->batch_count > 0;
  if (gl->render_stamp != r->stamp) { gen_fill_frame(r, gl, cam); gl->render_stamp = r->stamp; }
  gen_set_aux(r, m);
  gen_fill_draw(r, gl, m, o, batched);
  if (!gen_fill_ext(r, gl, m, o)) { r->info.skipped++; return; }
  t3_wgpu *w = r->wgpu;
  WGPURenderPassEncoder pass = t3_wgpu_pass_encoder(w);
  /* the pipeline state */
  t3_wgpu_state st;
  memset(&st, 0, sizeof st);
  st.topology = o->type == T3_LINE_SEGMENTS ? 1 : o->type == T3_LINE ? 2 : o->type == T3_POINTS ? 3 : 0;
  st.cull = state->side == T3_DOUBLE_SIDE ? 0 : state->side == T3_BACK_SIDE ? 2 : 1;
  st.depth_test = state->depth_test; st.depth_write = state->depth_write; st.depth_func = (uint8_t)state->depth_func;
  st.blend = state->transparent && state->blending != T3_NO_BLENDING ? (uint8_t)state->blending : 0;
  st.color_write = state->color_write;
  st.instance = batched ? 2 : o->type == T3_INSTANCED_MESH ? 1 : 0;
  st.color = (WGPUTextureFormat)r->wg_pass_format;
  st.depth = (WGPUTextureFormat)r->wg_depth_format;
  st.samples = (uint8_t)r->wg_samples;
  if (!st.depth) st.depth_test = st.depth_write = 0;
  /* vertex buffers in the manifest's attribute order */
  WGPUBuffer vbs[16]; uint64_t voffs[16];
  int nvb = 0, ns = 0;
  for (int a = 0; a < gl->src->n_attributes && a < 16; a++) {
    const t3_gen_attribute *A = &gl->src->attributes[a];
    int slot = gl->attr_slot[a];
    if (slot == T3_ATTR_IN_INSTANCE_DONE) continue;
    if (slot == T3_ATTR_IN_INSTANCE) {
      if (batched) { vbs[nvb] = t3_wgpu_floats(w, &r->wg_inst, NULL, 0); voffs[nvb++] = (uint64_t)it->batch_offset * INST_FLOATS * 4; }
      else if (o->type == T3_INSTANCED_MESH) { vbs[nvb] = t3_wgpu_attribute(w, ((t3_instanced_mesh *)o)->instance_matrix, false, NULL); voffs[nvb++] = 0; }
      else return;
      continue;
    }
    if (slot == T3_ATTR_IN_INSTANCE_COLOR) {
      if (o->type != T3_INSTANCED_MESH || !((t3_instanced_mesh *)o)->instance_color) return;
      vbs[nvb] = t3_wgpu_attribute(w, ((t3_instanced_mesh *)o)->instance_color, false, NULL);
      voffs[nvb++] = 0;
      continue;
    }
    t3_attribute *ga = slot >= 0 ? g->attributes[slot] : NULL;
    if (!ga) return;   /* r186 would warn and draw garbage; three.c skips */
    if (ns < 12) st.strides[ns++] = (uint8_t)ga->item_size;
    vbs[nvb] = A->type[0] == 'u' ? t3_wgpu_attribute_uint(w, ga) : t3_wgpu_attribute(w, ga, false, NULL);
    voffs[nvb++] = 0;
  }
  st.flip_y = r->wg_flip;
  WGPURenderPipeline p = t3_wgpu_pipeline(w, gl, &st);
  if (!p) { gen_note(r, "flip_y: a vertex program without the r186 clip-position epilogue"); return; }
  t3_wgpu_set_pipeline(w, p);
  if (!wgpu_bind_groups(r, gl, m, o, g, NULL)) return;
  for (int k = 0; k < nvb; k++) t3_wgpu_set_vertex_buffer(w, k, vbs[k], voffs[k]);
  /* draw range x group range, as issue_draw */
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
  int instances = batched ? it->batch_count : o->type == T3_INSTANCED_MESH ? ((t3_instanced_mesh *)o)->count : 1;
  if (g->index) {
    uint32_t fmt;
    WGPUBuffer ib = t3_wgpu_attribute(w, g->index, true, &fmt);
    t3_wgpu_set_index_buffer(w, ib, fmt);
    wgpuRenderPassEncoderDrawIndexed(pass, (uint32_t)count, (uint32_t)instances, (uint32_t)start, 0, 0);
  } else {
    wgpuRenderPassEncoderDraw(pass, (uint32_t)count, (uint32_t)instances, (uint32_t)start, 0);
  }
  r->info.calls++;
  if (st.topology == 0) r->info.triangles += (unsigned)(count / 3 * instances);
}
static void wgpu_draw_item(t3_renderer *r, const render_item *it, const t3_camera *cam) {
  t3_gen_gl *gl = gen_pick(r, it->material, it->object, it->geometry, it->batch_count > 0);
  if (gl) wgpu_draw_prog(r, it, cam, gl, it->material);
}

/* r186's shadow pass on WebGPU (ShadowNode / PointShadowNode with the
 * WebGPU coordinate system): a Depth24Plus map per casting light, cleared
 * and drawn with the caster programs from the light's camera; a point
 * light's six faces use WebGPU's cube directions */
static void wgpu_shadow_maps(t3_renderer *r, t3_scene *scene) {
  if (!r->shadow_map_enabled || !r->caster_count) return;
  t3_wgpu *w = r->wgpu;
  r->rendering_shadows = true;
  int pf = r->wg_pass_format, df = r->wg_depth_format, ns = r->wg_samples;
  bool flip = r->wg_flip;
  r->wg_pass_format = 0; r->wg_depth_format = WGPUTextureFormat_Depth24Plus; r->wg_samples = 1; r->wg_flip = false;
  for (int i = 0; i < r->caster_count; i++) {
    t3_light *l = r->casters[i];
    t3_light_shadow *sh = l->shadow;
    struct gen_shadow *s = gen_shadow_of(r, l, true);
    if (!s) continue;
    bool point = l->base.type == T3_POINT_LIGHT;
    int mw = sh->map_width, mh = point ? sh->map_width : sh->map_height;
    if (shadow_keep(r, s, mw, mh, point)) continue;
    s->rendered = true;
    WGPUTextureView faces[6];
    t3_wgpu_depth_map(w, &s->wg, mw, mh, point, faces);
    s->w = mw; s->h = mh; s->cube = point;
    for (int face = 0; face < (point ? 6 : 1); face++) {
      if (point) {
        static const float dirs[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
        static const float ups[6][3] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { 0, -1, 0 }, { 0, -1, 0 } };
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
        /* (WebGPU's shadow matrix keeps z from a [0,1] projection; with the
         * GL projection and GL bias the product is the same matrix) */
        shadow_update_matrices(r, l);
      }
      t3_wgpu_pass P = { .depth = faces[face], .vw = mw, .vh = mh };
      t3_wgpu_begin_pass(w, &P);
      for (int k = 0; k < r->gen_n; k++) if (r->gen[k].gl) r->gen[k].gl->render_stamp = 0;   /* frame data = this light's camera */
      gen_casters(r, &scene->base, l->shadow->camera);
      t3_wgpu_end_pass(w);
    }
  }
  for (int k = 0; k < r->gen_n; k++) if (r->gen[k].gl) r->gen[k].gl->render_stamp = 0;   /* the main camera next */
  r->wg_pass_format = pf; r->wg_depth_format = df; r->wg_samples = ns; r->wg_flip = flip;
  r->rendering_shadows = false;
}

/* r186's output pass on WebGPU: its generated output program over the
 * linear target, into the output texture */
static void wgpu_output_pass(t3_renderer *r, WGPUTextureView scene_view, WGPUTextureView out_view) {
  static const char *const tm[] = { NULL, "tmlinear", "tmreinhard", "tmcineon", "tmaces", NULL, "tmagx", "tmneutral" };
  char name[64] = "output";
  if (r->tone_mapping > 0 && r->tone_mapping < 8 && tm[r->tone_mapping]) {
    strcat(name, "+"); strcat(name, tm[r->tone_mapping]);
    if (r->output_color_space != T3_SRGB_COLOR_SPACE) strcat(name, "+lin");
  }
  const t3_gen_program *src = gen_by_name(r, name);
  t3_gen_gl *gl = src ? gen_program_enc(r, src, false) : NULL;
  if (!gl || !gl->complete) { gen_note(r, "no output pass program"); return; }
  t3_wgpu *w = r->wgpu;
  t3_wgpu_pass P = { .color = out_view, .load_color = true, .vx = r->view_x, .vy = r->view_y, .vw = r->view_w, .vh = r->view_h,
                     .scissor = r->scissor_test, .sx = r->sc_x, .sy = r->sc_y, .sw = r->sc_w, .sh = r->sc_h };
  t3_wgpu_begin_pass(w, &P);
  gen_fill_frame(r, gl, NULL);
  for (int k = gl->n_frame_ops; k < gl->n_ops; k++)
    if (gl->ops[k].op == GOP_TEX_FLIPY) { uint32_t zero = 0; memcpy(t3_gen_gl_op(gl, &gl->ops[k]), &zero, 4); }   /* WebGPU: no flip */
  t3_wgpu_state st;
  memset(&st, 0, sizeof st);
  st.color = r->wg_ext_out ? r->wg_ext_out_format : r->wg_out_format;
  st.color_write = 1;
  st.strides[0] = 3;
  WGPURenderPipeline p = t3_wgpu_pipeline(w, gl, &st);
  t3_wgpu_set_pipeline(w, p);
  wgpu_bind_groups(r, gl, NULL, NULL, NULL, scene_view);
  static const float tri[9] = { -1, 3, 0, -1, -1, 0, 3, -1, 0 };   /* QuadMesh's triangle */
  WGPUBuffer vb = t3_wgpu_floats(w, &r->wg_quad, tri, 9);
  t3_wgpu_set_vertex_buffer(w, 0, vb, 0);
  wgpuRenderPassEncoderDraw(t3_wgpu_pass_encoder(w), 3, 1, 0, 0);
  t3_wgpu_end_pass(w);
}

/* the attachments a render (or clear) draws into: the render target's, or
 * the canvas side's (r186's linear RGBA16F target, or the output texture
 * itself when the output needs no conversion) */
typedef struct { WGPUTextureView color, resolve, depth, out; bool direct, ext; } wgpu_dest;
static bool depth_has_stencil(int f) { return f == WGPUTextureFormat_Depth24PlusStencil8 || f == WGPUTextureFormat_Depth32FloatStencil8 || f == WGPUTextureFormat_Stencil8; }
static void wgpu_destination(t3_renderer *r, wgpu_dest *d) {
  t3_wgpu *w = r->wgpu;
  memset(d, 0, sizeof *d);
  r->wg_flip = false;
  if (!r->target && r->has_ext_target) {
    /* the embedder's attachments, drawn into directly: the output conversion
     * (sRGB encode, tone mapping) is the embedder's own pass */
    const t3_external_target *et = &r->ext_target;
    if (r->output_color_space == T3_SRGB_COLOR_SPACE || r->tone_mapping)
      gen_note(r, "an external WebGPU target is drawn directly: set the output colour space linear and tone mapping off, and convert in your own pass");
    d->color = (WGPUTextureView)et->color;
    d->resolve = et->samples > 1 ? (WGPUTextureView)et->color_resolve : NULL;
    d->depth = (WGPUTextureView)et->depth;
    d->direct = d->ext = true;
    r->wg_pass_format = et->color ? et->color_format : 0;
    r->wg_depth_format = et->depth ? et->depth_format : 0;
    r->wg_samples = et->samples > 1 ? et->samples : 1;
    r->wg_flip = et->flip_y;
    return;
  }
  if (r->target) {
    t3_wgpu_rt T;
    t3_wgpu_render_target(w, r->target, &T);
    d->color = T.color; d->resolve = T.resolve; d->depth = T.depth;
    d->direct = true;
    r->wg_pass_format = T.color_format;
    r->wg_depth_format = T.depth_format;
    r->wg_samples = T.samples;
    return;
  }
  d->out = r->wg_ext_out ? (WGPUTextureView)r->wg_ext_out : t3_wgpu_target(w, &r->wg_out, r->width, r->height, r->wg_out_format, true);
  d->depth = t3_wgpu_target(w, &r->wg_depth, r->width, r->height, WGPUTextureFormat_Depth24Plus, false);
  /* r186 draws straight into the canvas when the output needs no conversion
   * (no tone mapping, linear output colour space): no target, no output pass */
  d->direct = r->output_color_space != T3_SRGB_COLOR_SPACE && !r->tone_mapping;
  d->color = d->direct ? d->out : t3_wgpu_target(w, &r->wg_color, r->width, r->height, WGPUTextureFormat_RGBA16Float, true);
  r->wg_pass_format = d->direct ? (r->wg_ext_out ? r->wg_ext_out_format : r->wg_out_format) : WGPUTextureFormat_RGBA16Float;
  r->wg_depth_format = WGPUTextureFormat_Depth24Plus;
  r->wg_samples = 1;
}

static void wgpu_render(t3_renderer *r, t3_scene *scene, const t3_camera *cam) {
  t3_wgpu *w = r->wgpu;
  int items = r->opaque.n + r->transparent.n + 4;
  /* each shadow pass (a light, or a point light's face) draws its casters
   * too, from the same stream */
  int passes = 1;
  if (r->shadow_map_enabled)
    for (int i = 0; i < r->caster_count; i++) passes += r->casters[i]->base.type == T3_POINT_LIGHT ? 6 : 1;
  t3_wgpu_begin_frame(w, (size_t)items * passes * 2048);
  if (r->inst_n) t3_wgpu_floats(w, &r->wg_inst, r->inst, (size_t)r->inst_n * INST_FLOATS);
  wgpu_shadow_maps(r, scene);
  wgpu_dest D;
  wgpu_destination(r, &D);
  bool bg_color = scene->has_background && !scene->background_texture;
  t3_color cc = bg_color ? scene->background : r->clear_color;
  bool clear = bg_color || r->auto_clear;
  t3_wgpu_pass P = { .color = D.color, .resolve = D.resolve, .depth = D.depth, .load_color = !clear, .load_depth = !r->auto_clear,
                     .clear = { cc.r, cc.g, cc.b, bg_color ? 1 : r->clear_alpha } };
  if (D.ext) {
    /* the embedder's load ops and clear values (a scene background colour still wins for the colour) */
    const t3_external_target *et = &r->ext_target;
    P.load_color = et->load_color && !bg_color;
    P.load_depth = et->load_depth;
    if (!bg_color) memcpy(P.clear, et->clear_rgba, sizeof P.clear);
    P.depth_clear_set = true; P.clear_depth = et->clear_depth;
    P.stencil = depth_has_stencil(et->depth_format); P.clear_stencil = (uint32_t)et->clear_stencil;
    P.dmin = et->depth_min; P.dmax = et->depth_max;
  }
  const t3_render_target *rt = r->target;
  if (rt) {
    /* the target's own viewport and scissor (top-left origin, as r186) */
    P.vx = rt->viewport[0]; P.vy = rt->viewport[1]; P.vw = rt->viewport[2]; P.vh = rt->viewport[3];
    P.scissor = rt->scissor_test; P.sx = rt->scissor[0]; P.sy = rt->scissor[1]; P.sw = rt->scissor[2]; P.sh = rt->scissor[3];
  } else {
    P.vx = r->view_x; P.vy = r->view_y; P.vw = r->view_w; P.vh = r->view_h;
    P.scissor = r->scissor_test; P.sx = r->sc_x; P.sy = r->sc_y; P.sw = r->sc_w; P.sh = r->sc_h;
  }
  t3_wgpu_begin_pass(w, &P);
  backpass_side(r, T3_FRONT_SIDE);   /* (after the shadow maps, which see DoubleSide as r186's do) */
  /* what transmission copies: the colour drawn into (the resolve target when multisampled) */
  r->wg_cur_pass = P;
  /* (an external target's colour view, or its resolve view, must have TextureBinding usage;
   * with flip_y its rows are bottom-up and the copy flips them back) */
  r->wg_cur_src = (void *)(D.resolve ? D.resolve : D.color);
  r->wg_cur_src_format = r->wg_pass_format;
  r->wg_cur_w = rt ? rt->width : r->width;
  r->wg_cur_h = rt ? rt->height : r->height;
  gen_background(r, scene, cam);
  for (int i = 0; i < r->opaque.n; i++) wgpu_draw_item(r, &r->opaque.items[i], cam);
  if (r->backpass.n) {
    backpass_side(r, T3_BACK_SIDE);
    for (int i = 0; i < r->backpass.n; i++) wgpu_draw_item(r, &r->backpass.items[i], cam);
    backpass_side(r, T3_FRONT_SIDE);
  }
  for (int i = 0; i < r->transparent.n; i++) wgpu_draw_item(r, &r->transparent.items[i], cam);
  backpass_side(r, T3_DOUBLE_SIDE);
  t3_wgpu_end_pass(w);
  if (!D.direct) wgpu_output_pass(r, D.color, D.out);
  if (!rt) count_output_pass(r);
  t3_wgpu_end_frame(w);
  if (rt) t3_wgpu_render_target_done(w, r->target);
}

/* renderer.clear() on WebGPU: a pass whose load ops clear the linear target
 * (and the depth), whole attachments as r186's WebGPUBackend.clear; the
 * output texture is cleared to the encoded colour too, as the GLES path
 * clears the screen, so a later render with autoClear off finds it either way */
static bool wgpu_clear(t3_renderer *r, bool color, bool depth) {
  if (!r->wgpu) return false;
  t3_wgpu *w = r->wgpu;
  t3_wgpu_begin_frame(w, 0);
  wgpu_dest D;
  wgpu_destination(r, &D);
  t3_color cc = r->clear_color;
  t3_wgpu_pass P = { .color = D.color, .resolve = D.resolve, .depth = D.depth, .load_color = !color, .load_depth = !depth,
                     .clear = { cc.r, cc.g, cc.b, r->clear_alpha } };
  if (D.ext) {
    const t3_external_target *et = &r->ext_target;
    P.depth_clear_set = true; P.clear_depth = et->clear_depth;
    P.stencil = depth_has_stencil(et->depth_format); P.clear_stencil = (uint32_t)et->clear_stencil;
  }
  t3_wgpu_begin_pass(w, &P);
  t3_wgpu_end_pass(w);
  if (color && !r->target && !D.direct) {
    t3_wgpu_pass O = { .color = D.out, .clear = { t3_linear_to_srgb(cc.r), t3_linear_to_srgb(cc.g), t3_linear_to_srgb(cc.b), r->clear_alpha } };
    if (r->output_color_space != T3_SRGB_COLOR_SPACE) { O.clear[0] = cc.r; O.clear[1] = cc.g; O.clear[2] = cc.b; }
    t3_wgpu_begin_pass(w, &O);
    t3_wgpu_end_pass(w);
  }
  t3_wgpu_end_frame(w);
  return true;
}

void t3_texture_adopt_wgpu(t3_texture *t, void *texture, void *view, int w, int h, int levels, bool owns) {
  t3__wgpu_release = t3_wgpu_release;   /* (an adopted texture may come before any renderer) */
  free(t->pixels);
  t->pixels = NULL;   /* never uploaded */
  t->width = w;
  t->height = h;
  t->flip_y = false;
  t->generate_mipmaps = false;
  t3_wgpu_adopt(t, (WGPUTexture)texture, (WGPUTextureView)view, levels, owns);
}
t3_texture *t3_texture_from_wgpu(void *texture, void *view, int w, int h, int levels, bool is_cube, bool owns) {
  t3_texture *t = t3_texture_new(1, 1, NULL);
  if (is_cube) {
    t->is_cube = t->needs_flip_env_map = true;
    t->mapping = T3_CUBE_REFLECTION_MAPPING;
  }
  if (levels <= 1) t->min_filter = T3_LINEAR;
  t3_texture_adopt_wgpu(t, texture, view, w, h, levels, owns);
  return t;
}
void t3_renderer_use_wgpu(t3_renderer *r, void *device, void *queue, int out_format) {
  t3__wgpu_release = t3_wgpu_release;
  r->wgpu = t3_wgpu_new((WGPUDevice)device, (WGPUQueue)queue, (WGPUTextureFormat)out_format);
  r->wg_out_format = out_format;
  r->auto_instancing = true;
}
bool t3_renderer_wgpu_read(t3_renderer *r, uint8_t *rgba) {
  return r->wgpu && !r->wg_ext_out && t3_wgpu_read_rgba8(r->wgpu, r->wg_out, r->width, r->height, rgba);
}
void t3_renderer_wgpu_defer(t3_renderer *r, bool on) { if (r->wgpu) t3_wgpu_set_defer(r->wgpu, on); }
/* (tests) the WebGPU work counters: bind groups made, pass state set, sets skipped */
void t3__wgpu_counters(t3_renderer *r, unsigned out[3]);
void t3__wgpu_counters(t3_renderer *r, unsigned out[3]) { if (r->wgpu) t3_wgpu_counters(r->wgpu, out); else out[0] = out[1] = out[2] = 0; }
void t3_renderer_wgpu_submit(t3_renderer *r) { if (r->wgpu) t3_wgpu_submitted(r->wgpu); }
void t3_renderer_wgpu_set_encoder(t3_renderer *r, void *encoder) { if (r->wgpu) t3_wgpu_set_encoder(r->wgpu, (WGPUCommandEncoder)encoder); }
void t3_renderer_wgpu_set_output(t3_renderer *r, void *view, int format) {
  r->wg_ext_out = view;
  r->wg_ext_out_format = format;
}
#else
void t3_renderer_use_wgpu(t3_renderer *r, void *device, void *queue, int out_format) {
  (void)device; (void)queue; (void)out_format;
  gen_note(r, "this three.c was built without T3_WGPU");
}
bool t3_renderer_wgpu_read(t3_renderer *r, uint8_t *rgba) { (void)r; (void)rgba; return false; }
void t3_renderer_wgpu_defer(t3_renderer *r, bool on) { (void)r; (void)on; }
void t3_renderer_wgpu_submit(t3_renderer *r) { (void)r; }
void t3_renderer_wgpu_set_encoder(t3_renderer *r, void *encoder) { (void)r; (void)encoder; }
void t3_renderer_wgpu_set_output(t3_renderer *r, void *view, int format) { (void)r; (void)view; (void)format; }
void t3_texture_adopt_wgpu(t3_texture *t, void *texture, void *view, int w, int h, int levels, bool owns) {
  (void)t; (void)texture; (void)view; (void)w; (void)h; (void)levels; (void)owns;
}
t3_texture *t3_texture_from_wgpu(void *texture, void *view, int w, int h, int levels, bool is_cube, bool owns) {
  (void)texture; (void)view; (void)w; (void)h; (void)levels; (void)is_cube; (void)owns;
  return NULL;   /* (this three.c was built without T3_WGPU) */
}
static bool wgpu_clear(t3_renderer *r, bool color, bool depth) { (void)r; (void)color; (void)depth; return false; }
#endif
