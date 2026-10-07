/* Two physics demos for the bench host: Box3D bodies driving three.c meshes,
 * and Box2D bodies driving a 2D scene under an orthographic camera. Both
 * step physics at a fixed 60 Hz, one step per frame, so a frame number is
 * reproducible. */
#include <math.h>

#include "../bench/scenes.h"
#include "three_box2d.h"
#include "three_box3d.h"

static t3_renderer *R;
static t3_scene *S;
static t3_camera *C;

static t3_mesh *add_mesh(t3_geometry *g, t3_material *m) {
  t3_mesh *mesh = t3_mesh_new(g, m);
  t3_object_add(&S->base, mesh);
  t3_release(mesh);
  return mesh;
}

/* ── Box3D: a tower of boxes and spheres dropped on a slab ───────── */
static b3WorldId world3;

void physics3d_setup(void) {
  S = t3_scene_new();
  S->has_background = true;
  S->background = t3_color_hex(0x8fb8de);
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 500);
  t3_object_set_position(C, 0, 22, 46);
  t3_object_look_at(C, 0, 4, 0);
  R = t3_renderer_new(1280, 720);

  t3_object *hemi = (t3_object *)t3_hemisphere_light_new(0xddeeff, 0x302018, 0.6f);
  t3_object_add(&S->base, hemi);
  t3_release(hemi);
  t3_light *sun = t3_directional_light_new(0xffffff, 0.9f);
  t3_object_set_position(sun, 15, 30, 10);
  t3_object_add(&S->base, sun);
  t3_release(sun);

  b3WorldDef wd = b3DefaultWorldDef();
  world3 = b3CreateWorld(&wd);

  /* ground: a static slab, drawn as a box of the same size */
  t3_geometry *slab = t3_box_geometry_new(40, 1, 40, 1, 1, 1);
  t3_material *slab_mat = t3_mesh_standard_material_new(0x556655);
  slab_mat->roughness = 0.9f;
  t3_mesh *ground = add_mesh(slab, slab_mat);
  t3_object_set_position(ground, 0, -0.5f, 0);
  b3BodyDef gd = b3DefaultBodyDef();
  t3_box3d_place(&gd, &ground->base);
  b3BodyId gb = b3CreateBody(world3, &gd);
  b3BoxHull gh = b3MakeBoxHull(20, 0.5f, 20);
  b3ShapeDef sd = b3DefaultShapeDef();
  b3CreateHullShape(gb, &sd, &gh.base);

  t3_geometry *box = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_geometry *ball = t3_sphere_geometry_new(0.5f, 24, 16);
  t3_material *mats[4] = {
    t3_mesh_standard_material_new(0xe8553f), t3_mesh_standard_material_new(0xf2c14e),
    t3_mesh_phong_material_new(0x4d9de0), t3_mesh_lambert_material_new(0x7bc96f),
  };
  mats[0]->roughness = 0.4f;
  mats[1]->metalness = 0.6f; mats[1]->roughness = 0.3f;
  mats[2]->shininess = 80;
  b3BoxHull bh = b3MakeBoxHull(0.5f, 0.5f, 0.5f);
  b3Sphere sph = { { 0, 0, 0 }, 0.5f };
  int n = 0;
  for (int y = 0; y < 16; y++)
    for (int x = -4; x <= 4; x++)
      for (int z = -2; z <= 2; z++, n++) {
        bool is_ball = (x + y + z) % 3 == 0;
        t3_mesh *m = add_mesh(is_ball ? ball : box, mats[n % 4]);
        /* stagger each layer so the tower topples into a pile */
        t3_object_set_position(m, x * 1.15f + (y % 2) * 0.3f, 2 + y * 1.2f, z * 1.15f - (y % 3) * 0.2f);
        t3_object_set_rotation(m, 0.1f * y, 0.37f * x, 0.05f * z);
        b3BodyDef bd = b3DefaultBodyDef();
        bd.type = b3_dynamicBody;
        bd.userData = m;
        t3_box3d_place(&bd, &m->base);
        b3BodyId b = b3CreateBody(world3, &bd);
        b3ShapeDef s = b3DefaultShapeDef();
        if (is_ball) b3CreateSphereShape(b, &s, &sph);
        else b3CreateHullShape(b, &s, &bh.base);
      }
  t3_release(slab); t3_release(slab_mat); t3_release(box); t3_release(ball);
  for (int i = 0; i < 4; i++) t3_release(mats[i]);
}

