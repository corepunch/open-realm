# Thin pipelines and root structs

Tracking: [#89](https://github.com/corepunch/open-realm/issues/89).

## Constraint

No 64-bit GPU pointers / BDA on GL 3.1, macOS GL 4.1, GLES3 Mali-G31, or WebGPU.
Steal the design, not the pointers.

## Sources

- https://x.com/SebAaltonen/status/2096314523980349853
- https://github.com/sebbbi/NoGraphicsAPI
- https://www.sebastianaaltonen.com/blog/no-graphics-api
- SIGGRAPH 2026 extended talk *Reducing Graphics API Complexity*

## Mapping

| Steal | Do this on GL |
|-------|----------------|
| Root struct + one push | Shared POD, UBO/`glBufferSubData` once per draw |
| Thin PSO | `pipelineDesc_t` / `pipeline_t` in `r_backend.h` |
| Global heaps | 32-bit indices in the root; log GLES fallback |
| Explicit API | Fill root, then draw. `RB_*` is cache only |
| Tiny core | `r_backend.c` + `r_backend.h` |

## Status

Phase 1 landed: `RB_State` / `RB_Enable` / blend / depth / cull / scissor / VAO / FBO cache.
Call sites still use raw `R_Call(glEnable, …)` until Phase 2 migrates them to `RB_*`.
Phase 3+ (pipeline objects as the public bind, root-struct push, texture heap) follow.
