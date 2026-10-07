/* A GLES program built from the GLSL three.js r186's node renderer generates
 * (src/gen/programs_gl.c), run with the manifest's uniform blocks.
 *
 * The program text is executed as captured, except for one mechanical edit:
 * the generated vertex shader numbers its attributes in its own order
 * (standard+map has uv at 0, normal at 1, position at 2), and three.c keeps
 * one VAO per geometry at fixed locations, so the `layout(location = N)` of
 * each attribute is rewritten to the fixed location of its name. Anything the
 * rewrite does not know fails the build loudly.
 *
 * Uniform data lives in std140 blocks, one per manifest group, each with a
 * CPU mirror: writers fill named property slots (the manifest's fingerprinted
 * byte ranges), t3_gen_gl_flush uploads the groups that changed. */
#ifndef T3_GEN_PROGRAM_H
#define T3_GEN_PROGRAM_H

#include "gl.h"
#include "internal.h"
#include "gen/programs.h"

/* Fixed attribute locations: one VAO per geometry serves every program
 * (the generated GLSL is renumbered to these, gen_program.c). */
enum {
  A_POSITION = 0, A_NORMAL = 1, A_UV = 2, A_COLOR = 3, A_TANGENT = 4, A_UV1 = 5,
  A_INSTANCE_MATRIX = 7, /* 7..10 */
  A_INSTANCE_COLOR = 11,
  A_SKIN_INDEX = 6, A_SKIN_WEIGHT = 15
};

#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif

#define T3_GEN_MAX_GROUPS 6

/* What a manifest property is: each path the generator fingerprinted maps to
 * one op the renderer knows how to fill; `idx` is the light index of
 * `directional[2].color`. A path with no op makes the program incomplete (the
 * renderer then does not use it, and says which path was missing). */
typedef enum {
  GOP_UNSUPPORTED = 0,
  /* per frame: camera, lights, fog */
  GOP_CAM_PROJ, GOP_CAM_VIEW, GOP_AMBIENT,
  GOP_DIR_COLOR, GOP_DIR_POS, GOP_DIR_TARGET,
  GOP_POINT_COLOR, GOP_POINT_POS, GOP_POINT_DIST, GOP_POINT_DECAY,
  GOP_SPOT_COLOR, GOP_SPOT_POS, GOP_SPOT_WPOS, GOP_SPOT_TARGET, GOP_SPOT_DIST, GOP_SPOT_DECAY, GOP_SPOT_CONE, GOP_SPOT_PENUMBRA,
  GOP_HEMI_SKY, GOP_HEMI_GROUND, GOP_HEMI_POS,
  GOP_FOG_COLOR, GOP_FOG_NEAR, GOP_FOG_FAR, GOP_FOG_DENSITY,
  GOP_VIEWPORT_RECT, GOP_VIEWPORT_SIZE, GOP_DRAWBUF_SIZE, GOP_HALF_HEIGHT, GOP_CAM_WORLD, GOP_TONE_EXPOSURE,
  GOP_SHADOW,      /* idx: light index, sub: type << 4 | field (gen_program.c) */
  GOP_FRAME_END,   /* ops after this are per draw */
  GOP_MODEL, GOP_OBJ_POS, GOP_NORMAL_MATRIX, GOP_SKIN_BIND, GOP_SKIN_BIND_INV, GOP_MORPH_BASE, GOP_MORPH_INFLUENCE,
  GOP_MAT_COLOR, GOP_MAT_OPACITY, GOP_MAT_EMISSIVE, GOP_MAT_EMISSIVE_I, GOP_MAT_ROUGH, GOP_MAT_METAL,
  GOP_MAT_SPECULAR, GOP_MAT_SHININESS, GOP_MAT_IOR, GOP_MAT_CLEARCOAT, GOP_MAT_CLEARCOAT_R,
  GOP_MAT_ROTATION, GOP_MAT_SIZE, GOP_MAT_ALPHATEST, GOP_SPRITE_CENTER,
  /* MeshPhysicalMaterial */
  GOP_MAT_SPEC_INTENSITY, GOP_MAT_SPEC_COLOR, GOP_MAT_CC_NORMAL_SCALE, GOP_MAT_SHEEN, GOP_MAT_SHEEN_COLOR, GOP_MAT_SHEEN_R,
  GOP_MAT_IRID, GOP_MAT_IRID_IOR, GOP_MAT_IRID_THICK_MAX, GOP_MAT_ANISO_VEC,
  GOP_MAT_TRANSMISSION, GOP_MAT_THICKNESS, GOP_MAT_ATTEN_DIST, GOP_MAT_ATTEN_COLOR,
  /* environments, backgrounds, the PMREM passes (values from the renderer's
   * aux block, renderer_gen.inc gen_aux) */
  GOP_ENV_INTENSITY, GOP_ENV_ROTATION, GOP_MAT_REFLECTIVITY, GOP_MAT_REFRACTION,
  GOP_CUBEUV_TW, GOP_CUBEUV_TH, GOP_CUBEUV_MAXMIP,
  GOP_BG_INTENSITY, GOP_BG_ROTATION, GOP_BG_BLUR, GOP_BG_MATRIX, GOP_BG_FLIPY,
  GOP_PMREM_ROUGH, GOP_PMREM_MIP, GOP_PMREM_SIGMA, GOP_PMREM_FLIPY, GOP_ENV_FLIPY,
  GOP_MAP_MATRIX, GOP_TEX_MATRIX /* idx: T3_GEN_TEX_* */, GOP_TEX_FLIPY /* idx: T3_GEN_TEX_*, or T3_GEN_TEX_COUNT for the DFG LUT */, GOP_NORMAL_SCALE, GOP_AO_INTENSITY, GOP_BUMP_SCALE, GOP_LIGHTMAP_INTENSITY,
  GOP_COUNT
} t3_gen_op_kind;

