## Exact yaw placement rounding

`renderer/r_ents.c:R_YawEntityPlacement` must match the generic quaternion
matrix coefficient for coefficient. Store `sine * doubled_sine` in a separate
float before subtracting it from 1, matching the generic matrix's `zz`
temporary. A single `1 - sine * doubled_sine` expression permits fused
multiply/subtract on macOS arm64 and changes diagonal coefficients near
frustum edges. Keep the exact assertions in
`wc3_coordinates.yaw_only_entity_matrix_matches_quaternion_reference`;
loosening their tolerance hides this difference. Reproduce without a renderer
window using `build/bin/openwarcraft3-tests -data build/tests +dedicated 1
+test wc3_coordinates.yaw_only_entity_matrix_matches_quaternion_reference`.

## NightElf07 profiling comparison summary (October 25, 2026)

The comparisons below rank the measured **reduction in sampled CPU-cycle
share for the targeted hotspot**, using separate NightElf07 runs. A hotspot
reduction is `(before - after) / before`; it is not a reduction in total CPU
time or an FPS improvement. Sample shares can shift with workload and sampling
variation, and the rows are not additive. Grouped measurements cannot isolate
the contribution of each commit. Percentage suffixes on rewritten commit
subjects refer to these sampled hotspot reductions; suffixes marked `group`
describe a grouped comparison rather than an individual commit result.
Regression suffixes use `+` to show an increase in sampled cost.

| Rank | Commit | Targeted hotspot | Sample share | Hotspot reduction | Share drop |
|---:|---|---|---:|---:|---:|
| 1 | `client: cache active entity indicators per frame` | `CL_ApplyIndicator` | 2.99% → 0.05% | 98.3% | 2.94 pp |
| 2 | `renderer: hash registered texture IDs for fast model lookup` | `R_FindTextureByID` | 1.52% → 0.08% | 94.7% | 1.44 pp |
| 3 | `client: sample entity debug cvar once per scene batch` | NightElf07 string comparison | 5.94% → 0.32% | ~94.6% | 5.62 pp |
| 4 | `renderer: skip reference-only MDX attachment poses and reuse fire poses` | Attachment collection | 10.59% → 1.18% | 88.9% | 9.41 pp |
| 5 | `wc3: skip unchanged blight destructable scans` | `G_IsDestructable` | 1.96% → 0.32% | 83.7% | 1.64 pp |
| 6 | `client: cache selected entity during HUD layout rendering` | Selected-entity scan | 2.09% → 0.37% | 82.3% | 1.72 pp |
| 7 | `renderer: use direct scene indexing for MDX pose cache` | `MDLX_ViewPoseFind` | 1.35% → 0.31% | ~77% | 1.04 pp |
| 8 | `renderer: skip world event splats in HUD and cursor views` | HUD sprite rendering | — | 74.2% | — |
| 9 | `renderer: reuse MDX bone poses and transforms across render passes` | MDX alpha rendering | 9.87% → 3.68% | 62.7% | 6.19 pp |
| 10 | `renderer: skip identity-basis multiplication for entity transforms` | `R_GetEntityMatrix` | — | 60.6% | — |
| 11 | `renderer: reuse entity bounds matrix and optimize affine frustum culling` | Entity visibility | 12.11% → 5.40% | 55.4% | 6.71 pp |
| 12 | `wc3: optimise fog blocker-rim traversal with indexed rows` | Blocker-rim scan | 1.83% → 0.91% | 50.3% | 0.92 pp |
| 13 | `renderer: reuse culling results and filter redundant MDX alpha draws` | Alpha-surface rendering | — | 49.6% | — |
| 14 | `wc3: avoid repeated blight datagram compression retries` | `SV_SendClientMessages` | 7.15% → 3.70% | 48.3% | 3.45 pp |
| 15 | `renderer: cache MDX keyframe intervals within render views` | Keyframe-bound search; combined search/cache cost | — | 42.0%; 16.3% combined | — |
| 16 | `renderer: fast-path yaw-only entity matrix construction` | Entity matrix construction | 2.80% → 1.63% | 41.8% | 1.17 pp |
| 17 | `wc3: compact AI creep camp candidate searches` | `G_BotUnitAlive` | 1.94% → 1.14% | 41.2% | 0.80 pp |
| 18 | `shared: optimise axis-aligned frustum culling with support points` | AABB frustum culling, inclusive | 1.98% → 1.18% | 40.4% | 0.80 pp |
| 19 | `renderer: reuse terrain corners across moving shadow splat tiles` | Terrain-height sampling | — | 39.3% | — |
| 20 | `renderer: reduce terrain-splat cache collisions for unit shadows` | Splat-tile creation | — | 31.0% | — |
| 21 | `wc3: optimize fog shadowcasting and blocker rim traversal` | Blocker-rim samples | 1.77% → 1.34% | ~24%* | 0.43 pp |
| 22 | `renderer: reuse Warcraft III entity matrices across render passes` | Matrix multiplication | — | 20.6% | — |
| 23 | `renderer: avoid redundant clipped splat UV generation` | Splat-tile creation | 3.08% → 2.84% | ~7.8%* | 0.24 pp |
| 24 | `renderer: avoid duplicate global-sequence bone track evaluations` | `R_CalculateNodeMatrix` | — | 6.5% | — |
| 25 | `renderer: skip MDX bone binding for opaque-only alpha passes` | Alpha-pass bone binding | 6.98% → 6.56% | 6.0% | 0.42 pp |

The largest priorities to retain are MDX attachment and pose reuse, entity
culling, client HUD/indicator caching, texture-ID lookup, and blight processing.
These paths removed substantial sampled work from already prominent call
trees. Percentages for small functions can still represent modest absolute
reductions, so the targeted share and surrounding parent path matter when
prioritising follow-up work.

| Commit or group | Evidence and decision |
|---|---|
| `wc3: prune rejected fog shadowcasting slope windows` | Inclusive shadowcasting cost rose 5.1%; candidate for reverting. |
| `wc3: cache shadowcasting slopes across fog revealers` | Exclusive shadowcasting cost rose 1.4%; no demonstrated benefit. |
| `renderer: optimize affine MDX bone hierarchy transforms` | About 44% fewer scalar multiplications theoretically; runtime benefit not isolated. |
| `renderer: reuse MDX sequence selection across bone pose` | About 98.9% fewer sequence searches theoretically; runtime benefit not isolated. |
| `wc3: skip idle status updates and defer scheduled pulses` | Not separately quantified. |
| `wc3: reject out-of-range Disease Cloud targets before spell validation` | Not separately quantified; targets a previously expensive path. |
| `renderer: preserve entity-camera alpha rendering` | Correctness fix; no performance improvement measured. |
| Test and documentation commits | Coverage, fixtures, or documentation only; no performance claim. |

The shadowcasting figures above are reasons to review those changes separately,
not proof that either change alone caused the observed cost. A matched FPS or
frame-time comparison against an unoptimised build is still needed to measure
the complete game's speedup.

## October 22–23, 2026 ARM64 profiling comparison

These comparisons report reductions in sampled CPU cost for the named function
or subsystem, **not overall FPS or frame-time improvements**. Captures came
from separate runs, so workload and sampling variation apply. A percentage
reduction is relative to the targeted path; percentage-point (pp) changes show
the difference in its share of total samples. Parent paths and narrower
hotspots can overlap, and neither percentages nor share drops should be added
across rows. The detailed sections below describe the changes and their
correctness constraints.

