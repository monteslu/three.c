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

