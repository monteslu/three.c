/* three.lua's compare/scenes and three.c's feature scenes (test/scenes),
 * ported line for line to three.c. Each scene's setup() mirrors the .js
 * file's top level and frame() its globalThis.frame, so pixels at a frame
 * number compare directly against three.js. */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "scenes.h"

static t3_renderer *R;
static t3_scene *S;
static t3_camera *C;
static int frame_no;

void *bench_wgpu_device, *bench_wgpu_queue;   /* native_main.c --wgpu */
static t3_renderer *make_renderer(uint32_t clear) {
  t3_renderer *r = t3_renderer_new(1280, 720);
  if (getenv("BENCH_EXACT_OUTPUT")) t3_renderer_set_exact_output(r, true);
  if (bench_wgpu_device) t3_renderer_use_wgpu(r, bench_wgpu_device, bench_wgpu_queue, 0x16 /* WGPUTextureFormat_RGBA8Unorm */);
  if (bench_wgpu_device) t3_renderer_wgpu_defer(r, true);   /* one submit per frame (native_main submits after each) */
  t3_renderer_set_clear_color(r, clear, 1);
  return r;
}

/* Scenes 14-19 create a renderer of 320 x 240:
 * three.js renders into its own 320x240 offscreen canvas. Draw into a
 * 320x240 region (viewport + scissor, so the clear is 320x240 too) for the
 * same GPU work. */
static t3_renderer *make_offscreen_320(void) {
  t3_renderer *r = make_renderer(0x000000);
  t3_renderer_set_size(r, 320, 240);
  t3_renderer_set_scissor_test(r, true);
  return r;
}

static t3_mesh *add_mesh(t3_object *parent, t3_geometry *g, t3_material *m) {
  t3_mesh *mesh = t3_mesh_new(g, m);
  t3_object_add(parent, mesh);
  t3_release(mesh);
  return mesh;
}

static void add_light(t3_light *l) {
  t3_object_add(&S->base, l);
  t3_release(l);
}


/* ── s1-stress (test/scenes/s1-stress.js): CPU-bound ─────────────── */
static t3_object *st_groups[200];
static t3_raycaster *st_rc;
static int st_frame;
static double st_hits;
static uint32_t st_seed;
static double st_rand(void) {
  st_seed = (uint32_t)(((uint64_t)st_seed * 16807) % 2147483647);
  return st_seed / 2147483647.0;
}
static void stress_setup(void) {
  R = make_renderer(0x000000);
  S = t3_scene_new();
  S->has_background = true;
  S->background = t3_color_hex(0x202028);
  C = t3_perspective_camera_new(60, 1280.0f / 720, 0.1f, 500);
  st_seed = 20261005;
  st_frame = 0;
  st_hits = 0;
  add_light(t3_ambient_light_new(0x404040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 0.8f);
  t3_object_set_position(sun, 1, 2, 1);
  add_light(sun);
  t3_geometry *geo = t3_box_geometry_new(0.4f, 0.4f, 0.4f, 1, 1, 1);
  t3_material *mats[8];
  for (int i = 0; i < 8; i++) {
    mats[i] = t3_mesh_lambert_material_new(0xffffff);
    mats[i]->color = t3_color_hsl(i / 8.0f, 0.6f, 0.5f);
  }
  for (int g = 0; g < 200; g++) {
    t3_object *grp = t3_group_new();
    float x = (float)(st_rand() * 160 - 80), y = (float)(st_rand() * 20 - 10), z = (float)(st_rand() * 160 - 80);
    t3_object_set_position(grp, x, y, z);
    t3_object_add(&S->base, grp);
    t3_release(grp);
    st_groups[g] = grp;
    for (int i = 0; i < 100; i++) {
      t3_mesh *m = add_mesh(grp, geo, mats[(g + i) % 8]);
      float px = (float)(st_rand() * 8 - 4), py = (float)(st_rand() * 8 - 4), pz = (float)(st_rand() * 8 - 4);
      t3_object_set_position(m, px, py, pz);
      float rx = (float)(st_rand() * 6.28), ry = (float)(st_rand() * 6.28);
      t3_object_set_rotation(m, rx, ry, 0);
    }
  }
  t3_release(geo);
  for (int i = 0; i < 8; i++) t3_release(mats[i]);
  st_rc = t3_raycaster_new();
}
static void stress_frame(void) {
  st_frame++;
  double t = st_frame * 0.01;
  for (int g = 0; g < 200; g++) t3_object_set_rotation(st_groups[g], 0, (float)(t + g), 0);
  t3_object_set_position(C, (float)(cos(t * 0.5) * 60), 25, (float)(sin(t * 0.5) * 60));
  t3_object_look_at(C, 0, 0, 0);
  t3_object_update_matrix_world(S, false);
  t3_object_update_matrix_world(C, false);
  for (int i = 0; i < 8; i++) {
    t3_raycaster_set_from_camera(st_rc, (float)(sin(i * 1.7 + t) * 0.9), (float)(cos(i * 1.3 + t) * 0.9), C);
    st_hits += t3_raycaster_intersect_objects(st_rc, S->base.children, S->base.child_count, true, NULL);
  }
  t3_renderer_render(R, S, C);
}
static double stress_probe(void) { return st_hits; }

/* diagnostic: 06-heavy-instanced with the InstancedMesh drawn as three.js does */
static void inst_setup(void);
static void inst_plain_setup(void) { inst_setup(); t3_renderer_set_auto_instancing(R, false); }

/* ── 01-cubes ────────────────────────────────────────────────────── */
static void cubes_setup(void) {
  S = t3_scene_new();
  S->has_background = true;
  S->background = t3_color_hex(0x101018);
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 0, 8);
  t3_object_look_at(C, 0, 0, 0);
  R = make_renderer(0x101018);
  t3_geometry *box = t3_box_geometry_new(1.6f, 1.6f, 1.6f, 1, 1, 1);
  t3_material *red = t3_mesh_basic_material_new(0xff2020), *blue = t3_mesh_basic_material_new(0x2060ff),
              *green = t3_mesh_basic_material_new(0x20ff40);
  t3_geometry *bar = t3_box_geometry_new(2.4f, 0.8f, 0.8f, 1, 1, 1);
  t3_mesh *r = add_mesh(&S->base, box, red);
  t3_object_set_position(r, -2, 0, 0);
  t3_mesh *b = add_mesh(&S->base, box, blue);
  t3_object_set_position(b, 2, 0, 0);
  t3_mesh *g = add_mesh(&S->base, bar, green);
  t3_object_set_position(g, 0, 0, 2);
  t3_object_set_rotation(r, 0.4f, 0.6f, 0);
  t3_object_set_rotation(b, -0.3f, 0.8f, 0.2f);
  t3_release(box); t3_release(bar); t3_release(red); t3_release(blue); t3_release(green);
}
static void cubes_frame(void) { t3_renderer_render(R, S, C); }
static void cubes_noinst_setup(void) { cubes_setup(); t3_renderer_set_auto_instancing(R, false); }
/* N cubes of one geometry, each with its own colored Basic material: the
 * break-even probe for auto-instancing (cart vs cart, bench/ab.mjs). */
