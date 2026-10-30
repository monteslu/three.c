/* Every optional vertex attribute at once, on both backends.
 *
 * An InstancedMesh whose geometry carries position, normal, uv, uv1,
 * vertex colours and tangents, with instance colours, drawn with a
 * MeshPhysicalMaterial that uses map, normalMap (with the geometry's
 * tangents), aoMap and vertexColors: the most vertex buffers an instanced
 * program binds (7: position, normal, uv, color and tangent, the instance
 * matrix as one buffer as r187's InstancedInterleavedBuffer, the instance
 * colour; r187 samples the aoMap on uv, channel 0, so uv1 is on the
 * geometry but no program reads it). WebGPU guarantees 8 (maxVertexBuffers,
 * core and compatibility mode); one buffer per matrix column would be 10. The draw must not be
 * skipped, raise no WebGPU validation error, show each instance's colour
 * (red, green, blue, yellow quadrants) and, with a path to the GLES frame,
 * match it within 8 per channel (the 890M: 4 on GLES and WebGPU alike; Mesa's software GLES
 * against lavapipe WebGPU: 7).
 *
 * T3_TEST_CONTROL=1 (must fail): every instance colour white, so the
 * quadrant checks fail.
 *
 * GLES:   cc ... test/test_vertex_buffers.c <table>_gl.c build/native/libthree.a -lEGL -lGLESv2 -lz -lm
 *         ./test_vertex_buffers [out.rgba]
 * WebGPU: cc -DT3_WGPU ... test/test_vertex_buffers.c <table>_gl.c <table>_wgpu.c build/native-wgpu/libthree.a <dawn>
 *         ./test_vertex_buffers_wgpu [out.rgba] [gles.rgba]
 * The program table: tools/gen-project-table.mjs with the states this draws (test/run.sh makes it).
 *
 * Exits 0 on success, 77 when no WebGPU adapter is available. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "three.h"
#include "gen/programs.h"

extern const t3_gen_table vbtab_gl;
#ifdef T3_WGPU
extern const t3_gen_table vbtab_wgpu;
#endif

#define W 64
#define H 64

#ifdef T3_WGPU
#include <webgpu/webgpu.h>
static WGPUInstance inst;
static void adapter_cb(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUAdapter *)u1 = st == WGPURequestAdapterStatus_Success ? a : NULL; *(int *)u2 = 1;
}
static void device_cb(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUDevice *)u1 = st == WGPURequestDeviceStatus_Success ? d : NULL; *(int *)u2 = 1;
}
static int wgpu_errors;
static void error_cb(WGPUDevice const *d, WGPUErrorType t, WGPUStringView m, void *u1, void *u2) {
  (void)d; (void)u1; (void)u2;
  if (wgpu_errors++ < 8) fprintf(stderr, "webgpu error %d: %.*s\n", (int)t, (int)m.length, m.data);
}
static WGPUDevice dev;
static WGPUQueue queue;
static int setup(void) {
  inst = wgpuCreateInstance(NULL);
  WGPUAdapter ad = NULL; int done = 0;
  /* compatibility mode, as wasmcart gives carts: the default limits (8 vertex buffers) */
  WGPURequestAdapterOptions ao = { .powerPreference = WGPUPowerPreference_LowPower, .featureLevel = WGPUFeatureLevel_Compatibility };
  wgpuInstanceRequestAdapter(inst, &ao, (WGPURequestAdapterCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = adapter_cb, .userdata1 = &ad, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!ad) return 77;
  WGPUDeviceDescriptor dd = { .uncapturedErrorCallbackInfo = { .callback = error_cb } };
  done = 0;
  wgpuAdapterRequestDevice(ad, &dd, (WGPURequestDeviceCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = device_cb, .userdata1 = &dev, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!dev) return 77;
  WGPULimits lim = { 0 };
  if (wgpuDeviceGetLimits(dev, &lim) == WGPUStatus_Success) printf("device maxVertexBuffers %u\n", lim.maxVertexBuffers);
  queue = wgpuDeviceGetQueue(dev);
  return 0;
}
static t3_renderer *renderer(void) {
  t3_renderer *r = t3_renderer_new(W, H);
  t3_renderer_use_wgpu(r, dev, queue, WGPUTextureFormat_RGBA8Unorm);
  return r;
}
static void read_frame(t3_renderer *r, uint8_t *px) { t3_renderer_wgpu_read(r, px); }
#define BACKEND "webgpu"
#else
#include <EGL/egl.h>
#include <GLES3/gl3.h>
static int setup(void) {
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(d, NULL, NULL)) return fprintf(stderr, "eglInitialize failed\n"), 2;
  EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
  EGLConfig cfg;
  EGLint n = 0;
  if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) return fprintf(stderr, "no config\n"), 2;
  EGLint sa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
  EGLSurface surf = eglCreatePbufferSurface(d, cfg, sa);
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint xa[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xa);
  if (!ctx || !eglMakeCurrent(d, surf, surf, ctx)) return fprintf(stderr, "no context\n"), 2;
  return 0;
}
static t3_renderer *renderer(void) { return t3_renderer_new(W, H); }
/* (GL rows bottom-up: flipped so both backends write top-down frames) */
static void read_frame(t3_renderer *r, uint8_t *px) {
  (void)r;
  uint8_t tmp[W * H * 4];
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
  for (int y = 0; y < H; y++) memcpy(px + (size_t)y * W * 4, tmp + (size_t)(H - 1 - y) * W * 4, W * 4);
}
#define BACKEND "gles"
#endif

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL (" BACKEND "): "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static t3_texture *tex(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t px[4 * 4 * 4];
  for (int i = 0; i < 16; i++) { px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255; }
  return t3_data_texture_new(4, 4, px);
}