/* the textures a material binds, by the names the manifest uses */
enum { T3_GEN_TEX_MAP, T3_GEN_TEX_NORMAL, T3_GEN_TEX_AO, T3_GEN_TEX_EMISSIVE, T3_GEN_TEX_ROUGHNESS, T3_GEN_TEX_METALNESS,
       T3_GEN_TEX_ALPHA, T3_GEN_TEX_BUMP, T3_GEN_TEX_SPECULAR, T3_GEN_TEX_LIGHT, T3_GEN_TEX_CLEARCOAT_NORMAL, T3_GEN_TEX_COUNT };
extern const char *const t3_gen_tex_names[T3_GEN_TEX_COUNT];

enum { T3_GEN_LT_DIR, T3_GEN_LT_POINT, T3_GEN_LT_SPOT };
enum { T3_GEN_SH_BIAS, T3_GEN_SH_NORMAL_BIAS, T3_GEN_SH_RADIUS, T3_GEN_SH_INTENSITY, T3_GEN_SH_MAP_SIZE, T3_GEN_SH_MATRIX };
struct t3_gen_op { uint8_t op, idx, sub; int8_t group; uint16_t off, len; };

/* an object's values for the program's template parameters (src->tpl) */
typedef struct { uint16_t bones, morphs, morph_width; } t3_gen_params;

#define T3_GEN_MAX_BUFFERS 4

typedef struct t3_gen_gl {
  const t3_gen_program *src;
  t3_gen_params params;
  GLuint program;
  int n_groups;
  GLuint ubo[T3_GEN_MAX_GROUPS];
  uint8_t *mirror[T3_GEN_MAX_GROUPS];
  uint32_t bytes[T3_GEN_MAX_GROUPS];
  bool dirty[T3_GEN_MAX_GROUPS];
  struct t3_gen_op *ops;         /* frame ops first, then draw ops */
  int n_frame_ops, n_ops;
  char unsupported[64];          /* the first property path with no op, or "" */
  bool complete;
  bool encode;
  /* the one group every per-draw op writes (and no frame op does), streamed
   * per draw from the renderer's frame buffer; -1 = per-draw upload instead */
  int draw_group;
  /* the groups' extra buffers (bone matrices, morph influences): their own
   * blocks at binding points T3_GEN_MAX_GROUPS + i, uploaded per draw */
  int n_buffers;
  struct { GLuint ubo; uint32_t bytes; const char *source; GLuint point; } buf[T3_GEN_MAX_BUFFERS];
  /* sampler uniforms by manifest texture, set to units 0.. at build */
  int n_textures;
  struct { GLint loc; const t3_bk_texture_layout *layout; } tex[16];
  unsigned render_stamp;   /* the render the frame ops were last run for */
  struct gen_wgpu_prog *wg;   /* the WebGPU half (gen_wgpu.c), NULL on GLES */
} t3_gen_gl;

/* NULL with err set when the driver rejects the program or a name is unknown */
/* encode: write the output colour space (sRGB) in the fragment shader itself,
 * for frames drawn straight to the screen with no output pass */
t3_gen_gl *t3_gen_gl_build(const t3_gen_program *src, bool encode, t3_gen_params params, char *err, size_t errcap);
void t3_gen_gl_free(t3_gen_gl *g);
/* backend-neutral part of a build (ops, draw group, constants) */
void t3_gen_common(t3_gen_gl *g);
/* the program text with the object's template values (gen_program.c) */
char *t3_gen_apply_templates(const char *text, const t3_gen_program *src, t3_gen_params p, char *err, size_t errcap);
/* an op's bytes in its group mirror (marks the group dirty) */
uint8_t *t3_gen_gl_op(t3_gen_gl *g, const struct t3_gen_op *op);
/* bind and upload what changed: the frame groups (and the draw group too when
 * draw_group < 0) */
void t3_gen_gl_flush(t3_gen_gl *g);

#endif