static void grid_setup(int n, bool inst) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 0, 12);
  t3_object_look_at(C, 0, 0, 0);
  R = make_renderer(0x101018);
  t3_renderer_set_auto_instancing(R, inst);
  t3_geometry *box = t3_box_geometry_new(0.8f, 0.8f, 0.8f, 1, 1, 1);
  for (int i = 0; i < n; i++) {
    t3_material *m = t3_mesh_basic_material_new(0x203040u + (uint32_t)i * 0x0a0805u);
    t3_mesh *mesh = add_mesh(&S->base, box, m);
    t3_object_set_position(mesh, (float)(i % 8) - 3.5f, (float)(i / 8) - 1.0f, 0);
    t3_release(m);
  }
  t3_release(box);
}
/* CEILING PROBE, not a port: 01-cubes' three meshes pre-merged by hand into
 * ONE geometry (world positions baked, vertex colors), so the frame is a
 * clear and a single draw. Measures what auto-merging could reach. */
static void merged_setup(void) {
  cubes_setup();
  t3_object_update_matrix_world(S, true);
  int nv = 0;
  for (int i = 0; i < 3; i++) nv += ((t3_mesh *)S->base.children[i])->geometry->attributes[T3_ATTR_POSITION]->count;
  t3_attribute *pos = t3_attribute_new(T3_FLOAT32, NULL, nv, 3), *col = t3_attribute_new(T3_FLOAT32, NULL, nv, 3);
  int ni = 0;
  for (int i = 0; i < 3; i++) ni += ((t3_mesh *)S->base.children[i])->geometry->index->count;
  t3_attribute *idx = t3_attribute_new(T3_UINT16, NULL, ni, 1);
  float *pp = pos->array, *cc = col->array;
  uint16_t *ii = idx->array;
  int vbase = 0, ib = 0;
  for (int i = 0; i < 3; i++) {
    t3_mesh *m = (t3_mesh *)S->base.children[i];
    t3_attribute *gp = m->geometry->attributes[T3_ATTR_POSITION];
    for (int v = 0; v < gp->count; v++) {
      t3_vec3 w = t3_vec3_apply_mat4(t3_v3(((float *)gp->array)[v * 3], ((float *)gp->array)[v * 3 + 1], ((float *)gp->array)[v * 3 + 2]), &m->base.matrix_world);
      pp[(vbase + v) * 3] = w.x; pp[(vbase + v) * 3 + 1] = w.y; pp[(vbase + v) * 3 + 2] = w.z;
      cc[(vbase + v) * 3] = m->materials[0]->color.r; cc[(vbase + v) * 3 + 1] = m->materials[0]->color.g; cc[(vbase + v) * 3 + 2] = m->materials[0]->color.b;
    }
    const uint16_t *gi = m->geometry->index->array;
    for (int k = 0; k < m->geometry->index->count; k++) ii[ib++] = (uint16_t)(gi[k] + vbase);
    vbase += gp->count;
  }
  while (S->base.child_count) t3_object_remove(&S->base, S->base.children[0]);
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, pos);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, col);
  t3_geometry_set_index(g, idx);
  t3_geometry_compute_bounding_sphere(g);
  t3_material *mat = t3_mesh_basic_material_new(0xffffff);
  mat->vertex_colors = true;
  add_mesh(&S->base, g, mat);
  t3_release(pos); t3_release(col); t3_release(idx); t3_release(g); t3_release(mat);
}

static void g1i(void) { grid_setup(1, true); }
static void g1p(void) { grid_setup(1, false); }
static void g2i(void) { grid_setup(2, true); }
static void g2p(void) { grid_setup(2, false); }
static void g4i(void) { grid_setup(4, true); }
static void g4p(void) { grid_setup(4, false); }
static void g16i(void) { grid_setup(16, true); }
static void g16p(void) { grid_setup(16, false); }
static void g64i(void) { grid_setup(64, true); }
static void g64p(void) { grid_setup(64, false); }

/* all three cubes share one material: batches need no per-instance color */
static void cubes_same_setup(void) {
  cubes_setup();
  t3_material *m = ((t3_mesh *)S->base.children[0])->materials[0];
  for (int i = 1; i < 3; i++) {
    t3_mesh *mesh = (t3_mesh *)S->base.children[i];
    t3_retain(m);
    t3_release(mesh->materials[0]);
    mesh->materials[0] = m;
  }
}
static void cubes_same_noinst_setup(void) { cubes_same_setup(); t3_renderer_set_auto_instancing(R, false); }

/* ── 02-lit ──────────────────────────────────────────────────────── */
static void lit_setup(void) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 0, 8);
  t3_object_look_at(C, 0, 0, 0);
  R = make_renderer(0x101018);
  t3_geometry *sg = t3_sphere_geometry_new(1.5f, 32, 24);
  t3_material *sm = t3_mesh_lambert_material_new(0xffffff);
  t3_mesh *sphere = add_mesh(&S->base, sg, sm);
  t3_object_set_position(sphere, -2.2f, 0, 0);
  t3_geometry *tg = t3_torus_geometry_new(1.2f, 0.45f, 16, 32, 6.283185307179586f);
  t3_material *tm = t3_mesh_lambert_material_new(0x66ccff);
  t3_mesh *torus = add_mesh(&S->base, tg, tm);
  t3_object_set_position(torus, 2.2f, 0, 0);
  t3_object_set_rotation(torus, 0.6f, 0.3f, 0);
  add_light(t3_ambient_light_new(0x202020, 1));
  t3_light *l = t3_directional_light_new(0xffffff, 1);
  t3_object_set_position(l, 10, 0, 2);
  add_light(l);
  t3_release(sg); t3_release(sm); t3_release(tg); t3_release(tm);
}

static void lit_noinst_setup(void) { lit_setup(); t3_renderer_set_auto_instancing(R, false); }

/* ── 05-heavy ────────────────────────────────────────────────────── */
#define HEAVY_COUNT 2000
static t3_mesh *heavy[HEAVY_COUNT];

