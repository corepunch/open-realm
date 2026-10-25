## October 25 NightElf07 yaw-only entity placement (patch forty-six)

`R_GetEntityMatrix` previously built a general quaternion and full
rotation/translation/scale matrix even when `R_EntityPose` supplied only a yaw.
For these poses the quaternion is `(0, 0, sin(yaw/2), cos(yaw/2))`.
The shared renderer now writes the equivalent column-major uniform-scale
placement directly, computing only the half-angle sine and cosine and avoiding
the general quaternion conversion. Retaining **half-angle arithmetic** (rather
than `sin(yaw)` / `cos(yaw)`) preserves the reference transform's rounding near
view-frustum boundaries. Patch thirty-nine's exact identity-basis shortcut
still applies afterward; non-identity game/model bases still compose normally.

The fast path is guarded on zero pitch/roll and finite yaw, origin and scale,
so tilted poses and non-finite data keep the existing general builder. There
are no new persistent caches, game-specific renderer assumptions or changed
network/gameplay contracts. Tests
`wc3_coordinates.yaw_only_entity_matrix_matches_quaternion_reference` and
`wc3_coordinates.invalid_entity_pose_keeps_generic_matrix_behavior` compare
coefficients and synthetic frustum decisions with the old quaternion path,
including near-plane placements and exceptional floats. Warcraft III model
placement, culling and game-module conventions remain the source of truth.

This is a source-level CPU optimisation, **not** a measured FPS improvement.
No compilation, tests or game launch were run locally per the user's request.
Validate with the affected test suite and matched NightElf07 profiling of
`R_GetEntityMatrix`, `Quaternion_fromOrientation`,
`Matrix4_from_rotation_translation_scale_origin` and overall frame time/FPS.

## October 24 NightElf07 moving-shadow shared terrain corners (patch forty-five)

After patch forty-four the NightElf07 sample attributes 7.29% inclusive CPU
cycles to `R_DrawEntityShadows`, including 2.74% under
`R_GenerateSplatTiles` and 2.09% under `R_MakeSplatTile`. These are sampled
shares, not FPS or frame-time measurements. Shadows are already batched by
contiguous texture runs, and the persistent splat geometry cache intentionally
admits only repeated stationary rectangles. Moving shadows change their XY
rectangle every frame and take the uncached path.

The uncached splat builder used to sample all four terrain corners for **each**
accepted tile. For small rectangles crossing adjacent tiles, that reads the
same `R_GetVertexPosition` corners repeatedly, including the dynamic terrain
offset. Patch forty-five stores two adjacent columns of corner positions
**only for the duration of the current rectangle**. The columns are lazily
populated for accepted tiles, then rotated as the x-major traversal advances.
A fully accepted 2-by-2 rectangle now samples nine shared grid corners rather
than sixteen per-tile corners; a 4-by-4 rectangle samples 25 rather than 64.
There are no persistent allocations or new renderer/game APIs. Terrain
height/ramp/offset rules, clipped geometry, UVs, tints, blending order and
batch flushes still use the existing tile emitter.

Only uncached rectangles with between 2 and 32 candidate tiles use the new
stack-local cache. Single-tile rectangles avoid setup overhead; oversized
splats and geometry-cache baking retain the original four-sample path.
Previously cached geometry is still invalidated on deformation/map reset.
`renderer_terrain.moving_splat_corners_preserve_original_clipped_vertices`
compares generated vertices against the previous per-tile sampling algorithm
for clipped, moving, horizontal/vertical, edge and multi-height rectangles,
and asserts that corners are actually reused. The oversized fallback test
compares the original traversal. Existing geometry-cache/batching tests still
cover admission, release, atlas tint and draw order. This patch was reviewed
statically; the user will compile/run `+test 'renderer_terrain.*'` and compare
matched `R_MakeSplatTile`, `R_GetVertexPosition`, and FPS/frame times.

## October 24 NightElf07 hashed GL texture-ID lookup (patch forty-four)

The post-forty-three NightElf07 profile still samples `R_FindTextureByID`
near 1.52% exclusive CPU cycles, reached from `MDLX_GetTexture` during
geoset rendering. The model material references are numeric OpenGL texture
IDs, not case-insensitive archive paths. The renderer therefore maintains a
separate numeric-ID index from the existing 2,048-bucket texture-path cache.

`R_RegisterTextureFile` inserts a texture into one of 2,048 hashed ID buckets
when no texture with the same ID is registered. Each bucket uses the existing
`texture_t::next` pointer; it previously chained *all* registered textures
into one list. Lookups now traverse only a single bucket. No persistent
allocations, new public fields, renderer protocol changes, or production debug
logs are needed. Numeric IDs remain authoritative, including ID zero and
collisions, and repeated registrations keep the original first registered
pointer for a given ID.

The path cache (`R_CacheLoadedTexture`, `R_FindLoadedTexture`) remains the
owner of texture allocations and handles aliases, pinning, generations and
streaming reclaim. `R_ReclaimStreamedTextures` unlinks a reclaimed texture
from its **numeric** bucket before `glDeleteTextures` and memory free, so a
subsequent GL ID reuse cannot resolve a freed allocation. Explicitly released
non-cached textures also unlink before deletion. `R_ShutdownTextureCache`
clears all ID buckets after deleting owned textures, including registrations
that were not themselves path-cache entries. Non-cached textures are still
owned by their original caller and are not freed by cache shutdown.

`renderer_texture.hashed_id_lookup_handles_collisions_missing_ids_and_duplicates`
covers bucket collisions, duplicate IDs, missing IDs, and cleanup;
`renderer_texture.streamed_reclaim_unindexes_ids_before_reuse` checks
streaming eviction, numeric ID reuse and standalone release; and
`renderer_texture.shutdown_clears_registered_cached_texture_ids` checks
shutdown/re-registration. The earlier registry regression now clears its
stack fixture before leaving scope. Run `+test 'renderer_texture.*'` after
building; no compilation or tests were run during patch preparation.
Compare matched NightElf07 runs and actual frame times as well as
`R_FindTextureByID` self CPU-cycle share before crediting an FPS gain.

## October 24 NightElf07 per-frame entity-indicator lookup (patch forty-two)

After patch forty-one, `CL_ApplyIndicator` accounts for 2.99% exclusive CPU
cycles in the NightElf07 profile. The generic client used to scan all 32
indicator slots for **every** visible render entity, even when there were no
active indicators. This is CPU sampling evidence, not a matched frame-time
comparison. The affected path is presentation-only; no Warcraft-specific
ability logic or renderer API changes are required.

The client now builds a fixed 64-bucket, open-addressed lookup from the 32
indicator slots **once per client time value**. Rebuilding also expires old
entries. An incoming `TE_ENTITY_INDICATOR` immediately invalidates the lookup,
so events received at the same timestamp are visible without a frame delay.
`CL_ClearTEnts` invalidates it on session/map reset. When no indicators are
active, a single count check rejects the entity without hashing. The lookup
preserves the original first-slot-wins behaviour for duplicate entity IDs,
slot allocation/replacement order, 1,000 ms expiry, 500 ms blink period,
250 ms on phase and alpha-zero cancellation. No heap allocations, per-entity
state, protocol changes or production logs are introduced.

`client_tent.indicator_lookup_preserves_flash_events_and_hash_collisions`
checks multiple colliding entity IDs, blink boundaries, same-timestamp
replacement, alpha-zero cancellation and expiration.
`client_tent.indicator_lookup_preserves_first_slot_and_oldest_replacement`
checks duplicate-slot precedence and pool-full oldest replacement. The
`client_tent.indicator_lookup_expires_across_time_wrap_and_clears_on_reset`
regression covers wraparound timing and map/session reset. Run these
with `+test 'client_tent.*'` after compiling; the patch author did not compile
or run tests locally at the user's request. Re-profile `CL_ApplyIndicator`,
`V_AddClientEntity` and overall FPS/frame times in a matched NightElf07 scene.

## October 24 NightElf07 bounded blight datagram packing (patch forty-one)

The post-patch-forty NightElf07 profile attributes 3.32% inclusive sampled
CPU cycles to `G_BlightWriteDatagram` and 2.14% exclusive cycles to the shared
`MSG_EncodeRLE` function, mostly during blight updates. These are sampled
shares from one workload, not a measured frame-time slowdown caused by patch
forty. The old blight sender optimistically encoded all contiguous dirty rows
(or the whole pending sweep segment) and, if that failed the byte budget,
retried with one fewer row until compression or bitpacking fit. Fragmented
blight could therefore make the sender repeatedly visit the same cells.

The sender now **incrementally writes RLE once** while tracking the final-run
byte and the largest fully encoded row boundary fitting the payload budget.
In parallel, a constant-time capacity calculation finds the largest row
prefix fitting the existing LSB-first bitpack escape. It chooses whichever
prefix contains more rows, preferring RLE when equal, exactly as the prior
shrink-and-retry loop did. RLE is finalized at the recorded boundary; when
bitpacking wins it overwrites the temporary RLE output in place. The
`terrainMaskChunk_t` header, both wire codecs, row ordering, gap handling,
per-player dirty-bit clearing, incremental full synchronization, and periodic
sweep cursor remain unchanged. The payload length is additionally clamped to
the header's `uint16_t` capacity for oversized caller buffers. No persistent
compression cache or production debugging instrumentation was introduced.

`wc3_api.blight_packet_fit_preserves_previous_wire_for_full_and_fragmented_masks`
compares byte-for-byte payloads and dirty-row advancement with a test-only
copy of the old loop for multiple contents and tight capacities, including
run-length 255 boundaries and non-fitting packets.
`wc3_api.blight_packet_fit_preserves_large_map_sweeps_and_sparse_dirty_rows`
covers a larger synthetic mask, fragmented dirty runs, full-sync progress and
512-byte sweep bands. Existing bitpack/checkerboard, dropped-packet resync and
save-state tests remain relevant. The user will compile and run all tests;
this patch has received static review and Git applicability checks only.
Reprofile `G_BlightWriteDatagram`, `MSG_EncodeRLE`, `G_BlightPackBit`,
`SV_SendClientMessages`, and matched FPS/frame-time distributions rather than
interpreting relative sample shares as speedups.

## October 24 NightElf07 stable-global-clock MDX pose sampling (patch forty)

After patch thirty-nine removed WC3's redundant entity identity-basis matrix
multiplication, the next NightElf07 profile attributes 9.34% inclusive sampled
cycles to opaque `MDLX_BindBoneMatrices`, including 6.79% to node-matrix
calculation. Bone hierarchies already memoise parent matrices per bind and use
affine multiplication; replacing that recursion is not a safe assumption about
remaining cost.

`R_CalculateNodeMatrix` now returns a local identity matrix immediately for
nodes without translation, rotation or scale tracks, without unnecessary pivot
lookup. More importantly, **global-sequence tracks ignore the entity's old and
new local animation frames**: both samples use the entity's scoped global clock
or the fixed render-view timestamp. For such a stable clock, blended poses now
evaluate each global translation/rotation/scale track **once**, avoiding a
second keytrack evaluation and its searches. The existing vector lerp and
quaternion slerp remain in place using the same evaluated value at both
endpoints, retaining floating-point rounding for non-unit quaternions and
vectors. Regular sequence tracks still evaluate both frames normally; when no explicit clock
exists and the renderer would fall back to `SDL_GetTicks`, both global reads
are retained because live time can advance between them.

No evaluated poses or global values are persistently cached. Model-local parent
transforms, billboard evaluation, pivots, child attachments, frame blending,
paused animation clocks, sequence wrapping and the existing per-view caches
continue on their existing paths. The `renderer_model` regression
`mdx_blended_global_tracks_sample_once_and_preserve_hierarchy` compares
synthetic parent/child matrices to stateless track evaluation across global
clock wrap, verifies one global sample vs two regular samples, and covers an
explicit clock scope and unscoped SDL fallback. The sample counter is test-only.
No production debug logging has been added. `make test-renderer-model` passes
with 11,312 assertions across 224 tests. In-game visual checks remain.
Reprofile `MDLX_GetModelKeytrackValueResolved`, `R_CalculateNodeMatrix`,
`MDLX_BindBoneMatrices` and matched FPS/frame time rather than treating
CPU sample-share shifts as measured speedups.

## October 24 NightElf07 identity-basis entity transforms (patch thirty-nine)

Patch thirty-eight's NightElf07 capture attributes 8.22% inclusive sampled CPU
cycles to `R_EntityInView`, including 4.42% through `R_GetEntityMatrix`.
Warcraft III's `R_EntityPose` returns a fixed identity native-model basis,
but the shared matrix builder still multiplied the calculated placement by
that identity on every initial per-view transform. The calculation occurs
before the per-view matrix cache can serve subsequent solid/alpha draws.

`R_GetEntityMatrix` now recognizes an **exact identity basis** and copies its
placement matrix instead of running a full generic 4×4 multiplication. This
removes redundant arithmetic for Warcraft III and any other model that
supplies the same basis, without changing game-specific code or the existing
culling path. Other bases still use the original matrix multiplication;
quaternion evaluation, origin, yaw, scale, MDX bounds, frustum testing,
terrain-fitted model selection, event attachments and pose-cache invalidation
are unchanged. Only the native-basis composition is elided: no approximate
bounding volume or broader cross-frame matrix cache is introduced.

