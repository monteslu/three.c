/* See gen_program.h. */
#include "gen_program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* three.c's fixed attribute locations (gen_program.h) by the names r186's
 * generated programs use. nodeAttribute0..3 are the four vec4 columns of the
 * instance matrix (r186's instanced-attribute path), like A_INSTANCE_MATRIX. */
static int fixed_location(const char *name) {
  if (!strcmp(name, "position")) return A_POSITION;
  if (!strcmp(name, "normal")) return A_NORMAL;
  if (!strcmp(name, "uv")) return A_UV;
  if (!strcmp(name, "color")) return A_COLOR;
  if (!strcmp(name, "tangent")) return A_TANGENT;
  if (!strcmp(name, "skinIndex")) return A_SKIN_INDEX;
  if (!strcmp(name, "outputDirection")) return A_NORMAL;   /* the PMREM passes' planes carry it in the normal slot */
  if (!strcmp(name, "skinWeight")) return A_SKIN_WEIGHT;
  if (!strcmp(name, "instanceColor")) return A_INSTANCE_COLOR;   /* renamed by tools/emit-programs.mjs */
  if (!strncmp(name, "nodeAttribute", 13) && name[13] >= '0' && name[13] <= '3' && !name[14])
    return A_INSTANCE_MATRIX + (name[13] - '0');
  return -1;
}

/* the vertex source with each `layout( location = N ) in <type> <name>;`
 * renumbered; NULL with err set on an attribute we have no fixed place for */
static char *renumber_attributes(const char *src, const char *state, char *err, size_t errcap) {
  size_t cap = strlen(src) + 64, n = 0;
  char *out = malloc(cap);
  if (!out) return NULL;
  const char *p = src;
  const char *tag = "layout( location = ";
  size_t taglen = strlen(tag);
  while (*p) {
    const char *hit = strstr(p, tag);
    if (!hit) { size_t rest = strlen(p); if (n + rest + 1 > cap) { cap = n + rest + 1; out = realloc(out, cap); } memcpy(out + n, p, rest); n += rest; break; }
    const char *num = hit + taglen, *q = num;
    while (*q >= '0' && *q <= '9') q++;
    /* `) in ` marks a vertex input (outputs and uniform blocks use other words) */
    if (q == num || strncmp(q, " ) in ", 6)) {
      size_t keep = (size_t)(hit - p) + taglen;
      if (n + keep + 1 > cap) { cap = (n + keep) * 2; out = realloc(out, cap); }
      memcpy(out + n, p, keep); n += keep; p = hit + taglen; continue;
    }
    const char *decl = q + 6;          /* "<type> <name>;" */
    const char *sp = strchr(decl, ' ');
    const char *semi = sp ? strchr(sp, ';') : NULL;
    if (!sp || !semi) { snprintf(err, errcap, "%s: unparsable attribute declaration", state); free(out); return NULL; }
    char name[48];
    size_t nl = (size_t)(semi - (sp + 1));
    if (nl == 0 || nl >= sizeof name) { snprintf(err, errcap, "%s: attribute name too long", state); free(out); return NULL; }
    memcpy(name, sp + 1, nl); name[nl] = 0;
    int loc = fixed_location(name);
    if (loc < 0) { snprintf(err, errcap, "%s: attribute '%s' has no fixed location", state, name); free(out); return NULL; }
    char head[40];
    int hl = snprintf(head, sizeof head, "layout( location = %d", loc);
    size_t pre = (size_t)(hit - p);
    if (n + pre + (size_t)hl + 8 > cap) { cap = (n + pre + (size_t)hl) * 2 + 64; out = realloc(out, cap); }
    memcpy(out + n, p, pre); n += pre;
    memcpy(out + n, head, (size_t)hl); n += (size_t)hl;
    p = q;                              /* continues at " ) in ..." */
  }
  out[n] = 0;
  return out;
}

static GLuint compile(GLenum type, const char *src, char *err, size_t errcap) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLsizei len = 0;
    glGetShaderInfoLog(s, (GLsizei)errcap - 1, &len, err);
    err[len] = 0;
    glDeleteShader(s);
    return 0;
  }
  return s;
}

