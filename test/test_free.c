/* Releasing every mesh kind frees exactly what it owns. Build it with
 * AddressSanitizer (no GL needed: nothing here is drawn):
 *
 *   cc -std=c99 -g -fsanitize=address -D_POSIX_C_SOURCE=200809L -I include -I src \
 *     test/test_free.c src/{math,core,geometry,curves,animation,raycaster,loaders,gltf,renderer,backend_gles,gen_program}.c \
 *     src/gen/programs_gl.c src/gen/programs_dfg.c -lGLESv2 -lm -o build/test_free && ./build/test_free
 *
 * A SkinnedMesh once fell through into the InstancedMesh case of the object
 * free path and released fields an InstancedMesh has and it does not. */
#include <stdio.h>

#include "three.h"

int main(void) {
  for (int round = 0; round < 3; round++) {
    t3_geometry *g = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
    t3_material *m = t3_mesh_standard_material_new(0xffffff);
    t3_object *bones[2] = { t3_bone_new(), t3_bone_new() };
    t3_object_add(bones[0], bones[1]);
    t3_skeleton *sk = t3_skeleton_new(bones, 2, NULL);
    t3_scene *s = t3_scene_new();
    for (int i = 0; i < 4; i++) {
      t3_skinned_mesh *sm = t3_skinned_mesh_new(g, m);
      t3_skinned_mesh_bind(sm, sk, NULL);
      t3_object_add(&s->base, sm);
      t3_release(sm);
      t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, 8);
      t3_instanced_mesh_set_color_at(im, 3, t3_color_hex(0xff0000));
      t3_object_add(&s->base, im);
      t3_release(im);
      t3_mesh *plain = t3_mesh_new(g, m);
      t3_object_add(&s->base, plain);
      t3_release(plain);
    }
    t3_release(sk);
    t3_release(bones[0]);
    t3_release(bones[1]);
    t3_release(s);
    t3_release(g);
    t3_release(m);
  }
  puts("test_free: ok");
  return 0;
}
