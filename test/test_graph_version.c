/* t3_graph_version: it moves on every add, remove and reparent (in any
 * scene) and on the layers setter, and stays put while objects only move or
 * change visibility, so a cache of a scene's lights or meshes keyed on it is
 * rebuilt exactly when it has to be. The control (T3_TEST_CONTROL=1) expects
 * the version to stay put across an add, which must fail. No GL. */
#include <stdio.h>
#include <stdlib.h>

#include "three.h"

static int fails;
static void check(int ok, const char *what) {
  if (!ok) {
    printf("test_graph_version: FAIL %s\n", what);
    fails++;
  }
}

int main(void) {
  int control = getenv("T3_TEST_CONTROL") != NULL;
  t3_scene *s = t3_scene_new();
  t3_object *group = (t3_object *)t3_group_new();
  t3_light *l = t3_point_light_new(0xffffff, 1, 0, 2);
  uint32_t v = t3_graph_version();
  t3_object_add(&s->base, group);
  if (control) check(t3_graph_version() == v, "control: unchanged across an add");
  else check(t3_graph_version() != v, "an add moves it");
  v = t3_graph_version();
  t3_object_add(group, l);
  check(t3_graph_version() != v, "an add under a child moves it");
  v = t3_graph_version();
  t3_object_add(&s->base, l); /* reparent: from the group to the scene */
  check(t3_graph_version() != v, "a reparent moves it");
  v = t3_graph_version();
  t3_object_set_position(l, 1, 2, 3);
  l->base.visible = false;
  t3_object_update_matrix_world(s, true);
  check(t3_graph_version() == v, "moving, hiding and a matrix update leave it");
  t3_object_set_layers(l, 2);
  check(t3_graph_version() != v, "the layers setter moves it");
  v = t3_graph_version();
  t3_object_remove(&s->base, l);
  check(t3_graph_version() != v, "a remove moves it");
  /* another scene's change moves it too (one counter for every scene) */
  t3_scene *s2 = t3_scene_new();
  v = t3_graph_version();
  t3_object_add(&s2->base, l);
  check(t3_graph_version() != v, "a change in another scene moves it");
  t3_release(l);
  t3_release(group);
  t3_release(s);
  t3_release(s2);
  if (!fails) puts("test_graph_version: ok");
  return fails ? 1 : 0;
}