const char *const t3_gen_tex_names[T3_GEN_TEX_COUNT] = { "map", "normalMap", "aoMap", "emissiveMap", "roughnessMap", "metalnessMap",
                                                         "alphaMap", "bumpMap", "specularMap", "lightMap", "clearcoatNormalMap" };

static const struct { const char *tail; uint8_t op; } OPS[] = {
  { "camera.projectionMatrix", GOP_CAM_PROJ }, { "camera.matrixWorldInverse", GOP_CAM_VIEW },
  { "ambient.color", GOP_AMBIENT },
  { "directional[].color", GOP_DIR_COLOR }, { "directional[].position", GOP_DIR_POS }, { "directional[].targetPosition", GOP_DIR_TARGET },
  { "point[].color", GOP_POINT_COLOR }, { "point[].position", GOP_POINT_POS }, { "point[].distance", GOP_POINT_DIST }, { "point[].decay", GOP_POINT_DECAY },
  { "spot[].color", GOP_SPOT_COLOR }, { "spot[].position", GOP_SPOT_POS }, { "spot[].worldPosition", GOP_SPOT_WPOS },
  { "spot[].targetPosition", GOP_SPOT_TARGET }, { "spot[].distance", GOP_SPOT_DIST }, { "spot[].decay", GOP_SPOT_DECAY },
  { "spot[].coneCos", GOP_SPOT_CONE }, { "spot[].penumbraCos", GOP_SPOT_PENUMBRA },
  { "hemisphere[].skyColor", GOP_HEMI_SKY }, { "hemisphere[].groundColor", GOP_HEMI_GROUND }, { "hemisphere[].position", GOP_HEMI_POS },
  { "fog.color", GOP_FOG_COLOR }, { "fog.near", GOP_FOG_NEAR }, { "fog.far", GOP_FOG_FAR }, { "fog.density", GOP_FOG_DENSITY },
  { "renderer.viewport", GOP_VIEWPORT_RECT }, { "renderer.viewportSize", GOP_VIEWPORT_SIZE },
  { "renderer.drawingBufferSize", GOP_DRAWBUF_SIZE }, { "renderer.halfHeight", GOP_HALF_HEIGHT }, { "camera.worldMatrix", GOP_CAM_WORLD }, { "camera.position", GOP_CAM_POS },
  { "renderer.toneMappingExposure", GOP_TONE_EXPOSURE },
  { "skin.bindMatrix", GOP_SKIN_BIND }, { "skin.bindMatrixInverse", GOP_SKIN_BIND_INV },
  { "morph.baseInfluence", GOP_MORPH_BASE }, { "morph.influence[]", GOP_MORPH_INFLUENCE },
  { "object.matrixWorld", GOP_MODEL }, { "object.position", GOP_OBJ_POS }, { "object.normalMatrix", GOP_NORMAL_MATRIX },
  { "material.color", GOP_MAT_COLOR }, { "material.opacity", GOP_MAT_OPACITY }, { "material.emissive", GOP_MAT_EMISSIVE },
  { "material.emissiveIntensity", GOP_MAT_EMISSIVE_I }, { "material.roughness", GOP_MAT_ROUGH }, { "material.metalness", GOP_MAT_METAL },
  { "material.specular", GOP_MAT_SPECULAR }, { "material.shininess", GOP_MAT_SHININESS }, { "material.ior", GOP_MAT_IOR },
  { "material.clearcoat", GOP_MAT_CLEARCOAT }, { "material.clearcoatRoughness", GOP_MAT_CLEARCOAT_R },
  { "material.specularIntensity", GOP_MAT_SPEC_INTENSITY }, { "material.specularColor", GOP_MAT_SPEC_COLOR },
  { "material.clearcoatNormalScale", GOP_MAT_CC_NORMAL_SCALE }, { "material.sheen", GOP_MAT_SHEEN },
  { "material.sheenColor", GOP_MAT_SHEEN_COLOR }, { "material.sheenRoughness", GOP_MAT_SHEEN_R },
  { "material.iridescence", GOP_MAT_IRID }, { "material.iridescenceIOR", GOP_MAT_IRID_IOR },
  { "material.iridescenceThicknessMax", GOP_MAT_IRID_THICK_MAX }, { "material.anisotropyVector", GOP_MAT_ANISO_VEC },
  { "material.transmission", GOP_MAT_TRANSMISSION }, { "material.thickness", GOP_MAT_THICKNESS },
  { "material.attenuationDistance", GOP_MAT_ATTEN_DIST }, { "material.attenuationColor", GOP_MAT_ATTEN_COLOR },
  { "material.normalScale", GOP_NORMAL_SCALE }, { "material.aoMapIntensity", GOP_AO_INTENSITY },
  { "material.bumpScale", GOP_BUMP_SCALE }, { "material.rotation", GOP_MAT_ROTATION }, { "material.size", GOP_MAT_SIZE }, { "material.alphaTest", GOP_MAT_ALPHATEST },
  { "sprite.center", GOP_SPRITE_CENTER }, { "material.lightMapIntensity", GOP_LIGHTMAP_INTENSITY },
  { "material.envMapIntensity", GOP_ENV_INTENSITY }, { "scene.environmentIntensity", GOP_ENV_INTENSITY },
  { "material.envMapRotation", GOP_ENV_ROTATION }, { "scene.environmentRotation", GOP_ENV_ROTATION },
  { "material.reflectivity", GOP_MAT_REFLECTIVITY }, { "material.refractionRatio", GOP_MAT_REFRACTION },
  { "cubeuv.texelWidth", GOP_CUBEUV_TW }, { "cubeuv.texelHeight", GOP_CUBEUV_TH }, { "cubeuv.maxMip", GOP_CUBEUV_MAXMIP },
  { "scene.backgroundIntensity", GOP_BG_INTENSITY }, { "scene.backgroundRotationT", GOP_BG_ROTATION },
  { "scene.backgroundBlurriness", GOP_BG_BLUR }, { "background.matrix", GOP_BG_MATRIX }, { "background.flipY", GOP_BG_FLIPY },
  { "pmrem.roughness", GOP_PMREM_ROUGH }, { "pmrem.mipInt", GOP_PMREM_MIP }, { "pmrem.sigma", GOP_PMREM_SIGMA },
  { "pmrem.source.flipY", GOP_PMREM_FLIPY }, { "envMap.flipY", GOP_ENV_FLIPY }, { "environment.flipY", GOP_ENV_FLIPY },
};

