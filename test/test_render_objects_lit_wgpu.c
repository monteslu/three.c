/* t3_renderer_render_objects_lit on WebGPU, drawn as an embedder draws: into its own colour and depth views
 * (t3_external_target), every pass recorded on its own command encoder (t3_renderer_set_external_encoder), which it
 * then submits and reads back. The scene and the check are test_render_objects_lit.c's: 60 boxes
 * (MeshStandardMaterial, a colour each) on a ground plane, an ambient light and a directional light casting shadows
 * (PCF); every third box is the set. After a render of the whole scene (the shadow maps drawn from all of it), the
 * set is drawn two ways, with the shadow maps held, and must give the same picture byte for byte, at frames 1, 20
 * and 40:
 *   - the scene with everything outside the set hidden, through t3_renderer_render;
 *   - the whole scene left visible, through t3_renderer_render_objects_lit with the set.
 * The picture must be one (a share of pixels unlike the clear colour), and the set must be lit: its pixels differ
 * from what t3_renderer_render_objects (unlit: no lights) draws. No WebGPU validation error may be raised. The
 * control (T3_TEST_CONTROL=1) draws the set with t3_renderer_render_objects instead and must fail.
 *
 * Build after WGPU=1 ./build.sh (test/run.sh does this when Dawn is there):
 *   cc -std=c99 -O2 -DT3_WGPU -I include -I src -I <dawn>/include test/test_render_objects_lit_wgpu.c \
 *     build/native-wgpu/libthree.a -L <dawn>/lib -lwebgpu_dawn -Wl,-rpath,<dawn>/lib -lEGL -lGLESv2 -lz -lm
 *
 * Exits 0 on success, 1 on a failure, 77 when no WebGPU adapter is available. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webgpu/webgpu.h>

#include "three.h"

#define W 192   /* 768 bytes a row: a multiple of 256, as a texture to buffer copy needs */
#define H 144
#define N 60
#define FRAMES 40

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
                              .size = { W, H, 1 }, .format = f, .mipLevelCount = 1, .sampleCount = 1 };
  return wgpuDeviceCreateTexture(dev, &d);
}
static void read_back(WGPUTexture t, uint8_t *out) {
  WGPUBufferDescriptor bd = { .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead, .size = W * 4 * H };
  WGPUBuffer b = wgpuDeviceCreateBuffer(dev, &bd);
  WGPUCommandEncoder e = wgpuDeviceCreateCommandEncoder(dev, NULL);
  WGPUTexelCopyTextureInfo src = { .texture = t, .aspect = WGPUTextureAspect_All };
  WGPUTexelCopyBufferInfo dst = { .buffer = b, .layout = { .bytesPerRow = W * 4, .rowsPerImage = H } };
  WGPUExtent3D ext = { W, H, 1 };
  wgpuCommandEncoderCopyTextureToBuffer(e, &src, &dst, &ext);
  WGPUCommandBuffer cb = wgpuCommandEncoderFinish(e, NULL);
  wgpuQueueSubmit(queue, 1, &cb);
  int done = 0;
  WGPUBufferMapCallbackInfo mi = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = map_cb, .userdata1 = &done };
  wgpuBufferMapAsync(b, WGPUMapMode_Read, 0, W * 4 * H, mi);
  while (!done) wgpuInstanceProcessEvents(inst);
  if (done > 0) memcpy(out, wgpuBufferGetConstMappedRange(b, 0, W * 4 * H), W * 4 * H);
  else memset(out, 0, W * 4 * H), errors++;
  wgpuBufferUnmap(b);
  wgpuBufferRelease(b);
  wgpuCommandBufferRelease(cb);
  wgpuCommandEncoderRelease(e);
}

static t3_scene *s;
static t3_mesh *box[N], *ground;
static t3_camera *cam;
static t3_object *set[N], *all[N + 1];
static int nset, nall;

