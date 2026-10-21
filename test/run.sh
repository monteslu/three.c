#!/usr/bin/env bash
# Build and run three.c's C tests (after ./build.sh). GPU tests run headless
# on the EGL surfaceless platform.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
CC="${CC:-cc}"
mkdir -p build/test
fail=0
run() { local name=$1; shift; if "$@"; then echo "ok    $name"; else echo "FAIL  $name"; fail=1; fi; }
"$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_math.c build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o build/test/test_math
run test_math ./build/test/test_math
"$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections -I include -I src \
  test/test_shadow_cache.c -Wl,--gc-sections -lGLESv2 -lm -o build/test/test_shadow_cache
run test_shadow_cache ./build/test/test_shadow_cache
"$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections -I include -I src test/test_instance_stream.c -Wl,--gc-sections -lGLESv2 -lm -o build/test/test_instance_stream
run test_instance_stream ./build/test/test_instance_stream
"$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections -I include -I src test/test_prepass_mask.c -Wl,--gc-sections -lGLESv2 -lm -o build/test/test_prepass_mask
run test_prepass_mask ./build/test/test_prepass_mask
"$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections -I include -I src \
  test/test_scene_sort.c build/native/libthree.a -Wl,--gc-sections -lGLESv2 -lm -o build/test/test_scene_sort
run test_scene_sort ./build/test/test_scene_sort
"$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_attr_locations.c build/native/libthree.a -Wl,--allow-multiple-definition -lEGL -lGLESv2 -lz -lm -o build/test/test_attr_locations
run test_attr_locations ./build/test/test_attr_locations
"$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include test/test_no_gl.c build/native/libthree.a -lz -lm -o build/test/test_no_gl
run test_no_gl ./build/test/test_no_gl
"$CC" -std=c99 -g -fsanitize=address -D_POSIX_C_SOURCE=200809L -I include -I src test/test_free.c \
  src/{math,core,geometry,curves,animation,raycaster,loaders,gltf,renderer,backend_gles,gen_program}.c \
  src/gen/programs_gl.c src/gen/programs_dfg.c -w -lGLESv2 -lm -o build/test/test_free
run test_free ./build/test/test_free
for t in test_backend_seam test_shared_geometry test_instance_color test_gl_transmission_fbo test_extension; do
  "$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src "test/$t.c" build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o "build/test/$t"
  run "$t" env EGL_PLATFORM=surfaceless "./build/test/$t"
done
# extension materials auto-instanced (t3_renderer_set_extension_batching) and a texture sampled 24
# times: a table of the tests' own (the tint example, plain and instanced; many), captured on the mock GPU
mkdir -p build/test/exttab
printf 'standard+map+l1000+xtint\nstandard+map+instbig+l1000+xtint\nstandard+xmany\n' > build/test/exttab/states.txt
if node tools/gen-project-table.mjs --name exttab --out build/test/exttab --states build/test/exttab/states.txt \
     --cache build/test/exttab/cache --extend tools/extensions/tint.mjs --extend test/extensions/many.mjs >/dev/null 2>&1; then
  "$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_extension_batch.c build/test/exttab/exttab_gl.c \
    build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o build/test/test_extension_batch
  run test_extension_batch env EGL_PLATFORM=surfaceless ./build/test/test_extension_batch
  # an extension texture sampled 24 times (a splat terrain's layer arrays) still draws on GLES
  "$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_many_samples.c build/test/exttab/exttab_gl.c \
    build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o build/test/test_many_samples
  run test_many_samples env EGL_PLATFORM=surfaceless ./build/test/test_many_samples
else
  echo "FAIL  test_extension_batch (gen-project-table)"; fail=1
fi
# a texture sampled twice has a flipY flag per sample, all in the property map (mock GPU)
run test_flip_copies node test/test_flip_copies.mjs
# a project's own program table with three.c's core table: the same picture as the full table
if [ -d build/gen-programs ] && ls build/gen-programs/standard+map+nearest.gl.json >/dev/null 2>&1; then
  T3_TABLE=core ./build.sh >/dev/null 2>&1
  printf 'standard+map+nearest\n' > build/test/testproj-states.txt
  node tools/gen-project-table.mjs --name testproj --out build/test/testproj --states build/test/testproj-states.txt --cache build/gen-programs >/dev/null
  P="-std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_project_table.c"
  L="-lEGL -lGLESv2 -lz -lm"
  "$CC" $P -DFULL build/native/libthree.a $L -o build/test/test_project_full
  "$CC" $P -DPROJECT build/test/testproj/testproj_gl.c build/native_core/libthree.a $L -o build/test/test_project_proj
  "$CC" $P build/native_core/libthree.a $L -o build/test/test_project_core
  ok=1
  EGL_PLATFORM=surfaceless ./build/test/test_project_full full >/dev/null || ok=0
  EGL_PLATFORM=surfaceless ./build/test/test_project_proj project >/dev/null || ok=0
  EGL_PLATFORM=surfaceless ./build/test/test_project_core core >/dev/null || ok=0   # the control: must report missing
  cmp -s build/test/project-full.rgba build/test/project-project.rgba || ok=0
  cmp -s build/test/project-full.rgba build/test/project-core.rgba && ok=0         # the control must differ
  if [ $ok = 1 ]; then echo "ok    test_project_table"; else echo "FAIL  test_project_table"; fail=1; fi
else
  echo "skip  test_project_table (no captures in build/gen-programs: node tools/gen-programs.mjs)"
