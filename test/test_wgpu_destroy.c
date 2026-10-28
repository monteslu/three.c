/* A WebGPU renderer makes no GL call when it is destroyed.
 *
 * An embedder that imports both APIs (a dual GL / WebGPU wasm cart) must not
 * touch GL once it picked WebGPU: t3_renderer_destroy used to delete its GL
 * objects regardless, glDeleteTextures(0) among them, and the host trapped.
 * This test defines the GL delete calls itself (they interpose on the
 * library's), counts them, renders a shadowed, instanced scene on a WebGPU
 * device, destroys the renderer and expects none.
 *
 * T3_TEST_CONTROL=1 (must fail): the same with a GLES renderer (EGL
 * surfaceless), whose destroy does delete GL objects: the counter sees them.
 *
 *   cc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -DT3_WGPU -I include -I src -I <dawn>/include test/test_wgpu_destroy.c \
 *     build/native-wgpu/libthree.a -L <dawn>/lib -lwebgpu_dawn -Wl,-rpath,<dawn>/lib -lEGL -lGLESv2 -lz -lm
 *
 * Exits 0 on success, 77 when no WebGPU adapter is available. */
#include <stdio.h>
#include <stdlib.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <webgpu/webgpu.h>

#include "three.h"

static int gl_deletes;
void glDeleteTextures(GLsizei n, const GLuint *t) { (void)n; (void)t; gl_deletes++; }
void glDeleteFramebuffers(GLsizei n, const GLuint *t) { (void)n; (void)t; gl_deletes++; }
void glDeleteRenderbuffers(GLsizei n, const GLuint *t) { (void)n; (void)t; gl_deletes++; }
void glDeleteBuffers(GLsizei n, const GLuint *t) { (void)n; (void)t; gl_deletes++; }
void glDeleteVertexArrays(GLsizei n, const GLuint *t) { (void)n; (void)t; gl_deletes++; }

static void adapter_cb(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUAdapter *)u1 = st == WGPURequestAdapterStatus_Success ? a : NULL; *(int *)u2 = 1;
}
static void device_cb(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView m, void *u1, void *u2) {
  (void)m; *(WGPUDevice *)u1 = st == WGPURequestDeviceStatus_Success ? d : NULL; *(int *)u2 = 1;
}

static t3_renderer *wgpu_renderer(void) {
  WGPUInstance inst = wgpuCreateInstance(NULL);
  WGPUAdapter ad = NULL; int done = 0;
  WGPURequestAdapterOptions ao = { .powerPreference = WGPUPowerPreference_LowPower, .featureLevel = WGPUFeatureLevel_Compatibility };
  wgpuInstanceRequestAdapter(inst, &ao, (WGPURequestAdapterCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = adapter_cb, .userdata1 = &ad, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!ad) return NULL;
  WGPUDevice dev = NULL;
  WGPUDeviceDescriptor dd = { 0 };
  done = 0;
  wgpuAdapterRequestDevice(ad, &dd, (WGPURequestDeviceCallbackInfo){ .mode = WGPUCallbackMode_AllowProcessEvents, .callback = device_cb, .userdata1 = &dev, .userdata2 = &done });
  while (!done) wgpuInstanceProcessEvents(inst);
  if (!dev) return NULL;
  t3_renderer *r = t3_renderer_new(64, 64);
  t3_renderer_use_wgpu(r, dev, wgpuDeviceGetQueue(dev), WGPUTextureFormat_RGBA8Unorm);
  return r;
}

static t3_renderer *gles_renderer(void) {
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(d, NULL, NULL)) return NULL;
  EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
  EGLConfig cfg; EGLint n = 0;
  if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) return NULL;
  EGLint sa[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
  EGLSurface surf = eglCreatePbufferSurface(d, cfg, sa);
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint xa[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xa);
  if (!ctx || !eglMakeCurrent(d, surf, surf, ctx)) return NULL;
  return t3_renderer_new(64, 64);
}

int main(void) {
  bool control = getenv("T3_TEST_CONTROL") && atoi(getenv("T3_TEST_CONTROL"));
  t3_renderer *r = control ? gles_renderer() : wgpu_renderer();
  if (!r) { puts("test_wgpu_destroy: no device, skipped"); return 77; }
  t3_renderer_set_shadow_map(r, true, T3_PCF_SHADOW_MAP);
  t3_scene *s = t3_scene_new();
  t3_camera *cam = t3_perspective_camera_new(50, 1, 0.1f, 50);
  t3_object_set_position(cam, 0, 3, 6);
  t3_object_look_at(cam, 0, 0, 0);
  t3_light *sun = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(sun, 2, 5, 3);
  sun->base.cast_shadow = true;
  t3_object_add(&s->base, sun);
  t3_geometry *g = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_material *m = t3_mesh_standard_material_new(0x808080);
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, 3);
  for (int i = 0; i < 3; i++) {
    t3_mat4 mx;
    t3_mat4_identity(&mx);
    mx.e[12] = (float)(i - 1) * 1.5f;
    t3_instanced_mesh_set_matrix_at(im, i, &mx);
  }
  im->mesh.base.cast_shadow = im->mesh.base.receive_shadow = true;
  t3_object_add(&s->base, im);
  t3_mesh *floor = t3_mesh_new(t3_plane_geometry_new(8, 8, 1, 1), t3_mesh_standard_material_new(0xa0a0a0));
  t3_object_set_rotation(floor, -1.5707964f, 0, 0);
  t3_object_set_position(floor, 0, -0.5f, 0);
  floor->base.receive_shadow = true;
  t3_object_add(&s->base, floor);
  for (int f = 0; f < 2; f++) {
    t3_renderer_render(r, s, cam);
    if (!control) t3_renderer_wgpu_submit(r);
  }
  int before = gl_deletes;
  t3_renderer_destroy(r);
  int during = gl_deletes - before;
  printf("test_wgpu_destroy (%s): %d GL delete calls while rendering, %d in t3_renderer_destroy\n", control ? "gles control" : "webgpu", before, during);
  if (before || during) { fprintf(stderr, "FAIL: a %s renderer made GL delete calls\n", control ? "GLES" : "WebGPU"); return 1; }
  puts("test_wgpu_destroy: ok");
  return 0;
}
