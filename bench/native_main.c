/* Native bench host: a three.c scene on an offscreen EGL pbuffer (GLES 3.0),
 * timed with the method of three.lua's bench/lib/measure.mjs so the numbers
 * sit next to its three.js and three.lua results.
 *
 *   bench-native <scene> --mode time  [--min-ms N --reps N --warm-ms N --warm-frames N]
 *   bench-native <scene> --mode check --frames N --png out.png
 *   bench-native <scene> --mode cpu   [--frames N]   CPU time to issue one frame, the GPU idle
 *                                                    before each (glFinish, untimed); median
 *   bench-native <scene> --mode gpu   [--frames N]   that, plus the frame's GPU time (timer query,
 *                                                    GL_EXT_disjoint_timer_query); medians
 *   bench-native <scene> --mode firstuse            a fresh process: setup, the first and second
 *                                                    frames (each to GPU idle), then the steady median:
 *                                                    the first-use cost (program builds, uploads)
 *   bench-native <scene> --mode hitch [--frames N]   every frame to GPU idle: median, p99, max, frames
 *                                                    over 16.7 / 33.3 ms, and the frames the scene
 *                                                    tagged (bench_frame_tag: a new material state)
 *   bench-native - --mode inflate                    the packed program table's first source (the
 *                                                    whole table inflates) and a second, per backend
 *   --msaa N   a multisampled surface, as a browser canvas made with antialias: true
 *              (three.c then draws straight into it; the resolve is outside the frame)
 *
 * $BENCH_EGL_DEVICE picks the GPU whose DRM node path contains it. Output is
 * one `BENCH_RESULT {json}` line. */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#include "scenes.h"

/* compare/assets of the three.lua checkout ($THREE_LUA, default ../three.lua
 * next to this repo, found relative to the executable's build dir). */
bool bench_pointer(int *x, int *y) { (void)x; (void)y; return false; }

char *bench_read_asset(const char *name, size_t *len) {
  const char *root = getenv("THREE_LUA");
  char path[1024];
  /* three.c's own test assets, then three.lua's compare/assets */
  snprintf(path, sizeof path, "test/assets/%s", name);
  FILE *f = fopen(path, "rb");
  if (!f) {
    snprintf(path, sizeof path, "%s/compare/assets/%s", root ? root : "../three.lua", name);
    f = fopen(path, "rb");
  }
  if (!f) { fprintf(stderr, "bench: cannot open %s\n", path); return NULL; }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
  fclose(f);
  if (buf) buf[n] = 0;
  *len = (size_t)n;
  return buf;
}

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static int msaa_samples;
#include "gen/programs.h"
int t3_gen_tables(t3_gen_backend backend, const t3_gen_table *const **out);

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
/* the first program of a backend's base table (the last table searched) */
static void time_inflate(t3_gen_backend be, const char *key) {
  const t3_gen_table *const *t;
  int n = t3_gen_tables(be, &t);
  if (n < 1) return;
  const t3_gen_program *p = t[n - 1]->programs;
  char err[256];
  double a = now_ms();
  char *s1 = t3_gen_source(p, true, err, sizeof err);
  double b = now_ms();
  char *s2 = t3_gen_source(p + 1, true, err, sizeof err);
  double c = now_ms();
  printf(",\"%sFirstMs\":%.3f,\"%sNextMs\":%.4f,\"%sOk\":%s", key, b - a, key, c - b, key, s1 && s2 ? "true" : "false");
  free(s1); free(s2);
}
#ifdef T3_WGPU
/* --wgpu: render with three.c's WebGPU backend on Dawn (native-dawn) */
#include <webgpu/webgpu.h>
extern void *bench_wgpu_device, *bench_wgpu_queue;
static WGPUInstance wg_inst;
static void wg_adapter_cb(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUAdapter *)u1 = st == WGPURequestAdapterStatus_Success ? a : NULL; *(int *)u2 = 1;
}
static void wg_device_cb(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView m, void *u1, void *u2) {
  if (st != WGPURequestDeviceStatus_Success) fprintf(stderr, "requestDevice: %.*s\n", (int)m.length, m.data);
  *(WGPUDevice *)u1 = st == WGPURequestDeviceStatus_Success ? d : NULL; *(int *)u2 = 1;
}
static int wg_errors;
static char wg_adapter[256];
static void wg_error_cb(WGPUDevice const *d, WGPUErrorType t, WGPUStringView m, void *u1, void *u2) {
  (void)d; (void)u1; (void)u2;
  if (wg_errors++ < 8) fprintf(stderr, "webgpu error %d: %.*s\n", (int)t, (int)m.length, m.data);
}
static int wgpu_up(void) {
  wg_inst = wgpuCreateInstance(NULL);
  WGPURequestAdapterOptions ao = { .powerPreference = WGPUPowerPreference_HighPerformance };
  WGPUAdapter ad = NULL; int done = 0;
  WGPURequestAdapterCallbackInfo ac = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = wg_adapter_cb, .userdata1 = &ad, .userdata2 = &done };
  wgpuInstanceRequestAdapter(wg_inst, &ao, ac);
  while (!done) wgpuInstanceProcessEvents(wg_inst);
  if (!ad) { fprintf(stderr, "no WebGPU adapter\n"); return 0; }
  WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
  if (wgpuAdapterGetInfo(ad, &info) == WGPUStatus_Success)
    snprintf(wg_adapter, sizeof wg_adapter, "%.*s (%.*s)", (int)info.device.length, info.device.data,
             (int)info.description.length, info.description.data);
  /* r186 asks for every feature the adapter has; the generated programs
   * assume float32-filterable (a float render target read as a map) */
  WGPUFeatureName feats[] = { WGPUFeatureName_Float32Filterable };
  WGPUDeviceDescriptor dd = { .uncapturedErrorCallbackInfo = { .callback = wg_error_cb }, .requiredFeatureCount = 1, .requiredFeatures = feats };
  WGPUDevice dev = NULL; done = 0;
  WGPURequestDeviceCallbackInfo dc = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = wg_device_cb, .userdata1 = &dev, .userdata2 = &done };
  wgpuAdapterRequestDevice(ad, &dd, dc);
  while (!done) wgpuInstanceProcessEvents(wg_inst);
  if (!dev) return 0;
  bench_wgpu_device = dev;
  bench_wgpu_queue = wgpuDeviceGetQueue(dev);
  return 1;
}
#endif
static int use_wgpu;
#ifdef T3_WGPU
static void wg_done_cb(WGPUQueueWorkDoneStatus st, WGPUStringView m, void *u1, void *u2) { (void)st; (void)m; (void)u2; *(int *)u1 = 1; }
#endif
/* the frame's GPU work finished: glFinish, or on WebGPU the queue's
 * onSubmittedWorkDone (what three.lua's node-js-webgpu lane waits on) */
