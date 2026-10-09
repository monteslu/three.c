/* Perf scenes: what the ported compare scenes do not stress. Not ports of a
 * three.js scene (no parity image); bench/perf.mjs runs them on both
 * backends and compares with a baseline. Included by scenes.c.
 *
 *   c1-material-churn   a new material state every PF_EVERY frames, as a game
 *                       meeting materials mid-play: run with --mode hitch, the
 *                       tagged frames are the first-use (compile) stalls
 *   c2-compile-zoo      every state of c1 at once: --mode firstuse, the first
 *                       frame is the whole compile
 *   d1-unique-materials PF_N boxes, one program, a material each (uniforms)
 *   d2-unique-textures  PF_N boxes, one program, a texture each (bindings)
 *   d3-program-mix      PF_N boxes over PF_TYPES programs, a material each */

/* the frame introduced a material state (native_main.c --mode hitch) */
int bench_frame_tag;

#define PF_N 1000
#define PF_EVERY 10

typedef struct { t3_material_type type; const char *variant; } pf_state;
/* simple mesh states the full table has (see tools/gen-states.txt), no lights */
static const pf_state pf_states[] = {
  { T3_MESH_BASIC_MATERIAL, "" }, { T3_MESH_BASIC_MATERIAL, "map" }, { T3_MESH_BASIC_MATERIAL, "ds" },
  { T3_MESH_BASIC_MATERIAL, "trans" }, { T3_MESH_BASIC_MATERIAL, "vcol" },
  { T3_MESH_LAMBERT_MATERIAL, "" }, { T3_MESH_LAMBERT_MATERIAL, "map" }, { T3_MESH_LAMBERT_MATERIAL, "ds" },
  { T3_MESH_LAMBERT_MATERIAL, "trans" }, { T3_MESH_LAMBERT_MATERIAL, "vcol" }, { T3_MESH_LAMBERT_MATERIAL, "flat" },
  { T3_MESH_PHONG_MATERIAL, "" }, { T3_MESH_PHONG_MATERIAL, "map" }, { T3_MESH_PHONG_MATERIAL, "ds" },
  { T3_MESH_PHONG_MATERIAL, "trans" }, { T3_MESH_PHONG_MATERIAL, "vcol" }, { T3_MESH_PHONG_MATERIAL, "flat" },
  { T3_MESH_STANDARD_MATERIAL, "" }, { T3_MESH_STANDARD_MATERIAL, "map" }, { T3_MESH_STANDARD_MATERIAL, "ds" },
  { T3_MESH_STANDARD_MATERIAL, "trans" }, { T3_MESH_STANDARD_MATERIAL, "vcol" }, { T3_MESH_STANDARD_MATERIAL, "flat" },
  { T3_MESH_PHYSICAL_MATERIAL, "" }, { T3_MESH_PHYSICAL_MATERIAL, "map" }, { T3_MESH_PHYSICAL_MATERIAL, "ds" },
  { T3_MESH_PHYSICAL_MATERIAL, "trans" }, { T3_MESH_PHYSICAL_MATERIAL, "vcol" }, { T3_MESH_PHYSICAL_MATERIAL, "flat" },
  { T3_MESH_PHYSICAL_MATERIAL, "cc" }, { T3_MESH_PHYSICAL_MATERIAL, "sheen" }, { T3_MESH_PHYSICAL_MATERIAL, "irid" },
  { T3_MESH_PHYSICAL_MATERIAL, "aniso" }, { T3_MESH_PHYSICAL_MATERIAL, "transm" },
  { T3_MESH_NORMAL_MATERIAL, "" }, { T3_MESH_NORMAL_MATERIAL, "ds" }, { T3_MESH_NORMAL_MATERIAL, "flat" },
  { T3_MESH_NORMAL_MATERIAL, "trans" },
};
#define PF_STATES ((int)(sizeof pf_states / sizeof pf_states[0]))

static t3_texture *pf_checker(uint32_t a, uint32_t b, int n) {
  uint8_t *px = malloc((size_t)n * n * 4);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) {
      uint32_t c = ((x / 8 + y / 8) & 1) ? a : b;
      uint8_t *p = px + ((size_t)y * n + x) * 4;
      p[0] = c >> 16; p[1] = c >> 8; p[2] = c; p[3] = 255;
    }
  t3_texture *t = t3_texture_new(n, n, px);
  free(px);
  return t;
}