`renderer_game.wc3_identity_basis_matrix_matches_legacy_culling_composition`
compares the optimised builder against the prior placement-times-identity
composition and checks identical frustum decisions for varied entity
positions, angles, scales and bounds. This focused regression uses synthetic
models and no external Warcraft data. `make test-renderer-model` passes with
11,211 assertions across 223 tests. Compare matched `Matrix4_multiply`,
`R_GetEntityMatrix`, `R_EntityInView` and overall frame-time/FPS measurements;
CPU-cycle percentages alone are not proof of an overall speedup.

## October 24 NightElf07 per-view MDX keyframe interval reuse (patch thirty-eight)

Patch thirty-seven's NightElf07 profile attributes 8.89% inclusive sampled CPU
cycles to opaque `MDLX_BindBoneMatrices`, with 2.38% exclusive in
`R_KeyFrameBound`. For every animated translation/rotation/scale sample, MDX
previously repeated up to three binary searches: the first/last keys owned by
the selected sequence and the interpolation bracket for the current time.

The renderer now keeps a **1,024-slot, direct-mapped index cache scoped to the
current view**. Each entry is identified by the parsed track pointer, its
keyframe count/stride, and the exact sequence interval bounds. The first and
end key indexes can be reused across different units using the same model;
when the next sampled time stays between the stored adjacent keys, its right
bracket is reused as well. Bracket reuse checks the neighbouring key times
and uses the original lower-bound search otherwise, preserving duplicate-key
and exact-boundary semantics. Collisions simply fall back to the existing
binary searches. Interpolated values are **never** cached: the current time,
including global sequences, paused entity clocks, wraps, blended frames and
per-node interpolation, is always evaluated normally.

The cache starts with `MDLX_ViewPoseBegin` and becomes inaccessible after
`MDLX_ViewPoseEnd`, preventing pointers into replaced MDX resources from
surviving across views. Only bone-pose samples use it; general
`MDLX_GetModelKeytrackValue` consumers remain stateless, which also preserves
the behaviour of synthetic mutable test tracks. The implementation adds no
debug logging or game/network state. A cache entry requires an immutable
loaded MDX track for the duration of its current view, as in normal rendering.

`renderer_model.mdx_view_key_span_reuses_sequence_bounds_and_nearby_brackets`
compares the view-cache path with the original stateless evaluator across
sequences, changed animation times and a fresh view.
`renderer_model.mdx_view_key_span_preserves_global_clock_wrap_and_duplicate_keys`
checks global-sequence wrap and duplicate authored key times. The test-only
counters measure skipped binary-search calls. `make test-renderer-model`
passes with 11,109 assertions across 222 tests. Reprofile
`R_KeyFrameBound` and `MDLX_BindBoneMatrices`, and measure matched FPS/frame
times; raw sampling percentages do not establish a speedup.

## October 24 NightElf07 shadow-splat cache collision avoidance (patch thirty-seven)

The patch-thirty-six NightElf07 profile still attributes 6.55% inclusive
sampled cycles to `R_DrawEntityShadows`, including terrain-conforming splat
reconstruction. The terrain splat renderer already caches geometry for repeated
small rectangles, but the previous 128-slot **direct-mapped** table discarded a
cached stationary shadow whenever a different rectangle hashed to the same
slot. A large number of units can therefore defeat the reuse even when none
moves. These figures are profiler sample shares, not frame-time measurements.

The cache now has 128 hash sets with four LRU ways each, a 512 KiB maximum
vertex allocation budget and a two-observation admission rule. Four distinct
rectangles in one set can coexist; a fifth evicts the least recently used way.
If the vertex budget fills, admission releases the oldest baked entries until
there is room. Transient/moving splats still follow the original generator on
their first observation, and allocations that fail fall back to the original
path. Entries retain exact world-map and rectangle bitwise identities and
store terrain-conforming positions with neutral UVs/colour. Changing atlas
frames, textures or tint still happens at draw time with the original batching
and draw order. `R_InvalidateSplatGeometryCache` continues to clear all entries
on terrain-height/deformation updates and map reset; no stale height data
should survive those operations.

`renderer_terrain.splat_cache_keeps_colliding_stationary_shadows_and_evicts_lru`
exercises four simultaneous collisions, LRU replacement, cache reuse and
reset. Existing `renderer_terrain.splat_cache_*` tests cover clipped geometry,
UV/tint replay, moving rectangles, deformation reset and allocation failure.
The native build passed; tests were not run. Reprofile `R_MakeSplatTile` and
`R_DrawEntityShadows` and compare matched FPS/frame times; the largest benefit
requires repeated stationary shadows, so mixed moving scenes might improve less.

## October 24 NightElf07 world-splat isolation in HUD/cursor views (patch thirty-six)

Patch thirty-five reduced main-scene alpha rendering to 5.14% inclusive
sampled cycles. However, the NightElf07 call tree still routed the same
world-owned `R_W3DrawEventSplats` pass through HUD model sprites (2.74%
inclusive within that nested path) and the MDX cursor (1.49%). These are
separate call paths within CPU samples, not measured frame-time savings.

Both sprite types create nested `R_RenderView` calls with `RDF_NOWORLDMODEL`.
`_W3M_DrawWorld` already suppresses terrain geometry in such views, but the
game-level `R_DrawWorld` previously followed it with world event splats and
world event-spawn children regardless of the flag. Patch thirty-six skips
those world-owned effects in no-world subviews, while leaving the existing
terrain-deformation update in `_W3M_DrawWorld` and all normal world-view
splat processing intact. `R_W3DrawEventSplats` also rejects no-world views
at its own entry to prevent accidental direct replay. The world pass still
advances the original event lifetime and fade/atlas animation; no cached
geometry, rendering order, shader blend state or splat ownership changes.

`renderer_game.no_world_subviews_do_not_replay_event_splats` checks HUD and
other no-world views do not batch or draw splats, a real world view draws
them once, the alpha pass does not duplicate them and the normal solid pass
still expires events. The native build passed; tests were not run. Compare
matched FPS/frame times and the HUD/cursor `R_W3DrawEventSplats` call branches.

## October 24 NightElf07 alpha-pass filtering and cull reuse (patch thirty-five)

After patch thirty-four, `R_EntityInView` still accounts for 7.29% inclusive
sampled cycles and alpha rendering for 10.20%. `R_W3GetViewEntityMatrix`
remains visible inside alpha draws. These numbers are sampling proportions,
not measured frame-time improvements; patch thirty-five has not been profiled.

The opaque entity pass now records each scene **position's** visibility in a
view-local generation-stamped array. The Warcraft III alpha pass consults the
same view's results once the water pass has completed, so a bare parent model
that the solid frustum rejected need not re-enter its alpha model path. An
unknown result (missing cull pass, changed view, out-of-budget index,
`r_entities=0`) disables only cull-based rejection; the independent
opaque-only alpha filter still applies. Attachment models, ribbons,
model events and building-fire slots deliberately retain the original path:
their contributions may outlive or extend beyond parent geometry. Transparent
instance tint and blended/additive materials remain eligible; models with
only opaque/alpha-key materials are rejected before matrix construction,
rather than after binding the parent. Dynamic cliff replacements remain
eligible without making assumptions about the fitted mesh. Rejected MDX
entities still update their animation pause clocks, and orphan ribbon trails
still tick even when no entity reaches `R_RenderModel` in alpha.

The existing independent WC3 per-view transform cache is increased from 1,024
to 4,096 entries (about 344 KiB of matrix/key storage). It retains its scene
slot and changed-pose checks and normal overflow fallback, but can serve more
of NightElf07's scene after the cull and solid passes. No persistent per-unit
state, game/network contract, or new production diagnostics are introduced.

`renderer_game.wc3_alpha_model_filter_preserves_attachments_ribbons_and_transparency`,
`renderer_game.solid_culling_handoff_is_view_local_and_uses_scene_positions`,
and `renderer_game.wc3_alpha_filter_skips_opaque_scene_without_building_matrices`
cover conservative alpha eligibility, stale-view rejection, scene identity,
and a real alpha pass without matrix construction. The native build passed;
tests were not run. Verify transparent units, ribbons, child attachments,
spell effects, terrain-fitted cliffs and NightElf07 FPS and sampled alpha/matrix
costs on the target hardware.

## October 24 low-core x86 per-view Warcraft III entity-matrix reuse (patch thirty-four)

NightElf07 profiling after patch thirty-three still attributes about 3.56%
*inclusive* CPU-cycle samples in the alpha branch to `R_GetEntityMatrix`,
while the culling branch spends another 3.49%. These paths overlap with
`Matrix4_multiply` (3.78% exclusive), and are sample shares, not measured FPS.

A separate Warcraft III view-local matrix cache now shares model placement
between event presentation, model-bounds culling, opaque rendering and alpha
rendering. Unlike the MDX pose cache (96 entries, qualifying blended models
only), it can serve ordinary opaque and unanimated scene models too. Direct
scene-array indexes and generation stamps avoid scans and per-view clearing
of the full index; the patch-thirty-four cache originally held 1,024 transforms and recomputed
normally beyond the budget. Lookups verify unchanged origin, yaw and scale,
which are the complete inputs to Warcraft III's fixed-basis model placement.
Both the view scene-array identity and view lifecycle are checked.

Selection of terrain-fitted special doodad models still runs normally against
the reused transform. Child effects, temporary attachment entities, sprites
and other entities not in the current view's scene array never enter the cache.
The cache is ended at the same boundary as the MDX pose cache: after alpha
rendering. Changes to entity placement within a view recompute and replace
that entity's matrix, while changes to model, animation time and camera do not
invalidate its fixed-basis placement matrix (their downstream model/pose
processing is unchanged). This is static optimisation, not persistent state;
no new production diagnostics, save fields or wire fields are introduced.

`renderer_game.wc3_view_matrices_reuse_cull_solid_alpha_and_invalidate_mutations`
and `renderer_game.wc3_view_matrix_cache_bounds_and_scene_identity` cover
shared placement across passes, distinct entity identities, movement/rotation/
scale invalidation, temporary entities, isolated view expiration, sparse scene
indices and cache exhaustion. The native build passed; tests were not run. Compare
matched NightElf07 frame times/FPS and `R_GetEntityMatrix`/`Matrix4_multiply`
profiles, and inspect attached models, special doodads and cinematics.

## October 24 low-core x86 direct-indexed MDX view poses (patch thirty-two)

NightElf07 profiling after patch thirty-one assigns about 1.21% exclusive
CPU samples to `MDLX_ViewPoseFind`. The previous lookup compared up to 96
pose records in reverse order for every cached-draw and restore query.
`MDLX_ViewPoseFind` now indexes the current view's scene-array position in
constant time, without searching those entries. The map uses a 16-bit
per-view generation stamp and one-byte cache-entry indexes; a full stamp
clear is needed only when the generation wraps. It is **not** keyed by the
entity's wire number, which may be shared among presentation instances.

The original limits of 96 poses and 4,096 stored node matrices remain. The
normal fallback handles any uncached entity, out-of-range scene-array slot,
child/sprite copy, or exhausted budget. The existing snapshot, view time,
lerp factor, camera, effective global animation time, selected model,
remapped frame, matrix and node-ID checks still gate reuse; this patch does
not widen cross-pass eligibility or retain anything across views.

`renderer_model.mdx_view_pose_direct_index_*` tests cover sparse/nonsequential
scene slots, duplicate scene-number values, out-of-array copies, repeat
stores, budget exhaustion and new-view invalidation. Test-only counters
verify that a cache hit examines exactly one candidate. The renderer model
suite passed (10,996 assertions across 213 tests), and the full `make test`
suite passed. Re-profile NightElf07 before claiming an FPS improvement.

## October 24 low-core x86 cross-pass MDX pose reuse (patch thirty-one)

NightElf07 profiling after patch thirty still shows about 7.03% inclusive CPU
samples in solid-pass `MDLX_BindBoneMatrices` and 6.56% in alpha-pass bone
binding. These are *sample shares*, not FPS or measured elapsed time.

`MDX_RenderModel` now saves the pose of a scene MDX entity after its solid-pass
bind **only when its materials, tint or ribbons require alpha work**. The
snapshot uses a fixed per-view budget of 96 entities and 4,096 active nodes
(about 280 KiB for node data), retaining only matrices for authored nodes.
After other models overwrite the global bone scratch, an alpha-pass draw can
restore the identical pose instead of rerunning animation keytrack searches.
The `R_RenderModel` entrypoint also reuses the selected model matrix for
eligible entities, eliminating redundant transform construction.

Re-use demands the exact view entity pointer and contents, identical selected
model, local frames (including Gold/Lumber remapping), effective global-sequence
time, model transform, render time and camera view-projection matrix. The
cache deliberately excludes terrain-fitted special meshes, temporary child
models and sprites; unmatched/overflowed entities use the prior full bind.
Every restored pose receives a fresh scratch generation so attachment and fire
collectors still invalidate correctly after intervening child renders. The
cache starts in the solid scene pass (or when an isolated portrait first saves
an eligible model) and is cleared at the end of alpha rendering. It does not
survive frames, nested view completion or model reload boundaries.

`renderer_model.mdx_view_pose_*` tests validate scratch-pose restoration,
child overwrites, frame and matrix changes, entity mutation, view time and
camera changes, and end-of-view invalidation. The renderer model suite passed
(10,974 assertions across 211 tests), and the full `make test` suite passed.
Confirm sprite portraits, transparent units, attachments, billboard bones and
construction animations visually and compare NightElf07 frame times on
x86/ARM64 before retaining.

# WC3 Performance Hot Spots

## October 24 x86 MDX alpha-pass bone-pose fast path (patch thirty)

