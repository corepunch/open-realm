# ARM CPU worker experiments (historical, 22 November 2026)

These are retained results from a completed experiment. The worker
implementations and their cvars have been removed; none of the switches below
is available in the current build. The renderer, client, and server run
sequentially in `common/main.c`: `SV_Frame` precedes `CL_Frame`, which draws
the world. Offloading only helps when it overlaps useful main-thread work and
does not block presentation. The measurements below found no benefit.

## Experiment design

The temporary patch set evaluated three independent worker switches:

| Cvar | Boundary | What actually runs on SDL thread |
| --- | --- | --- |
| `r_cull_worker` | render view (64+ entities) | pure frustum tests, overlapping shadow submission |
| `wc3_fow_worker` | server fog update tick | clearing connected players' prior visibility masks, overlapping blocker checks/rebuild |
| `r_mdx_worker` | MDX view-time sample | bone local/global matrices; render thread waits immediately |

### Frustum culling design tested

The temporary `r_cull_worker` path used a single SDL worker when a view
contained at least 64 entities. The render thread resolved each model's bounds,
transform, and fallback sphere before publishing the input. The worker only
executed pure frustum tests against a copied camera frustum; it owned no OpenGL
context and did not read game or renderer mutable global state. While it ran,
the render thread submitted ground-shadow splats, then waited for the ordered
visibility bits before rendering solid entities. Shutdown joined the worker
before releasing model resources. With the switch off, the original serial
path ran.

The culling experiment left model bounds/transform preparation and renderer
event presentation on the render thread. Its ARM measurements showed a
regression, so this worker was removed.

### Fog and MDX experimental limits

- **Fog:** clearing prior visibility for players writes independent player
  grids, so it can overlap checking/rebuilding the separate blocker grid.
  Shadowcasting, revealed-cell caches, script modifiers and delta transmission
  remained authoritative on the game thread. The major shadowcasting cost was
  **not** offloaded by this switch.
- **MDX:** `MDLX_BindBoneMatrices` uses shared scratch matrices and frame
  interpolation caches. The worker executes that routine, but rendering waits
  immediately before reading those matrices; no GL or simulation code is
  called on the worker. **This is a controlled thread-overhead experiment,
  not asynchronous pose/render overlap.** It may reduce performance. Real
  MDX speedup requires per-job owned scratch and a per-view immutable snapshot
  before safely overlapping draw submission.

## Measurement method

The captures used a fixed map/camera window and recorded `com_profile`,
`r_profile`, `wc3_perf_log`, VSync-off, and uncapped frame settings. The
`r_norefresh` diagnostic helped separate rendering from simulation but did not
represent rendered FPS. For this experiment, frame-rate and renderer-stage
captures were used; no `simpleperf` sampled call tree was collected.

## NightElf07 results on krane

The opt-in SDL workers were built and profiled on this ARM64 machine using
`Maps\\Campaign\\NightElf07.w3m`, 1920x1200, 100% render scale, VSync off,
unit shadows off, and a fixed 540-loop capture. `R_STATS`, `R_CPU`, and
`COM_FRAME` were recorded in `build/profiles/nightelf-workers-*.log`. Each
switch was measured independently twice, with a serial run before each; the
table averages the last 15 steady-state samples from each run.

| Configuration | FPS | renderer frame ms | world ms | entity ms | alpha ms | fog ms | swap ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Serial baseline A | 19.37 | 31.18 | 7.21 | 10.04 | 3.43 | 0.15 | 8.13 |
| Serial baseline B | 19.29 | 31.47 | 7.28 | 10.47 | 3.60 | 0.15 | 7.71 |
| `r_cull_worker 1` | 18.85 | 32.36 | 7.45 | 12.20 | 3.79 | 0.16 | 6.56 |
| `r_cull_worker 1` repeat | 19.09 | 32.27 | 7.36 | 11.76 | 3.64 | 0.16 | 7.10 |
| `wc3_fow_worker 1` | 19.21 | 31.26 | 7.33 | 10.23 | 3.49 | 0.15 | 7.90 |
| `wc3_fow_worker 1` repeat | 19.15 | 31.28 | 7.36 | 10.26 | 3.49 | 0.16 | 7.75 |
| `r_mdx_worker 1` | 12.65 | 51.62 | 10.23 | 28.87 | 5.78 | 0.21 | 4.22 |
| `r_mdx_worker 1` repeat | 11.65 | 56.04 | 10.43 | 32.86 | 6.18 | 0.22 | 4.07 |
| All three switches | 12.45 | 52.09 | 10.10 | 29.80 | 5.55 | 0.21 | 4.11 |

Serial baselines differ by under 0.1 FPS. The culling worker is consistently
slower by about 0.2–0.5 FPS and adds roughly 1.3–2.2 ms to entity preparation.
The fog worker changes FPS by less than the baseline spread and does not
measurably reduce fog time (about 0.15 ms). The MDX worker regresses by about
6.6–7.6 FPS and adds 18.4–22.4 ms to the renderer entity stage: a synchronous
semaphore handoff per pose cannot pay off on this workload. The all-workers
run confirms the MDX regression dominates. None of the three workers is a
candidate to retain; their implementation and cvars were removed after the
experiment. The logs remain under `build/profiles` for cleanup with the build
directory.

These are captured FPS and renderer-stage timings rather than a
`perf`/`simpleperf` sampled call tree. They establish that these workers did
not improve this map on this machine, but do not rank other CPU hotspots. The
worker cvars are not available; do not use the example `+set` commands from
the experiment design as current launch settings.