static void heavy_common(void) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(60, 1280.0f / 720, 0.1f, 300);
  t3_object_set_position(C, 0, 0, 70);
  t3_object_look_at(C, 0, 0, 0);
  R = make_renderer(0x0a0a12);
}
static void heavy_lights(void) {
  add_light(t3_ambient_light_new(0x303040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 1);
  t3_object_set_position(sun, 6, 10, 8);
  add_light(sun);
}

static void heavy_setup(void) {
  heavy_common();
  t3_geometry *geo = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_material *mat = t3_mesh_lambert_material_new(0x66ccff);
  for (int i = 0; i < HEAVY_COUNT; i++) {
    t3_mesh *m = add_mesh(&S->base, geo, mat);
    float x = (float)(i % 50 - 25), y = (float)((i / 50) % 40 - 20);
    t3_object_set_position(m, x * 1.4f, y * 1.4f, 0);
    heavy[i] = m;
  }
  heavy_lights();
  t3_release(geo); t3_release(mat);
}

static void heavy_frame(void) {
  frame_no++;
  /* JS computes in doubles and stores float32 only at upload; do the same */
  double t = frame_no / 60.0;
  for (int i = 0; i < HEAVY_COUNT; i++) {
    t3_object *o = &heavy[i]->base;
    o->rotation.x = (float)(t + i * 0.01);
    o->rotation.y = (float)(t * 0.7 + i * 0.02);
    t3_object_rotation_changed(o);
  }
  t3_renderer_render(R, S, C);
}

/* ── 06-heavy-instanced ──────────────────────────────────────────── */
static t3_instanced_mesh *inst;

static void inst_setup(void) {
  heavy_common();
  t3_geometry *geo = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_material *mat = t3_mesh_lambert_material_new(0x66ccff);
  inst = t3_instanced_mesh_new(geo, mat, HEAVY_COUNT);
  /* three.js culls an InstancedMesh against its geometry's sphere; at
   * the origin, in view, so it draws either way */
  inst->mesh.base.frustum_culled = true;
  t3_object_add(&S->base, inst);
  t3_release(inst);
  heavy_lights();
  t3_release(geo); t3_release(mat);
}

/* Matrix4.compose(p, Quaternion.setFromEuler(XYZ), unit scale) in doubles,
 * stored as float: what the JS scene does (float math here moved cube edges
 * by a pixel across thousands of instances) */
static void compose_xyz_d(t3_mat4 *m, double px, double py, double pz, double ex, double ey, double ez) {
  double c1 = cos(ex / 2), c2 = cos(ey / 2), c3 = cos(ez / 2), s1 = sin(ex / 2), s2 = sin(ey / 2), s3 = sin(ez / 2);
  double x = s1 * c2 * c3 + c1 * s2 * s3, y = c1 * s2 * c3 - s1 * c2 * s3, z = c1 * c2 * s3 + s1 * s2 * c3, w = c1 * c2 * c3 - s1 * s2 * s3;
  double x2 = x + x, y2 = y + y, z2 = z + z, xx = x * x2, xy = x * y2, xz = x * z2, yy = y * y2, yz = y * z2, zz = z * z2;
  double wx = w * x2, wy = w * y2, wz = w * z2;
  double e[16] = { 1 - (yy + zz), xy + wz, xz - wy, 0, xy - wz, 1 - (xx + zz), yz + wx, 0, xz + wy, yz - wx, 1 - (xx + yy), 0, px, py, pz, 1 };
  for (int k = 0; k < 16; k++) m->e[k] = (float)e[k];
}

static void inst_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  t3_mat4 m;
  for (int i = 0; i < HEAVY_COUNT; i++) {
    double x = (double)(i % 50 - 25), y = (double)((i / 50) % 40 - 20);
    compose_xyz_d(&m, x * 1.4, y * 1.4, 0, t + i * 0.01, t * 0.7 + i * 0.02, 0);
    t3_instanced_mesh_set_matrix_at(inst, i, &m);
  }
  inst->instance_matrix->version++;
  t3_renderer_render(R, S, C);
}

/* ── 03 / 07 / 09 / 10: static InstancedMesh grids ─────────────── */
static void static_inst(int count, int cols, float spacing, float camz, float far, int wrap) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(60, 1280.0f / 720, 0.1f, far);
  t3_object_set_position(C, 0, 0, camz);
  t3_object_look_at(C, 0, 0, 0);
  R = make_renderer(0x0a0a12);
  t3_geometry *geo = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_material *mat = t3_mesh_lambert_material_new(0x66ccff);
  t3_instanced_mesh *im = t3_instanced_mesh_new(geo, mat, count);
  im->mesh.base.frustum_culled = true; /* culled by the geometry's sphere (at the origin, in view), as three.js */
  t3_mat4 m;
  for (int i = 0; i < count; i++) {
    float x = (float)(i % cols - cols / 2), y = (float)((i / cols) % wrap - wrap / 2);
    t3_euler e = { i * 0.01f, i * 0.02f, 0, T3_XYZ };
    t3_mat4_compose(&m, t3_v3(x * spacing, y * spacing, 0), t3_quat_from_euler(e), t3_v3(1, 1, 1));
    t3_instanced_mesh_set_matrix_at(im, i, &m);
  }
  t3_object_add(&S->base, im);
  t3_release(im);
  heavy_lights();
  t3_release(geo); t3_release(mat);
}
static void s03(void) { static_inst(300, 20, 2.2f, 46, 200, 15); }
static void s07(void) { static_inst(2000, 50, 1.4f, 70, 300, 40); }
static void s09(void) { static_inst(64, 50, 1.4f, 70, 300, 40); }
static void s10(void) { static_inst(256, 50, 1.4f, 70, 300, 40); }

/* ── 04-geometry ─────────────────────────────────────────────────── */
static void geo_place(t3_geometry *g, uint32_t color, float x, float y) {
  t3_material *m = t3_mesh_basic_material_new(color);
  t3_mesh *mesh = add_mesh(&S->base, g, m);
  t3_object_set_position(mesh, x, y, 0);
  t3_object_set_rotation(mesh, 0.5f, 0.7f, 0.1f);
  t3_release(m);
  t3_release(g);
}
static void s04(void) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 0, 14);
  t3_object_look_at(C, 0, 0, 0);
  R = make_renderer(0x101018);
  geo_place(t3_box_geometry_new(1.6f, 1.6f, 1.6f, 1, 1, 1), 0xff4040, -5.0f, 2.0f);
  geo_place(t3_sphere_geometry_new(1.0f, 16, 12), 0x40ff40, -2.5f, 2.0f);
  geo_place(t3_cylinder_geometry_new(0.8f, 0.8f, 1.8f, 20, 1, false), 0x4040ff, 0.0f, 2.0f);
  geo_place(t3_cone_geometry_new(1.0f, 1.8f, 20, 1, false), 0xffff40, 2.5f, 2.0f);
  geo_place(t3_torus_geometry_new(0.8f, 0.3f, 12, 24, 6.283185307179586f), 0xff40ff, 5.0f, 2.0f);
  geo_place(t3_plane_geometry_new(1.8f, 1.8f, 1, 1), 0x40ffff, -3.5f, -2.0f);
  geo_place(t3_circle_geometry_new(1.0f, 24), 0xffa040, -1.0f, -2.0f);
  geo_place(t3_ring_geometry_new(0.5f, 1.0f, 24, 1), 0xa040ff, 1.5f, -2.0f);
  geo_place(t3_cone_geometry_new(1.0f, 1.8f, 5, 1, false), 0x80ff80, 4.0f, -2.0f);
}

/* ── 14-cpu-math ─────────────────────────────────────────────────── */
static double cm_sink;
static void cm_setup(void) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(60, 320.0f / 240, 0.1f, 100);
  R = make_offscreen_320();
}
static void cm_frame(void) {
  t3_vec3 va, vb, vc;
  t3_quat q1, q2;
  t3_mat4 m1, m2;
  for (int i = 1; i <= 2000; i++) {
    float t = i * 0.001f;
    va = t3_v3(t, t * 2, t * 3);
    vb = t3_v3(t * 0.5f, -t, t * 0.25f);
    vc = t3_vec3_scale(t3_vec3_add(va, vb), 0.5f);
    vc = t3_vec3_normalize(vc);
    cm_sink += t3_vec3_dot(vc, va) + t3_vec3_length_sq(vc);
    vc = t3_vec3_cross(va, vb);
    cm_sink += t3_vec3_length(vc) + t3_vec3_distance_sq(va, vb);
    t3_euler e1 = { t, t * 0.7f, t * 1.3f, T3_XYZ };
    q1 = t3_quat_from_euler(e1);
    va = t3_vec3_normalize(va);
    q2 = t3_quat_from_axis_angle(va, t);
    q1 = t3_quat_slerp(q1, q2, 0.5f);
    cm_sink += q1.w;
    t3_mat4_compose(&m1, va, q1, vb);
    t3_mat4_invert(&m2, &m1);
    t3_mat4_multiply(&m1, &m1, &m2);
    cm_sink += m1.e[0];
    vc = t3_vec3_apply_mat4(vc, &m1);
    vc = t3_vec3_apply_quat(vc, q1);
    cm_sink += vc.x;
  }
  t3_renderer_render(R, S, C);
}
static double cm_probe(void) { return cm_sink; }