NightElf07 low-core x86 profiling after patch twenty-nine puts
`MDLX_BindBoneMatrices` at 7.37% inclusive in the opaque entity branch and
6.98% in the alpha entity branch. These are disjoint call paths, but sampled
CPU shares are not direct frame-time or FPS measurements.

The alpha pass traverses all MDX entities, although an untinted model whose
materials have only `BLEND_MODE_NONE` or `BLEND_MODE_ALPHAKEY` layers and no
ribbons contributes no parent-model geometry in that pass. `MDX_RenderModel`
now skips its own frustum, shader setup, lighting and bone-pose bind for that
case. It still calls `MDLX_UpdateEntityAnimationPause` before returning, so
held global-sequence clocks remain correct. The existing opaque pass is not
changed; alpha-tinted instances, blended/additive/modulate materials and
ribbon emitters all use the previous path.

The skip is *inside* `MDX_RenderModel`, not `R_RenderModel` or
`R_DrawAlphaSurfaces`, so the caller still ticks detached ribbons, checks and
renders child-model attachments, and processes building-fire overlays. A
parent pose is rebound on demand if an attachment or fire overlay requires it.
Material inspection uses existing loaded lists each call, avoiding persistent
cache invalidation and conservatively allowing alpha work when a blended
material is present even if no currently visible geoset uses it. Particle
emitters remain exclusive to the solid pass as before.

`mdx_geoset.alpha_work_classifies_opaque_tinted_blended_and_ribbons` covers
opaque-only classification, white/unset and translucent tint, blended and
alpha-key modes, ribbon preservation, and material changes between calls. The
native build passed. The geoset tests passed (46 assertions across 2 tests),
and the full suite passed. Compare fixed-scene FPS, median frame times, and
`MDLX_BindBoneMatrices` alpha-pass samples on x86 low-performance cores. Run on
ARM64 before considering the optimisation validated for handhelds.


## October 24 low-core x86 entity culling matrix and affine frustum (patch twenty-nine)

NightElf07's post-patch-28 x86 samples identify `R_EntityInView` at 12.11%
*inclusive* CPU share, including repeated `R_GetEntityMatrix` work and eight
`Matrix4_multiply_vector3` operations per frustum box test. These sample shares
are not frame-time or FPS measurements.

The Warcraft III model-bounds provider needs a world matrix to choose the
correct fitted doodad model. Previously the common entity culler called that
provider, which computed the matrix, then recomputed the same matrix to test
the resulting bounds. The new `R_GetEntityBoundsAndMatrix` contract returns
both in one call. Warcraft III shares the matrix with fitted-model selection,
while the StarCraft II provider computes a matrix only when model bounds
exist; WoW retains its sphere-only fallback without introducing transforms.
The old `R_GetEntityBounds` API remains available for rings and other callers.

`Frustum_ContainsBox` now computes a support corner for each frustum plane in
local coordinates when the model matrix is affine. It transforms only that
candidate corner, rather than all eight corners, to determine whether the
box is wholly outside the plane. Reflection, rotation and nonuniform scale
are supported; the original eight-corner algorithm handles projective
matrices and plane-contact cases where float rounding could change the strict
`> 0` test. This is not an occlusion or visibility-rule change. No persistent
cache, build-system flags, new diagnostics or save/network fields were added.

`renderer_model.affine_frustum_box_matches_corner_test_across_rotations_and_boundaries`
compares to the previous corner algorithm for rotated/mirrored/nonuniform
transforms, outside/inside positions, exact plane contacts and a projective
matrix. `renderer_game.wc3_entity_bounds_share_the_exact_culling_matrix`
checks that the matrix used to choose WC3 bounds is the matrix passed to the
culler and retains the unsupported-model fallback. The native build passed;
tests were not run. Verify fixed-camera NightElf07 frame times and FPS, then
repeat on Human02, Undead07 and ARM64.

## October 24 x86 MDX attachment reference-only fast path (patch twenty-eight)

New NightElf07 and Undead07 low-performance x86 core `perf` call stacks show
`MDLX_CollectAttachmentPositionsWithPose` at 10.59% and 9.22% inclusive sample
share through the alpha-pass attachment renderer, primarily under
`MDLX_BindBoneMatrices`. These are sampled workload shares, not measured FPS.
The old renderer collected every attachment *before* checking its nonempty
child-model path, and could therefore rebuild a costly pose after a culled
parent draw even when every attachment was merely an empty-path reference.

`R_W3RenderAttachmentModels` now calls a render-only collection entrypoint that
checks for at least one child-model path before binding any bones. If none is
present it returns an empty list immediately; no drawn children can be lost.
For mixed lists the original collector still runs, preserving encounter order,
visibility filtering, the fixed 32-position limit (including reference-only
entries) and the existing child model/animation mapping. This optimisation
does not change `MDLX_CollectAttachmentPositions` for overhead, events,
building overlays or other consumers of reference points.

The building-fire `Sprite ` collector now accepts the parent's generation from
the preceding MDX draw, just as the ordinary child-model collector does. The
same checks require identical model identity, transform and frames, and a
matching current scratch-pose generation; intervening model-child draws and
other binds correctly force recalculation. The original model and frame
contracts remain authoritative even for remapped Gold/Lumber animations and
terrain-fitted models; unmatched poses still bind normally. No global or
per-instance long-lived cache or debug logging was added.

`renderer_model.mdx_render_attachments_*` verifies reference-only lists avoid
binding after a culled draw, mixed lists retain reference ordering and cap
semantics, and matching/mismatched poses reuse or recompute as appropriate.
The existing `mdx_attachment_pose_reuse_invalidates_after_child_model` and
`mdx_attachment_missing_prefix_skips_pose_binding` tests cover the shared
scratch-pose invalidation and prefix cases. Compile and test on the x86
low-performance cores, then compare fixed-scene FPS, median frame times and
attachment/pose samples; verify ARM64 separately.

## October 24 low-core x86 occluded fog shadowcast/rim loop (patch twenty-six)

The low-performance-core x86 profiles identify `G_FowCastLight` (3.89% of
exclusive samples on NightElf07) and `G_FowRevealBlockerRim` (1.77%) as
significant simulation costs when units illuminate blocker-heavy terrain.
The sample shares are hotspots, not predicted FPS gains; profile comparable
scenes to determine whether this patch improves actual frame times.

`G_FowCastLight` keeps exactly the same floating-point slope divisions,
comparison order, octant order and recursive blocking decisions. It now
computes each octant row's fixed world offset only once, evaluates the right
slope before the left slope, and calculates world cell coordinates only after
both slope filters accept the candidate. Rejected cells cannot change any
fog state, so their coordinates were redundant. No rounding-sensitive slope
reciprocal approximation is introduced.

`G_FowRevealBlockerRim` clips candidate x/y bounds to the map once per
revealer and checks the sparse blocker plane before computing the disk radius.
The existing one-cell light margin has exactly four possible orthogonal
visible neighbours once the candidate's center has been checked as invisible:
these are now tested directly. Importantly, **the scan remains ordered by y
then x**, and the temporary `visible == 2` markers still count as visible to
subsequent rim candidates. The final commit still marks the same dirty rows,
visible cells and explored cells. No fog buffer, cache, allocations, protocol,
or save format changes occur.

Added `wc3_game.fow_shadowcast_blocker_rim_rebuild_is_stable` and
`wc3_game.fow_shadowcast_blocker_rim_clips_at_map_corner` regressions that
exercise the actual fog update and blocker rebuild paths; run the existing
blocker, tree-pathtex, fog state and packed-mask tests too. The native
game-module build passed; tests were not run. Validate on x86 low-core FPS and
frame times first, followed by ARM64 gameplay tests.

## October 24 low-core x86 MDX affine hierarchy composition (patch twenty-five)

Profiling Human02, NightElf07 and Undead07 on low-performance x86 CPU cores
showed significant exclusive samples in general `Matrix4_multiply` (3.68%,
4.76% and 5.34%, respectively). Those samples aggregate multiple call sites;
they are *not* a prediction of patch twenty-five's FPS improvement.

`R_GetNodeGlobalMatrix` now takes advantage of the MDX transform contract:
`R_CalculateNodeMatrix` builds only translation/rotation/scale (affine) local
matrices. For an unanimated child (no translation, rotation or scale tracks),
the identity local matrix is composed by simply copying the already-resolved
parent matrix. For animated children, parent/local composition uses
`Matrix4_multiply_affine`, which evaluates the 3x4 affine product instead of
an arbitrary 4x4 product. Billboard cancellation multiplies that are also
known affine use the same helper. The generic `Matrix4_multiply` remains
unchanged for projection, camera and other potentially projective callers;
the view-projection/model multiplication in the billboard path remains
fully general. The helper requires non-overlapping operands just as the
original multiply did. No persistent cache, allocations, shared renderer
state or new log output are introduced.

Headless `renderer_model.mdx_affine_*` regressions compare the specialised
multiply with the general implementation for rotated, scaled, translated
and mirrored matrices, and check a three-node hierarchy containing an
unanimated intermediate node and animated parent/child. The existing
parented-billboard, attachment, ribbon and sprite tests should also be run.
The native build passed; tests were not run. Compare matched x86 frame times
first, then validate on ARM64 without assuming the x86 result transfers
proportionally.

## October 24 ARM64 status-update frame gate and periodic pulse deadline (patch twenty-two)

The Human02, NightElf07 and Undead07 profiles after the first 21 performance
patches identify a hot instruction inside `unit_updatestatuses` at 3.37%,
5.20% and 4.21% *exclusive sampled CPU shares* respectively. Those values
represent a hot instruction rather than total function cost; the profiles do
not establish a frame-rate change on their own. A reported FPS regression
across patches nineteen through twenty-one has not been causally isolated.

`G_RunEntity` previously called `unit_updatestatuses` on every live edict,
including entities without `unit_state` and units whose validated timed-status
mask has no timed entries. The frame caller now bypasses the function if no
unit state exists or the mask equals `TIMED_STATUS_UPDATE_VALID` (a validated
index with no timed slots). It still calls the updater for an invalid index
(e.g. spawn/load, direct fixture edits) and for any live timed slot. This
preserves the full index rebuild, expiration/removal, timed-life death and
other status lifecycle work, including statuses added during `spell_run_frame`.
Managed add/remove operations already maintain the index. The function itself
remains callable directly from combat, save/load and tests.

Within the existing status update, statuses with a future nonzero `next_tick`
now skip `A_STATUS_TICK` ability dispatch until their deadline **while the
applying source is valid**, still running expiry checks first. The current
scheduled owners (Disease Cloud, Poison and Entangling Roots) have no periodic
work before the next pulse; Disease Cloud also retires an invalidated source,
so invalid/missing sources must still enter their callback immediately.
Slots with `next_tick == 0` retain the original per-frame tick-dispatch path.
The loop still reads the live timed-status mask
before each slot so callbacks can remove or apply later slots; no persistent
fields, timer heap or new cache invalidation contract are introduced.

Headless tests `wc3_combat.status_frame_*` use the actual `G_RunEntity` caller
to verify no sweeps for valid empty/permanent-only indexes, dynamic add/remove
and invalid-mask rebuild after direct edits. The
`wc3_combat.status_update_skips_future_pulses_but_not_due_or_expired` test
checks just-before, exact-deadline and expiry ordering via test-only counters;
existing poison, roots, Disease Cloud and timed-life tests cover live spell
behaviour. These test counters are excluded from release builds. No debug
logging was added. The native game-module build passed; run the headless tests,
then compare identical gameplay scenes and median frame times across missions
on ARM64 before drawing performance conclusions. Tests were not run here.

## October 23 ARM64 Disease Cloud range-first targeting (patch nineteen)

Profiles collected on Human02, NightElf07 and Undead07 after the first
18 patches show that the earlier Human02-only renderer bias does not capture
all missions. On Undead07, a hot instruction in `S_PossessionSpellImmune`
accounts for 7.30% exclusive sampled cycles and is reached from
`CAbilityDiseaseCloud` through `S_SpellAllowsTarget` (19.78% inclusive along
that call chain). These shares are *not* complete function costs or FPS estimates.

`CAbilityDiseaseCloud` previously invoked `S_SpellAllowsTarget` on every
non-source edict before checking the target's range. It now rejects entities
outside an axis-aligned square first, then performs the **original exact
Euclidean range check**, and only then resolves spell eligibility. This
avoids immunity/status/ability scans for distant entities without expanding
the circle, changing exact-edge behavior, altering authored target masks or
changing infection timing, source identity, refresh rules or save data.

A headless `wc3_spell.disease_cloud_rejects_distant_edicts_before_spell_eligibility`
regression exercises exact-range edges, near/corner/out-of-range enemies,
allies and magic-immune targets; a `BZ_TESTS`-only counter verifies that
out-of-range edicts do not enter expensive target validation. Existing
`creep_disease_cloud_alias_ticks_authored_area_damage` and
`disease_cloud_independent_sources_and_refresh` tests cover damage and
source-specific status phase. No debug logging, persistent cache or extra
allocation was added. The native game-module build passed; automated tests
and ARM64 timing remain for the maintainer. Compare fixed-scene Undead07 frame times and `perf`
profiles and verify Human02/NightElf07 do not regress.

## October 23 ARM64 ground-surface candidate index (patch sixteen)

