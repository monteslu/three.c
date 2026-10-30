/* Feature scenes: the scripts in test/scenes, ported line for line. Included by scenes.c. */

/* ── r1-render-target ────────────────────────────────────────────── */
static t3_scene *r1_inner;
static t3_camera *r1_inner_cam;
static t3_mesh *r1_knot, *r1_box, *r1_cube;
static t3_render_target *r1_rt[4];
static void r1_setup(void) {
  R = make_renderer(0x202028);
  r1_inner = t3_scene_new();
  r1_inner->has_background = true;
  r1_inner->background = t3_color_hex(0x335577);
  r1_inner_cam = t3_perspective_camera_new(60, 1, 2, 12);
  t3_object_set_position(r1_inner_cam, 0, 0, 6);
  t3_geometry *kg = t3_torus_knot_geometry_new(1.2f, 0.4f, 96, 12, 2, 3);
  t3_material *km = t3_mesh_phong_material_new(0xffaa33);
  km->shininess = 60;
  r1_knot = add_mesh(&r1_inner->base, kg, km);
  t3_geometry *bg = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_material *bm = t3_mesh_lambert_material_new(0x44ff88);
  r1_box = add_mesh(&r1_inner->base, bg, bm);
  t3_object_set_position(r1_box, 1.8f, 1.2f, -1);
  t3_light *a = t3_ambient_light_new(0x404040, 1);
  t3_object_add(&r1_inner->base, a); t3_release(a);
  t3_light *d = t3_directional_light_new(0xffffff, 1);
  t3_object_set_position(d, 2, 3, 4);
  t3_object_add(&r1_inner->base, d); t3_release(d);
  t3_release(kg); t3_release(km); t3_release(bg); t3_release(bm);

  t3_render_target_options o = t3_render_target_options_default();
  o.min_filter = T3_LINEAR_MIPMAP_LINEAR;
  o.generate_mipmaps = true;
  t3_texture *depth = t3_depth_texture_new(256, 256, 0);
  o.depth_texture = depth;
  r1_rt[0] = t3_render_target_new(256, 256, &o);
  t3_release(depth);
  o = t3_render_target_options_default();
  o.type = T3_HALF_FLOAT_TYPE;
  r1_rt[1] = t3_render_target_new(256, 256, &o);
  r1_rt[1]->texture->color_space = T3_SRGB_COLOR_SPACE;
  o = t3_render_target_options_default();
  o.samples = 4;
  r1_rt[2] = t3_render_target_new(256, 256, &o);
  o = t3_render_target_options_default();
  o.type = T3_FLOAT_TYPE;
  r1_rt[3] = t3_render_target_new(64, 64, &o);

  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 0, 10);
  t3_geometry *quad = t3_plane_geometry_new(2.6f, 2.6f, 1, 1);
  t3_texture *maps[4] = { r1_rt[0]->texture, r1_rt[0]->depth_texture, r1_rt[1]->texture, r1_rt[2]->texture };
  for (int i = 0; i < 4; i++) {
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    t3_material_set_map(m, maps[i]);
    t3_mesh *q = add_mesh(&S->base, quad, m);
    t3_object_set_position(q, -4.5f + i * 3, 1.6f, 0);
    t3_release(m);
  }
  t3_release(quad);
  t3_geometry *cg = t3_box_geometry_new(1.4f, 1.4f, 1.4f, 1, 1, 1);
  t3_material *cm = t3_mesh_standard_material_new(0xffffff);
  t3_material_set_map(cm, r1_rt[0]->texture);
  cm->roughness = 0.5f;
  r1_cube = add_mesh(&S->base, cg, cm);
  t3_object_set_position(r1_cube, -1.5f, -2, 0);
  t3_geometry *sg = t3_plane_geometry_new(0.5f, 0.5f, 1, 1);
  t3_material *sm = t3_mesh_basic_material_new(0xffffff);
  t3_material_set_map(sm, r1_rt[0]->texture);
  t3_mesh *small = add_mesh(&S->base, sg, sm);
  t3_object_set_position(small, 1.5f, -2, 0);
  t3_geometry *fg = t3_plane_geometry_new(1.6f, 1.6f, 1, 1);
  t3_material *fmat = t3_mesh_basic_material_new(0xffffff);
  t3_material_set_map(fmat, r1_rt[3]->texture);
  t3_mesh *floaty = add_mesh(&S->base, fg, fmat);
  t3_object_set_position(floaty, 4, -2, 0);
  t3_release(fg); t3_release(fmat);
  t3_release(cg); t3_release(cm); t3_release(sg); t3_release(sm);
  add_light(t3_ambient_light_new(0x606060, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 0.8f);
  t3_object_set_position(sun, -2, 3, 5);
  add_light(sun);
}
static void r1_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  t3_object_set_rotation(r1_knot, (float)(t * 0.7), (float)(t * 1.1), 0);
  t3_object_set_rotation(r1_box, (float)t, (float)(t * 0.5), 0);
  t3_object_set_rotation(r1_cube, (float)(t * 0.4), (float)(t * 0.6), 0);
  for (int i = 0; i < 4; i++) {
    t3_renderer_set_render_target(R, r1_rt[i]);
    t3_renderer_render(R, r1_inner, r1_inner_cam);
  }
  t3_renderer_set_render_target(R, NULL);
  t3_renderer_render(R, S, C);
}

/* ── r2-tone-mapping ─────────────────────────────────────────────── */
static t3_scene *r2_scenes[6];
static t3_mesh *r2_spin[6];
static const struct { t3_tone_mapping tm; float exposure; t3_color_space enc; } r2_modes[6] = {
  { T3_NO_TONE_MAPPING, 1, T3_LINEAR_SRGB_COLOR_SPACE }, { T3_LINEAR_TONE_MAPPING, 0.6f, T3_LINEAR_SRGB_COLOR_SPACE },
  { T3_REINHARD_TONE_MAPPING, 1.5f, T3_LINEAR_SRGB_COLOR_SPACE }, { T3_CINEON_TONE_MAPPING, 1, T3_LINEAR_SRGB_COLOR_SPACE },
  { T3_ACES_FILMIC_TONE_MAPPING, 0.8f, T3_LINEAR_SRGB_COLOR_SPACE }, { T3_ACES_FILMIC_TONE_MAPPING, 1.2f, T3_SRGB_COLOR_SPACE },
};
static void r2_setup(void) {
  R = make_renderer(0x101018);
  t3_renderer_set_scissor_test(R, true);
  C = t3_perspective_camera_new(50, 426.0f / 360, 0.1f, 50);
  t3_object_set_position(C, 0, 1, 5);
  t3_object_look_at(C, 0, 0, 0);
  t3_geometry *sg = t3_sphere_geometry_new(0.9f, 32, 16), *tg = t3_torus_geometry_new(0.5f, 0.2f, 16, 40, 6.283185307179586f);
  t3_geometry *pg = t3_plane_geometry_new(1.2f, 1.2f, 1, 1);
  for (int i = 0; i < 6; i++) {
    t3_scene *s = r2_scenes[i] = t3_scene_new();
    t3_material *sm = t3_mesh_standard_material_new(0xff8844);
    sm->roughness = 0.35f; sm->metalness = 0.1f;
    t3_mesh *sphere = add_mesh(&s->base, sg, sm);
    t3_object_set_position(sphere, -0.9f, 0, 0);
    t3_material *tm = t3_mesh_phong_material_new(0x66aaff);
    tm->shininess = 80; tm->emissive = t3_color_hex(0x220000);
    r2_spin[i] = add_mesh(&s->base, tg, tm);
    t3_object_set_position(r2_spin[i], 1.1f, 0.3f, 0);
    t3_material *fm = t3_mesh_basic_material_new(0xffcc66);
    fm->tone_mapped = false;
    t3_mesh *flat = add_mesh(&s->base, pg, fm);
    t3_object_set_position(flat, 1.1f, -0.9f, 0.5f);
    t3_material *bm = t3_mesh_basic_material_new(0xffcc66);
    t3_mesh *bright = add_mesh(&s->base, pg, bm);
    t3_object_set_position(bright, -1.6f, -1.1f, 0.5f);
    t3_release(sm); t3_release(tm); t3_release(fm); t3_release(bm);
    t3_light *a = t3_ambient_light_new(0x404040, 1);
    t3_object_add(&s->base, a); t3_release(a);
    t3_light *sun = t3_directional_light_new(0xffffff, 3);
    t3_object_set_position(sun, 2, 3, 4);
    t3_object_add(&s->base, sun); t3_release(sun);
    t3_light *fill = t3_point_light_new(0x88aaff, 2, 20, 2);
    t3_object_set_position(fill, -3, 1, 2);
    t3_object_add(&s->base, fill); t3_release(fill);
  }
  t3_release(sg); t3_release(tg); t3_release(pg);
  S = r2_scenes[0];
}
static void r2_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 6; i++) {
    t3_object_set_rotation(r2_spin[i], (float)t, (float)(t * 0.7), 0);
    int x = (i % 3) * 426 + 1, y = i < 3 ? 360 : 0;
    t3_renderer_set_viewport(R, x, y, 426, 360);
    t3_renderer_set_scissor(R, x, y, 426, 360);
    t3_renderer_set_tone_mapping(R, r2_modes[i].tm, r2_modes[i].exposure);
    t3_renderer_set_output_color_space(R, r2_modes[i].enc);
    t3_renderer_render(R, r2_scenes[i], C);
  }
}