/* ── 16-curves ───────────────────────────────────────────────────── */
static t3_curve *cv_spline, *cv_bez, *cv_ellipse;
static double cv_sink;
static void cv_setup(void) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(60, 320.0f / 240, 0.1f, 100);
  R = make_offscreen_320();
  t3_vec3 pts[5] = { { -10, 0, 0 }, { -5, 5, 2 }, { 0, 0, -3 }, { 5, -5, 1 }, { 10, 0, 0 } };
  cv_spline = t3_catmull_rom_curve3_new(pts, 5, false, T3_CENTRIPETAL, 0.5f);
  cv_bez = t3_cubic_bezier_curve3_new(t3_v3(0, 0, 0), t3_v3(0, 10, 0), t3_v3(10, 10, 0), t3_v3(10, 0, 0));
  cv_ellipse = t3_ellipse_curve_new(0, 0, 5, 3, 0, 6.283185307179586f, false, 0);
}
static void cv_frame(void) {
  for (int i = 0; i < 400; i++) {
    float t = i / 400.0f;
    cv_sink += t3_curve_get_point(cv_spline, t).x;
    cv_sink += t3_curve_get_point(cv_bez, t).y;
    cv_sink += t3_curve_get_point(cv_ellipse, t).x;
  }
  t3_curve_update_arc_lengths(cv_spline);
  for (int i = 0; i < 64; i++) cv_sink += t3_curve_get_point_at(cv_spline, i / 64.0f).z;
  cv_sink += t3_curve_get_length(cv_spline);
  t3_renderer_render(R, S, C);
}
static double cv_probe(void) { return cv_sink; }

/* ── 17-animation ────────────────────────────────────────────────── */
static t3_animation_mixer *an_mixer;
static char an_names[60][8];
static void an_setup(void) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(60, 320.0f / 240, 0.1f, 100);
  R = make_offscreen_320();
  t3_object *root = t3_object_new();
  t3_keyframe_track *tracks[180];
  int nt = 0;
  for (int i = 1; i <= 60; i++) {
    t3_object *child = t3_object_new();
    snprintf(an_names[i - 1], sizeof an_names[0], "obj%d", i);
    child->name = an_names[i - 1];
    t3_object_add(root, child);
    t3_release(child);
    char nm[32];
    const float t3k[3] = { 0, 1, 2 }, t2k[2] = { 0, 2 };
    const float pos[9] = { 0, 0, 0, (float)i, i * 0.5f, (float)-i, 0, 0, 0 };
    const float quat[12] = { 0, 0, 0, 1, 0, 0.7071067811865476f, 0, 0.7071067811865476f, 0, 0, 0, 1 };
    const float ro[2] = { 0, 10 };
    snprintf(nm, sizeof nm, "obj%d.position", i);
    tracks[nt++] = t3_keyframe_track_new(T3_TRACK_VECTOR, nm, t3k, 3, pos);
    snprintf(nm, sizeof nm, "obj%d.quaternion", i);
    tracks[nt++] = t3_keyframe_track_new(T3_TRACK_QUATERNION, nm, t3k, 3, quat);
    snprintf(nm, sizeof nm, "obj%d.renderOrder", i);
    tracks[nt++] = t3_keyframe_track_new(T3_TRACK_NUMBER, nm, t2k, 2, ro);
  }
  t3_animation_clip *clip = t3_animation_clip_new("bench", -1, tracks, nt);
  an_mixer = t3_animation_mixer_new(root);
  t3_animation_action_play(t3_animation_mixer_clip_action(an_mixer, clip));
  t3_release(clip);
  t3_release(root);
}
static void an_frame(void) {
  t3_animation_mixer_update(an_mixer, 1.0f / 60);
  t3_renderer_render(R, S, C);
}
static double an_probe(void) {
  /* sum of every animated value, to compare with three.js at the same frame */
  double s = 0;
  t3_object *root = an_mixer->root;
  for (int i = 0; i < root->child_count; i++) {
    t3_object *o = root->children[i];
    s += o->position.x + o->position.y + o->position.z + o->quaternion.x + o->quaternion.y + o->quaternion.z +
         o->quaternion.w;
  }
  return s;
}

/* ── 15-raycast ──────────────────────────────────────────────────── */
static t3_raycaster *rc_rc;
static double rc_hits;
static int rc_frame_no;
static uint32_t rc_seed;
static double rc_rand(void) {
  rc_seed = (uint32_t)(((uint64_t)rc_seed * 16807) % 2147483647);
  return rc_seed / 2147483647.0;
}
static void rc_setup(void) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(60, 320.0f / 240, 0.1f, 200);
  C->base.position.z = 40;
  t3_object_update_matrix_world(C, false);
  R = make_offscreen_320();
  rc_seed = 20260914;
  t3_geometry *geo = t3_icosahedron_geometry_new(1, 1);
  t3_material *mat = t3_mesh_basic_material_new(0xffffff);
  for (int i = 0; i < 200; i++) {
    t3_mesh *m = add_mesh(&S->base, geo, mat);
    float x = (float)(rc_rand() * 30 - 15), y = (float)(rc_rand() * 30 - 15), z = (float)(rc_rand() * 30 - 15);
    t3_object_set_position(m, x, y, z);
  }
  t3_object_update_matrix_world(S, false);
  rc_rc = t3_raycaster_new();
  t3_release(geo); t3_release(mat);
}
static void rc_frame(void) {
  rc_frame_no++;
  for (int i = 1; i <= 40; i++) {
    double a = (i + rc_frame_no) * 0.137;
    t3_raycaster_set_from_camera(rc_rc, (float)(sin(a) * 0.9), (float)(cos(a * 1.31) * 0.9), C);
    rc_hits += t3_raycaster_intersect_objects(rc_rc, S->base.children, S->base.child_count, false, NULL);
  }
  t3_renderer_render(R, S, C);
}
static double rc_probe(void) { return rc_hits; }

/* ── 18-geomgen ──────────────────────────────────────────────────── */
static double gg_sink;
static void gg_setup(void) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(60, 320.0f / 240, 0.1f, 100);
  R = make_offscreen_320();
}
static void gg_frame(void) {
  t3_geometry *g;
  g = t3_sphere_geometry_new(1, 24, 16); gg_sink += g->attributes[T3_ATTR_POSITION]->count; t3_release(g);
  g = t3_torus_geometry_new(1, 0.4f, 16, 32, 6.283185307179586f); gg_sink += g->attributes[T3_ATTR_POSITION]->count; t3_release(g);
  g = t3_cylinder_geometry_new(1, 1, 2, 24, 1, false); gg_sink += g->attributes[T3_ATTR_POSITION]->count; t3_release(g);
  g = t3_icosahedron_geometry_new(1, 2); gg_sink += g->attributes[T3_ATTR_POSITION]->count; t3_release(g);
  g = t3_torus_knot_geometry_new(1, 0.3f, 48, 8, 2, 3); gg_sink += g->attributes[T3_ATTR_POSITION]->count; t3_release(g);
  g = t3_box_geometry_new(1, 1, 1, 4, 4, 4); gg_sink += g->attributes[T3_ATTR_POSITION]->count;
  t3_geometry_compute_vertex_normals(g);
  t3_geometry_compute_bounding_box(g);
  t3_geometry_compute_bounding_sphere(g);
  gg_sink += g->bounding_sphere.radius;
  t3_release(g);
  t3_geometry *s = t3_sphere_geometry_new(1, 12, 8);
  t3_geometry *flat = t3_geometry_to_non_indexed(s);
  gg_sink += flat->attributes[T3_ATTR_POSITION]->count;
  t3_release(flat); t3_release(s);
  t3_renderer_render(R, S, C);
}
static double gg_probe(void) { return gg_sink; }