`R_ConformGroundSurfaces` previously rescanned every view entity for each
`RF_GROUND_CONFORM` entity, even though only `RF_GROUND_SURFACE` entries can
supply an MDX deck. It now gathers up to 128 surface indices from the **current
view** in entity order and checks only those indices for each conformer. This
is a per-call stack index, not a cross-frame cache; moving/hidden surfaces and
changes to animated bridge/elevator geometry are re-evaluated every frame.
With N view entities, C conformers and S surface candidates, the common-case
classification work changes from O(C*N) to O(N+C*S). If more than 128
surfaces are present, it falls back to the original full list scan rather
than discard any. No heap allocation, persistent state, network fields or
new per-frame logging are involved.

`R_W3WalkableSurfaceHit` still performs exact `MDLX_TraceWalkableSurface` and
rejects hidden or model-less surfaces; highest-hit selection, equal-height
source order, `ground_offset`, `ground_snapshot_z`, and the never-lower rule
remain unchanged. The support marker flag is still cleared on each normal
call, including calls with zero surface candidates. `RDF_NOWORLDMODEL`
retains the original early-exit behavior.

Headless production-path regression tests under `renderer_game.ground_surface_index_*`
cover no hit, hidden/model-less surfaces, highest of overlapping supports,
animated elevation, altitude bounds, a 96-entity sparse view and the
129-surface overflow fallback. Test-only visit and exact-trace counters
check the work reduction; release builds have neither counter. **No local
compilation or tests were performed**. Validate both the headless tests and
bridge/elevator scenes (particularly Orc04) and compare median frame times
and fresh `cycles:ppp` profiles on ARM64, keeping camera/scene fixed.

## October 23 ARM64 contiguous splat submission batching (patch fourteen)

The post-patch-thirteen handheld profile puts the world splat geometry path at
about 2.81% inclusive CPU sample share and the immediate-event splat draw path
at about 3.21% within the sampled HUD/rendering call chain. These paths
**overlap other renderer costs** and cannot be summed or translated directly
into an FPS improvement. The prior small-rectangle geometry cache showed a 1.4% increase in sampled
splat-generation cost and was removed; this pass targets redundant submissions.

`R_AddRectSplatUV` reuses the existing vertex buffer, per-texture flush, and
shader state setup for **contiguous compatible splats**. Different atlas frames,
vertex tints, and UVs are written at submission time into the existing vertex
order. Texture *or shader* changes flush first, as does buffer exhaustion;
blended splats are never sorted, grouped across intervening draw passes, or
moved across a model draw. An empty batch does not issue GL state changes.

Only the uninterrupted `R_DrawDecals` pass and the retained SPL/FPT/UBR event
splat pass in `R_W3DrawEventSplats` opt into batching. Per-entity splats remain
immediate because `R_RenderModel` and supported/flat overlays can intervene;
newly emitted event splats drawn outside the retained pass also remain
immediate. Lazy event-texture loads finish the current batch *before* any GL
resource registration, then restart a fresh batch. Terrain-conforming geometry clipping, depth bias, alpha blending, and atlas
animation remain in effect.

Regression tests `renderer_terrain.consecutive_splats_batch_without_changing_atlas_color_or_texture_order`
and `renderer_terrain.splat_batch_empty_pass_does_not_change_gl_state`
exercise submission count, texture/shader boundaries, UV/tint order and an
empty pass with mock GL calls; renderer-game event tests also assert retained
splat batching while preserving one-off event renders. **No local compilation
or test execution was performed for this patch.** Compare the same gameplay
scene on ARM64 using stable frame-time measurements and a fresh cycle profile.

## October 23 ARM64 client overlay paint-pass grouping

After the occupied-index status-state patch, `SCR_DrawLayout` was 10.78%
inclusive and `SCR_LayoutDrawOverlay` 6.22% inclusive in the sampled handheld
profile. These paths include real rendering; neither figure is wholly
recoverable scan overhead. The generic client previously traversed the
complete decoded frame array for each of 11 paint passes, including four
late-caption ranks.

`SCR_LayoutDrawOverlay` now performs a stable per-draw partition into the
same eleven groups, preserving frame-number order within each paint pass,
normal/overlay sprite and texture ordering, status bars under labels,
separate glue-button highlights and descending caption ranks. No persistent
cache, game-specific UI logic, or network/save format change is involved.
Regression coverage is in `client_layout.overlay_paint_groups_stay_stable_with_mixed_frames`
and the existing sprite-over-button ordering test. Compare FPS or median
frame times in a repeatable scene; only that can establish a device-level
gain from fewer scans.

## October 23 ARM64 categorical status-state lookups

The ARM64 `cycles:ppp` capture with the first ten performance patches reports
`unit_hasstatusstate` at 7.54% exclusive sample share through
`monster_think`'s stun check. Unlike `G_UnitStatusLevel`, the categorical
state query still inspected all 16 status records, including unoccupied
152-byte records, every time a unit with an active Move was considered.
This is a sampled share for the recorded scene, **not** a projected FPS gain.

`unit_hasstatusstate` now uses the same `status_occupied_mask` and
`TIMED_STATUS_UPDATE_VALID` contract already maintained for status levels.
A valid, empty mask answers immediately; otherwise the query uses
`__builtin_ctz` to inspect only occupied slots. An invalid index (new,
restored, or explicitly edited unit state) conservatively falls back to all
16 slots until `unit_updatestatuses` rebuilds it. Managed additions,
removals and refreshes already maintain both indexes. The helper preserves
built-in buff/state mapping, explicit `state_mask` contributions, the
single-state argument restriction, time-based expiry before cleanup,
and independent simultaneous providers. No per-unit field, persistence
format, network field or debug logging was added.

`wc3_combat.status_state_index_*` tests cover cached empty, persistent,
custom, sparse timed, independent-provider, removal/reuse and invalidated
index cases. The existing saved-unit-state index rebuild test remains
applicable. Build and run the tests, then compare the same gameplay scene's
`unit_hasstatusstate` samples and median frame times on ARM64.

## October 22 ARM64 selection revalidation scan

On the handheld, `G_UpdateClientSelections` accounted for about 1.6% of
exclusive `cycles:ppp` samples. Previously it scanned `globals.num_edicts`
for *each* client on every server frame, regardless of how many units were
selected. JASS `SelectUnit` and `ClearSelection` can alter `edict_t.selected`
directly, so a persistent index maintained only by the C selection helpers
would not represent authoritative selection state.

`G_UpdateClientSelections` now gathers all edicts with any raw selection bit
in a single scan, in entity order. Each player's current bit is tested when
processing that player, then the existing selectability and selection-limit
rules apply unchanged. Hidden, dead, fogged, and newly unselectable units still
have stale raw bits removed. Cap reduction continues to use Warcraft priority
ordering and publish the original JASS events. A selection change triggers a
fresh candidate scan before processing another client, because those events
may select units that were not present in the initial snapshot. Abnormally
large scripted selections (more than `MAX_CLIENTS * WC3_SELECTION_MAX`
distinct edicts) use the old unbounded scan instead of truncating the set.

There is no persistent index, save-format change, or network-protocol change.
The `wc3_api.selection_revalidation_*` and existing selection-limit/JASS tests
cover the raw-bit and lifecycle cases. Compare frame times in the same scene;
relative CPU sampling shares alone do not prove an FPS increase.

For process footprint, allocation profiling, and RAM reduction priorities, see [WC3 memory](memory.md).

## October 22 ARM64 Moon Well class-lookup caching

`perf` after the Moon Well dispatch guard found two `S_MoonWellMayUpdate`
addresses with about 1.19% combined exclusive CPU-cycle share. Although the
negative guard avoids costly ownership resolution, it still called
`G_AbilityCode` for every authored or added ability on each update tick.

Moon Well classification now uses a small, direct-mapped table keyed by the
four-character ability rawcode and `G_AbilityDataGeneration()`. Both positive
(custom aliases inheriting `Ambt`) and negative classifications are cached;
collisions are checked against the complete key. The generation changes on
ability SLK replacement, map ability override replacement, unit-data startup
and shutdown, so cached classifications cannot outlive those definitions.

Unit ownership is *not* cached: the current native ability list and runtime
additions are still inspected each tick, and `moon_well_alias` retains its
removed-skill check. Thus runtime additions/removals, morphs and save/load need
no new invalidation, persistent edict state or serialization. `t_spell.c`
covers reuse across units, metadata replacement in both directions, and hash
collisions; prior dispatch and effect-lifecycle tests remain in place. Measure
actual frame times separately; this optimisation does not promise an FPS gain.

## October 22 ARM64 Moon Well update filtering

The latest ARM64 profile, after the ability-update dispatch patch, still
identifies `CAbilityManaBattery` inside `S_RunAbilityUpdates`. Its Moon Well
`A_UPDATE` handler previously checked every authored skill with
`G_ActorHasSkill` before determining whether it inherited `Ambt`; this repeated
native list parsing even for units with only unrelated abilities.

The dispatcher now uses `S_MoonWellMayUpdate` to reject units whose native and
runtime-added abilities have no `Ambt` base (including custom `AbilityData.code`
aliases). The original `moon_well_alias` still makes the final ownership check,
including removed native abilities, but tests class membership *before* calling
`G_ActorHasSkill`. Genuine Moon Wells incur an extra lightweight class scan;
non-owners avoid the much more costly repeated ownership lookups.

The filter is recomputed every tick: no saved derived state, network changes,
or debug logs. `A_DISABLE`, `A_DEATH` and `A_UNIT_REMOVE` continue to own effect
cleanup; construction suppresses persistent effects as before. The tests in
`t_spell.c` exercise unrelated abilities, custom inherited aliases, runtime
additions/removals, authored-list changes, and construction cleanup. Recheck
matching campaign scenes and `S_RunAbilityUpdates`/`CAbilityManaBattery` samples
alongside repeated frame-time measurements; CPU sample percentages are not FPS.

## October 22 ARM64 per-unit ability-update dispatch

The RG40XX-H `cycles:ppp` capture after the MDX pose-reuse change samples
`monster_think -> S_RunAbilityUpdates` at 11.37% inclusive cycles, with a
3.45% branch into `shadowmeld_common` and repeated
`G_UnitAbilityLevel`/`G_HeroSkillLevel`/`actor_has_skill` lookups. These are
inclusive sample shares, not time savings or an FPS estimate.

`S_RunAbilityUpdates` still invokes the handlers in the original registration
order and runs `S_UpdateUnitPassiveEffects` unconditionally. It now uses
conservative per-tick negative guards for Shadow Meld (`Ashm`/`Ahid`),
Graveyard (`Agyd`) and Exhume (`Aexh`). Native ability strings and runtime
additions are checked without repeatedly entering hero/ability resolution;
any learned Hero ability is deliberately treated as a possible alias. The
original handler remains authoritative for removed, unavailable, inherited
or custom abilities. No persistent per-unit cache is stored, so runtime
ability changes, unit-type changes and save/load do not require invalidation.

The state-only callbacks for Raven Form, Ensnare, Blight Growth, Mana Flare
and Entangled Gold Mine are skipped when their required state is absent.
Other handlers remain unconditional because Root can initialize itself from
unit data and Disease Cloud resolves arbitrary aliases. Moon Well has a
separate custom-alias-aware guard; its effect lifecycle callbacks remain
active. Existing Shadow Meld state always reaches its handler; ability-disable
lifecycle events still own fade/Hide cleanup.

`tests/t_shadowmeld.c` covers absence, immediate runtime addition/removal,
authored-list changes and hero-rank ownership through the real dispatcher.
Existing Raven/Ensnare/corpse/mine tests cover the state-dependent paths.
After rebuilding on ARM64, compare the same scene's `shadowmeld_common`,
`G_UnitAbilityLevel` and `S_RunAbilityUpdates` samples and repeated median
frame times. The profiler percentages alone do not establish speedup.

## October 21 native CPU profile

An October 21 native CPU profile contains about 14K `cpu/cycles/P` samples and reports no lost samples. Its call graph puts 53.67% of sampled cycles under
`SV_Map`, including 47.87% under `G_LoadMap`. These are inclusive sampled-cycle shares, not wall-clock durations; parent
and child percentages overlap. The capture includes `SV_InitGame` as well as `SV_Map`, and does not record the map,
build mode, command line, elapsed time, or whether the run was a cold start or a map replacement. Use it to choose
measurements, not to estimate load seconds or claim a speedup.

The largest visible `G_LoadMap` branches are:

| Branch | Inclusive share | Profile detail |
|--------|----------------:|----------------|
| `G_SpawnEntities` | 16.18% | `SP_SpawnDestructable` is 10.17%; `G_LoadShadowTexture`/`G_FileExists` accounts for 6.12%, `M_LoadPathTex` 4.02%, and doodad model existence checks about 3.84%. |
| `CM_LoadMap` | 15.46% | `CM_ReadHeightmap` is 10.64%, mostly MPQ sector inflation (10.55%); `CM_ReadDoodads` is 3.62%. |
| `G_StartScripts` | 9.61% | Script execution creates units; `CreateUnit`/`SP_SpawnUnit` accounts for 7.85%, with unit sound registration visible beneath it. |
| `G_ApplyMapGameDataSet` | 6.39% | `InitUnitData` is 6.15%; SLK loading and typed conversion are prominent. |


## October 21, 2026: native map-loading investigation and plan

### Evidence and benchmark scope

