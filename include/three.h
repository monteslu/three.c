/* three.c: the three.js API in C99, rendering with the programs three.js
 * r186 generates, on OpenGL ES 3.0 / WebGL2 or on WebGPU
 * (t3_renderer_use_wgpu).
 *
 * Naming follows three.js: THREE.Mesh is t3_mesh, mesh.position is
 * mesh->position, scene.add(m) is t3_object_add(scene, m),
 * renderer.render(scene, camera) is t3_renderer_render(r, scene, camera).
 *
 * Ownership is reference counting. Every *_new returns a reference the caller
 * owns; t3_release drops it. Containers retain what they hold: a parent its
 * children, a mesh its geometry and material, a material its textures. So the
 * usual pattern is
 *
 *     t3_mesh *m = t3_mesh_new(geo, mat);
 *     t3_object_add(scene, m);
 *     t3_release(m);            // the scene keeps it alive
 *
 * GL objects are created lazily by the renderer and freed when the owning
 * geometry / material / texture is released (three.js's dispose()).
 *
 * Fields are public and may be written directly, with one rule carried over
 * from three.js's onChange wiring: rotation and quaternion are two views of
 * one orientation, so set them with t3_object_set_rotation /
 * t3_object_set_quaternion (or call t3_object_rotation_changed after writing
 * rotation fields), and write material / geometry fields then bump
 * ->version (three.js's needsUpdate). */
#ifndef THREE_H
#define THREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T3_REVISION "186"   /* three.js 0.186.1 */

/* ── math ─────────────────────────────────────────────────────────── */
typedef struct { float x, y; } t3_vec2;
typedef struct { float x, y, z; } t3_vec3;
typedef struct { float x, y, z, w; } t3_vec4;
typedef struct { float x, y, z, w; } t3_quat;
typedef struct { float r, g, b; } t3_color;
/* Column-major, like three.js Matrix4.elements. */
typedef struct { float e[16]; } t3_mat4;
typedef struct { float e[9]; } t3_mat3;
typedef enum { T3_XYZ, T3_YXZ, T3_ZXY, T3_ZYX, T3_YZX, T3_XZY } t3_euler_order;
typedef struct { float x, y, z; t3_euler_order order; } t3_euler;
typedef struct { t3_vec3 center; float radius; } t3_sphere;
typedef struct { t3_vec3 min, max; } t3_box3;
typedef struct { t3_vec3 normal; float constant; } t3_plane;
typedef struct { t3_plane planes[6]; } t3_frustum;

#define T3_DEG2RAD 0.017453292519943295f

static inline t3_vec3 t3_v3(float x, float y, float z) { t3_vec3 v = { x, y, z }; return v; }
t3_color t3_color_hex(uint32_t hex);
t3_color t3_color_hsl(float h, float s, float l); /* new Color().setHSL(h, s, l) */

/* sin and cos together; faster than sinf + cosf, same float results */
void t3_sincos(float x, float *s, float *c);
t3_vec3 t3_vec3_add(t3_vec3 a, t3_vec3 b);
t3_vec3 t3_vec3_sub(t3_vec3 a, t3_vec3 b);
t3_vec3 t3_vec3_scale(t3_vec3 a, float s);
float t3_vec3_dot(t3_vec3 a, t3_vec3 b);
t3_vec3 t3_vec3_cross(t3_vec3 a, t3_vec3 b);
float t3_vec3_length(t3_vec3 a);
float t3_vec3_length_sq(t3_vec3 a);
float t3_vec3_distance(t3_vec3 a, t3_vec3 b);
float t3_vec3_distance_sq(t3_vec3 a, t3_vec3 b);
t3_vec3 t3_vec3_lerp(t3_vec3 a, t3_vec3 b, float t);
t3_vec3 t3_vec3_normalize(t3_vec3 a);
t3_vec3 t3_vec3_apply_mat4(t3_vec3 v, const t3_mat4 *m);
t3_vec3 t3_vec3_apply_quat(t3_vec3 v, t3_quat q);
t3_vec3 t3_vec3_transform_direction(t3_vec3 v, const t3_mat4 *m);
t3_vec3 t3_mat4_get_position(const t3_mat4 *m);

t3_quat t3_quat_identity(void);
t3_quat t3_quat_from_euler(t3_euler e);
t3_quat t3_quat_from_axis_angle(t3_vec3 axis, float angle);
t3_quat t3_quat_from_rotation_matrix(const t3_mat4 *m);
t3_quat t3_quat_multiply(t3_quat a, t3_quat b);
t3_quat t3_quat_normalize(t3_quat q);
t3_quat t3_quat_slerp(t3_quat a, t3_quat b, float t);
/* Quaternion.slerpFlat on xyzw float arrays (dst may alias a source) */
void t3_quat_slerp_flat(float *dst, const float *a, const float *b, float t);
t3_euler t3_euler_from_quat(t3_quat q, t3_euler_order order);

void t3_mat4_identity(t3_mat4 *m);
void t3_mat4_multiply(t3_mat4 *out, const t3_mat4 *a, const t3_mat4 *b);
void t3_mat4_compose(t3_mat4 *m, t3_vec3 p, t3_quat q, t3_vec3 s);
/* compose from double-precision inputs (as three.js holds them), rounded
 * once into the float elements */
void t3_mat4_compose_d(t3_mat4 *m, const double p[3], const double q[4], const double s[3]);
/* Matrix4.compose / multiplyMatrices / invert on doubles (three.js's numbers) */
void t3_mat4d_compose(double out[16], const double p[3], const double q[4], const double s[3]);
void t3_mat4d_multiply(double out[16], const double a[16], const double b[16]);
void t3_mat4d_invert(double out[16], const double m[16]);
void t3_mat4_decompose(const t3_mat4 *m, t3_vec3 *p, t3_quat *q, t3_vec3 *s);
void t3_mat4_invert(t3_mat4 *out, const t3_mat4 *m);
void t3_mat4_look_at(t3_mat4 *m, t3_vec3 eye, t3_vec3 target, t3_vec3 up);
void t3_mat4_make_perspective(t3_mat4 *m, float left, float right, float top, float bottom, float near, float far);
void t3_mat4_make_orthographic(t3_mat4 *m, float left, float right, float top, float bottom, float near, float far);
float t3_mat4_max_scale_on_axis(const t3_mat4 *m);
void t3_mat3_normal_matrix(t3_mat3 *out, const t3_mat4 *m);

void t3_frustum_from_matrix(t3_frustum *f, const t3_mat4 *m);
bool t3_frustum_intersects_sphere(const t3_frustum *f, t3_sphere s);

/* ── memory ──────────────────────────────────────────────────────── */
/* Route every allocation three.c makes (cgltf's and stb_image's too) through
 * the embedder's functions, with libc's malloc / realloc / free semantics
 * (free(NULL) included). Call before anything else; NULLs restore libc.
 * Memory three.c frees that the embedder allocated (a t3_gltf_read_fn's
 * buffers) must come from the same allocator: t3_malloc / t3_free are it. */
void t3_set_allocator(void *(*malloc_fn)(size_t), void *(*realloc_fn)(void *, size_t), void (*free_fn)(void *));
void *t3_malloc(size_t n);
void t3_free(void *p);

/* ── reference counting ──────────────────────────────────────────── */
void *t3_retain(void *thing);
void t3_release(void *thing);

/* ── textures ────────────────────────────────────────────────────── */
/* texture.colorSpace and renderer.outputColorSpace: NoColorSpace (data),
 * SRGBColorSpace, LinearSRGBColorSpace */
typedef enum { T3_NO_COLOR_SPACE = 0, T3_SRGB_COLOR_SPACE = 1, T3_LINEAR_SRGB_COLOR_SPACE = 2 } t3_color_space;
/* texture.mapping */
typedef enum {
  T3_UV_MAPPING = 300, T3_CUBE_REFLECTION_MAPPING = 301, T3_CUBE_REFRACTION_MAPPING = 302,
  T3_EQUIRECTANGULAR_REFLECTION_MAPPING = 303, T3_EQUIRECTANGULAR_REFRACTION_MAPPING = 304,
  T3_CUBE_UV_REFLECTION_MAPPING = 306, T3_CUBE_UV_REFRACTION_MAPPING = 307
} t3_mapping;
typedef enum { T3_REPEAT = 1000, T3_CLAMP_TO_EDGE = 1001, T3_MIRRORED_REPEAT = 1002 } t3_wrapping;
typedef enum {
  T3_NEAREST = 1003, T3_NEAREST_MIPMAP_NEAREST = 1004, T3_NEAREST_MIPMAP_LINEAR = 1005,
  T3_LINEAR = 1006, T3_LINEAR_MIPMAP_NEAREST = 1007, T3_LINEAR_MIPMAP_LINEAR = 1008
} t3_filter;