| Commit | Targeted path and measured result | Evidence |
|---|---|---|
| `renderer: preserve negative MDX camera key times` | — | Correctness fix, not a performance optimisation. |
| `test: update regression fixtures for current state` | — | Test maintenance. |
| `renderer: optimize MDX keytrack interval lookups` | MDX hotspot 0.93% → 0.54% (41.9% reduction) | Targeted hotspot measurement. |
| `renderer: index walkable surfaces for ground conformance` | Ground conformance 0.65% → 0.31% (52.3% reduction) | Targeted hotspot measurement; [implementation notes](#october-23-arm64-ground-surface-candidate-index-patch-sixteen). |
| `renderer: batch contiguous Warcraft III splat submissions` | Retained event-splat path 3.61% → 1.39% (61.5% reduction) | Strong targeted result; [implementation notes](#october-23-arm64-contiguous-splat-submission-batching-patch-fourteen). |
| `renderer: cache repeated terrain-conforming splat geometry` | Splat generation 2.77% → 2.81% (1.4% increase; 0.04 pp) | Dropped after this measured regression; the later [four-way collision follow-up](#october-24-nightelf07-shadow-splat-cache-collision-avoidance-patch-thirty-seven) retains the revised cache. |
| `wc3: optimize status-state queries using occupied-slot index` | Monster thinking 17.77% → 10.42% (41.4% reduction); original 7.54% status hotspot disappeared | Broad parent path plus hotspot; [implementation notes](#october-23-arm64-categorical-status-state-lookups). |
| `fix(wc3): update optimizations for extracted unit state` | — | Compatibility/correctness fix; no separate measurement. |
| `wc3: cache aura family classification and filter ability ownership checks` | Aura processing 5.93% → 4.26% (28.2% reduction) | Targeted path; [implementation notes](#october-22-arm64-aura-family-classification-and-ownership-filtering). |
| `wc3: cache moon well ability classification by data generation` | Moon Well hotspot 1.19% → 1.09% (8.4% reduction; 0.10 pp) | Provisional; [implementation notes](#october-22-arm64-moon-well-class-lookup-caching). |
| `wc3: optimize moon well ability update dispatch` | Ability-update branch 2.70% → 1.38% (48.9% reduction; 1.32 pp) | Targeted branch measurement; [implementation notes](#october-22-arm64-moon-well-update-filtering). |
| `wc3: skip unnecessary per-unit ability update callbacks` | Ability-update path 11.37% → 2.70% (76.3% reduction; 8.67 pp) | One of the largest measured share drops; [implementation notes](#october-22-arm64-per-unit-ability-update-dispatch). |
| `wc3: reuse bound mdx bone pose for attachment collection` | Sampled rendering path 27.89% → 25.01%; alpha surfaces improved 31.1% | Broader rendering path; measurements overlap. |
| `wc3: optimize aura provider discovery with single-pass resolution` | Aura-update path 8.73% → 5.59% (36.0% reduction; 3.14 pp) | Targeted path. |
| `wc3: optimize client tint visibility and snapshot packing` | Snapshot subpath 11.19% → 2.59% (76.9% reduction; 8.60 pp); broader client-message path improved 52.2% | Large subpath and parent-path changes; do not add together. |
| `wc3: optimize status level queries with occupied slot index` | Original `G_UnitStatusLevel` hotspot at 8.15% disappeared | Large hotspot elimination; exact reduction unavailable. |
| `wc3: optimize timed status updates with indexed slots` | Timed-status hotspot 8.15% → ~0.64% (~92.1% reduction; ~7.51 pp) | Another status-query hotspot remained until the following optimisation. |

### Reverted or dropped performance changes

These changes were removed because the available profiles did not establish a
useful improvement for their added code paths. Their results are hotspot
sample-share comparisons from separate runs, not frame-time measurements.

| Commit | Decision and evidence |
|---|---|
| `b47e389b` — skip untinted entities during client datagram generation | Reverted. Datagram generation changed from 2.31% to 2.29% of samples (0.02 percentage points; 0.9% relative), too small to distinguish from sampling variation. |
| `2ae8ad97` — selection revalidation with a single entity scan | Reverted. Selection update changed from 1.59% to 1.56% (0.03 percentage points; 1.9% relative), inconclusive against run variation. |
| `6e495b3b` — HUD overlay stable paint groups | Reverted. HUD layout changed from 10.78% to 10.23% (0.55 percentage points; 5.1% relative), a small result without matched frame-time confirmation. |
| `e5118fec` — combine regeneration aura provider and effect scans | Reverted. It merged traversals, but no isolated post-change profile measured its effect, so the runtime benefit was unconfirmed. |
| `renderer: cache repeated terrain-conforming splat geometry` | Dropped after splat-generation samples rose from 2.77% to 2.81% (0.04 percentage points; 1.4% relative). A later collision-avoidance cache follow-up remains and is measured separately. |

### Largest measured contributors in this group

The largest reported reductions are per-unit ability-update dispatch
(8.67 pp), client tint/snapshot packing (8.60 pp in its subpath), timed-status
indexing (~7.51 pp), status-state indexing (the monster-thinking parent path
dropped 7.35 pp while its original 7.54% status hotspot disappeared), and
contiguous event-splat batching (2.22 pp). The status-state result combines a
parent-path change with the disappearance of a narrower hotspot; those figures
describe overlapping views of work. These are useful priorities for retention
and regression coverage, not an estimate of cumulative frame-time savings.

The earlier status-level index result has no exact post-change percentage,
although its original hotspot disappeared. Regeneration-aura traversal,
MDX-bound pose reuse and single-pass aura-provider discovery also lack an
isolated commit-level reduction in the supplied comparison. Treat them as
unquantified here rather than assigning them a percentage based on adjacent
measurements.

### Results to keep provisional or review

The initial terrain-conforming splat geometry cache measured a slight increase
in splat-generation share (2.77% to 2.81%) and its commit was dropped. A later
four-way collision follow-up reintroduces a revised cache with a separate
NightElf07 measurement; see its dated section below. The untinted-entity
datagram change (0.02 pp) and selection scan change (0.03 pp) are too small to
distinguish confidently from sampling variation. HUD grouping and Moon Well
classification showed small reductions and merit matched A/B measurements.
These results do not prove that the changes have no value on other workloads.

The large results identify worthwhile hot paths, but do not establish an
end-to-end speedup: profiles were collected in separate runs, some rows cover
overlapping parent and child paths, and removing one hotspot can shift the
remaining sample shares. Use matched scenes, repeated frame-time measurements
and comparable profiles to assess complete-game impact.

## October 25 NightElf07 fog blocker-rim word index (patch fifty-one; patches 49/50 excluded)

The latest NightElf07 profile after patch fifty (which is **not** retained)
shows `G_FowRevealBlockerRim` at **2.06% exclusive** CPU-cycle sample
share, while `G_FowCastLight` costs 5.35%. The retained baseline for this
change is **patch forty-eight**: the speculative two-way MDX keyspan cache
(patch 49) and translation-only bone hierarchy composition (patch 50) are
both excluded. These percentages are sampled relative CPU shares, not
projected frame-time gains.

The rim pass previously inspected every cell in the clipped circular
bounding square, rejecting most cells at the `blocked` byte lookup. The
game-side fog module now maintains a compact **32-cells-per-word, row-major
blocker bit index** alongside the authoritative blocked byte grid. The bit
index is allocated and cleared at fog map initialization, cleared whenever
the blocker grid is rebuilt, and populated by the same `G_FowSetBlocked`
writer that handles trees, other occluders, path-texture pixels and
dilation. When a bit-index allocation fails, the unchanged rectangular
scan remains available; no extra persistent state is written to saves.

For the blocker-rim pass, each clipped row visits only set bits in the
intersecting 32-cell words, in increasing x order. The original circular
inclusion test, four-connected visible-neighbour check, transient marker
value 2, deferred commit order, explored/dirty-row updates and map-edge
clipping are unchanged. The bit index does **not** replace the byte grid
used by recursive shadowcasting, nor does it alter normal `G_FowCastLight`
visibility. Rebuild synchronisation prevents stale blockers after movement,
death, removal, path-texture changes or map transitions.

`wc3_game.fow_blocker_rim_bit_index_matches_rectangular_reference` compares
both traversals byte-for-byte for empty, sparse, dense, striped and
multi-cell blocker patterns, 32-bit word boundaries, several sight radii,
map corners, transient rim propagation and successive rebuilds. The existing
`wc3_game.fow_shadowcast_blocker_rim_rebuild_is_stable` and
`wc3_game.fow_shadowcast_blocker_rim_clips_at_map_corner` tests exercise the
live entity/rebuild path. Compile and test locally, then profile matched
NightElf07 runs for `G_FowRevealBlockerRim`, `G_FowUpdate`, the cost of
rebuilding blocker indices and FPS/frame times. No compile or runtime tests
were executed while preparing this patch; no production debug logs added.

## October 25 NightElf07 selected-entity HUD lookup (patch forty-eight)

After patch forty-seven, `SCR_LayoutSelectedEntity` accounted for roughly
**2.09% exclusive** CPU-cycle sample share in the NightElf07 capture. The
command-button drawer called it for each button, and it scanned up to
`cl.num_entities` slots every time even though all buttons in one layout draw
use the same first selected entity.

The generic client now resolves the first `RF_SELECTED` entity **lazily once
per complete `SCR_DrawLayout` invocation**, sharing the result across its HUD
layers and transient windows. An independently drawn `SCR_LayoutDrawOverlay`
(including loading UI or a standalone window) receives its own lookup scope.
Nested overlays preserve their parent's resolved selection, and direct
`SCR_LayoutDrawCommandButton` calls outside a layout draw retain the previous
uncached scan. Both selected pointers and the absence of selection are cached;
end-of-draw cleanup drops the pointer and resolved state, so the next draw
reflects snapshot changes and reused entity slots. The original lowest-index
selected-entity precedence and ability-glow semantics are unchanged. No game
logic, protocol changes, heap allocations or production debug logs are added.

`client_layout.selected_entity_lookup_*` tests cover first-slot precedence,
repeated reads, nested overlay scopes, cached NULL, selection changes between
separate draws, empty entity ranges and reused slots. These are regression
cases for the scoped lookup; compile and run them plus integration UI tests
locally. Compare subsequent NightElf07 `SCR_LayoutSelectedEntity` sample share,
`SCR_LayoutDrawCommandButton` call trees and actual frame times. No compilation
or runtime tests were performed while preparing this patch.

## October 25 NightElf07 axis-aligned frustum culling (patch forty-seven)

After patch forty-six, `Frustum_ContainsAABox` still used the original eight-corner
search for every shadow or terrain AABB and occupied approximately **1.14%**
exclusive CPU-cycle sample share; `Plane3_MultiplyVector3` also remained visible
in shadow frustum rejection. Those samples indicate a promising hot path,
not an observed speedup.

The shared `Frustum_ContainsAABox` now evaluates only the per-plane support
corner selected by its normal's signs. For ordinary finite, correctly ordered
bounds, this reduces up to eight corner dot products per plane to one. It
preserves the original corner-order and **strict `> 0`** decisions through a
conservative error interval: contact/borderline cases, non-finite inputs,
reversed bounds, and overflow/invalid plane arithmetic run the original
corner-test implementation. The error interval uses all possible corner
magnitudes, not just the chosen support corner, so different-corner rounding
near a plane cannot silently change culling. No frustum or game-specific
state is cached; Warcraft III shadows, terrain and other shared callers retain
their existing visibility decisions.

`renderer_model.axis_aligned_frustum_box_*` tests compare with an independent
copy of the original eight-corner reference for real and oblique planes,
inside/outside/straddling and degenerate boxes, near-plane contacts, large and
small coordinates, inverted extents, NaNs and infinities. Compile and run the
renderer tests, then compare matched NightElf07 shadow/terrain `perf` call
stacks and frame time/FPS. No compilation or tests were run locally while
preparing the patch.

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
matrices, reversed or non-finite box endpoints, and plane-contact cases where
float rounding could change the strict `> 0` test. Support-point selection
requires ordered finite endpoints; the original corner path preserves authored
boxes that violate that assumption. The regression
`renderer_model.affine_frustum_box_preserves_inverted_bounds` covers all seven
combinations of reversed axes. This is not an occlusion or visibility-rule change. No persistent
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
into an FPS improvement. The initial small-rectangle geometry-cache commit
showed a 1.4% increase in sampled splat-generation cost and was dropped; the
later four-way cache follow-up remains. This pass targets redundant submissions.

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

## October 5: constructor definitions and ordered fog checkpoints

The constructor now executes explicit ordered stages with a test-only trace
oracle captured before extraction. Address-stable, versioned runtime definitions
replace independent type/resource caches. Attack configuration is shared and
immutable; gameplay writers allocate per-unit overrides through Attack's API.
This reduces the x86-64 edict from 2,784 to 2,608 bytes (704 KiB less clearing per
4,096 creations). Prepared initialization plans retain callback order and
mutation barriers. Resource appends no longer invalidate existing bindings.
Save format 104 persists logical attack defaults and owned overrides; older
formats are rejected. See [construction contract](unit-spawn-lifecycle.md#ordered-construction-and-shared-defaults)
and [save/load](save-load.md).

Fog now has bounded ordered prefix checkpoints, with exact exploration and
row-dirty reconstruction. Independently dirty blocks prevent an unchanged early
prefix from hiding a later mutation. The 800-source differential fixture checks
all five planes against the original rasterizer; complete Classic and TFT engine
runs pass 2,625 tests / 6,879,123 assertions each at runtime310. See
[ordered fog checkpoints](fog-and-cinematics.md#ordered-source-checkpoints).

These are implemented architecture changes, **not performance acceptance**.
Runtime308 raw creation on populated IceCrown takes 8.476353 ms CPU; runtime310
is 8.981335 ms. Both retain exact source221 final positions, membership and RNG.
The runtime310 rendered capture still misses deadlines (228.663719-ms maximum
swap interval). A same-binary native Frida A/B of fog checkpoints versus full
replay measures median fog CPU of 9.901318 versus 9.670793 ms; that pair establishes
no speedup. Runtime311 additionally retains sight when blocker edits leave occlusion cells
unchanged; all 16 fog tests / 10,709 assertions pass in both data modes. Its final
rendered diagnostic still misses deadlines (186.088622-ms maximum interval;
9.436413-ms median fog CPU). The 2-ms creation and presentation targets remain
open. No retail research task is closed by matching the current engine.

## October 5: direct fog geometry and ordered words

The next implementation replaces per-cell cached replay with aligned 64-cell
base/rim masks and an exact ascending rim-word operation. Cold construction
records directly into those masks, eliminating a second bitmap, candidate-cell
allocation/copy and the square per-cell rim scan. A shared blocker bitmap allows
rim disk spans to intersect obstacles a word at a time. Base visibility is
published once after geometry construction, at the same source boundary. No
simulation work is deferred or skipped on a timing budget.

The runtime316/317 IceCrown captures add 4096 units to the populated map. Median
fog CPU changes from 4.517524 to 3.352541 ms; the maximum first-update cost changes
from 99.377249 to 58.255559 ms. Presentation still fails: peak interval changes
from 165.833142 to 122.454727 ms. This is a useful reduction in post-spawn work,
not satisfaction of either the creation or frame budget. The first runtime318
rendered capture is materially worse (10.176142-ms median fog CPU and a
417.986757-ms peak presentation interval), so the earlier pair is not a stable
frame-rate claim. Runtime318 raw creation is 8.098037 ms with exact source221
final positions/member state/RNG. The final runtime316/318 same-window pair
measures fog median 4.184831 → 3.617416 ms and first-update maximum
81.897565 → 68.348673 ms, but active-server median does not improve
(38.280777 → 39.441367 ms) and deadlines still fail. Final `make test` passes;
Classic and TFT each complete 2,630 tests / 7,075,879 assertions. See
[ordered word evaluation](fog-and-cinematics.md#exact-ordered-word-evaluation)
for complexity, exactness tests, prior rejected experiments and capture details.

## October 5: shared fog rays and ordered spatial hashing

Runtime320 replaces repeated shadow-ray slope construction with immutable ray
rows and binary searches using the original float comparisons. Row/column
blocker bitmaps identify transparent intervals, which emit spans directly into
source masks. Obstructed intervals retain the original recursive transitions.
The geometry evaluator has explicit immutable inputs and an exclusively owned
output; it no longer intercepts global visibility writes. See
[shared ray geometry](fog-and-cinematics.md#shared-ray-geometry-and-transparent-spans).

A connected-viewer, headless IceCrown pair (`perf318-fog-viewer-paired-4096.jsonl`
and `perf320-fog-viewer-paired-4096.jsonl`) measures median fog CPU
14.905264 → 4.140197 ms and maximum 221.504292 → 37.786242 ms over 41 updates.
Both finish with identical positions, membership and RNG. The viewer connects
at simulation time 6000; the first update includes cold original-map visibility
and blocker preparation. It is not interchangeable with an already-connected
rendered capture. Raw headless runs without a viewer do almost no fog work and
must not be used as visibility-performance evidence.

Native phase timing then identifies entity updates as the larger remaining
owner: runtime320 averages 40.82 ms in `G_RunEntities` versus 5.71 ms in fog.
A separate instruction-pointer sample attributes 22,131 of 93,237 samples to
`SV_AreaEdicts_r`; this is sampled ownership, not a call-stack profile or an
exclusive CPU percentage. The server's shallow Quake tree repeatedly scans
large lists for small neighborhood queries.

Runtime324 adds a derived hierarchical spatial hash while retaining those lists
as the encounter-order authority. Each exported edict has one entry at a grid
level large enough for its bounds. Queries visit relevant cells at occupied
levels, then restore node/list order through a stable fixed-width radix sort.
Same-cell relinks retain bucket membership but advance canonical order. Large
queries and costly bucket chains use the original list traversal; the choice
happens before any predicate runs. Nested queries and predicate-driven relinks
retain the old observation semantics. See
[server query contract](../../../ARCHITECTURE.md#ordered-server-spatial-queries).

The structure uses O(entity capacity) derived storage. Normal link/unlink is
expected O(1); a query costs visited cells/bucket entries plus linear candidate
ordering. Hash collisions are bounded by switching to the reached list scan.
A whole-map query still has linear output work; this does not make dense
all-neighbor interactions subquadratic. Query order, strict tree boundaries,
result limits and predicate effects are covered by 744 differential assertions.

The rendered `perf320-area-pair-4096.jsonl` / `perf324-area-pair-4096.jsonl` pair
uses the same runtime320 game library and changes the server executable:

| Measurement | Original lists | Hierarchical hash |
|---|---:|---:|
| Mean entity-update CPU | 36.297128 ms | 26.038076 ms |
| Mean fog CPU | 5.441414 ms | 5.702687 ms |
| Maximum presentation interval | 108.909396 ms | 94.242930 ms |
| Double-period gaps | 21 | 18 |

These are bounded captures, not a stable-FPS claim. An intermediate runtime323
capture had a worse 121.48-ms maximum interval. Runtime324 raw synchronous
creation is 8.254482 ms for 4096 units, with the complete final record exactly
matching source221. The raw harness separately spends 65.15 ms selecting
traversable corridor locations through diagnostic calls before the native
batch; that preparation is not included in the 8.25-ms constructor figure.
Earlier hash revisions measured 10.39–11.33 ms, so a new
index cannot be accepted on query timing alone. All captures use populated
IceCrown (6630 original entities, including 4471 trees), corridor spacing 128,
CPU 2 and inline path jobs. Reports reside under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/`.

**Creation, total frame time and the 0.8-ms pathfinding allowance remain open.**
These changes do not close any retail fidelity task. The phase probe is
`perf324-phase-probe.py`; shared-ray viewer timing is
`perf324-fog-viewer-probe.py` in the same report directory.

Final `make -j6 TEST_JOBS=6 test openwarcraft3` passes. Classic and TFT each
complete 2,633 tests / 7,093,714 assertions; this includes the fog differential
checks and 744 server query assertions. The normal launcher build is refreshed.
The first full build exposed the new shutdown hook missing from the standalone
network-test stub; that integration was corrected before the successful run.
`perf324-summary.json` records the exact comparisons and timing aggregates.

## October 5: compiled persistent updates and active primary timers

Runtime326 extends the shared ordered ability plans to persistent updates.
Each `AB_TYPE_UPDATE` owner answers a pure `A_UNIT_TYPE_UPDATE` query with an
always-run, absent-for-this-type, or live optional-state-pointer requirement.
Authored alias classification uses rawcode/base identity, not procedure identity.
Unknown procedures keep the complete broadcast. Runtime additions and learned
Hero ranks also retain that path. After a callback changes metadata, ownership,
or the plan epoch, execution resumes at the next original procedure; it never
repeats callbacks or reads a plan freed by that callback. State-pointer checks
remain live so an earlier owner can activate a later owner in the same pass.

`perf326-update-plans-render-4096.jsonl` measures mean entity-update CPU
18.701451 ms versus runtime324's 26.038076 ms. The maximum presentation interval
is still 96.790716 ms, so this does not satisfy the target. These benchmark
commands default to **`earc` (Archer)**, not Footman. Runtime326's separate
instrumented movement capture preserves the complete source221 final record.
Its leaf hooks inflate CPU and are not acceptance measurements.

The next hardware-IP profile (`perf326-movement-sampled-4096.jsonl`) records
3,482 of 47,730 samples in Chaos's primary-timer procedure. It was scanning every
edict at every 5-ms tick regardless of whether any Chaos timer existed.
Runtime327 replaces that scan with an ability-owned hierarchical bitset of
pending units. Ascending live iteration preserves original encounter order,
including newly scheduled later slots. Scheduling, cancellation and blocked
research update membership; cleared/reused slots retire when encountered.
Dead pending units stay indexed so revival retains timer timing.

`A_TIMERS_RESET` / `A_TIMERS_REBUILD` are generic timer-owner lifecycle messages.
Level reset discards derived membership, and load reconstructs it from saved
unit fields. Move handles the same contract for its existing active index.
No process pointer or index is saved, and no save-layout change is needed.
Normal timer work is proportional to pending owners plus occupied bitset words,
rather than the total map population. Empty timer sets do constant bounded work.
Focused Classic/TFT tests cover update-plan mutation barriers and Chaos
cancellation, reset/rebuild, dead pending owners and cleared slots. The existing
retail moving-morph journey also passes its pending-timer save/load checkpoints.
Performance acceptance and the complete suite for this step are still pending.

Runtime327's uninstrumented movement run retains the complete source221 final
record. Median server CPU is 29.992590 ms per 100-ms simulation tick, first tick
348.323903 ms, creation 8.603865 ms and initial order submission 51.293819 ms.
These remain failures of the required budgets.

Runtime328 replaces restoring/Newton integer square roots with an SSE2 estimate
validated and corrected by 64-bit integer products. Other architectures retain
the restoring implementation. This computes the same integer floor under all
host rounding modes; it does **not** replace retail software scalar operations
with ordinary floating-point arithmetic. The adaptive Newton stopping rule
also returns that floor. Boundary/random differential tests cover both recovered
algorithms under all four rounding modes at `-O0`/`-O2`; the 13 adaptive storage
and passage tests retain original node, heap and route traces. Repeated improved
edges can reuse their node's already-published heuristic because its representative
and the request goal remain fixed. Unreached nodes retain their original zero.

Safe presentation checkpoints are extended to completed entity callbacks,
physical-owner boundaries and fog source boundaries. They draw the preceding
client snapshot without dispatching gameplay input or changing simulation
work. These checkpoints address presentation stalls separately from raw CPU;
rendered captures and simulation latency still determine acceptance.

The runtime328 order-only IP sample attributes 1,646 of 3,712 samples to waypoint
collection/retention. The current collector repeatedly scans all entity roots
when each 128-slot destination batch is exhausted. Replacing this with tracked
root changes is the next structural task; growing batches indiscriminately
would change entity identity allocation and is not an equivalent optimization.

## October 5: incremental waypoint root ownership

Runtime330 replaces repeated whole-map waypoint tracing with compact derived
root columns and hierarchical bitsets in Move. Every goal-reference writer uses
`S_SetMoveGoal(owner, &slot, goal)`. This writes the pointer immediately and marks
the owner dirty; reads and reference writes do not allocate. Entity allocation,
removal and sparse Ancient Root release also mark the owner. Bulk pool reset and
map/save replacement invalidate the index. A whole-record fixture replacement
must call `S_MarkMoveGoals` or reset the index too.

At reclamation, only dirty owners reconcile their eleven retained goal fields
against compact previous identities. Counts include references to ordinary
entities so later slot reuse cannot lose an already-stored reference. Bitsets
separate waypoint kinds, live storage, roots and secondary-edge owners. The
collector starts with rooted waypoints, then traverses only rooted secondary
chains using the original first-mark rule. Shared chains and unreachable cycles
therefore retain the old behavior. It computes reusable identities a word at a
time. The initial rebuild is O(all entities); subsequent reclamations cost
O(changed owners + fixed-capacity bitset words + reached secondary-chain edges).
It no longer scans every scenery/unit row for every 128 new destinations.

The original lease boundary and 128-slot growth batches remain intact. No IDs
are reserved ahead of the original allocation point, and allocation still picks
the same eligible slot. Roots/indexes are derived and excluded from saves;
after load the first collection rebuilds them from ordinary saved pointers.
Classic and TFT focused checks pass: 16 assertions for incremental/reference
collector equality and scaling, 1,853 for public group destination capacity plus
restore, and nine for waypoint save references. Full-suite/performance acceptance
for runtime330 is pending. The preceding runtime329 full suite passed 2,636 tests
and 7,093,832 assertions in each data mode, plus 604 Python tests.

## October 5: immutable animation variant families

Runtime331 compiles numbered animation variants into model-owned immutable
spans. Preparation parses each sequence once, canonicalizes its secondary tag
set, and groups by syncpoint, primary name and tag set. The final span retains
authored sequence order. Storage is O(sequence count); cold preparation is
O(sequence count log sequence count), bounded-tag parsing aside. A warm walk
selection visits only actual variants, with the same reservoir-sampling `rand()`
call for every eligible sequence. It does not draw once from a preselected list,
which would change all later RNG results.

The existing request/property selection cache now retains both the tagged result
and the exact-name fallback. Variant selection uses only the former, preserving
cases where fallback previously consumed no random draws. Model release frees
the family storage and invalidates selections. No per-unit allocation or saved
state is added. Cold preparation remains part of the first order's measured CPU.
Classic/TFT model tests each pass 3,648 assertions across six tests, including
candidate/result and next-RNG comparisons with the original selector, reordered
and duplicate tags, distinct syncpoints, missing requests, and a warm parse-count
check. Full suite and paired performance captures are pending for runtime331.

Runtime330's first raw capture preserves the complete source221 final record but
is slower overall: creation 12.142817 ms, initial orders 50.718925 ms, median
simulation 37.658633 ms and first movement tick 433.581222 ms. This is not accepted
as a performance improvement. Compare old/new binaries in the same capture window
before attributing that change; performance targets remain unmet.

## October 4: movement and fine-grid scaling

The Rise of the Naga regression was reproduced in the game module, rather than
attributed to drawing moving models. A debug `NightElfX01.w3x` capture found
4,034 effective-speed evaluations taking 3,993 ms during 200 simulation frames.
Endurance Aura reparsed ability ownership across every edict on every query.
`M_RunScheduledThinks` took 4,475 ms, of which `unit_current_speed` took 4,005 ms.
These nested Frida measurements include instrumentation overhead and overlap;
they identify a repeated scan, not exclusive CPU percentages.

Three data structures replace repeated reconstruction while retaining the retail
numerical and ordering contracts:

- `g_world.c` maintains intrusive fine-cell occupant lists. A query visits the
  occupants of its sampled cells; it no longer builds and sorts an entity snapshot
  or scans the complete entity list per cell. Each object has at most sixteen links.
  Saved `wc3SpatialActive_t` ranks still select encounter order, target dominance
  and the first 32 blockers. Link insertion order cannot change those decisions.
  Publication retains intersection ranks and relinks only changed rectangles.
  Unchanged world/fine pose and radius bits skip coordinate conversion entirely.
- `wc3AccSearch_t` remembers previously indexed nodes. Engine-owned scratch clears
  only those cells between adaptive requests instead of clearing four complete
  map planes. The first request initializes every plane. Node identities, ushort
  wrap, heap ordering, budgets, partial routes and gate policy stay unchanged.
  `reuse_indices` is enabled only while the engine owns the same planes; standalone
  oracle callers may replace their planes and retain full initialization.
- `s_endurance_aura.c` discovers providers after ability ownership/data changes,
  in original edict order. Every query checks live spawn identity, rank, health,
  visibility, alliance and range. Speed multiplications and attack-bonus additions
  remain separate operations in the original order, rather than multiplying a
  precombined aura factor. Bind/type change, add/remove, learn/set-rank, status
  ownership and map/save-load lifecycle invalidate discovery. Regeneration-provider
  discovery also excludes free slots and static scenery before parsing fifteen
  ability families; those actors cannot pass `S_AuraUnitActive`.

The spatial index is derived state. Map replacement/cache teardown releases the
cell heads; save loading reconstructs links from the authoritative saved boxes and
ranks. No new save fields or float approximation were introduced. Changed owners are now queued in a two-level dirty bitset and synchronized in
ascending edict order. The game wraps its imported `LinkEntity` to observe
non-Move geometry writers, and visibility/data/lifetime setters mark membership
changes explicitly. A query with no changed owners performs no world scan.
Scheduled thinks, presentation samples and Move timers use separate ordered
membership bitsets. Their cost follows participating actors, including mutations
during callbacks, rather than every tree or effect. Physical group lookup also
retains a validated derived binding per unit; teardown clears bindings before
freeing the stable group allocations.

Local measurements in matching debug builds:

| Work | Before | Indexed implementation |
|---|---:|---:|
| Fine endpoint with 1,900 idle units | 0.45 ms | 0.05 ms |
| Step blockers with 1,900 idle units | 0.37 ms | 0.04 ms |
| Scheduled movement, same 200-frame instrumented capture | 4,475 ms | 888 ms |
| Effective-speed queries, same capture | 3,993 ms | 782 ms |
| Rise of the Naga simulation-thread CPU, simulation 1–6 s | 167.34 ms/frame | 30.41 ms/frame |
| Same map, simulation 6–15 s | 18.59 ms/frame | 11.81 ms/frame |
| Same map, simulation 15–25 s | 15.35 ms/frame | 10.42 ms/frame |

The last three rows use `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` with only the
production frame hook. They exclude competing-process CPU time, but are still
instrumented measurements. Both runs end with random-owner words
2392768281/2753341456. The indexed CPU capture precedes the additional scenery
filter; it must not be described as a final release frame-rate measurement.
The initial debug synthetic `wc3_perf.twelve_movers_with_4000_scenery` benchmark
used public group Move and production `RunFrame` and averaged 9.70 ms per 100-ms
simulation tick. After dirty publication and sparse owner scheduling, the full
classic/TFT validation run measured 1.76 ms/tick and still asserted that all
twelve movers advance. These fixture timings do not prove rendered FPS.

Reports and bounded Frida scripts are retained externally under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/perf102-*`.
`perf102-frame-cpu.py` records simulation-thread CPU;
`perf102-render.py` records main-loop intervals, accepted public group orders,
unit identities and executable/library SHA-256 values. The native-memory script
keeps strong references to allocated order names through each call. Exploratory
captures with temporary name allocations reported rejected orders; they are
excluded from the workload claim. The exact reason for those rejections was not
isolated, so they are not evidence of an engine order failure.

The first accelerated Intel/Mesa release render capture uses the seven controllable
starting units and repeated group moves every four simulation seconds. From 6–55 s,
one-second windows measure 56.65–60.66 FPS at a 60 FPS cap. Opening cinematic windows
fall below 55 and the capture overlaps a test-module build, so this is preliminary
and does not close the user's twelve-unit/55-FPS acceptance target.
A subsequent twelve-mover capture creates five stock Archers through `unit_create`,
then repeats accepted twelve-member public group moves. Its 57 one-second windows
after simulation 6 s range from 50.93 to 59.05 FPS (median 55.95); seventeen fall
below 55. Simulation-thread CPU averages 14.39 ms/tick during 6–25 s and
13.72 ms/tick during 25–55 s. This run overlaps the TFT validation process.
It demonstrated that the target remained unmet at that checkpoint; neither capture establishes
scaling to hundreds or thousands of movers.

Build modes must be explicit: default `BUILD=debug` uses `-O0`; `BUILD=release`
uses `-O2`. `DEBUG=0` does not select release, and changing `BUILD` alone does not
invalidate existing make targets. Use a fresh output tree for a reproducible A/B
build, then measure with the same camera, resolution, build mode and workload.
A headless simulation timing alone does not prove rendered FPS.

Relevant regression entry points are `wc3_pathfinding.*`, `wc3_movement.*`,
`wc3_collision.*`, `wc3_combat.endurance_provider_changes_are_immediate_and_saved`,
and `wc3_spell.combat_aura_alias_resolution_scales_with_edicts`. The aura test
covers changes within one frame, visibility/range changes, provider removal,
first learned rank, save/load and edict reuse. The scenery test reproduces the
old resolver work bound with 1,900 authored scenery rows. Retail admission
recovery is separately covered by the native retry/random movement fixtures;
worker and Wind Walk traversal fixtures now begin with their intended legal
footprints instead of allowing that recovery to alter the setup.
See [retail movement integration](retail-pathfinding-engine.md) and
[retail pathfinding corpus](retail-pathfinding-corpus.md) for the exactness limits.

## October 4 literature shortlist: exact retail simulation at scale

The target workload is hundreds to low thousands of **moving** units. Small
campaign improvements and benchmarks populated with idle units do not establish
that target. The following is an engineering recommendation informed by primary
sources, not a claim that any cited paper proves Warcraft III equivalence.

Retail equivalence includes complete waypoint/velocity words, tie behavior,
work-budget exhaustion, partial-route selection, temporary object exclusion,
encounter ranks, logical scheduling, random draw order and saved continuation.
Equal shortest-path length is insufficient. The existing fine heap promotes new
entries above equal-key parents, chooses the right child on ties and retains
stale records. Replacing it with an ordinary stable heap can change results.

Recommended replacements, in implementation order:

| Candidate | Replace | Scaling objective | Fidelity condition |
|---|---|---|---|
| Event-maintained fine-cell grid, with separate static ownership and mobile occupancy | Full-edict synchronization in `move_query_objects` and step-blocker collection | Update changed footprints; query only sampled cells and occupants, eliminating the query-count × edict-count term | Every position/radius/eligibility/lifetime writer publishes at the correct observation point; preserve per-cell encounter ranks, exclusions and token caps |
| Compact active-mover records, sparse membership and contiguous hot fields (SoA or AoSoA where measured useful) | Repeated traversal of 4,048-byte edicts and discovery of nonparticipants | Work follows active movers/providers rather than all scenery and effects; reduce cache traffic | Preserve stable identity, owner scheduling and arithmetic order; keep gameplay state owned by Move and avoid unsynchronized duplicate truth |
| Generation-stamped flat node lookup, reusable arenas and sparse scratch reset | Fine search's unconditional 65,536-slot/256-KiB hash clear, repeated allocations and remaining map-sized resets | Initialization follows visited states rather than map area or maximum node capacity | Preserve node creation/identity/wrap and exact heap operations; reset epochs safely on wrap, map replacement and restoration |
| Exact class-specific static footprint bitplanes and locally updated hierarchy | Repeated identical static cell/footprint classification | Precompute static Boolean queries and rebuild only affected rectangles/parents | Same cell-boundary, class, terrain-level, gate-marker and temporary-exclusion rules; no new pruning, heuristic or route simplification |
| Compact binary heap and hot search-node storage retaining retail operations | Allocation/copy/cache cost inside the existing search | Lower constant cost per identical expansion | Replay push/pop identities and stale generations exactly; a different heap arity is conditional on equivalence proof |
| Deterministic jobs for independent preprocessing, with validated speculative searches only later | Serial execution of proven-independent expensive work | Use available cores without changing logical tick budgets | Later movers must observe earlier committed poses; no stale snapshot, reordered random consumption or wall-time-dependent admission |

The first three replacements are implemented in the engine. Static admission
also uses cached class-specific edge bits and an occupied-cell bitmap; the
follow-up measurements below describe their effect. Remaining search and local
density costs still need measurement; dense crowds have no universal linear
bound.

Uniform grids are a good first candidate because retail already uses bounded
fine-cell footprints. [NVIDIA's broadphase chapter](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-32-broad-phase-collision-detection-cuda)
describes conservative spatial subdivision followed by exact narrow-phase tests.
Its GPU implementation is not proposed as our simulation implementation.
A [dynamic AABB tree](https://box2d.org/documentation/group__tree.html) is the
alternative to benchmark for unusually large objects or sparse variable-size
queries. Candidate enumeration must be restored to the retail order regardless
of the tree traversal order; this does not adopt Box2D's motion solver.
[Intel's memory-layout guidance](https://www.intel.com/content/www/us/en/developer/articles/technical/memory-layout-transformations.html)
supports compact SoA storage for avoiding expensive gathers, but layout selection
must follow the measured access pattern rather than convert every structure.
[Chen et al.'s priority-queue study](https://www3.cs.stonybrook.edu/~rezaul/papers/TR-07-54.html)
shows why cache behavior and the actual operation mix matter. Its Dijkstra results
do not prove that a 4-ary heap is faster or equivalent for our bounded retail heap.

Recent and established planner candidates considered:

- [HPA*](https://webdocs.cs.ualberta.ca/~mmueller/ps/2004/hpastar.pdf)
  partitions a map into clusters and caches crossing costs between entrances,
  searching the abstract graph before refining local legs. The original paper
  reports up to tenfold search speedups with paths within 1% of optimal after
  smoothing. This is useful for long-distance static routing and repeated
  requests, but does not establish Warcraft simulation equivalence. Retail
  already promotes/subdivides aligned squares across its adaptive hierarchy;
  that algorithm is integrated in `G_UnitMoveGroupDestination` and member
  refinement. No cluster-portal graph has been recovered. Replacing its chosen
  corridor or pruning its expansions changes budget exhaustion, partial paths,
  encounter ties and subsequent movement. Reuse hierarchy classifications and
  crossing/query data only under a proof preserving those decisions, including
  temporary source/target exclusions, Way Gates and delayed coarse publication.
- [JPS/JPS+ and block scanning](https://ojs.aaai.org/index.php/ICAPS/article/view/13633),
  plus [JPS4 (2025)](https://arxiv.org/abs/2501.14816), reduce expansions using
  symmetry pruning. The useful transferable idea is compact block processing.
  Direct replacement changes expansion identities and work-budget exhaustion;
  JPS4 additionally assumes four-connected movement.
- [Key-Interval A* (July 2026 preprint)](https://arxiv.org/abs/2607.23393)
  searches a lightweight interval abstraction and proves optimality for
  four-connected grids. It is a research candidate for an independent planner,
  not an established equivalence-preserving replacement for retail's eight-edge
  graph, partial routes or gate/footprint policy.
- [Goal bounding and compressed path databases](https://pathfinding.ai/pdf/hhqy-jair21-rgbajps.pdf)
  offer substantial static-grid acceleration, including Warcraft-map benchmarks.
  Those are map-family benchmarks, not native Warcraft simulation comparisons.
  Shortest-path preprocessing cannot substitute for retail's bounded search
  without proof covering partial outcomes and mutable obstacles.
- [LPA*](https://www.sciencedirect.com/science/article/pii/S000437020300225X) and
  [D* Lite](https://www.cs.cmu.edu/afs/cs/Web/People/motionplanning/papers/sbp_papers/integrated3/koenig_dstarlite_aaai02b.pdf)
  reuse search work as graph costs change. Repairing a prior search does not
  automatically reproduce a fresh budgeted retail search. Adopt ownership-driven
  incremental **data updates** first; adopting these planners is a separate
  equivalence problem.
- [Real-Time LaCAM (SoCS 2025)](https://arxiv.org/abs/2504.06091) addresses
  coordinated multi-agent planning with bounded planning time and completeness.
  [ORCA](https://gamma.cs.unc.edu/ORCA/publications/ORCA.pdf) chooses velocities
  through reciprocal collision constraints. Both solve useful crowd problems,
  but their movement decisions differ from retail's yielding, overlap, turning
  and retry behavior. Generic shared flow-field steering has the same fidelity
  issue. Share routes only where the native cohort already owns shared results.

Acceptance for the rewrite: fixed hardware/resolution and matched release builds;
64, 256, 512, 1,024 and 2,048 movers; shared-goal and distinct-goal traffic;
open terrain, chokepoints, opposing streams, blocked goals, mutable obstacles and
mixed footprints. Report simulation CPU, rendered one-second FPS minima, frame
p50/p95/p99/max, expansions, broadphase visits and publication count. Increase
unrelated scenery separately to detect remaining world-size dependence. The
Rise of the Naga floor remains 55 FPS; the older measurements above leave it open.
Compare old/new complete motion, scheduler, route, random and save traces, and
retain native differential fixtures. Any mismatching optimization fails the
fidelity gate even if it is faster. No universal speedup factor or claim of full
retail equivalence follows from the literature shortlist.

## Exact connectivity reuse and spawn profiling

Static flow-field connectivity is now shared by footprint class and static-map
revision, independently of destination. Eight bounded class slots lazily retain
one edge byte and one validity bit per cell. Each queue expansion still charges
one original work unit and retains the same neighbor/relaxation/enqueue order,
10/14 weights and completion tick. Static edits invalidate edges; map replacement
releases storage. This is memoization of the original predicates, not hierarchical
pruning. `multiple_destinations_share_static_connectivity` verifies cold/warm work,
clearance and explicit revision invalidation.

A quiet rendered Icecrown A/B (`perf154-icecrown-host-owners-1024.jsonl` against
`perf157-icecrown-connectivity-host-1024.jsonl`) retains all 5,957 static objects
and 4,471 trees. Flow-job CPU over 80 ticks falls from 138.42 to 68.20 ms;
complete movement averages fall from 10.45 to 9.40 ms/tick. Final member state,
positions and random words match exactly. **The display-frame peak remains
unacceptable: 113.75 versus 117.88 ms.** Initial/replacement command work and
owner decisions dominate that peak; the reduced flow cost is not budget acceptance.
Only 144 units advance on an average tick, 387 at peak, so this does not establish
1,024 simultaneous moving-unit throughput.

The independent Linux main-thread CPU sampler in `tools/wc3_cpu_sampler.c` is
loaded only by the diagnostic tool. `--sample-us 200 --sample-phase spawn`
isolates native `unit_create` batches; `--sample-phase orders` isolates the first
native public command batch; `movement` samples the complete measured interval.
Signal handlers retain only PC and owner phase in a bounded buffer. Histograms
resolve instruction addresses to nearest executable/module symbols after stopping
the timer; they are not call stacks or exclusive nested-function timings.
Requested timer periods do not imply kernel delivery at that resolution.
`--sample-cycles 25000 --sample-phase spawn` uses a Linux perf-event ring for
hardware instruction samples. It reports the cycle period and buffer overflow,
and explicitly marks owner-phase attribution unavailable for this backend.
Hardware startup failures print their system error and fail the capture. The
sampler is compiled as a shared library by the benchmark; `make tools` excludes
this source from its executable targets.

`perf159-icecrown-spawn-4096.jsonl` retains the original world and completes
4,096 native creations and accepted public Moves. Creation consumes 13,627 ms
thread CPU (placement selection separately 106.98 ms). Of 13,722 CPU samples,
8,422 hit Huffman decompression, 3,247 ADPCM decompression and 1,017 MPQ lookup.
`G_FileExists` had read/decompressed death-sound payloads on every creation just
to decide numbered-versus-unnumbered registration. The mandatory generic
`game_import.FileExists` now delegates to the mounted filesystem metadata probe;
WC3 sound and shadow lookup use it without reading payloads. Engine and game
modules must be rebuilt together after this import-table change. Wire and save
formats are unchanged. The registration regression checks 1,024 repeated units,
correct death-sound resolution and zero payload reads.

Pose prediction now has one tagged cache slot per live entity number, covering
full engine capacity instead of 256 colliding entries. Complete input words
remain the key. For unclocked or zero-velocity predictions the clock fields are
canonicalized: the original integer scalar multiply contributes exactly zero,
including signed zero and epoch changes. Publication uses the same pure predictor.
The 4,096-owner regression compares all raw predicted poses and verifies retained
cache hits across an epoch boundary. Moving keys still include both clocks;
no float reassociation, simulation time redistribution or saved state was added.

The rebuilt `perf160-icecrown-spawn-fixed-4096.jsonl` reduces native creation
from 13,627 to 1,062 ms thread CPU (13.97 to 1.08 seconds elapsed), with identical
final poses, members and random words. Its remaining spawn histogram is dominated
by `FindBlockIndex` (809 of 1,073 samples). Archive-local absence caching removes
repeated scans for missing variant names; see [filesystem probes](../../fs-loading-architecture.md#file-existence-probes-and-repeated-absent-names).

The first-command-only profile `perf162-icecrown-orders-1024.jsonl` resolves
15 samples to `FindAbilityByClassname`, 100 to libc and seven to waypoint lease
collection. Its instrumented 167.95-ms command is slower than the unsampled
51.74-ms command in `perf161`; do not compare sampled and unsampled timings as
an optimization A/B. Registry name lookup now uses a fixed sparse string index,
rebuilt before any `A_INIT` callback. Full command names, case sensitivity, first
duplicate-row selection, ability indexes and missing-index255 remain unchanged.
The regression compares every registry name against original first-row selection
and bounds 1,024 repeated successful/unsuccessful lookups; it failed the work bound
before the index and passes afterward.

### Deterministic worker boundary

The first worker stage is implemented for the legacy static flow-field frontier.
`wc3_path_threads 1` uses one persistent SDL worker on multicore machines;
`wc3_path_threads 0` and single-core machines execute the same callback inline.
The game chooses an identical queue-pop budget and snapshots the numeric job
before dispatch. Connectivity selection and allocation happen on the main thread.
The worker reads static geometry and owns frontier scratch; it never reads edicts,
allocates, chooses deadlines or publishes generation handles.

Client command-card/resource/info payload construction can overlap that work.
`CM_FinishPathJobs` joins and publishes before collision callbacks, deaths and
frees can change the eligibility of the next queued request. Cache invalidation,
static-byte edits, synchronous scratch users, map replacement and shutdown also
join before touching borrowed inputs. Test instrumentation uses separate worker
counters merged after joining. The regression compares every numeric cell price
and the complete job status after each fixed-budget step for two queued fields,
then replaces and invalidates geometry while work is pending. The executor test
also proves execution on a different thread and identical inline results.

This stage does not parallelize the much more expensive adaptive/fine owner work.
`g_world.c` still shares `move_acc`, hierarchy index planes and `move_acc_points`,
and temporarily clears mover/target rectangles in class planes. Fine queries
also read live spatial lists. Those searches need private mutable scratch and
immutable geometry/occupancy inputs before concurrency is safe. Completion ticks,
ties, partial paths, random consumption and callbacks must remain in owner order.

The benchmark counts worker CPU in addition to the main-thread movement union.
Its wall bound conservatively adds worker and main intervals, even when they
overlap; lower main-thread time alone cannot establish the movement budget.
`--cpu` pins the main thread and restores the original allowed CPU set on newly
created workers so inherited affinity cannot accidentally serialize the benchmark.
`--path-threads 0/1` selects the two execution modes for identical-input comparisons.

### Media and catalog registration

Server media lookup now uses derived string indexes instead of scanning every
configstring for each model, image, font or aliased sound. Configstring writes
invalidate the affected namespace; rebuilding retains first duplicate selection
and the original first-empty-slot rule. Map reset clears the indexes with `sv`.
Sound aliases and model extension normalization retain their existing identities.
The regression passed 5,044 assertions, including edited slots, holes and aliases.

`perf168-icecrown-media-spawn-4096.jsonl` measures 196.46 ms CPU / 201.19 ms wall
for the entire synchronous public creation batch on CPU 2. This is still far
above the acceptable frame budget; no asynchronous hiding or amortization is
used. The remaining CPU profile identifies repeated linear sound-catalog lookup.
`perf169-icecrown-catalog-spawn-4096.jsonl` reduces the same creation batch on
CPU 2 to 110.54 ms CPU / 115.42 ms wall. Full-name indexes replace scans of all
seven immutable sound catalogs, preserving catalog precedence and first duplicate
selection. Table replacement and shutdown release the indexes; pointer/count tags
also protect replacement fixtures. A 512-row regression failed the repeated-work
bound before the change, then passed all 2,054 assertions; Classic/TFT each passed
2,644 assertions in the complete SLK suite.

Authored sound policies also have a bounded cache keyed by complete row identity,
variant and catalog generation. Initially it cached decoded policy only; the
follow-up below also caches registration identities. Per-player response history remains live. Invalid
policies still report errors. Presentation reset clears it; catalog replacement
changes its generation. The regression reproduced 1,024 redundant decodes, then
passed with one decode while retaining all 1,024 server registrations and checking
policy/volume after reset. Classic/TFT each passed 4,630 assertions in 102 unit tests.

### October 5 synchronous creation follow-up

Fresh `perf195-icecrown-raw-spawn-4096.jsonl` measures **21.895 ms CPU / 23.263 ms
wall** for 4,096 public `CreateUnit` calls on CPU 2. Fixture placement is separately
measured at 60.129 ms and is outside this creation measurement. IceCrown retains
6,630 existing live objects, including 5,957 static objects and 4,471 trees /
destructables. This remains above the **2 ms raw creation target**. Rendered runs
include checkpoint drawing and cannot establish raw creation acceptance.

The engine now compiles immutable authored ability lists once, retains stable
borrowed arrays across nested lookups, and uses numeric membership during lifecycle
and aura queries. Parsing into caller-owned token storage preserves outer parser
tokens during nested ability queries. Bound unit rows, visual resources and fresh
unit sound resources are cached per type. Metadata generations and the mandatory
server `MediaRevision` invalidate derived resources after edits or world replacement;
pending sound state remains per unit. Sound duration queries decode audio lazily,
retaining exact durations without decompression during registration. Engine lifecycle
events visit only opted-in registry rows in their original order. Player technology
lookups use a derived hash index over the original save-owned slots; units with no
completed research skip upgrade-list decoding. Public placement, callbacks, food,
orders and RNG remain synchronous.

Classic and TFT each pass 2,578 tests / 6,841,695 assertions at this checkpoint.
The earlier `perf180`, `perf187` and `perf190` captures have identical final unit
poses, member state and RNG words. These are reimplementation regression checks;
they do not prove full retail parity or close the remaining research tasks.

The host also provides a presentation checkpoint after completed public creation.
It reserves recent render cost from the frame deadline and draws the last decoded
client snapshot while the same simulation stack continues. It does not advance
simulation time, execute input, process commands or publish a partial snapshot.
Actual swap intervals are captured with `--spawn-only --render` through 60 normal
client frames after the batch, recording every intermediate checkpoint draw.
Counting checkpoint draws toward the stopping condition can end before the first
new snapshot and does not establish presentation acceptance. **Hitch-free creation is
not yet established.** The rendered stress test exposed a snapshot merge crash:
the old 9,999 end marker collided with legal high entity IDs. Exhausted iterators
now use `INT_MAX`; a regression adds and removes IDs 9,999, 10,000 and the maximum
game entity ID without changing the wire format. Deterministic animation selection
also caches complete request/property keys until model teardown; randomized variant
selection retains its RNG calls.

The source218 raw IceCrown capture measures **20.539 ms CPU / 22.328 ms wall**;
the first unit costs 2.288 ms CPU and the remaining 4,095 cost 18.251 ms. Source202,
211, and 214 retain identical final poses, member state, and RNG words compared
with source187/195. The separate source214 compiler experiment with
`-fno-semantic-interposition` gives 20.853 ms and is not adopted. Source218's
hardware diagnostic captures 2,748 instruction samples; its 27.305 ms measured
batch includes sampling overhead and cannot establish raw acceptance.

Compiled ability tokens retain both exact four-character membership and the
original name-lookup prefix/length semantics, preserving malformed/long-token
behavior in mine and resource-return classification. Their mapped behavior codes
refresh with AbilityData generation. Cargo, harvest, Blight, and aura membership
queries use these compiled lists. Move-speed bonus queries visit explicit
`AB_MOVE_SPEED_BONUS` subscribers, retaining duplicate handling, removed abilities,
item eligibility, and maximum reduction. Rawcode dispatch resolution caches the
existing complete name resolver's result, including unknown results; metadata
changes and registry initialization invalidate it.

Source221's next raw capture measures **19.362 ms CPU / 19.451 ms wall** with
exact source202 final positions, member state, and RNG. The same engine with
resource accounting enabled (`perf222-icecrown-spawn-rusage-4096.jsonl`) measures
18.270 ms CPU / 18.351 ms wall and 132 minor faults, zero major faults. Thread
resource counters attribute the work to user CPU; those counters have coarser
accounting than the thread CPU clock used for the raw measurement. This rejects
bulk page faulting as the dominant remaining creation cost. These runs remain
well above the 2 ms target.

Post-spawn profiling also identified fog geometry as a major frame cost. An exact
per-viewer unit-sight cache skips unchanged geometry and restores exploration
before applying current scripted fog and Far Sight. Any source geometry or
blocker change still requires rebuilding the affected viewers; this optimization
alone does not establish moving-army scaling. Source202's valid rendered trace
peaks at **272.456 ms** between swaps. Source221 with the corrected rendered
harness (`perf222-icecrown-presentation-spawn-4096.jsonl`) completes 60 normal
post-batch frames, records 62 draws and peaks at **230.912 ms**, with 60 gaps
longer than two 60-Hz periods. It fails presentation acceptance. An experiment adding general entity/movement/
fog checkpoints starved normal simulation and publication and was reverted.
**The 2 ms raw target and hitch-free presentation target remain open.**

Source242 (`perf242-icecrown-raw-spawn-and-move-4096.jsonl`) measures **12.928 ms
CPU / 13.164 ms wall** for the same synchronous 4,096-unit native batch, including
**1.476 ms CPU** for the first unit. Placement-fixture planning takes a separate
69.202 ms; it is excluded only from the native creation counter. After the full
four-second Move workload, all 4,096 final positions, member records and RNG
words exactly match source221. Spawn-only source226/228/231/236/240/241 reports
also have identical immediate positions and RNG, but those stop at simulation
6100 and must not be compared with source221's simulation10000 final record.

The additional implementation compiles ordered per-type lifecycle dispatch
plans and lets innate procedures declare their own broadcast subscriptions.
Unknown procedures retain the complete original broadcast contract. Plans retain
rawcode deduplication, synchronous callback order, active-channel precedence,
runtime additions/removals, learned ranks and callback-driven metadata/type
changes. A differential regression compares the compiled path with the retained
original dispatcher across those mutations. Aura-free types cache only authored
classification; live ownership remains uncached. Combat queries visit combat
providers instead of regeneration-only providers. Spatial unlink visits the
actual clipped footprint rather than every maximum-size slot. Immutable spawn
classification and projectile resources are shared per type with metadata and
media invalidation; per-unit attacks, callbacks and player upgrades remain live.

The server now streams MDLX sequence chunks instead of inflating complete models.
Per-open-file MPQ caching prevents repeated sector decoding for small chunk-header
reads. See [archive-read contracts](../../fs-loading-architecture.md#partial-archive-reads-for-server-model-sequences).
The source240 compiler experiment with `-fno-semantic-interposition` measured
13.508 ms CPU and was not adopted. It does not remove the remaining creation cost.

The valid source242 rendered capture records all 60 normal post-batch frames.
Its peak swap interval is **175.121 ms**; all 60 gaps exceed two 60-Hz periods.
It still fails presentation acceptance. Creation is faster, but post-batch server
work remains a separate major bottleneck. Source227's hardware diagnostic assigns
36.8% of samples to fog update/shadowcasting, 11.1% to the server area query, 3.5% to
cargo-holder world scans and smaller shares to cleanup scans. Those diagnostic
samples include sampler overhead and are not raw timing acceptance. **Neither
the 2 ms creation target nor hitch-free 4,096-unit presentation is complete.**

Source255 (`perf255-icecrown-raw-spawn-and-move-4096.jsonl`) measures **11.334 ms
CPU / 11.385 ms wall**, including **1.045 ms CPU** for the first unit. Placement
fixture planning takes a separate 64.279 ms. It runs on the same IceCrown map,
CPU 2 and inline pathfinding, retaining 5,957 static objects and 4,471 trees.
It remains above the 2 ms requirement. Source244 measured 12.350 ms, source246
13.509 ms, source251 12.309 ms and source253 11.310 ms; these are individual
unhooked native batches, not a statistical latency guarantee. Source244/246/251/253
all retain exact source221 final positions, member state and RNG after the
complete four-second movement run.

Source255 also retains exact source221 final positions, member records and RNG.

Cargo transport discovery now visits an ordered holder registry. Pool allocation,
free, reset and unit initialization maintain that derived registry; live count
and passenger arrays remain authoritative, including first-edict precedence.
The regression covers 2,048 unrelated entities, duplicate holders, free/reuse
and mutable passenger arrays. Save data and persistent edict layout are unchanged.
Known authored procedures reuse only their exact procedure's subscription
contract; passive subclasses keep their original broadcast behavior. Registry
procedure changes and callback-driven alias changes invalidate the skip decision.

Per-thread movement geometry shares map scales, preserving software arithmetic
and the old denormalized-transform subtraction used for cell sizes. The regression
checks exact words across uneven negative bounds and live bounds changes. Combat
aura queries resolve rows when provider ownership changes, read their areas live,
and reject distant providers before eligibility checks. One distance calculation
serves every family from a provider. The regression retains exact radius inclusion,
visibility, direct row edits and metadata replacement. This reduced the measured
source251 batch from 12.309 ms to source253's 11.310 ms; it does not establish the
overall target. Archive lookup now compiles the old probe/fallback selection into
a flat name-hash index; missing distinct names and full tables need no runtime
scan. [Archive contracts](../../fs-loading-architecture.md#partial-archive-reads-for-server-model-sequences)
cover duplicate/deleted records and protected table shapes.

Source244's valid rendered capture peaks at **164.959 ms**, with all 60 normal
post-batch frame gaps exceeding two 60-Hz periods. It still fails. The retained
source258 full Classic/TFT suite passes 2,603 tests and 6,854,481 assertions per
mode, including the cargo, geometry, procedure and aura additions. Its 18 fresh
retail contracts preserve their declared outcomes and known adaptive differences.
An unhooked source258 batch measures **11.661 ms CPU / 11.721 ms wall**, with a
1.035 ms first unit; final positions, members and RNG exactly match source221.
This remains above the **2 ms** raw creation target.

Source255's rendered run still fails, with a median normal-frame interval of
113.331 ms and a 978.169 ms peak. That peak includes a client presentation wall
interval of 883.669 ms versus 17.623 ms thread CPU; the remaining normal frames
also exceed the display budget. A conservative fog coverage experiment in
source257 preserves complete visibility/exploration planes in its differential
regression, but records a 117.876 ms median and 169.090 ms peak on IceCrown.
It is reverted in source258 because it does not improve this workload. These
captures do not establish hitch-free presentation, regardless of raw creation
improvements.

Source277 measures **8.598 ms CPU / 8.649 ms wall** for the same synchronous
4,096-unit IceCrown batch, including **0.771 ms CPU** for the first unit.
Fixture planning costs a separate 64.679 ms. The four-second movement run keeps
source221's complete final positions, member state and RNG words. This is one
unhooked batch, still **4.30 times the 2 ms target**. Rendered acceptance remains
open; the earlier failed captures remain the latest presentation evidence.

Pure mechanical queries now discover occupied statuses, learned ranks and
ordered authored ownership once per scope. Scopes never cross a gameplay
callback or an ownership mutation. Authored membership uses a 64-bit rejection
filter, followed by exact rawcode comparisons on a set bit; hash collisions
cannot grant ownership. Callback enumeration still uses the ordered arrays.
The walk animation selector reads only its primary request token where that
is sufficient, retaining the full tagged selector and RNG sequence.

Units allocate the eight ordered status records only when a status is applied
(format102), and the sixteen-entry command ring only on the first queued order
(format103). Emptying either allocation retains its slot addresses until edict
release. Per-unit scalar state, inverse callbacks, FIFO wrap and saved nested
source references remain authoritative. An edict is now **2,784 bytes**, down
from 4,048 before these two changes. Pool allocation introduces virgin records
in ascending order and recycles freed records in the original LIFO order. First
use no longer constructs a capacity-sized free list; reset invalidates only
the allocated prefix, and every allocation still clears its full payload.

Release modules retain `-O2 -fno-semantic-interposition` after the later source273
measurement improved the batch from source272's 10.937 ms to 9.682 ms. Imported
module APIs and canonical procedure identities remain in use. This flag does
not enable floating-point reassociation; see the [GCC option contract](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html).
The retained source277 Classic/TFT suites each pass **2,610 tests / 6,856,230
assertions**, including sparse queue save/load and callback regressions. All
three release applications build. Source275's eighteen fresh numerical/search
contracts pass with their two declared adaptive/reference differences. These
checks do not close the 142 outstanding retail research tasks or establish
complete simulation fidelity.

Source285 measures **7.950 ms CPU / 7.980 ms wall** for synchronous creation of
4,096 units on IceCrown, including a **0.700 ms CPU** cold first unit. Fixture
planning is separate at 64.922 ms. Final positions, order members and RNG exactly
match source221. This single batch remains **3.97 times the 2 ms target**;
rendered acceptance is still open. Classic/TFT optimized-build suites each pass
**2,614 tests / 6,856,630 assertions** at this checkpoint. Source282's complete
repository suite passes, including 603 pathfinding Python checks, and its eighteen
fresh retail contracts retain their two declared adaptive/reference differences.

The factory uses `SP_SpawnFreshUnit` and `S_InitFreshUnitAbilities` only for newly
allocated units. An explicit `AB_TYPE_INIT` owner contract compiles a per-type
initialization policy; arbitrary boolean results cannot opt a procedure in.
Unowned, inert callbacks may be elided while preserving their original return
and rawcode deduplication. Move and natural sleep initialization still execute.
Shadow Meld initialization only cleans existing Hide state, so fresh units may
skip it even when they own Hide. Existing optional state, procedure replacement,
or an unknown callback disables specialization. Unknown callbacks receive the
original non-NULL unit, and all subsequent callbacks run normally. Type rebinds
use the full lifecycle dispatcher. Regressions compare complete fresh-unit state
with full dispatch, including an unknown callback mutating later-owner state and
cleanup of existing Hide state.

Combat text classification caches the five immutable defense/attack/weapon enum
results, validating row identities, generations and the five text pointers. It
runs at the original assignment phase after ability initialization. Empty learned
rank arrays return before alias resolution; populated records retain live reads.
Constructor stand transitions and `unit_movedistance` remain in their original
order: the latter seeds the aura deadline at the requested pose before placement.

A compact fresh-plan traversal experiment is reverted: its source284 batch costs
10.222 ms CPU, versus source282's 8.621 ms. An isolated `-O3` module experiment
also costs more (10.864 ms) and is not retained. These are single-batch comparisons,
not statistical guarantees. Diagnostic spawn profiles name the factory and mode
dispatcher; compiler inlining may still remove inner interception sites. Use the
unhooked public `unit_create` batch for raw-cost acceptance, not inner-hook totals.

Source287 additionally borrows the authored membership filter in pure status
query scopes, with exact equality on collisions and live runtime additions and
removals taking precedence. Constant rawcode callers use numeric ownership
queries directly; the string API checks exactly four bytes without scanning a
long name. Three unhooked IceCrown batches measure **8.417, 8.644 and 8.286 ms
CPU** (median **8.417 ms**). Each preserves source221's complete positions,
member records and RNG. This does not demonstrate a timing improvement over
source285's single batch, and remains above the 2 ms target. The complete
repository suite passes, including **2,616 tests / 6,856,904 assertions** in each
Classic/TFT engine mode and 603 pathfinding Python checks. All three release
applications build. The focused unit suites each pass 124 tests / 12,609 assertions.

The source288 presentation probe records actual SDL swaps directly. Depending
on `SCR_UpdateScreen` interception lost samples when local compiler inlining
bypassed that entry; source287's empty swap capture cannot establish acceptance.
The new `swap_cpu_ms` / `swap_wall_ms` fields measure swap work only; host client
timings still include the entire client frame. A native controller regression
exercises swaps without a screen hook, both creation/post-creation phases, the
60-frame completion gate and overflow rejection. Six benchmark-tool tests pass.
The completed source288 capture measures **60 swaps**, a **112.460 ms median**
interval and **159.496 ms peak**, with **57 double-period gaps**. Its ordinary
server frames frequently cost 90–115 ms. Presentation therefore still fails;
raw constructor savings do not resolve the remaining per-frame bottleneck.


### Per-source sight reuse preserves ordered blocker rims

Source293 retains each source's packed base visibility and ordered blocker-rim
candidates. The cache key is the source's edict slot, grid center and radius.
Moving one source rebuilds that source rather than rerasterizing all 4,096 idle
sources. Cached base visibility is unioned into each current viewer; rim candidates
are evaluated against the live, ordered viewer union. Caching the final rim itself
would change the original neighbor-dependent propagation and is deliberately
avoided. Viewer/alliance/visibility changes remain outside cached geometry.

An actual blocker-grid rebuild compares its new union with the prior cell bytes
and builds a summed-area changed-cell table. Each cached source checks its entire
shadowcaster/rim dependency rectangle in constant time. Unchanged regions remain
cached; adding/removing an overlapping blocker that leaves the cell union unchanged
invalidates no source. These are derived caches, freed at fog shutdown, with no
saved-state or scalar-math changes. Packed-byte application preserves every
nonzero visibility value, including temporary rim markers, and clips row tails.

The differential fixture restores identical initial planes before each original
and cached pass, and compares visible/explored cells and all row/dirty flags for
three viewers. Twelve phases cover source motion/radius, blocker motion/death/
revival/overlap, source death/revival, shared vision and exploration masking.
Source293 passes the full repository suite: **2,618 tests / 6,867,341 assertions**
in each Classic/TFT engine mode and 604 pathfinding Python checks. Its unhooked
4,096-unit IceCrown batch measures **7.833 ms CPU** and retains source221's entire
final position/member/RNG record. The synchronous **2 ms target remains open**.

Complete actual-swap captures still fail frame acceptance. Compared with
source288's 112.460 ms median interval and 99.760 ms median active server tick,
source290's per-source cache measures 16.696 ms / 52.446 ms. Source293 measures
16.917 ms / 58.689 ms, with a **167.914 ms peak**, 26 double-period gaps and a
complete 60-swap capture. The low median swap interval does not establish 60 FPS.
The packed-byte writer alone (source292) showed no total-tick improvement over
the scalar cached writer; the remaining costs require separate profiling.

### Acquisition and cleanup avoid unrelated entity scans

Source297 adds a rejection-only enemy-presence grid in `g_ai.c`. Each 512-world-unit
cell holds owner bits for the **full server bounds** of linked monsters. The
existing once-per-frame entity loop rebuilds it; LinkEntity and SetUnitPlayer
expand presence immediately. Unlink/death/motion can leave conservative positives
until the next rebuild. Out-of-map/exceptional bounds contribute global overflow
owner bits. Queries compute directional alliances live, and only reject a region
proven to contain no hostile candidates. Positive queries retain the original
BoxEdicts filter, candidate cap, encounter order and distance tie decisions.
Differential tests cover dense friendly crowds, alliance/owner changes, motion,
visibility/death, relinking, wide bounds and out-of-map targets.

`skills/s_wards.c` replaces the land-mine thinker's repeated world scan with a
reverse owner table. A sparse ordered thinker set handles duplicate candidates,
retaining the lowest matching edict after the first thinker is freed. Live owner
and think-function checks reject reused records. `skills/s_rally.c` similarly
visits an ordered set of producers with rally storage, checking current target
pointer and spawn identity. Pool allocation invalidates producer membership;
release removes it. Map/save replacement resets both indexes, and the first
lookup reconstructs them from authoritative restored entities. No saved fields
or save-format changes were added. A save/load fixture verifies restored thinker
identity, arming deadline, owner identity and rally target invalidation/removal.

Command-card invalidation skips viewers whose cards are already dirty and units
with no selected-player bits. It still visits a shared controller who rebuilt a
card independently while the owner's card stayed dirty. The batch regression
checks both this transition and zero repeated selection scans for dirty viewers.

Source298 passes the full repository suite, including **2,623 tests / 6,869,578
assertions in each Classic/TFT mode**, the restoration fixture and 604 pathfinding
Python checks. Source297's 4,096-unit IceCrown motion capture preserves source221's
complete final positions, members and RNG, with **8.208 ms synchronous creation
CPU**. The map contains 6,630 original objects, including 5,957 static objects
and 4,471 trees; placement-fixture planning is measured separately.

The complete source297 60-swap capture reduces median active server CPU from
source293's **58.689 ms to 46.021 ms**, and mean swap interval from 42.925 ms to
33.722 ms. It still has a **160.651 ms peak and 20 double-period gaps**. Neither
**2 ms creation** nor **uninterrupted 60 FPS** is achieved. Post-spawn cycle
sampling (44,576 samples, no overflow; diagnostic timing only) still identifies
area traversal and fog visibility as substantial server work. Mine/rally scans
are absent from the leading samples.

Two experiments were reverted. Source294 split metadata/ability cache misses
into separate functions but measured 9.288 ms creation versus source293's 7.833
ms, with unchanged final state. Source299 maintained compact area bounds and
ordered links alongside the original intrusive list. It passed 777 differential
checks per mode, including predicate encounter order and world reset, and retained
the complete motion/RNG baseline. Its capture only changed median server CPU
from 46.021 to 44.702 ms and mean swap interval from 33.722 to 32.507 ms, retained
20 double-period gaps, and measured 8.516 ms raw creation. This did not establish
an overall win sufficient to retain the extra index and lifetime maintenance.

## October 4 production-map follow-up

`tools/wc3_pathfinding_benchmark.py` runs bounded public orders on loaded maps,
records executable/library hashes and compiled ABI offsets, and counts original
scenery before spawning movers. `--existing --selected` exercises the actual
selected-unit command owner. Each accepted member must acquire a new request
identity or destination owner while retaining the Move task and order; retaining
an old Move after a failed replacement is insufficient. Reports distinguish
requested units, advancing units and nonzero velocities.

Native frame callbacks use thread CPU and monotonic clocks. JavaScript record
delivery occurs after measurement. `--profile --profile-detail owners` separately
bounds scheduled thinks, Move timers, pose sampling, flow jobs and order work,
counting nested owners once. Scheduled thinks include other ability owners, so
this is a conservative bound for point-Move traffic. Native orders executed
inside a Gum listener suppress nested interception; their CPU is charged
explicitly. Older `perf127/128` owner totals omit that order contribution and
must not be used for complete-frame budget acceptance. Diagnostic hooks add
overhead; use separate unprofiled captures for rendered FPS and total CPU.

The first-tick hook is now primed before the first post-order game body. Reports
before `perf144` omitted the 6100-ms simulation tick from frame records, although
function totals observed it. Their averages are partial/steady checkpoints, not
complete command-to-motion budget acceptance. Initial setup and submission at
6000 ms are separate records; subsequent submission is included in its frame.
`perf144` exposed a 94.71-ms movement cost on that previously omitted first tick
in the 1,024-unit workload; its concurrent build makes it diagnostic evidence,
not a quiet timing comparison.

**The movement allowance is 0.8 ms in each display frame: 5% of the specified
16-ms frame budget.** A 100-ms simulation tick executes inside one host/display
iteration; dividing its cost among six display frames is invalid. Initial order
submission, subsequent orders, search, steering and pose publication must fit
together in that iteration. Both CPU time and elapsed movement time are measured.
One-second FPS averages do not establish solid 60 FPS. Earlier amortized-budget
acceptance claims are withdrawn; all measurements below are diagnostic results.

Historical partial-frame release checkpoints (`-O2`, native SDL2, Intel/Mesa;
first simulation tick omitted as described above):

| Workload and external report | Result | Limit |
|---|---|---|
| Rise of the Naga, seven starting units plus five Archers, actual selected commands; `perf133-selected-render-12.jsonl` | Four moves retain all 12 members; 159 simulation frames average 4.09 ms of whole game CPU; 16 measured one-second render windows span 56.23–60.00 FPS at 1920×1080 | Individual intervals reach 88.60 ms around instrumented setup; window FPS is not an every-frame guarantee |
| Same selected workload, separate owner timers; `perf134-selected-owners-12.jsonl` | Movement average 0.437 ms/tick, p95 0.718, max 1.987; whole game average 3.59 ms/tick | Does not establish the 0.8-ms display-frame limit; first search work was omitted |
| IceCrown, 1,024 independent public moves, 128 spacing and 512-unit legal corridors; `perf137-icecrown-owners-quiet.jsonl` | Original 5,957 static scenery entities and 4,471 trees remain; movement average 8.85 ms/tick, 7.59 excluding the replacement-order tick; replacement submission 54.86 ms | Fails the 0.8-ms display-frame limit; only 145 units advance on an average tick, 387 at peak |
| IceCrown, 2,048 units in 12-member cohorts; `perf121-icecrown-2048.jsonl` | Scenery retained; whole game average 27.06 ms/tick; all initial members acquire Move | Only 25 units advance on an average tick; this is a capacity/lifecycle check, not 2,048-moving-unit performance |

Corrected complete-tick captures, with no competing build/test process:

| Workload and external report | Result | Limit |
|---|---|---|
| Selected Rise of the Naga, rendered; `perf148-selected-full-render-12.jsonl` | All 12 acquire a new Move on four commands; 160 ticks average 4.04 ms whole game CPU; 16 one-second windows span 55.47–60.00 FPS | Individual render intervals reach 98.87 ms around instrumented setup; this is not an every-frame guarantee |
| Same selected workload, owner timers; `perf147-selected-full-owners-12.jsonl` | Movement average 0.409 ms/tick, p95 1.033, max 1.556; first post-order tick 1.033 ms; initial submission separately 0.918 ms | Fails the 0.8-ms limit: p95/max ticks exceed it before initial command work is added |
| IceCrown, 1,024 independent moves after gate indexing; `perf146-icecrown-gate-index.jsonl` | Movement average 8.69 ms/tick, first tick 53.82, max 83.57; initial/replacement submission 108.93/49.56 ms; all members accept both commands | Fails the 0.8-ms display-frame limit; average 144 advancing units and 131 nonzero velocities, not 1,024 simultaneous movers |

`S_WaygateBuildEdges` previously scanned every edict for every adaptive query,
even on maps without gates. An ordered ownership bitset now limits repeated
queries to gate owners; activation, deferred removal, ability aliases and
destination words remain live. Allocation uses the same ordered owners and
lowest available 1..255 ID. Initialization and save/map replacement reconstruct
derived membership once. Exhausted ID0 gates retain their original allocation
and publication policy. Eighteen Way Gate tests cover ownership, exhaustion,
deferred replacement, mutation and restoration, including a work-bound regression
with 1,900 scenery actors.

The quiet pre-index capture `perf149-icecrown-before-gates-quiet.jsonl` averages
9.09 ms of movement per tick, with 72.64 ms on the first tick and 97.29 ms maximum.
The indexed capture retains identical final position words, member state and RNG.
Steady ticks excluding the two command/search spikes are effectively unchanged
(7.15 ms before and after); the observed gain is in route setup, not steady
steering. These single paired captures do not establish a general speedup.

The final format101 release (`perf151-icecrown-final-owners.jsonl`) reproduces the
indexed capture's final positions, members and RNG exactly. Movement averages
8.72 ms/tick (7.20 outside the two spikes), with 54.62 ms on the first tick and
81.44 ms maximum; initial/replacement submissions take 111.67/47.80 ms. This
confirms that mass movement still fails the target. Diagnostic `pipeline` and
`calls` details omit some owner timers and must not establish budget acceptance;
their function totals overlap.

The diagnostic `perf152-icecrown-pipeline.jsonl` records 258,894 group-route visits,
221,815 fine-route lookups, 255,882 adaptive-progress calls and 4,097 gate-edge
builds. Group-route time is 195.20 ms over the run, including nested fine-route
time of 174.57 ms; motion commit totals 121.14 ms. These hooks add per-call cost
and do not isolate expansion time. Some exported helpers are inlined into their
callers, so zero intercepted calls do not mean zero work. Repeated route servicing
requires profiling alongside fresh search work; a new coarse graph alone cannot
remove local movement/steering cost. Final position, member and RNG words match
the owner capture exactly.

The retry-member lookup now uses the physical group's bounded member rows.
Against the previous implementation, the 1,024-unit run retains identical final
position words, member state and RNG words while reducing the older diagnostic
movement total from 15.88 to 8.95 ms/tick (`perf127/128`). These totals exclude the
native order contribution as described above. A monotonic reserved-ID upper
bound then removes whole-world collision checks from ordinary request allocation;
wrap and IDs restored ahead of the counter retain authoritative checks. The
matched replacement submission falls from 142.41 to 59.32 ms (`perf130/131`), again
with identical positions, members and RNG. Physical owners maintain creation-order
links and the lowest reusable slot; restoration sorts once and every owner pass
freezes pointer/sequence pairs so callback-created groups wait until the next pass.

Free-edict discovery similarly maintains an ordered candidate bitset. Spawn/free
notifications update membership; restore reconstructs once. Allocation keeps the
original lowest eligible slot and exact unsigned cooldown predicate, including
time wrap, without walking thousands of occupied scenery slots for every request.

Repeated pure pose prediction uses a 256-entry working-set cache keyed by actor
address and all 68 bytes of its inputs: world/fine/published coordinates, map
origin, velocity, current/committed clock and validity flags. Hits return the
original software-arithmetic result; collisions recompute it. No movement state,
clock or velocity is committed by the cache. Twenty mutation cases compare raw
pose words and cache hits/misses, including signed zero and clock wrap. This
avoids repeating coordinate conversion within one owner decision while keeping
callback writes observable without a separate invalidation protocol.

The former 256-waypoint ring could overwrite a destination still owned by a
moving unit. Destination leasing now traces live roots and grows ordinary edict
storage when all retained destinations are referenced. The event ring supports
normal world-sized issued-order batches; Food and Shadow Meld state scale to the
edict limit. These are correctness/capacity fixes, not evidence of equal retail
trajectories at arbitrary population. Mixed/air selections and the remaining
physical-group admission policies still require their native witnesses.

Reports are under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/`. For example:

```bash
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 \
/home/lofcz/.local/share/uv/tools/frida-tools/bin/python \
  tools/wc3_pathfinding_benchmark.py \
  --binary /GitHub/wc3-analysis/perf150-release/bin/openwarcraft3 \
  --library /GitHub/wc3-analysis/perf150-release/lib/libgame.so \
  --data /run/media/lofcz/ssd_external/Games/w3 \
  --map 'Maps/(12)IceCrown.w3m' --units 1024 --placement corridor \
  --spacing 128 --distance 512 --cohort 1 --duration 8000 \
  --profile --profile-detail owners --output /tmp/wc3-icecrown-owners.jsonl
```

Build the explicit `openwarcraft3` target in an isolated release output tree;
plain `make BUILD=release` selects the default Lua target. Run timing captures
without competing builds/tests and validate artifacts before attributing changes.
Sampling experiments with insufficient PC samples are excluded from acceptance.

The files passed to `--binary` and `--library` must exist before invoking the
harness. For a repository-local release output tree, build first:

```bash
make -j6 BUILD=release \
  BIN_DIR=build/icecrown-perf/bin \
  LIB_DIR=build/icecrown-perf/lib \
  SHARE_INSTALL=build/icecrown-perf/share openwarcraft3
```

Then pass `--binary build/icecrown-perf/bin/openwarcraft3` and
`--library build/icecrown-perf/lib/libgame.so`. The harness reports missing
files before importing Frida or inspecting the game ABI.

For manual play, use `--interactive`. This implies `--render --keep-open
--spawn-only`: the harness creates the units, records post-spawn presentation,
removes its interception hooks and submits no movement orders. Ordinary unit
AI remains active. The terminal keeps Frida storage alive until the game closes;
closing the window or pressing Ctrl+C finishes the session. For example:

```bash
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 \
/home/lofcz/.local/share/uv/tools/frida-tools/bin/python \
  tools/wc3_pathfinding_benchmark.py \
  --binary build/icecrown-perf/bin/openwarcraft3 \
  --library build/icecrown-perf/lib/libgame.so \
  --data /run/media/lofcz/ssd_external/Games/w3 \
  --map 'Maps/(12)IceCrown.w3m' \
  --units 4096 --unit earc --placement corridor --spacing 128 \
  --distance 512 --path-threads 1 --interactive \
  --output /tmp/icecrown-4096-interactive.jsonl
```

`--render --keep-open` alone still runs the automatic stress workload for
`--duration` and retains its last orders afterward. It does not create an idle
manual-play session. In the 4096-Archer IceCrown reproduction, those repeated
orders left about 3600 requests in the player's fine-search FIFO. A subsequent
three-member public Move turned the units but remained queued beyond ten
simulation seconds. In the rendered idle-creation check, all 4096 Archers had
no Move order and the FIFO was empty before the command. The same three-member
order advanced all three Archers within the next 100-ms simulation tick.
This explains the original interactive-demo delay. The interactive option itself
does not change search budgets, queue ordering or movement rules; the scheduler
policy below addresses admission under the automatic workload.

Keep-open disables the engine's automatic frame limit; the capture timeout
still cleans up an incomplete or failed run. After capture completion the
timeout no longer terminates manual play.

### Responsive fine-search admission

`wc3_path_scheduler responsive` is the default for new maps. The benchmark
records this choice and exposes `--path-scheduler responsive|retail`.
The policy is fixed at map creation and saved with the simulation. Loading a
save restores its policy rather than reading the current console setting.

Retail's player-wide FIFO allows 1100 charged pops every two owner updates.
An ordinary search can charge 701 pops, including the denied next iteration.
Therefore faster execution alone cannot remove the large-battle admission
delay. With user authorization to change movement start times, responsive mode
freezes `max(1100, queued_count * 701)` work at each existing reset boundary.
Every request already waiting has enough credit for one bounded search. The
grant does not shrink as requests leave the FIFO, and unused demand grants
expire at the next reset. Admission, charging and reset are constant-time;
the queue remains intrusive and ordered. Search algorithms, request limits,
retry intervals and publication order are unchanged. No wall-clock budget,
worker completion or client selection chooses admission.

This intentionally changes crowded simulation outcomes relative to retail:
earlier movement changes subsequent occupancy observations. It does not promise
identical retail trajectories under contention. `--path-scheduler retail` keeps
the original admission limit for parity investigations. Existing frozen retail
tests explicitly use that policy; responsive scheduling has separate coverage.

The lifecycle audit also corrected `move_adaptive_waypoint`: starting a new
adaptive search first removes the old fine-queue entry, as original `166c30`
does through `168800`. A denied subsequent fine refill joins the tail. Retrying
an already retained adaptive leg preserves its place. The regression failed
10 assertions before this correction and passes afterward. The corresponding
Ghidra functions carry a saved `OpenRealm_queue_lifecycle` tag explaining both
the recovered lifecycle and the separate engine policy.

Real IceCrown, 4096 Archers, original scenery, independent orders:

| Measurement | Fixed retail admission | Responsive admission |
|---|---:|---:|
| Units advancing in first 100-ms simulation tick | 71 | 4062 |
| Mean advancing units, 20-second repeated-order run | 119.14 | 2074.115 |
| New three-unit order after stress recording | Still queued after 10 seconds | All advancing within 200 ms |
| Median whole-server CPU per simulation tick | 22.884 ms | 81.366 ms |

These are headless diagnostics, not a 60-FPS acceptance claim. More units
actually moving require more work: first-tick CPU remains 308.170 ms and peak
CPU is 406.056 ms in the responsive long run. The 0.8-ms pathfinding allowance
and whole-frame target remain unmet. Separate two-second 4096-unit captures
with a replacement order produce identical complete final positions, members
and RNG with the worker enabled and disabled. Reports are external under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/queue-*`.

Save108 adds the policy and frozen grant to existing level/bucket serialization;
it rejects older save formats. Tests cover FIFO demand at 1, 12, 256, 1024,
1600 and 4096 requests, partial spending across save/load, malformed grants,
and adaptive replacement. Retail behavior is not inferred from responsive
stress results.

Validation: explicit debug/release builds and `make test` pass; Classic and TFT
each run 2,559 engine tests with 6,818,215 assertions. The benchmark's two Python
contract tests and ability registration audit pass. Eighteen fresh strict corpus
reports pass their declared expectations, including the two retained adaptive
reference differences; those differences are not retail-parity closures. Save101
rejects older formats and retains current waypoint/event continuation. Network
transport and client behavior are unchanged.

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
and reparsing source ability lists for each recipient. Provider discovery now
resolves Devotion and Unholy alongside the regeneration families after actual
ownership/data changes, rather than every frame or ordinary nonsource spawn;
presentation still checks live range, alliance, and target rules per recipient, but
iterates cached providers. Provider spawn identity, activity and current authored
rank remain live. Map replacement, restoration, source removal and ability-list
changes invalidate discovery. Overlays still update every frame; recipient numeric
bonuses retain the existing two-second refresh.

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

### Constructor dispatch and saturated presentation (runtime333–334)

Fresh initialization plans now encode the next actual initializer and the OR of
constant returns across each no-op run. The constructor walks actual callbacks
and mutation barriers instead of revisiting all skipped registry entries. Registry
procedure replacement goes through `S_ReplaceAbilityProcedure`: it advances both
the registry generation and borrowed-plan epoch without freeing an active plan.
Unknown procedures retain complete dispatch; `UNIT_INIT_RUN` rechecks dependencies
before the following block, while `UNIT_INIT_RUN_LOCAL` forbids metadata, registry,
ability-ownership and callback mutation. A regression replaces a later initializer
inside a recognized callback and compares the complete instance and invocation
counts with ordinary dispatch. Direct registry procedure writes bypass this
contract and must not be introduced.

Classic and TFT focused validation: 23 dispatch tests / 12,929 assertions and
5 constructor tests / 9,371 assertions. The focused constructor group exposed an
existing fixture restoration bug: `G_SetSLKRows` treated an absent original table
as a null text parse, leaving the temporary stack-backed replacement installed.
An empty table now restores as an empty table; it does not parse null text.

The complete runtime331 suite passed Classic and TFT independently (2,639 tests,
7,095,128 assertions each). Paired raw IceCrown captures in one measurement window
recorded 14.480778 ms creation with runtime327 and 11.362129 ms with runtime331;
the latter accepted all 4,096 orders in 29.816581 ms. Both complete final records
match `perf221-icecrown-raw-spawn-4096.jsonl`. These costs still fail the targets.
Runtime331's bounded rendered spawn capture timed out before the measured batch,
so it supplies no presentation acceptance evidence.

A saturated presentation estimate previously clamped simulation work between
checkpoint redraws to 250 microseconds. A 25 ms redraw could then insert ten
seconds of repeated stale presentation into 100 ms of simulation work. The host
now allows a full display period of simulation work when the render reserve
already exhausts the deadline. Below saturation, normal deadline reservation is
unchanged. The deterministic scheduling regression checks six redraws / 250 ms
total for that synthetic workload, then recovery after the old peak expires.
This prevents starvation; it does not make an over-budget renderer meet 60 FPS.
Runtime334 rendered validation is still pending.

Runtime334's rendered capture completed in 26.53 seconds, including the real
IceCrown batch and 60 subsequent host frames. Creation was 10.572515 ms CPU;
178 presentation intervals were recorded, all over budget, with a 191.418671 ms
maximum. This is a starvation fix, not performance acceptance. Do not compare
inclusive `G_RunEntities`/`G_FowUpdate` timings directly against earlier builds:
these owners now contain checkpoint presentation, which must be attributed to
the client before reporting their simulation CPU cost.

The benchmark accepts `--unit-types earc,hfoo,ogru,hrif` to cycle rawcodes in
synchronous creation order. A single-type run retains its literal rawcode in
the native timed loop. Mixed runs perform no asset or runtime-definition warming
outside that loop. The existing `--placement region --spacing 16` provides a
crowded requested-position variant; admitted positions and failures remain
owned by the ordinary public native. Mixed final records require their own
baseline; they cannot be compared to the Archer-only perf221 final record.

### Scalar alignment (runtime335)

`wc3_add_bits` now aligns and sums unsigned 32-bit words instead of widening
both signed significands to 64 bits. Each doubled significand has at most 25
magnitude bits, so the aligned sum fits signed 27 bits. XOR/sign-mask alignment
reproduces arithmetic right shift, including negative truncation, without
implementation-defined signed shifts. Exponent guards, cancellation, exceptional
words and final normalization remain unchanged. Differential tests cover all
512 × 512 exponent/sign pairs with six boundary mantissas per operand (9,437,184
pairs), plus one million deterministic random pairs, at O0 and O2. This does not
substitute host floating-point arithmetic for retail scalar operations.

### Snapshot support traversal and construction inputs (runtime336)

A rendered IceCrown post-spawn hardware sample attributed 16,932 of 57,028
instruction-pointer samples to `R_ConformGroundSurfaces`: each conforming unit
scanned every snapshot entity, including ordinary scenery. The renderer now
compacts visible walkable surface providers once, preserving snapshot order,
then performs the unchanged exact MDX queries. Cost is O(N + U*S), where S is
actual walkable surfaces, instead of O(U*N). With no surfaces it returns after
one linear pass. This is not yet a spatial index for maps with many bridges.

The same diagnostic in runtime336 recorded 88 of 52,079 samples in this owner.
These are sampled weights, not isolated function timings. The capture still
failed: 170 presentation intervals, 99 beyond one refresh period, four gaps
beyond two periods, peak 55.443265 ms. Creation was 8.068043 ms CPU, still above
2 ms. The renderer regression suite passed 146 tests / 6,041 assertions.

Prepared initialization plans now distinguish UnitData identity as well as the
ability row. Creep Sleep can fold its fresh-unit no-op for a type whose data
explicitly disables sleep, without folding the owner-dependent sleeping case.
Classic and TFT dispatch tests each passed 24 tests / 12,936 assertions.

`perf336-spawn-sample.jsonl` retained the complete final position/member/RNG
record from `perf221-icecrown-raw-spawn-4096.jsonl`. Its sampled constructor cost
is not an acceptance timing. An unsampled mixed `earc,hfoo,ogru,hrif` batch also
completed (9.804860 ms creation CPU, 16.615289 ms wall); it does not meet the
creation target, and mixed-type retail trajectory equivalence is not established.

### Ordered indexes and construction storage (runtime337–341)

- Acquisition presence now bounds finite out-of-map providers per owner instead
  of treating every overflow provider as potentially near every query. Invalid
  geometry remains conservatively unbounded; live alliance checks are unchanged.
- Selection has an ordered derived membership index. All selection writers use
  `G_SetEntitySelectionMask`; save restoration rebuilds it. See
  [selection and control](selection-and-control.md). Runtime338 Classic and TFT
  each passed 2,643 tests / 7,095,299 assertions.
- Move categories are parsed on owned UnitData load/edit. Borrowed or rebound
  rows whose source string differs are decoded live. This does not treat the
  authored `fly` string as a substitute for the existing `AI_FLYING` path flag.
- Fine-search lookup packs its 15-bit node identity and 17-bit scratch generation
  in one word, reducing the lookup arrays from 512 to 256 KiB. Generation wrap
  clears lookup storage. Heap order, node admission and work budgets are unchanged.
  The full 32,768-node identity test and O0/O2 retail fine-search corpus pass.
- `G_ClearEdictStorage` records which arena slots are initially zero. `G_InitEdict`
  omits its second clear only for never-used slots in that proven range. Reused
  slots still clear fully; save restoration marks its populated prefix used.
  Allocator-cache resets do not erase this evidence. Initial arena preparation
  still performs and accounts for the original bulk clear.

These changes have **not established a creation timing improvement**. Unsampled
runtime338 creation was 8.014309 ms CPU; runtime341 was 9.201373 ms CPU (9.245837
wall), with 80.524244 ms fixture placement preparation reported separately.
Runtime341's final position/member/RNG record exactly matches perf221. The
runtime338 unsampled rendered run also failed: 106 intervals, 57 beyond one
period, six beyond two periods, peak 41.371211 ms. Hardware sampling and host
checkpoint cadence differ between captures; do not infer isolated savings from
these aggregate timing differences.

### Incremental aura ownership (runtime342)

Aura records are now addressed by edict identity. Hierarchical bitsets track
regeneration/combat, Slow and Endurance membership in ascending edict order;
separate dirty sets coalesce repeated changes to a single provider. Type binding,
runtime ability enable/disable, learned ranks and removal notify the local owner.
Metadata changes and load/reset still invalidate the complete derived registry.
Removal notifies again after clearing because a removal callback can consume the
first notification while the entity is still live.

Previously each ownership change discarded the whole provider list. A query
inside a creation loop rediscovered all previous providers and unrelated actors.
The new discovery work after initialization is proportional to changed identities,
with constant-time membership updates and no shifting of dense ordered arrays.
Recipient evaluation still traverses eligible provider sets; this change does
not claim spatial acceleration for thousands of overlapping aura providers.

Overlay recovery has a separate frame stamp: a local provider change no longer
rescans every entity for overlay effects in the same frame. Overlay creators
already maintain recipient pointers synchronously; full invalidation and the
next frame retain the ordered recovery scan. Recipient refresh deadlines, live
range/alliance/visibility checks and floating-point stacking order are unchanged.

Classic and TFT dispatch tests each pass 26 tests / 13,460 assertions, including
128 local provider additions among 4,096 unrelated units (128 discovery visits,
zero repeated overlay visits), rank changes, removal and forced-full-rebuild
comparisons. Endurance checks 64 additions with exactly 64 discovery visits and
bit-exact ordered multiplication against full rebuilds after removals.

Runtime342's full Classic and TFT runs each passed 2,648 tests / 7,161,456
assertions. Its unsampled Archer-only creation was 9.272169 ms CPU (9.326979 ms
wall), still failing; the complete final record equals perf221. The aura change
addresses provider-heavy mutation scaling, not a demonstrated homogeneous-batch
speedup. The headless pipeline diagnostic recorded 528,186 group-route visits
and 528,739 motion commits over four simulation seconds; interception timings
are not acceptance timings.

### Retained task destinations (runtime343)

`G_UnitMoveGroupDestination` now retains the exact world request whose clipped
fine destination the route already owns. Repeated owner ticks reuse that goal
when request bits, map revision, mask and world-transform revision still match.
A changed request is still clipped first: two distinct out-of-bounds requests
that admit the same goal retain the original sampled footprint and route.

The transform revision includes world bounds, center, terrain dimensions and
pathmap dimensions. Cache keys are process-local derived data, cleared in both
unit and group serializers. Save format105 rejects earlier layouts. The native
route goals, selected indexes, points, work budgets and encounter order are
unchanged. A focused test performs 4,096 retained queries with zero repeated
goal conversions, then covers equivalent clipped requests, changed world
bounds and save restoration (12,309 assertions).

Rendered owner diagnostics currently include presentation checkpoints nested
inside a scheduled owner and record outer host iterations. Treat those counters
as conservative upper bounds, not isolated movement CPU or actual swap gaps.
The `--render --spawn-only` presentation capture separately records actual SDL
swaps. Neither the runtime342 owner capture nor any earlier capture demonstrates
the requested movement/frame budget.

Runtime343 passed the full Classic and TFT suites, each with 2,649 tests /
7,173,765 assertions. The Archer-only final record still exactly equals perf221.
Bounded IceCrown results below are CPU milliseconds; simulation ticks are
100 ms and **must not be presented as 16-ms display frames**.

| Units | Public creation | First simulation tick | Median later tick |
|---:|---:|---:|---:|
| 1 | 0.259 | 3.954 | 1.896 |
| 12 | 0.294 | 4.040 | 1.862 |
| 256 | 0.856 | 31.596 | 6.068 |
| 1024 | 2.142 | 60.976 | 9.757 |
| 1600 | 3.316 | 89.173 | 12.277 |
| 4096 | 7.450 | 304.597 | 25.741 |

The mixed `earc,hfoo,ogru,hrif` batch took 7.966058 ms creation CPU. A separate
12-unit selected-group run accepted all 12 Move orders. These results remain
above the required budgets. The runtime342-to343 timing difference combines
route memoization, empty dirty-set rejection and run-to-run variation; it does
not isolate any single change's speedup.

### Immutable sound definitions (runtime344)

Authored selection, order, ready, chop, attack and death sound identities now
live in immutable shared profiles. The instance retains only the 16-byte pending
event state and one profile pointer. Registration preserves its original order;
changing one profile does not mutate another unit, and clearing resource bindings
does not destroy profiles held by live instances. Save format106 writes logical
profile values, validates variant counts and excludes process pointers.

The edict shrinks from 2616 to 2560 bytes. Classic and TFT each passed 2650 tests /
7,173,821 assertions. The unsampled IceCrown batch took 7.781830 ms creation CPU;
its complete final position/member/RNG record equals perf221. The first simulation
tick took 296.708065 ms CPU. The separate rendered spawn capture took 6.553720 ms
creation CPU and recorded 125 presentation intervals, 60 beyond one period and
six beyond two periods, with a 35.979898-ms peak. Neither target is met.

A movement hardware-IP sample recorded 42,102 samples without an overflow.
Adaptive search, predicted poses and movement dispatch remain prominent. This
sampler does not capture call stacks; symbol weights are diagnostic leads, not
exclusive owner timings or proof of a particular architectural speedup.

### Shared animation state (runtime345)

Animation requests and property sets now use immutable shared records. Prepared
type definitions bind normalized properties at the original construction stage.
Warm requests retain their parsed primary family and resolved selection without
text copies, token parsing or direct-mapped-cache eviction. Walk variants still
consume the same ordered RNG draws. This shrinks the edict from 2560 to 2368 bytes.
Classic and TFT each pass 2652 tests / 7,186,146 assertions.

The unsampled Archer batch took 6.705618 ms creation CPU; its complete final record
exactly matches perf221. First-tick CPU was 282.756869 ms and median later-tick CPU
25.000813 ms. Mixed `earc,hfoo,ogru,hrif` creation took 8.009345 ms CPU. The rendered
spawn capture had 136 swap intervals, 56 beyond one period and two beyond two
periods, peaking at 38.469908 ms. Targets remain unmet. A preliminary capture that
overlapped the release build is explicitly excluded from these results.

The newly measured crowded case uses `--placement region --spacing 16` with 4096
units in the same IceCrown map. Creation took **822.389253 ms CPU**, exposing the
repeated overlapping footprint tests in the bounded placement rings. This
scenario is separate from the spaced corridor acceptance case; its final record
is retained for differential validation of the placement replacement.

### Ordered placement rejection index (runtime346)

Public placement and Stop recovery now keep query-local row/column rejection
bitsets. A rejected cell supplies a proven blocking rectangle. Expanding that
rectangle by the mover footprint rules out every center whose footprint must
intersect it; bit scans skip those centers in the existing bottom/right/top/left
ring order. Initial legal points retain their exact scalar pair and require no
index initialization. Terrain-level admission still runs on surviving candidates.
Portal callbacks retain the scalar traversal because their observations differ.

The witness is read from the current ordered occupancy structure, after ordinary
dirty publication. It is discarded at query end; no blocked state survives unit
creation, removal, type changes or movement. Fine occupancy queries without a
target observer can return on the first blocker. Queries observing target ranks
still traverse the complete cell and retain their rank comparison.

Differential kernel validation compares 12,000 randomized placements at O0/O2
with UBSan, including all footprint classes, limits 0–32, negative/fractional
points, integer outputs and admission rejection. A single fully blocked window
requires one cell read instead of 3969. The live game test compares indexed and
scalar placement across collision sizes, moving blockers, flyers, hidden units,
virtual captains, disabled pathing and repositioning, including publication rank
equality. Constructor stage traces remain unchanged. Classic and TFT each pass
2653 tests / 7,189,030 assertions. The broader tool suite exposed an unused
header-function warning under `-Werror`; its indexed entry is now `static inline`
and all 604 affected standalone pathfinding tests pass.

The crowded IceCrown batch falls from 822.389253 to **103.900295 ms creation CPU**;
its complete final position/member/RNG record exactly equals runtime345. The
spaced Archer batch is 6.997376 ms CPU and still exactly equals perf221. This is
an approximately 7.9-fold crowded-case improvement, but neither creation target
is met. Its first crowded simulation tick still takes 854.308245 ms; reducing
placement does not remove the subsequent movement work.

The rendered spawn capture records 129 swap intervals: 60 beyond one period,
three beyond two periods, and a 37.395557-ms peak. Creation takes 6.223710 ms CPU
in that capture. Presentation acceptance remains unmet.

### Spatial publication and active construction plans (runtime347–348)

The server retains the bounds associated with each spatial entry. An unchanged
relink can reuse its BSP node and hash membership while still moving the object
to the tail of its Quake list and advancing both publication serial and mutation
revision. Bounds are recomputed from current position/collision before comparing
the retained geometry. NaNs take the complete path. The spatial differential test
passes 1000 assertions, including repeated relinks and caller-modified bounds.

The unsampled runtime347 IceCrown batch takes 6.877938 ms creation CPU and matches
the complete perf221 final record. This small timing difference is not evidence
of a substantial speedup, and the creation target remains unmet.

Execution-plan clearing now retires plans referenced by active event dispatches.
Nested constructors immediately acquire new plans; old plans are reclaimed when
the outermost dispatch returns. This fixes a possible use-after-free when an
initialization callback clears the registry plans. The regression compares nested
prepared construction with complete dispatch and checks callback counts, final
instance state, and reclamation. All 27 focused dispatch tests pass 13,478
assertions. Runtime348 passes the full suite, with 2654 Classic and TFT engine
tests and 7,189,304 assertions per edition.

### Adaptive request epochs and clear-ancestor aliases (runtime349)

Adaptive lookup stores the request epoch above the original 16-bit node
identity. Ordinary resets advance that epoch rather than clearing every map
plane or visiting all previously indexed nodes. Clear descendant cells retain
aliases to their canonical ancestor for the current request. Creating nodes,
choosing representatives, publishing low-16-bit identities and heap ordering
remain unchanged. Epoch wrap clears all planes; the complete-clear reference
path remains available.

The standalone O0/O2 UBSan differential exercises 6000 requests with changing
class planes, footprints, work budgets, repeated descendants, epoch wrap and
16-bit node-identity wrap. The full suite including the attacked-object geometry
regression passes 2655 Classic and TFT engine tests with 7,192,928 assertions
per edition.

The unsampled 4096-Archer IceCrown capture takes 6.882618 ms creation CPU,
285.319508 ms for the first simulation tick and 25.638251 ms median subsequent
tick CPU. Its complete final position/member/RNG record exactly equals perf221.
These measurements still fail the creation and frame-budget targets; they do
not establish a meaningful creation speedup over runtime347.

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