The `OrcX01` log `openwarcraft3(20261021-134309).log` supplied for this
investigation contains six completed loads of
`Maps\FrozenThrone\Campaign\OrcX01.w3x` during **one Linux x86_64 application
session** (1600x1200, Intel Iris Plus/Mesa OpenGL). The first load includes
initial sound decoding; later loads use retained sound caches. These are not
independent cold starts. The log is an external capture, not a committed test
fixture; preserve its provenance when comparing future measurements. The log
was produced with the optional `DEBUG_MAP_LOAD=1` instrumentation, which
is available in this repository; regular builds compile the diagnostics out.
The clock and logging macros live in `common/map_load_debug.h` so client and
sound diagnostics do not depend on a game header or game-specific build flag.
Disabled logging still type-checks its arguments without evaluating them.

The client-load-cycle metric measures `CL_BeginLoadingMap` through client
refresh completion; it includes the server load and must **not** be added to
`G_LoadMap` or `CL_PrepRefresh` times. Timings below are elapsed wall time,
not inclusive `perf` CPU sample percentages.

| Load | Scenario as observed in log | Client load cycle | Server `G_LoadMap` | Client refresh |
|---|---|---:|---:|---:|
| 1 | Initial application map load | 25.427 s | 11.773 s | 13.305 s |
| 2 | Same-map restart | 15.631 s | 10.665 s | 4.795 s |
| 3 | New game initialization after returning to menu | 18.527 s | 13.569 s | 4.779 s |
| 4 | Same-map restart | 15.010 s | 10.291 s | 4.547 s |
| 5 | Same-map restart | 15.404 s | 10.578 s | 4.662 s |
| 6 | Same-map restart | 14.995 s | 10.488 s | 4.341 s |

The median cycle for loads 2–6 is **15.404 s**; the observed range is
14.995–18.527 s. Treat the menu-return run as a separate scenario for
future benchmarking: its script startup takes 2.638 s versus approximately
0.8–0.9 s for the later same-map restarts.

Representative medians for repeated loads (2–6) identify the next work:

| Measured phase | Median | Interpretation |
|---|---:|---|
| Server `CM_LoadMap` | 3.946 s | Heightmap and doodad reads dominate |
| Server heightmap reader | 2.668 s | Repeated small MPQ reads are a candidate |
| Server doodad reader | 0.969 s | Separate parsing target |
| `G_SpawnEntities` | 3.936 s | 8,724 spawned placements from 8,974 records |
| Map data setup | 1.701 s | Mostly `InitUnitData` |
| Renderer `RegisterMap` | 3.621 s | Renderer terrain parse dominates |
| Renderer terrain parse | 2.835 s | Independent terrain parse on every refresh |
| Model registration | 1.013 s | Repeatedly loads 298 models |
| Sound registration | 0.002 s | Reused decoded sounds; initial run was 3.931 s |

These are phase medians, not guaranteed additive components of any one run.
The server heightmap reader and renderer terrain parse together represent
roughly **5.5 s of separate repeated work**, before accounting for the
additional client `CM_LoadMap` pass (3.880 s on the first run). The initial
sound registration decoded 149 MP3s (2.957 s spent in MP3 decoding) and
read about 31.6 MB of source audio. Later restarts had 414 sound-cache hits
and no MP3 decoding, so sound work primarily affects the initial load.

Map placement summaries consistently report 8,974 placements, 8,724 spawned,
250 skipped as script-owned, 3,262 destructables and `units=0`. Validate
what the `units` counter actually counts before basing an optimization on it.
The repeatedly missing `Buildings\Orc\PigFarm\PigFarmWhat1.wav` is an
asset/data compatibility issue, not evidence of costly repeated decoding.
Snapshot delta-base warnings and unavailable sound recipient messages should
be investigated separately from load-time changes.

### Revised implementation sequence

Make one measurable change at a time; preserve RoC/TFT file semantics, map
archive priority and replacement cleanup, and keep generic engine modules
independent of WC3-specific headers. The sequence below prioritizes changes
that improve *repeated* loads and are relatively self-contained; re-rank
based on new measured results rather than treating the order as fixed.

0. **Validate the committed opt-in diagnostics and establish comparable
   baselines.** The phase instrumentation is present behind
   `DEBUG_MAP_LOAD=1`; keep it out of regular builds and verify consistent
   begin/end boundaries. Document the map, data set, revision, compiler flags,
   machine, command line, and whether each run is an initial start, same-map
   restart, or menu-return new initialization. Use at least five runs per
   *scenario*, report medians and spread, and record memory high-water marks.
   Do not mistake warmed filesystem caches for true cold starts.
1. **Bulk-decode W3E terrain vertices (implemented).** The common world
   loader and WC3 renderer now use the bounded reader in
   `games/warcraft-3/common/war3map_vertices.h`, decoding seven-byte records
   in batches of up to 4,096 vertices. The previous five reads per vertex
   meant about 186,245 calls per parse for OrcX01's 193x193 grid. Tests cover
   packed values, dimensions, truncation, and allocation bounds. Compare server heightmap,
   client heightmap when present, renderer terrain parse, and end-to-end
   cycle times; verify cliffs, ramps, water, blight, pathing and minimap.
2. **Eliminate redundant whole-file existence reads during spawning (implemented; benchmark pending).** The game now probes `G_FileExists` and
   `SP_DoodadModelExists` through `game_import.FileExists`/`FS_FileExists`
   rather than reading/freeing each full asset. Preserve archive precedence,
   verify missing-variation and shadow fallback, and remeasure spawn time
   and memory/I/O. Presence does not establish successful decoding.
3. **Index SLK typed-conversion lookups.** Investigate per-table row/column
   indexes and seen-row-ID handling, preserving case rules, duplicate IDs,
   row precedence, custom map overrides and RoC/TFT schema differences.
   Compare both process initialization and per-map `InitUnitData` timing.
4. **Reduce repeated spawn-time asset-resolution work.** Measure shadow
   names, path textures, and model candidate resolution separately before
   adding bounded map-scoped caches. Define ownership, invalidation and
   teardown on restart or map replacement; keep the original resolution
   order and behavior.
5. **Optimize doodad readers if still material.** Profile the nearly one
   second server doodad parse independently; preserve placement order,
   malformed-input rejection and all map-object fields. Evaluate any
   duplicated client reader only where it actually runs.
6. **Improve first-load audio cost.** Profile `minimp3` decode, allocations,
   resampling and reads before considering parallel decoding. Keep sound
   registration readiness and cache lifetime semantics; avoid regressing the
   approximately two-millisecond repeat-load registration path. Compare
   process-first-load medians, not only restart timings.
7. **Investigate renderer terrain construction.** With bulk W3E decoding
   in place, profile segment construction, ground layers and cliffs;
   optimize only material residual costs while preserving GPU-resource
   cleanup and visual equivalence across map transitions.
8. **Consider sharing parsed terrain only if needed.** The server/client
   common reader and renderer have distinct consumers and lifetimes. Share
   an immutable representation only after proving that it respects module
   boundaries, archive ownership, map replacement and error paths. Otherwise
   keep independently optimized decoders; a first-run-only client reader is
   not evidence of a repeated-load bottleneck.
9. **Reassess models and remaining work.** Investigate approximately one
   second of repeated model registration, including portrait probes and GPU
   uploads. Re-profile `G_StartScripts`, spawn dispatch and residual server
   work; prioritize only what the new end-to-end profiles justify.


### October 21 follow-up: W3E result and spawn asset probes

The subsequent instrumented OrcX01 first-load capture
`openwarcraft3(20261021-143458).log` (external user log) observed
**17.896 s** for the full client load cycle, versus **25.427 s** for the
previous unoptimized first load. These are two separate runs, not controlled
medians. Client W3E heightmap reading decreased from 2,629.035 ms to
**2.567 ms** and renderer W3E parsing from 2,597.571 ms to **5.469 ms**.
The combined client `CM_LoadMap` phase decreased from 3,879.519 ms to
1,227.635 ms, and renderer `RegisterMap` from 3,577.415 ms to 1,025.508
ms. The newer build did not print `G_LoadMap` server diagnostics: **do not
claim measured server-phase gains from this capture**. Visual/pathing checks
and repeated-run medians remain necessary.

The next focused optimization replaces only two server game-side
**read-and-free existence probes**, leaving actual asset registration and
loader ownership intact:

| Game call | Previously | Now | Selection behavior |
|---|---|---|---|
| `G_FileExists` (`g_monster.c`) | `gi.ReadFile` then `gi.MemFree` | `gi.FileExists` | Try shadow `.blp` first, optional `.dds`, then original default shadow fallback |
| `SP_DoodadModelExists` (`g_spawn.c`) | `gi.ReadFile` then `gi.MemFree` | `gi.FileExists` | Probe variation `N.mdx` only when `numVar > 1`; fall back to unsuffixed `.mdx` when absent |

`server/sv_game.c` wires `game_import.FileExists` directly to the existing
`FS_FileExists` in `common/common.c`. That VFS service checks the current
priority map MPQ first, then mounted archives, nested/loose assets and game
data directories as available. It does not read or allocate an entire asset
just to decide whether to register its name. No cached existence results
are retained across map changes, avoiding stale map-import decisions.
Because the shared `game_import` structure gains a callback, rebuild/relink
the server and all game modules together. The universal client is unchanged.

**Existence is not decode success.** `FS_FileExists` intentionally answers
whether a path is present, not whether an archive entry decompresses or a
model/image parses. A corrupt present variation could therefore pass the
probe where the old `ReadFile` test would have failed. The asset loader is
still responsible for errors and fallback policy; this is a compatibility
edge case to check with malformed MPQs rather than asserting perfect
read-success equivalence. The normal **missing-file** fallback order is
unchanged.

Focused tests: `wc3_spawn.shadow_texture_probes_blp_then_optional_dds_without_full_file_reads`,
`wc3_slk.doodad_model_uses_file_stem_and_only_appends_real_variations`,
`wc3_slk.doodad_model_missing_variation_falls_back_to_unsuffixed_asset`,
`wc3_slk.map_w3d_original_variations_and_custom_inheritance`,
`wc3_unit.death_sound_uses_existing_numbered_asset`,
`wc3_unit.death_sound_uses_existing_unnumbered_asset`, and
`commands.priority_archive_file_exists_tracks_map_mount`. The existing
asset-resolution tests now mock `gi.FileExists` rather than `gi.ReadFile`.
They cover path selection and priority-mount teardown without requiring
retail assets. Run `make test` and the affected native game/server builds,
then compare matched OrcX01 `spawn-entities` and whole loading-cycle
medians for first load, restart, and menu-return cases. The later capture
below provides an initial, uncontrolled spawn comparison; it is not enough
to establish a speedup. If spawning remains dominant, profile repeated
shadow names, model candidates, and path textures before introducing any
map-scoped caches.

### October 21 follow-up: repeated OrcX01 loads

The later `openwarcraft3.log` capture contains six completed loads of the same
OrcX01 map in one application session. It does not record the executable
revision, build flags, machine details, or exact restart scenario for each
load. Treat it as directional evidence, not a controlled comparison. The
first load includes decoding 149 MP3s; the five subsequent loads reuse the
sound cache.

| Load | Client load cycle | Server `G_LoadMap` | Client refresh |
|---|---:|---:|---:|
| Initial | 16.822 s | 8.617 s | 7.858 s |
| Warm 1 | 9.731 s | 7.927 s | 1.638 s |
| Warm 2 | 9.059 s | 7.234 s | 1.661 s |
| Warm 3 | 8.971 s | 7.167 s | 1.641 s |
| Warm 4 | 9.094 s | 7.279 s | 1.653 s |
| Warm 5 | 9.262 s | 7.424 s | 1.677 s |

Warm-load medians are **9.094 s** for the client cycle, **7.279 s** for server
`G_LoadMap`, and **1.653 s** for client refresh. The initial client cycle is
1.074 s faster than the preceding 17.896 s capture in the W3E follow-up, but
the runs are not controlled and this difference should not be attributed to a
specific change. The current server `CM_LoadMap` is about 1.2–1.3 s and
renderer map registration about 0.7–1.0 s. W3E reads remain in the low
millisecond range, consistent with the earlier terrain measurements.

`G_SpawnEntities` is 3.745 s on the initial load and 3.764–4.081 s on warm
loads (median **3.814 s**). This is close to the earlier repeated-load median
of 3.936 s and does not demonstrate a spawn-probe speedup. Initial sound
registration takes 3.810 s, including 2.832 s of MP3 decoding; warm sound
registration takes about 0.6 ms with 414 cache hits. The missing Pig Farm
sound and empty LOS-blocker model fallback recur. The log also contains
unsupported retained-field warnings for several splats and a failed read for
`Sound\\Units\\Combat\\`; none prevents the map from loading. Investigate
these asset/rendering messages separately from load-time measurements.

### Acceptance and measurement gates

Before each change, establish a focused reproducer that calls the production
path and demonstrates the old behavior or excessive work. Use minimal MPQ
fixtures under `games/warcraft-3/tests/resources-src`, not local retail data.
Test valid and truncated W3E, invalid dimensions, RoC/TFT differences,
archive precedence, duplicate/replaced map state, and any affected renderer
or gameplay output. Report which visual properties still require a bounded
manual check. For code changes, run affected builds and `make test` per
`CONTRIBUTING.md`; documentation-only updates need text, link and
`git diff --check` validation.

Compare at least five controlled runs for each relevant scenario, reporting
end-to-end load-cycle medians and spread alongside phase medians, memory and
I/O. A phase improvement alone is not a demonstrated user-visible speedup.
If the overall gain is within ordinary run-to-run variation, or correctness
regresses, do not merge the optimization. Keep temporary counters behind
opt-in diagnostic/test mechanisms and remove investigative logging before
committing release changes.