/* "directional[2].color" -> op GOP_DIR_COLOR, idx 2 ("directional[].color" in
 * the table); false when the path is not one we fill */
static bool parse_op(const char *path, uint8_t *op, uint8_t *idx, uint8_t *sub) {
  char norm[96];
  size_t n = 0;
  int index = 0;
  for (const char *p = path; *p && *p != '#' && n < sizeof norm - 1; p++) {   /* "path#1": another copy of the same value */
    if (*p == '[') {
      const char *q = p + 1;
      int v = 0;
      while (*q >= '0' && *q <= '9') v = v * 10 + (*q++ - '0');
      if (*q == ']' && q > p + 1) { index = v; norm[n++] = '['; norm[n++] = ']'; p = q; continue; }
    }
    norm[n++] = *p;
  }
  norm[n] = 0;
  for (size_t i = 0; i < sizeof OPS / sizeof OPS[0]; i++)
    if (!strcmp(norm, OPS[i].tail)) { *op = OPS[i].op; *idx = (uint8_t)index; return true; }
  size_t nl = strlen(norm);
  /* "<type>[i].shadow.<field>" */
  {
    static const char *const types[] = { "directional[].shadow.", "point[].shadow.", "spot[].shadow." };
    static const char *const fields[] = { "bias", "normalBias", "radius", "intensity", "mapSize", "matrix" };
    for (int t = 0; t < 3; t++) {
      size_t tl = strlen(types[t]);
      if (strncmp(norm, types[t], tl)) continue;
      for (int f = 0; f < 6; f++)
        if (!strcmp(norm + tl, fields[f])) { *op = GOP_SHADOW; *idx = (uint8_t)index; *sub = (uint8_t)(t << 4 | f); return true; }
    }
  }
  /* "shadow:<type>[i].flipY": a depth texture, so flipped */
  if (!strncmp(norm, "shadow:", 7) && nl > 6 && !strcmp(norm + nl - 6, ".flipY")) { *op = GOP_TEX_FLIPY; *idx = T3_GEN_TEX_COUNT + 1; return true; }
  /* "<texture>.flipY": its sampling flip flag */
  if (nl > 6 && !strcmp(norm + nl - 6, ".flipY")) {
    if (!strncmp(norm, "dfg_lut", nl - 6) && nl - 6 == 7) { *op = GOP_TEX_FLIPY; *idx = T3_GEN_TEX_COUNT; return true; }
    if (!strncmp(norm, "output", nl - 6) && nl - 6 == 6) { *op = GOP_TEX_FLIPY; *idx = T3_GEN_TEX_COUNT + 1; return true; }   /* a framebuffer texture: flipped */
    for (int t = 0; t < T3_GEN_TEX_COUNT; t++)
      if (strlen(t3_gen_tex_names[t]) == nl - 6 && !strncmp(norm, t3_gen_tex_names[t], nl - 6)) { *op = GOP_TEX_FLIPY; *idx = (uint8_t)t; return true; }
  }
  /* any other "<source>.flipY" (a morph texture, a DataTexture): 0 */
  if (nl > 6 && !strcmp(norm + nl - 6, ".flipY")) { *op = GOP_TEX_FLIPY; *idx = T3_GEN_TEX_COUNT; return true; }
  /* "<texture>.matrix": the texture's uv transform */
  if (nl > 7 && !strcmp(norm + nl - 7, ".matrix"))
    for (int t = 0; t < T3_GEN_TEX_COUNT; t++)
      if (strlen(t3_gen_tex_names[t]) == nl - 7 && !strncmp(norm, t3_gen_tex_names[t], nl - 7)) { *op = GOP_TEX_MATRIX; *idx = (uint8_t)t; return true; }
  return false;
}

