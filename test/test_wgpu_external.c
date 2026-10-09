/* The WebGPU embedder hooks: a t3_external_target drawn into the caller's
 * colour and depth views (its load ops, clear values and rect), recorded on
 * the caller's encoder, flip_y for attachments stored bottom-up, and
 * t3_renderer_render_depth_to on a caller's depth view, and an embedder's
 * cube (t3_texture_from_wgpu) as background and PMREM source. A MeshNormalMaterial
 * box seen at an angle colours each face by its normal, so a wrong front
 * face (back faces drawn) changes the picture, not just a mirror.
 *
 * Build after WGPU=1 ./build.sh (test/run.sh does this when Dawn is there):
 *   cc -std=c99 -O2 -DT3_WGPU -I include -I src -I <dawn>/include test/test_wgpu_external.c \
 *     build/native-wgpu/libthree.a -L <dawn>/lib -lwebgpu_dawn -Wl,-rpath,<dawn>/lib -lEGL -lGLESv2 -lz -lm
 *
 * Exits 0 on success, 77 when no WebGPU adapter is available. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webgpu/webgpu.h>

#include "three.h"

#define S 64

static WGPUInstance inst;
static WGPUDevice dev;
static WGPUQueue queue;
static int errors;

static void adapter_cb(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUAdapter *)u1 = st == WGPURequestAdapterStatus_Success ? a : NULL; *(int *)u2 = 1;
}
static void device_cb(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUDevice *)u1 = st == WGPURequestDeviceStatus_Success ? d : NULL; *(int *)u2 = 1;
}
static void error_cb(WGPUDevice const *d, WGPUErrorType t, WGPUStringView m, void *u1, void *u2) {
  (void)d; (void)u1; (void)u2;
  if (errors++ < 8) fprintf(stderr, "webgpu error %d: %.*s\n", (int)t, (int)m.length, m.data);
}
static void map_cb(WGPUMapAsyncStatus st, WGPUStringView m, void *u1, void *u2) { (void)m; (void)u2; *(int *)u1 = st == WGPUMapAsyncStatus_Success ? 1 : -1; }

static WGPUTexture texture(WGPUTextureFormat f) {
  WGPUTextureDescriptor d = { .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding, .dimension = WGPUTextureDimension_2D,
                              .size = { S, S, 1 }, .format = f, .mipLevelCount = 1, .sampleCount = 1 };
  return wgpuDeviceCreateTexture(dev, &d);
}
/* the texture's rows as stored, row 0 first (4 bytes a texel) */
static void read_back(WGPUTexture t, bool depth, uint8_t *out) {
  WGPUBufferDescriptor bd = { .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead, .size = 256 * S };
  WGPUBuffer b = wgpuDeviceCreateBuffer(dev, &bd);
  WGPUCommandEncoder e = wgpuDeviceCreateCommandEncoder(dev, NULL);
  WGPUTexelCopyTextureInfo src = { .texture = t, .aspect = depth ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All };
  WGPUTexelCopyBufferInfo dst = { .buffer = b, .layout = { .bytesPerRow = 256, .rowsPerImage = S } };
  WGPUExtent3D ext = { S, S, 1 };
  wgpuCommandEncoderCopyTextureToBuffer(e, &src, &dst, &ext);
  WGPUCommandBuffer cb = wgpuCommandEncoderFinish(e, NULL);
  wgpuQueueSubmit(queue, 1, &cb);
  int done = 0;
  WGPUBufferMapCallbackInfo mi = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = map_cb, .userdata1 = &done };
  wgpuBufferMapAsync(b, WGPUMapMode_Read, 0, 256 * S, mi);
  while (!done) wgpuInstanceProcessEvents(inst);
  const uint8_t *m = wgpuBufferGetConstMappedRange(b, 0, 256 * S);
  for (int y = 0; y < S; y++) memcpy(out + y * S * 4, m + y * 256, S * 4);
  wgpuBufferUnmap(b);
  wgpuBufferRelease(b);
  wgpuCommandBufferRelease(cb);
  wgpuCommandEncoderRelease(e);
}
/* texels of a and b (S x S, 4 bytes) differing, b read upside down when flipped */
static int differ(const uint8_t *a, const uint8_t *b, bool flipped) {
  int n = 0;
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++) {
      const uint8_t *p = a + (y * S + x) * 4, *q = b + ((flipped ? S - 1 - y : y) * S + x) * 4;
      for (int c = 0; c < 4; c++) if (abs(p[c] - q[c]) > 2) { n++; break; }
    }
  return n;
}
static int distinct(const uint8_t *a) {
  uint32_t seen[64]; int n = 0;
  for (int i = 0; i < S * S && n < 64; i++) {
    uint32_t v; memcpy(&v, a + i * 4, 4);
    int k = 0; while (k < n && seen[k] != v) k++;
    if (k == n) seen[n++] = v;
  }
  return n;
}