fi
# the runtime builder (after T3_BUILDER=1 T3_TABLE=core ./build.sh): the core
# table draws a material state it lacks by building it on first use in the
# embedded QuickJS, the same picture as the full table; and a sample of the
# full table's states built that way equals the emitted programs field for field
if [ -f build/native_core-builder/libthree.a ]; then
  P="-std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src test/test_project_table.c"
  L="-lEGL -lGLESv2 -lz -lm -ldl"
  "$CC" $P -DFULL build/native/libthree.a $L -o build/test/test_project_full
  "$CC" $P -DBUILDER build/native_core-builder/libthree.a $L -o build/test/test_project_builder
  ok=1
  EGL_PLATFORM=surfaceless ./build/test/test_project_full full >/dev/null || ok=0
  EGL_PLATFORM=surfaceless ./build/test/test_project_builder builder >/dev/null || ok=0
  cmp -s build/test/project-full.rgba build/test/project-builder.rgba || ok=0
  if [ $ok = 1 ]; then echo "ok    test_builder_draw"; else echo "FAIL  test_builder_draw"; fail=1; fi
  B=build/test/parity
  mkdir -p $B
  "$CC" -O1 -w -I include -I src -Dt3_gen_base_gl=full_gl -c src/gen/programs_gl.c -o $B/full_gl.o
  "$CC" -O1 -w -I include -I src -Dt3_gen_base_wgpu=full_wgpu -c src/gen/programs_wgpu.c -o $B/full_wgpu.o
  "$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -DT3_BUILDER -I include -I src test/test_builder_parity.c $B/full_gl.o $B/full_wgpu.o \
    build/native_core-builder/libthree.a $L -o $B/test_builder_parity
  run test_builder_parity "./$B/test_builder_parity" "${T3_PARITY_STRIDE:-40}"
else
  echo "skip  test_builder_draw, test_builder_parity (T3_BUILDER=1 T3_TABLE=core ./build.sh)"
fi
# WebGPU (after WGPU=1 ./build.sh, with native-dawn): exit 77 = no adapter, skipped
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) PLAT=linux-x64 ;; Linux-aarch64) PLAT=linux-arm64 ;; Darwin-arm64) PLAT=darwin-arm64 ;; Darwin-x86_64) PLAT=darwin-x64 ;; *) PLAT=unknown ;;
esac
ND="${NATIVE_DAWN:-$ROOT/node_modules/native-dawn/dist/$PLAT}"
if [ -f build/native-wgpu/libthree.a ] && [ -f "$ND/include/webgpu/webgpu.h" ]; then
  "$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -DT3_WGPU -I include -I src -I "$ND/include" test/test_wgpu_external.c \
    build/native-wgpu/libthree.a -L "$ND/lib" -lwebgpu_dawn -Wl,-rpath,"$ND/lib" -lEGL -lGLESv2 -lz -lm -o build/test/test_wgpu_external
  set +e; ./build/test/test_wgpu_external > build/test/test_wgpu_external.log 2>&1; st=$?; set -e
  if [ $st = 0 ]; then echo "ok    test_wgpu_external"; elif [ $st = 77 ]; then echo "skip  test_wgpu_external (no WebGPU adapter)"
  else echo "FAIL  test_wgpu_external"; grep -v '^Warning' build/test/test_wgpu_external.log; fail=1; fi
  # an extension program filled at run time, on WebGPU (the GLES run is in the loop above)
  "$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -DT3_WGPU -I include -I src -I "$ND/include" test/test_extension.c \
    build/native-wgpu/libthree.a -L "$ND/lib" -lwebgpu_dawn -Wl,-rpath,"$ND/lib" -lEGL -lGLESv2 -lz -lm -o build/test/test_extension_wgpu
  set +e; ./build/test/test_extension_wgpu > build/test/test_extension_wgpu.log 2>&1; st=$?; set -e
  if [ $st = 0 ]; then echo "ok    test_extension_wgpu"; elif [ $st = 77 ]; then echo "skip  test_extension_wgpu (no WebGPU adapter)"
  else echo "FAIL  test_extension_wgpu"; grep -v '^Warning' build/test/test_extension_wgpu.log; fail=1; fi
  if [ -f build/test/exttab/exttab_wgpu.c ]; then
    "$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -DT3_WGPU -I include -I src -I "$ND/include" test/test_extension_batch.c \
      build/test/exttab/exttab_gl.c build/test/exttab/exttab_wgpu.c build/native-wgpu/libthree.a -L "$ND/lib" -lwebgpu_dawn \
      -Wl,-rpath,"$ND/lib" -lEGL -lGLESv2 -lz -lm -o build/test/test_extension_batch_wgpu
    set +e; ./build/test/test_extension_batch_wgpu > build/test/test_extension_batch_wgpu.log 2>&1; st=$?; set -e
    if [ $st = 0 ]; then echo "ok    test_extension_batch_wgpu"; elif [ $st = 77 ]; then echo "skip  test_extension_batch_wgpu (no WebGPU adapter)"
    else echo "FAIL  test_extension_batch_wgpu"; grep -v '^Warning' build/test/test_extension_batch_wgpu.log; fail=1; fi
  fi
  # ... and on a compatibility-mode device (as wasmcart gives carts): a texture binds only with the view dimension it was made with
  set +e; ./build/test/test_extension_wgpu compat > build/test/test_extension_wgpu_compat.log 2>&1; st=$?; set -e
  if [ $st = 0 ]; then echo "ok    test_extension_wgpu_compat"; elif [ $st = 77 ]; then echo "skip  test_extension_wgpu_compat (no WebGPU adapter)"
  else echo "FAIL  test_extension_wgpu_compat"; grep -v '^Warning' build/test/test_extension_wgpu_compat.log; fail=1; fi
else
  echo "skip  test_wgpu_external, test_extension_wgpu (no WGPU=1 build or no native-dawn)"
fi
exit $fail