/* ── r6-sprite-points ────────────────────────────────────────────── */
static t3_sprite *r6_sprites[5];
static t3_mesh *r6_cloud, *r6_dots, *r6_box;
static uint32_t r6_seed;
static double r6_rand(void) {
  r6_seed = (uint32_t)(((uint64_t)r6_seed * 16807) % 2147483647);
  return r6_seed / 2147483647.0;
}
static void r6_setup(void) {
  R = make_renderer(0x101820);
  S = t3_scene_new();
  C = t3_perspective_camera_new(55, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 1, 14);
  t3_object_look_at(C, 0, 0, 0);
  r6_seed = 12345;
  enum { N = 64 };
  uint8_t *d = malloc(N * N * 4);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      double dx = (x + 0.5) / N - 0.5, dy = (y + 0.5) / N - 0.5, r = sqrt(dx * dx + dy * dy) * 2;
      double a = (1 - r) * 3;
      a = a < 0 ? 0 : a > 1 ? 1 : a;
      uint8_t *p = d + (y * N + x) * 4;
      p[0] = 255; p[1] = (uint8_t)lround(255 - 120 * r); p[2] = (uint8_t)lround(200 * (1 - r)); p[3] = (uint8_t)lround(a * 255);
    }
  t3_texture *disc = t3_data_texture_new(N, N, d);
  free(d);
  disc->mag_filter = disc->min_filter = T3_LINEAR;
  static const struct { float x, y; uint32_t c; float s, rot, cx, cy; bool att; } sp[5] = {
    { -5, 2, 0x00ffff, 1.5f, 0.0f, 0.5f, 0.5f, true }, { -2.5f, 2, 0xffffff, 2.0f, 0.6f, 0.5f, 0.5f, true },
    { 0, 2, 0xff8080, 1.2f, 0.0f, 0.0f, 0.0f, true }, { 2.5f, 2, 0x80ff80, 1.8f, 1.2f, 1.0f, 1.0f, true },
    { 5, 2, 0xffff60, 0.08f, 0.3f, 0.5f, 0.5f, false },
  };
  for (int i = 0; i < 5; i++) {
    t3_material *m = t3_sprite_material_new(sp[i].c);
    t3_material_set_map(m, disc);
    m->rotation = sp[i].rot;
    m->size_attenuation = sp[i].att;
    t3_sprite *s = r6_sprites[i] = t3_sprite_new(m);
    t3_object_set_position(s, sp[i].x, sp[i].y, 0);
    t3_object_set_scale(s, sp[i].s, sp[i].s * (sp[i].att ? 0.75f : 1), 1);
    s->center.x = sp[i].cx; s->center.y = sp[i].cy;
    t3_object_add(&S->base, s);
    t3_release(s); t3_release(m);
  }
  t3_material *pm = t3_sprite_material_new(0x8844ff);
  pm->opacity = 0.7f;
  t3_sprite *plain = t3_sprite_new(pm);
  t3_object_set_position(plain, -4, -2.5f, -1);
  t3_object_set_scale(plain, 2.5f, 2.5f, 1);
  t3_object_add(&S->base, plain);
  t3_release(plain); t3_release(pm);
  t3_geometry *bg = t3_box_geometry_new(1.5f, 1.5f, 1.5f, 1, 1, 1);
  t3_material *bm = t3_mesh_lambert_material_new(0xcc8844);
  r6_box = add_mesh(&S->base, bg, bm);
  t3_object_set_position(r6_box, -4, -2.5f, 0.5f);
  t3_release(bg); t3_release(bm);

  int n = 1500;
  float *pos = malloc((size_t)n * 12), *col = malloc((size_t)n * 12);
  for (int i = 0; i < n; i++) {
    double u = r6_rand() * 2 - 1, t = r6_rand() * 3.141592653589793 * 2, r = 2 + r6_rand() * 0.4, q = sqrt(1 - u * u);
    pos[i * 3] = (float)(r * q * cos(t)); pos[i * 3 + 1] = (float)(r * u); pos[i * 3 + 2] = (float)(r * q * sin(t));
    col[i * 3] = (float)(0.5 + 0.5 * u); col[i * 3 + 1] = (float)r6_rand(); col[i * 3 + 2] = (float)(1 - 0.5 * r6_rand());
  }
  t3_geometry *cg = t3_geometry_new();
  t3_attribute *pa = t3_attribute_new(T3_FLOAT32, pos, n, 3), *ca = t3_attribute_new(T3_FLOAT32, col, n, 3);
  t3_geometry_set_attribute(cg, T3_ATTR_POSITION, pa);
  t3_geometry_set_attribute(cg, T3_ATTR_COLOR, ca);
  t3_release(pa); t3_release(ca); free(pos); free(col);
  t3_material *cm = t3_points_material_new(0xffffff);
  cm->size = 0.35f;
  t3_material_set_map(cm, disc);
  cm->vertex_colors = true;
  cm->alpha_test = 0.5f;
  r6_cloud = t3_points_new(cg, cm);
  t3_object_set_position(r6_cloud, 1.5f, -2.2f, 0);
  t3_object_add(&S->base, r6_cloud);
  t3_release(r6_cloud); t3_release(cg); t3_release(cm);
  float pos2[40 * 3];
  for (int i = 0; i < 40; i++) {
    pos2[i * 3] = (float)(5 + cos(i * 0.4) * 1.2); pos2[i * 3 + 1] = (float)(-3.5 + i * 0.12); pos2[i * 3 + 2] = (float)(sin(i * 0.4) * 1.2);
  }
  t3_geometry *lg = t3_geometry_new();
  t3_attribute *la = t3_attribute_new(T3_FLOAT32, pos2, 40, 3);
  t3_geometry_set_attribute(lg, T3_ATTR_POSITION, la);
  t3_release(la);
  t3_material *dm = t3_points_material_new(0xff60c0);
  dm->size = 6;
  dm->size_attenuation = false;
  r6_dots = t3_points_new(lg, dm);
  t3_object_add(&S->base, r6_dots);
  t3_release(r6_dots); t3_release(lg); t3_release(dm);
  add_light(t3_ambient_light_new(0x404040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 0.8f);
  t3_object_set_position(sun, 2, 3, 4);
  add_light(sun);
  t3_release(disc);
}
static void r6_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  t3_object_set_rotation(r6_cloud, (float)(t * 0.3), (float)(t * 0.5), 0);
  t3_object_set_rotation(r6_dots, 0, (float)(t * 0.4), 0);
  r6_sprites[1]->mesh.materials[0]->rotation = (float)(0.6 + t);
  r6_sprites[0]->mesh.base.position.y = (float)(2 + sin(t * 2) * 0.5);
  t3_object_set_rotation(r6_box, (float)(t * 0.5), (float)(t * 0.3), 0);
  t3_renderer_render(R, S, C);
}

/* ── r7-lod ──────────────────────────────────────────────────────── */
static t3_lod *r7_lods[10], *r7_manual;
static void r7_setup(void) {
  R = make_renderer(0x202020);
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 200);
  t3_geometry *geos[3] = { t3_icosahedron_geometry_new(1, 3), t3_icosahedron_geometry_new(1, 1), t3_icosahedron_geometry_new(1, 0) };
  t3_material *mats[3] = { t3_mesh_phong_material_new(0x44aaff), t3_mesh_phong_material_new(0x44ff88),
                           t3_mesh_phong_material_new(0xff6644) };
  for (int k = 0; k < 3; k++) mats[k]->flat_shading = true;
  for (int i = 0; i < 10; i++) {
    t3_lod *lod = r7_lods[i] = t3_lod_new();
    for (int k = 2; k >= 0; k--) {
      t3_mesh *m = t3_mesh_new(geos[k], mats[k]);
      t3_lod_add_level(lod, m, k * 8.0f);
      t3_release(m);
    }
    t3_object_set_position(lod, i % 2 ? 2.5f : -2.5f, 0, -i * 5.0f);
    t3_object_add(&S->base, lod);
    t3_release(lod);
  }
  r7_manual = t3_lod_new();
  r7_manual->auto_update = false;
  for (int k = 0; k < 3; k++) {
    t3_mesh *m = t3_mesh_new(geos[k], mats[k]);
    t3_lod_add_level(r7_manual, m, k * 8.0f);
    t3_release(m);
  }
  t3_object_set_position(r7_manual, 0, 3, -6);
  t3_object_add(&S->base, r7_manual);
  t3_release(r7_manual);
  for (int k = 0; k < 3; k++) { t3_release(geos[k]); t3_release(mats[k]); }
  add_light(t3_ambient_light_new(0x404040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 0.9f);
  t3_object_set_position(sun, 3, 5, 4);
  add_light(sun);
}
static double r7_hits;
static void r7_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  t3_object_set_position(C, (float)(sin(t) * 2), 2, (float)(8 - t * 4));
  t3_object_look_at(C, 0, 0, -25);
  /* as three.js: lookAt leaves matrixWorld to the render, so manual.update
   * sees the camera where the last render put it */
  t3_lod_update(r7_manual, C);
  for (int i = 0; i < 10; i++) t3_object_set_rotation(r7_lods[i], 0, (float)t, 0);
  t3_renderer_render(R, S, C);
  r7_hits = r7_manual->current_level + 10 * r7_lods[3]->current_level;
}
static double r7_probe(void) { return r7_hits; }

/* ── r4-env-map ──────────────────────────────────────────────────── */
static t3_scene *r4_scene2;
static t3_camera *r4_cam2;
static t3_mesh *r4_knot;
enum { R4_S = 64 };
static void r4_face(int f, uint8_t *d) {
  static const int tint[6][3] = { { 255, 90, 70 }, { 80, 220, 120 }, { 90, 140, 255 }, { 240, 220, 80 }, { 220, 100, 230 }, { 90, 230, 230 } };
  for (int y = 0; y < R4_S; y++)
    for (int x = 0; x < R4_S; x++) {
      uint8_t *p = d + (y * R4_S + x) * 4;
      int g = ((x >> 3) + (y >> 3)) & 1;
      double k = 0.55 + 0.45 * ((double)y / R4_S);
      for (int c = 0; c < 3; c++) p[c] = (uint8_t)lround(tint[f][c] * k * (g ? 1 : 0.7));
      if (x == R4_S >> 1 || y == R4_S >> 1) p[0] = p[1] = p[2] = 255;
      p[3] = 255;
    }
}
static void r4_setup(void) {
  R = make_renderer(0x000000);
  t3_renderer_set_scissor_test(R, true);
  static uint8_t faces[6][R4_S * R4_S * 4];
  const uint8_t *fp[6];
  for (int f = 0; f < 6; f++) { r4_face(f, faces[f]); fp[f] = faces[f]; }
  t3_texture *env = t3_cube_texture_new(R4_S, fp), *refr = t3_cube_texture_new(R4_S, fp);
  env->color_space = T3_SRGB_COLOR_SPACE;
  refr->mapping = T3_CUBE_REFRACTION_MAPPING;
  S = t3_scene_new();
  t3_scene_set_background_texture(S, env);
  C = t3_perspective_camera_new(60, 960.0f / 720, 0.1f, 100);
  t3_geometry *geo = t3_sphere_geometry_new(1, 48, 24);
  t3_material *mats[6] = { t3_mesh_basic_material_new(0xffeecc), t3_mesh_lambert_material_new(0xffffff),
                           t3_mesh_phong_material_new(0x3366aa), t3_mesh_standard_material_new(0xffffff),
                           t3_mesh_standard_material_new(0xff8844), t3_mesh_phong_material_new(0x88ff88) };
  for (int i = 0; i < 6; i++) t3_material_set_texture(mats[i], T3_ENV_MAP, i == 1 ? refr : env);
  mats[0]->reflectivity = 0.8f;
  mats[1]->refraction_ratio = 0.9f;
  mats[2]->combine = T3_MIX_OPERATION; mats[2]->reflectivity = 0.5f; mats[2]->shininess = 80;
  mats[3]->metalness = 1; mats[3]->roughness = 0.15f;
  mats[4]->metalness = 0.4f; mats[4]->roughness = 0.6f; mats[4]->env_map_intensity = 0.7f;
  mats[5]->combine = T3_ADD_OPERATION; mats[5]->reflectivity = 0.3f;
  for (int i = 0; i < 6; i++) {
    t3_mesh *m = add_mesh(&S->base, geo, mats[i]);
    t3_object_set_position(m, (i % 3 - 1) * 2.6f, i < 3 ? 1.3f : -1.3f, 0);
    t3_release(mats[i]);
  }
  t3_release(geo);
  add_light(t3_ambient_light_new(0x404040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 0.8f);
  t3_object_set_position(sun, 3, 4, 5);
  add_light(sun);
  r4_scene2 = t3_scene_new();
  static uint8_t f2[R4_S * R4_S * 4];
  r4_face(2, f2);
  t3_texture *bg2 = t3_data_texture_new(R4_S, R4_S, f2);
  bg2->wrap_s = bg2->wrap_t = T3_REPEAT;
  bg2->repeat.x = 2; bg2->repeat.y = 1.5f;
  t3_scene_set_background_texture(r4_scene2, bg2);
  t3_scene_set_environment(r4_scene2, env);
  t3_geometry *kg = t3_torus_knot_geometry_new(0.8f, 0.3f, 80, 10, 2, 3);
  t3_material *km = t3_mesh_standard_material_new(0xffffff);
  km->metalness = 0.8f; km->roughness = 0.3f;
  r4_knot = add_mesh(&r4_scene2->base, kg, km);
  t3_release(kg); t3_release(km);
  r4_cam2 = t3_perspective_camera_new(50, 320.0f / 720, 0.1f, 50);
  t3_object_set_position(r4_cam2, 0, 0, 6);
  t3_release(env); t3_release(refr); t3_release(bg2);
}
static void r4_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  t3_object_set_position(C, (float)(sin(t * 0.5) * 7), 1.5f, (float)(cos(t * 0.5) * 7));
  t3_object_look_at(C, 0, 0, 0);
  t3_object_set_rotation(r4_knot, (float)t, (float)(t * 0.6), 0);
  t3_renderer_set_viewport(R, 0, 0, 960, 720);
  t3_renderer_set_scissor(R, 0, 0, 960, 720);
  t3_renderer_render(R, S, C);
  t3_renderer_set_viewport(R, 960, 0, 320, 720);
  t3_renderer_set_scissor(R, 960, 0, 320, 720);
  t3_renderer_render(R, r4_scene2, r4_cam2);
}