/* texture.type / texture.format */
typedef enum {
  T3_UNSIGNED_BYTE_TYPE = 1009, T3_UNSIGNED_SHORT_TYPE = 1012, T3_UNSIGNED_INT_TYPE = 1014, T3_FLOAT_TYPE = 1015,
  T3_HALF_FLOAT_TYPE = 1016, T3_UNSIGNED_INT_248_TYPE = 1020
} t3_texture_type;
typedef enum { T3_RGBA_FORMAT = 1023, T3_DEPTH_FORMAT = 1026, T3_DEPTH_STENCIL_FORMAT = 1027 } t3_texture_format;

typedef struct t3_texture {
  uint32_t _kind; int _refs;
  uint32_t id;
  int width, height;
  /* RGBA texels, owned, row 0 = top (flipY applies): bytes for
   * T3_UNSIGNED_BYTE_TYPE, floats for T3_FLOAT_TYPE, halves (uint16) for
   * T3_HALF_FLOAT_TYPE. NULL for a texture whose contents the GPU makes (a
   * render target's texture, a DepthTexture). */
  uint8_t *pixels;
  t3_wrapping wrap_s, wrap_t;
  t3_filter mag_filter, min_filter;
  bool generate_mipmaps, flip_y;
  int unpack_alignment;     /* 4, or 1 for a DataTexture */
  float anisotropy;         /* texture.anisotropy (1) */
  t3_color_space color_space;   /* texture.colorSpace (NoColorSpace) */
  t3_vec2 offset, repeat, center;
  float rotation;
  uint32_t version;
  void *_gl;                /* renderer-private */
  t3_texture_type type;     /* T3_UNSIGNED_BYTE_TYPE */
  t3_texture_format format; /* T3_RGBA_FORMAT */
  t3_mapping mapping;       /* T3_UV_MAPPING; a CubeTexture's is T3_CUBE_REFLECTION_MAPPING */
  /* a CubeTexture: pixels hold the six faces (px, nx, py, ny, pz, nz) one
   * after another; needs_flip_env_map is CubeTexture._needsFlipEnvMap */
  bool is_cube, needs_flip_env_map;
  /* the GPU makes its contents (a render target's texture, a DepthTexture):
   * r186's isRenderTargetTexture / isDepthTexture, which flip its sampling */
  bool gpu_made;
  void *_wgpu;              /* renderer-private: the WebGPU texture */
} t3_texture;

/* Takes a copy of w*h RGBA8 pixels. three.js Texture defaults: flipY,
 * mipmaps, linear filtering, clamp. */
t3_texture *t3_texture_new(int w, int h, const uint8_t *rgba);
/* DataTexture (RGBAFormat): no flipY, no mipmaps, nearest filtering. */
t3_texture *t3_data_texture_new(int w, int h, const uint8_t *rgba);
/* Decodes PNG/JPEG/etc. with stb_image. NULL on failure. */
t3_texture *t3_texture_load(const char *path);
t3_texture *t3_texture_load_memory(const void *data, size_t len);
/* DataTexture(data, w, h, RGBAFormat, FloatType / HalfFloatType): takes a
 * copy of w*h RGBA floats (FloatType) or halves (HalfFloatType, uint16) */
t3_texture *t3_data_texture_new_typed(int w, int h, const void *rgba, t3_texture_type type);
/* DepthTexture(w, h, type): format DepthFormat (DepthStencilFormat for
 * T3_UNSIGNED_INT_248_TYPE), nearest filtered, no mipmaps; type 0 = the
 * default (UnsignedIntType). Attach it to a render target. */
t3_texture *t3_depth_texture_new(int w, int h, t3_texture_type type);
/* A texture whose GL object the embedder made and filled (a compressed
 * format, a video frame, another library's output): gl_name is a
 * GL_TEXTURE_2D (or, with is_cube, a GL_TEXTURE_CUBE_MAP) of width x height
 * with `levels` mip levels. three.c binds it as it is and never uploads to it
 * or sets its parameters: wrap, filters and mipmaps are whatever the embedder
 * gave the GL object (the t3_texture's own wrap / filter / flip_y fields are
 * ignored; offset / repeat / rotation / center still make the uv transform).
 * color_space: leave it T3_NO_COLOR_SPACE for an sRGB internal format (GL
 * decodes on sampling) or linear data, set T3_SRGB_COLOR_SPACE for sRGB data
 * in a linear format. owns_gl: true, three.c deletes the GL texture when the
 * t3_texture is released; false, the embedder deletes it, after releasing the
 * t3_texture (and after the last render that used it). Needs the GL context. */
t3_texture *t3_texture_from_gl(unsigned gl_name, int width, int height, int levels, bool is_cube, bool owns_gl);
/* the same, into an existing texture (materials using it see the new
 * image): its previous GL object is freed (if three.c owned it) and its
 * pixels dropped; other fields are kept */
void t3_texture_adopt_gl(t3_texture *t, unsigned gl_name, int width, int height, int levels, bool owns_gl);
/* exchange two textures' GL objects (with their sizes and pixels), e.g. to
 * hot-reload an image under the t3_texture materials already hold */
void t3_texture_swap_gl(t3_texture *a, t3_texture *b);
/* The WebGPU counterparts: an embedder's WGPUTexture (a 2D texture, or a cube
 * of 6 layers made with a cube textureBindingViewDimension for compatibility
 * mode) with `levels` mip levels, sampled by three.c and never written. view
 * is the WGPUTextureView to sample (a cube view for a cube), or NULL for
 * three.c to make one. Unlike GL, WebGPU samplers are separate objects: the
 * t3_texture's wrap and filter fields choose them (a texture with one level
 * gets T3_LINEAR minification). color_space as for t3_texture_from_gl. owns:
 * true, three.c releases the texture and view when the t3_texture is
 * released; false, the embedder releases them after that (and after the
 * last render that used it). Returns NULL from a build without T3_WGPU. A
 * cube adopted this way works as scene.background, envMap and as the source
 * of t3_pmrem_from_cubemap. */
t3_texture *t3_texture_from_wgpu(void *wgpu_texture, void *wgpu_view, int width, int height, int levels, bool is_cube, bool owns);
void t3_texture_adopt_wgpu(t3_texture *t, void *wgpu_texture, void *wgpu_view, int width, int height, int levels, bool owns);
/* CubeTexture from six size x size RGBA8 faces (px, nx, py, ny, pz, nz; each
 * row 0 = top, no flipY): mipmapped, linear */
t3_texture *t3_cube_texture_new(int size, const uint8_t *const faces[6]);

/* ── render targets ──────────────────────────────────────────────── */
/* RenderTarget's options, with three.js's defaults from
 * t3_render_target_options_default(): RGBA, UnsignedByteType, linear
 * filtering without mipmaps, clamp, a depth buffer, no stencil, one sample. */
typedef struct {
  t3_wrapping wrap_s, wrap_t;
  t3_filter mag_filter, min_filter;
  t3_texture_format format;
  t3_texture_type type;
  float anisotropy;
  t3_color_space color_space;
  bool generate_mipmaps, depth_buffer, stencil_buffer;
  t3_texture *depth_texture;  /* retained by the target; NULL = none */
  int samples;                /* > 1: multisampled (resolved after each render) */
} t3_render_target_options;

typedef struct t3_render_target {
  uint32_t _kind; int _refs;
  int width, height;
  t3_texture *texture;        /* owned: read it as a material map */
  t3_texture *depth_texture;  /* retained, or NULL */
  bool depth_buffer, stencil_buffer;
  int samples;                /* 0, or MSAA samples (resolved after each render) */
  int viewport[4], scissor[4]; /* x, y, w, h (bottom left), as three.js */
  bool scissor_test;
  void *_gl;                  /* renderer-private */
  void *_wgpu;                /* renderer-private (WebGPU) */
} t3_render_target;

t3_render_target_options t3_render_target_options_default(void);
/* new RenderTarget(w, h, options); options NULL = the defaults */
t3_render_target *t3_render_target_new(int w, int h, const t3_render_target_options *options);
/* RenderTarget.setSize: resizes (GPU storage is remade on next use) and
 * resets viewport and scissor */
void t3_render_target_set_size(t3_render_target *rt, int w, int h);

/* ── geometry ────────────────────────────────────────────────────── */
typedef enum { T3_FLOAT32, T3_UINT16, T3_UINT32 } t3_attr_type;
typedef enum { T3_STATIC_DRAW = 35044, T3_DYNAMIC_DRAW = 35048 } t3_usage;