/* ── 12-suzanne (INSTANCED method) ──────────────────────────────── */
static uint32_t sz_seed;
static double sz_rand(void) {
  sz_seed = (uint32_t)(((uint64_t)sz_seed * 16807) % 2147483647);
  return sz_seed / 2147483647.0;
}
static void sz_setup(void) {
  S = t3_scene_new();
  S->has_background = true;
  S->background = t3_color_hex(0xffffff);
  C = t3_perspective_camera_new(70, 1280.0f / 720, 1, 100);
  C->base.position.z = 30;
  R = make_renderer(0xffffff);
  sz_seed = 20260914;
  size_t len = 0;
  char *json = bench_read_asset("suzanne_buffergeometry.json", &len);
  t3_geometry *g = json ? t3_buffer_geometry_loader_parse(json, len) : NULL;
  free(json);
  if (!g) return;
  t3_geometry_compute_vertex_normals(g);
  t3_material *mat = t3_mesh_normal_material_new();
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, mat, 1000);
  im->mesh.base.frustum_culled = true; /* as three.js */
  t3_mat4 m;
  for (int i = 0; i < 1000; i++) {
    t3_vec3 p;
    p.x = (float)(sz_rand() * 40 - 20);
    p.y = (float)(sz_rand() * 40 - 20);
    p.z = (float)(sz_rand() * 40 - 20);
    double th1 = 2 * 3.141592653589793 * sz_rand(), th2 = 2 * 3.141592653589793 * sz_rand(), x0 = sz_rand();
    double r1 = sqrt(1 - x0), r2 = sqrt(x0);
    t3_quat q = { (float)(r1 * sin(th1)), (float)(r1 * cos(th1)), (float)(r2 * sin(th2)), (float)(r2 * cos(th2)) };
    float sc = (float)(sz_rand() * 1);
    t3_mat4_compose(&m, p, q, t3_v3(sc, sc, sc));
    t3_instanced_mesh_set_matrix_at(im, i, &m);
  }
  t3_object_add(&S->base, im);
  t3_release(im); t3_release(mat); t3_release(g);
}

/* ════ feature scenes: test/scenes/NAME.js, rendered by tools/ref-browser.mjs ════ */

/* ── t1-texture ──────────────────────────────────────────────────── */
static t3_texture *checker(int n, const uint8_t a[3], const uint8_t b[3]) {
  uint8_t *d = malloc((size_t)n * n * 4);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) {
      const uint8_t *c = (((x >> 2) + (y >> 2)) & 1) ? a : b;
      uint8_t *p = d + (y * n + x) * 4;
      p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
    }
  t3_texture *t = t3_data_texture_new(n, n, d);
  free(d);
  return t;
}
static t3_mesh *t1_meshes[4];
static void t1_setup(void) {
  R = make_renderer(0x202028);
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 1.5f, 9);
  t3_object_look_at(C, 0, 0, 0);
  const uint8_t r1[3] = { 230, 60, 40 }, w1[3] = { 250, 240, 220 }, b2[3] = { 40, 90, 220 }, w2[3] = { 240, 240, 240 };
  const uint8_t g3[3] = { 30, 160, 80 }, y3[3] = { 250, 220, 90 };
  t3_texture *t1 = checker(32, r1, w1), *t2 = checker(16, b2, w2), *t3 = checker(64, g3, y3);
  t2->wrap_s = t2->wrap_t = T3_REPEAT;
  t2->repeat.x = 3; t2->repeat.y = 2;
  t2->offset.x = 0.25f; t2->offset.y = 0.1f;
  t2->rotation = 0.3f;
  t2->mag_filter = T3_LINEAR;
  t3->color_space = T3_SRGB_COLOR_SPACE;
  t3->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t3->mag_filter = T3_LINEAR;
  t3->generate_mipmaps = true;
  t3_material *mats[4] = { t3_mesh_basic_material_new(0xffffff), t3_mesh_lambert_material_new(0xffffff),
                           t3_mesh_phong_material_new(0xffffff), t3_mesh_standard_material_new(0xffffff) };
  t3_material_set_map(mats[0], t1);
  t3_material_set_map(mats[1], t2);
  t3_material_set_map(mats[2], t3);
  mats[2]->shininess = 40;
  t3_material_set_map(mats[3], t1);
  mats[3]->roughness = 0.6f; mats[3]->metalness = 0.1f;
  t3_geometry *geos[4] = { t3_box_geometry_new(1.6f, 1.6f, 1.6f, 1, 1, 1), t3_sphere_geometry_new(1, 32, 16),
                           t3_torus_geometry_new(0.8f, 0.3f, 16, 48, 6.283185307179586f), t3_plane_geometry_new(2, 2, 1, 1) };
  for (int i = 0; i < 4; i++) {
    t1_meshes[i] = add_mesh(&S->base, geos[i], mats[i]);
    t3_object_set_position(t1_meshes[i], -4.5f + i * 3, 0, 0);
    t3_release(geos[i]); t3_release(mats[i]);
  }
  add_light(t3_ambient_light_new(0x404040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 0.9f);
  t3_object_set_position(sun, 3, 5, 6);
  add_light(sun);
  t3_release(t1); t3_release(t2); t3_release(t3);
}
static void t1_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 4; i++) t3_object_set_rotation(t1_meshes[i], (float)(t * 0.5 + i), (float)(t * 0.8 + i * 0.5), 0);
  t3_renderer_render(R, S, C);
}

/* ── t2-fog ──────────────────────────────────────────────────────── */
static t3_scene *t2_a, *t2_b;
static void t2_fill(t3_scene *sc, t3_geometry *geo, t3_material **mats) {
  for (int i = 0; i < 40; i++) {
    t3_mesh *m = add_mesh(&sc->base, geo, mats[i % 4]);
    t3_object_set_position(m, (float)((i % 5) * 2 - 4), 0, (float)(-(i / 5) * 4));
    t3_object_set_rotation(m, 0, i * 0.3f, 0);
  }
  t3_light *a = t3_ambient_light_new(0x404040, 1);
  t3_object_add(&sc->base, a); t3_release(a);
  t3_light *d = t3_directional_light_new(0xffffff, 0.8f);
  t3_object_set_position(d, 2, 4, 3);
  t3_object_add(&sc->base, d); t3_release(d);
}
static void t2_setup(void) {
  R = make_renderer(0x8899aa);
  t3_renderer_set_auto_clear(R, false);
  t2_a = t3_scene_new(); t2_b = t3_scene_new();
  t2_a->fog = t3_fog_linear(0x8899aa, 5, 30);
  t2_b->fog = t3_fog_exp2(0x8899aa, 0.06f);
  C = t3_perspective_camera_new(60, 640.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 2, 6);
  t3_object_look_at(C, 0, 0, -10);
  t3_geometry *geo = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_material *mats[4] = { t3_mesh_basic_material_new(0xff8040), t3_mesh_lambert_material_new(0x40c0ff),
                           t3_mesh_phong_material_new(0x80ff60), t3_mesh_standard_material_new(0xffffff) };
  mats[3]->roughness = 0.5f;
  t2_fill(t2_a, geo, mats);
  t2_fill(t2_b, geo, mats);
  t3_release(geo);
  for (int i = 0; i < 4; i++) t3_release(mats[i]);
  S = t2_a;
}
static void t2_frame(void) {
  t3_renderer_set_scissor_test(R, false);
  t3_renderer_clear(R, true, true, true);
  t3_renderer_set_scissor_test(R, true);
  t3_renderer_set_viewport(R, 0, 0, 640, 720); t3_renderer_set_scissor(R, 0, 0, 640, 720);
  t3_renderer_render(R, t2_a, C);
  t3_renderer_set_viewport(R, 640, 0, 640, 720); t3_renderer_set_scissor(R, 640, 0, 640, 720);
  t3_renderer_render(R, t2_b, C);
}