/* ── r5-pmrem ────────────────────────────────────────────────────── */
static t3_render_target *r5_env[3];
static void r5_setup(void) {
  R = make_renderer(0x000000);
  static uint8_t faces[6][R4_S * R4_S * 4];
  const uint8_t *fp[6];
  for (int f = 0; f < 6; f++) { r4_face(f, faces[f]); fp[f] = faces[f]; }
  t3_texture *cube = t3_cube_texture_new(R4_S, fp);
  cube->color_space = T3_SRGB_COLOR_SPACE;
  enum { EW = 128, EH = 64 };
  uint8_t *ed = malloc(EW * EH * 4);
  for (int y = 0; y < EH; y++)
    for (int x = 0; x < EW; x++) {
      uint8_t *p = ed + (y * EW + x) * 4;
      double v = (double)y / (EH - 1);
      int band = (x >> 4) & 1;
      double sun = 1 - hypot(x - 90, y - 44) / 6;
      if (sun < 0) sun = 0;
      double c0 = round(40 + 150 * v + 200 * sun + 30 * band), c1 = round(60 + 120 * v + 180 * sun);
      double c2 = round(120 + 100 * (1 - v) + 100 * sun);
      p[0] = (uint8_t)(c0 > 255 ? 255 : c0); p[1] = (uint8_t)(c1 > 255 ? 255 : c1); p[2] = (uint8_t)(c2 > 255 ? 255 : c2);
      p[3] = 255;
    }
  t3_texture *equirect = t3_data_texture_new(EW, EH, ed);
  free(ed);
  equirect->color_space = T3_SRGB_COLOR_SPACE;
  t3_scene *capture = t3_scene_new();
  capture->has_background = true;
  capture->background = t3_color_hex(0x203040);
  static const uint32_t colours[6] = { 0xff4040, 0x40ff40, 0x4040ff, 0xffff40, 0xff40ff, 0x40ffff };
  static const float dirs[6][3] = { { 3, 0, 0 }, { -3, 0, 0 }, { 0, 3, 0 }, { 0, -3, 0 }, { 0, 0, 3 }, { 0, 0, -3 } };
  for (int i = 0; i < 6; i++) {
    t3_geometry *bg = t3_box_geometry_new(1.5f, 1.5f, 1.5f, 1, 1, 1);
    t3_material *bm = t3_mesh_basic_material_new(colours[i]);
    t3_mesh *b = add_mesh(&capture->base, bg, bm);
    t3_object_set_position(b, dirs[i][0], dirs[i][1], dirs[i][2]);
    t3_release(bg); t3_release(bm);
  }
  t3_pmrem_generator *pm = t3_pmrem_generator_new(R);
  r5_env[0] = t3_pmrem_from_cubemap(pm, cube);
  r5_env[1] = t3_pmrem_from_equirectangular(pm, equirect);
  r5_env[2] = t3_pmrem_from_scene(pm, capture, 0.04f, 0.1f, 100);
  t3_pmrem_generator_destroy(pm);
  t3_release(capture); t3_release(cube); t3_release(equirect);
  S = t3_scene_new();
  t3_scene_set_background_texture(S, r5_env[0]->texture);
  C = t3_perspective_camera_new(45, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 0, 13);
  t3_geometry *geo = t3_sphere_geometry_new(0.75f, 48, 24);
  static const float rough[5] = { 0, 0.25f, 0.5f, 0.75f, 1 };
  for (int row = 0; row < 3; row++)
    for (int i = 0; i < 5; i++) {
      t3_material *m = t3_mesh_standard_material_new(row == 2 ? 0xffffff : 0xffeedd);
      m->metalness = row == 1 ? 0.5f : 1;
      m->roughness = rough[i];
      t3_material_set_texture(m, T3_ENV_MAP, r5_env[row]->texture);
      t3_mesh *mesh = add_mesh(&S->base, geo, m);
      t3_object_set_position(mesh, (i - 2) * 1.9f, (1 - row) * 1.9f, 0);
      t3_release(m);
    }
  t3_release(geo);
}
static void r5_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  t3_object_set_position(C, (float)(sin(t * 0.4) * 4), (float)sin(t * 0.3), 12);
  t3_object_look_at(C, 0, 0, 0);
  t3_renderer_render(R, S, C);
}

/* ── a1-allocator: g4-water-bottle (glTF, stb_image, PBR maps) and the
 * PMREM generator under a counting allocator (t3_set_allocator) that tags
 * each block, so a free of memory it did not hand out is caught. probe:
 * blocks freed that were not its own (must be 0), negated if nothing at all
 * was allocated through it. Same pixels as g4-water-bottle. ─────────── */
static long a1_live, a1_total, a1_foreign;
#define A1_MAGIC 0x7433A110u
static void *a1_malloc(size_t n) {
  uint32_t *p = malloc(n + 16);
  if (!p) return NULL;
  p[0] = A1_MAGIC;
  a1_live++; a1_total++;
  return (char *)p + 16;
}
static void a1_free(void *q) {
  if (!q) return;
  uint32_t *p = (uint32_t *)((char *)q - 16);
  if (p[0] != A1_MAGIC) { a1_foreign++; return; }
  p[0] = 0;
  a1_live--;
  free(p);
}
static void *a1_realloc(void *q, size_t n) {
  if (!q) return a1_malloc(n);
  uint32_t *p = (uint32_t *)((char *)q - 16);
  if (p[0] != A1_MAGIC) { a1_foreign++; return NULL; }
  uint32_t *np = realloc(p, n + 16);
  return np ? (char *)np + 16 : NULL;
}
static void a1_setup(void) {
  t3_set_allocator(a1_malloc, a1_realloc, a1_free);
#ifdef A1_CONTROL
  t3_free(malloc(32)); /* a control that must register as foreign */
#endif
  g4_setup();
  t3_pmrem_generator *pm = t3_pmrem_generator_new(R);
  t3_render_target *rt = t3_pmrem_from_scene(pm, S, 0, 0.1f, 100);
  t3_pmrem_generator_destroy(pm);
  t3_release(rt);
}
static double a1_probe(void) { return a1_total ? (double)a1_foreign : -1; }

/* ── i1-instance-color ────────────────────────────────────────────── */
static t3_instanced_mesh *i1_grid, *i1_ring, *i1_row;
static t3_instanced_mesh *i1_add(t3_geometry *g, t3_material *m, int n) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  t3_object_add(&S->base, im);
  t3_release(im);
  t3_release(g);
  t3_release(m);
  return im;
}
static t3_color i1_rgb(double r, double g, double b) { return (t3_color){ (float)r, (float)g, (float)b }; }
/* Matrix4.makeTranslation / makeScale + setPosition */
static void i1_place(t3_instanced_mesh *im, int i, double s, double x, double y, double z) {
  t3_mat4 m;
  t3_mat4_identity(&m);
  m.e[0] = m.e[5] = m.e[10] = (float)s;
  m.e[12] = (float)x; m.e[13] = (float)y; m.e[14] = (float)z;
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void i1_setup(void) {
  R = make_renderer(0x182028);
  t3_renderer_set_shadow_map(R, true, T3_PCF_SHADOW_MAP);
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 8, 14);
  t3_object_look_at(C, 0, 0, 0);
  t3_geometry *pg = t3_plane_geometry_new(24, 24, 1, 1);
  t3_material *gm = t3_mesh_standard_material_new(0x8090a0);
  gm->roughness = 0.9f;
  t3_mesh *ground = add_mesh(&S->base, pg, gm);
  t3_object_set_rotation(ground, -3.141592653589793f / 2, 0, 0);
  ground->base.receive_shadow = true;
  t3_release(pg); t3_release(gm);
  t3_material *bm = t3_mesh_standard_material_new(0xffffff);
  bm->roughness = 0.6f;
  i1_grid = i1_add(t3_box_geometry_new(0.8f, 0.8f, 0.8f, 1, 1, 1), bm, 64);
  i1_grid->mesh.base.cast_shadow = i1_grid->mesh.base.receive_shadow = true;
  for (int i = 0; i < 64; i++) t3_instanced_mesh_set_color_at(i1_grid, i, i1_rgb((i % 8) / 7.0, (i / 8) / 7.0, 0.5));
  i1_ring = i1_add(t3_sphere_geometry_new(0.35f, 16, 8), t3_mesh_lambert_material_new(0xffffff), 300);
  for (int i = 0; i < 300; i++)
    t3_instanced_mesh_set_color_at(i1_ring, i, i1_rgb(0.5 + 0.5 * sin(i * 0.3), 0.5 + 0.5 * sin(i * 0.3 + 2), 0.5 + 0.5 * sin(i * 0.3 + 4)));
  i1_row = i1_add(t3_box_geometry_new(0.6f, 0.6f, 0.6f, 1, 1, 1), t3_mesh_basic_material_new(0xffffff), 16);
  add_light(t3_ambient_light_new(0x303040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 1.5f);
  t3_object_set_position(sun, 5, 10, 4);
  sun->base.cast_shadow = true;
  sun->shadow->map_width = sun->shadow->map_height = 1024;
  t3_camera *sc = sun->shadow->camera;
  sc->left = -10; sc->right = 10; sc->top = 10; sc->bottom = -10; sc->near = 1; sc->far = 30;
  t3_camera_update_projection_matrix(sc);
  sun->shadow->bias = -0.0005f;
  add_light(sun);
}
static void i1_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 64; i++) i1_place(i1_grid, i, 1, (i % 8) * 1.2 - 4.2, 0.4 + 0.3 * sin(t * 2 + i * 0.5), (i / 8) * 1.2 - 4.2);
  i1_grid->instance_matrix->version++;
  for (int i = 0; i < 300; i++) {
    double a = i / 300.0 * 3.141592653589793 * 2 + t * 0.3, rad = 7 + 0.5 * sin(i * 0.7);
    i1_place(i1_ring, i, 0.6 + 0.4 * ((i * 7) % 5) / 4, cos(a) * rad, 1.5 + sin(i * 1.3) * 0.8, sin(a) * rad);
  }
  i1_ring->instance_matrix->version++;
  for (int i = 0; i < 16; i++) {
    i1_place(i1_row, i, 1, i * 0.9 - 6.75, 4.5, -3);
    t3_instanced_mesh_set_color_at(i1_row, i, i1_rgb(0.5 + 0.5 * sin(t * 3 + i * 0.4), 0.3, 0.5 + 0.5 * cos(t * 2 + i * 0.4)));
  }
  i1_row->instance_matrix->version++;
  i1_row->instance_color->version++;
  t3_renderer_render(R, S, C);
}