/* the fragment source writing sRGB itself: the generated main writes linear
 * `fragColor`; that becomes a plain variable and a last statement encodes it
 * with r186's sRGB transfer (the same formula its output pass uses) */
static char *encode_output(const char *src, const char *state, char *err, size_t errcap) {
  const char *decl = "layout( location = 0 ) out vec4 fragColor;";
  const char *d = strstr(src, decl);
  const char *end = strrchr(src, '}');
  if (!d || !end || end < d) { snprintf(err, errcap, "%s: fragment output not where expected", state); return NULL; }
  const char *repl = "layout( location = 0 ) out vec4 t3_fragOut;\nvec4 fragColor;\n"
                     "vec3 t3_oetf( vec3 v ) { return mix( pow( v, vec3( 0.41666 ) ) * 1.055 - vec3( 0.055 ), v * 12.92, vec3( lessThanEqual( v, vec3( 0.0031308 ) ) ) ); }";
  /* as r186's output pass: alpha clamped, unpremultiplied (0 where alpha
   * is 0), encoded, premultiplied again */
  const char *tail = "\t{ float t3_a = clamp( fragColor.a, 0.0, 1.0 );\n"
                     "\t  vec3 t3_c = t3_a == 0.0 ? vec3( 0.0 ) : fragColor.rgb / vec3( t3_a );\n"
                     "\t  t3_fragOut = vec4( t3_oetf( t3_c ) * vec3( t3_a ), t3_a ); }\n";
  size_t n = strlen(src) + strlen(repl) + strlen(tail) + 1;
  char *out = malloc(n);
  if (!out) return NULL;
  size_t a = (size_t)(d - src), b = (size_t)(end - (d + strlen(decl)));
  char *p = out;
  memcpy(p, src, a); p += a;
  memcpy(p, repl, strlen(repl)); p += strlen(repl);
  memcpy(p, d + strlen(decl), b); p += b;
  memcpy(p, tail, strlen(tail)); p += strlen(tail);
  strcpy(p, end);
  return out;
}