/* six faces of one colour each, 0 / 255 per channel (exact as half floats too) */
static const uint8_t face_rgb[6][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 } };
#define CS 16
/* an embedder's RGBA16Float cube (one level), as a procedural sky would make */
static WGPUTexture half_cube(void) {
  WGPUTextureBindingViewDimension bvd = { .chain = { .sType = WGPUSType_TextureBindingViewDimension }, .textureBindingViewDimension = WGPUTextureViewDimension_Cube };
  WGPUTextureDescriptor d = { .nextInChain = &bvd.chain, .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, .dimension = WGPUTextureDimension_2D,
                              .size = { CS, CS, 6 }, .format = WGPUTextureFormat_RGBA16Float, .mipLevelCount = 1, .sampleCount = 1 };
  WGPUTexture t = wgpuDeviceCreateTexture(dev, &d);
  static uint16_t px[CS * CS * 4];
  for (int f = 0; f < 6; f++) {
    for (int i = 0; i < CS * CS; i++)
      for (int c = 0; c < 4; c++) px[i * 4 + c] = c == 3 || face_rgb[f][c] ? 0x3C00 : 0;   /* 1.0 / 0.0 */
    WGPUTexelCopyTextureInfo dst = { .texture = t, .origin = { 0, 0, (uint32_t)f }, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferLayout lay = { .bytesPerRow = CS * 8, .rowsPerImage = CS };
    WGPUExtent3D ext = { CS, CS, 1 };
    wgpuQueueWriteTexture(queue, &dst, px, sizeof px, &lay, &ext);
  }
  return t;
}
/* the same cube uploaded by three.c */
static t3_texture *cpu_cube(void) {
  static uint8_t faces[6][CS * CS * 4];
  const uint8_t *fp[6];
  for (int f = 0; f < 6; f++) {
    for (int i = 0; i < CS * CS; i++) { memcpy(faces[f] + i * 4, face_rgb[f], 3); faces[f][i * 4 + 3] = 255; }
    fp[f] = faces[f];
  }
  t3_texture *t = t3_cube_texture_new(CS, fp);
  t->generate_mipmaps = false;
  t->min_filter = T3_LINEAR;
  return t;
}
/* a sky (background) and a mirror sphere lit by the sky's PMREM, into the caller's view */
static void sky_frame(t3_renderer *r, t3_texture *cube, const t3_external_target *et, WGPUTexture ct, uint8_t *out) {
  t3_scene *s = t3_scene_new();
  t3_scene_set_background_texture(s, cube);
  t3_pmrem_generator *pg = t3_pmrem_generator_new(r);
  t3_render_target *env = t3_pmrem_from_cubemap(pg, cube);
  t3_geometry *g = t3_sphere_geometry_new(1, 32, 16);
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->metalness = 1; m->roughness = 0.4f;
  t3_material_set_texture(m, T3_ENV_MAP, env->texture);
  t3_mesh *ball = t3_mesh_new(g, m);
  t3_object_add(&s->base, ball);
  t3_camera *cam = t3_perspective_camera_new(70, 1, 0.1f, 20);
  t3_object_set_position(cam, 0.5f, 0.8f, 3);
  t3_object_look_at(cam, 0, 0, 0);
  t3_renderer_set_external_target(r, et);
  t3_renderer_render(r, s, cam);
  t3_renderer_wgpu_submit(r);
  read_back(ct, false, out);
  t3_release(ball); t3_release(g); t3_release(m); t3_release(cam);
  t3_release(env);
  t3_pmrem_generator_destroy(pg);
  t3_release(s);
}

/* a striped wall behind a glass block (transmission), into the caller's view */
static void glass_frame(t3_renderer *r, const t3_external_target *et, WGPUTexture ct, uint8_t *out) {
  t3_scene *s = t3_scene_new();
  t3_geometry *bar = t3_box_geometry_new(0.3f, 4, 0.2f, 1, 1, 1);
  for (int i = 0; i < 8; i++) {
    t3_material *m = t3_mesh_basic_material_new(i % 2 ? 0xe04040 : 0x40c060);
    t3_mesh *b = t3_mesh_new(bar, m);
    t3_object_set_position(b, -2.1f + i * 0.6f, (float)(i % 3) * 0.3f, -2);
    t3_object_add(&s->base, b);
    t3_release(b); t3_release(m);
  }
  t3_geometry *g = t3_box_geometry_new(1.4f, 1.4f, 1.4f, 1, 1, 1);
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->metalness = 0; m->roughness = 0.1f; m->transmission = 1; m->thickness = 1; m->ior = 1.5f;
  t3_mesh *glass = t3_mesh_new(g, m);
  t3_object_set_rotation(glass, 0.5f, 0.7f, 0);
  t3_object_set_position(glass, 0.2f, 0.3f, 0);
  t3_object_add(&s->base, glass);
  t3_light *sun = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(sun, 2, 3, 4);
  t3_object_add(&s->base, sun);
  t3_camera *cam = t3_perspective_camera_new(50, 1, 0.1f, 20);
  t3_object_set_position(cam, 0, 0, 4);
  t3_renderer_set_external_target(r, et);
  t3_renderer_render(r, s, cam);
  t3_renderer_wgpu_submit(r);
  read_back(ct, false, out);
  t3_release(glass); t3_release(g); t3_release(m); t3_release(bar); t3_release(sun); t3_release(cam); t3_release(s);
}

void t3__wgpu_counters(t3_renderer *r, unsigned out[3]);   /* (renderer_wgpu.inc) */
static int fails;
static void check(bool ok, const char *what) { printf("%s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

int main(void) {
  inst = wgpuCreateInstance(NULL);
  WGPUAdapter ad = NULL; int done = 0;
  WGPURequestAdapterOptions ao = { .powerPreference = WGPUPowerPreference_HighPerformance };
  wgpuInstanceRequestAdapter(inst, &ao, (WGPURequestAdapterCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = adapter_cb, .userdata1 = &ad, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!ad) { puts("test_wgpu_external: no WebGPU adapter, skipped"); return 77; }
  WGPUFeatureName feats[] = { WGPUFeatureName_Float32Filterable };
  WGPUDeviceDescriptor dd = { .uncapturedErrorCallbackInfo = { .callback = error_cb }, .requiredFeatureCount = 1, .requiredFeatures = feats };
  done = 0;
  wgpuAdapterRequestDevice(ad, &dd, (WGPURequestDeviceCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = device_cb, .userdata1 = &dev, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!dev) { puts("test_wgpu_external: no WebGPU device, skipped"); return 77; }
  queue = wgpuDeviceGetQueue(dev);

  t3_renderer *r = t3_renderer_new(S, S);
  t3_renderer_use_wgpu(r, dev, queue, WGPUTextureFormat_RGBA8Unorm);
  t3_renderer_set_output_color_space(r, T3_LINEAR_SRGB_COLOR_SPACE);   /* drawn directly into the caller's view */
  t3_scene *scene = t3_scene_new();
  t3_camera *cam = t3_perspective_camera_new(50, 1, 0.1f, 20);
  t3_object_set_position(cam, 1.5f, 2.0f, 4);
  t3_object_look_at(cam, 0, 0.3f, 0);
  t3_geometry *g = t3_box_geometry_new(1.4f, 1, 1, 1, 1, 1);
  t3_material *m = t3_mesh_normal_material_new();
  t3_mesh *box = t3_mesh_new(g, m);
  t3_object_set_position(box, 0.3f, 0.6f, 0);   /* off centre: the flip is visible */
  box->base.cast_shadow = true;                 /* (render_depth_to draws the casters) */
  t3_object_add(&scene->base, box);
  t3_release(box); t3_release(g); t3_release(m);

  WGPUTexture ct = texture(WGPUTextureFormat_RGBA8Unorm), dt = texture(WGPUTextureFormat_Depth32Float);
  WGPUTextureView cv = wgpuTextureCreateView(ct, NULL), dv = wgpuTextureCreateView(dt, NULL);
  t3_external_target et = { .color = (uintptr_t)cv, .depth = (uintptr_t)dv, .color_format = WGPUTextureFormat_RGBA8Unorm,
                            .depth_format = WGPUTextureFormat_Depth32Float, .samples = 1,
                            .clear_rgba = { 0.1f, 0.2f, 0.6f, 1 }, .clear_depth = 1, .x = 0, .y = 0, .w = S, .h = S };
  static uint8_t a[S * S * 4], b[S * S * 4], c[S * S * 4], da[S * S * 4], db[S * S * 4];

  /* A: plain */
  t3_renderer_set_external_target(r, &et);
  t3_renderer_render(r, scene, cam);
  t3_renderer_wgpu_submit(r);
  read_back(ct, false, a);
  check(distinct(a) >= 4, "the caller's colour view holds the scene (box faces and clear colour)");
  printf("      corner texel %d %d %d %d\n", a[0], a[1], a[2], a[3]);
  check(abs(a[0] - 26) <= 1 && abs(a[1] - 51) <= 1 && abs(a[2] - 153) <= 1, "clear_rgba cleared the caller's view");

  /* B: flip_y: the same picture, rows stored bottom-up */
  et.flip_y = true;
  t3_renderer_set_external_target(r, &et);
  t3_renderer_render(r, scene, cam);
  t3_renderer_wgpu_submit(r);
  read_back(ct, false, b);
  int nb = differ(a, b, true);
  printf("      flip_y: %d texels differ from A mirrored\n", nb);
  check(nb <= S / 4, "flip_y stores A's rows bottom-up, front faces kept");
  check(differ(a, b, false) > 40, "flip_y changed the stored rows");

  /* C: the caller's encoder, a rect (the top half as stored) and LOAD: the rest keeps B */
  et.flip_y = false;
  et.load_color = et.load_depth = true;
  et.x = 0; et.y = 0; et.w = S; et.h = S / 2;
  WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(dev, NULL);
  t3_renderer_set_external_encoder(r, (uintptr_t)enc);
  t3_renderer_set_external_target(r, &et);
  t3_renderer_render(r, scene, cam);
  t3_renderer_set_external_encoder(r, 0);
  WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, NULL);
  wgpuQueueSubmit(queue, 1, &cb);
  wgpuCommandBufferRelease(cb);
  wgpuCommandEncoderRelease(enc);
  read_back(ct, false, c);
  check(!memcmp(c + S / 2 * S * 4, b + S / 2 * S * 4, S / 2 * S * 4), "outside the rect the loaded colour is kept");
  check(memcmp(c, b, S / 2 * S * 4) != 0, "inside the rect the render on the caller's encoder landed");

  /* D: render_depth_to on the caller's depth view, plain and flipped */
  et.load_depth = false; et.x = 0; et.y = 0; et.w = S; et.h = S;
  t3_renderer_render_depth_to(r, scene, cam, &et);
  t3_renderer_wgpu_submit(r);
  read_back(dt, true, da);
  int near = 0, far = 0;
  for (int i = 0; i < S * S; i++) { float z; memcpy(&z, da + i * 4, 4); if (z < 1) near++; else far++; }
  printf("      render_depth_to: %d texels with depth, %d at the clear depth\n", near, far);
  check(near > 100 && far > 100, "render_depth_to wrote the casters' depth");
  et.flip_y = true;
  t3_renderer_render_depth_to(r, scene, cam, &et);
  t3_renderer_wgpu_submit(r);
  read_back(dt, true, db);
  int nd = 0;
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++) {
      float p, q; memcpy(&p, da + (y * S + x) * 4, 4); memcpy(&q, db + ((S - 1 - y) * S + x) * 4, 4);
      if ((p < 1) != (q < 1)) nd++;
    }
  printf("      render_depth_to flip_y: %d texels differ from the plain depth mirrored\n", nd);
  check(nd <= S / 4, "render_depth_to with flip_y stores the depth bottom-up");

  /* E: an embedder's RGBA16Float cube (t3_texture_from_wgpu) as background and
   * PMREM source: the same pixels as three.c's own upload of the same cube */
  et.flip_y = false; et.load_color = et.load_depth = false;
  static uint8_t ea[S * S * 4], eb[S * S * 4];
  WGPUTexture hc = half_cube();
  t3_texture *adopted = t3_texture_from_wgpu(hc, NULL, CS, CS, 1, true, false);
  check(adopted != NULL, "t3_texture_from_wgpu made a texture");
  sky_frame(r, adopted, &et, ct, ea);
  t3_texture *own = cpu_cube();
  sky_frame(r, own, &et, ct, eb);
  int ne = differ(ea, eb, false);
  printf("      adopted cube vs three.c's upload: %d texels differ, %d colours\n", ne, distinct(ea));
  check(distinct(ea) >= 6, "the adopted cube drew a sky and a lit sphere");
  check(ne <= 4, "an adopted cube draws as three.c's own upload of it (background and PMREM)");
  t3_release(adopted);
  t3_release(own);
  wgpuTextureRelease(hc);   /* (the embedder's, released after the t3_texture) */
  /* released textures' views are gone: a new texture must not hit a stale bind group */
  t3_texture *again = cpu_cube();
  sky_frame(r, again, &et, ct, ea);
  check(differ(ea, eb, false) <= 4, "after releases, a new texture draws as before");
  t3_release(again);

  /* F: transmission on the caller's target: its colour view is the backdrop the
   * glass copies; with flip_y the same picture stored bottom-up. The control
   * is the glass with transmission off (the backdrop must show through). */
  static uint8_t fa[S * S * 4], fb[S * S * 4];
  et.flip_y = false; et.load_color = et.load_depth = false; et.x = 0; et.y = 0; et.w = S; et.h = S;
  glass_frame(r, &et, ct, fa);
  et.flip_y = true;
  glass_frame(r, &et, ct, fb);
  int nf = differ(fa, fb, true);
  printf("      transmission flip_y: %d texels differ from the plain render mirrored\n", nf);
  check(nf <= S / 4, "transmission on a flip_y target draws the same picture, rows bottom-up");
  int green = 0;   /* the backdrop's stripes seen through the glass, at its centre */
  for (int y = S / 2 - 4; y < S / 2 + 4; y++) for (int x = S / 2 - 4; x < S / 2 + 4; x++) { const uint8_t *p = fa + (y * S + x) * 4; if (p[1] > p[0] + 20 || p[0] > p[1] + 20) green++; }
  check(green > 8, "the glass shows the coloured backdrop (copied from the caller's view)");
  et.flip_y = false;

  /* G: PMREM and the scene recorded on one external encoder, one submit (the
   * embedder's frame): the same picture as each submitting on its own */
  {
    static uint8_t ga[S * S * 4], gb[S * S * 4];
    t3_texture *cube = cpu_cube();
    sky_frame(r, cube, &et, ct, ga);   /* own submits */
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(dev, NULL);
    t3_renderer_set_external_encoder(r, (uintptr_t)enc);
    t3_scene *sc = t3_scene_new();
    t3_scene_set_background_texture(sc, cube);
    t3_pmrem_generator *pg = t3_pmrem_generator_new(r);
    t3_render_target *env = t3_pmrem_from_cubemap(pg, cube);   /* recorded on enc */
    t3_geometry *g = t3_sphere_geometry_new(1, 32, 16);
    t3_material *m = t3_mesh_standard_material_new(0xffffff);
    m->metalness = 1; m->roughness = 0.4f;
    t3_material_set_texture(m, T3_ENV_MAP, env->texture);
    t3_mesh *ball = t3_mesh_new(g, m);
    t3_object_add(&sc->base, ball);
    t3_camera *cam = t3_perspective_camera_new(70, 1, 0.1f, 20);
    t3_object_set_position(cam, 0.5f, 0.8f, 3);
    t3_object_look_at(cam, 0, 0, 0);
    t3_renderer_set_external_encoder(r, (uintptr_t)enc);   /* (the embedder sets it again for its scene pass) */
    t3_renderer_set_external_target(r, &et);
    t3_renderer_render(r, sc, cam);
    t3_renderer_set_external_encoder(r, 0);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, NULL);
    wgpuQueueSubmit(queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);
    read_back(ct, false, gb);
    int ng = differ(ga, gb, false);
    printf("      PMREM + scene on one external encoder vs own submits: %d texels differ\n", ng);
    check(ng <= 4, "PMREM recorded on the embedder's encoder lights the scene as when it submits itself");
    t3_release(ball); t3_release(g); t3_release(m); t3_release(cam); t3_release(env);
    t3_pmrem_generator_destroy(pg); t3_release(sc); t3_release(cube);
  }

  /* E: an embedder's frame loop (its encoder each frame, its submit, then
   * t3_renderer_wgpu_submit): after the first frame the per-draw uniform
   * stream keeps its buffer, so no bind group is made, and a draw re-sets
   * only the pass state that changed. The control: a material given a new
   * texture must make one, or the count proves nothing. */
  {
    t3_scene *sc = t3_scene_new();
    t3_geometry *bg = t3_box_geometry_new(0.6f, 0.6f, 0.6f, 1, 1, 1);
    uint8_t px[4 * 4 * 4];
    for (int i = 0; i < 16; i++) { px[i * 4] = (uint8_t)(i * 16); px[i * 4 + 1] = 200; px[i * 4 + 2] = (uint8_t)(255 - i * 16); px[i * 4 + 3] = 255; }
    t3_texture *t1 = t3_texture_new(4, 4, px), *t2 = t3_texture_new(4, 4, px);
    t3_material *tm = t3_mesh_basic_material_new(0xffffff);
    t3_material_set_map(tm, t1);
    for (int i = 0; i < 6; i++) {   /* six boxes, two programs, one geometry: state a draw can keep */
      t3_material *mm = i % 2 ? t3_mesh_normal_material_new() : tm;
      t3_mesh *b = t3_mesh_new(bg, mm);
      t3_object_set_position(b, -1.2f + (float)(i % 3) * 1.2f, i < 3 ? 0.7f : -0.7f, 0);
      t3_object_add(&sc->base, b);
      t3_release(b);
      if (i % 2) t3_release(mm);
    }
    t3_camera *ec = t3_perspective_camera_new(50, 1, 0.1f, 20);
    t3_object_set_position(ec, 0, 0, 4);
    et.flip_y = false; et.load_color = et.load_depth = false;
    et.x = 0; et.y = 0; et.w = S; et.h = S;
    static uint8_t e0[S * S * 4], e1[S * S * 4];
    t3_renderer_set_auto_instancing(r, false);   /* six draws, not batches */
    unsigned c[7][3];
    for (int fr = 0; fr < 6; fr++) {
      if (fr == 4) t3_material_set_map(tm, t2);   /* the control frame */
      WGPUCommandEncoder fe = wgpuDeviceCreateCommandEncoder(dev, NULL);
      t3_renderer_set_external_encoder(r, (uintptr_t)fe);
      t3_renderer_set_external_target(r, &et);
      t3_renderer_render(r, sc, ec);
      t3_renderer_set_external_encoder(r, 0);
      WGPUCommandBuffer fb = wgpuCommandEncoderFinish(fe, NULL);
      wgpuQueueSubmit(queue, 1, &fb);
      wgpuCommandBufferRelease(fb);
      wgpuCommandEncoderRelease(fe);
      t3_renderer_wgpu_submit(r);
      t3__wgpu_counters(r, c[fr]);
      if (fr == 0) read_back(ct, false, e0);
      if (fr == 3) read_back(ct, false, e1);
    }
    unsigned made = c[3][0] - c[0][0], sets = c[3][1] - c[0][1], skipped = c[3][2] - c[0][2];
    printf("      frames 2-4 on the embedder's encoder: %u bind groups made, %u pass sets, %u skipped; the new-texture frame made %u\n",
           made, sets, skipped, c[4][0] - c[3][0]);
    check(made == 0, "an embedder's frames after the first make no bind groups");
    check(c[4][0] - c[3][0] > 0, "control: a new texture makes a bind group (the count sees one)");
    check(skipped > 0, "a draw keeps the pass state it shares with the one before");
    t3_renderer_set_auto_instancing(r, true);
    check(!memcmp(e0, e1, sizeof e0), "the fourth frame matches the first");
    check(distinct(e0) >= 4, "the frame holds the scene");
    t3_release(ec); t3_release(tm); t3_release(t1); t3_release(t2); t3_release(bg); t3_release(sc);
  }

  const char *err = t3_renderer_last_error(r);
  check(!err, err ? err : "no renderer error");
  check(errors == 0, "no WebGPU validation errors");
  t3_release(scene); t3_release(cam);
  t3_renderer_destroy(r);
  puts(fails ? "test_wgpu_external: FAILED" : "test_wgpu_external: ok");
  return fails ? 1 : 0;
}