static void world(void) {
  s = t3_scene_new();
  t3_light *amb = t3_ambient_light_new(0x404858, 0.6f);
  t3_object_add(&s->base, amb);
  t3_release(amb);
  t3_light *sun = t3_directional_light_new(0xfff0e0, 2.5f);
  t3_object_set_position(sun, 4, 9, 3);
  sun->base.cast_shadow = true;
  t3_object_add(&s->base, sun);
  t3_release(sun);
  t3_geometry *pg = t3_plane_geometry_new(30, 30, 1, 1);
  t3_material *pm = t3_mesh_standard_material_new(0x808070);
  ground = t3_mesh_new(pg, pm);
  t3_object_set_rotation(ground, -1.5707963f, 0, 0);
  ground->base.receive_shadow = true;
  t3_object_add(&s->base, ground);
  t3_release(ground);
  t3_release(pm);
  t3_release(pg);
  all[nall++] = &ground->base;
  t3_geometry *g = t3_box_geometry_new(0.5f, 0.9f, 0.5f, 1, 1, 1);
  for (int i = 0; i < N; i++) {
    t3_material *m = t3_mesh_standard_material_new(0x305070u + (uint32_t)i * 0x030201u);
    box[i] = t3_mesh_new(g, m);
    box[i]->base.cast_shadow = box[i]->base.receive_shadow = true;
    t3_object_add(&s->base, box[i]);
    t3_release(box[i]);
    t3_release(m);
    all[nall++] = &box[i]->base;
    if (i % 3 == 0) set[nset++] = &box[i]->base;
  }
  t3_release(g);
  cam = t3_perspective_camera_new(50, (float)W / H, 0.5f, 60);
  t3_object_set_position(cam, 0, 6, 11);
  t3_object_look_at(cam, 0, 0, 0);
}

static void pose(int f) {
  for (int i = 0; i < N; i++) {
    float x = (float)(i % 10) * 1.0f - 4.5f, z = (float)(i / 10) * 1.1f - 3.0f;
    t3_object_set_position(box[i], x, 0.45f + 0.4f * (float)((f * 5 + i * 11) % 13) / 13.0f, z);
    t3_object_set_rotation(box[i], 0, (float)((f + i) % 23) * 0.25f, 0);
  }
  t3_object_update_matrix_world(s, false);
}

static bool in_set(const t3_object *o) {
  for (int i = 0; i < nset; i++) if (set[i] == o) return true;
  return false;
}

enum { RENDER, OBJECTS, OBJECTS_LIT };
static t3_renderer *r;
static WGPUTexture ct;
static t3_external_target et;
/* one render the embedder's way: on its encoder, into its views, submitted by it */
static void draw(int how, uint8_t *out) {
  WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(dev, NULL);
  t3_renderer_set_external_encoder(r, (uintptr_t)enc);
  t3_renderer_set_external_target(r, &et);
  if (how == RENDER) t3_renderer_render(r, s, cam);
  else if (how == OBJECTS) t3_renderer_render_objects(r, s, cam, set, nset);
  else t3_renderer_render_objects_lit(r, s, cam, set, nset);
  t3_renderer_set_external_encoder(r, 0);
  WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, NULL);
  wgpuQueueSubmit(queue, 1, &cb);
  wgpuCommandBufferRelease(cb);
  wgpuCommandEncoderRelease(enc);
  if (out) read_back(ct, out);
}

