/* The runtime builder (T3_BUILDER=1) against the checked-in table: for every
 * state of src/gen/programs_{gl,wgpu}.c (or every Nth: argv[1]), the program
 * the embedded QuickJS builds must equal the emitted one field for field.
 * test/run.sh links the tables in under other names (-Dt3_gen_base_gl=...). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gen_program.h"

extern const t3_gen_table full_gl, full_wgpu;

static int diffs;
#define NEQ(what, cond) do { if (cond) { if (diffs++ < 40) printf("  %s: %s differs\n", a->state, what); return; } } while (0)
static bool seq(const char *x, const char *y) { return (!x && !y) || (x && y && !strcmp(x, y)); }
static void cmp(const t3_gen_program *a, const t3_gen_program *b) {
  NEQ("state", !seq(a->state, b->state));
  NEQ("kind", a->kind != b->kind);
  NEQ("features", a->features != b->features);
  NEQ("lights", memcmp(a->lights, b->lights, 4));
  NEQ("extension", !seq(a->extension, b->extension));
  NEQ("tpl", memcmp(a->tpl, b->tpl, sizeof a->tpl));
  NEQ("cast", memcmp(a->cast, b->cast, 3));
  NEQ("vertex", !seq(a->vertex, b->vertex));
  NEQ("fragment", !seq(a->fragment, b->fragment));
  NEQ("n_groups", a->n_groups != b->n_groups);
  for (int i = 0; i < a->n_groups; i++) {
    const t3_bk_group_layout *x = a->groups[i], *y = b->groups[i];
    NEQ("group", !seq(x->name, y->name) || x->index != y->index || x->uniform_bytes != y->uniform_bytes || x->uniform_binding != y->uniform_binding
        || x->n_textures != y->n_textures || x->n_buffers != y->n_buffers);
    for (uint32_t t = 0; t < x->n_textures; t++) {
      const t3_bk_texture_layout *p = &x->textures[t], *q = &y->textures[t];
      NEQ("texture", !seq(p->name, q->name) || p->binding != q->binding || p->sample != q->sample || p->dim != q->dim || p->has_sampler != q->has_sampler
          || p->compare_sampler != q->compare_sampler || p->storage != q->storage || !seq(p->source, q->source) || p->sampler_binding != q->sampler_binding);
    }
    for (uint32_t t = 0; t < x->n_buffers; t++) {
      const t3_bk_buffer_layout *p = &x->buffers[t], *q = &y->buffers[t];
      NEQ("buffer", !seq(p->name, q->name) || p->binding != q->binding || p->bytes != q->bytes || p->storage != q->storage || !seq(p->source, q->source));
    }
  }
  NEQ("n_properties", a->n_properties != b->n_properties);
  for (int i = 0; i < a->n_properties; i++) {
    const t3_gen_property *p = &a->properties[i], *q = &b->properties[i];
    NEQ("property", !seq(p->path, q->path) || p->group != q->group || p->offset != q->offset || p->length != q->length || p->std140_mat3 != q->std140_mat3);
  }
  NEQ("n_attributes", a->n_attributes != b->n_attributes);
  for (int i = 0; i < a->n_attributes; i++) {
    const t3_gen_attribute *p = &a->attributes[i], *q = &b->attributes[i];
    NEQ("attribute", !seq(p->name, q->name) || !seq(p->type, q->type) || p->location != q->location || p->instanced != q->instanced);
  }
  NEQ("n_constants", a->n_constants != b->n_constants);
  for (int i = 0; i < a->n_constants; i++) {
    const t3_gen_constant *p = &a->constants[i], *q = &b->constants[i];
    NEQ("constant", p->group != q->group || p->offset != q->offset || p->length != q->length || memcmp(p->bytes, q->bytes, p->length));
  }
}

int main(int argc, char **argv) {
  int stride = argc > 1 ? atoi(argv[1]) : 1, from = argc > 2 ? atoi(argv[2]) : 0, n = 0, failed = 0;
  if (stride < 1) stride = 1;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  const t3_gen_table *tabs[2] = { &full_gl, &full_wgpu };
  for (int be = 0; be < 2; be++)
    for (unsigned i = (unsigned)from; i < tabs[be]->count; i += (unsigned)stride) {
      const t3_gen_program *want = &tabs[be]->programs[i];
      if (want->extension) continue;   /* (generator extensions are offline only) */
      char err[2048];
      const t3_gen_program *got = t3_builder_build(want->state, be ? T3_GEN_WGPU : T3_GEN_GL, err, sizeof err);
      n++;
      if (!got) { printf("  %s.%s: build failed: %.600s\n", want->state, be ? "wgpu" : "gl", err); failed++; continue; }
      int before = diffs;
      cmp(got, want);
      if (diffs != before) { printf("  ^ %s\n", be ? "wgpu" : "gl"); failed++; }
    }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double s = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
  printf("%d programs built, %d differ or failed, %.0f ms each\n", n, failed, n ? s * 1000 / n : 0);
  return failed != 0 || n == 0;
}
