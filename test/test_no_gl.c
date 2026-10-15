/* A tool that builds scenes without drawing them (an asset packer, say) links
 * libthree.a with no GL library: nothing it calls may pull in the renderer.
 * Run by test/run.sh, linked with -lm -lz only. */
#include <stdio.h>

#include "three.h"

int main(void) {
  t3_scene *s = t3_scene_new();
  t3_geometry *g = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  int ok = t3_material_set_extension(m, "tint");
  t3_mesh *mesh = t3_mesh_new(g, m);
  t3_object_add(&s->base, mesh);
  t3_release(mesh); t3_release(m); t3_release(g); t3_release(s);
  if (ok) puts("test_no_gl: ok");
  return ok ? 0 : 1;
}
