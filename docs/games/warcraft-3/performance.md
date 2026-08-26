# WC3 Performance Hot Spots

For process footprint, allocation profiling, and RAM reduction priorities, see [WC3 memory](memory.md).

Profile-driven optimizations across the renderer, client, and server. The five sampled hot spots and the fixes applied to each are listed below so a future reader understands *why* each path is shaped the way it is.

## MDX bone setup (was ~16%)

`MDLX_BindBoneMatrices` (`games/warcraft-3/renderer/mdx/r_mdx_anim.c`) is called once per rendered model per frame. The old path:

1. `memset(node_matrices, 0, sizeof(node_matrices))` — 64 KiB (1024 nodes × 64-byte matrix) cleared per model.
2. Two loops over all `MDX_MAX_NODES` (1024) slots, each null-checking the sparse `model->nodes[]` array.
3. A `bone_matrices[MDX_MATRIX_PALETTE]` build loop plus a `glUniformMatrix4fv(uBones, ...)` upload — dead work, because `MDLX_BindGeosetMatrixPalette` re-uploads the real per-geoset palette immediately afterward.

Fix: `R_LoadModelMDLX` now builds a compact `model->node_list[]`/`num_nodes` (the nodes the model actually has, typically tens), and `MDLX_BindBoneMatrices` iterates only that list, zeroing each used matrix individually to reset the `v[15]==0` "computed" flag that `R_GetNodeGlobalMatrix` relies on. The dead `bone_matrices` build + upload were removed.

Why pose *caching* was rejected: a persistent cache key would have to include both the interpolated `frame0/frame1` pair (with `lerpfrac`) and the global-sequence render clock (`SDL_GetTicks`-derived), which changes every frame for global-sequence tracks — so caching buys little while the memset/scan removal eliminates the bulk of the cost.

## MDX geometry / material drawing (was ~19%)

`MDLX_BindGeosetMatrixPalette` (`r_mdx_geoset.c`) uploaded the full `BZ_BONE_PALETTE_MAX` (128) matrices per geoset. Skin indices are geoset-local (`0..num_matrixPalette-1`), so only `min(num_matrixPalette, BZ_BONE_PALETTE_MAX)` entries ever need to reach the shader. Uploading 128 when a geoset references, say, 12 was wasted uniform traffic on every draw.

## Entity shadows (was ~8%)

Unit shadows are ground decals: one shader (`SHADER_SHADOWSPLAT`), differing only by texture and rect. `R_RenderShadow` previously called `R_RenderRectSplat` per unit, which bound the splat shader/VAO/VBO, uploaded view uniforms, re-set blend/depth-mask, re-set texture wrap, then uploaded the vertex buffer (`glBufferData`) and issued a draw per shadow — hundreds of tiny draws/upload per scene.

Fix: `r_war3map_ground.c` now exposes `R_BeginSplatBatch` / `R_AddRectSplat` / `R_EndSplatBatch`. `R_RenderRectSplat` was refactored to share `R_SetupSplatState` + `R_GenerateSplatTiles` with the batch path. `R_DrawEntityShadows` (in `renderer/r_ents.c`) runs a dedicated pre-pass that accumulates every visible unit shadow and flushes only when the texture changes or the buffer fills, collapsing N shadows into one upload + draw per contiguous texture run. WoW and SC2 provide immediate fallback implementations (WoW shadows already go through `R_GameRenderShadow` returning true; SC2 splats are flat quads).

## Client entity collection + snapshot copying (was ~11%)

Both `CL_ParseFrame` (snapshot copy) and `CL_AddEntities` (collection) scanned all `MAX_CLIENT_ENTITIES` (16384) slots every frame, even though a scene has far fewer live entities.

Fix: `client_state` gains `active_entities[]` / `num_active`, a compact list of entity numbers whose current state carries a live model. It is maintained incrementally in `CL_ReadPacketEntities` (U_REMOVE drops the index; a `!old.model → model` transition adds it) and `CL_ParseBaseline`, reset in `CL_BeginLoadingMap` on map change and in `CL_ClearState` on disconnect. `CL_ParseFrame` and `CL_AddEntities` iterate the compact list instead of 16384 slots.

## Server entity simulation (was ~10%)

`G_RunEntities` ran three full passes over `globals.num_edicts`, and `G_RunEntity` ran `spell_run_frame` + `unit_updatestatuses` + status compression for every edict — including freed edicts, since `G_FreeEdict` memsets in place and `num_edicts` is a high-water mark that never shrinks.

Fix: all three `G_RunEntities` loops skip `!ent->inuse`, and `G_RunEntity` guards with `if (!ent->inuse) return;`. Freed edicts carry no simulation state and are never re-sent, so this is a pure skip.

## Verification

```bash
make test                    # full umbrella, incl. in-engine WC3 suites
make test-wc3-engine         # 344 tests / 918 assertions, incl. wc3_perf.run_entities_1900
make -j4 openwarcraft3 openwow opensc2   # all three renderers still build
build/bin/openwarcraft3 -data 'data/Warcraft III' +menu_main +screenshot 10 +com_frame_limit 20       # ROC
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +menu_main +com_frame_limit 20                 # TFT
```

The `wc3_perf.run_entities_1900` benchmark (`games/warcraft-3/game/tests/t_game.c`) measures `G_RunEntities` over 1900 active units.

### Review regression cases (PR #164)

- The active-list invariant is membership iff `cl.ents[index].current.model != 0`. Test baselines, duplicate adds,
  model-to-zero deltas, removal, slot reuse, frame copying, and map reset through the real client parser.
  `SV_BuildClientFrame` also transmits model-less entities carrying sound/events, so `U_REMOVE` is not the only way
  to lose a model. At `34a556f2`, a baseline `{number=7, model=1}`, followed by a packet delta to
  `{number=7, model=0, sound=1}`, leaves `num_active == 1`; a subsequent `U_REMOVE` still leaves that stale entry
  because removal is guarded by `old.model`. A focused wire-parser test reproduced both failures; the existing
  umbrella suite passes but does not exercise this lifecycle. Extend `tests/test_net.c` for regression coverage.
- `CL_BeginLoadingMap` in `games/warcraft-3/tests/test_client_stubs.c` does not mirror the new list reset;
  standalone parser tests using that stub do not validate production map-reset behavior.
- The WC3 splat implementation batches **contiguous texture runs**, not all occurrences of each distinct texture.
  `R_AddRectSplat` flushes at every texture change; `R_GenerateSplatTiles` also flushes at buffer capacity.
  Below capacity, A/A/B/B produces two draws, but A/B/A/B produces four. Entity order is not sorted by shadow
  texture and swap-removal changes it. Use both patterns when checking draw/upload counters; the distinct-texture
  claims above describe the optimization goal, not a guaranteed bound in this revision.
- The follow-up `10904293` restores the MDX particle size factor removed from `R_DrawParticles` in `97a52d18`.
  `ReadParticleEmitter` doubles all three `ParticleScaling` lifecycle values once; both MDX head and tail spawns
  consume those values. The shared renderer and M2 particle scaling are unchanged.
