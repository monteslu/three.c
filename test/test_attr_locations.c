/* Every vertex attribute name r186's generated programs use gets one of
 * three.c's fixed locations (gen_program.c fixed_location, renumber_attributes).
 * A name with no fixed place must be refused, which is the control. */
#include "../src/gen_program.c"

static int check(const char *name, int want_ok) {
  char src[256], err[256] = "";
  snprintf(src, sizeof src, "#version 300 es\nlayout( location = 9 ) in vec2 %s;\nvoid main() {}\n", name);
  char *out = renumber_attributes(src, "test", err, sizeof err);
  int ok = out != NULL;
  if (ok != want_ok) {
    fprintf(stderr, "test_attr_locations: '%s' %s (%s)\n", name, ok ? "was given a location" : "has no fixed location", err);
    free(out);
    return 1;
  }
  free(out);
  return 0;
}

int main(void) {
  static const char *names[] = { "position", "normal", "uv", "uv1", "color", "tangent", "skinIndex",
                                 "skinWeight", "instanceColor", "nodeAttribute0", "nodeAttribute3" };
  int fails = 0;
  for (size_t i = 0; i < sizeof names / sizeof *names; i++) fails += check(names[i], 1);
  fails += check("uv7", 0); /* control: a name three.c has no slot for is refused */
  if (fails) return 1;
  printf("test_attr_locations: ok\n");
  return 0;
}