/* ── t3-shadow-dir ───────────────────────────────────────────────── */
static t3_mesh *t3_casters[9];
static void t3s_setup(void) {
  R = make_renderer(0x202830);
  t3_renderer_set_shadow_map(R, true, T3_PCF_SHADOW_MAP);
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 7, 12);
  t3_object_look_at(C, 0, 0, 0);
  t3_geometry *pg = t3_plane_geometry_new(20, 20, 1, 1);
  t3_material *gm = t3_mesh_standard_material_new(0x8090a0);
  gm->roughness = 0.9f;
  t3_mesh *ground = add_mesh(&S->base, pg, gm);
  t3_object_set_rotation(ground, -3.141592653589793f / 2, 0, 0);
  ground->base.receive_shadow = true;
  t3_geometry *shapes[3] = { t3_box_geometry_new(1.2f, 1.2f, 1.2f, 1, 1, 1), t3_sphere_geometry_new(0.8f, 32, 16),
                             t3_torus_knot_geometry_new(0.6f, 0.2f, 96, 12, 2, 3) };
  t3_material *cm[3] = { t3_mesh_standard_material_new(0xe05040), t3_mesh_lambert_material_new(0x40a0e0),
                         t3_mesh_phong_material_new(0xe0c040) };
  cm[0]->roughness = 0.5f;
  cm[2]->shininess = 60;
  for (int i = 0; i < 9; i++) {
    t3_mesh *m = add_mesh(&S->base, shapes[i % 3], cm[(i + i / 3) % 3]);
    t3_object_set_position(m, (float)((i % 3) * 3 - 3), 1.2f + (i % 2) * 0.8f, (float)((i / 3) * 3 - 3));
    m->base.cast_shadow = m->base.receive_shadow = true;
    t3_casters[i] = m;
  }
  add_light(t3_ambient_light_new(0x303040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 1);
  t3_object_set_position(sun, 5, 10, 4);
  sun->base.cast_shadow = true;
  sun->shadow->map_width = sun->shadow->map_height = 1024;
  t3_camera *sc = sun->shadow->camera;
  sc->left = -8; sc->right = 8; sc->top = 8; sc->bottom = -8; sc->near = 1; sc->far = 30;
  t3_camera_update_projection_matrix(sc);
  sun->shadow->bias = -0.0005f;
  add_light(sun);
  t3_release(pg); t3_release(gm);
  for (int i = 0; i < 3; i++) { t3_release(shapes[i]); t3_release(cm[i]); }
}
static void t3s_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 9; i++) t3_object_set_rotation(t3_casters[i], (float)(t * 0.7 + i), (float)(t * 0.5 + i * 0.3), 0);
  t3_renderer_render(R, S, C);
}

/* ── t4-shadow-spot-point ─────────────────────────────────────────── */
static t3_mesh *t4_casters[6];
static t3_light *t4_pt;
static void t4_setup(void) {
  R = make_renderer(0x101418);
  t3_renderer_set_shadow_map(R, true, T3_PCF_SOFT_SHADOW_MAP);
  S = t3_scene_new();
  C = t3_perspective_camera_new(55, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 8, 13);
  t3_object_look_at(C, 0, 0, 0);
  t3_material *room = t3_mesh_phong_material_new(0xa0a0a0);
  room->shininess = 10;
  t3_geometry *fg = t3_plane_geometry_new(24, 24, 1, 1), *wg = t3_plane_geometry_new(24, 10, 1, 1);
  t3_mesh *floor_ = add_mesh(&S->base, fg, room);
  t3_object_set_rotation(floor_, -3.141592653589793f / 2, 0, 0);
  floor_->base.receive_shadow = true;
  t3_mesh *wall = add_mesh(&S->base, wg, room);
  t3_object_set_position(wall, 0, 5, -6);
  wall->base.receive_shadow = true;
  t3_geometry *geo[3] = { t3_box_geometry_new(1, 2, 1, 1, 1, 1), t3_cylinder_geometry_new(0.6f, 0.6f, 1.6f, 24, 1, false),
                          t3_sphere_geometry_new(0.7f, 24, 12) };
  t3_material *mat[3] = { t3_mesh_standard_material_new(0xd06040), t3_mesh_lambert_material_new(0x50a0d0),
                          t3_mesh_phong_material_new(0xd0d050) };
  for (int i = 0; i < 6; i++) {
    t3_mesh *m = add_mesh(&S->base, geo[i % 3], mat[i % 3]);
    t3_object_set_position(m, i * 2.2f - 5.5f, 1, (float)((i % 2) * 2 - 1));
    m->base.cast_shadow = m->base.receive_shadow = true;
    t4_casters[i] = m;
  }
  t3_light *spot = t3_spot_light_new(0xffe0c0, 1.2f, 40, 3.141592653589793f / 6, 0.3f, 1);
  t3_object_set_position(spot, -6, 9, 6);
  t3_object_set_position(spot->target, -2, 0, 0);
  t3_object_add(&S->base, spot->target);
  spot->base.cast_shadow = true;
  spot->shadow->map_width = spot->shadow->map_height = 1024;
  spot->shadow->bias = -0.0005f;
  add_light(spot);
  t4_pt = t3_point_light_new(0x80c0ff, 1.0f, 30, 1);
  t3_object_set_position(t4_pt, 3, 4, 2);
  t4_pt->base.cast_shadow = true;
  t4_pt->shadow->bias = -0.001f;
  t3_object_add(&S->base, t4_pt);
  t3_release(t4_pt);
  add_light(t3_ambient_light_new(0x202020, 1));
  t3_light *fill = t3_directional_light_new(0x404050, 0.4f);
  t3_object_set_position(fill, 0, 5, 10);
  add_light(fill);
  t3_release(room); t3_release(fg); t3_release(wg);
  for (int i = 0; i < 3; i++) { t3_release(geo[i]); t3_release(mat[i]); }
}
static void t4_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 6; i++) t3_object_set_rotation(t4_casters[i], 0, (float)(t + i), 0);
  t4_pt->base.position.x = (float)(3 + sin(t) * 2);
  t3_renderer_render(R, S, C);
}