## September 21 CPU profile: Graveyard update scan

Inspection of `build/perf-full.txt` found about 7K `cpu/cycles/P` samples, no lost samples,
and 89.30% inclusive cost under `SV_Frame`. These are sampled CPU-cycle shares, not wall-time or GPU measurements;
inclusive parent/child percentages overlap. The report does not identify the capture command, map, or build revision.

`graveyard_find_thinker` accounts for 74.76% self / 75.28% inclusive in the historical capture. The confirmed code path is
`monster_think -> S_RunAbilityUpdates -> CAbilityGraveyard -> graveyard_ensure -> graveyard_find_thinker`.
`S_RunAbilityUpdates` dispatches every registered update procedure for each unit. The current
`skills/s_undead_abilities.c` implementation checks `G_UnitAbilityLevel(..., Agyd)` before scanning
`globals.num_edicts`, so ordinary living units do not perform that fruitless full scan. `git blame`
attributes the original ordering to `b8deb304` (`wc3: implement corpse mechanics`); the correction is
covered by the current branch's Graveyard tests.

The first two recommendations below are already implemented on the current branch:

1. `graveyard_ensure` checks `G_UnitAbilityLevel(..., Agyd)` before scanning for its thinker,
   so ordinary units do not pay the full edict scan. The focused Graveyard and corpse tests cover
   duplicate updates, ability removal/re-add, death and save/load.
2. `InitAbilities` builds a procedure-to-first-registry-index cache for `GetAbilityIndex`, preserving
   duplicate-procedure first-match behavior and the 255 miss sentinel.

Remaining implementation sequence:

1. Re-profile the corrected Graveyard path through real `S_RunAbilityUpdates` dispatch with many
   non-Agyd units and increasing edict counts, including freed slots. Keep behavior in `CAbilityGraveyard`;
   retain corpse production timing, cap, radii and
   saveable thinker ownership. Extend `tests/t_exhume.c` coverage for duplicate updates, ability removal/re-add,
   death and save/load. Measure real Graveyard counts before introducing a persistent thinker index.
2. Re-profile the same fixed scene with matching build mode, camera, entity population and simulation interval.
   Record absolute server/frame timings and repeated-run medians. The existing `wc3_perf.run_entities_1900`
   benchmark is a starting point; verify that its fixture actually enters `monster_think` and ability updates.
   Removing all current lookup self cost has an idealized fixed-work CPU speedup ceiling of about 4x,
   not a predicted FPS gain.
3. `R_ConformGroundSurfaces` candidate gathering was implemented by patch sixteen (see above).
   Historical captures measured 1.76% self / 2.19% inclusive in one scene and 11.01% self /
   11.12% inclusive in `screenshots/2026-09-25/fixed4/perf-full-Orc04.txt` (about 3K
   sampled CPU cycles, no lost samples). These were **different maps**, not an A/B test.
   The new bounded per-view index removes repeated full entity scans in the common case.
   Follow up with identical Orc04 scenes and repeated median frame times; consider spatial
   filtering only if exact MDX trace tests still dominate after this change.
4. `MDLX_BindBoneMatrices` is 2.01% inclusive, including geometry and attachment collection. Investigate
   sharing the already evaluated entity pose with `MDLX_CollectAttachmentPositions`; global scratch matrices
   can be overwritten by another model, so reuse requires explicit lifetime/identity. Preserve interpolation,
   billboard/view dependencies and portrait/world separation. Do not begin with SIMD or interpolation changes.
5. `G_BlightPackRows` is 0.86% inclusive. `G_BlightWriteDatagram` retries encoding with one fewer row on
   overflow. Measure retry counts and patterns before selecting bounded chunk sizing or incremental packing;
   preserve dirty-row delivery, periodic sweeps and the existing wire encoding. The profile alone does not
   prove retries dominate this cost.

Run the baseline `make test` before implementation, reproduce each regression before its fix, and use focused
`wc3_spell.graveyard*`, corpse/save tests and `wc3_perf.*` comparisons for the first change. Subsequent optimizations
should be selected from the new profile, since the dominant scan currently suppresses their relative shares.
See [corpse mechanics](corpse-mechanics.md) for the authored Graveyard contract.

## October 22 ARM64 aura-family classification and ownership filtering

The nine-patch ARM64 `perf` capture attributes about 5.68% inclusive sampled
CPU cycles to `hero_aura_bonus` and the `regen_aura_cache_update` path. Four
instructions inside provider discovery carry about 2.9% exclusive samples in
total, though those samples do not establish how much time can be saved.

The provider snapshot already scans each unit's ability sources only once per
frame. Its next optimisation classifies each four-character rawcode against
the 14 aura families through a shared 256-entry direct-mapped cache tagged
with `G_AbilityDataGeneration()`. Both hits and misses (including non-aura
abilities) avoid repeated `G_AbilityCode` resolution across units and frames.
The family bitmask preserves an unusual but valid case where a rawcode matches
one aura directly and a different aura through its authored parent. The
native and runtime-added `G_ActorHasSkill` lookup is now performed only for
rawcodes that could provide an aura; owner removal, hero rank, and morph data
remain live. The 14 authored combat keys have a direct family-slot lookup
instead of each key linearly searching all family codes on every provider.

The existing per-frame provider cache and aura effect lifecycle remain
unchanged. `wc3_spell.aura_classification_cache_*` regression tests cover
positive/negative reuse, live ownership, SLK generation invalidation,
collisions and multi-family aliases. Compare this path on the same ARM64 scene
alongside measured frame times after compiling; CPU-cycle percentages are not
expected FPS improvements. See [Aura targets and overlays](aura-targets-and-overlays.md).

## Hero aura presentation cache

An earlier full profile assigned 41.62% of sampled CPU cycles to
`S_UpdateHeroAuraEffects`, with `hero_aura_presentation` scanning the edict list
and reparsing source ability lists for each recipient. The per-frame provider cache
now resolves Devotion and Unholy providers alongside the regeneration families;
presentation still checks live range, alliance, and target rules per recipient, but
iterates cached providers. Cache validity uses the `UINT_MAX` reset sentinel, so
frame zero does not rebuild the provider list for every entity.

The in-engine 1,900-unit benchmark measured `G_RunEntities` at 2,394.48 ms/call
while frame-zero cache invalidation rebuilt the list per entity. After the generation
invalidation and provider reuse it measured 2.27 ms/call in the same debug test
binary. This benchmark is a regression signal for cache invalidation and scaling;
it is not an end-to-end frame-rate claim. Aura presentation tests drive
`G_RunEntities` at frame zero and after a timed range change so the scheduler path
and cache reset contract remain covered.

The September 23 `build/perf-full.txt` capture exposed a separate mechanical path:
`S_UnholyHealthRegen -> hero_aura_bonus -> actor_aura_ability` accounts for 72.99%
inclusive sampled cycles. Each recipient refresh scanned all edicts and resolved all
combat aura aliases for every active friendly unit. `regen_aura_cache_update` now
caches those alias/rank references with the other aura providers, so a shared source
pass replaces the repeated recipient-by-edict alias resolution. Each recipient still
checks live source activity, alliance, range, and authored target masks; its numeric
cache keeps the existing two-second refresh and invalidates on AbilityData generation
changes. `wc3_spell.combat_aura_alias_resolution_scales_with_edicts` reproduces the
old resolver-call growth with 32 aura sources and 96 recipients, then checks the
linear edict-bound pass and runtime ability removal/re-add. The stored profile has
only about 4K CPU samples and no scene/build metadata; take a new weighted profile to
measure the post-fix cycle share.

## October 23 ARM64 regeneration-aura combined cache traversal

The ARM64 profile after MDX keytrack fast paths still samples regeneration-aura
provider processing during simulation. `regen_aura_cache_update` previously
walked every allocated edict twice on a cache miss: first to resolve active
provider abilities, then to rebuild the recipient and caster presentation-effect
slots. Those scans now share **one traversal** while retaining increasing-edict
provider precedence and first-match overlay precedence. Entities without any
unit-state ability storage or authored `abilList` are skipped by the provider
resolver but remain eligible for the overlay index (for example, attached
spell effects). No new persistent indexing or save data is introduced.

The provider and overlay indexes are still recomputed on simulation-frame or
`G_AbilityDataGeneration()` changes. Recipient distances, alliances, target
masks and immediate active/invisible/death checks remain on the existing live
paths; the two-second numeric/presentation refresh cadence is unchanged.
`wc3_spell.regen_aura_cache_combines_provider_and_overlay_scans` verifies one
edict pass per cache refresh, same-frame reuse, source/recipient artwork role
separation and effect replacement on the next frame. The companion generation
test covers same-frame metadata changes. These tests also exercise the current
heap-owned `unit_state` rank and added-ability layout; no new debug output is
emitted. CPU or FPS impact must be assessed on a comparable ARM64 capture.

Profile-driven optimizations across the renderer, client, and server. The five sampled hot spots and the fixes applied to each are listed below so a future reader understands *why* each path is shaped the way it is.

## Client frame-rate limiter

OpenWarcraft3 registers the archived `com_maxfps` cvar with a default of `64`, matching the intended classic-Warcraft presentation target. `0` disables the limiter. The same engine main loop is shared with the other game binaries, but their built-in default remains `0` unless their own configuration overrides it.

The limiter lives in `common/main.c` and applies only to non-dedicated clients. It measures the complete main-loop iteration with SDL's performance counter, including simulation, client work, rendering, and any VSync blocking, then sleeps/yields only for the remaining part of the target frame interval. It therefore does **not** change `FRAMETIME`, server tick accounting, animation/game timers, or `com_fast_forward`; if a frame already takes longer than the cap interval, no extra delay is added. `r_vsync` can still impose a lower effective frame rate than `com_maxfps`.

Keep `com_maxfps` separate from `com_frame_limit`: the latter is a diagnostic auto-quit counter for main-loop iterations. Useful commands are:

```bash
# Default Warcraft III cap: 64 FPS
build/bin/openwarcraft3 -data 'data/Warcraft III' +map 'Maps/Campaign/Human02.w3m'

# Uncapped rendering/performance measurement
build/bin/openwarcraft3 -data 'data/Warcraft III' +set com_maxfps 0 +set r_vsync 0 +map 'Maps/Campaign/Human02.w3m'

# Explicit alternate cap
build/bin/openwarcraft3 -data 'data/Warcraft III' +set com_maxfps 120 +map 'Maps/Campaign/Human02.w3m'
```

## MDX per-pose sequence resolution (patch twenty-four, x86 low-performance cores)

The x86 Human02, NightElf07 and Undead07 profiles continued to attribute
exclusive cycles to `MDLX_GetModelKeytrackValue` and `R_KeyFrameBound` after
keytrack binary interval-bound optimisations. While binding one bone pose,
all nodes sample the same current and previous animation times; previously
*each* animated translation/rotation/scale track independently called
`R_FindSequenceAtTime`, linearly searching the model's authored sequences.

`MDLX_BindBoneMatrices` now provides a **stack-local, two-sample sequence
selection** shared by all node tracks for that bind. It resolves each unique
sample on its first non-global keytrack and reuses the identity for the other
tracks, reducing up to six or more repeated sequence-list walks per node to
one or two walks per complete pose. Global-sequence-only tracks do not cause
a regular sequence lookup. This does not cache results across models,
attachments, animation samples, frame changes or reloads. The public
`MDLX_GetModelKeytrackValue` remains stateless, and signed authored key times,
sequence boundaries, interpolation and wrap-around remain unchanged.

`renderer_model.mdx_bone_pose_resolves_each_sequence_once_per_bind` covers
multiple animated bones, single vs dual sample times, transform results and
in-place sequence edits on the next bind; a second regression checks that
global-only tracks do not perform irrelevant sequence lookups. The
instrumentation is under `BZ_TESTS` only. The native build passed; tests were
not run. Compare matched-scene x86 frame times and ARM64 validation before
attributing a concrete FPS improvement.

## Terrain splat clipped-vertex UV generation (patch twenty-seven, low-core x86)

The Human02 x86 profile identifies `R_MakeSplatTile` as a 3.08% exclusive
sampling hotspot. The previous clipping path built six full-tile vertices and
remapped their atlas UVs before determining whether those UVs would survive
clipping. Partially clipped tiles then recomputed UVs from final world-space
positions. The updated path samples four terrain positions once, checks
whether the tile is fully inside the splat rectangle, and builds/remaps its
quad only in that case. Partial tiles go directly through the original
Sutherland-Hodgman world-XY clipping path, preserving left/right/bottom/top
clip order and terrain triangle order; emitted positions keep the original
interpolation, and UVs come from final world coordinates.

Whole tiles transform four unique UV corners and share them between the two
triangles. Blight tile generation still uses the original full-quad helper.
Batching, caching, tint, terrain deformation and GL upload behavior are
unchanged. `renderer_terrain.splat_tile_deferred_uv_matches_original_across_clipping`
compares vertices to the previous algorithm across edge clips, a four-edge
clip, exact boundaries, varied terrain heights, tint and atlas UV ranges.
The native build passed; tests were not run. Measure matched Human02 x86 scenes
and validate ARM64 separately before attributing a frame-time improvement.

## MDX keytrack and ground splat hot paths

An Instruments capture after the 64 FPS cap was added sampled `R_KeyFrameBound`
for 321 ms and `R_ClipSplatPoly` for 273 ms over 9.23 seconds. These are useful
CPU targets, but changing them cannot raise FPS while `com_maxfps` remains 64.