static void bench_finish(void) {
#ifdef T3_WGPU
  if (use_wgpu) {
    int done = 0;
    WGPUQueueWorkDoneCallbackInfo ci = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = wg_done_cb, .userdata1 = &done };
    wgpuQueueOnSubmittedWorkDone((WGPUQueue)bench_wgpu_queue, ci);
    while (!done) wgpuInstanceProcessEvents(wg_inst);
    return;
  }
#endif
  glFinish();
}
/* one frame: the scene's renders, then (WebGPU, deferred) one submit */
/* $BENCH_SLOW_US: spin that long each frame (bench/perf.mjs --selftest: a
 * slowdown the comparison must catch) */
static double slow_ms;
static void bench_frame(const bench_scene *sc) {
  if (slow_ms > 0) { double t = now_ms(); while (now_ms() - t < slow_ms) {} }
  sc->frame();
  if (use_wgpu) t3_renderer_wgpu_submit(bench_renderer());
}

static int egl_up(int w, int h) {
  PFNEGLQUERYDEVICESEXTPROC qd = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
  PFNEGLQUERYDEVICESTRINGEXTPROC qs = (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
  PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  EGLDisplay dpy = EGL_NO_DISPLAY;
  if (qd && gpd) {
    EGLDeviceEXT devs[16];
    EGLint n = 0;
    if (qd(16, devs, &n) && n > 0) {
      int pick = 0;
      const char *want = getenv("BENCH_EGL_DEVICE");
      for (int i = 0; want && *want && qs && i < n; i++) {
        const char *p = qs(devs[i], EGL_DRM_DEVICE_FILE_EXT);
        const char *r = qs(devs[i], 0x3377 /* EGL_DRM_RENDER_NODE_FILE_EXT */);
        if ((p && strstr(p, want)) || (r && strstr(r, want))) { pick = i; break; }
      }
      dpy = gpd(EGL_PLATFORM_DEVICE_EXT, devs[pick], NULL);
    }
  }
  if (dpy == EGL_NO_DISPLAY) dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(dpy, NULL, NULL)) { fprintf(stderr, "eglInitialize failed\n"); return 0; }
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_SAMPLE_BUFFERS, msaa_samples ? 1 : 0, EGL_SAMPLES, msaa_samples,
    EGL_NONE };
  EGLConfig cfg; EGLint nc = 0;
  if (!eglChooseConfig(dpy, ca, &cfg, 1, &nc) || nc < 1) { fprintf(stderr, "no EGL config\n"); return 0; }
  const EGLint sa[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE };
  EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, sa);
  const EGLint xa[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
  if (!surf || !ctx || !eglMakeCurrent(dpy, surf, surf, ctx)) { fprintf(stderr, "EGL context failed\n"); return 0; }
  return 1;
}

