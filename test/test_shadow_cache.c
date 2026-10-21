/* Exercise the actual private cache without a GPU. Scene changes must not
 * exhaust its 16 entries, evict another current caster, or reuse stale maps
 * when a new light occupies an old address. Link with --gc-sections. */
#include <assert.h>
#include "../src/renderer.c"

int main(void) {
  t3_renderer r = {0};
  t3_light *casters[2];
  r.casters = casters;
  t3_light lights[40] = {0};
  for (int i = 0; i < 40; i++) lights[i].base.id = (uint32_t)i + 1;
  r.casters[0] = &lights[0];
  r.caster_count = 1;
  struct gen_shadow *first = gen_shadow_of(&r, &lights[0], true);
  assert(first);
  first->tex = 123;
  first->rendered = true;
  for (int i = 1; i < 40; i++) {
    r.casters[0] = &lights[i];
    struct gen_shadow *s = gen_shadow_of(&r, &lights[i], true);
    assert(s == first && s->tex == 123 && !s->rendered);
    s->rendered = true;
  }
  assert(r.gen_sh_n == 1);
  r.casters[1] = &lights[0];
  r.caster_count = 2;
  struct gen_shadow *other = gen_shadow_of(&r, &lights[0], true);
  assert(other && other != first);
  assert(gen_shadow_of(&r, &lights[39], false) == first);
  /* Same pointer, new object identity: force a fresh shadow rendering. */
  lights[39].base.id = 1000;
  assert(!gen_shadow_of(&r, &lights[39], false));
  assert(gen_shadow_of(&r, &lights[39], true) == first);
  assert(!first->rendered && gen_shadow_of(&r, &lights[0], false) == other);
  puts("test_shadow_cache: 40 scene changes, live casters and address reuse pass");
  return 0;
}