/* ── t5-transparent-basic ───────────────────────────────────────────── */
static t3_mesh *t5_boxes[8];
static void t5_setup(void) {
  R = make_renderer(0x202a33);
  S = t3_scene_new();
  C = t3_orthographic_camera_new(-16, 16, 16.5f, -1.5f, -10, 10);
  t3_object_set_position(C, 0, 0, 5);
  t3_material *solid = t3_mesh_basic_material_new(0xe0a040), *ground = t3_mesh_basic_material_new(0x5a6676);
  t3_material *glass = t3_mesh_basic_material_new(0x4aa3df);
  glass->transparent = true;
  glass->opacity = 0.6f;
  t3_geometry *box = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_mesh *floor_ = add_mesh(&S->base, box, ground);
  t3_object_set_scale(floor_, 32, 1, 1);
  for (int i = 0; i < 8; i++) {
    t5_boxes[i] = add_mesh(&S->base, box, i % 3 == 0 ? glass : solid);
    t3_object_set_position(t5_boxes[i], -12 + i * 3.2f, 3, i % 3 == 0 ? 1 : 0);
    t3_object_set_scale(t5_boxes[i], 1.6f, 1.6f, 1);
  }
  t3_release(box); t3_release(solid); t3_release(ground); t3_release(glass);
}
static void t5_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 8; i++) {
    t5_boxes[i]->base.position.y = (float)(3 + sin(t * 2 + i) * 2);
    t3_object_set_rotation(t5_boxes[i], 0, 0, (float)(t + i));
  }
  t3_renderer_render(R, S, C);
}

/* ── p1-physical ─────────────────────────────────────────────────── */
static t3_mesh *p1_balls[6];
static void p1_setup(void) {
  R = make_renderer(0x181c22);
  S = t3_scene_new();
  C = t3_perspective_camera_new(45, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 0.6f, 9);
  t3_object_look_at(C, 0, 0, 0);
  enum { N = 32 };
  static uint8_t nd[N * N * 4];
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      uint8_t *p = nd + (y * N + x) * 4;
      double a = sin(x * 0.8) * 0.5, b = cos(y * 0.6) * 0.5;
      p[0] = (uint8_t)lround((a * 0.5 + 0.5) * 255); p[1] = (uint8_t)lround((b * 0.5 + 0.5) * 255); p[2] = 230; p[3] = 255;
    }
  t3_texture *bumps = t3_data_texture_new(N, N, nd);
  bumps->wrap_s = bumps->wrap_t = T3_REPEAT;
  bumps->repeat = (t3_vec2){ 3, 2 };
  t3_geometry *geo = t3_sphere_geometry_new(0.8f, 48, 24);
  static const uint32_t colors[6] = { 0xc04040, 0x2050c0, 0x20a050, 0x602040, 0x202020, 0xb0b0b0 };
  for (int i = 0; i < 6; i++) {
    t3_material *m = t3_mesh_physical_material_new(colors[i]);
    m->metalness = 0;
    switch (i) {
    case 0: m->roughness = 0.5f; m->ior = 1.8f; m->specular_intensity = 0.7f; m->specular_color = t3_color_hex(0xffd0a0); break;
    case 1: m->roughness = 0.6f; m->clearcoat = 1; m->clearcoat_roughness = 0.05f; break;
    case 2: m->roughness = 0.6f; m->clearcoat = 0.8f; m->clearcoat_roughness = 0.2f;
            t3_material_set_texture(m, T3_CLEARCOAT_NORMAL_MAP, bumps); m->clearcoat_normal_scale = (t3_vec2){ 0.8f, 0.8f }; break;
    case 3: m->roughness = 0.8f; m->sheen = 1; m->sheen_color = t3_color_hex(0xffa0c0); m->sheen_roughness = 0.4f; break;
    case 4: m->roughness = 0.25f; m->metalness = 0.9f; m->iridescence = 1; m->iridescence_ior = 1.6f;
            m->iridescence_thickness_range[0] = 100; m->iridescence_thickness_range[1] = 520; break;
    case 5: m->roughness = 0.4f; m->metalness = 1; m->anisotropy = 0.8f; m->anisotropy_rotation = 0.6f; break;
    }
    p1_balls[i] = add_mesh(&S->base, geo, m);
    t3_object_set_position(p1_balls[i], (float)((i % 3 - 1) * 2.3), i < 3 ? 1.1f : -1.1f, 0);
    t3_release(m);
  }
  t3_release(geo); t3_release(bumps);
  add_light(t3_ambient_light_new(0x404050, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 2.5f);
  t3_object_set_position(sun, 3, 4, 5);
  add_light(sun);
  t3_light *lamp = t3_point_light_new(0xffd8a0, 30, 0, 2);
  t3_object_set_position(lamp, -3, -1, 4);
  add_light(lamp);
}
static void p1_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 6; i++) t3_object_set_rotation(p1_balls[i], (float)(t * 0.3), (float)(t * 0.5 + i), 0);
  t3_renderer_render(R, S, C);
}

/* ── p2-transmission ─────────────────────────────────────────────── */
static t3_mesh *xm_blocks[4];
static void xm_setup(void) {
  R = make_renderer(0x202830);
  S = t3_scene_new();
  C = t3_perspective_camera_new(45, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 0.5f, 9);
  t3_object_look_at(C, 0, 0, 0);
  static const uint32_t stripes[4] = { 0xe04040, 0xe0e040, 0x40c060, 0x4080e0 };
  t3_geometry *sg = t3_box_geometry_new(0.6f, 6, 0.2f, 1, 1, 1);
  for (int i = 0; i < 12; i++) {
    t3_material *m = t3_mesh_standard_material_new(stripes[i % 4]);
    m->roughness = 0.7f;
    t3_mesh *st = add_mesh(&S->base, sg, m);
    t3_object_set_position(st, -6.6f + i * 1.2f, 0, -2);
    t3_release(m);
  }
  t3_release(sg);
  t3_geometry *geo = t3_box_geometry_new(1.6f, 1.6f, 1.6f, 1, 1, 1);
  for (int i = 0; i < 4; i++) {
    t3_material *m = t3_mesh_physical_material_new(i == 3 ? 0xfff0e0 : 0xffffff);
    m->metalness = 0;
    m->transmission = 1;
    switch (i) {
    case 0: m->roughness = 0.05f; m->thickness = 0.5f; m->ior = 1.5f; break;
    case 1: m->roughness = 0.45f; m->thickness = 0.5f; m->ior = 1.5f; break;
    case 2: m->roughness = 0.1f; m->thickness = 2; m->ior = 1.4f; m->attenuation_color = t3_color_hex(0x40c0ff); m->attenuation_distance = 1.5f; break;
    case 3: m->transmission = 0.9f; m->roughness = 0.15f; m->thickness = 1; m->ior = 1.6f; m->side = T3_DOUBLE_SIDE; break;
    }
    xm_blocks[i] = add_mesh(&S->base, geo, m);
    t3_object_set_position(xm_blocks[i], -3.6f + i * 2.4f, 0, 0);
    t3_release(m);
  }
  t3_release(geo);
  add_light(t3_ambient_light_new(0x606070, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(sun, 3, 5, 4);
  add_light(sun);
}
static void xm_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 4; i++) t3_object_set_rotation(xm_blocks[i], (float)(t * 0.4 + i), (float)(t * 0.6), 0);
  t3_renderer_render(R, S, C);
}

/* ── e1-extension ─────────────────────────────────────────────────
 * three.c's example extension program (tools/extensions/tint.mjs, state
 * standard+map+xtint): one material on three objects, its tint (a shared
 * uniform, changed every frame), tintMap (a texture) and tintScale (per
 * object) filled by the hook; a fourth object draws the same material
 * without the extension. BENCH_EXT_NOFILL=1 registers no hook: the three
 * extension draws are skipped, the control that must not match. */
static t3_mesh *e1_meshes[4];
static t3_texture *e1_tint_map;
static float e1_tint[3] = { 1, 1, 1 };
static void e1_fill(t3_extension_ctx *c, void *user) {
  static const float scales[3] = { 0.6f, 1, 1.4f };
  const t3_object *o = t3_extension_object(c);
  float s = 1;
  for (int i = 0; i < 3; i++) if (o == &e1_meshes[i]->base) s = scales[i];
  t3_extension_set_uniform(c, "tint", e1_tint, 3);
  t3_extension_set_uniform(c, "tintScale", &s, 1);
  t3_extension_set_texture(c, "tintMap", e1_tint_map);
  (void)user;
}
static t3_texture *e1_checker(int n, const uint8_t a[3], const uint8_t b[3]) {
  t3_texture *t = checker(n, a, b);
  t->mag_filter = T3_LINEAR; t->min_filter = T3_LINEAR_MIPMAP_LINEAR; t->generate_mipmaps = true;
  return t;
}
static void e1_setup(void) {
  R = make_renderer(0x202028);
  if (!getenv("BENCH_EXT_NOFILL")) t3_renderer_set_extension(R, "tint", e1_fill, NULL);
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 1.5f, 9);
  t3_object_look_at(C, 0, 0, 0);
  const uint8_t m1[3] = { 230, 200, 160 }, m2[3] = { 250, 250, 250 }, t1[3] = { 255, 120, 60 }, t2[3] = { 80, 160, 255 };
  t3_texture *map = e1_checker(32, m1, m2);
  e1_tint_map = e1_checker(16, t1, t2);
  t3_material *ext = t3_mesh_standard_material_new(0xffffff), *plain = t3_mesh_standard_material_new(0xffffff);
  t3_material *mats[2] = { ext, plain };
  for (int i = 0; i < 2; i++) { t3_material_set_map(mats[i], map); mats[i]->roughness = 0.5f; mats[i]->metalness = 0.1f; }
  t3_material_set_extension(ext, "tint");
  t3_geometry *geos[4] = { t3_box_geometry_new(1.6f, 1.6f, 1.6f, 1, 1, 1), t3_sphere_geometry_new(1, 32, 16),
                           t3_torus_geometry_new(0.8f, 0.3f, 16, 48, 6.283185307179586f), t3_sphere_geometry_new(1, 32, 16) };
  for (int i = 0; i < 4; i++) {
    e1_meshes[i] = add_mesh(&S->base, geos[i], i < 3 ? ext : plain);
    t3_object_set_position(e1_meshes[i], -4.5f + i * 3, 0, 0);
    t3_release(geos[i]);
  }
  t3_release(ext); t3_release(plain); t3_release(map);
  add_light(t3_ambient_light_new(0x404040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 0.9f);
  t3_object_set_position(sun, 3, 5, 6);
  add_light(sun);
  t3_light *lamp = t3_point_light_new(0xffe0c0, 6, 0, 2);
  t3_object_set_position(lamp, -2, 2, 3);
  add_light(lamp);
}
static void e1_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  e1_tint[0] = 1; e1_tint[1] = (float)(0.6 + 0.4 * sin(t)); e1_tint[2] = 0.8f;
  for (int i = 0; i < 4; i++) t3_object_set_rotation(e1_meshes[i], (float)(t * 0.5 + i), (float)(t * 0.8 + i * 0.5), 0);
  t3_renderer_render(R, S, C);
}