/* replace every occurrence of `from` with `to`; *n counts them */
static char *subst(char *text, const char *from, const char *to, int *n) {
  size_t fl = strlen(from), tl = strlen(to), count = 0;
  for (const char *p = text; (p = strstr(p, from)); p += fl) count++;
  if (!count) return text;
  char *out = malloc(strlen(text) + count * (tl > fl ? tl - fl : 0) + 1);
  if (!out) { free(text); return NULL; }
  char *d = out;
  const char *p = text, *q;
  while ((q = strstr(p, from))) { memcpy(d, p, (size_t)(q - p)); d += q - p; memcpy(d, to, tl); d += tl; p = q + fl; }
  strcpy(d, p);
  free(text);
  *n += (int)count;
  return out;
}

/* The program text with the object's template values in place of the
 * capture's (src->tpl): r186 bakes the bone count into the bone-matrix array
 * size, the morph count into the influence array size and the morph loop, the
 * morph texture's row width into the vertex-index arithmetic. The capture used
 * values that occur nowhere else; each pattern must be found, or the build
 * fails. */
static char *apply_templates(const char *text, const t3_gen_program *src, t3_gen_params p, char *err, size_t errcap) {
  size_t tl0 = strlen(text);
  char *t = malloc(tl0 + 1);
  if (!t) return NULL;
  memcpy(t, text, tl0 + 1);
  char a[32], b[32];
  int n;
  if (src->tpl[0]) {
    n = 0;
    snprintf(a, sizeof a, "[%u];", src->tpl[0]); snprintf(b, sizeof b, "[%u];", p.bones);
    t = subst(t, a, b, &n);
    snprintf(a, sizeof a, "f32>, %u >", src->tpl[0]); snprintf(b, sizeof b, "f32>, %u >", p.bones);   /* WGSL array< mat4x4<f32>, N > */
    if (t) t = subst(t, a, b, &n);
    if (t && (strstr(text, "mat4 buffer") || strstr(text, "array< mat4x4<f32>")) && n != 1) { snprintf(err, errcap, "%s: bone array size not found once (%d)", src->state, n); free(t); return NULL; }
  }
  if (t && src->tpl[1]) {
    n = 0;
    snprintf(a, sizeof a, "[%u];", src->tpl[1]); snprintf(b, sizeof b, "[%u];", p.morphs);
    t = subst(t, a, b, &n);
    snprintf(a, sizeof a, "vec4<f32>, %u >", src->tpl[1]); snprintf(b, sizeof b, "vec4<f32>, %u >", p.morphs);   /* WGSL */
    if (t) t = subst(t, a, b, &n);
    snprintf(a, sizeof a, "i < %u;", src->tpl[1]); snprintf(b, sizeof b, "i < %u;", p.morphs);
    if (t) t = subst(t, a, b, &n);
    if (t && (strstr(text, "gl_VertexID") || strstr(text, "vertex_index")) && n != 2) { snprintf(err, errcap, "%s: morph count found %d times, not 2", src->state, n); free(t); return NULL; }
  }
  if (t && src->tpl[2]) {
    n = 0;
    snprintf(a, sizeof a, "/ %u )", src->tpl[2]); snprintf(b, sizeof b, "/ %u )", p.morph_width);
    t = subst(t, a, b, &n);
    snprintf(a, sizeof a, "* %u )", src->tpl[2]); snprintf(b, sizeof b, "* %u )", p.morph_width);
    if (t) t = subst(t, a, b, &n);
    if (t && (strstr(text, "gl_VertexID") || strstr(text, "vertex_index")) && n < 2) { snprintf(err, errcap, "%s: morph width found %d times", src->state, n); free(t); return NULL; }
  }
  return t;
}

