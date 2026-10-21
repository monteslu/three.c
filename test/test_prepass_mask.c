/* Material changes must never re-enable colour writes during a prepass. */
#include <assert.h>
#include "../src/renderer.c"
#undef calloc
void *t3__calloc(size_t n,size_t size) { return calloc(n,size); }
void t3__fatal(const char *what) {(void)what;abort();}
static int writes=-1,calls;
void glColorMask(GLboolean r,GLboolean g,GLboolean b,GLboolean a) {
  assert(r==g&&g==b&&b==a);writes=r;calls++;
}
int main(void) {
  t3_renderer r={0};
  r.cur_cull=1;r.cur_depth_test=4;r.cur_depth_write=1;r.cur_color_mask=-1;
  t3_material m={0};m.side=T3_FRONT_SIDE;m.depth_test=true;m.depth_func=3;
  m.depth_write=true;m.color_write=true;
  set_material_state(&r,&m);assert(writes==1&&calls==1);
  r.depth_prepass=true;
  set_material_state(&r,&m);assert(writes==0&&calls==2);
  m.color_write=false;set_material_state(&r,&m);
  m.color_write=true;set_material_state(&r,&m);assert(writes==0&&calls==2);
  r.depth_prepass=false;
  set_material_state(&r,&m);assert(writes==1&&calls==3);
  m.color_write=false;set_material_state(&r,&m);assert(writes==0&&calls==4);
  return 0;
}