int main(void) {
  bool control = getenv("T3_TEST_CONTROL") != NULL;
  inst = wgpuCreateInstance(NULL);
  WGPUAdapter ad = NULL; int done = 0;
  WGPURequestAdapterOptions ao = { .powerPreference = WGPUPowerPreference_HighPerformance };
  wgpuInstanceRequestAdapter(inst, &ao, (WGPURequestAdapterCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = adapter_cb, .userdata1 = &ad, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!ad) { puts("test_render_objects_lit_wgpu: no WebGPU adapter, skipped"); return 77; }
  WGPUDeviceDescriptor dd = { .uncapturedErrorCallbackInfo = { .callback = error_cb } };
  done = 0;
  wgpuAdapterRequestDevice(ad, &dd, (WGPURequestDeviceCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = device_cb, .userdata1 = &dev, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!dev) { puts("test_render_objects_lit_wgpu: no WebGPU device, skipped"); return 77; }
  queue = wgpuDeviceGetQueue(dev);

  world();
  r = t3_renderer_new(W, H);
  t3_renderer_use_wgpu(r, dev, queue, WGPUTextureFormat_RGBA8Unorm);
  t3_renderer_set_output_color_space(r, T3_LINEAR_SRGB_COLOR_SPACE);   /* drawn directly into the caller's view */
  t3_renderer_set_shadow_map(r, true, T3_PCF_SHADOW_MAP);
  ct = texture(WGPUTextureFormat_RGBA8Unorm);
  WGPUTexture dt = texture(WGPUTextureFormat_Depth32Float);
  WGPUTextureView cv = wgpuTextureCreateView(ct, NULL), dv = wgpuTextureCreateView(dt, NULL);
  et = (t3_external_target){ .color = (uintptr_t)cv, .depth = (uintptr_t)dv, .color_format = WGPUTextureFormat_RGBA8Unorm,
                             .depth_format = WGPUTextureFormat_Depth32Float, .samples = 1,
                             .clear_rgba = { 0x10 / 255.0f, 0x20 / 255.0f, 0x30 / 255.0f, 1 }, .clear_depth = 1, .w = W, .h = H };
  static uint8_t a[W * H * 4], b[W * H * 4], u[W * H * 4];
  int fail = 0;
  for (int f = 1; f <= FRAMES; f++) {
    pose(f);
    if (f != 1 && f != FRAMES / 2 && f != FRAMES) continue;
    /* the frame's shadow maps, from the whole scene */
    t3_renderer_set_shadow_hold(r, false);
    draw(RENDER, NULL);
    t3_renderer_set_shadow_hold(r, true);
    /* the whole-scene way: everything outside the set hidden */
    for (int i = 0; i < nall; i++) all[i]->visible = in_set(all[i]);
    draw(RENDER, a);
    for (int i = 0; i < nall; i++) all[i]->visible = true;
    /* the set alone, lit (the control: unlit) */
    draw(control ? OBJECTS : OBJECTS_LIT, b);
    /* what the unlit way draws, to prove the light shows */
    draw(OBJECTS, u);
    t3_renderer_set_shadow_hold(r, false);
    int diff = 0, lit = 0;
    unsigned drawn = 0;
    for (int i = 0; i < W * H * 4; i++) diff += a[i] != b[i], lit += a[i] != u[i];
    for (int i = 0; i < W * H * 4; i += 4) drawn += memcmp(a + i, a, 4) != 0;
    printf("frame %d: %d of %d meshes drawn, %d bytes differ, %d bytes unlike the unlit draw, %u pixels unlike the first\n", f, nset,
           nall, diff, lit, drawn);
    if (diff) printf("FAIL  frame %d: the set drawn alone differs from the scene with the rest hidden (%d bytes)\n", f, diff), fail = 1;
    if (lit < W * H / 10) printf("FAIL  frame %d: the set does not look lit\n", f), fail = 1;
    if (drawn < (unsigned)(W * H / 20)) printf("FAIL  frame %d: not a picture\n", f), fail = 1;
  }
  if (t3_renderer_info(r)->skipped) printf("FAIL  %u draws skipped (a program missing)\n", t3_renderer_info(r)->skipped), fail = 1;
  const char *e = t3_renderer_last_error(r);
  if (e && *e) printf("FAIL  renderer error: %s\n", e), fail = 1;
  if (errors) printf("FAIL  %d WebGPU validation errors\n", errors), fail = 1;
  t3_renderer_destroy(r);
  t3_release(cam);
  t3_release(s);
  wgpuTextureViewRelease(cv);
  wgpuTextureViewRelease(dv);
  wgpuTextureRelease(ct);
  wgpuTextureRelease(dt);
  if (!fail) puts("ok    the lit set drawn alone equals the scene with the rest hidden, byte for byte");
  return fail;
}