/* ── e2-extension-shadow ──────────────────────────────────────────
 * the example extension that moves vertices (tools/extensions/swell.mjs):
 * three objects pushed along their normals by a value per object, casting
 * with the depth+xswell caster onto a ground plane; a fourth, plain one casts
 * with the plain caster. BENCH_EXT_NOFILL=1 registers no hook. */
static t3_mesh *e2_meshes[4];
static void e2_fill(t3_extension_ctx *c, void *user) {
  static const float swells[3] = { 0.3f, 0.5f, 0.15f };
  const t3_object *o = t3_extension_object(c);
  float s = 0;
  for (int i = 0; i < 3; i++) if (o == &e2_meshes[i]->base) s = swells[i];
  t3_extension_set_uniform(c, "swell", &s, 1);
  (void)user;
}
static void e2_setup(void) {
  R = make_renderer(0x202830);
  t3_renderer_set_shadow_map(R, true, T3_PCF_SHADOW_MAP);
  if (!getenv("BENCH_EXT_NOFILL")) t3_renderer_set_extension(R, "swell", e2_fill, NULL);
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 6, 11);
  t3_object_look_at(C, 0, 0, 0);
  t3_geometry *pg = t3_plane_geometry_new(20, 20, 1, 1);
  t3_material *gm = t3_mesh_standard_material_new(0x8090a0);
  gm->roughness = 0.9f;
  t3_mesh *ground = add_mesh(&S->base, pg, gm);
  t3_object_set_rotation(ground, -3.141592653589793f / 2, 0, 0);
  ground->base.receive_shadow = true;
  t3_material *ext = t3_mesh_standard_material_new(0xe08040), *plain = t3_mesh_standard_material_new(0x40a0e0);
  ext->roughness = plain->roughness = 0.5f;
  t3_material_set_extension(ext, "swell");
  t3_geometry *geos[4] = { t3_box_geometry_new(1.2f, 1.2f, 1.2f, 4, 4, 4), t3_sphere_geometry_new(0.8f, 32, 16),
                           t3_torus_geometry_new(0.7f, 0.25f, 16, 48, 6.283185307179586f), t3_sphere_geometry_new(0.8f, 32, 16) };
  for (int i = 0; i < 4; i++) {
    e2_meshes[i] = add_mesh(&S->base, geos[i], i < 3 ? ext : plain);
    t3_object_set_position(e2_meshes[i], i * 3 - 4.5f, 1.6f, 0);
    e2_meshes[i]->base.cast_shadow = true;
    t3_release(geos[i]);
  }
  add_light(t3_ambient_light_new(0x303040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 1.2f);
  t3_object_set_position(sun, 3, 10, 5);
  sun->base.cast_shadow = true;
  sun->shadow->map_width = sun->shadow->map_height = 1024;
  t3_camera *sc = sun->shadow->camera;
  sc->left = -8; sc->right = 8; sc->top = 8; sc->bottom = -8; sc->near = 1; sc->far = 30;
  t3_camera_update_projection_matrix(sc);
  sun->shadow->bias = -0.0005f;
  add_light(sun);
  t3_release(pg); t3_release(gm); t3_release(ext); t3_release(plain);
}
static void e2_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 4; i++) t3_object_set_rotation(e2_meshes[i], (float)(t * 0.6 + i), (float)(t * 0.4 + i * 0.3), 0);
  t3_renderer_render(R, S, C);
}

/* ── e3-extension-textures ────────────────────────────────────────
 * the example extension with an array texture and an integer texture
 * (tools/extensions/layers.mjs): an R32UI index picks per cell a layer of an
 * RGBA8 DataArrayTexture; a shift per object. BENCH_EXT_NOFILL=1 registers
 * no hook. */
static t3_mesh *e3_meshes[4];
static t3_texture *e3_index, *e3_map;
static void e3_fill(t3_extension_ctx *c, void *user) {
  const t3_object *o = t3_extension_object(c);
  float s = 0;
  for (int i = 0; i < 3; i++) if (o == &e3_meshes[i]->base) s = (float)i;
  t3_extension_set_uniform(c, "layerShift", &s, 1);
  t3_extension_set_texture(c, "layerIndex", e3_index);
  t3_extension_set_texture(c, "layerMap", e3_map);
  (void)user;
}
static void e3_setup(void) {
  enum { LAYERS = 4 };
  R = make_renderer(0x202028);
  if (!getenv("BENCH_EXT_NOFILL")) t3_renderer_set_extension(R, "layers", e3_fill, NULL);
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 1.5f, 9);
  t3_object_look_at(C, 0, 0, 0);
  uint32_t idx[16];
  for (int i = 0; i < 16; i++) idx[i] = (uint32_t)((i * 7 + (i >> 2)) % LAYERS);
  e3_index = t3_data_texture_new_format(4, 4, idx, T3_RED_INTEGER_FORMAT, T3_UNSIGNED_INT_TYPE);
  static const int r0[4] = { 230, 60, 240, 40 }, g0[4] = { 70, 200, 220, 90 }, b0[4] = { 50, 80, 60, 230 };
  uint8_t data[4 * 4 * 4 * LAYERS];
  for (int l = 0; l < LAYERS; l++)
    for (int i = 0; i < 16; i++) {
      uint8_t *o = data + (l * 16 + i) * 4;
      o[0] = (uint8_t)(r0[l] - (i & 3) * 12); o[1] = (uint8_t)(g0[l] + (i >> 2) * 8); o[2] = (uint8_t)b0[l]; o[3] = 255;
    }
  e3_map = t3_data_array_texture_new(4, 4, LAYERS, data, T3_RGBA_FORMAT, T3_UNSIGNED_BYTE_TYPE);
  e3_map->mag_filter = e3_map->min_filter = T3_LINEAR;
  t3_material *ext = t3_mesh_standard_material_new(0xffffff), *plain = t3_mesh_standard_material_new(0xffffff);
  ext->roughness = plain->roughness = 0.6f;
  ext->metalness = plain->metalness = 0.1f;
  t3_material_set_extension(ext, "layers");
  t3_geometry *geos[4] = { t3_box_geometry_new(1.6f, 1.6f, 1.6f, 1, 1, 1), t3_sphere_geometry_new(1, 32, 16),
                           t3_torus_geometry_new(0.8f, 0.3f, 16, 48, 6.283185307179586f), t3_sphere_geometry_new(1, 32, 16) };
  for (int i = 0; i < 4; i++) {
    e3_meshes[i] = add_mesh(&S->base, geos[i], i < 3 ? ext : plain);
    t3_object_set_position(e3_meshes[i], -4.5f + i * 3, 0, 0);
    t3_release(geos[i]);
  }
  t3_release(ext); t3_release(plain);
  add_light(t3_ambient_light_new(0x404040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 1.2f);
  t3_object_set_position(sun, 3, 5, 6);
  add_light(sun);
}
static void e3_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 4; i++) t3_object_set_rotation(e3_meshes[i], (float)(t * 0.5 + i), (float)(t * 0.8 + i * 0.5), 0);
  t3_renderer_render(R, S, C);
}

/* ── e4-extension-many-textures ───────────────────────────────────
 * the example extension with many textures (tools/extensions/mosaic.mjs):
 * ten tiles and a height texture read in both stages, thirteen bindings in
 * one group; a lift per object. BENCH_EXT_NOFILL=1 registers no hook. */
enum { E4_TILES = 10 };
static t3_mesh *e4_meshes[4];
static t3_texture *e4_tiles[E4_TILES], *e4_height;
static void e4_fill(t3_extension_ctx *c, void *user) {
  static const float lifts[3] = { 0, 0.15f, 0.3f };
  const t3_object *o = t3_extension_object(c);
  float s = 0;
  for (int i = 0; i < 3; i++) if (o == &e4_meshes[i]->base) s = lifts[i];
  t3_extension_set_uniform(c, "mosaicLift", &s, 1);
  for (int i = 0; i < E4_TILES; i++) {
    char name[16];
    snprintf(name, sizeof name, "tile%d", i);
    t3_extension_set_texture(c, name, e4_tiles[i]);
  }
  t3_extension_set_texture(c, "height", e4_height);
  (void)user;
}
static void e4_setup(void) {
  R = make_renderer(0x202028);
  if (!getenv("BENCH_EXT_NOFILL")) t3_renderer_set_extension(R, "mosaic", e4_fill, NULL);
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 1.5f, 9);
  t3_object_look_at(C, 0, 0, 0);
  for (int i = 0; i < E4_TILES; i++) {
    const uint8_t px[16] = { (uint8_t)(25 * i), (uint8_t)(255 - 20 * i), 128, 255, 200, (uint8_t)(30 * i), (uint8_t)(255 - 25 * i), 255,
                             90, 90, (uint8_t)(20 * i), 255, 255, 255, 255, 255 };
    e4_tiles[i] = t3_data_texture_new(2, 2, px);
    e4_tiles[i]->mag_filter = e4_tiles[i]->min_filter = T3_LINEAR;
  }
  uint8_t hpx[64];
  for (int i = 0; i < 64; i++) hpx[i] = (uint8_t)((i * 37) & 255);
  e4_height = t3_data_texture_new(4, 4, hpx);
  t3_material *ext = t3_mesh_standard_material_new(0xffffff), *plain = t3_mesh_standard_material_new(0xffffff);
  ext->roughness = plain->roughness = 0.6f;
  ext->metalness = plain->metalness = 0.1f;
  t3_material_set_extension(ext, "mosaic");
  t3_geometry *geos[4] = { t3_box_geometry_new(1.6f, 1.6f, 1.6f, 4, 4, 4), t3_sphere_geometry_new(1, 32, 16),
                           t3_torus_geometry_new(0.8f, 0.3f, 16, 48, 6.283185307179586f), t3_sphere_geometry_new(1, 32, 16) };
  for (int i = 0; i < 4; i++) {
    e4_meshes[i] = add_mesh(&S->base, geos[i], i < 3 ? ext : plain);
    t3_object_set_position(e4_meshes[i], -4.5f + i * 3, 0, 0);
    t3_release(geos[i]);
  }
  t3_release(ext); t3_release(plain);
  add_light(t3_ambient_light_new(0x404040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 1.2f);
  t3_object_set_position(sun, 3, 5, 6);
  add_light(sun);
}
static void e4_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 4; i++) t3_object_set_rotation(e4_meshes[i], (float)(t * 0.5 + i), (float)(t * 0.8 + i * 0.5), 0);
  t3_renderer_render(R, S, C);
}

