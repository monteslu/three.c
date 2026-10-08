#!/usr/bin/env bash
# wasmcart carts: three.c + a bench scene (+ Box2D / Box3D), standalone wasm
# with GL imported from the cart ABI's "gl" module. Needs emcc, node and a
# wasmcart checkout (WASMCART, default ../wasmcart).
#
#   wasm/build.sh                 # carts for 01-cubes, 05-heavy, 06-heavy-instanced, physics3d, physics2d
#   SCENES="t3-shadow-dir g2-cesium-man" wasm/build.sh
#   WGPU=1 wasm/build.sh          # dual carts: GL and WebGPU (tools/fetch-emdawnwebgpu.sh first)
#   SIMD=0 wasm/build.sh          # scalar wasm (no simd128): SIMD is on by default
#   PROFILE=1 wasm/build.sh       # keep function names (--profiling-funcs)
#   T3_BUILDER=1 wasm/build.sh    # the runtime program builder (embedded QuickJS; tools/fetch-builder-deps.sh)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WASMCART="${WASMCART:-$ROOT/../wasmcart}"
BOX2D="${BOX2D:-$ROOT/../box2d}"
BOX3D="${BOX3D:-$ROOT/../box3d}"
OUT="$ROOT/build/wasm"
# WGPU=1: dual carts (GL and WebGPU through wasmcart's WebGPU tier, emdawnwebgpu)
EMDAWN="${EMDAWN:-$ROOT/third_party/emdawnwebgpu}"   # tools/fetch-emdawnwebgpu.sh
# T3_TABLE=core: only the renderer's own programs (a project registers its table)
TBL="" TBLF=""; [ "${T3_TABLE:-full}" = core ] && TBL="_core" TBLF="-DT3_TABLE_CORE"
TOBJ="$OUT/obj/three$TBL"
WGPUF=""
if [ "${WGPU:-0}" = 1 ]; then
  [ -f "$EMDAWN/emdawnwebgpu.port.py" ] || { echo "WGPU=1: no emdawnwebgpu at $EMDAWN (run tools/fetch-emdawnwebgpu.sh, or set EMDAWN)" >&2; exit 1; }
  TOBJ="$OUT/obj/three-wgpu$TBL"
  WGPUF="-DT3_WGPU --use-port=$EMDAWN/emdawnwebgpu.port.py"
fi
# T3_BUILDER=1: states no table has are built on first use (src/builder.c)
BLDF="" BLDL=""
if [ "${T3_BUILDER:-0}" = 1 ]; then
  QJS="$ROOT/third_party/quickjs" TPKG="$ROOT/third_party/three-0.186.1"
  [ -f "$QJS/quickjs.c" ] && [ -f "$TPKG/build/three.webgpu.js" ] || "$ROOT/tools/fetch-builder-deps.sh"
  TOBJ="$TOBJ-builder"
  BLDF="-DT3_BUILDER -I $QJS"
  # QuickJS bounds the capture's JS recursion at 1 MB of stack (src/builder.c)
  BLDL="-sSTACK_SIZE=4194304"
fi
mkdir -p "$TOBJ" "$OUT/obj/box2d" "$OUT/obj/box3d"
OPT="-O2 -DNDEBUG ${EXTRA_CFLAGS:-}"
# LTO across three.c's files: the renderer calls small math helpers per mesh
TFLAGS="$OPT -flto"
# wasm simd128 (every current browser and wasmcart host): three.c's vector
# types and the compiler's autovectorizer, and Box2D / Box3D's SSE2 paths,
# which Emscripten lowers to simd128
SIMDF=""
[ "${SIMD:-1}" = 1 ] && SIMDF="-msimd128 -msse2"
TFLAGS="$TFLAGS $SIMDF"
# PROFILE=1 keeps function names in the cart for browser profiles
LINKX=""
[ "${PROFILE:-0}" = 1 ] && LINKX="--profiling-funcs"