/* An RGB PNG, rows top-down, from a bottom-up RGBA readback. */
static void put32(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len) {
  unsigned char hdr[8];
  put32(hdr, len);
  memcpy(hdr + 4, type, 4);
  fwrite(hdr, 1, 8, f);
  if (len) fwrite(data, 1, len, f);
  uLong crc = crc32(0, (const Bytef *)type, 4);
  if (len) crc = crc32(crc, data, len);
  unsigned char c[4];
  put32(c, (uint32_t)crc);
  fwrite(c, 1, 4, f);
}
static int write_png(const char *path, int w, int h, const unsigned char *rgba) {
  size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
  unsigned char *raw = malloc(raw_len);
  for (int y = 0; y < h; y++) {
    unsigned char *row = raw + (size_t)y * (1 + (size_t)w * 3);
    const unsigned char *src = rgba + (size_t)(h - 1 - y) * w * 4;
    row[0] = 0;
    for (int x = 0; x < w; x++) memcpy(row + 1 + x * 3, src + x * 4, 3);
  }
  uLongf zlen = compressBound(raw_len);
  unsigned char *z = malloc(zlen);
  compress2(z, &zlen, raw, raw_len, 6);
  FILE *f = fopen(path, "wb");
  if (!f) { free(raw); free(z); return 0; }
  fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
  unsigned char ihdr[13];
  put32(ihdr, w); put32(ihdr + 4, h);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
  chunk(f, "IHDR", ihdr, 13);
  chunk(f, "IDAT", z, (uint32_t)zlen);
  chunk(f, "IEND", NULL, 0);
  fclose(f);
  free(raw);
  free(z);
  return 1;
}

static void json_str(FILE *f, const char *s) {
  fputc('"', f);
  for (; s && *s; s++) {
    if (*s == '"' || *s == '\\') { fputc('\\', f); fputc(*s, f); }
    else if ((unsigned char)*s < 0x20) fprintf(f, "\\u%04x", *s);
    else fputc(*s, f);
  }
  fputc('"', f);
}

int main(int argc, char **argv) {
  const char *name = NULL, *mode = "time", *png = NULL;
  double min_ms = 1500, warm_ms = 1000;
  int reps = 5, warm_frames = 120, frames = 100;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode = argv[++i];
    else if (!strcmp(argv[i], "--min-ms") && i + 1 < argc) min_ms = atof(argv[++i]);
    else if (!strcmp(argv[i], "--warm-ms") && i + 1 < argc) warm_ms = atof(argv[++i]);
    else if (!strcmp(argv[i], "--warm-frames") && i + 1 < argc) warm_frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--png") && i + 1 < argc) png = argv[++i];
    else if (!strcmp(argv[i], "--msaa") && i + 1 < argc) msaa_samples = atoi(argv[++i]);
#ifdef T3_WGPU
    else if (!strcmp(argv[i], "--wgpu")) use_wgpu = 1;
#endif
    else if (argv[i][0] != '-') name = argv[i];
  }
  if (getenv("BENCH_SLOW_US")) slow_ms = atof(getenv("BENCH_SLOW_US")) / 1000;
  if (!strcmp(mode, "inflate")) {
    printf("BENCH_RESULT {\"lane\":\"native\",\"side\":\"three.c\",\"scene\":\"inflate\"");
    time_inflate(T3_GEN_GL, "gl");
#ifdef T3_WGPU
    time_inflate(T3_GEN_WGPU, "wgpu");
#endif
    printf("}\n");
    return 0;
  }
  const bench_scene *sc = NULL;
  for (const bench_scene *s = bench_scenes; name && s->name; s++)
    if (!strncmp(s->name, name, strlen(name))) { sc = s; break; }
  if (!sc) {
    fprintf(stderr, "usage: bench-native <scene> [--mode time|check]\nscenes:");
    for (const bench_scene *s = bench_scenes; s->name; s++) fprintf(stderr, " %s", s->name);
    fprintf(stderr, "\n");
    return 2;
  }

  const int W = 1280, H = 720;
  if (!egl_up(W, H)) return 1;
#ifdef T3_WGPU
  if (use_wgpu && !wgpu_up()) return 1;