/* ── t6-shadow-cover ──────────────────────────────────────────────── */
static t3_mesh *sc_balls[3];
static t3_light *sc_flash;
static void sc_setup(void) {
  R = make_renderer(0x182028);
  t3_renderer_set_shadow_map(R, true, T3_PCF_SHADOW_MAP);
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 5, 11);
  t3_object_look_at(C, 0, 0.5f, 0);
  enum { N = 16 };
  static const uint8_t tints[6][3] = { { 200, 120, 90 }, { 90, 160, 220 }, { 230, 230, 240 }, { 60, 70, 60 }, { 160, 200, 140 }, { 120, 100, 180 } };
  static uint8_t fd[6][N * N * 4];
  const uint8_t *fp[6];
  for (int f = 0; f < 6; f++) {
    for (int i = 0; i < N * N; i++) { memcpy(fd[f] + i * 4, tints[f], 3); fd[f][i * 4 + 3] = 255; }
    fp[f] = fd[f];
  }
  t3_texture *env = t3_cube_texture_new(N, fp);
  env->generate_mipmaps = false;
  env->min_filter = T3_LINEAR;
  t3_scene_set_environment(S, env);
  t3_release(env);
  t3_geometry *pg = t3_plane_geometry_new(16, 16, 1, 1);
  t3_material *gm = t3_mesh_standard_material_new(0x8090a0);
  gm->roughness = 0.9f;
  t3_mesh *ground = add_mesh(&S->base, pg, gm);
  t3_object_set_rotation(ground, -3.141592653589793f / 2, 0, 0);
  ground->base.receive_shadow = true;
  t3_release(pg); t3_release(gm);
  t3_geometry *geo = t3_sphere_geometry_new(0.8f, 32, 16);
  static const uint32_t cols[3] = { 0xe05040, 0x40a0e0, 0xe0c040 };
  for (int i = 0; i < 3; i++) {
    t3_material *m = t3_mesh_standard_material_new(cols[i]);
    m->roughness = 0.3f + i * 0.2f; m->metalness = i * 0.4f;
    sc_balls[i] = add_mesh(&S->base, geo, m);
    t3_object_set_position(sc_balls[i], (float)((i - 1) * 2.5), 0.8f, 0);
    sc_balls[i]->base.cast_shadow = sc_balls[i]->base.receive_shadow = true;
    t3_release(m);
  }
  t3_release(geo);
  add_light(t3_hemisphere_light_new(0x8090ff, 0x302010, 0.6f));
  t3_light *sun = t3_directional_light_new(0xffffff, 1.5f);
  t3_object_set_position(sun, 4, 8, 3);
  sun->base.cast_shadow = true;
  sun->shadow->map_width = sun->shadow->map_height = 1024;
  t3_camera *sh = sun->shadow->camera;
  sh->left = -8; sh->right = 8; sh->top = 8; sh->bottom = -8; sh->near = 1; sh->far = 30;
  t3_camera_update_projection_matrix(sh);
  sun->shadow->bias = -0.0005f;
  add_light(sun);
  sc_flash = t3_point_light_new(0xffa040, 25, 0, 2);
  t3_object_add(&S->base, sc_flash);
  t3_release(sc_flash);
}
static void sc_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  t3_object_set_position(sc_flash, (float)(sin(t) * 3), 1.5f, (float)(cos(t) * 2 + 1));
  for (int i = 0; i < 3; i++) sc_balls[i]->base.position.y = (float)(0.8 + fabs(sin(t * 2 + i)) * 0.8);
  t3_renderer_render(R, S, C);
}

/* ── w1-world-matrix ─────────────────────────────────────────────── */
static t3_object *w1_group;
static t3_mesh *w1_a, *w1_boxes[6];
/* the embedder's own transform math: Object3D.updateMatrix in doubles */
static void w1_world(t3_object *o, double px, double py, double pz, double rx, double ry, double rz, double sy) {
  t3_euler e = { (float)rx, (float)ry, (float)rz, T3_XYZ };
  t3_quat q = t3_quat_from_euler(e);
  const double p[3] = { px, py, pz }, qq[4] = { q.x, q.y, q.z, q.w }, s[3] = { 1, sy, 1 };
  double m[16];
  t3_mat4d_compose(m, p, qq, s);
  t3_object_set_matrix_world_d(o, m);
}
static void w1_setup(void) {
  R = make_renderer(0x181818);
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 3, 12);
  t3_object_look_at(C, 0, 0, 0);
  w1_group = t3_group_new();
  t3_object_add(&S->base, w1_group);
  t3_release(w1_group);
  t3_geometry *ag = t3_box_geometry_new(1, 1, 1, 1, 1, 1), *bg = t3_sphere_geometry_new(0.6f, 24, 12);
  t3_material *am = t3_mesh_phong_material_new(0xff8844), *bm = t3_mesh_phong_material_new(0x44aaff);
  w1_a = add_mesh(w1_group, ag, am);
  t3_object_set_position(w1_a, 2, 0, 0);
  t3_mesh *b = add_mesh(w1_group, bg, bm);
  t3_object_set_position(b, -2, 0.5f, 0);
  t3_release(ag); t3_release(bg); t3_release(am); t3_release(bm);
  t3_geometry *boxg = t3_box_geometry_new(0.7f, 0.7f, 0.7f, 1, 1, 1);
  t3_material *boxm = t3_mesh_lambert_material_new(0x88ee66);
  for (int i = 0; i < 6; i++) w1_boxes[i] = add_mesh(&S->base, boxg, boxm);
  t3_release(boxg); t3_release(boxm);
  add_light(t3_ambient_light_new(0x404040, 1));
  t3_light *sun = t3_directional_light_new(0xffffff, 0.9f);
  t3_object_set_position(sun, 3, 5, 4);
  add_light(sun);
}
static void w1_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  w1_world(w1_group, sin(t) * 1.5, 1, 0, 0, t, 0.2, 1);
  t3_object_set_rotation(w1_a, (float)(t * 2), 0, 0); /* a child three.c updates under the embedder's group */
  for (int i = 0; i < 6; i++) w1_world(&w1_boxes[i]->base, -5 + i * 2, -2, sin(t + i), t + i, t * 0.5, 0, 1 + 0.3 * sin(t * 2 + i));
  t3_renderer_render(R, S, C);
}

/* ── q1-gpu-timer: 05-heavy with GPU timing on. probe: 1 when a GPU time
 * arrived (results come once the GL commands are flushed: a frame late when
 * the host swaps or finishes every frame), -1 when timer queries are not available in
 * this context, 0 when they are but no result came (a failure) ────────── */
static double q1_ms;
static void q1_setup(void) {
  heavy_setup();
  t3_renderer_set_gpu_timing(R, true);
}
static void q1_frame(void) {
  heavy_frame();
  const t3_render_info *in = t3_renderer_info(R);
  q1_ms = in->gpu_timer_supported == 0 ? -1 : in->gpu_ms > 0 ? 1 : 0;
  if (in->gpu_ms > 0 && in->frame % 25 == 0) fprintf(stderr, "q1: frame %u gpu %.3f ms (frame %u)\n", in->frame, in->gpu_ms, in->gpu_frame);
}
static double q1_probe(void) { return q1_ms; }

/* ── x1-texture-from-gl: t1-texture with two of its textures uploaded by
 * "the embedder" in raw GL and handed over with t3_texture_from_gl (one
 * owned by three.c, one kept, with the embedder's own mipmaps and sRGB
 * decode): the same pixels as t1-texture. ───────────────────────────── */
#include "../src/gl.h"
static GLuint x1_kept;
static GLuint x1_upload(int n, const uint8_t a[3], const uint8_t b[3], bool mip) {
  uint8_t *d = malloc((size_t)n * n * 4);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) {
      const uint8_t *c = (((x >> 2) + (y >> 2)) & 1) ? a : b;
      uint8_t *p = d + (y * n + x) * 4;
      p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
    }
  GLuint tex;
  glGenTextures(1, &tex);
  glActiveTexture(GL_TEXTURE0 + 5);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, d);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mip ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
  if (mip) glGenerateMipmap(GL_TEXTURE_2D);
  free(d);
  return tex;
}
static void x1_setup(void) {
  t1_setup();
  /* t1's textures 1 (meshes 0 and 3) and 3 (mesh 2), uploaded here instead */
  const uint8_t r1[3] = { 230, 60, 40 }, w1[3] = { 250, 240, 220 }, g3[3] = { 30, 160, 80 }, y3[3] = { 250, 220, 90 };
  GLuint own = x1_upload(32, r1, w1, false);
  x1_kept = x1_upload(64, g3, y3, true);
  t3_texture *ta = t3_texture_from_gl(own, 32, 32, 1, false, true);
  t3_texture *tb = t3_texture_from_gl(x1_kept, 64, 64, 7, false, false);
  tb->color_space = T3_SRGB_COLOR_SPACE;
  t3_material_set_map(t1_meshes[0]->materials[0], ta);
  t3_material_set_map(t1_meshes[3]->materials[0], ta);
  t3_material_set_map(t1_meshes[2]->materials[0], tb);
  t3_release(ta);
  t3_release(tb);
}

/* ── r9-physical-lights ──────────────────────────────────────────── */
static t3_scene *r9_scenes[2];
static t3_mesh *r9_movers[2];
static void r9_add_light(t3_scene *s, t3_light *l, float x, float y, float z) {
  t3_object_set_position(l, x, y, z);
  t3_object_add(&s->base, l);
  t3_release(l);
}
static void r9_setup(void) {
  R = make_renderer(0x080810);
  t3_renderer_set_scissor_test(R, true);
  C = t3_perspective_camera_new(55, 640.0f / 720, 0.1f, 60);
  t3_object_set_position(C, 0, 2.2f, 7);
  t3_object_look_at(C, 0, 0.8f, 0);
  t3_geometry *fg = t3_plane_geometry_new(10, 10, 1, 1), *sg = t3_sphere_geometry_new(0.6f, 32, 16);
  t3_geometry *bg = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  for (int i = 0; i < 2; i++) {
    t3_scene *s = r9_scenes[i] = t3_scene_new();
    float decay = i ? 1 : 2;
    t3_material *fm = t3_mesh_standard_material_new(0x9a9a9a);
    fm->roughness = 0.8f;
    t3_mesh *floor = add_mesh(&s->base, fg, fm);
    t3_object_set_rotation(floor, -3.141592653589793f / 2, 0, 0);
    t3_material *am = t3_mesh_standard_material_new(0xff8040);
    am->roughness = 0.4f; am->metalness = 0.2f;
    t3_mesh *a = add_mesh(&s->base, sg, am);
    t3_object_set_position(a, -1.6f, 0.6f, 0);
    t3_material *bm = t3_mesh_phong_material_new(0x40a0ff);
    bm->shininess = 60;
    t3_mesh *b = add_mesh(&s->base, sg, bm);
    t3_object_set_position(b, 0, 0.6f, 0.5f);
    t3_material *cm = t3_mesh_lambert_material_new(0x80ff60);
    r9_movers[i] = add_mesh(&s->base, bg, cm);
    t3_object_set_position(r9_movers[i], 1.6f, 0.5f, 0);
    t3_release(fm); t3_release(am); t3_release(bm); t3_release(cm);
    t3_light *amb = t3_ambient_light_new(0x202020, 1);
    t3_object_add(&s->base, amb); t3_release(amb);
    t3_light *hemi = t3_hemisphere_light_new(0x8090ff, 0x302010, 0.4f);
    t3_object_add(&s->base, hemi); t3_release(hemi);
    r9_add_light(s, t3_directional_light_new(0xffffff, 0.8f), -3, 5, 2);
    r9_add_light(s, t3_point_light_new(0xffaa66, 6, 0, decay), -1, 1.8f, 1.5f);
    r9_add_light(s, t3_point_light_new(0x66ccff, 8, 6, decay), 1.5f, 1.2f, 1.8f);
    t3_light *spot = t3_spot_light_new(0xffffff, 20, 0, 3.141592653589793f / 6, 0.3f, decay);
    t3_object_set_position(spot->target, 0, 0, 0.5f);
    t3_object_add(&s->base, spot->target);
    r9_add_light(s, spot, 0, 4, 2);
  }
  t3_release(fg); t3_release(sg); t3_release(bg);
  S = r9_scenes[0];
}
static void r9_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 2; i++) {
    t3_object_set_rotation(r9_movers[i], 0, (float)t, 0);
    t3_renderer_set_viewport(R, i * 640, 0, 640, 720);
    t3_renderer_set_scissor(R, i * 640, 0, 640, 720);
    t3_renderer_render(R, r9_scenes[i], C);
  }
}