NEWEST_H=$(ls -t "$ROOT"/include/*.h "$ROOT"/src/*.h "$ROOT"/src/*.inc "$ROOT"/src/gen/*.h "$WASMCART"/include/*.h | head -1)
pids=()
cc() { # out src flags...
  local o=$1 s=$2; shift 2
  # Rebuild when the source OR ANY HEADER (or a .inc renderer.c includes) is newer: a struct that grew in
  # three.h left core.o allocating materials at the old size while renderer.o
  # wrote the new fields past the end (heap corruption that crashed Chromium).
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ "$NEWEST_H" -nt "$o" ] || [ "${FORCE:-0}" = 1 ]; then
    rm -f "$o"
    emcc -c "$s" -o "$o" "$@" &
    pids+=($!)
  fi
  while [ "$(jobs -r | wc -l)" -ge "$(nproc)" ]; do sleep 0.05; done
}

SRCS="math core geometry curves animation raycaster loaders gltf renderer backend_gles gen_program gen/programs${TBL}_gl gen/programs_dfg"
[ "${WGPU:-0}" = 1 ] && SRCS="$SRCS gen_wgpu gen/programs${TBL}_wgpu"
if [ -n "$BLDF" ]; then
  node "$ROOT/tools/embed-builder.mjs" "$TPKG" "$TOBJ/builder_js.c" >/dev/null
  cc "$TOBJ/builder_js.o" "$TOBJ/builder_js.c" $OPT
  for q in dtoa libregexp libunicode quickjs; do cc "$TOBJ/qjs_$q.o" "$QJS/$q.c" $OPT -w -D_GNU_SOURCE -I "$QJS"; done
  SRCS="$SRCS builder"
fi
for s in $SRCS; do
  cc "$TOBJ/${s//\//_}.o" "$ROOT/src/$s.c" $TFLAGS $WGPUF $BLDF $TBLF -std=c99 -DT3_WASMCART \
    -I "$WASMCART/include" -I "$ROOT/include" -I "$ROOT/src"
done
# Physics single threaded; SIMD unless SIMD=0
PSIMD3="$SIMDF"; PSIMD2="$SIMDF"
[ -z "$SIMDF" ] && PSIMD3="-DBOX3D_DISABLE_SIMD" && PSIMD2="-DBOX2D_DISABLE_SIMD"
for f in "$BOX3D"/src/*.c; do
  cc "$OUT/obj/box3d/$(basename "${f%.c}").o" "$f" $OPT $PSIMD3 -I "$BOX3D/include" -I "$BOX3D/src"
done
for f in "$BOX2D"/src/*.c; do
  cc "$OUT/obj/box2d/$(basename "${f%.c}").o" "$f" $OPT $PSIMD2 -std=gnu17 -I "$BOX2D/include" -I "$BOX2D/src"
done
# every compile must succeed: a dropped object would link anyway, since
# undefined symbols become imports
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done
[ $fail = 0 ] || { echo "compile failed" >&2; exit 1; }

for scene in ${SCENES:-01-cubes 05-heavy 06-heavy-instanced physics3d physics2d}; do
  name="three-$scene$TBL"
  [ -n "$BLDF" ] && name="$name-builder"
  CARTF=""
  [ "${WGPU:-0}" = 1 ] && name="$name-wgpu" && CARTF="$WGPUF -DCART_WGPU"
  emcc $TFLAGS $CARTF -std=c11 -DCART_SCENE="\"$scene\"" -DT3_WASMCART \
    -I "$WASMCART/include" -I "$ROOT/include" -I "$ROOT/bench" -I "$BOX2D/include" -I "$BOX3D/include" \
    "$ROOT/wasm/cart.c" "$ROOT/bench/scenes.c" "$ROOT/examples/physics.c" \
    "$TOBJ"/*.o "$OUT"/obj/box3d/*.o "$OUT"/obj/box2d/*.o \
    -sSTANDALONE_WASM=1 -sEXPORTED_FUNCTIONS='["_wc_init","_wc_render","_wc_get_info"]' \
    -sERROR_ON_UNDEFINED_SYMBOLS=0 -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=1 \
    --no-entry $LINKX $BLDL -o "$OUT/$name.wasm"
  # the only imports a cart may have: GL, the wasmcart env functions, WASI
  node "$ROOT/wasm/check-imports.mjs" "$OUT/$name.wasm"
  # three.c's test/assets, and three.lua's compare/assets when a checkout is
  # there (the compare scenes' models)
  ASSETS="$OUT/assets"
  LUA_ASSETS="${THREE_LUA:-$ROOT/../three.lua}/compare/assets"
  if [ ! -d "$ASSETS" ] || [ -n "$(find "$ROOT/test/assets" "$LUA_ASSETS" -newer "$ASSETS" -type f 2>/dev/null | head -1)" ]; then
    rm -rf "$ASSETS" && mkdir -p "$ASSETS"
    cp "$ROOT"/test/assets/*.glb "$ASSETS"/
    if [ -d "$LUA_ASSETS" ]; then cp "$LUA_ASSETS"/* "$ASSETS"/; fi
  fi
  node "$WASMCART/bin/wasmcart-pack.js" --wasm "$OUT/$name.wasm" --name "three.c $scene" --assets "$ASSETS" \
    --version 0.1.0 --output "$OUT/$name.wasc" >/dev/null
  echo "built build/wasm/$name.wasc ($(wc -c < "$OUT/$name.wasc") bytes)"
done