/* ── g1 / g2 / g3: glTF through t3_gltf_parse ─────────────────────── */
static t3_gltf *gl_model;
static t3_animation_mixer *gl_mixer;
static t3_gltf *load_glb(const char *name) {
  size_t len = 0;
  char *bytes = bench_read_asset(name, &len);
  t3_gltf *g = bytes ? t3_gltf_parse(bytes, len, NULL, NULL) : NULL;
  free(bytes);
  return g;
}
static void g1_setup(void) {
  R = make_renderer(0x303438);
  S = t3_scene_new();
  C = t3_perspective_camera_new(45, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 1.2f, 3);
  t3_object_look_at(C, 0, 0, 0);
  add_light(t3_ambient_light_new(0x606060, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 1);
  t3_object_set_position(sun, 2, 3, 4);
  add_light(sun);
  gl_model = load_glb("BoxTextured.glb");
  if (gl_model) t3_object_add(&S->base, gl_model->scene);
}
static void g1_frame(void) {
  frame_no++;
  if (gl_model) t3_object_set_rotation(gl_model->scene, 0, (float)(frame_no / 60.0), 0);
  t3_renderer_render(R, S, C);
}
static void gl_animated(const char *file, uint32_t clear) {
  R = make_renderer(clear);
  S = t3_scene_new();
  gl_model = load_glb(file);
  if (!gl_model) return;
  t3_object_add(&S->base, gl_model->scene);
  gl_mixer = t3_animation_mixer_new(gl_model->scene);
  t3_animation_action_play(t3_animation_mixer_clip_action(gl_mixer, gl_model->animations[0]));
}
static void g2_setup(void) {
  gl_animated("CesiumMan.glb", 0x2a3036);
  C = t3_perspective_camera_new(40, 1280.0f / 720, 0.05f, 50);
  t3_object_set_position(C, 1.6f, 1.0f, 2.4f);
  t3_object_look_at(C, 0, 0.8f, 0);
  add_light((t3_light *)t3_hemisphere_light_new(0xddeeff, 0x302010, 0.8f));
  t3_light *sun = t3_directional_light_new(0xffffff, 0.8f);
  t3_object_set_position(sun, 2, 4, 3);
  add_light(sun);
}
static void g3_setup(void) {
  gl_animated("AnimatedMorphCube.glb", 0x283038);
  C = t3_perspective_camera_new(40, 1280.0f / 720, 0.01f, 50);
  t3_object_set_position(C, 2.5f, 2, 3.5f);
  t3_object_look_at(C, 0, 0, 0);
  add_light(t3_ambient_light_new(0x505050, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 0.9f);
  t3_object_set_position(sun, 3, 5, 2);
  add_light(sun);
}
static void gl_anim_frame(void) {
  if (gl_mixer) t3_animation_mixer_update(gl_mixer, 1.0f / 60);
  t3_renderer_render(R, S, C);
}

static void g4_setup(void) {
  R = make_renderer(0x1c2024);
  S = t3_scene_new();
  C = t3_perspective_camera_new(35, 1280.0f / 720, 0.01f, 10);
  t3_object_set_position(C, 0, 0.05f, 0.6f);
  t3_object_look_at(C, 0, 0, 0);
  add_light((t3_light *)t3_hemisphere_light_new(0xffffff, 0x404040, 0.7f));
  t3_light *sun = t3_directional_light_new(0xffffff, 1.2f);
  t3_object_set_position(sun, 1, 2, 2);
  add_light(sun);
  t3_light *rim = t3_point_light_new(0x80a0ff, 1.0f, 5, 1);
  t3_object_set_position(rim, -0.4f, 0.2f, -0.3f);
  add_light(rim);
  gl_model = load_glb("WaterBottle.glb");
  if (gl_model) t3_object_add(&S->base, gl_model->scene);
}
static void g4_frame(void) {
  frame_no++;
  if (gl_model) t3_object_set_rotation(gl_model->scene, 0.3f, (float)(frame_no / 50.0), 0);
  t3_renderer_render(R, S, C);
}
static t3_light *g5_sun;
static void g5_setup(void) {
  R = make_renderer(0x202020);
  S = t3_scene_new();
  C = t3_perspective_camera_new(45, 1280.0f / 720, 0.1f, 50);
  t3_object_set_position(C, 0, 0, 3.2f);
  t3_object_look_at(C, 0, 0, 0);
  add_light(t3_ambient_light_new(0x404040, 1));
  g5_sun = t3_directional_light_new(0xffffff, 1);
  t3_object_add(&S->base, g5_sun);
  t3_release(g5_sun);
  gl_model = load_glb("NormalTangentMirrorTest.glb");
  if (gl_model) t3_object_add(&S->base, gl_model->scene);
}
static void g5_frame(void) {
  frame_no++;
  double a = frame_no / 40.0;
  t3_object_set_position(g5_sun, (float)(cos(a) * 2), (float)(sin(a * 0.7) * 2), 2);
  t3_renderer_render(R, S, C);
}

/* ── 11-mixed ────────────────────────────────────────────────────── */
#define MIXED_COUNT 600
static t3_mesh *movers[MIXED_COUNT];
static t3_object *mixed_groups[10];

static void mixed_setup(void) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(55, 1280.0f / 720, 0.1f, 200);
  t3_object_set_position(C, 0, 8, 32);
  t3_object_look_at(C, 0, 0, 0);
  R = make_renderer(0x0a0a12);
  add_light(t3_ambient_light_new(0x303040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 1);
  t3_object_set_position(sun, 6, 10, 8);
  add_light(sun);
  t3_light *pt = t3_point_light_new(0xffaa66, 1.0f, 60, 2);
  t3_object_set_position(pt, -8, 6, 10);
  add_light(pt);
  t3_geometry *geos[8] = {
    t3_box_geometry_new(1, 1, 1, 1, 1, 1), t3_sphere_geometry_new(0.7f, 16, 12),
    t3_cone_geometry_new(0.7f, 1.4f, 14, 1, false), t3_cylinder_geometry_new(0.5f, 0.5f, 1.3f, 14, 1, false),
    t3_torus_geometry_new(0.6f, 0.22f, 10, 18, 6.283185307179586f), t3_circle_geometry_new(0.8f, 18),
    t3_ring_geometry_new(0.35f, 0.8f, 18, 1), t3_plane_geometry_new(1.2f, 1.2f, 1, 1),
  };
  t3_material *mats[6] = {
    t3_mesh_lambert_material_new(0xff6040), t3_mesh_phong_material_new(0x40ff80),
    t3_mesh_basic_material_new(0x4080ff), t3_mesh_standard_material_new(0xffcc40),
    t3_mesh_lambert_material_new(0xcc60ff), t3_mesh_basic_material_new(0x40ffee),
  };
  mats[1]->shininess = 50;
  mats[3]->roughness = 0.5f;
  mats[5]->transparent = true;
  mats[5]->opacity = 0.6f;
  for (int g = 0; g < 10; g++) {
    t3_object *grp = t3_group_new();
    t3_object_set_position(grp, (float)((g % 5 - 2) * 7), g < 5 ? 4.0f : -4.0f, 0);
    t3_object_add(&S->base, grp);
    t3_release(grp);
    mixed_groups[g] = grp;
  }
  for (int i = 0; i < MIXED_COUNT; i++) {
    t3_mesh *m = add_mesh(mixed_groups[i % 10], geos[i % 8], mats[i % 6]);
    int k = i / 10;
    t3_object_set_position(m, (float)(k % 10) - 4.5f, (float)((k / 10) % 6) - 2.5f, (float)(i % 10) - 4.5f);
    t3_object_set_scale(m, 0.5f, 0.5f, 0.5f);
    movers[i] = m;
  }
  for (int i = 0; i < 8; i++) t3_release(geos[i]);
  for (int i = 0; i < 6; i++) t3_release(mats[i]);
}

static void mixed_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < MIXED_COUNT; i++) {
    t3_object *o = &movers[i]->base;
    o->rotation.x = (float)(t + i * 0.013);
    o->rotation.y = (float)(t * 0.6 + i * 0.021);
    t3_object_rotation_changed(o);
  }
  for (int g = 0; g < 10; g++) {
    mixed_groups[g]->rotation.y = (float)(t * 0.2 + g);
    t3_object_rotation_changed(mixed_groups[g]);
  }
  t3_renderer_render(R, S, C);
}