#endif
  double setup_t0 = now_ms();
  sc->setup();
  double setup_ms = now_ms() - setup_t0;

  printf("BENCH_RESULT {\"lane\":\"native\",\"side\":\"three.c\",\"scene\":");
  json_str(stdout, sc->name);
  printf(",\"renderer\":");
  json_str(stdout, (const char *)glGetString(GL_RENDERER));
  printf(",\"driver\":");
  json_str(stdout, (const char *)glGetString(GL_VERSION));

  if (!strcmp(mode, "rt")) {
    /* bench/rt/driver.mjs lane: RT_BEGIN, frames for --min-ms, RT_END */
    for (int i = 0; i < 120; i++) bench_frame(sc);
    bench_finish();
    printf("RT_BEGIN\n");
    fflush(stdout);
    struct timespec pause = { 0, 100000000 };
    nanosleep(&pause, NULL);
    double t0 = now_ms();
    long n = 0;
    while (now_ms() - t0 < min_ms) { for (int k = 0; k < 10; k++) bench_frame(sc); n += 10; }
    bench_finish();
    printf("RT_END %ld %.3f\n", n, now_ms() - t0);
    fflush(stdout);
    struct timespec hold = { 0, 500000000 }; /* the driver reads memory now */
    nanosleep(&hold, NULL);
    return 0;
  }
  printf(",\"backend\":\"%s\"", use_wgpu ? "wgpu" : "gl");
#ifdef T3_WGPU
  if (use_wgpu) { printf(",\"adapter\":"); json_str(stdout, wg_adapter); }
