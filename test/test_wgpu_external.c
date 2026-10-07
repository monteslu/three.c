/* The WebGPU embedder hooks: a t3_external_target drawn into the caller's
 * colour and depth views (its load ops, clear values and rect), recorded on
 * the caller's encoder, flip_y for attachments stored bottom-up, and
 * t3_renderer_render_depth_to on a caller's depth view. A MeshNormalMaterial
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
  WGPUTextureDescriptor d = { .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc, .dimension = WGPUTextureDimension_2D,
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

  const char *err = t3_renderer_last_error(r);
  check(!err, err ? err : "no renderer error");
  check(errors == 0, "no WebGPU validation errors");
  t3_release(scene); t3_release(cam);
  t3_renderer_destroy(r);
  puts(fails ? "test_wgpu_external: FAILED" : "test_wgpu_external: ok");
  return fails ? 1 : 0;
}