typedef struct t3_attribute {
  uint32_t _kind; int _refs;
  t3_attr_type type;
  void *array;              /* owned */
  int item_size, count;
  bool normalized;
  t3_usage usage;
  uint32_t version;
  void *_gl;
  void *_wgpu;              /* renderer-private: the WebGPU buffer */
} t3_attribute;

t3_attribute *t3_attribute_new(t3_attr_type type, const void *data, int count, int item_size);
static inline float *t3_attribute_f32(t3_attribute *a) { return (float *)a->array; }

typedef enum {
  T3_ATTR_POSITION, T3_ATTR_NORMAL, T3_ATTR_UV, T3_ATTR_COLOR, T3_ATTR_TANGENT, T3_ATTR_UV1,
  T3_ATTR_INSTANCE_MATRIX, T3_ATTR_INSTANCE_COLOR,
  T3_ATTR_SKIN_INDEX, T3_ATTR_SKIN_WEIGHT,
  T3_ATTR_COUNT
} t3_attr_slot;

typedef struct { int start, count, material_index; } t3_group;

typedef struct t3_geometry {
  uint32_t _kind; int _refs;
  uint32_t id;
  t3_attribute *attributes[T3_ATTR_COUNT];
  t3_attribute *index;
  t3_group *groups; int group_count;
  int draw_start, draw_count; /* draw range; count < 0 = all */
  bool has_bounding_sphere, has_bounding_box;
  t3_sphere bounding_sphere;
  t3_box3 bounding_box;
  /* geometry.morphAttributes.position / .normal (either may be NULL), and
   * morphTargetsRelative */
  t3_attribute **morph_position, **morph_normal;
  int morph_count;
  bool morph_relative;
  uint32_t version;
  void *_gl;
  void *_wgpu;              /* renderer-private (WebGPU) */
  /* attributes by name beyond the fixed slots, for ShaderMaterials
   * (geometry.setAttribute( 'size', ... )) */
  struct { char name[48]; t3_attribute *attribute; } *named; int named_count;
} t3_geometry;

t3_geometry *t3_geometry_new(void);
void t3_geometry_set_attribute(t3_geometry *g, t3_attr_slot slot, t3_attribute *a);
void t3_geometry_set_index(t3_geometry *g, t3_attribute *index);
/* geometry.setAttribute(name, a) for a name outside the fixed slots (a
 * ShaderMaterial's own attribute); a NULL a deletes it */
void t3_geometry_set_named_attribute(t3_geometry *g, const char *name, t3_attribute *a);
t3_attribute *t3_geometry_get_named_attribute(const t3_geometry *g, const char *name);
void t3_geometry_add_group(t3_geometry *g, int start, int count, int material_index);
/* Takes references to count position (and normal, may be NULL) attributes. */
void t3_geometry_set_morph_attributes(t3_geometry *g, t3_attribute **position, t3_attribute **normal, int count,
                                      bool relative);
void t3_geometry_compute_bounding_box(t3_geometry *g);
void t3_geometry_compute_bounding_sphere(t3_geometry *g);
void t3_geometry_compute_vertex_normals(t3_geometry *g);

t3_geometry *t3_box_geometry_new(float w, float h, float d, int ws, int hs, int ds);
t3_geometry *t3_plane_geometry_new(float w, float h, int ws, int hs);
t3_geometry *t3_sphere_geometry_new(float radius, int ws, int hs);
t3_geometry *t3_sphere_geometry_new_ex(float radius, int ws, int hs, float phi_start, float phi_length,
                                       float theta_start, float theta_length);
t3_geometry *t3_cylinder_geometry_new(float rtop, float rbottom, float h, int rs, int hs, bool open_ended);
t3_geometry *t3_torus_geometry_new(float radius, float tube, int rs, int ts, float arc);
t3_geometry *t3_circle_geometry_new(float radius, int segments);
t3_geometry *t3_polyhedron_geometry_new(const float *vertices, const int *indices, int index_count, float radius,
                                        int detail);
t3_geometry *t3_icosahedron_geometry_new(float radius, int detail);
t3_geometry *t3_octahedron_geometry_new(float radius, int detail);
t3_geometry *t3_tetrahedron_geometry_new(float radius, int detail);
t3_geometry *t3_dodecahedron_geometry_new(float radius, int detail);
/* LatheGeometry(points, segments, phiStart, phiLength) */
t3_geometry *t3_lathe_geometry_new(const t3_vec2 *points, int count, int segments, float phi_start, float phi_length);
t3_geometry *t3_torus_knot_geometry_new(float radius, float tube, int tubular_segments, int radial_segments, float p,
                                        float q);
/* A new geometry (or the same one, retained, if already non-indexed). */
t3_geometry *t3_geometry_to_non_indexed(const t3_geometry *g);
void t3_geometry_normalize_normals(t3_geometry *g);
t3_geometry *t3_cone_geometry_new(float radius, float h, int rs, int hs, bool open_ended);
t3_geometry *t3_ring_geometry_new(float inner, float outer, int theta_segments, int phi_segments);

/* ── materials ───────────────────────────────────────────────────── */
typedef enum {
  T3_MESH_BASIC_MATERIAL, T3_MESH_LAMBERT_MATERIAL, T3_MESH_PHONG_MATERIAL,
  T3_MESH_STANDARD_MATERIAL, T3_MESH_NORMAL_MATERIAL,
  T3_LINE_BASIC_MATERIAL, T3_SPRITE_MATERIAL, T3_POINTS_MATERIAL, T3_MESH_PHYSICAL_MATERIAL
} t3_material_type;
typedef enum { T3_FRONT_SIDE = 0, T3_BACK_SIDE = 1, T3_DOUBLE_SIDE = 2 } t3_side;
/* three.js's constants for material.blending, blendEquation, blendSrc /
 * blendDst and depthFunc (the material fields are ints; the alpha ones -1 = null) */
typedef enum {
  T3_NO_BLENDING = 0, T3_NORMAL_BLENDING = 1, T3_ADDITIVE_BLENDING = 2, T3_SUBTRACTIVE_BLENDING = 3,
  T3_MULTIPLY_BLENDING = 4, T3_CUSTOM_BLENDING = 5
} t3_blending;
typedef enum {
  T3_ADD_EQUATION = 100, T3_SUBTRACT_EQUATION = 101, T3_REVERSE_SUBTRACT_EQUATION = 102, T3_MIN_EQUATION = 103,
  T3_MAX_EQUATION = 104
} t3_blend_equation;
typedef enum {
  T3_ZERO_FACTOR = 200, T3_ONE_FACTOR = 201, T3_SRC_COLOR_FACTOR = 202, T3_ONE_MINUS_SRC_COLOR_FACTOR = 203,
  T3_SRC_ALPHA_FACTOR = 204, T3_ONE_MINUS_SRC_ALPHA_FACTOR = 205, T3_DST_ALPHA_FACTOR = 206,
  T3_ONE_MINUS_DST_ALPHA_FACTOR = 207, T3_DST_COLOR_FACTOR = 208, T3_ONE_MINUS_DST_COLOR_FACTOR = 209,
  T3_SRC_ALPHA_SATURATE_FACTOR = 210
} t3_blend_factor;
typedef enum {
  T3_NEVER_DEPTH = 0, T3_ALWAYS_DEPTH = 1, T3_LESS_DEPTH = 2, T3_LESS_EQUAL_DEPTH = 3, T3_EQUAL_DEPTH = 4,
  T3_GREATER_EQUAL_DEPTH = 5, T3_GREATER_DEPTH = 6, T3_NOT_EQUAL_DEPTH = 7
} t3_depth_func;

