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
"$CC" -std=c99 -g -fsanitize=address -D_POSIX_C_SOURCE=200809L -I include -I src test/test_free.c \
  src/{math,core,geometry,curves,animation,raycaster,loaders,gltf,renderer,backend_gles,gen_program}.c \
  src/gen/programs_gl.c src/gen/programs_dfg.c -w -lGLESv2 -lm -o build/test/test_free
run test_free ./build/test/test_free
for t in test_backend_seam test_shared_geometry test_instance_color test_gl_transmission_fbo; do
  "$CC" -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -I include -I src "test/$t.c" build/native/libthree.a -lEGL -lGLESv2 -lz -lm -o "build/test/$t"
  run "$t" env EGL_PLATFORM=surfaceless "./build/test/$t"
done
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
else
  echo "skip  test_wgpu_external (no WGPU=1 build or no native-dawn)"
fi
exit $fail