void physics3d_frame(void) {
  b3World_Step(world3, 1.0f / 60, 4);
  t3_box3d_sync(world3);
  t3_renderer_render(R, S, C);
}

/* ── Box2D: a pyramid hit by a stream of balls ───────────────────── */
static b2WorldId world2;
static t3_geometry *ball2;
static t3_material *ball2_mat;
static int frame2;

void physics2d_setup(void) {
  S = t3_scene_new();
  S->has_background = true;
  S->background = t3_color_hex(0x1b1d2a);
  /* 64 x 36 world units, y up, ground at y = 0 */
  C = t3_orthographic_camera_new(-32, 32, 34, -2, -10, 10);
  R = t3_renderer_new(1280, 720);

  b2WorldDef wd = b2DefaultWorldDef();
  world2 = b2CreateWorld(&wd);

  t3_geometry *ground_g = t3_plane_geometry_new(64, 2, 1, 1);
  t3_material *ground_m = t3_mesh_basic_material_new(0x3a3f5c);
  t3_mesh *ground = add_mesh(ground_g, ground_m);
  t3_object_set_position(ground, 0, -1, 0);
  b2BodyDef gd = b2DefaultBodyDef();
  t3_box2d_place(&gd, &ground->base);
  b2BodyId gb = b2CreateBody(world2, &gd);
  b2Polygon gp = b2MakeBox(32, 1);
  b2ShapeDef sd = b2DefaultShapeDef();
  b2CreatePolygonShape(gb, &sd, &gp);

  t3_geometry *brick = t3_plane_geometry_new(1, 1, 1, 1);
  t3_material *brick_m[3] = { t3_mesh_basic_material_new(0xf25f5c), t3_mesh_basic_material_new(0xffe066),
                              t3_mesh_basic_material_new(0x70c1b3) };
  b2Polygon bp = b2MakeBox(0.5f, 0.5f);
  const int base = 20;
  for (int row = 0; row < base; row++)
    for (int i = 0; i < base - row; i++) {
      t3_mesh *m = add_mesh(brick, brick_m[(row + i) % 3]);
      t3_object_set_position(m, 6 + (i - (base - row) / 2.0f) * 1.02f, 0.5f + row * 1.0f, 0);
      b2BodyDef bd = b2DefaultBodyDef();
      bd.type = b2_dynamicBody;
      bd.userData = m;
      t3_box2d_place(&bd, &m->base);
      b2BodyId b = b2CreateBody(world2, &bd);
      b2ShapeDef s = b2DefaultShapeDef();
      b2CreatePolygonShape(b, &s, &bp);
    }
  ball2 = t3_circle_geometry_new(0.6f, 24);
  ball2_mat = t3_mesh_basic_material_new(0xf7f7ff);
  t3_release(ground_g); t3_release(ground_m); t3_release(brick);
  for (int i = 0; i < 3; i++) t3_release(brick_m[i]);
}

void physics2d_frame(void) {
  /* a ball every 6 frames for the first 4 seconds, thrown at the pyramid */
  if (frame2 < 240 && frame2 % 6 == 0) {
    t3_mesh *m = add_mesh(ball2, ball2_mat);
    t3_object_set_position(m, -28, 6 + (frame2 / 6 % 5) * 2.5f, 0.1f);
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.userData = m;
    bd.linearVelocity = (b2Vec2){ 30, 6 };
    t3_box2d_place(&bd, &m->base);
    b2BodyId b = b2CreateBody(world2, &bd);
    b2ShapeDef s = b2DefaultShapeDef();
    s.density = 4;
    b2Circle c = { { 0, 0 }, 0.6f };
    b2CreateCircleShape(b, &s, &c);
  }
  frame2++;
  b2World_Step(world2, 1.0f / 60, 4);
  t3_box2d_sync(world2);
  t3_renderer_render(R, S, C);
}

t3_renderer *physics_renderer(void) { return R; }