static t3_texture *pf_tex;
static t3_material *pf_material(int i) {
  const pf_state *s = &pf_states[i];
  t3_material *m = t3_material_new(s->type);
  m->color = t3_color_hex(0x406080u + (uint32_t)i * 0x050307u);
  const char *v = s->variant;
  if (!strcmp(v, "map")) t3_material_set_map(m, pf_tex);
  else if (!strcmp(v, "ds")) m->side = T3_DOUBLE_SIDE;
  else if (!strcmp(v, "trans")) { m->transparent = true; m->opacity = 0.7f; }
  else if (!strcmp(v, "vcol")) m->vertex_colors = true;
  else if (!strcmp(v, "flat")) m->flat_shading = true;
  else if (!strcmp(v, "cc")) m->clearcoat = 1;
  else if (!strcmp(v, "sheen")) { m->sheen = 1; m->sheen_color = t3_color_hex(0xff8040); }
  else if (!strcmp(v, "irid")) m->iridescence = 1;
  else if (!strcmp(v, "aniso")) m->anisotropy = 0.5f;
  else if (!strcmp(v, "transm")) { m->transmission = 1; m->thickness = 0.5f; }
  return m;
}

static t3_geometry *pf_geo;
/* a sphere with an rgb color attribute, for the vcol states */
static t3_geometry *pf_sphere(void) {
  t3_geometry *g = t3_sphere_geometry_new(0.6f, 24, 16);
  int n = g->attributes[T3_ATTR_POSITION]->count;
  t3_attribute *col = t3_attribute_new(T3_FLOAT32, NULL, n, 3);
  float *c = col->array;
  for (int i = 0; i < n; i++) { c[i * 3] = 1; c[i * 3 + 1] = (float)(i % 7) / 6; c[i * 3 + 2] = 0.5f; }
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, col);
  t3_release(col);
  return g;
}
static void pf_scene(float camz) {
  S = t3_scene_new();
  C = t3_perspective_camera_new(50, 1280.0f / 720, 0.1f, 200);
  t3_object_set_position(C, 0, 0, camz);
  t3_object_look_at(C, 0, 0, 0);
  R = make_renderer(0x101018);
  pf_tex = pf_checker(0xffffff, 0x3060a0, 64);
}
/* the state's mesh at slot i of an 8-column grid */
static void pf_add(int i) {
  t3_material *m = pf_material(i);
  t3_mesh *mesh = add_mesh(&S->base, pf_geo, m);
  t3_object_set_position(mesh, (float)(i % 8) * 1.4f - 4.9f, 2.8f - (float)(i / 8) * 1.4f, 0);
  t3_release(m);
}

/* ── c1-material-churn / c2-compile-zoo ──────────────────────────── */
static int pf_added, pf_frame;
static void c1_setup(void) {
  pf_scene(12);
  pf_geo = pf_sphere();
  pf_add(0);
  pf_added = 1;
  pf_frame = 0;
}
static void c1_frame(void) {
  bench_frame_tag = 0;
  if (++pf_frame % PF_EVERY == 0 && pf_added < PF_STATES) { pf_add(pf_added++); bench_frame_tag = 1; }
  t3_renderer_render(R, S, C);
}
static double c1_probe(void) { return pf_added; }
static void c2_setup(void) {
  pf_scene(12);
  pf_geo = pf_sphere();
  for (int i = 0; i < PF_STATES; i++) pf_add(i);
  pf_added = PF_STATES;
}

/* ── d1 / d2 / d3: PF_N boxes on a 40 x 25 grid ───────────────────── */
static void pf_grid(int kind) {
  pf_scene(34);
  t3_renderer_set_auto_instancing(R, false);
  t3_geometry *box = t3_box_geometry_new(0.6f, 0.6f, 0.6f, 1, 1, 1);
  static const t3_material_type types[] = { T3_MESH_BASIC_MATERIAL, T3_MESH_LAMBERT_MATERIAL, T3_MESH_PHONG_MATERIAL,
                                            T3_MESH_STANDARD_MATERIAL, T3_MESH_NORMAL_MATERIAL, T3_MESH_PHYSICAL_MATERIAL };
  for (int i = 0; i < PF_N; i++) {
    t3_material *m;
    if (kind == 3) m = t3_material_new(types[i % 6]);
    else m = t3_mesh_basic_material_new(0);
    m->color = t3_color_hex(0x203040u + (uint32_t)i * 0x0a0805u);
    t3_texture *t = NULL;
    if (kind == 2) { t = pf_checker(0xffffff, 0x203040u + (uint32_t)i * 0x0a0805u, 16); t3_material_set_map(m, t); }
    t3_mesh *mesh = add_mesh(&S->base, box, m);
    t3_object_set_position(mesh, (float)(i % 40) - 19.5f, (float)(i / 40) - 12.0f, 0);
    t3_object_set_rotation(mesh, 0.5f, 0.7f, 0);
    t3_release(m);
    if (t) t3_release(t);
  }
  t3_release(box);
  if (kind == 3) {   /* the lit types see something */
    add_light(t3_ambient_light_new(0x404040, 1));
    t3_light *l = t3_directional_light_new(0xffffff, 1);
    t3_object_set_position(l, 3, 4, 10);
    add_light(l);
  }
}
static void d1_setup(void) { pf_grid(1); }
static void d2_setup(void) { pf_grid(2); }
static void d3_setup(void) { pf_grid(3); }