t3_gen_gl *t3_gen_gl_build(const t3_gen_program *src, bool encode, t3_gen_params params, char *err, size_t errcap) {
  if (src->n_groups > T3_GEN_MAX_GROUPS) { snprintf(err, errcap, "%s: %d groups", src->state, src->n_groups); return NULL; }
  char *vt = apply_templates(src->vertex, src, params, err, errcap);
  if (!vt) return NULL;
  char *vs = renumber_attributes(vt, src->state, err, errcap);
  free(vt);
  if (!vs) return NULL;
  GLuint v = compile(GL_VERTEX_SHADER, vs, err, errcap);
  free(vs);
  if (!v) return NULL;
  char *ft = apply_templates(src->fragment, src, params, err, errcap);
  if (!ft) { glDeleteShader(v); return NULL; }
  char *fs = encode ? encode_output(ft, src->state, err, errcap) : NULL;
  if (encode && !fs) { free(ft); glDeleteShader(v); return NULL; }
  GLuint f = compile(GL_FRAGMENT_SHADER, fs ? fs : ft, err, errcap);
  free(fs);
  free(ft);
  if (!f) { glDeleteShader(v); return NULL; }
  GLuint prog = glCreateProgram();
  glAttachShader(prog, v);
  glAttachShader(prog, f);
  glLinkProgram(prog);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLsizei len = 0;
    glGetProgramInfoLog(prog, (GLsizei)errcap - 1, &len, err);
    err[len] = 0;
    glDeleteProgram(prog);
    return NULL;
  }
  t3_gen_gl *g = calloc(1, sizeof *g);
  T3_CHECK_ALLOC(g);
  g->src = src;
  g->params = params;
  g->encode = encode;
  g->program = prog;
  g->n_groups = src->n_groups;
  for (int i = 0; i < src->n_groups; i++) {
    const t3_bk_group_layout *L = src->groups[i];
    g->bytes[i] = L->uniform_bytes;
    if (!L->uniform_bytes) continue;
    GLuint idx = glGetUniformBlockIndex(prog, L->name);
    if (idx == GL_INVALID_INDEX) {
      /* a group the stage that links does not use is optimised out */
      g->bytes[i] = 0;
      continue;
    }
    glUniformBlockBinding(prog, idx, (GLuint)i);
    glGenBuffers(1, &g->ubo[i]);
    glBindBuffer(GL_UNIFORM_BUFFER, g->ubo[i]);
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)L->uniform_bytes, NULL, GL_DYNAMIC_DRAW);
    g->mirror[i] = calloc(1, L->uniform_bytes);
    T3_CHECK_ALLOC(g->mirror[i]);
    g->dirty[i] = true;
  }
  /* the extra buffers: their own uniform blocks, sized for this object */
  for (int i = 0; i < src->n_groups; i++) {
    const t3_bk_group_layout *L = src->groups[i];
    for (uint32_t k = 0; k < L->n_buffers && g->n_buffers < T3_GEN_MAX_BUFFERS; k++) {
      const t3_bk_buffer_layout *B = &L->buffers[k];
      uint32_t bytes = B->bytes;
      if (B->source && !strcmp(B->source, "boneMatrices")) bytes = (uint32_t)params.bones * 64;
      else if (B->source && !strcmp(B->source, "morphInfluences")) bytes = (uint32_t)params.morphs * 16;
      GLuint idx = glGetUniformBlockIndex(prog, B->name);
      if (idx == GL_INVALID_INDEX) continue;
      int bi = g->n_buffers++;
      g->buf[bi].point = (GLuint)(T3_GEN_MAX_GROUPS + bi);
      g->buf[bi].bytes = bytes;
      g->buf[bi].source = B->source;
      glUniformBlockBinding(prog, idx, g->buf[bi].point);
      glGenBuffers(1, &g->buf[bi].ubo);
      glBindBuffer(GL_UNIFORM_BUFFER, g->buf[bi].ubo);
      glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)bytes, NULL, GL_DYNAMIC_DRAW);
    }
  }
  t3_gen_common(g);
  /* the sampler uniforms, one unit per manifest texture in order */
  glUseProgram(prog);
  for (uint32_t i = 0; i < src->n_groups; i++) {
    const t3_bk_group_layout *L = src->groups[i];
    for (uint32_t t = 0; t < L->n_textures && g->n_textures < 16; t++) {
      GLint loc = glGetUniformLocation(prog, L->textures[t].name);
      if (loc >= 0) {
        glUniform1i(loc, g->n_textures);
        g->tex[g->n_textures].loc = loc;
        g->tex[g->n_textures].layout = &L->textures[t];
        g->n_textures++;
      }
    }
  }
  return g;
}

