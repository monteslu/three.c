/* Real renderer sort/cache helpers without a GPU. Geometry far from its
 * object origin must sort by bounds only when explicitly enabled and opaque. */
#include <assert.h>
#include "../src/renderer.c"
static void eq(float a,float b) { if(fabsf(a-b)>=.0001f) fprintf(stderr,"depth got %g, expected %g\n",a,b); assert(fabsf(a-b)<.0001f); }
int main(void) {
  t3_renderer r={0};t3_mat4_identity(&r.proj_screen);
  t3_geometry *g=t3_box_geometry_new(2,2,2,1,1,1);
  t3_geometry_translate(g,0,0,20);
  t3_material *mat=t3_mesh_basic_material_new(0xffffff);
  t3_mesh *mesh=t3_mesh_new(g,mat);
  t3_object_set_position(mesh,0,0,3);
  t3_object_update_matrix_world(mesh,false);
  eq(item_z(&r,&mesh->base,g,true),3);
  t3_renderer_set_sort_bounds(&r,true);
  eq(item_z(&r,&mesh->base,g,true),23);
  eq(item_z(&r,&mesh->base,g,false),3);
  t3_geometry_translate(g,0,0,5);
  eq(item_z(&r,&mesh->base,g,true),28);
  t3_object_set_position(mesh,0,0,7);
  t3_object_update_matrix_world(mesh,false);
  eq(item_z(&r,&mesh->base,g,true),32);
  r.proj_screen.e[10]=.5f;
  eq(item_z(&r,&mesh->base,g,true),16);
  t3_mat4_identity(&r.proj_screen);
  t3_object skinned={0};skinned.type=T3_SKINNED_MESH;
  t3_mat4_identity(&skinned.matrix_world);skinned.matrix_world.e[14]=9;
  eq(item_z(&r,&skinned,g,true),9);
  push_mesh_item(&r,&mesh->base,g,mat,NULL,false);
  assert(r.opaque.n==1);eq(r.opaque.items[0].z,32);
  mat->transparent=true;
  push_mesh_item(&r,&mesh->base,g,mat,NULL,false);
  assert(r.transparent.n==1);eq(r.transparent.items[0].z,7);
  mat->transparent=false;
  free(r.opaque.items);free(r.transparent.items);
  t3_instanced_mesh *im=t3_instanced_mesh_new(g,mat,1);
  t3_mat4 transform;t3_mat4_identity(&transform);transform.e[14]=8;
  t3_instanced_mesh_set_matrix_at(im,0,&transform);
  t3_object_update_matrix_world(im,false);
  eq(item_z(&r,&im->mesh.base,g,true),33);
  transform.e[14]=12;t3_instanced_mesh_set_matrix_at(im,0,&transform);
  im->instance_matrix->version++; /* publish the edited instance buffer */
  eq(item_z(&r,&im->mesh.base,g,true),37);
  eq(item_z(&r,&im->mesh.base,g,false),0);
  render_item near={.object=&mesh->base,.z=1,.render_order=2};
  render_item far={.object=&im->mesh.base,.z=10,.render_order=1};
  assert(painter_sort(&near,&far)>0); /* explicit order wins over depth */
  near.render_order=1;assert(painter_sort(&near,&far)<0);
  assert(reverse_painter_sort(&near,&far)>0);
  t3_scene *scene=t3_scene_new();t3_object_add(&scene->base,mesh);
  t3_object_update_matrix_world(scene,false);
  assert(flatten_scene(&scene->base)->n==2);
  t3_object_add(&scene->base,im);
  assert(flatten_scene(&scene->base)->n==3);
  t3_object_remove(&scene->base,mesh);
  scene_flat *flat=flatten_scene(&scene->base);
  assert(flat->n==2 && flat->obj[1]==&im->mesh.base);
  t3_release(scene);t3_release(mesh);t3_release(im);t3_release(g);t3_release(mat);
  puts("test_scene_sort: bounds, origins, transparency, instance updates, explicit order and scene membership pass");
}
