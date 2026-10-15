/* Extension programs filled at run time: three.c's example extension
 * (tools/extensions/tint.mjs, state standard+map+xtint) multiplies the
 * material colour by a shared uniform (tint), a texture (tintMap) and a value
 * per object (tintScale), all set by the hook t3_renderer_set_extension
 * registers. Two boxes side by side:
 *   - with tint 1, a white tintMap and scale 1 the picture equals the same
 *     material without the extension (standard+map);
 *   - a scale on the left box changes the left box only, with the two drawn
 *     from one program (the per-draw block of each object is its own);
 *   - a new tint changes the picture on the next render, and the old tint
 *     brings the old picture back (the shared group is uploaded on change);
 *   - the controls: with no hook, or a hook that leaves the texture unset, the
 *     two draws are skipped, counted in info.skipped and named in the error.
 * And the example that moves vertices (tools/extensions/swell.mjs) casting a
 * shadow onto a floor, the object itself out of view: with swell 0 the shadow
 * equals the plain material's (plain caster), a swell grows it (the caster
 * is depth+xswell, filled by the hook), and with no hook it is gone.
 * And the example with an array texture and an integer texture
 * (tools/extensions/layers.mjs) on a plane facing the camera: an R32UI index
 * of all 1 draws what an index of all 0 with a shift of 1 draws (the integer
 * is read, the shift added and the layer picked); new texels in a layer, or
 * in the index, change the picture; with no hook the draw is skipped.
 * And the example with many textures (tools/extensions/mosaic.mjs): thirteen
 * bindings in one group, the height texture bound for both stages. New
 * texels in the tenth tile, or in the height texture, change the picture
 * (no binding past the eighth is dropped).
 *
 * GLES (built against build/native/libthree.a, EGL surfaceless):
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_extension.c \
 *     build/native/libthree.a -lEGL -lGLESv2 -lz -lm
 * WebGPU (-DT3_WGPU, against build/native-wgpu/libthree.a and Dawn):
 *   cc -std=c99 -O2 -DT3_WGPU -I include -I src -I <dawn>/include test/test_extension.c \
 *     build/native-wgpu/libthree.a -L <dawn>/lib -lwebgpu_dawn -Wl,-rpath,<dawn>/lib -lEGL -lGLESv2 -lz -lm
 *
 * The WebGPU build takes "compat" as its argument for a compatibility-mode
 * device (test/run.sh runs both).
 *
 * Exits 0 on success, 77 when no WebGPU adapter is available. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "three.h"

#define W 128
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
static bool compat;   /* argv[1] "compat": a compatibility-mode device, as wasmcart gives carts */
static int setup(void) {
  inst = wgpuCreateInstance(NULL);
  WGPUAdapter ad = NULL; int done = 0;
  WGPURequestAdapterOptions ao = { .powerPreference = WGPUPowerPreference_HighPerformance,
                                   .featureLevel = compat ? WGPUFeatureLevel_Compatibility : WGPUFeatureLevel_Core };
  wgpuInstanceRequestAdapter(inst, &ao, (WGPURequestAdapterCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = adapter_cb, .userdata1 = &ad, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!ad) return 77;
  WGPUDeviceDescriptor dd = { .uncapturedErrorCallbackInfo = { .callback = error_cb } };
  done = 0;
  wgpuAdapterRequestDevice(ad, &dd, (WGPURequestDeviceCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = device_cb, .userdata1 = &dev, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!dev) return 77;
  queue = wgpuDeviceGetQueue(dev);
  return 0;
}
static t3_renderer *renderer(void) {
  t3_renderer *r = t3_renderer_new(W, H);
  t3_renderer_use_wgpu(r, dev, queue, WGPUTextureFormat_RGBA8Unorm);
  t3_renderer_set_auto_instancing(r, false);   /* (the plain picture: each box its own draw, as the extension's are) */
  return r;
}
static void read_frame(t3_renderer *r, uint8_t *px) { t3_renderer_wgpu_read(r, px); }
#define BACKEND "webgpu"
#define MODE (compat ? " compat" : "")
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
static t3_renderer *renderer(void) {
  t3_renderer *r = t3_renderer_new(W, H);
  t3_renderer_set_auto_instancing(r, false);   /* (the plain picture: each box its own draw, as the extension's are) */
  return r;
}
static void read_frame(t3_renderer *r, uint8_t *px) { (void)r; glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); }
#define BACKEND "gles"
#define MODE ""
#endif

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL (" BACKEND "%s): ", MODE); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* texels of a and b differing by more than 1 in a channel, over columns [x0, x1) */
static int differ(const uint8_t *a, const uint8_t *b, int x0, int x1) {
  int n = 0;
  for (int y = 0; y < H; y++)
    for (int x = x0; x < x1; x++) {
      const uint8_t *p = a + (y * W + x) * 4, *q = b + (y * W + x) * 4;
      for (int c = 0; c < 3; c++) if (abs(p[c] - q[c]) > 1) { n++; break; }
    }
  return n;
}

/* what the hook sets */
static float tint[3] = { 1, 1, 1 }, left_scale = 1;
static t3_texture *tint_map;
static const t3_object *left;
static bool set_texture = true;
static int fills;
static void fill(t3_extension_ctx *c, void *user) {
  (void)user;
  fills++;
  float s = t3_extension_object(c) == left ? left_scale : 1;
  t3_extension_set_uniform(c, "tint", tint, 3);
  t3_extension_set_uniform(c, "tintScale", &s, 1);
  if (set_texture) t3_extension_set_texture(c, "tintMap", tint_map);
}

static float swell;
static void fill_swell(t3_extension_ctx *c, void *user) { (void)user; t3_extension_set_uniform(c, "swell", &swell, 1); }

static float layer_shift;
static t3_texture *layer_index, *layer_map;
static void fill_layers(t3_extension_ctx *c, void *user) {
  (void)user;
  t3_extension_set_uniform(c, "layerShift", &layer_shift, 1);
  t3_extension_set_texture(c, "layerIndex", layer_index);
  t3_extension_set_texture(c, "layerMap", layer_map);
}

static t3_texture *mosaic_tiles[10], *mosaic_height;
static void fill_mosaic(t3_extension_ctx *c, void *user) {
  (void)user;
  float lift = 0.2f;
  t3_extension_set_uniform(c, "mosaicLift", &lift, 1);
  for (int i = 0; i < 10; i++) {
    char name[16];
    snprintf(name, sizeof name, "tile%d", i);
    t3_extension_set_texture(c, name, mosaic_tiles[i]);
  }
  t3_extension_set_texture(c, "height", mosaic_height);
}

static t3_scene *scene;
static t3_camera *cam;
static t3_material *mat;
static void render(t3_renderer *r, uint8_t *px) {
  t3_renderer_render(r, scene, cam);
#ifdef T3_WGPU
  t3_renderer_wgpu_submit(r);
#endif
  read_frame(r, px);
}

int main(int argc, char **argv) {
#ifdef T3_WGPU
  compat = argc > 1 && !strcmp(argv[1], "compat");
#else
  (void)argc; (void)argv;
#endif
  int st = setup();
  if (st == 77) { puts("test_extension (" BACKEND "): no WebGPU adapter, skipped"); return 77; }
  if (st) return st;
  /* the same filtering the captured program bakes: linear, mipmapped, clamped */
  uint8_t white[4 * 4 * 4], checker[8 * 8 * 4];
  memset(white, 255, sizeof white);
  for (int i = 0; i < 64; i++) {
    bool on = ((i % 8) / 2 + (i / 8) / 2) & 1;
    checker[i * 4] = on ? 230 : 40; checker[i * 4 + 1] = on ? 200 : 90; checker[i * 4 + 2] = on ? 120 : 220; checker[i * 4 + 3] = 255;
  }
  tint_map = t3_data_texture_new(4, 4, white);
  t3_texture *map = t3_data_texture_new(8, 8, checker);
  t3_texture *texs[2] = { tint_map, map };
  for (int i = 0; i < 2; i++) { texs[i]->mag_filter = T3_LINEAR; texs[i]->min_filter = T3_LINEAR_MIPMAP_LINEAR; texs[i]->generate_mipmaps = true; }

  scene = t3_scene_new();
  cam = t3_perspective_camera_new(40, (float)W / H, 0.1f, 50);
  t3_object_set_position(cam, 0, 1.2f, 5);
  t3_object_look_at(cam, 0, 0, 0);
  t3_light *amb = t3_ambient_light_new(0x404040, 1), *sun = t3_directional_light_new(0xffffff, 1.5f);
  t3_object_set_position(sun, 2, 4, 3);
  t3_object_add(&scene->base, amb); t3_object_add(&scene->base, sun);
  t3_release(amb); t3_release(sun);
  mat = t3_mesh_standard_material_new(0xffffff);
  mat->roughness = 0.6f;
  t3_material_set_map(mat, map);
  t3_geometry *g = t3_box_geometry_new(1.2f, 1.2f, 1.2f, 1, 1, 1);
  t3_mesh *boxes[2];
  for (int i = 0; i < 2; i++) {
    boxes[i] = t3_mesh_new(g, mat);
    t3_object_set_position(boxes[i], i ? 1.1f : -1.1f, 0, 0);
    t3_object_set_rotation(boxes[i], 0.4f, 0.6f, 0);
    t3_object_add(&scene->base, boxes[i]);
    t3_release(boxes[i]);
  }
  left = &boxes[0]->base;
  /* (the camera looks at the gap between the boxes: columns [0, W/2) hold the left one) */

  static uint8_t plain[W * H * 4], full[W * H * 4], scaled[W * H * 4], red[W * H * 4], back[W * H * 4];
  t3_renderer *r = renderer();
  render(r, plain);   /* the material without the extension */
  CHECK(t3_renderer_info(r)->skipped == 0, "the plain material skipped %u draws (%s)", t3_renderer_info(r)->skipped, t3_renderer_generated_missing(r) ? t3_renderer_generated_missing(r) : "");

  CHECK(t3_material_set_extension(mat, "tint"), "set_extension(tint)");
  CHECK(!strcmp(t3_material_get_extension(mat), "tint"), "get_extension");
  CHECK(!t3_material_set_extension(mat, "Not A Name"), "a bad extension name was accepted");
  t3_renderer_set_extension(r, "tint", fill, NULL);
  render(r, full);
  CHECK(t3_renderer_info(r)->skipped == 0, "the extension material skipped %u draws: %s", t3_renderer_info(r)->skipped,
        t3_renderer_last_error(r) ? t3_renderer_last_error(r) : "");
  CHECK(fills >= 2, "the hook ran %d times for two draws", fills);
  int d = differ(plain, full, 0, W);
  CHECK(d == 0, "tint 1, a white tintMap and scale 1 differ from the plain material at %d texels", d);

  left_scale = 0.4f;
  render(r, scaled);
  int dl = differ(full, scaled, 0, W / 2), dr = differ(full, scaled, W / 2, W);
  CHECK(dl > 100, "a scale on the left box changed only %d of its texels", dl);
  CHECK(dr == 0, "a scale on the left box changed %d texels of the right one", dr);
  left_scale = 1;

  tint[1] = tint[2] = 0;
  render(r, red);
  d = differ(full, red, 0, W);
  CHECK(d > 200, "a new tint changed only %d texels", d);
  tint[1] = tint[2] = 1;
  render(r, back);
  d = differ(full, back, 0, W);
  CHECK(d == 0, "the old tint left %d texels changed", d);

  /* the tintMap: a checker of its own changes the picture */
  t3_texture *other = t3_data_texture_new(8, 8, checker);
  other->mag_filter = T3_LINEAR; other->min_filter = T3_LINEAR_MIPMAP_LINEAR; other->generate_mipmaps = true;
  t3_texture *keep = tint_map;
  tint_map = other;
  render(r, back);
  d = differ(full, back, 0, W);
  CHECK(d > 200, "another tintMap changed only %d texels", d);
  tint_map = keep;

  /* the controls, each on a fresh renderer (the error is the first one) */
  t3_renderer *r2 = renderer();
  render(r2, back);
  CHECK(t3_renderer_info(r2)->skipped == 2, "with no hook %u draws were skipped, not 2", t3_renderer_info(r2)->skipped);
  CHECK(t3_renderer_last_error(r2) && strstr(t3_renderer_last_error(r2), "no hook for extension tint"), "with no hook the error is: %s",
        t3_renderer_last_error(r2) ? t3_renderer_last_error(r2) : "(none)");
  d = differ(full, back, 0, W);
  CHECK(d > 200, "with no hook the picture still matches the filled one (%d texels differ)", d);
  t3_renderer_set_extension(r2, "tint", fill, NULL);
  t3_renderer_set_extension(r2, "tint", NULL, NULL);   /* removed again */
  render(r2, back);
  CHECK(t3_renderer_info(r2)->skipped == 2, "a removed hook still filled (%u skipped)", t3_renderer_info(r2)->skipped);

  t3_renderer *r3 = renderer();
  t3_renderer_set_extension(r3, "tint", fill, NULL);
  set_texture = false;
  render(r3, back);
  set_texture = true;
  CHECK(t3_renderer_info(r3)->skipped == 2, "with tintMap unset %u draws were skipped, not 2", t3_renderer_info(r3)->skipped);
  CHECK(t3_renderer_last_error(r3) && strstr(t3_renderer_last_error(r3), "left texture tintMap unset"), "with tintMap unset the error is: %s",
        t3_renderer_last_error(r3) ? t3_renderer_last_error(r3) : "(none)");

  /* and without the extension again: the plain program */
  t3_material_set_extension(mat, NULL);
  render(r, back);
  d = differ(plain, back, 0, W);
  CHECK(d == 0 && t3_material_get_extension(mat) == NULL, "clearing the extension left %d texels changed", d);

  /* the shadow of an object whose vertices the extension moves */
  {
    t3_scene *s2 = t3_scene_new();
    t3_camera *c2 = t3_perspective_camera_new(40, (float)W / H, 0.1f, 50);
    t3_object_set_position(c2, -1, 5, 0.01f);
    t3_object_look_at(c2, -1, 0, 0);
    t3_light *amb2 = t3_ambient_light_new(0x404040, 1), *sun2 = t3_directional_light_new(0xffffff, 1.5f);
    t3_object_set_position(sun2, 8, 6, 0);
    sun2->base.cast_shadow = true;
    sun2->shadow->map_width = sun2->shadow->map_height = 512;
    t3_object_add(&s2->base, amb2); t3_object_add(&s2->base, sun2);
    t3_release(amb2); t3_release(sun2);
    t3_geometry *fg = t3_plane_geometry_new(12, 12, 1, 1), *sg = t3_sphere_geometry_new(0.8f, 32, 16);
    t3_material *fm = t3_mesh_standard_material_new(0xc0c0c0), *bm = t3_mesh_standard_material_new(0xe08040);
    t3_mesh *floor = t3_mesh_new(fg, fm);
    t3_object_set_rotation(floor, -3.141592653589793f / 2, 0, 0);
    floor->base.receive_shadow = true;
    t3_mesh *ball = t3_mesh_new(sg, bm);
    t3_object_set_position(ball, 3, 3, 0);
    ball->base.cast_shadow = true;
    t3_object_add(&s2->base, floor); t3_object_add(&s2->base, ball);
    t3_release(floor); t3_release(ball); t3_release(fg); t3_release(sg); t3_release(fm);
    static uint8_t sp[W * H * 4], s0[W * H * 4], s1[W * H * 4], sn[W * H * 4];
    t3_release(scene); t3_release(cam);
    scene = s2; cam = c2;
    t3_renderer *r4 = renderer();
    t3_renderer_set_shadow_map(r4, true, T3_PCF_SHADOW_MAP);
    render(r4, sp);   /* plain material, plain caster */
    CHECK(t3_renderer_info(r4)->skipped == 0, "the plain shadow scene skipped %u draws", t3_renderer_info(r4)->skipped);
    t3_material_set_extension(bm, "swell");
    t3_renderer_set_extension(r4, "swell", fill_swell, NULL);
    swell = 0;
    render(r4, s0);
    CHECK(t3_renderer_info(r4)->skipped == 0, "the swell scene skipped %u draws: %s (%s)", t3_renderer_info(r4)->skipped,
          t3_renderer_last_error(r4) ? t3_renderer_last_error(r4) : "", t3_renderer_generated_missing(r4) ? t3_renderer_generated_missing(r4) : "");
    d = differ(sp, s0, 0, W);
    CHECK(d == 0, "a swell of 0 changed the shadow at %d texels", d);
    swell = 0.4f;
    render(r4, s1);
    d = differ(s0, s1, 0, W);
    CHECK(d > 50, "a swell of 0.4 changed the shadow at only %d texels (the caster ignores the extension)", d);
    t3_renderer_set_extension(r4, "swell", NULL, NULL);
    render(r4, sn);
    CHECK(t3_renderer_info(r4)->skipped >= 1 && t3_renderer_last_error(r4) && strstr(t3_renderer_last_error(r4), "no hook for extension swell"),
          "with no hook the caster still drew (%u skipped, error %s)", t3_renderer_info(r4)->skipped, t3_renderer_last_error(r4) ? t3_renderer_last_error(r4) : "(none)");
    d = differ(s1, sn, 0, W);
    CHECK(d > 50, "with no hook the shadow is still there (%d texels differ)", d);
    t3_renderer_destroy(r4);
    t3_release(bm);   /* (s2 and c2 stay the scene and camera until the next block) */
  }

  /* an array texture and an integer texture */
  {
    t3_scene *s3 = t3_scene_new();
    t3_camera *c3 = t3_perspective_camera_new(40, (float)W / H, 0.1f, 50);
    t3_object_set_position(c3, 0, 0, 3);
    t3_object_look_at(c3, 0, 0, 0);
    t3_light *amb3 = t3_ambient_light_new(0x404040, 1), *sun3 = t3_directional_light_new(0xffffff, 1.5f);
    t3_object_set_position(sun3, 1, 2, 4);
    t3_object_add(&s3->base, amb3); t3_object_add(&s3->base, sun3);
    t3_release(amb3); t3_release(sun3);
    uint32_t idx[16];
    for (int i = 0; i < 16; i++) idx[i] = 0;
    layer_index = t3_data_texture_new_format(4, 4, idx, T3_RED_INTEGER_FORMAT, T3_UNSIGNED_INT_TYPE);
    uint8_t layers[4 * 4 * 4 * 4];
    for (int l = 0; l < 4; l++)
      for (int i = 0; i < 16; i++) {
        uint8_t *o = layers + (l * 16 + i) * 4;
        o[0] = (uint8_t)(40 + l * 60); o[1] = (uint8_t)(220 - l * 50); o[2] = (uint8_t)(90 + (i & 3) * 30); o[3] = 255;
      }
    layer_map = t3_data_array_texture_new(4, 4, 4, layers, T3_RGBA_FORMAT, T3_UNSIGNED_BYTE_TYPE);
    layer_map->mag_filter = layer_map->min_filter = T3_LINEAR;
    CHECK(layer_map->depth == 4 && t3_texture_texel_bytes(T3_RED_INTEGER_FORMAT, T3_UNSIGNED_INT_TYPE) == 4, "array / integer texture fields");
    t3_geometry *pg = t3_plane_geometry_new(2.4f, 2.4f, 1, 1);
    t3_material *lm = t3_mesh_standard_material_new(0xffffff);
    t3_material_set_extension(lm, "layers");
    t3_mesh *plane = t3_mesh_new(pg, lm);
    t3_object_add(&s3->base, plane);
    t3_release(plane); t3_release(pg); t3_release(lm);
    t3_release(scene); t3_release(cam);
    scene = s3; cam = c3;
    static uint8_t l0[W * H * 4], l1[W * H * 4], ls[W * H * 4], le[W * H * 4];
    t3_renderer *r5 = renderer();
    t3_renderer_set_extension(r5, "layers", fill_layers, NULL);
    layer_shift = 0;
    render(r5, l0);
    CHECK(t3_renderer_info(r5)->skipped == 0, "the layers scene skipped %u draws: %s (%s)", t3_renderer_info(r5)->skipped,
          t3_renderer_last_error(r5) ? t3_renderer_last_error(r5) : "", t3_renderer_generated_missing(r5) ? t3_renderer_generated_missing(r5) : "");
    uint32_t *ix = (uint32_t *)layer_index->pixels;
    for (int i = 0; i < 16; i++) ix[i] = 1;
    layer_index->version++;
    render(r5, l1);
    d = differ(l0, l1, 0, W);
    CHECK(d > 500, "an index of 1 instead of 0 changed only %d texels", d);
    for (int i = 0; i < 16; i++) ix[i] = 0;
    layer_index->version++;
    layer_shift = 1;
    render(r5, ls);
    d = differ(l1, ls, 0, W);
    CHECK(d == 0, "index 0 shifted by 1 differs from index 1 at %d texels", d);
    layer_map->pixels[(16 + 5) * 4 + 1] ^= 0xff;   /* layer 1, one texel */
    layer_map->version++;
    render(r5, le);
    d = differ(ls, le, 0, W);
    CHECK(d > 20, "new texels in layer 1 changed only %d texels", d);
    /* a mipmapped DataArrayTexture seen from far away: its levels average a
     * one-texel checker to grey, as a flat grey array draws it (made by the
     * GPU from the level above: on WebGPU through a 2D-array view, the only
     * one compatibility mode binds) */
    {
      static uint8_t ck[64 * 64 * 4 * 4], gr[64 * 64 * 4 * 4];
      for (int i = 0; i < 64 * 64 * 4; i++) {
        uint8_t v = (((i % 64) + (i / 64)) & 1) ? 255 : 0;
        ck[i * 4] = ck[i * 4 + 1] = ck[i * 4 + 2] = v; ck[i * 4 + 3] = 255;
        gr[i * 4] = gr[i * 4 + 1] = gr[i * 4 + 2] = 128; gr[i * 4 + 3] = 255;
      }
      t3_texture *keep_map = layer_map;
      t3_texture *mc = t3_data_array_texture_new(64, 64, 4, ck, T3_RGBA_FORMAT, T3_UNSIGNED_BYTE_TYPE);
      t3_texture *mg = t3_data_array_texture_new(64, 64, 4, gr, T3_RGBA_FORMAT, T3_UNSIGNED_BYTE_TYPE);
      t3_texture *ts[2] = { mc, mg };
      for (int k = 0; k < 2; k++) {
        ts[k]->wrap_s = ts[k]->wrap_t = T3_REPEAT;
        ts[k]->mag_filter = T3_LINEAR;
        ts[k]->min_filter = T3_LINEAR_MIPMAP_LINEAR;
        ts[k]->generate_mipmaps = true;
      }
      t3_object_set_position(cam, 0, 0, 40);
      static uint8_t fc[W * H * 4], fg[W * H * 4];
      layer_map = mc; render(r5, fc);
      layer_map = mg; render(r5, fg);
      int far = 0, off = 0;
      for (int i = 0; i < W * H; i++) {
        int dd = 0;
        for (int c = 0; c < 3; c++) { int x = abs(fc[i * 4 + c] - fg[i * 4 + c]); if (x > dd) dd = x; }
        if (dd > 12) off++;
        if (fg[i * 4] > 30) far++;   /* (the lit grey plane, on the dark clear colour) */
      }
      CHECK(off <= 2, "a mipmapped checker array differs from flat grey at %d texels from far away (its mips)", off);
      CHECK(far > 4, "the far plane covers %d texels", far);
      t3_object_set_position(cam, 0, 0, 3);
      layer_map = keep_map;
      t3_release(mc); t3_release(mg);
    }
    t3_renderer_set_extension(r5, "layers", NULL, NULL);
    render(r5, le);
    CHECK(t3_renderer_info(r5)->skipped == 1, "with no hook %u draws were skipped, not 1", t3_renderer_info(r5)->skipped);
    t3_renderer_destroy(r5);
    t3_release(layer_index); t3_release(layer_map);
  }

  /* many textures */
  {
    uint8_t px[16] = { 200, 120, 60, 255, 60, 200, 120, 255, 120, 60, 200, 255, 255, 255, 255, 255 };
    for (int i = 0; i < 10; i++) {
      mosaic_tiles[i] = t3_data_texture_new(2, 2, px);
      mosaic_tiles[i]->mag_filter = mosaic_tiles[i]->min_filter = T3_LINEAR;
    }
    uint8_t hpx[64];
    for (int i = 0; i < 64; i++) hpx[i] = (uint8_t)((i * 37) & 255);
    mosaic_height = t3_data_texture_new(4, 4, hpx);
    t3_mesh *plane = (t3_mesh *)scene->base.children[scene->base.child_count - 1];
    t3_material *mm = t3_mesh_standard_material_new(0xffffff);
    t3_material_set_extension(mm, "mosaic");
    t3_geometry *pg = t3_plane_geometry_new(2.4f, 2.4f, 8, 8);
    t3_mesh *mp = t3_mesh_new(pg, mm);
    plane->base.visible = false;
    t3_object_add(&scene->base, mp);
    t3_release(mp); t3_release(pg); t3_release(mm);
    static uint8_t m0[W * H * 4], m1[W * H * 4], m2[W * H * 4];
    t3_renderer *r6 = renderer();
    t3_renderer_set_extension(r6, "mosaic", fill_mosaic, NULL);
    render(r6, m0);
    CHECK(t3_renderer_info(r6)->skipped == 0, "the mosaic scene skipped %u draws: %s (%s)", t3_renderer_info(r6)->skipped,
          t3_renderer_last_error(r6) ? t3_renderer_last_error(r6) : "", t3_renderer_generated_missing(r6) ? t3_renderer_generated_missing(r6) : "");
    for (int i = 0; i < 16; i += 4) { mosaic_tiles[9]->pixels[i] ^= 0xff; mosaic_tiles[9]->pixels[i + 2] ^= 0xff; }
    mosaic_tiles[9]->version++;
    render(r6, m1);
    d = differ(m0, m1, 0, W);
    CHECK(d > 500, "new texels in the tenth tile changed only %d texels", d);
    for (int i = 0; i < 64; i++) mosaic_height->pixels[i] = (uint8_t)(255 - mosaic_height->pixels[i]);
    mosaic_height->version++;
    render(r6, m2);
    d = differ(m1, m2, 0, W);
    CHECK(d > 500, "a new height texture changed only %d texels", d);
    t3_renderer_destroy(r6);
    for (int i = 0; i < 10; i++) t3_release(mosaic_tiles[i]);
    t3_release(mosaic_height);
  }

  t3_release(scene); t3_release(cam);
  t3_release(other); t3_release(tint_map); t3_release(map); t3_release(g); t3_release(mat);
  t3_renderer_destroy(r); t3_renderer_destroy(r2); t3_renderer_destroy(r3);
#ifdef T3_WGPU
  CHECK(wgpu_errors == 0, "%d WebGPU validation errors", wgpu_errors);
#endif
  if (!failures) printf("test_extension (" BACKEND "%s): ok\n", MODE);
  return failures ? 1 : 0;
}