/* ── 19-scenegraph (CPU: updateMatrixWorld + getWorldPosition) ─────── */
#define SG_BRANCHES 40
#define SG_DEPTH 12
static t3_object *sg_nodes[SG_BRANCHES * SG_DEPTH];
static int sg_n;
static double sg_sink;

static void sg_build(t3_object *parent, int depth) {
  if (depth == 0) return;
  t3_object *n = t3_object_new();
  t3_object_set_position(n, 0.1f, 0.2f, 0.3f);
  t3_object_set_rotation(n, 0.01f * depth, 0.02f, 0.03f);
  t3_object_add(parent, n);
  t3_release(n);
  sg_nodes[sg_n++] = n;
  sg_build(n, depth - 1);
}

static void sg_setup(void) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(60, 320.0f / 240, 0.1f, 100);
  R = make_offscreen_320();
  for (int i = 0; i < SG_BRANCHES; i++) sg_build(&S->base, SG_DEPTH);
}

static void sg_frame(void) {
  frame_no++;
  double t = frame_no * 0.01;
  for (int i = 0; i < sg_n; i += 7) {
    sg_nodes[i]->rotation.y = (float)t;
    t3_object_rotation_changed(sg_nodes[i]);
    sg_nodes[i]->position.x = (float)sin(t + i + 1);
  }
  t3_object_update_matrix_world(S, true);
  sg_sink += sg_nodes[0]->matrix_world.e[12];
  for (int i = 0; i < sg_n; i += 11) sg_sink += t3_object_get_world_position(sg_nodes[i]).x;
  t3_renderer_render(R, S, C);
}
static double sg_probe(void) { return sg_sink; }

void physics3d_setup(void);
void physics3d_frame(void);
void physics2d_setup(void);
void physics2d_frame(void);
t3_renderer *physics_renderer(void);
static int use_physics;
static void p3_setup(void) { use_physics = 1; physics3d_setup(); }
static void p2_setup(void) { use_physics = 1; physics2d_setup(); }

static void empty_setup(void) { R = make_renderer(0); }
static void empty_frame(void) {}
static t3_scene *clear_scene;
static t3_camera *clear_cam;
static void clear_setup(void) {
  R = make_renderer(0x101018);
  clear_scene = t3_scene_new();
  clear_cam = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
}
static void clear_frame(void) { t3_renderer_render(R, clear_scene, clear_cam); }

#include "features.c"
#include "perf.c"

const bench_scene bench_scenes[] = {
  { "00-empty", empty_setup, empty_frame },
  { "00-clear", clear_setup, clear_frame },
  { "01-cubes", cubes_setup, cubes_frame },
  { "01x-cubes-noinst", cubes_noinst_setup, cubes_frame },
  { "02x-lit-noinst", lit_noinst_setup, cubes_frame },
  { "01y-cubes-same", cubes_same_setup, cubes_frame },
  { "01m-cubes-merged-probe", merged_setup, cubes_frame },
  { "g1i", g1i, cubes_frame }, { "g1p", g1p, cubes_frame },
  { "g2i", g2i, cubes_frame }, { "g2p", g2p, cubes_frame },
  { "g4i", g4i, cubes_frame }, { "g4p", g4p, cubes_frame },
  { "g16i", g16i, cubes_frame }, { "g16p", g16p, cubes_frame },
  { "g64i", g64i, cubes_frame }, { "g64p", g64p, cubes_frame },
  { "01z-cubes-same-noinst", cubes_same_noinst_setup, cubes_frame },
  { "02-lit", lit_setup, cubes_frame },
  { "05-heavy", heavy_setup, heavy_frame },
  { "06-heavy-instanced", inst_setup, inst_frame },
  { "06p-heavy-instanced-plain", inst_plain_setup, inst_frame },
  { "03-instanced", s03, cubes_frame },
  { "04-geometry", s04, cubes_frame },
  { "07-static-instanced", s07, cubes_frame },
  { "09-static-64", s09, cubes_frame },
  { "10-static-256", s10, cubes_frame },
  { "11-mixed", mixed_setup, mixed_frame },
  { "12-suzanne", sz_setup, cubes_frame },
  { "t1-texture", t1_setup, t1_frame },
  { "t2-fog", t2_setup, t2_frame },
  { "t3-shadow-dir", t3s_setup, t3s_frame },
  { "t4-shadow-spot-point", t4_setup, t4_frame },
  { "g1-box-textured", g1_setup, g1_frame },
  { "g2-cesium-man", g2_setup, gl_anim_frame },
  { "g3-morph-cube", g3_setup, gl_anim_frame },
  { "g4-water-bottle", g4_setup, g4_frame },
  { "g5-normal-tangent", g5_setup, g5_frame },
  { "14-cpu-math", cm_setup, cm_frame, cm_probe },
  { "16-curves", cv_setup, cv_frame, cv_probe },
  { "17-animation", an_setup, an_frame, an_probe },
  { "15-raycast", rc_setup, rc_frame, rc_probe },
  { "18-geomgen", gg_setup, gg_frame, gg_probe },
  { "19-scenegraph", sg_setup, sg_frame, sg_probe },
  { "r1-render-target", r1_setup, r1_frame },
  { "r2-tone-mapping", r2_setup, r2_frame },
  { "r4-env-map", r4_setup, r4_frame },
  { "r5-pmrem", r5_setup, r5_frame },
  { "r6-sprite-points", r6_setup, r6_frame },
  { "r7-lod", r7_setup, r7_frame, r7_probe },
  { "r9-physical-lights", r9_setup, r9_frame },
  { "r10-light-map", r10_setup, r10_frame },
  { "a1-allocator", a1_setup, g4_frame, a1_probe },
  { "w1-world-matrix", w1_setup, w1_frame },
  { "i1-instance-color", i1_setup, i1_frame },
  { "t5-transparent-basic", t5_setup, t5_frame },
  { "p1-physical", p1_setup, p1_frame },
  { "p2-transmission", xm_setup, xm_frame },
  { "t6-shadow-cover", sc_setup, sc_frame },
  { "q1-gpu-timer", q1_setup, q1_frame, q1_probe },
  { "x1-texture-from-gl", x1_setup, t1_frame },
  { "s1-stress", stress_setup, stress_frame, stress_probe },
  { "physics3d", p3_setup, physics3d_frame },
  { "physics2d", p2_setup, physics2d_frame },
  { "c1-material-churn", c1_setup, c1_frame, c1_probe },
  { "c2-compile-zoo", c2_setup, cubes_frame, c1_probe },
  { "d1-unique-materials", d1_setup, cubes_frame },
  { "d2-unique-textures", d2_setup, cubes_frame },
  { "d3-program-mix", d3_setup, cubes_frame },
  { NULL, NULL, NULL },
};

t3_renderer *bench_renderer(void) { return use_physics ? physics_renderer() : R; }