typedef struct t3_material {
  uint32_t _kind; int _refs;
  uint32_t id;
  t3_material_type type;
  t3_color color, emissive, specular;
  float opacity, shininess, roughness, metalness, emissive_intensity;
  t3_texture *map;          /* retained; set with t3_material_set_map / t3_material_set_texture */
  t3_texture *normal_map, *ao_map, *emissive_map, *roughness_map, *metalness_map, *alpha_map;
  t3_vec2 normal_scale;
  float ao_map_intensity;
  t3_side side;
  bool transparent, depth_test, depth_write, vertex_colors, flat_shading, wireframe, visible, fog;
  float alpha_test;
  /* material.blending (NoBlending 0, NormalBlending 1, AdditiveBlending 2,
   * SubtractiveBlending 3, MultiplyBlending 4, CustomBlending 5),
   * premultipliedAlpha, the CustomBlending factors and equations (the alpha
   * ones -1 = null) and depthFunc (NeverDepth 0 .. NotEqualDepth 7,
   * LessEqualDepth 3) */
  int blending;
  bool premultiplied_alpha;
  int blend_src, blend_dst, blend_equation, blend_src_alpha, blend_dst_alpha, blend_equation_alpha;
  int depth_func;
  int shadow_side;          /* -1 = null (the opposite of side) */
  uint32_t version;
  bool tone_mapped;         /* material.toneMapped (true) */
  bool color_write, dithering; /* material.colorWrite (true), material.dithering */
  /* PointsMaterial.size, sizeAttenuation (Points and Sprite, true) and
   * SpriteMaterial.rotation */
  float size;
  bool size_attenuation;
  float rotation;
  /* material.envMap (a CubeTexture or a PMREM target's texture; set with
   * t3_material_set_texture( T3_ENV_MAP )), envMapIntensity (Standard),
   * reflectivity, refractionRatio and combine (Basic / Lambert / Phong:
   * T3_MULTIPLY_OPERATION, T3_MIX_OPERATION, T3_ADD_OPERATION) */
  t3_texture *env_map;
  float env_map_intensity, reflectivity, refraction_ratio;
  int combine;
  t3_euler env_map_rotation;   /* material.envMapRotation */
  /* material.lightMap (retained; t3_material_set_texture( T3_LIGHT_MAP )) and
   * lightMapIntensity (1): irradiance added to the indirect diffuse light */
  t3_texture *light_map;
  float light_map_intensity;
  /* MeshPhysicalMaterial (T3_MESH_PHYSICAL_MATERIAL; the Standard fields
   * apply too). As in r186, clearcoat, sheen, iridescence, anisotropy and
   * transmission each turn on when their strength is above 0. */
  float ior, specular_intensity;            /* 1.5, 1 */
  t3_color specular_color;                  /* (1, 1, 1) */
  float clearcoat, clearcoat_roughness;     /* 0, 0 */
  t3_texture *clearcoat_normal_map;         /* retained; t3_material_set_texture( T3_CLEARCOAT_NORMAL_MAP ) */
  t3_vec2 clearcoat_normal_scale;           /* (1, 1) */
  float sheen, sheen_roughness;             /* 0, 1 */
  t3_color sheen_color;                     /* (0, 0, 0) */
  float iridescence, iridescence_ior;       /* 0, 1.3 */
  float iridescence_thickness_range[2];     /* 100, 400 (nm); with no thickness map the maximum is used */
  float anisotropy, anisotropy_rotation;    /* 0, 0 */
  float transmission, thickness, attenuation_distance;   /* 0, 0, INFINITY */
  t3_color attenuation_color;               /* (1, 1, 1) */
} t3_material;
enum { T3_MULTIPLY_OPERATION = 0, T3_MIX_OPERATION = 1, T3_ADD_OPERATION = 2 };

t3_material *t3_material_new(t3_material_type type);
t3_material *t3_mesh_basic_material_new(uint32_t color);
/* LineBasicMaterial; linewidth is always 1 */
t3_material *t3_line_basic_material_new(uint32_t color);
t3_material *t3_mesh_lambert_material_new(uint32_t color);
t3_material *t3_mesh_phong_material_new(uint32_t color);
t3_material *t3_mesh_standard_material_new(uint32_t color);
t3_material *t3_mesh_physical_material_new(uint32_t color);
t3_material *t3_mesh_normal_material_new(void);
void t3_material_set_map(t3_material *m, t3_texture *map);
typedef enum {
  T3_MAP, T3_NORMAL_MAP, T3_AO_MAP, T3_EMISSIVE_MAP, T3_ROUGHNESS_MAP, T3_METALNESS_MAP, T3_ALPHA_MAP,
  T3_ENV_MAP, T3_LIGHT_MAP, T3_CLEARCOAT_NORMAL_MAP
} t3_map_slot;
void t3_material_set_texture(t3_material *m, t3_map_slot slot, t3_texture *t);
/* Material.clone: a new material with the same values, maps retained */
t3_material *t3_material_clone(const t3_material *m);

/* ── objects ─────────────────────────────────────────────────────── */
typedef enum {
  T3_OBJECT3D, T3_GROUP, T3_SCENE, T3_MESH, T3_INSTANCED_MESH, T3_SKINNED_MESH, T3_BONE,
  T3_PERSPECTIVE_CAMERA, T3_ORTHOGRAPHIC_CAMERA,
  T3_AMBIENT_LIGHT, T3_HEMISPHERE_LIGHT, T3_DIRECTIONAL_LIGHT, T3_POINT_LIGHT, T3_SPOT_LIGHT,
  T3_LINE, T3_LINE_SEGMENTS, T3_LINE_LOOP,
  T3_SPRITE, T3_POINTS, T3_LOD
} t3_object_type;

typedef struct t3_object t3_object;
struct t3_object {
  uint32_t _kind; int _refs;
  uint32_t id;
  t3_object_type type;
  const char *name;         /* not owned */
  t3_object *parent;
  t3_object **children; int child_count, _child_cap;
  t3_vec3 position, scale, up;
  t3_euler rotation;
  t3_quat quaternion;
  t3_mat4 matrix, matrix_world;
  t3_mat4 _model_view;       /* renderer scratch, like three's modelViewMatrix */
  t3_mat3 _normal_matrix;
  bool matrix_auto_update, matrix_world_needs_update;
  bool visible, frustum_culled, cast_shadow, receive_shadow;
  uint32_t layers;          /* object.layers.mask (1): drawn when it shares a bit with the camera's */
  /* raycasting leaves this object to the embedder (its raycast is not
   * Mesh.raycast): t3_raycaster_intersect_objects_ex hands it back */
  bool raycast_custom;
  int render_order;
  void *user_data;
  void (*on_destroy)(t3_object *);
  /* a subclass's updateMatrixWorld override: runs first, then the default */
  void (*on_update_matrix_world)(t3_object *);
  /* internal: bumped whenever matrix_world is recomputed, and the world
   * bounding sphere cached against it (meshes; see t3__world_sphere) */
  uint32_t _world_version, _ws_world, _ws_bounds;
  const void *_ws_geometry;
  float _ws[4];
  /* internal: position, quaternion, scale as last composed into matrix by
   * the scene update (bitwise), so an unchanged object skips the work */
  float _composed[10]; bool _composed_valid;
  /* matrix and matrix_world in double precision, as three.js holds them
   * (T3_F64_MATH builds keep these; the float ones are their roundings) */
  double _md[16], _mwd[16];
  /* internal: the parent's _world_version when matrix_world was last
   * computed (_world_computed), so a forced update skips what cannot have
   * changed: the same local matrix under the same parent world */
  uint32_t _parent_wv; bool _world_computed;
  /* the embedder owns matrix_world (t3_object_set_matrix_world): three.c's
   * matrix updates leave it alone (its children still update from it) */
  bool matrix_world_external;
};

typedef t3_object t3_group_object;

typedef enum { T3_FOG_NONE, T3_FOG_LINEAR, T3_FOG_EXP2 } t3_fog_type;
typedef struct { t3_fog_type type; t3_color color; float near, far, density; } t3_fog;

typedef struct t3_scene {
  t3_object base;
  bool has_background; t3_color background;
  t3_fog fog;               /* scene.fog: Fog (linear) or FogExp2 */
  bool auto_update;
  /* scene.background as a texture and scene.environment (retained; set them
   * with the calls below) */
  struct t3_texture *background_texture, *environment;
  /* r186: scene.backgroundIntensity (1), backgroundBlurriness (0: a cube
   * background drawn sharp; above 0 through its PMREM), backgroundRotation,
   * environmentIntensity (1), environmentRotation (the generated renderer) */
  float background_intensity, background_blurriness, environment_intensity;
  t3_euler background_rotation, environment_rotation;
} t3_scene;

static inline t3_fog t3_fog_linear(uint32_t color, float near, float far) {
  t3_fog f = { T3_FOG_LINEAR, t3_color_hex(color), near, far, 0 };
  return f;
}
static inline t3_fog t3_fog_exp2(uint32_t color, float density) {
  t3_fog f = { T3_FOG_EXP2, t3_color_hex(color), 0, 0, density };
  return f;
}

typedef struct t3_mesh {
  t3_object base;
  t3_geometry *geometry;          /* retained */
  t3_material **materials; int material_count; /* retained; one, or one per group */
  float *morph_influences;        /* mesh.morphTargetInfluences (t3_mesh_update_morph_targets) */
  int morph_influence_count;
} t3_mesh;

/* Skeleton: bones (retained) with their inverse bind matrices; the bone
 * matrices go to the GPU as a uniform / storage buffer, as r186's skinning node. */
typedef struct t3_skeleton {
  uint32_t _kind; int _refs; void (*_destroy)(void *);   /* misc header (see core.c) */
  t3_object **bones;
  t3_mat4 *bone_inverses;
  int bone_count;
  float *bone_matrices;           /* bone_texture_size^2 * 4 floats */
  int bone_texture_size;
  unsigned frame;                 /* renderer frame of the last update */
  uint32_t version;
  void *_gl;
} t3_skeleton;