/* Backend-neutral setup once g->src, params, bytes[] (0 = group absent) and
 * mirror[] are set: the op list, the draw group, the constants. */
void t3_gen_common(t3_gen_gl *g) {
  const t3_gen_program *src = g->src;
  /* the ops: frame ops first, draw ops after */
  g->ops = calloc((size_t)src->n_properties + 1, sizeof *g->ops);
  T3_CHECK_ALLOC(g->ops);
  g->complete = true;
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k < src->n_properties; k++) {
      const t3_gen_property *pr = &src->properties[k];
      uint8_t op, idx, sub = 0;
      if (!parse_op(pr->path, &op, &idx, &sub)) {
        if (pass == 0) { g->complete = false; if (!g->unsupported[0]) snprintf(g->unsupported, sizeof g->unsupported, "%s", pr->path); }
        continue;
      }
      if (op == GOP_MORPH_INFLUENCE) continue;   /* lives in the influence buffer, filled whole per draw */
      bool frame = op < GOP_FRAME_END;
      if (frame != (pass == 0)) continue;
      if (!g->bytes[pr->group]) continue;   /* its group was optimised out of the link */
      struct t3_gen_op *o = &g->ops[g->n_ops++];
      o->op = op; o->idx = idx; o->sub = sub; o->group = (int8_t)pr->group; o->off = pr->offset; o->len = pr->length;
      if (pass == 0) g->n_frame_ops = g->n_ops;
    }
  /* the draw group: where every draw op writes, if that is one group with no frame op */
  g->draw_group = -1;
  for (int k = g->n_frame_ops; k < g->n_ops; k++) {
    int gr = g->ops[k].group;
    if (g->draw_group < 0) g->draw_group = gr;
    else if (g->draw_group != gr) { g->draw_group = -2; break; }
  }
  for (int k = 0; k < g->n_frame_ops && g->draw_group >= 0; k++)
    if (g->ops[k].group == g->draw_group) g->draw_group = -2;
  if (g->draw_group < 0) g->draw_group = -1;
  /* constants: the bytes r186 uploaded for uniforms nothing varies */
  for (int k = 0; k < src->n_constants; k++) {
    const t3_gen_constant *c = &src->constants[k];
    if (g->mirror[c->group]) memcpy(g->mirror[c->group] + c->offset, c->bytes, c->length);
  }
}

char *t3_gen_apply_templates(const char *text, const t3_gen_program *src, t3_gen_params p, char *err, size_t errcap) {
  return apply_templates(text, src, p, err, errcap);
}