int main(int argc, char **argv) {
  bool control = getenv("T3_TEST_CONTROL") && atoi(getenv("T3_TEST_CONTROL"));
  int st = setup();
  if (st == 77) { puts("test_vertex_buffers (" BACKEND "): no WebGPU adapter, skipped"); return 77; }
  if (st) return st;
#ifdef T3_WGPU
  t3_register_program_table(&vbtab_gl, &vbtab_wgpu);
#else
  t3_register_program_table(&vbtab_gl, NULL);
#endif
  t3_renderer *r = renderer();
  t3_renderer_set_clear_color(r, 0x000000, 1);
  t3_camera *cam = t3_orthographic_camera_new(-2, 2, 2, -2, 0.1f, 20);
  t3_object_set_position(cam, 0, 0, 10);
  t3_scene *scene = t3_scene_new();
  t3_object_add(&scene->base, t3_ambient_light_new(0xffffff, 1.0f));
  t3_light *sun = t3_directional_light_new(0xffffff, 2.0f);
  t3_object_set_position(sun, 1, 2, 5);
  t3_object_add(&scene->base, sun);

  /* a box with every optional per-vertex attribute: uv1, vertex colours (grey, rgb), tangents (vec4) */
  t3_geometry *g = t3_box_geometry_new(1.4f, 1.4f, 1.4f, 1, 1, 1);
  t3_attribute *uv = g->attributes[T3_ATTR_UV];
  int n = uv->count;
  t3_geometry_set_attribute(g, T3_ATTR_UV1, t3_attribute_new(T3_FLOAT32, uv->array, n, 2));
  float *col = malloc((size_t)n * 3 * sizeof(float)), *tan = malloc((size_t)n * 4 * sizeof(float));
  for (int i = 0; i < n; i++) {
    col[i * 3] = col[i * 3 + 1] = col[i * 3 + 2] = 0.9f;
    tan[i * 4] = 1; tan[i * 4 + 1] = 0; tan[i * 4 + 2] = 0; tan[i * 4 + 3] = 1;
  }
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, n, 3));
  t3_geometry_set_attribute(g, T3_ATTR_TANGENT, t3_attribute_new(T3_FLOAT32, tan, n, 4));
  free(col); free(tan);

  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->roughness = 0.8f; m->metalness = 0;
  m->vertex_colors = true;
  t3_material_set_texture(m, T3_MAP, tex(240, 240, 240));
  t3_material_set_texture(m, T3_NORMAL_MAP, tex(128, 128, 255));
  t3_material_set_texture(m, T3_AO_MAP, tex(230, 230, 230));

  /* four instances in the four quadrants, each its own colour */
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, 4);
  const float pos[4][2] = { { -1, 1 }, { 1, 1 }, { -1, -1 }, { 1, -1 } };   /* top-left, top-right, bottom-left, bottom-right */
  const uint32_t hex[4] = { 0xff0000, 0x00ff00, 0x0000ff, 0xffff00 };
  for (int i = 0; i < 4; i++) {
    t3_mat4 mx;
    t3_mat4_identity(&mx);
    mx.e[12] = pos[i][0]; mx.e[13] = pos[i][1];
    t3_instanced_mesh_set_matrix_at(im, i, &mx);
    t3_instanced_mesh_set_color_at(im, i, t3_color_hex(control ? 0xffffff : hex[i]));
  }
  t3_object_add(&scene->base, im);

  static uint8_t px[W * H * 4];
  t3_renderer_render(r, scene, cam);
#ifdef T3_WGPU
  t3_renderer_wgpu_submit(r);
#endif
  read_frame(r, px);
  CHECK(t3_renderer_info(r)->skipped == 0, "a draw was skipped: %s", t3_renderer_generated_missing(r));
  /* each quadrant's centre: its instance colour dominates */
  for (int i = 0; i < 4; i++) {
    int x = W / 4 + (i & 1) * W / 2, y = H / 4 + (i >> 1) * H / 2;
    const uint8_t *p = px + ((size_t)y * W + x) * 4;
    int want[3] = { (hex[i] >> 16) & 255 ? 1 : 0, (hex[i] >> 8) & 255 ? 1 : 0, hex[i] & 255 ? 1 : 0 };
    printf("instance %d at (%d,%d): %3d %3d %3d\n", i, x, y, p[0], p[1], p[2]);
    for (int c = 0; c < 3; c++)
      CHECK(want[c] ? p[c] > 150 : p[c] < 70, "instance %d channel %d is %d: not its instance colour %06x", i, c, p[c], hex[i]);
  }
#ifdef T3_WGPU
  CHECK(wgpu_errors == 0, "%d WebGPU validation errors", wgpu_errors);
#endif
  if (argc > 1) {
    FILE *f = fopen(argv[1], "wb");
    if (f) { fwrite(px, 1, sizeof px, f); fclose(f); }
  }
  if (argc > 2) {   /* the GLES frame: GL vs WebGPU */
    static uint8_t ref[W * H * 4];
    FILE *f = fopen(argv[2], "rb");
    CHECK(f && fread(ref, 1, sizeof ref, f) == sizeof ref, "cannot read %s", argv[2]);
    if (f) fclose(f);
    int maxd = 0, over = 0;
    for (size_t i = 0; i < sizeof px; i++) {
      int d = abs(px[i] - ref[i]);
      if (d > maxd) maxd = d;
      if (d > 3) over++;
    }
    printf("GL vs WebGPU: max channel difference %d, %d channels over 3\n", maxd, over);
    CHECK(maxd <= 8, "GL vs WebGPU differ by %d (%d channels over 3)", maxd, over);
  }
  if (failures) return 1;
  puts("test_vertex_buffers (" BACKEND "): ok");
  return 0;
}