#endif
  if (!strcmp(mode, "firstuse")) {
    double t[4];
    t[0] = now_ms(); bench_frame(sc); bench_finish();
    t[1] = now_ms(); bench_frame(sc); bench_finish();
    t[2] = now_ms();
    int n = frames > 0 ? frames : 60;
    double *st = malloc(sizeof *st * (size_t)n);
    for (int i = 0; i < n; i++) { double a = now_ms(); bench_frame(sc); bench_finish(); st[i] = now_ms() - a; }
    qsort(st, (size_t)n, sizeof *st, cmp_d);
    (void)t[3];
    printf(",\"setupMs\":%.3f,\"firstFrameMs\":%.3f,\"secondFrameMs\":%.3f,\"steadyMs\":%.4f",
           setup_ms, t[1] - t[0], t[2] - t[1], st[n / 2]);
    free(st);
  } else if (!strcmp(mode, "hitch")) {
    int n = frames > 0 ? frames : 600;
    double *all = malloc(sizeof *all * (size_t)n), *tag = malloc(sizeof *tag * (size_t)n), *rest = malloc(sizeof *rest * (size_t)n);
    int nt = 0, nr = 0, over16 = 0, over33 = 0;
    bench_finish();
    for (int i = 0; i < n; i++) {
      bench_frame_tag = 0;
      double a = now_ms();
      bench_frame(sc);
      bench_finish();
      double d = now_ms() - a;
      all[i] = d;
      if (bench_frame_tag) tag[nt++] = d; else rest[nr++] = d;
      over16 += d > 16.667; over33 += d > 33.333;
    }
    qsort(all, (size_t)n, sizeof *all, cmp_d);
    qsort(tag, (size_t)nt, sizeof *tag, cmp_d);
    qsort(rest, (size_t)(nr ? nr : 1), sizeof *rest, cmp_d);
    double tsum = 0;
    for (int i = 0; i < nt; i++) tsum += tag[i];
    printf(",\"setupMs\":%.3f,\"frames\":%d,\"medianMs\":%.4f,\"p99Ms\":%.3f,\"maxMs\":%.3f,\"over16\":%d,\"over33\":%d",
           setup_ms, n, all[n / 2], all[n * 99 / 100], all[n - 1], over16, over33);
    printf(",\"taggedFrames\":%d", nt);
    if (nt) printf(",\"taggedMedianMs\":%.3f,\"taggedMaxMs\":%.3f,\"taggedTotalMs\":%.3f", tag[nt / 2], tag[nt - 1], tsum);
    if (nr) printf(",\"untaggedMedianMs\":%.4f,\"untaggedMaxMs\":%.3f", rest[nr / 2], rest[nr - 1]);
    free(all); free(tag); free(rest);
  } else if (!strcmp(mode, "gpu")) {
    typedef void (*get_u64_fn)(GLuint, GLenum, GLuint64 *);
    get_u64_fn get_u64 = (get_u64_fn)eglGetProcAddress("glGetQueryObjectui64vEXT");
    if (!get_u64) { fprintf(stderr, "no GL_EXT_disjoint_timer_query\n"); return 1; }
    for (int i = 0; i < 60; i++) bench_frame(sc);
    int n = frames > 0 ? frames : 200;
    double *c = malloc(sizeof *c * (size_t)n), *g = malloc(sizeof *g * (size_t)n);
    GLuint q;
    glGenQueries(1, &q);
    for (int i = 0; i < n; i++) {
      bench_finish();
      double a = now_ms();
      glBeginQuery(0x88BF /* GL_TIME_ELAPSED_EXT */, q);
      bench_frame(sc);
      glEndQuery(0x88BF);
      c[i] = now_ms() - a;
      bench_finish();
      GLuint64 ns = 0;
      get_u64(q, GL_QUERY_RESULT, &ns);
      g[i] = ns / 1e6;
    }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && c[j] < c[j - 1]; j--) { double x = c[j]; c[j] = c[j - 1]; c[j - 1] = x; }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && g[j] < g[j - 1]; j--) { double x = g[j]; g[j] = g[j - 1]; g[j - 1] = x; }
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    printf(",\"cpuMedianMs\":%.6f,\"gpuMedianMs\":%.6f,\"fb0Samples\":%d", c[n / 2], g[n / 2], samples);
    free(c); free(g);
  } else if (!strcmp(mode, "cpu")) {
    for (int i = 0; i < 60; i++) bench_frame(sc);
    int n = frames > 0 ? frames : 300;
    double *t = malloc(sizeof *t * (size_t)n);
    for (int i = 0; i < n; i++) {
      bench_finish();
      double a = now_ms();
      bench_frame(sc);
      t[i] = now_ms() - a;
    }
    bench_finish();
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && t[j] < t[j - 1]; j--) { double x = t[j]; t[j] = t[j - 1]; t[j - 1] = x; }
    printf(",\"cpuMedianMs\":%.6f,\"cpuP10Ms\":%.6f,\"cpuP90Ms\":%.6f", t[n / 2], t[n / 10], t[n * 9 / 10]);
    free(t);
  } else if (!strcmp(mode, "time")) {
    double t0 = now_ms();
    int n = 0;
    while (n < warm_frames || now_ms() - t0 < warm_ms) { bench_frame(sc); n++; }
    bench_finish();
    double per = (now_ms() - t0) / n;
    long fr = (long)(min_ms * 1.05 / per);
    if (fr < 10) fr = 10;
    printf(",\"warmFrames\":%d,\"warmPerFrame\":%.6f,\"runs\":[", n, per);
    int got = 0, guard = 0;
    while (got < reps && guard++ < reps * 4) {
      bench_finish();
      double a = now_ms();
      for (long i = 0; i < fr; i++) bench_frame(sc);
      double b = now_ms();
      bench_finish();
      double c = now_ms();
      if (c - a < min_ms * 0.95) { fr = (long)(fr * (min_ms * 1.05) / (c - a > 1e-3 ? c - a : 1e-3)); continue; }
      printf("%s{\"frames\":%ld,\"cpuMs\":%.6f,\"totalMs\":%.6f,\"wallMs\":%.3f}", got ? "," : "", fr,
             (b - a) / fr, (c - a) / fr, c - a);
      got++;
    }
    printf("]");
  } else {
    for (int i = 0; i < frames; i++) bench_frame(sc);
    bench_finish();
    if (png) {
      unsigned char *px = malloc((size_t)W * H * 4);
#ifdef T3_WGPU
      if (use_wgpu) {
        /* WebGPU rows run top first; write_png takes GL's bottom-first rows */
        unsigned char *top = malloc((size_t)W * H * 4);
        if (!t3_renderer_wgpu_read(bench_renderer(), top)) fprintf(stderr, "webgpu readback failed\n");
        for (int y = 0; y < H; y++) memcpy(px + (size_t)y * W * 4, top + (size_t)(H - 1 - y) * W * 4, (size_t)W * 4);
        free(top);
      } else
#endif
      {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
      }
      write_png(png, W, H, px);
      free(px);
    }
  }
  if (sc->probe) printf(",\"probe\":%.17g", sc->probe());
  const t3_render_info *info = t3_renderer_info(bench_renderer());
  printf(",\"calls\":%u,\"triangles\":%u,\"programs\":%u,\"skipped\":%u", info->calls, info->triangles, info->programs, info->skipped);
  const char *err = t3_renderer_last_error(bench_renderer());
  printf(",\"missingStates\":");
  json_str(stdout, t3_renderer_generated_missing(bench_renderer()));
  printf(",\"error\":");
  if (err) json_str(stdout, err); else printf("null");
  printf(",\"glError\":%u}\n", glGetError());
  return 0;
}
