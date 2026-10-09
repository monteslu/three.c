# Perf suite

`bench/perf.mjs` measures what the ported compare scenes (README, "Performance")
do not: the stall when a material is drawn for the first time, frame hitches
over a play session, and the CPU cost of draws that change state. It runs
both backends, compares each metric with a saved baseline and exits non-zero
on a regression.

    ./build.sh && WGPU=1 ./build.sh
    node bench/perf.mjs                  # compare with bench/perf-baseline.json
    node bench/perf.mjs --save-baseline  # after an intended change
    node bench/perf.mjs --selftest       # the comparison must catch a slowdown
    node bench/perf.mjs --gpu 890m       # the integrated GPU instead of the RX 7600

Runs are headless (EGL surfaceless, Dawn on Vulkan) and take the shared GPU
lock (`$XDG_RUNTIME_DIR/cartwheel-gpu-suite.lock`). A full run takes about a
minute. The suite is not in CI: llvmpipe's numbers say nothing about a GPU.

## What it measures

| Metric | Scene and mode | Value |
| --- | --- | --- |
| `inflate.<backend>` | `--mode inflate` | the first program source of a backend's packed table: the whole table inflates once |
| `firstuse.c2-compile-zoo.<backend>.<cache>` | 38 material states drawn at once, `--mode firstuse` | the first frame (every program built), to GPU idle |
| `hitch.c1-material-churn.<backend>.<cache>.stallTotal` | a new material state every 10 frames, `--mode hitch` | the summed time of the frames that introduced a state; also their max, median, the p99 frame and frames over 16.7 ms |
| `cpu.<scene>.<backend>` | `--mode cpu` | the median CPU time to issue a frame, the GPU idle before each |

`cold` runs with `MESA_SHADER_CACHE_DISABLE=true`, as a player's first run;
`warm` runs with the driver's shader cache as it is. The CPU scenes:
`d1-unique-materials` (1000 boxes, one program, a material each),
`d2-unique-textures` (the same, a texture each), `d3-program-mix` (six
programs, a material each), plus `01-cubes`, `g64p`, `05-heavy`, `s1-stress`
and `p1-physical` from the compare set.

Each metric is the median of 3 processes. The best run is not used: one lucky
run became a baseline 40% under every run after it. A value counts as slower
above `base * (1 + tol)` and by more than `floor`: 20% and 0.1 ms for CPU
metrics, 25% and 10 ms for cold compiles, 2x and 25 ms for warm compiles (the
cache's contents decide them), 30% and 6 ms for inflate. `--selftest` adds a
spin of 35% of the frame to `d1-unique-materials` on each backend and must
see SLOWER, and must not see it on an unchanged run. Three reruns against a
fresh baseline came back with no regressions.

## Results (RX 7600, Mesa 26.0.8, 80195dc)

First use (compile), in ms:

| | GL warm | GL cold | WebGPU warm | WebGPU cold |
| --- | ---: | ---: | ---: | ---: |
| c2 first frame, 38 states | 46 | 284 | 294 | 544 |
| c1 stall total, 37 new states | 44 | 435 | 327 | 546 |
| c1 worst single stall | 6.8 | 31.5 | 23.4 | 35.9 |
| c1 frames over 16.7 ms | 1 | 9 | 5 | 16 |
| packed table inflate (once) | 20 | | 14 | |

CPU per frame, in ms:

| Scene | draws | GL | WebGPU |
| --- | ---: | ---: | ---: |
| g64p | 65 | 0.062 | 0.122 |
| d1-unique-materials | 1001 | 1.06 | 1.89 |
| d2-unique-textures | 1001 | 1.25 | 5.70 |
| d3-program-mix | 1001 | 1.30 | 2.47 |
| s1-stress | 9 | 7.64 | 5.62 |

## What the numbers say

- A material state's first draw costs about 10 ms on a cold cache on either
  backend, and up to 36 ms; at 60 fps that is a dropped frame or two each
  time. A game meeting materials mid-play hitches until async compile
  (`t3_renderer_compile_async` / `poll`, r186's `compileAsync`) lands.
- WebGPU's warm-cache stalls are close to its cold ones (327 vs 546 ms) while
  GL's warm cache removes 90% (44 vs 435 ms): render pipelines are not
  reused across runs on the WebGPU side. A pipeline cache (Dawn's blob cache)
  is the lead.
- WebGPU's per-draw CPU cost is about 2x GL's, and with a texture per
  material it is 4.6x (5.7 vs 1.25 ms for 1000 draws): a texture change costs
  far more on WebGPU than a uniform change. That is the next profile to take.
- The packed program tables cost 14 to 20 ms once, at the first program built.