/* ── r10-light-map ───────────────────────────────────────────────── */
static t3_scene *r10_scenes[2];
static t3_mesh *r10_movers[4];
static t3_texture *r10_tex(int seed) {
  enum { n = 32 };
  static uint8_t data[n * n * 4];
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) {
      uint8_t *p = data + (y * n + x) * 4;
      p[0] = (uint8_t)((x * 8 + seed * 40) & 255);
      p[1] = (uint8_t)((y * 8) & 255);
      p[2] = (uint8_t)(((x ^ y) * 8 + seed * 16) & 255);
      p[3] = 255;
    }
  t3_texture *t = t3_data_texture_new(n, n, data);
  t->mag_filter = T3_LINEAR;
  t->min_filter = T3_LINEAR;
  return t;
}
static t3_material *r10_lit(t3_material *m, t3_texture *lm, float intensity) {
  t3_material_set_texture(m, T3_LIGHT_MAP, lm);
  m->light_map_intensity = intensity;
  return m;
}
static void r10_setup(void) {
  R = make_renderer(0x080810);
  t3_renderer_set_scissor_test(R, true);
  C = t3_perspective_camera_new(55, 640.0f / 720, 0.1f, 60);
  t3_object_set_position(C, 0, 3.2f, 7);
  t3_object_look_at(C, 0, 0.6f, 0);
  t3_texture *lma = r10_tex(0), *lmb = r10_tex(1), *lmc = r10_tex(2), *ao = r10_tex(3);
  lmb->color_space = T3_SRGB_COLOR_SPACE;
  lmc->offset = (t3_vec2){ 0.25f, 0.1f };
  lmc->repeat = (t3_vec2){ 2, 1.5f };
  lmc->wrap_s = lmc->wrap_t = T3_REPEAT;
  ao->repeat = (t3_vec2){ 0.5f, 0.5f };
  t3_geometry *fg = t3_plane_geometry_new(10, 10, 1, 1);
  t3_geometry *bg = t3_box_geometry_new(1.2f, 1.2f, 1.2f, 1, 1, 1);
  t3_geometry *sg = t3_sphere_geometry_new(0.7f, 32, 16);
  for (int i = 0; i < 2; i++) {
    t3_scene *s = r10_scenes[i] = t3_scene_new();
    t3_material *fm = r10_lit(t3_mesh_standard_material_new(0x9a9a9a), lma, 0.6f);
    fm->roughness = 0.8f;
    t3_mesh *floor = add_mesh(&s->base, fg, fm);
    t3_object_set_rotation(floor, -3.141592653589793f / 2, 0, 0);
    t3_material *am = r10_lit(t3_mesh_phong_material_new(0xff8040), lmb, 1);
    am->shininess = 40;
    t3_object_set_position(add_mesh(&s->base, sg, am), -2.4f, 0.7f, 0);
    t3_material *bm = r10_lit(t3_mesh_lambert_material_new(0x80ff60), lmc, 1.5f);
    r10_movers[i * 2] = add_mesh(&s->base, bg, bm);
    t3_object_set_position(r10_movers[i * 2], -0.8f, 0.6f, 0.6f);
    t3_material *cm = r10_lit(t3_mesh_basic_material_new(0x8080ff), lma, 0.8f);
    r10_movers[i * 2 + 1] = add_mesh(&s->base, bg, cm);
    t3_object_set_position(r10_movers[i * 2 + 1], 0.8f, 0.6f, 0.6f);
    t3_material *dm = r10_lit(t3_mesh_standard_material_new(0xffffff), lmc, 1);
    dm->roughness = 0.5f; dm->metalness = 0.1f;
    t3_material_set_texture(dm, T3_AO_MAP, ao);
    t3_object_set_position(add_mesh(&s->base, sg, dm), 2.4f, 0.7f, 0);
    t3_release(fm); t3_release(am); t3_release(bm); t3_release(cm); t3_release(dm);
    t3_light *amb = t3_ambient_light_new(0x101010, 1);
    t3_object_add(&s->base, amb); t3_release(amb);
    if (i == 0) r9_add_light(s, t3_directional_light_new(0xffffff, 0.5f), -3, 5, 2);
  }
  t3_release(fg); t3_release(sg); t3_release(bg);
  t3_release(lma); t3_release(lmb); t3_release(lmc); t3_release(ao);
  S = r10_scenes[0];
}
static void r10_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  for (int i = 0; i < 2; i++) {
    t3_object_set_rotation(r10_movers[i * 2], 0, (float)t, 0);
    t3_object_set_rotation(r10_movers[i * 2 + 1], (float)(t * 0.5), 0, 0);
    t3_renderer_set_viewport(R, i * 640, 0, 640, 720);
    t3_renderer_set_scissor(R, i * 640, 0, 640, 720);
    t3_renderer_render(R, r10_scenes[i], C);
  }
}


/* ── p3-eon: diffuseRoughness (EON rough diffuse) ─────────────────── */
static void eon_setup(void) {
  R = make_renderer(0x101018);
  C = t3_perspective_camera_new(40, 1280.0f / 720, 0.1f, 60);
  t3_object_set_position(C, 0, 0, 11);
  S = t3_scene_new();
  enum { n = 32 };
  static uint8_t data[n * n * 4];
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) {
      uint8_t *p = data + (y * n + x) * 4;
      p[0] = (uint8_t)(120 + ((x ^ y) & 7) * 16); p[1] = (uint8_t)(90 + ((x + y) & 7) * 12); p[2] = 70; p[3] = 255;
    }
  t3_texture *map = t3_data_texture_new(n, n, data);
  map->mag_filter = map->min_filter = T3_LINEAR;
  map->color_space = T3_SRGB_COLOR_SPACE;
  t3_geometry *geo = t3_sphere_geometry_new(0.85f, 48, 24);
  static const float rough[5] = { 0, 0.25f, 0.5f, 0.75f, 1 };
  for (int i = 0; i < 5; i++) {
    t3_material *a = t3_mesh_physical_material_new(0xc08060);
    a->roughness = 0.9f; a->metalness = 0; a->diffuse_roughness = rough[i];
    t3_object_set_position(add_mesh(&S->base, geo, a), (i - 2) * 2.0f, -1.1f, 0);
    t3_material *b = t3_mesh_physical_material_new(0xffffff);
    t3_material_set_texture(b, T3_MAP, map);
    b->roughness = 0.7f; b->metalness = 0; b->diffuse_roughness = rough[i];
    t3_object_set_position(add_mesh(&S->base, geo, b), (i - 2) * 2.0f, 1.1f, 0);
    t3_release(a); t3_release(b);
  }
  t3_light *amb = t3_ambient_light_new(0x202030, 1);
  t3_object_add(&S->base, amb); t3_release(amb);
  r9_add_light(S, t3_directional_light_new(0xffffff, 3), 6, 1, 1);
  t3_release(geo); t3_release(map);
}
static void eon_frame(void) { t3_renderer_render(R, S, C); }

/* ── u1-uv-channel: texture.channel = 1, the second UV set ──────────── */
static void u1_setup(void) {
  R = make_renderer(0x080810);
  C = t3_perspective_camera_new(45, 1280.0f / 720, 0.1f, 60);
  t3_object_set_position(C, 0, 1.5f, 8);
  t3_object_look_at(C, 0, 0, 0);
  S = t3_scene_new();
  t3_geometry *geo = t3_box_geometry_new(1.6f, 1.6f, 1.6f, 1, 1, 1);
  t3_attribute *uv = geo->attributes[T3_ATTR_UV];
  float *v1 = malloc(sizeof(float) * 2 * (size_t)uv->count);
  const float *v0 = uv->array;
  for (int i = 0; i < uv->count; i++) { v1[i * 2] = v0[i * 2] * 0.25f + 0.6f; v1[i * 2 + 1] = v0[i * 2 + 1] * 0.25f + 0.1f; }
  t3_attribute *a1 = t3_attribute_new(T3_FLOAT32, v1, uv->count, 2);
  t3_geometry_set_attribute(geo, T3_ATTR_UV1, a1);
  t3_release(a1); free(v1);
  t3_texture *a = r10_tex(0), *b = r10_tex(1), *c = r10_tex(2);
  a->channel = b->channel = c->channel = 1;
  t3_material *m[6] = { t3_mesh_standard_material_new(0xffffff), t3_mesh_standard_material_new(0xffffff), t3_mesh_standard_material_new(0xffffff),
                        t3_mesh_standard_material_new(0xffffff), t3_mesh_basic_material_new(0xffffff), t3_mesh_lambert_material_new(0xffffff) };
  t3_material_set_texture(m[0], T3_MAP, a);
  t3_material_set_texture(m[1], T3_AO_MAP, b);
  t3_material_set_texture(m[2], T3_LIGHT_MAP, c);
  t3_material_set_texture(m[3], T3_MAP, a); t3_material_set_texture(m[3], T3_AO_MAP, b); t3_material_set_texture(m[3], T3_LIGHT_MAP, c);
  t3_material_set_texture(m[4], T3_MAP, a);
  t3_material_set_texture(m[5], T3_LIGHT_MAP, c);
  for (int i = 0; i < 6; i++) {
    if (i < 4) m[i]->roughness = 0.6f;
    t3_mesh *o = add_mesh(&S->base, geo, m[i]);
    t3_object_set_position(o, (i % 3 - 1) * 3.0f, i < 3 ? 1.3f : -1.3f, 0);
    t3_object_set_rotation(o, 0.4f, 0.5f, 0);
    t3_release(m[i]);
  }
  t3_light *amb = t3_ambient_light_new(0x808080, 1);
  t3_object_add(&S->base, amb); t3_release(amb);
  r9_add_light(S, t3_directional_light_new(0xffffff, 1.5f), -2, 4, 5);
  t3_release(geo); t3_release(a); t3_release(b); t3_release(c);
}
static void u1_frame(void) { t3_renderer_render(R, S, C); }