void t3_gen_gl_free(t3_gen_gl *g) {
  if (!g) return;
  for (int i = 0; i < g->n_groups; i++) {
    if (g->ubo[i]) glDeleteBuffers(1, &g->ubo[i]);
    free(g->mirror[i]);
  }
  free(g->ops);
  for (int i = 0; i < g->n_buffers; i++) glDeleteBuffers(1, &g->buf[i].ubo);
  glDeleteProgram(g->program);
  free(g);
}

uint8_t *t3_gen_gl_op(t3_gen_gl *g, const struct t3_gen_op *op) {
  g->dirty[op->group] = true;
  return g->mirror[op->group] + op->off;
}

void t3_gen_gl_flush(t3_gen_gl *g) {
  /* uploads only; the renderer binds the blocks to their indices */
  for (int i = 0; i < g->n_groups; i++) {
    if (!g->ubo[i] || i == g->draw_group || !g->dirty[i]) continue;
    glBindBuffer(GL_UNIFORM_BUFFER, g->ubo[i]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)g->bytes[i], g->mirror[i]);
    g->dirty[i] = false;
  }
}

/* ── the program tables ───────────────────────────────────────────────
 * Projects add their own (tools/gen-project-table.mjs) ahead of three.c's,
 * which is the full table (programs_gl.c) or, in a core-only build, just the
 * renderer's own programs (programs_core_gl.c). Process-wide: register before
 * the first render. */
#define T3_MAX_TABLES 16
#define T3_BUILT_BLOCK 64     /* built programs per block (the blocks never move) */
#define T3_BUILT_BLOCKS 64
static const t3_gen_table *tables_gl[T3_MAX_TABLES], *tables_wgpu[T3_MAX_TABLES];
static int n_gl, n_wgpu;
/* the runtime builder's programs, per backend, in blocks of T3_BUILT_BLOCK */
static t3_gen_table built[2][T3_BUILT_BLOCKS];
static int n_built[2];
static const t3_gen_table *all_tables[2][T3_MAX_TABLES + T3_BUILT_BLOCKS + 1];
void t3_register_program_table(const struct t3_gen_table *gl_table, const struct t3_gen_table *wgpu_table) {
  if (gl_table && n_gl < T3_MAX_TABLES) tables_gl[n_gl++] = gl_table;
  if (wgpu_table && n_wgpu < T3_MAX_TABLES) tables_wgpu[n_wgpu++] = wgpu_table;
}
const t3_gen_program *t3_gen_add_built(const t3_gen_program *p) {
  int b = p->backend == T3_GEN_WGPU;
  t3_gen_table *t = n_built[b] ? &built[b][n_built[b] - 1] : NULL;
  if (!t || t->count == T3_BUILT_BLOCK) {
    if (n_built[b] == T3_BUILT_BLOCKS) t3__fatal("the runtime builder made more programs than it has room for");
    t = &built[b][n_built[b]++];
    t3_gen_program *ps = calloc(T3_BUILT_BLOCK, sizeof *ps);
    T3_CHECK_ALLOC(ps);
    t->programs = ps;
  }
  t3_gen_program *kept = &((t3_gen_program *)t->programs)[t->count++];
  *kept = *p;
  free((void *)p);   /* (its fields now belong to the block's copy) */
  return kept;
}
int t3_gen_tables(t3_gen_backend backend, const t3_gen_table *const **out) {
  /* project tables, then built programs, then the base table: a project's
   * programs win a tie */
  int b = backend == T3_GEN_WGPU, n = 0;
  const t3_gen_table **dst = all_tables[b];
  const t3_gen_table *const *proj = b ? tables_wgpu : tables_gl;
  for (int i = 0, np = b ? n_wgpu : n_gl; i < np; i++) dst[n++] = proj[i];
  for (int i = 0; i < n_built[b]; i++) dst[n++] = &built[b][i];
  if (b) {
#ifdef T3_WGPU
    dst[n++] = &t3_gen_base_wgpu;
#else
    if (!n) { *out = NULL; return 0; }
#endif
  } else dst[n++] = &t3_gen_base_gl;
  *out = dst;
  return n;
}