typedef enum { T3_BIND_ATTACHED, T3_BIND_DETACHED } t3_bind_mode;

typedef struct t3_skinned_mesh {
  t3_mesh mesh;
  t3_skeleton *skeleton;          /* retained */
  t3_bind_mode bind_mode;
  t3_mat4 bind_matrix, bind_matrix_inverse;
} t3_skinned_mesh;

typedef struct t3_instanced_mesh {
  t3_mesh mesh;
  int count;
  t3_attribute *instance_matrix;  /* count x 16 floats */
  t3_attribute *instance_color;   /* NULL until a color is set */
  void *_gl;
  void *_cull;                    /* renderer-private: culled, sorted instances */
} t3_instanced_mesh;

typedef struct t3_camera {
  t3_object base;
  t3_mat4 matrix_world_inverse, projection_matrix, projection_matrix_inverse;
  double _mwid[16];         /* matrix_world_inverse in double (T3_F64_MATH) */
  float fov, aspect, near, far, zoom;          /* perspective */
  float left, right, top, bottom;              /* orthographic */
} t3_camera;

typedef struct t3_light_shadow {
  t3_camera *camera;            /* owned */
  float bias, normal_bias, radius, focus;
  int map_width, map_height;
  t3_mat4 matrix;
  void *_gl;
  float intensity;              /* LightShadow.intensity (1; r186): how dark the shadow is */
} t3_light_shadow;

typedef struct t3_light {
  t3_object base;
  t3_color color, ground_color;
  float intensity, distance, decay, angle, penumbra;
  t3_object *target;            /* owned; three.js's light.target */
  t3_light_shadow *shadow;      /* directional / spot / point */
} t3_light;

t3_object *t3_object_new(void);
t3_object *t3_group_new(void);
t3_scene *t3_scene_new(void);
/* scene.background = a CubeTexture (or a PMREM texture), or a 2D texture
 * drawn behind everything; NULL = the colour background (if any). */
void t3_scene_set_background_texture(t3_scene *s, t3_texture *t);
/* scene.environment: the envMap of every MeshStandardMaterial without its own */
void t3_scene_set_environment(t3_scene *s, t3_texture *t);
/* Line / LineSegments / LineLoop: a t3_mesh drawn as GL_LINE_STRIP / GL_LINES
 * / GL_LINE_LOOP */
t3_mesh *t3_line_new(t3_geometry *g, t3_material *m);
t3_mesh *t3_line_segments_new(t3_geometry *g, t3_material *m);
t3_mesh *t3_line_loop_new(t3_geometry *g, t3_material *m);
/* new THREE.Points(geometry, material): a t3_mesh drawn as GL_POINTS */
t3_mesh *t3_points_new(t3_geometry *g, t3_material *m);
/* PointsMaterial({ color }) (size 1, sizeAttenuation) and SpriteMaterial({
 * color }) (transparent, sizeAttenuation, rotation 0) */
t3_material *t3_points_material_new(uint32_t color);
t3_material *t3_sprite_material_new(uint32_t color);
/* new THREE.Sprite(material): a unit quad facing the camera, scaled by the
 * object's scale; center is sprite.center (0.5, 0.5) */
typedef struct t3_sprite {
  t3_mesh mesh;             /* the shared quad and the material */
  t3_vec2 center;
} t3_sprite;
t3_sprite *t3_sprite_new(t3_material *m);
/* THREE.LOD: levels sorted by distance; each render shows the level for the
 * camera's distance (lod.update) unless auto_update is off */
typedef struct t3_lod {
  t3_object base;
  struct { t3_object *object; float distance; } *levels; /* objects are children */
  int level_count, _level_cap;
  bool auto_update;         /* true */
  int current_level;        /* lod.getCurrentLevel() */
} t3_lod;
t3_lod *t3_lod_new(void);
/* lod.addLevel(object, distance): adds object as a child */
void t3_lod_add_level(t3_lod *lod, void *object, float distance);
/* lod.getObjectForDistance and lod.update(camera) */
t3_object *t3_lod_get_object_for_distance(const t3_lod *lod, float distance);
void t3_lod_update(t3_lod *lod, const struct t3_camera *camera);
/* GridHelper(size, divisions, color1, color2): LineSegments on the XZ plane */
t3_mesh *t3_grid_helper_new(float size, int divisions, uint32_t color1, uint32_t color2);
/* SkeletonHelper(object): LineSegments from each bone (with a bone parent) to
 * its parent, recomputed in updateMatrixWorld as three.js does */
t3_mesh *t3_skeleton_helper_new(t3_object *object);
t3_mesh *t3_mesh_new(t3_geometry *g, t3_material *m);
t3_mesh *t3_mesh_new_multi(t3_geometry *g, t3_material **ms, int count);
t3_instanced_mesh *t3_instanced_mesh_new(t3_geometry *g, t3_material *m, int count);
t3_skinned_mesh *t3_skinned_mesh_new(t3_geometry *g, t3_material *m);
/* SkinnedMesh.bind(skeleton, bindMatrix); bind_matrix NULL = the mesh's
 * current world matrix after posing the skeleton (three.js's default) */
void t3_skinned_mesh_bind(t3_skinned_mesh *sm, t3_skeleton *sk, const t3_mat4 *bind_matrix);
t3_object *t3_bone_new(void);
/* Skeleton(bones, boneInverses); inverses NULL = computed from the bones'
 * current world matrices (Skeleton.calculateInverses) */
t3_skeleton *t3_skeleton_new(t3_object *const *bones, int count, const t3_mat4 *inverses);
/* Skeleton.update: bone world x inverse into bone_matrices */
void t3_skeleton_update(t3_skeleton *sk);
/* Mesh.updateMorphTargets: one influence per morph target of the geometry */
void t3_mesh_update_morph_targets(t3_mesh *m);
/* Object3D.getObjectByName, depth first */
t3_object *t3_object_get_by_name(t3_object *root, const char *name);
void t3_instanced_mesh_set_matrix_at(t3_instanced_mesh *im, int i, const t3_mat4 *m);
void t3_instanced_mesh_get_matrix_at(t3_instanced_mesh *im, int i, t3_mat4 *m);
void t3_instanced_mesh_set_color_at(t3_instanced_mesh *im, int i, t3_color c);
t3_camera *t3_perspective_camera_new(float fov, float aspect, float near, float far);
t3_camera *t3_orthographic_camera_new(float left, float right, float top, float bottom, float near, float far);
void t3_camera_update_projection_matrix(t3_camera *c);
t3_light *t3_ambient_light_new(uint32_t color, float intensity);
t3_light *t3_hemisphere_light_new(uint32_t sky, uint32_t ground, float intensity);
t3_light *t3_directional_light_new(uint32_t color, float intensity);
t3_light *t3_point_light_new(uint32_t color, float intensity, float distance, float decay);
t3_light *t3_spot_light_new(uint32_t color, float intensity, float distance, float angle, float penumbra, float decay);

void t3_object_add(t3_object *parent, void *child);
void t3_object_remove(t3_object *parent, void *child);
void t3_object_set_position(void *o, float x, float y, float z);
void t3_object_set_rotation(void *o, float x, float y, float z);
void t3_object_set_quaternion(void *o, t3_quat q);
void t3_object_rotation_changed(void *o);
void t3_object_set_scale(void *o, float x, float y, float z);
/* object.layers.mask, and whether raycasting is left to the embedder (set
 * these through here: raycasters cache their candidates against them) */
void t3_object_set_layers(void *o, uint32_t mask);
void t3_object_set_raycast_custom(void *o, bool custom);
/* The local transform from double-precision values (an embedder whose
 * numbers are doubles, as three.js's are): stores them, composes the local
 * matrix in double precision and records it as composed, so the scene update
 * keeps this matrix rather than recomposing from the float copies. */
void t3_object_set_trs_d(void *o, const double p[3], const double q[4], const double s[3]);
/* a hand-set local matrix (matrixAutoUpdate = false) in double precision */
void t3_object_set_matrix_d(void *o, const double e[16]);
/* An embedder that keeps its own world transforms writes them here: sets
 * matrix_world (and the double copy for T3_F64_MATH), marks it the
 * embedder's (matrix_world_external: no updateMatrixWorld recomputes it; clear
 * the flag to hand it back) and invalidates what depends on it: bounds,
 * culling, children's worlds, a camera's inverse. */
