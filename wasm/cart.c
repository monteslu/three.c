/* A wasmcart cart running one bench scene (bench/scenes.c), chosen at build
 * time with -DCART_SCENE='"05-heavy"'. The host owns the GL context and calls
 * wc_render once per frame. */
#define WC_USE_GL
#include "wasmcart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scenes.h"

#ifndef CART_SCENE
#define CART_SCENE "05-heavy"
#endif

#define WIDTH 1280
#define HEIGHT 720
#define AUDIO_CAP 1024

static uint32_t framebuffer[4]; /* GL cart: the host presents the GL surface */
static float audio_ring[AUDIO_CAP * 2];
static uint32_t audio_write_cursor;
static wc_pad_t pads[4];
static wc_time_t time_info;
static wc_info_t info;
static wc_host_info_t host_info;
static wc_pointer_t pointers[10]; /* mouse / touch, written by the host before wc_render */
static const bench_scene *scene;

bool bench_pointer(int *x, int *y) {
  if (!pointers[0].active) return false;
  *x = pointers[0].x;
  *y = pointers[0].y;
  return true;
}

char *bench_read_asset(const char *name, size_t *len) {
  int n = wc_asset_size(name, (unsigned)strlen(name));
  if (n < 0) return NULL;
  char *buf = malloc((size_t)n + 1);
  if (!buf || wc_load_asset(name, (unsigned)strlen(name), buf, (unsigned)n) != n) { free(buf); return NULL; }
  buf[n] = 0;
  *len = (size_t)n;
  return buf;
}

__attribute__((export_name("wc_get_info")))
wc_info_t *wc_get_info(void) {
  info.version = WC_ABI_VERSION;
  info.width = WIDTH;
  info.height = HEIGHT;
  info.fb_ptr = (uint32_t)(uintptr_t)framebuffer;
  info.audio_ptr = (uint32_t)(uintptr_t)audio_ring;
  info.audio_cap = AUDIO_CAP;
  info.audio_write_ptr = (uint32_t)(uintptr_t)&audio_write_cursor;
  info.input_ptr = (uint32_t)(uintptr_t)pads;
  info.time_ptr = (uint32_t)(uintptr_t)&time_info;
  info.host_info_ptr = (uint32_t)(uintptr_t)&host_info;
  info.pointer_ptr = (uint32_t)(uintptr_t)pointers;
  info.flags = WC_FLAG_AUDIO_F32 | WC_FLAG_POINTER;
#ifdef CART_WGPU
  info.gpu_api = 2;   /* imports WebGPU too; the host may still pick GL */
#endif
  return &info;
}

#ifdef CART_WGPU
/* a dual cart (WGPU=1 wasm/build.sh): GL and WebGPU imported; the host's
 * host-info flag picks one. Both routes run r186's generated programs. */
#include <webgpu/webgpu.h>
extern void *bench_wgpu_device, *bench_wgpu_queue;
static WGPUSurface surface;
static WGPUTextureFormat surface_format;
/* The device the way r186's WebGPUBackend asks for one: a compatibility
 * adapter, then a device with every feature the adapter has (which is core
 * where the hardware allows). wasmcart's tier keeps carts at compatibility
 * level whatever they ask (three.c then takes r186's compatibility programs,
 * t3_wgpu_compat), but grants features beyond the host's device. The answer
 * arrives between frames; frames before it draw nothing. Without an adapter:
 * the host's device. */
static int wgpu_state;   /* 0 not started, 1 waiting, 2 ready */
static WGPUInstance instance;
static void wgpu_configure(WGPUDevice device) {
  bench_wgpu_device = device;
  bench_wgpu_queue = wgpuDeviceGetQueue(device);
  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas = {
    .chain = { .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector }, .selector = { "#canvas", 7 } };
  WGPUSurfaceDescriptor sd = { .nextInChain = &canvas.chain };
  surface = wgpuInstanceCreateSurface(instance, &sd);
  WGPUSurfaceCapabilities caps = { 0 };
  wgpuSurfaceGetCapabilities(surface, NULL, &caps);
  surface_format = caps.formats[0];
  WGPUSurfaceConfiguration cfg = { .device = device, .format = surface_format, .usage = WGPUTextureUsage_RenderAttachment,
    .width = WIDTH, .height = HEIGHT, .alphaMode = WGPUCompositeAlphaMode_Opaque, .presentMode = WGPUPresentMode_Fifo };
  wgpuSurfaceConfigure(surface, &cfg);
  wgpu_state = 2;
}
static void on_device(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView msg, void *u1, void *u2) {
  (void)msg; (void)u1; (void)u2;
  wgpu_configure(st == WGPURequestDeviceStatus_Success ? d : emscripten_webgpu_get_device());
}
static void on_adapter(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView msg, void *u1, void *u2) {
  (void)msg; (void)u1; (void)u2;
  if (st != WGPURequestAdapterStatus_Success || !a) { wgpu_configure(emscripten_webgpu_get_device()); return; }
  WGPUSupportedFeatures sf = { 0 };
  wgpuAdapterGetFeatures(a, &sf);
  WGPUDeviceDescriptor dd = { .requiredFeatureCount = sf.featureCount, .requiredFeatures = sf.features };
  WGPURequestDeviceCallbackInfo ci = { .mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_device };
  wgpuAdapterRequestDevice(a, &dd, ci);
}
static void wgpu_up(void) {
  instance = wgpuCreateInstance(NULL);
  WGPURequestAdapterOptions ao = { .featureLevel = WGPUFeatureLevel_Compatibility, .powerPreference = WGPUPowerPreference_HighPerformance };
  WGPURequestAdapterCallbackInfo ci = { .mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_adapter };
  wgpu_state = 1;
  wgpuInstanceRequestAdapter(instance, &ao, ci);
}
#endif

__attribute__((export_name("wc_init")))
void wc_init(void) {
#ifdef CART_WGPU
  if (host_info.flags & WC_HOST_FLAG_GPU_WGPU) wgpu_up();
#endif
  for (const bench_scene *s = bench_scenes; s->name; s++)
    if (!strcmp(s->name, CART_SCENE)) scene = s;
  if (!scene) { WC_LOG("three.c cart: unknown scene"); return; }
#ifdef CART_WGPU
  if (wgpu_state) return;   /* set up once the device arrives (wc_render) */
#endif
  scene->setup();
}

static int frames;

__attribute__((export_name("wc_render")))
void wc_render(void) {
  if (!scene) return;
#ifdef CART_WGPU
  static int set_up;
  if (wgpu_state == 1) return;
  if (wgpu_state == 2 && !set_up) { set_up = 1; scene->setup(); }
  WGPUSurfaceTexture st = { 0 };
  WGPUTextureView view = NULL;
  if (surface) {
    wgpuSurfaceGetCurrentTexture(surface, &st);
    view = wgpuTextureCreateView(st.texture, NULL);
    t3_renderer_wgpu_set_output(bench_renderer(), view, (int)surface_format);
  }
#endif
  scene->frame();
#ifdef CART_WGPU
  if (surface) {
    t3_renderer_wgpu_submit(bench_renderer());
    t3_renderer_wgpu_set_output(bench_renderer(), NULL, 0);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(st.texture);
  }
#endif
  /* a scene's probe after frame 100, as the bench lanes read it (the first
   * BENCH_PROBE line in the cart's log) */
  if (++frames == 100 && scene->probe) {
    char line[96];
    snprintf(line, sizeof line, "BENCH_PROBE {\"probe\":%.17g}", scene->probe());
    wc_log(line, (unsigned)strlen(line));
  }
}
