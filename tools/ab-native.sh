#!/usr/bin/env bash
# Interleaved native A/B of the CPU time to issue a frame (bench-native --mode cpu):
# A and B alternate, ROUNDS times, so both see the same machine state.
#   tools/ab-native.sh <A bench-native> <B bench-native> <scene> [rounds=8]
# Prints each round and the median of per-round ratios A/B (> 1: B faster).
set -euo pipefail
A=$1 B=$2 SCENE=$3 ROUNDS=${4:-8}
export BENCH_EGL_DEVICE=${BENCH_EGL_DEVICE:-renderD128}
ms() { "$1" "$SCENE" --mode cpu --frames 300 | grep -o '"cpuMedianMs":[0-9.]*' | cut -d: -f2; }
ratios=()
for r in $(seq "$ROUNDS"); do
  if (( r % 2 )); then a=$(ms "$A"); b=$(ms "$B"); else b=$(ms "$B"); a=$(ms "$A"); fi
  ratio=$(awk -v a="$a" -v b="$b" 'BEGIN { printf "%.3f", a / b }')
  ratios+=("$ratio")
  echo "round $r: A $a ms  B $b ms  A/B $ratio"
done
printf '%s\n' "${ratios[@]}" | sort -n | awk '{ v[NR] = $1 } END { printf "median A/B %.3f  (min %.3f, max %.3f)\n", v[int((NR + 1) / 2)], v[1], v[NR] }'