void t3_object_set_matrix_world(void *o, const t3_mat4 *m);
void t3_object_set_matrix_world_d(void *o, const double e[16]);
void t3_object_look_at(void *o, float x, float y, float z);
void t3_object_update_matrix(void *o);
void t3_object_update_matrix_world(void *o, bool force);
void t3_object_update_world_matrix(void *o, bool update_parents, bool update_children);
/* a counter bumped for every world matrix recomputed (by the calls above or
 * a render): unchanged across a call means no world matrix changed */
uint32_t t3_world_recompute_count(void);
t3_vec3 t3_object_get_world_position(void *o);
void t3_object_traverse(void *o, void (*fn)(t3_object *, void *), void *ctx);
#define T3_OBJ(p) ((t3_object *)(p))

/* ── curves ──────────────────────────────────────────────────────── */
typedef enum {
  T3_CENTRIPETAL, T3_CHORDAL, T3_CATMULLROM,   /* CatmullRomCurve3 curveType */
  T3_CUBIC_BEZIER_CURVE3, T3_ELLIPSE_CURVE
} t3_curve_kind;

typedef struct t3_curve {
  uint32_t _kind; int _refs;
  t3_curve_kind kind;
  t3_vec3 *points; int point_count; bool closed; float tension;   /* CatmullRom */
  t3_vec3 v[4];                                                     /* CubicBezier */
  float ax, ay, xr, yr, start, end, rotation; bool clockwise;       /* Ellipse */
  int arc_length_divisions;
  float *lengths; int lengths_n; bool needs_update;
} t3_curve;

t3_curve *t3_catmull_rom_curve3_new(const t3_vec3 *points, int n, bool closed, t3_curve_kind type, float tension);
t3_curve *t3_cubic_bezier_curve3_new(t3_vec3 v0, t3_vec3 v1, t3_vec3 v2, t3_vec3 v3);
t3_curve *t3_ellipse_curve_new(float ax, float ay, float xr, float yr, float start_angle, float end_angle,
                               bool clockwise, float rotation);
t3_vec3 t3_curve_get_point(const t3_curve *c, float t);
t3_vec3 t3_curve_get_point_at(t3_curve *c, float u);
float t3_curve_get_length(t3_curve *c);
const float *t3_curve_get_lengths(t3_curve *c, int *count);
void t3_curve_update_arc_lengths(t3_curve *c);
float t3_curve_u_to_t(t3_curve *c, float u);

/* ── loaders ───────────────────────────────────────────────────────── */
/* BufferGeometryLoader.parse on the JSON text; NULL if it is not a
 * BufferGeometry document with a position attribute. */
/* BufferGeometry.applyMatrix4 / scale: positions, normals (normal matrix), tangents */
void t3_geometry_apply_matrix4(t3_geometry *g, const t3_mat4 *m);
void t3_geometry_scale(t3_geometry *g, float x, float y, float z);
void t3_geometry_rotate_x(t3_geometry *g, float angle);
void t3_geometry_rotate_y(t3_geometry *g, float angle);
void t3_geometry_rotate_z(t3_geometry *g, float angle);
void t3_geometry_translate(t3_geometry *g, float x, float y, float z);
t3_geometry *t3_buffer_geometry_loader_parse(const char *json, size_t len);

/* GLTFLoader (glTF 2.0, .gltf or .glb). gltf->scene is three.js's gltf.scene
 * (a Group); gltf->animations its clips. The objects use names the t3_gltf
 * owns: keep it alive (retained) while they are in use. read fetches an
 * external file (buffer or image) by its uri, returning malloc'd bytes, or
 * NULL; it may be NULL for a self-contained .glb. */
/* (the bytes returned are freed with t3_free: allocate them with t3_malloc) */
typedef void *(*t3_gltf_read_fn)(void *ctx, const char *uri, size_t *len);
typedef struct t3_gltf {
  uint32_t _kind; int _refs; void (*_destroy)(void *);
  t3_object *scene;
  struct t3_animation_clip **animations;
  int animation_count;
  char **names; int name_count;
} t3_gltf;
t3_gltf *t3_gltf_parse(const void *data, size_t len, t3_gltf_read_fn read, void *ctx);

/* ── raycasting ──────────────────────────────────────────────────── */
typedef struct { t3_vec3 origin, direction; } t3_ray;

typedef struct {
  float distance;
  t3_vec3 point;            /* world space */
  t3_object *object;
  int face_index;
  uint32_t a, b, c;         /* the face's vertex indices */
  int material_index;       /* face.materialIndex: the group's, else 0 */
} t3_intersection;

typedef struct t3_raycaster {
  uint32_t _kind; int _refs; void (*_destroy)(void *);
  t3_ray ray;
  float near, far;
  t3_intersection *hits; int hit_count, hit_cap;
  void *_batch;             /* internal: flattened candidates (raycaster.c) */
  uint32_t layers;          /* raycaster.layers.mask (1) */
} t3_raycaster;

t3_raycaster *t3_raycaster_new(void);
void t3_raycaster_set(t3_raycaster *rc, t3_vec3 origin, t3_vec3 direction);
void t3_raycaster_set_from_camera(t3_raycaster *rc, float ndc_x, float ndc_y, const t3_camera *camera);
/* Returns the number of hits, nearest first; *out points into the
 * raycaster's own array, valid until its next query. */
int t3_raycaster_intersect_objects(t3_raycaster *rc, t3_object *const *objects, int n, bool recursive,
                                   const t3_intersection **out);
int t3_raycaster_intersect_object(t3_raycaster *rc, t3_object *object, bool recursive, const t3_intersection **out);
/* intersectObjects, handing each object three.c does not raycast itself
 * (anything but a plain Mesh, or raycast_custom) to other(object, ctx) in
 * traversal order, for the embedder to test (its hits are its own to merge). */
int t3_raycaster_intersect_objects_ex(t3_raycaster *rc, t3_object *const *objects, int n, bool recursive,
                                      const t3_intersection **out, void (*other)(t3_object *, void *), void *ctx);

/* ── animation ───────────────────────────────────────────────────── */
typedef enum { T3_TRACK_VECTOR, T3_TRACK_QUATERNION, T3_TRACK_NUMBER } t3_track_type;
typedef enum {
  T3_PROP_POSITION, T3_PROP_QUATERNION, T3_PROP_SCALE, T3_PROP_RENDER_ORDER, T3_PROP_VISIBLE, T3_PROP_MORPH_INFLUENCES
} t3_track_property;
typedef enum { T3_INTERPOLATE_LINEAR, T3_INTERPOLATE_DISCRETE, T3_INTERPOLATE_CUBIC_SPLINE_GLTF } t3_interpolation;

typedef struct t3_keyframe_track {
  t3_track_type type;
  char name[128];         /* "nodeName.property", as three.js */
  float *times, *values;
  int count, value_size;  /* values per key (x3 in, value, out for glTF CUBICSPLINE) */
  t3_interpolation interpolation;
} t3_keyframe_track;

typedef struct t3_animation_clip {
  uint32_t _kind; int _refs; void (*_destroy)(void *);
  char name[64];
  float duration;
  t3_keyframe_track **tracks; int track_count;
} t3_animation_clip;

typedef struct { t3_object *target; t3_track_property property; } t3_track_binding;
const char *t3_property_binding_sanitize(const char *name, char *out, size_t cap);

typedef struct t3_animation_action {
  t3_animation_clip *clip;
  float time, time_scale, weight; /* action.time, .timeScale, .weight */
  int loop_count;
  bool playing, enabled, paused;  /* play()/stop(), .enabled, .paused */
  int *props;                     /* per track: its PropertyMixer in the mixer, -1 = unbound */
  int *cache;
} t3_animation_action;

/* PropertyMixer: one per animated (object, property), shared by every action
 * that animates it; renderer-independent mixer internals */
typedef struct t3_property_mixer t3_property_mixer;

typedef struct t3_animation_mixer {
  uint32_t _kind; int _refs; void (*_destroy)(void *);
  t3_object *root;
  t3_animation_action **actions; int action_count;
  t3_property_mixer *props; int prop_count;
  float time, time_scale;         /* mixer.time, mixer.timeScale */
} t3_animation_mixer;

/* Copies times / values. new VectorKeyframeTrack(name, times, values) etc. */
t3_keyframe_track *t3_keyframe_track_new(t3_track_type type, const char *name, const float *times, int n,
                                         const float *values);
/* any value size (a morphTargetInfluences track has one per target) and
 * interpolation; values hold n * value_size floats (x3 for CUBICSPLINE) */
t3_keyframe_track *t3_keyframe_track_new_ex(t3_track_type type, const char *name, const float *times, int n,
                                            const float *values, int value_size, t3_interpolation interp);