`MDLX_GetModelKeytrackValue` searches three bounds in a packed keytrack. The
keyframe stride depends only on the track's data and interpolation types, so
compute it once per evaluation and pass it to each search and key access.
`R_MakeSplatTile` builds an axis-aligned terrain tile. Only rectangle edges
inside that tile can cut its two triangles; pass those edges to
`R_ClipSplatPoly` in the original left/right/bottom/top order. Keep Z
interpolation on the original terrain triangles.

A local `BUILD=debug` headless microbenchmark with a 32-key linear track and
one tile measured 140 to 93 ns per keytrack evaluation, 443 to 276 ns for a
single-edge splat, and 509 to 504 ns for a four-edge splat. These measurements
cover the CPU entry points only; they do not predict full-frame FPS. Use
`make test-renderer-model` for keytrack sequence and partial-tile geometry
coverage, then profile an uncapped run before pursuing another change.

### MDX interval-bound fast paths (patch seventeen, ARM64)

`MDLX_GetModelKeytrackValue` still honours the selected MDX sequence and
uses the entity-scoped render clock for global sequences. Constant
(single-key) tracks can now return directly **after** their sequence interval
has been validated. Multi-key tracks whose authored first and last keys are
already within the active interval skip both binary searches for interval
bounds. Other tracks retain binary bounds but restrict the interpolation-key
search to the in-sequence slice; two-key slices avoid that search as well.
The old interpolation routines (linear, step, Hermite, Bezier and quaternion)
remain unchanged; so do tail wrapping, the special DNC global-sequence key
and old/current pose blending. There is no cross-instance or cross-frame
cache to invalidate during reloads or animation transitions.

Headless `renderer_model.mdx_*` regressions exercise constant-key sequence
ownership, first-key clamping, exact keys, interpolation, interval-tail wrap,
step tracks, cross-sequence exclusion and global pause state. The patch only
reduces key lookup work: the 0.93% exclusive hotspot in a 7K-sample handheld
profile is motivation, **not** evidence of a measured improvement. Benchmark
matched scenes after building and running the tests.

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

## Lumber order routing hitch (August 2026)

A weighted ARM `perf report` captured while ordering a Peasant to harvest a tree resolved the server hot path through `ai_walktree -> unit_changeangle_for_radius -> M_RefreshHeatmap -> CM_BuildHeatmapForRadius`. Roughly half of that route-build cost was `build_heatmap()` repeatedly expanding the mover footprint, and the other half was `bake_flow_field()` computing vectors for every reachable cell even though the Peasant samples only its current location.

The route now keeps three performance boundaries:

1. Harvest first searches only the small `HARVEST_RANGE` interaction disc for a collision-safe point with a direct static line from the worker. Reachable trees therefore steer to the interaction area without building a whole-map field.
2. Cached routes store integration prices only. `get_flow_direction()` computes the four local samples needed for interpolation on demand; there is no whole-map flow-vector bake on the order path.
3. Radius-expanded static walkability uses a summed-area table over baked `nowalk` cells, making each square footprint query O(1) during a genuine heatmap flood.

The unreachable-interior-tree behavior remains unchanged: when no direct harvest-range point is visible, the collision-sized integration field still drives the worker to the forest edge and exposes route completion/unreachability to Harvest, which owns retargeting.

## Verification

```bash
make test                    # full umbrella, incl. in-engine WC3 suites
make test-wc3-engine         # 344 tests / 918 assertions, incl. wc3_perf.run_entities_1900
make -j4 openwarcraft3 openwow opensc2   # all three renderers still build
build/bin/openwarcraft3 -data 'data/Warcraft III' +menu_main +screenshot 10 +com_frame_limit 20       # ROC
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +menu_main +com_frame_limit 20                 # TFT
```

The `wc3_perf.run_entities_1900` benchmark (`games/warcraft-3/game/tests/t_game.c`) measures `G_RunEntities` over 1900 active units.

## Static-scenery snapshot saturation (RG40xx report)