/* ── z1-reversed-depth: reversedDepthBuffer ─────────────────────────── */
static t3_mesh *z1_mover;
static void z1_setup(void) {
  R = make_renderer(0x101820);
  t3_renderer_set_reversed_depth(R, true);
  t3_renderer_set_shadow_map(R, true, T3_PCF_SHADOW_MAP);
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 200000);
  t3_object_set_position(C, 0, 3, 9);
  t3_object_look_at(C, 0, 1, 0);
  S = t3_scene_new();
  t3_geometry *pg = t3_plane_geometry_new(30, 30, 1, 1), *bg = t3_box_geometry_new(1.5f, 1.5f, 1.5f, 1, 1, 1), *sg = t3_sphere_geometry_new(0.8f, 32, 16);
  t3_geometry *wg = t3_plane_geometry_new(60000, 30000, 1, 1);
  t3_material *fm = t3_mesh_standard_material_new(0x808890), *bm = t3_mesh_standard_material_new(0xd06030), *sm = t3_mesh_phong_material_new(0x40a0e0);
  t3_mesh *floor = add_mesh(&S->base, pg, fm);
  t3_object_set_rotation(floor, -3.141592653589793f / 2, 0, 0);
  floor->base.receive_shadow = true;
  z1_mover = add_mesh(&S->base, bg, bm);
  t3_object_set_position(z1_mover, -2, 1, 0);
  z1_mover->base.cast_shadow = z1_mover->base.receive_shadow = true;
  t3_mesh *ball = add_mesh(&S->base, sg, sm);
  t3_object_set_position(ball, 2, 1, 0.5f);
  ball->base.cast_shadow = ball->base.receive_shadow = true;
  /* two walls a metre apart 100 km out: a linear 24-bit depth buffer fights here */
    t3_material *w1 = t3_mesh_basic_material_new(0x40c040), *w0 = t3_mesh_basic_material_new(0xc04040);
  t3_mesh *front = add_mesh(&S->base, wg, w1);
  t3_object_set_position(front, 0, 0, -99999);
  t3_mesh *back = add_mesh(&S->base, wg, w0);
  t3_object_set_position(back, 0, 5000, -100000);
  back->base.render_order = 1;   /* drawn after the near wall, whatever the sort */
  t3_light *amb = t3_ambient_light_new(0x303040, 1);
  t3_object_add(&S->base, amb); t3_release(amb);
  t3_light *sun = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(sun, 4, 8, 3);
  t3_object_add(&S->base, sun);
  sun->base.cast_shadow = true;
  sun->shadow->map_width = sun->shadow->map_height = 1024;
  t3_release(sun);
  t3_light *pt = t3_point_light_new(0xffc080, 30, 0, 2);
  t3_object_set_position(pt, 0, 4, 2);
  t3_object_add(&S->base, pt);
  pt->base.cast_shadow = true;
  pt->shadow->map_width = pt->shadow->map_height = 512;
  t3_release(pt);
  t3_release(pg); t3_release(bg); t3_release(sg); t3_release(wg);
  t3_release(fm); t3_release(bm); t3_release(sm); t3_release(w0); t3_release(w1);
}
static void z1_frame(void) {
  frame_no++;
  t3_object_set_rotation(z1_mover, 0, frame_no / 60.0f, 0);
  t3_renderer_render(R, S, C);
}

/* ── z2-reversed-far: reversedDepthBuffer at a far / near of 1e7 ─────────────────────────── */
static t3_mesh *z2_mover;
static void z2_setup(void) {
  R = make_renderer(0x101820);
  t3_renderer_set_reversed_depth(R, true);
  t3_renderer_set_shadow_map(R, true, T3_PCF_SHADOW_MAP);
  C = t3_perspective_camera_new(50, 1280.0f / 720, 1, 10000000);
  t3_object_set_position(C, 0, 3, 9);
  t3_object_look_at(C, 0, 1, 0);
  S = t3_scene_new();
  t3_geometry *pg = t3_plane_geometry_new(30, 30, 1, 1), *bg = t3_box_geometry_new(1.5f, 1.5f, 1.5f, 1, 1, 1), *sg = t3_sphere_geometry_new(0.8f, 32, 16);
  t3_geometry *wg = t3_plane_geometry_new(6000000, 3000000, 1, 1);
  t3_material *fm = t3_mesh_standard_material_new(0x808890), *bm = t3_mesh_standard_material_new(0xd06030), *sm = t3_mesh_phong_material_new(0x40a0e0);
  t3_mesh *floor = add_mesh(&S->base, pg, fm);
  t3_object_set_rotation(floor, -3.141592653589793f / 2, 0, 0);
  floor->base.receive_shadow = true;
  z2_mover = add_mesh(&S->base, bg, bm);
  t3_object_set_position(z2_mover, -2, 1, 0);
  z2_mover->base.cast_shadow = z2_mover->base.receive_shadow = true;
  t3_mesh *ball = add_mesh(&S->base, sg, sm);
  t3_object_set_position(ball, 2, 1, 0.5f);
  ball->base.cast_shadow = ball->base.receive_shadow = true;
  /* two walls a metre apart 100 km out: a linear 24-bit depth buffer fights here */
    t3_material *w1 = t3_mesh_basic_material_new(0x40c040), *w0 = t3_mesh_basic_material_new(0xc04040);
  t3_mesh *front = add_mesh(&S->base, wg, w1);
  t3_object_set_position(front, 0, 0, -9800000);
  t3_mesh *back = add_mesh(&S->base, wg, w0);
  t3_object_set_position(back, 0, 500000, -9900000);
  back->base.render_order = 1;   /* drawn after the near wall, whatever the sort */
  t3_light *amb = t3_ambient_light_new(0x303040, 1);
  t3_object_add(&S->base, amb); t3_release(amb);
  t3_light *sun = t3_directional_light_new(0xffffff, 2);
  t3_object_set_position(sun, 4, 8, 3);
  t3_object_add(&S->base, sun);
  sun->base.cast_shadow = true;
  sun->shadow->map_width = sun->shadow->map_height = 1024;
  t3_release(sun);
  t3_light *pt = t3_point_light_new(0xffc080, 30, 0, 2);
  t3_object_set_position(pt, 0, 4, 2);
  t3_object_add(&S->base, pt);
  pt->base.cast_shadow = true;
  pt->shadow->map_width = pt->shadow->map_height = 512;
  t3_release(pt);
  t3_release(pg); t3_release(bg); t3_release(sg); t3_release(wg);
  t3_release(fm); t3_release(bm); t3_release(sm); t3_release(w0); t3_release(w1);
}
static void z2_frame(void) {
  frame_no++;
  t3_object_set_rotation(z2_mover, 0, frame_no / 60.0f, 0);
  t3_renderer_render(R, S, C);
}

/* ── r12-depth-only: a render target with no colour attachment ────── */
static t3_scene *r12_inner;
static t3_camera *r12_cam;
static t3_render_target *r12_rt;
static t3_mesh *r12_mover;
static void r12_setup(void) {
  R = make_renderer(0x202028);
  r12_inner = t3_scene_new();
  t3_geometry *bg = t3_box_geometry_new(1, 1, 1, 1, 1, 1), *pg = t3_plane_geometry_new(4, 4, 1, 1);
  t3_material *bm = t3_mesh_basic_material_new(0xff0000);
  r12_mover = add_mesh(&r12_inner->base, bg, bm);
  r12_cam = t3_perspective_camera_new(50, 1, 1, 6);
  t3_object_set_position(r12_cam, 0, 0, 3);
  t3_render_target_options o = t3_render_target_options_default();
  t3_texture *depth = t3_depth_texture_new(256, 256, 0);
  o.depth_texture = depth;
  o.depth_only = true;
  r12_rt = t3_render_target_new(256, 256, &o);
  t3_release(depth);
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 50);
  t3_object_set_position(C, 0, 0, 5);
  t3_material *pm = t3_mesh_basic_material_new(0xffffff);
  t3_material_set_texture(pm, T3_MAP, r12_rt->depth_texture);
  add_mesh(&S->base, pg, pm);
  t3_release(bg); t3_release(pg); t3_release(bm); t3_release(pm);
}
static void r12_frame(void) {
  frame_no++;
  t3_object_set_rotation(r12_mover, frame_no / 60.0f, frame_no / 40.0f, 0);
  t3_renderer_set_render_target(R, r12_rt);
  t3_renderer_render(R, r12_inner, r12_cam);
  t3_renderer_set_render_target(R, NULL);
  t3_renderer_render(R, S, C);
}

/* ── r11-pmrem-probe: PMREMGenerator.fromScene's options as a probe
 * refreshed every frame into one target (size 64, a position off the
 * origin, renderTarget); a box orbits inside the captured ring. ─────── */
static t3_scene *r11_capture;
static t3_mesh *r11_orbiter;
static t3_pmrem_generator *r11_pm;
static t3_render_target *r11_probe;
static t3_mesh *r11_spheres[5];
static int r11_reused; /* frames that drew into the previous frame's target */
static double r11_probe_fn(void) { return r11_reused; }
static const t3_vec3 R11_P = { 6, 2, -4 };
static void r11_setup(void) {
  R = make_renderer(0x000000);
  r11_capture = t3_scene_new();
  static const uint32_t colours[6] = { 0xff4040, 0x40ff40, 0x4040ff, 0xffff40, 0xff40ff, 0x40ffff };
  static const float dirs[6][3] = { { 3, 0, 0 }, { -3, 0, 0 }, { 0, 3, 0 }, { 0, -3, 0 }, { 0, 0, 3 }, { 0, 0, -3 } };
  for (int i = 0; i < 6; i++) {
    t3_geometry *bg = t3_box_geometry_new(1.5f, 1.5f, 1.5f, 1, 1, 1);
    t3_material *bm = t3_mesh_basic_material_new(colours[i]);
    t3_mesh *b = add_mesh(&r11_capture->base, bg, bm);
    t3_object_set_position(b, R11_P.x + dirs[i][0], R11_P.y + dirs[i][1], R11_P.z + dirs[i][2]);
    t3_release(bg); t3_release(bm);
  }
  t3_geometry *og = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_material *om = t3_mesh_basic_material_new(0xffffff);
  r11_orbiter = add_mesh(&r11_capture->base, og, om);
  t3_release(og); t3_release(om);
  r11_pm = t3_pmrem_generator_new(R);
  S = t3_scene_new();
  C = t3_perspective_camera_new(45, 1280.0f / 720, 0.1f, 100);
  t3_object_set_position(C, 0, 0, 9);
  t3_object_look_at(C, 0, 0, 0);
  t3_geometry *geo = t3_sphere_geometry_new(0.75f, 48, 24);
  static const float rough[5] = { 0, 0.2f, 0.4f, 0.7f, 1 };
  for (int i = 0; i < 5; i++) {
    t3_material *m = t3_mesh_standard_material_new(0xffffff);
    m->metalness = 1;
    m->roughness = rough[i];
    r11_spheres[i] = add_mesh(&S->base, geo, m);
    t3_object_set_position(r11_spheres[i], (i - 2) * 1.9f, 0, 0);
    t3_release(m);
  }
  t3_release(geo);
}
static void r11_frame(void) {
  frame_no++;
  double t = frame_no / 60.0;
  t3_object_set_position(r11_orbiter, (float)(R11_P.x + cos(t * 2) * 2), (float)(R11_P.y + sin(t * 3)), (float)(R11_P.z + sin(t * 2) * 2));
  t3_render_target *next = t3_pmrem_from_scene_ex(r11_pm, r11_capture, 0, 0.1f, 100,
                                                  &(t3_pmrem_scene_options){ .size = 64, .position = R11_P, .render_target = r11_probe });
  if (r11_probe) r11_reused += next == r11_probe, t3_release(r11_probe);
  r11_probe = next;
  for (int i = 0; i < 5; i++) t3_material_set_texture(r11_spheres[i]->materials[0], T3_ENV_MAP, r11_probe->texture);
  t3_renderer_render(R, S, C);
}
