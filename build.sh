#!/usr/bin/env bash
# Native build: libthree.a and the bench host.
#
#   ./build.sh                  build/native: GLES 3
#   WGPU=1 ./build.sh           build/native-wgpu: GLES 3 and WebGPU (Dawn)
#   CFLAGS=-O0\ -g ./build.sh
#
# Needs cc and the EGL / GLESv2 / zlib development headers; the bench host
# also needs Box2D v3 and Box3D checkouts (BOX2D / BOX3D, default ../box2d and
# ../box3d) and cmake + ninja. WebGPU needs Dawn's webgpu.h and
# libwebgpu_dawn: NATIVE_DAWN=<dir with include/ and lib/>, by default the
# native-dawn package `npm install` puts in node_modules. The program tables
# (src/gen/) are checked in; tools/gen-programs.mjs + tools/emit-programs.mjs
# regenerate them.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CC="${CC:-cc}"
CFLAGS="${CFLAGS:--O2}"
OUT="$ROOT/build/native"
mkdir -p "$OUT/obj"

WARN="-std=c99 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"
SRCS="math core geometry curves animation raycaster loaders gltf renderer backend_gles gen_program gen/programs_gl gen/programs_dfg"
# WGPU=1: the WebGPU backend too (webgpu.h + libwebgpu_dawn from native-dawn; NATIVE_DAWN=<dist dir>)
WGPU_FLAGS="" WGPU_LIBS=""
if [ "${WGPU:-0}" = 1 ]; then
  case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) PLAT=linux-x64 ;; Linux-aarch64) PLAT=linux-arm64 ;;
    Darwin-arm64) PLAT=darwin-arm64 ;; Darwin-x86_64) PLAT=darwin-x64 ;; *) PLAT=unknown ;;
  esac
  ND="${NATIVE_DAWN:-$ROOT/node_modules/native-dawn/dist/$PLAT}"
  [ -f "$ND/include/webgpu/webgpu.h" ] || { echo "WGPU=1: no Dawn at $ND (npm install, or set NATIVE_DAWN)" >&2; exit 1; }
  SRCS="$SRCS gen_wgpu gen/programs_wgpu"
  WGPU_FLAGS="-DT3_WGPU -I $ND/include"
  WGPU_LIBS="-L $ND/lib -lwebgpu_dawn -Wl,-rpath,$ND/lib"
  OUT="$ROOT/build/native-wgpu"
  mkdir -p "$OUT/obj"
fi
objs=()
for s in $SRCS; do
  o="$OUT/obj/${s//\//_}.o"
  W="$WARN"; [ "$s" = gltf ] && W="-std=c99 -w" # cgltf / stb_image compile in this unit
  "$CC" $CFLAGS $W $WGPU_FLAGS -D_POSIX_C_SOURCE=200809L -I "$ROOT/include" -I "$ROOT/src" -c "$ROOT/src/$s.c" -o "$o"
  objs+=("$o")
done
ar rcs "$OUT/libthree.a" "${objs[@]}"

# Box2D v3 and Box3D for the physics demos, from sibling checkouts.
BOX2D="${BOX2D:-$ROOT/../box2d}"
BOX3D="${BOX3D:-$ROOT/../box3d}"
DEPS="$ROOT/build/deps"
for lib in box2d box3d; do
  src=$BOX2D; [ $lib = box3d ] && src=$BOX3D
  up=$(echo $lib | tr a-z A-Z)
  if [ ! -f "$DEPS/$lib/src/lib$lib.a" ]; then
    cmake -S "$src" -B "$DEPS/$lib" -G Ninja -DCMAKE_BUILD_TYPE=Release -D${up}_SAMPLES=OFF \
      -D${up}_UNIT_TESTS=OFF -D${up}_VALIDATE=OFF >/dev/null
    ninja -C "$DEPS/$lib" >/dev/null
  fi
done

"$CC" $CFLAGS -std=c11 -Wall $WGPU_FLAGS -I "$ROOT/include" -I "$ROOT/bench" -I "$BOX2D/include" -I "$BOX3D/include" \
  "$ROOT/bench/native_main.c" "$ROOT/bench/scenes.c" "$ROOT/examples/physics.c" "$OUT/libthree.a" \
  "$DEPS/box3d/src/libbox3d.a" "$DEPS/box2d/src/libbox2d.a" \
  -lEGL -lGLESv2 -lz -lm -lpthread $WGPU_LIBS -o "$OUT/bench-native"
echo "built ${OUT#$ROOT/}/libthree.a ${OUT#$ROOT/}/bench-native"