Commit `215d9630` classified doodads/destructibles correctly but made `G_FowPlayerCanSeeEntity` return true for every
`SVF_STATIC_SCENERY` entity, including unexplored cells. A bounded ROC Human02 run measured 2,430 edicts, of which 2,299
were static scenery; all 2,299 passed visibility and filled the 1,024-entity snapshot cap on every sampled server frame.
This explained the report's simultaneous tree-pop improvement, unexplored waterfall rendering, and CPU-bound FPS drop.
The accompanying [post-regression ARM address map](https://gist.github.com/sookyboo/0fc1e06966e3b677e22e8ff7c1f0edc0)
repeatedly resolves through `SV_AddVisibleEntityCandidate` / `SV_BuildClientFrame`, corroborating the measured path. It is
an `addr2line` mapping without sample counts, so do not derive percentages from repetitions in that text.
The reporter's [22 FPS baseline mapping](https://gist.github.com/sookyboo/b9ac1cc2657ad46209644878fab777b0)
contains the expected MDX geoset, entity-shadow, simulation, and fog-update stacks but no server snapshot/candidate stack.
The later capture adding that path is qualitatively consistent with the scenery regression; neither mapping contains the
underlying sample weights, so this comparison identifies a new path but cannot quantify its frame-time share.

Static scenery now follows the explored plane, like buildings: it does not pop when current sight leaves, but unexplored
scenery neither consumes snapshot candidates nor renders through black fog. Diagnose future regressions by temporarily
counting total and visible `SVF_STATIC_SCENERY` entities in `SV_BuildClientFrame` during a bounded Human02 run; remove the
counter after capturing the result.

### PortMaster building-cache claim audit

[Commit `f98c70c9`](https://github.com/corepunch/open-realm/commit/f98c70c94a3d01a784f5d6c9492ad50338e97199)
reports a Human02 increase from about 20 to 25 FPS after adding `G_FowUnitIsBuilding`. The submitted helper is never
called: `G_FowPlayerCanSeeEntity` still expands `UNIT_IS_BUILDING(ent->class_id)` directly, and the only executed change
is `g_fow_building_cache_count = 0` once during `G_FowInit`. Release `-O2` may remove the unused static helper entirely;
debug builds may retain it and shift code addresses, but neither case supplies a credible 20% frame-time reduction.

The intended optimization became unnecessary after typed-row binding. `G_BindEntityData` caches each immutable SLK row
on the edict, and `UnitMetaBoolean`/`UnitMetaString` resolve reflected fields through a compile-time FOURCC descriptor and
two offsets. Hot visibility code reads `ent->runtime.flags` directly. Test further changes separately from scene
progression, thermal/DVFS state, controller-helper changes, and build-mode changes; Human02 draw counts change as the
cinematic and entity population advance, so two instantaneous `r_stats` readings are not an A/B benchmark.

### PortMaster post-fix address-map audit

The [post-fix Human02 gist](https://gist.github.com/sookyboo/301df705f27a9ead75dcca9204ab7893) is three
`addr2line` outputs, not the sampled profile that produced the addresses. It contains symbols from FOW, snapshots, AI,
MDX animation/geosets, splats, particles, UI layout, minimap, text, and network parsing, but no sample counts,
percentages, timestamps, shared-library/GL-driver frames, or call tree. Repetition in the text is not a weight: use it
only to identify code that was reachable. Obtain `perf report --stdio` plus `perf script` or folded stacks from a fixed
Human02 interval before choosing work from this capture.

The best candidates based on scaling and current code, pending weighted data, are:

1. Make FOW work dirty-driven. `G_FowUpdate` currently hashes every blocker, clears every visible cell, and scans every
   edict every 100 ms. Track blocker/revealer cell changes and sight/alliance/modifier changes so a stationary world can
   skip blocker rebuilding and shadowcasts; clear only cells touched by the previous visible generation.
2. Stop scanning all static scenery for every client snapshot. Human02 has about 2,299 static-scenery edicts. Bucket
   them by FOW cell and add newly explored buckets to a per-client visible set, while retaining the ordinary dynamic
   entity path and snapshot delta contract.
3. Cache immutable unit metadata at spawn. `G_AcquisitionRange` and building visibility still reach linear SLK lookup
   paths. Extend the existing per-unit balance data for hot class values instead of querying `UNIT_*` macros during AI
   acquisition or snapshot construction.
4. Cache decoded client layout geometry until its payload or viewport changes, and preserve precomputed draw-order
   lists. `SCR_DrawLayout` calls `SCR_Clear` for every visible layer every frame; that function clears the full frame
   arrays, decodes the wire payload, and resolves layout again before three frame scans.
5. Batch adjacent UI quads by texture/shader/blend/clip. `R_DrawImageEx` currently reaches `glBufferData` and
   `glDrawArrays` for each image. This is a plausible GL4ES/handheld driver cost even when desktop CPU profiles make it
   look small. Text already has an atlas batch path that demonstrates the appropriate boundary.

MDX pose sharing and splat topology caching are secondary measurement candidates. Harvest routing is separately covered by the weighted tree-order profile above. The
local Human02 pose-key audit found little identical-pose reuse; disabling shadows barely changed desktop FPS; and
`get_flow_direction` itself only scans four generations and performs a bilinear interpolation. Do not trade animation,
terrain-conforming shadows, or movement quality for these without weighted evidence. First isolate the handheld run
with `r_norefresh`, then A/B `r_entities`, `r_unit_shadows`, `r_particles`, and `r_fogofwar` over the same scripted time
window; a large `r_norefresh` gain implicates rendering/driver cost, while a low no-refresh rate implicates game/FOW/
snapshot work.

### Cutscene snapshot and MDX report audit (August 2026)

The reported `SV_SendClientDatagram` subtree includes both `SV_BuildClientFrame` and `SV_WriteFrameToClient`; it does not
isolate wire serialization. The builder's overflow policy was an actual scaling defect: after filling the 1,024-entity
budget, every additional visible entity scanned all retained candidates to find the farthest one. The candidate set is
now a bounded max-heap, reducing selection from O(E*K) to O(E*log K), followed by the same entity-number sort required
by delta encoding. A server test feeds farther entities first, forces nearer replacements after the heap is full, and
checks the final wire order.

Unchanged entity deltas now compare the contiguous `entityState_t` first. Exact matches skip the descriptor-table walk;
changed or forced states retain field-granular encoding and the existing wire format. This benefits local and remote
clients equally. ioquake3's `SV_SendClientSnapshot` does not bypass serialization for loopback clients: it exempts
loopback from rate limiting, while only bots consume snapshots without transmission. Keep Open Realm's one snapshot
contract too; a direct-pointer loopback path would hide remote-client cost and create a second state-delivery path.

WC3 MDX key tracks were already contiguous file-shaped allocations, so repacking was not the prerequisite claimed by
the animation report. The measured defect was lookup: each sample scanned the complete packed track once for sequence
bounds and again for its interpolation pair. `MDLX_GetModelKeytrackValue` now uses binary lower/upper bounds over the
existing variable stride. Tests preserve exact-key sampling, pre-first-key clamping, interpolation, interval-tail wrap,
and exclusion of adjacent-sequence keys.

Do not apply the remaining proposals without a weighted same-scene A/B capture:

1. `nlerp` changes authored rotation timing, while Hermite/Bezier quaternion tracks require spherical quadrangle
   interpolation. Add a visual corpus and angular-error threshold before changing either the key-track interpolation or
   the separate old-frame/current-frame render blend.
2. MDX, M2, and M3 have separate loaders, animation clocks, and render paths. A shared SoA runtime is a cross-game
   architecture change, not an MDX load-time cleanup; measure each path before replacing its file-shaped representation.
3. NEON is useful only after a scalar batch with four independent bones exists. Hierarchy concatenation is parent
   dependent, and the current global matrix cache is indexed by sparse authored node IDs. Benchmark a scalar packed-pose
   prototype before adding an architecture-specific kernel.
4. Spawn/damage staggering changes JASS-visible timing and ordering. Shadow splats are already batched, and the local
   Human02 A/B above found negligible FPS change from disabling unit shadows. Neither change is justified by an address
   map that only proves reachability.

Re-profile a fixed Human02 interval with weighted stacks after these changes. Keep `SV_BuildClientFrame`,
`MSG_WriteDeltaEntity`, `MDLX_GetModelKeytrackValue`, and `Quaternion_slerp` separate in the report; only then choose
between spatial snapshot indexing, animation cursor state, pose packing, or interpolation approximation.

### Implemented follow-up: sparse FOW clears and spawn-cached unit fields

A temporary bounded Human02 diagnostic at the old `G_FowClearVisible` loop confirmed that each connected player
scanned all 65,536 cells of the 256x256 FOW grid although only 2,569 cells were visible in the sampled updates. The
player grid now records which rows contain visible cells. The next update scans 256 row flags and only `memset`s rows
that were actually populated; it still marks those rows dirty so the existing network delta contract is unchanged.
The row-occupied state is allocated, reset, and freed with the other per-player FOW planes.

Immutable unit acquisition range and building classification are now resolved by `SP_SpawnUnit`. AI acquisition and
FOW/snapshot visibility read the cached values instead of repeatedly walking unit metadata/SLK tables. Acquisition
range retains the old default-to-half-day-sight and cap-to-day-sight behavior. Building classification retains the
explored-plane rule, so buildings stay shrouded after current vision leaves while ordinary units disappear.

Clean detached worktrees at `d1a60ff4`, both including the waterfall particle-FOW shader change, were built with
`BUILD=release`. Five independent runs produced these medians on Apple M1 arm64:

| In-engine benchmark | Before | After | Change |
|---|---:|---:|---:|
| `G_FowUpdate`, 256x256 grid, 160 revealers, 2 players | 0.20 ms/update | 0.14 ms/update | 30% less time |
| `G_AcquisitionRange`, 1,900 units x 10 passes | 12.02 ms | 0.02 ms | 99.8% less time |

Run the comparison with:

```sh
make -j4 BUILD=release build/bin/openwarcraft3-tests
for run in 1 2 3 4 5; do
    build/bin/openwarcraft3-tests -data build/tests/wc3-engine-data -tft +dedicated 1 +test 'wc3_perf.*'
done
```

These are subsystem timings, not a claimed handheld FPS increase. A 1,200-frame release Human02 render A/B changed
draw counts throughout the cinematic and showed run-order/thermal noise larger than the expected server saving, so it
was not treated as an end-to-end result. Repeat the fixed-scene weighted profile on the reported ARM/gl4es device to
measure the actual frame-rate effect; renderer submission still dominates the available weighted profile.

### Contributor FOW follow-up audit

The non-PortMaster FOW commits from `sookyboo/portmaster_rebase_28_08_2026_2` were audited individually against current
main. `ffcb57ca` (replace the visible-cell loop with `memchr`/`memset`) is obsolete: the `visible_rows` implementation
above avoids scanning empty rows altogether. The dirty-blocker idea from `722469c8` and rim-cell list from `e279778d`
remain useful, but were reapplied rather than cherry-picked because current destructable lifecycle ownership has changed.

A temporary bounded ROC Human02 diagnostic over 50 FOW updates recorded 121,500 blocker-hash edict visits and 540,700
second-pass rim-cell visits for 41,909 committed rim cells. Steady updates now skip the blocker hash until a blocker is
spawned, freed, moved, scaled, hidden, killed, restored, or revived. Dirty updates retain the hash comparison as a
correctness check, so redundant invalidations do not rebuild the grid. The rim pass records its temporary blocker cells
while discovering them and commits only that list; initialization treats allocation failure as fatal instead of silently
falling back to the square scan. `wc3_game.fow_blocker_cache_skips_clean_and_unchanged_dirty_updates` covers the clean
cache hit, dirty/hash hit, and dirty/hash miss paths.

### Retail Game.dll fog audit

The ROC demo `data/Warcraft3demo/Game.dll` (build 4486, SHA-256
`286823c37a1083e91f07d040e46a9df7af4c4952e01fcbba460589bd4e297654`) retains `CFogOfWarMap.cpp`,
`CFogMaskTable.h`, `CFogModifier`, and a fog-checksum diagnostic. With image base `0x6f000000`, the routines at
`Game.dll+0x38e730`, `+0x38e7f0`, and `+0x38e8b0` index rows through a stored shift, clip spans, expand a 16-bit mask
to both halves of a 32-bit word, and update paired planes with `or`/`and`. The dispatcher at `+0x38f4b0` clamps a
map-space window, reads table entries, and calls those span helpers. Retail therefore uses packed, table-driven fog
planes rather than a byte setter for every cell.

The packed-mask series ending at `af82327b` is closer to retail in storage and update shape: it adds 16-bit current
and explored planes and writes word spans. It is not retail-exact. OpenRealm uses `(width + 15) / 16` rows rather than
retail's power-of-two dimensions, generates circular spans with `sqrtf` instead of the unrecovered mask table, and
enables that path only with `wc3_fow_fast`, which skips blocker occlusion. The normal shadowcast remains the closer
visibility behavior around blockers; packed storage alone is not evidence that fast fog matches retail silhouettes,
modifier selection, plane semantics, or scheduling.

`WC3_FOW_PACKED_MASK` remains a removable build guard: delete its blocks in `g_fow.c`, its fields in `g_local.h`, and
the define in `game.mk` to remove the experiment. Do not enable `wc3_fow_fast` for correctness or parity validation
until the retail mask tables and modifier semantics have been recovered.

### Local release A/B and remaining cost

A same-machine release-build comparison used ROC Human02, 150 console `wait` commands, `cmd cancel`, then 1,200 waits
with `r_stats 1` at the default 2048x1536 Retina drawable. Detached worktrees are required because changing
`BUILD=debug/release` does not invalidate existing make outputs. Results are directional because the campaign continues
to change the scene during the one-second statistic windows:

| Revision/configuration | Settled FPS | Draws/frame |
|---|---:|---:|
| `6c274d96` suspected-good | 345, then 435 as entities left the view/snapshot | 115, then 34 |
| `e471c472` | 311-316 | 146-148 |
| current + explored-scenery fix | 338-351 | 146-148 |
| current, `r_unit_shadows 0` | 342-350 | 129-130 |
| current, `r_entities 0` | up to 472 | 41 |

The current release therefore reaches the expected ~350 FPS locally even at the doubled Retina drawable. Disabling
shadows does not materially change FPS; entity model submission is the remaining scalable owner. A temporary strict
batch key `(model, skin, frame, oldframe, team, flags)` found 49 visible entities at the settled camera, 40 unique keys,
and only 14 entities across five repeated keys (largest group six). Naive MDX instancing has limited coverage there.
Do not treat the suspected-good 435 FPS window as equivalent content: its draw count fell to 34 as the older visibility
lifecycle removed scenery. Obtain a post-fix handheld profile with sample weights before redesigning MDX pose/geoset
submission; the address-only gists cannot choose between CPU submission and driver/GPU stalls.

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

## Adaptive harvest interaction lanes (August 31, 2026)

An RG40xx-H weighted `perf report` after the blocked-footprint worker approach fix showed the new interaction helper dominating the server frame. The resolved game stack was `G_RunFrame -> G_RunEntities -> G_RunEntity -> monster_think`, with about 34% under `ai_goldmine_walkback -> gold_find_direct_footprint_approach` and another 11% under `ai_walkmine -> gold_find_direct_footprint_approach`. Both paths entered `CM_FindApproachPointToFootprintForRadius`, whose old candidate search called `CM_DistanceToPathingFootprint` for every candidate cell. Because that distance helper scans every authored footprint pixel, one edge selection became candidate-count times footprint-area work and accounted for roughly 45% of sampled cycles in that capture.

Caching one selected edge point for an entire resource leg was rejected after runtime testing. Packed Peasants are displaced by live-unit collision as they converge on a Town Hall; a point that was the nearest legal edge lane earlier in the leg can later lie behind another worker or across the blocked building footprint. Keeping that stale point made several returners repeatedly steer back toward the old lane and visibly dance instead of completing the deposit. Gold and lumber therefore re-select the nearest direct footprint edge from the worker's **current** position each think. The later 30-Peasant Human02 crowd simulation moved live-unit separation out of same-tree lane allocation entirely: tree approach keeps the closest direct legal chop point, while resource-worker local avoidance queues same-stream workers and uses deterministic bounded right-first passing only for crossing or persistently blocked traffic. This removes the previous full-edict same-tree slot/occupancy scans as well as their forced angular detours. See [worker-crowd-routing.md](worker-crowd-routing.md).

`CM_FindApproachPointToFootprintForRadius` now makes that adaptive selection cheap without changing its authored-footprint semantics. The pathmap owns a reusable one-byte-per-cell scratch mask. For each blocked footprint pixel, the helper marks only nearby pathmap cell centres whose exact grid-rectangle distance is within the requested range; it then scans those marked candidates once. This replaces the nested candidate-by-footprint distance scan with footprint-area times a small local range plus one candidate pass. Once a direct candidate has been found, farther candidates also skip redundant line-walkability traces. The mask is scratch state only and is cleared over the small search rectangle on each call.

Keep the single `CM_DistanceToPathingFootprint` call in the per-think interaction-range check: that authored-footprint distance decides exact mine/deposit completion. The optimized cost is the optional staging search, not the behavior-owned interaction boundary. `wc3_pathfinding.footprint_approach_respects_sparse_path_texture` protects irregular footprint accuracy, and `wc3_movement.gold_return_reselects_footprint_edge_after_displacement` protects the adaptive lane behavior that prevents the Town Hall dance.

## October 21 W3E terrain bulk-decoding implementation (pending benchmark)

The OrcX01 profiling log `openwarcraft3(20261021-134309).log` captures one first load
(25.427 s) and five subsequent loads in one process (median 15.404 s). During
repeated loads, server heightmap reading was around 2.7 s, while renderer
`FileReadWar3Map` terrain parsing was around 2.8 s. These numbers are *before*
this optimization, not evidence of a measured speedup. The first load additionally
parses the map on the client, while repeated same-map loads in this log do not.

`common/world.c:CM_ReadHeightmap` and
`games/warcraft-3/renderer/w3m/r_war3map.c:FileReadWar3Map` use the shared
`games/warcraft-3/common/war3map_vertices.h` W3E reader. The serialized vertex is **seven bytes**
(two LE16 values and three packed bytes); `war3mapVertex_t` is an expanded
in-memory representation, not a file layout. The reader now takes **4096 vertices
per MPQ read** instead of five calls per vertex, decoding the entire packed record
without relying on a struct cast. Each request verifies the returned byte count
because `SFileReadFile` can return success for a short read. The scratch buffer
is fixed-size (28 KiB), and successful decoded vertices use the caller's existing
map-owned allocator. The reader rejects truncated data, invalid/oversized
geometry, and palette counts over 256, without exposing a partially decoded
vertex array. Both callers retain map-replacement cleanup responsibilities; a rejected
server W3E stops `CM_LoadMapFormat` before later readers run.

`wc3_terrain.w3e_*` tests in `games/warcraft-3/game/tests/t_w3e_bulk.c`
cover packed flags, batch boundaries, and malformed input using tiny synthetic
MPQs. Verify with the affected native build and `make test`, then replay terrain,
water, cliffs, ramps, blight, fog/minimap, and repeated map transitions on RoC
and TFT. This change deliberately does **not** share terrain data between renderer
and collision world; their decoded arrays remain independently owned.

For a useful timing comparison, capture at least five measurements of each
relevant load scenario on the same build mode, hardware, and data set. Compare
`CM_LoadMap` heightmap, renderer terrain parse, and end-to-end loading medians,
and track peak RAM. Keep initial load, same-map restart, and return-to-menu
reload separate. If diagnostics are enabled, use a matching diagnostic build for
both baselines; do not claim a gain solely from the prior unoptimized log.

### AI creep camp candidate compaction (patch twenty-one, ARM64)

NightElf07 sampling identified `G_BotUnitAlive` among AI hotspots. The `GetCreepCamp`
query had a known repeated-work pattern: each breadth-first-search step scanned
`globals.num_edicts` and re-evaluated liveness, ownership, and unit eligibility on
unrelated entities. `G_BotGetCreepCamp` now filters eligible Neutral Hostile
creeps once at query entry, then searches the compact, edict-ordered candidate
list, with a rectangular distance precheck before the original exact radial
comparison. For N map edicts and K eligible creeps, the grouping search changes from
O(K*N) to O(N + K*K), with no persistent cache or simulation-frame
invalidation. The query allocates two transient buffers and releases them on
completion. The candidate order, inclusive 600-unit membership rule, transitive
camps, flyer filter and nearest representative are unchanged. Tests cover
non-creep decoys, the exact radius boundary, death/removal, flight type and
membership changes.

This addresses a source-level repeated scan, not a confirmed 1.94% reduction:
`G_BotUnitAlive` is also used by other AI queries. Compare matched NightElf07
scenes and symbolised inclusive call paths before claiming an FPS gain.

### Blight destructable update on changed cells only (patch twenty, ARM64)

`G_SetBlightPoint`, `G_SetBlightRadius`, and `G_SetBlightRect` now use the
changed-cell count already produced by `G_SetBlightCorner`. On additive
Blight calls that paint no new cells, skip the expensive full-edict
`G_BlightUpdateDestructables` scan while preserving authored rasterization,
network dirty-row accounting, explicit removal, and one-way destructable tint.
Freshly spawned destructables retain their separate spawn-time Blight
initialization. Tests `wc3_destructable.blight_{point,radius,rect}_noop_skips_destructable_rescan`
check repeat additions and clear/re-add behavior. This addresses the
NightElf07 `CAbilityBlightGrowth` → `G_SetBlightRadius` path, where
`G_IsDestructable` and `Box2_containsPoint` sampled heavily. These were
profile hotspots, not a measured post-patch improvement; compare matched
NightElf07 scenes and frame times on the target ARM64 device.

### Per-client tint scan rejection (patch fifteen, ARM64)

`G_WriteClientDatagram` still visits edicts in entity-number order so its tint
section remains complete and deterministic, including otherwise unselected
units and tints which are not part of the bounded entity snapshot. Most map
scenery, however, is neither explicitly tinted nor a source of viewer-relative
alpha. A conservative, constant-time candidate predicate now rejects such
edicts before evaluating hero dissipation or FoW/detector visibility. A live
`currentmove`, Shadow Meld state, `RF_HIDDEN`, permanent/ghost invisibility,
or the authored tint bit always falls back to the full original evaluation.

This does **not** cache FoW across server snapshots or replace the tint set
with `SV_BuildClientFrame`'s bounded entity candidates: those sets have
different eligibility and saturation semantics. Tint count, ordering, overflow
(all-or-nothing), weather/lightning placement and blight reservations retain
their existing wire contract. This is a CPU fast-path candidate only; compare
matched scenes and FPS/cycle rates before claiming a speedup. In-engine
regressions cover sparse untinted scenes, toggle-on/off, owner ghost alpha,
untinted hero dissipation, and full-buffer decisions.