/* Takes the tracks; duration < 0 computes it from the tracks (three.js -1). */
t3_animation_clip *t3_animation_clip_new(const char *name, float duration, t3_keyframe_track **tracks, int n);
t3_animation_mixer *t3_animation_mixer_new(t3_object *root);
t3_animation_action *t3_animation_mixer_clip_action(t3_animation_mixer *m, t3_animation_clip *clip);
void t3_animation_action_play(t3_animation_action *a);
void t3_animation_action_stop(t3_animation_action *a);
/* setEffectiveWeight / setEffectiveTimeScale (fades and warps are not ported) */
void t3_animation_action_set_effective_weight(t3_animation_action *a, float weight);
void t3_animation_action_set_effective_time_scale(t3_animation_action *a, float time_scale);
float t3_animation_action_get_effective_weight(const t3_animation_action *a);
void t3_animation_mixer_update(t3_animation_mixer *m, float dt);

/* ── renderer ────────────────────────────────────────────────────── */
typedef enum { T3_BASIC_SHADOW_MAP = 0, T3_PCF_SHADOW_MAP = 1, T3_PCF_SOFT_SHADOW_MAP = 2 } t3_shadow_map_type;
typedef enum {
  T3_NO_TONE_MAPPING = 0, T3_LINEAR_TONE_MAPPING = 1, T3_REINHARD_TONE_MAPPING = 2, T3_CINEON_TONE_MAPPING = 3,
  T3_ACES_FILMIC_TONE_MAPPING = 4, T3_CUSTOM_TONE_MAPPING = 5,
  T3_AGX_TONE_MAPPING = 6, T3_NEUTRAL_TONE_MAPPING = 7 /* r186 (the generated renderer) */
} t3_tone_mapping;

typedef struct t3_render_info {
  unsigned frame, calls, triangles, programs, geometries, textures;
  unsigned skipped;   /* draws of the last render whose material state the program table lacks */
  /* GPU timing (t3_renderer_set_gpu_timing): the GPU time of the latest
   * render whose timer query has a result (they arrive a frame or more
   * late), that render's frame number, and whether timer queries work here
   * (EXT_disjoint_timer_query; -1 = not asked yet) */
  double gpu_ms;
  unsigned gpu_frame;
  int gpu_timer_supported;
} t3_render_info;

typedef struct t3_renderer t3_renderer;

/* A renderer on the current OpenGL ES 3.0 / WebGL2 context; for WebGPU call
 * t3_renderer_use_wgpu before the first render. */
t3_renderer *t3_renderer_new(int width, int height);
void t3_renderer_destroy(t3_renderer *r);
void t3_renderer_set_size(t3_renderer *r, int width, int height);
/* renderer.setViewport / setScissor / setScissorTest: in pixels, y measured
 * from the top, as three.js r186 on both backends. */
void t3_renderer_set_viewport(t3_renderer *r, int x, int y, int w, int h);
void t3_renderer_set_scissor(t3_renderer *r, int x, int y, int w, int h);
void t3_renderer_set_scissor_test(t3_renderer *r, bool on);
/* renderer.autoClear (default true) and renderer.clear(color, depth, stencil) */
void t3_renderer_set_auto_clear(t3_renderer *r, bool on);
/* new WebGPURenderer({ antialias: true }): render multisampled (samples, e.g.
 * 4; 0 = off) and resolve into the default framebuffer after each render. */
void t3_renderer_set_antialias(t3_renderer *r, int samples);
/* The framebuffer the renderer draws to as "the screen" (default 0): an
 * embedder's own target, such as a 2D engine's canvas FBO. */
void t3_renderer_set_framebuffer(t3_renderer *r, unsigned fbo);
void t3_renderer_clear(t3_renderer *r, bool color, bool depth, bool stencil);
void t3_renderer_set_clear_color(t3_renderer *r, uint32_t hex, float alpha);
void t3_renderer_set_shadow_map(t3_renderer *r, bool enabled, t3_shadow_map_type type);
/* renderer.outputColorSpace (SRGBColorSpace) */
void t3_renderer_set_output_color_space(t3_renderer *r, t3_color_space cs);
/* renderer.toneMapping and renderer.toneMappingExposure (1), applied in the
 * output pass */
void t3_renderer_set_tone_mapping(t3_renderer *r, t3_tone_mapping tm, float exposure);
/* Draw opaque meshes that share geometry, material and group as one
 * instanced draw (default on). Same pixels, far fewer draw calls. */
void t3_renderer_set_auto_instancing(t3_renderer *r, bool on);
/* Forget cached GL state (three.js renderer.resetState()): call after
 * issuing GL calls of your own between renders. */
void t3_renderer_reset_state(t3_renderer *r);
/* Forget only the context-wide state (bound framebuffer, program, VAO,
 * texture units, enables, viewport, clear colour), keeping what each of the
 * renderer's own programs holds: for an embedder whose GL calls never touch
 * three.c's programs, so their uniforms need not be re-sent. */
void t3_renderer_reset_bindings(t3_renderer *r);
/* What the renderer left GL in, from its own tracking (no glGet): for an
 * embedder that shares the context and puts back only what was changed.
 * -1 = unknown (not set since the last reset). Unpack alignment is always
 * left at 4, colour mask, stencil and polygon offset are never touched. */
typedef struct {
  int fbo;                    /* bound framebuffer */
  bool vp_known; int vp_x, vp_y, vp_w, vp_h;
  int active_unit;            /* glActiveTexture unit */
  int program, vao;           /* 0 = none bound by the renderer */
  int blend; bool blend_func_set;
  int depth_test, depth_write, cull, scissor;
  bool clear_set;             /* glClearColor issued */
  float clear[4];             /* ... with this colour */
} t3_gl_state;
void t3_renderer_gl_state(const t3_renderer *r, t3_gl_state *s);
/* Forget only some of the renderer's GL state (an embedder that knows what
 * its own GL calls changed since the renderer last ran). */
enum {
  T3_FORGET_FBO = 1, T3_FORGET_VIEWPORT = 2, T3_FORGET_PROGRAM = 4, T3_FORGET_VAO = 8,
  T3_FORGET_UNIT = 16,        /* the active texture unit (bindings kept) */
  T3_FORGET_TEXTURES = 32,    /* every unit's binding */
  T3_FORGET_CAPS = 64,        /* cull / depth test / depth mask / blend (and blend function) */
  T3_FORGET_SCISSOR = 128, T3_FORGET_CLEAR_COLOR = 256
};
void t3_renderer_forget(t3_renderer *r, unsigned what);
/* whether the next render clears the whole drawing buffer (autoClear, no
 * scissor, the viewport the whole buffer): an embedder can skip a clear of
 * its own that this one would overwrite */
bool t3_renderer_clears_target(const t3_renderer *r);
/* renderer.setRenderTarget: render (and clear) into rt until set back
 * to NULL (the screen). The renderer does not retain rt. */
void t3_renderer_set_render_target(t3_renderer *r, t3_render_target *rt);
t3_render_target *t3_renderer_get_render_target(const t3_renderer *r);
void t3_renderer_render(t3_renderer *r, t3_scene *scene, t3_camera *camera);
/* renderer.compile( scene, camera ): builds the programs the scene's
 * materials need, without drawing */
void t3_renderer_compile(t3_renderer *r, t3_scene *scene, t3_camera *camera);
/* getters for what the setters above set (renderer.getClearColor,
 * .autoClear, .toneMapping, .toneMappingExposure, .outputColorSpace) */
t3_color t3_renderer_get_clear_color(const t3_renderer *r, float *alpha);
bool t3_renderer_get_auto_clear(const t3_renderer *r);
/* the viewport, scissor (returns the scissor test), drawing-buffer size,
 * shadow map (returns enabled) and antialias samples as set */
void t3_renderer_get_viewport(const t3_renderer *r, int *x, int *y, int *w, int *h);
bool t3_renderer_get_scissor(const t3_renderer *r, int *x, int *y, int *w, int *h);
void t3_renderer_get_size(const t3_renderer *r, int *w, int *h);
bool t3_renderer_get_shadow_map(const t3_renderer *r, t3_shadow_map_type *type);
int t3_renderer_get_antialias(const t3_renderer *r);
t3_tone_mapping t3_renderer_get_tone_mapping(const t3_renderer *r, float *exposure);
t3_color_space t3_renderer_get_output_color_space(const t3_renderer *r);

/* ── for embedders: depth passes, GL names (three.c, not three.js) ── */

/* Draw the scene's shadow casters (visible meshes with cast_shadow) depth
 * only into the bound framebuffer and viewport, seen from camera, with the
 * shadow pass's caster programs (skinned, morphed and instanced variants),
 * material.shadowSide (else the opposite of side), frustum culled against
 * camera. No clear, no framebuffer change; point an FBO with only a depth
 * attachment at it for a depth map. Updates world matrices
 * (scene.autoUpdate) first. For an embedder's own shadow maps (cascades,
 * atlases): needs t3_renderer_reset_bindings after the embedder's own GL,
 * like any render. GLES only. */
void t3_renderer_render_depth(t3_renderer *r, t3_scene *scene, t3_camera *camera);
/* The GL texture object behind a texture (a render target's colour or depth
 * texture after its first render, an uploaded texture after its first use),
 * 0 when there is none yet. For copying or sampling it outside three.c. */
unsigned t3_texture_gl_name(const t3_texture *t);

/* ── colour management, programs, WebGPU ──────────────────────────── */
/* ColorManagement.enabled (true): colours made from hex are sRGB, stored
 * linear */
void t3_set_color_management(bool on);
bool t3_get_color_management(void);
float t3_srgb_to_linear(float c);
float t3_linear_to_srgb(float c);
/* GLES: with nothing blended, no tone mapping and an sRGB output the
 * renderer encodes in each program and draws straight to the screen
 * (r186 draws into an RGBA16F target and encodes in an output pass; same
 * formula, ~0.07 ms of GPU time saved at 720p). The two differ only where
 * the driver's rounding of a different program changes a sparkle (specular
 * aliasing at grazing angles). exact: always r186's target and pass. */
void t3_renderer_set_exact_output(t3_renderer *r, bool on);
/* A project's own table of r186 programs (tools/gen-project-table.mjs writes
 * <name>_gl.c and <name>_wgpu.c, each defining `const struct t3_gen_table
 * <name>_gl` / `_wgpu`): searched before three.c's own for every renderer.
 * Either may be NULL. Call before the first render. With a project table,
 * three.c can be built with only its core table (T3_TABLE=core) */
struct t3_gen_table;
void t3_register_program_table(const struct t3_gen_table *gl_table, const struct t3_gen_table *wgpu_table);
/* The renderer draws with the programs three.js r186's node renderer
 * generates (src/gen). A draw whose material state the table lacks is
 * skipped; this names those states, one per line, in the generator's syntax:
 * add them to tools/gen-states.txt and regenerate. */
const char *t3_renderer_generated_missing(const t3_renderer *r);
/* Render with WebGPU instead of GLES (three.c built with T3_WGPU): device and
 * queue are a WGPUDevice and its WGPUQueue (core webgpu.h), out_format the
 * WGPUTextureFormat of the output texture. */
void t3_renderer_use_wgpu(t3_renderer *r, void *device, void *queue, int out_format);
/* the last render's output texture as RGBA8 rows (top row first), blocking */
bool t3_renderer_wgpu_read(t3_renderer *r, uint8_t *rgba);
/* WebGPU submits: by default each render (and clear) submits its own command
 * buffer, as r186 does. Deferred: renders record into one open command
 * buffer and t3_renderer_wgpu_submit sends it (one queue submit per frame;
 * Dawn's submit costs ~25 us). An embedder's encoder (WGPUCommandEncoder):
 * renders record into it and never submit; the embedder submits it and then
 * calls t3_renderer_wgpu_submit, which only closes three.c's frame. */
void t3_renderer_wgpu_defer(t3_renderer *r, bool on);
void t3_renderer_wgpu_submit(t3_renderer *r);
void t3_renderer_wgpu_set_encoder(t3_renderer *r, void *encoder);
/* draw the final image into the embedder's view (a WGPUTextureView of the
 * renderer's size, WGPUTextureFormat format: a swapchain texture) instead of
 * three.c's own output texture; NULL = back to that texture */
void t3_renderer_wgpu_set_output(t3_renderer *r, void *view, int format);

/* ── backend-neutral embedder hooks ── */

/* The clip-space z convention of the depth the renderer writes, stable per
 * backend: [-1, 1] on GLES, [0, 1] on WebGPU. The projection matrix the
 * renderer used for its last render (16 floats, column-major) is readable so
 * an embedder reconstructing linear depth / world positions from that depth
 * uses exactly it. */
typedef enum { T3_CLIP_Z_NEG1_1 = 0, T3_CLIP_Z_0_1 = 1 } t3_clip_z;
t3_clip_z t3_renderer_clip_z(const t3_renderer *r);
const float *t3_renderer_last_projection(const t3_renderer *r);

/* An embedder's own target: the whole attachment set plus the rect to draw
 * in. GLES: color is the framebuffer object (its attachments carry the
 * formats; color_resolve, depth and the formats are ignored). WebGPU: color
 * is a WGPUTextureView (0 for a depth-only target), color_resolve the
 * resolve view when samples > 1, depth a WGPUTextureView, the formats
 * WGPUTextureFormat values. load_color / load_depth keep what an earlier
 * pass drew (LOAD) instead of clearing to the clear values. x, y, w, h is
 * the viewport and the scissor inside the attachments (an atlas tile, or
 * the whole target). */
typedef struct t3_external_target {
  uintptr_t color, color_resolve, depth;
  int color_format, depth_format;
  int samples;
  bool load_color, load_depth;
  float clear_rgba[4]; float clear_depth; int clear_stencil;
  int x, y, w, h;
  /* WebGPU: the attachments hold rows bottom-up, as GL (an embedder whose
   * passes are GL ports): the renderer negates clip y and swaps the front
   * face for every draw into them, and x, y, w, h count y from the bottom
   * row, as on GLES. GLES: ignored. */
  bool flip_y;
} t3_external_target;
/* Render into t until set back with NULL (the renderer's own screen target,
 * framebuffer 0 on GLES). On GLES this is t3_renderer_set_framebuffer plus the
 * viewport, scissor and the clear the descriptor asks for (autoClear of only
 * what is not loaded, to its clear values); the two calls set the same
 * state, the last one wins. On WebGPU the renders draw straight into the
 * views with no output pass, so the output colour space should be linear
 * and tone mapping off (the embedder converts in its own pass); the
 * renderer's shadow maps and other internal targets are its own. */
void t3_renderer_set_external_target(t3_renderer *r, const t3_external_target *t);
/* WebGPU: record every pass of the next renders on the embedder's
 * WGPUCommandEncoder and never submit (0 = the renderer owns encoding and
 * submit). GLES: accepted and ignored. */
void t3_renderer_set_external_encoder(t3_renderer *r, uintptr_t wgpu_command_encoder);
/* t3_renderer_render_depth into a caller's depth-only target rect: color is
 * the depth-only framebuffer on GLES (depth the view, depth_format its
 * format, on WebGPU; the only depth render there), LOAD unless
 * load_depth is false (then the rect is cleared to clear_depth first), the
 * rect as viewport and scissor, so many tiles of one shadow atlas survive
 * each other. Leaves the framebuffer, viewport and scissor as it set them,
 * like t3_renderer_render_depth leaves the caller's. */
void t3_renderer_render_depth_to(t3_renderer *r, t3_scene *scene, t3_camera *camera, const t3_external_target *depth_only);

/* ── PMREMGenerator ──────────────────────────────────────────────── */
/* three.js r186's PMREMGenerator: prefiltered radiance environment maps in
 * the cube-UV layout (a half-float render target whose texture has
 * T3_CUBE_UV_REFLECTION_MAPPING), for MeshStandardMaterial.envMap,
 * scene.environment or scene.background. A cube or equirectangular envMap
 * gets one made automatically, as PMREMNode does. The targets returned are
 * the caller's (t3_release them). */
typedef struct t3_pmrem_generator t3_pmrem_generator;
t3_pmrem_generator *t3_pmrem_generator_new(t3_renderer *r);
void t3_pmrem_generator_destroy(t3_pmrem_generator *g);
t3_render_target *t3_pmrem_from_cubemap(t3_pmrem_generator *g, t3_texture *cube);
t3_render_target *t3_pmrem_from_equirectangular(t3_pmrem_generator *g, t3_texture *equirect);
/* fromScene(scene, sigma, near, far): renders the scene from the origin into
 * the six faces, a 256 cube */
t3_render_target *t3_pmrem_from_scene(t3_pmrem_generator *g, t3_scene *scene, float sigma, float near, float far);
const t3_render_info *t3_renderer_info(t3_renderer *r);
/* time each render on the GPU (GL_TIME_ELAPSED_EXT queries, read back
 * without stalling) into t3_renderer_info's gpu_ms; off by default. In a
 * WebGL host this needs EXT_disjoint_timer_query_webgl2 enabled by the host. */
void t3_renderer_set_gpu_timing(t3_renderer *r, bool on);
/* The last shader compile / link error, or NULL. */
const char *t3_renderer_last_error(t3_renderer *r);

#ifdef __cplusplus
}
#endif
#endif
