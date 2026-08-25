# Fog of War Algorithm Report

## Executive Summary

WC3 gameplay visibility is an authoritative low-resolution per-player grid, but the renderer also retains an older
high-resolution path that attempts per-revealer occlusion by drawing entity silhouettes. That visual path can look
nice, but it scales poorly when many revealers and blockers are present.

Recommended direction: keep the authoritative grid, but calculate only the connected viewers and their shared-vision
owners. Rebuild blocker cells only when blocker state changes, then run grid shadowcasting for those viewers. Upload
the resulting `R8` data once per packet and let the renderer filter it. Do not replace this with a result cache: the
large win is eliminating irrelevant players and repeated whole-grid/publication work.

## Current Implementation

Relevant files:

- `renderer/r_fogofwar.c`
- `renderer/r_draw.c`
- `client/cl_view.c`
- `common/shared.h`
- `docs/games/warcraft-3/architecture/map-renderer.md`

The current renderer path:

1. Collects revealers from `tr.viewDef.entities` whose `team == tr.viewDef.player`, not hidden, and flagged `RF_FOW_REVEALER`.
2. Collects blockers flagged `RF_FOW_BLOCKER` and with `radius >= 10`.
3. Builds one blocker vertex buffer if each blocker is near any revealer.
4. For each revealer:
   - Clears alpha in `FOW_RT_IMMEDIATE`.
   - Draws a smooth circular sight texture.
   - Draws blocker silhouette wedges using a ray-cast-like shader.
   - Draws white visibility through the resulting alpha mask.
5. Accumulates `FOW_RT_IMMEDIATE` into `FOW_RT_HISTORY` with `GL_MAX`.
6. Produces `FOW_RT_RESULT` as half history plus half immediate.
7. Samples the result in world/minimap shaders.

The authoritative WC3 path now also receives RLE-compressed `svc_fogofwar` row chunks from the server. The client
decodes each chunk into `visible`/`explored` planes, updates only those rows in its combined R8 buffer, and publishes
the assembled texture once after the complete server message. The renderer defines storage on the first update and
uses `glTexSubImage2D` while dimensions remain unchanged. Chunk boundaries must never trigger independent texture
uploads: they are transport framing, not distinct visual states.

Server reveals are viewer-centric and do not cache reveal results. Each update builds a stack-local bitmask of
connected viewers plus a stack-local owner-to-viewer mask, clears only those grids, and applies each source directly
to viewers that receive its owner's vision. Entities owned by players with no consuming viewer are rejected before
revealer classification, and their unused grids are never touched. Active fog modifiers follow the same viewer rule.
The player must be marked via `G_FowConnectPlayer` before the first update so its full sync contains current visibility.

On Orc01, a like-for-like 1 kHz Time Profiler capture over 1,200 bounded frames measured `G_FowUpdate` falling from
430 ms to 61 ms inclusive across the capture (86% lower). The unit-reveal subtree fell from 227 ms to 9 ms (96%
lower). `G_FowBlockersChanged` is now the largest remaining FOW component at 41 ms across the capture; optimizing that
requires explicit blocker lifecycle invalidation and is separate from the no-cache reveal algorithm.

The original client decoder expanded every run one cell at a time, doing division and modulo for each cell. It then
rescanned the complete grid and called `glTexImage2D` after every chunk. A 1 kHz Time Profiler capture after replacing
that work with `memset` runs, row-local compositing, packet-level publication, and `glTexSubImage2D` measured 2 ms
inclusive in `CL_ParseFogOfWar` over the complete 20-second capture; the decoder had no sampled self time. Reproduce
with:

```sh
xctrace record --template 'Time Profiler' --time-limit 20s --launch -- \
  build/bin/openwarcraft3 -data 'data/Warcraft III' +map 'Maps\Campaign\Orc01.w3m' +com_frame_limit 1200
```

[WarSmash](https://github.com/Retera/WarsmashModEngine) is a useful behavioral reference, not a fast implementation to
copy verbatim: it uses an ordinary logical player fog grid and separates that state from minimap presentation.
[OpenRA](https://github.com/OpenRA/OpenRA) likewise treats shroud as simulation-owned spatial state exposed to
rendering. Both support the same architectural boundary used here: visibility is ordinary game data; texture work is
a publication step, never part of RLE parsing.

Important constants:

- `FOW_UPDATE_INTERVAL_MS = 100`
- `SIGHT_DISTANCE = 2000`
- `SIGHT_SIZE = 64`
- FOW render target resolution is map shadow resolution, roughly `(width - 1) * 4` by `(height - 1) * 4`.

## Complexity Smell

The expensive part is not just the render-target size. It is the combination of broad blocker search and repeated rendering:

- `R_CasterNearRevealers` makes blocker collection `O(num_blockers * num_revealers)`.
- The resulting blocker buffer is drawn once per revealer, so raster work trends toward `O(num_revealers * nearby_blocker_vertices)`.
- `SIGHT_DISTANCE` is fixed at `2000`, not per-unit sight radius, so many units may reveal too much and search too widely.
- The legacy renderer visibility and history are GPU-only; WC3 gameplay must continue using its authoritative server grid.

Verdict: yes, it is overly complex for the first fast, correct RTS FOW pass. The current path is closer to a visual shadow/occlusion experiment than a robust RTS visibility system.

## Modern RTS Behavior Target

Public StarCraft II documentation does not expose Blizzard's exact internal algorithm, but its behavior strongly suggests a grid/layer visibility model rather than per-object renderer-only shadowing:

- Fog has at least three player-facing states: unexplored/dark, explored/grayed, and currently visible.
- At game start, multiplayer terrain and fixed resource locations are visible, while enemy movements/new structures remain hidden in fog.
- Units, structures, and abilities provide active vision.
- High ground, ramps, smoke/undergrowth, and sight blockers affect vision.
- The public StarCraft II learning/API interface exposes visibility as feature layers: fixed-size spatial grids where each pixel maps to a consistent amount of world space. The raw API reports visible units, while fog still exists.

Design inference: model visibility as simulation data first, then render it. A renderer texture is an output of visibility, not the source of truth.

## Simpler Baseline Pattern

A simple RTS FOW pattern is:

```text
per player:
    fog_state[cell] = masked | fogged | visible

on fixed FOW tick:
    visible cells become fogged
    apply permanent/scripted fog modifiers
    for each live revealer:
        stamp or compute visible cells inside sight radius
    upload dirty region or full R8 texture to GPU

render:
    sample fog texture in terrain/entity/minimap shaders
    linearly filter or blur for soft edges
```

This has several advantages:

- Predictable cost: roughly `O(players * grid_cells_changed + revealers * sight_area)` before occlusion.
- Easy multiplayer: each player owns one grid, shared vision ORs/merges allied grids.
- Easy gameplay queries: cell lookups are cheap and deterministic.
- Easy renderer integration: one texture per local player, optionally double-buffered for smooth transitions.
- Easy save/load/replay: fog history is ordinary byte data.

The main downside is visual coarseness unless the renderer smooths it.

## Algorithm Options

### Option A: Circle Stamp, No Occlusion

For each revealer, stamp a precomputed disk mask into the player's current-visibility grid. Keep `explored` as a persistent bit. Use bilinear texture filtering and optional blur for visuals.

Cost: very low.

Quality: acceptable for an RTS with no tactical LOS blockers, or as an immediate baseline.

Good fit here: excellent first step. It would probably outperform the current path by a wide margin and give game code an authoritative visibility state.

### Option B: Grid Shadowcasting

Use recursive/symmetric shadowcasting on the FOW grid from each revealer. The map provides opaque cells from cliffs, terrain height transitions, trees, buildings, or explicit sight blockers.

Cost: usually near `O(visible_cells_per_revealer)`, because fully shadowed sectors are skipped.

Quality: strong tactical LOS, especially on grid-like terrain.

Good fit here: best second phase if ground-unit LOS blockers matter. Use fixed-size arrays and octant scans in Quake/id style, no heap allocations in the hot path.

### Option C: Height-Aware Grid LOS

Keep circle stamping, but gate cells by simple terrain rules:

- Ground units cannot see cells above a height threshold unless the ray crosses a ramp or the unit is also high.
- Air units ignore ground blockers.
- Scripted sight blockers are opaque only for ground vision.

Cost: medium if implemented with shadowcasting; high if implemented as Bresenham rays to every perimeter cell.

Quality: close to modern RTS expectations without full polygon visibility.

Good fit here: likely the right gameplay target after Option A.

### Option D: GPU Render Texture Stamping

Render reveal masks into a low-resolution R8/RG8 render target, optionally with instanced quads, then compose current/history textures on GPU.

Cost: low GPU cost if batched; little CPU cost.

Quality: visually smooth. Gameplay still needs CPU-side visibility or a mirrored grid.

Good fit here: good renderer layer, but not as the sole source of truth.

### Option E: Polygon/Shadow-Volume LOS

Use blocker geometry and compute visibility polygons or shadow volumes.

Cost: expensive and more complex to maintain.

Quality: visually rich but usually unnecessary for an RTS FOW grid.

Good fit here: not recommended for the core FOW system. Keep this as a future visual enhancement only.

## Recommended Architecture

Use a two-layer design:

1. **Game visibility grid**
   - Owned by game/server-side state, not renderer-only state.
   - One compact grid per player.
   - Store bits/bytes: `VISIBLE`, `EXPLORED`, maybe `DETECT`, `RADAR`, `SCRIPT_VISIBLE`.
   - Update on a fixed cadence, probably 5-10 Hz.
   - Merge allied/shared vision explicitly.

2. **Renderer fog texture**
   - Client receives or derives the local player's grid.
   - Upload as `GL_R8` or `GL_RG8`.
   - Use one channel for target/current visibility and one channel for history or transition if useful.
   - Smooth visually in shader; do not make the simulation depend on the smoothed texture.

Suggested first implementation path, when implementation begins:

1. Add a small FOW grid module on the game side.
2. Implement Option A disk stamping using per-radius cached masks.
3. Replace the current per-revealer blocker ray rendering with a simple texture upload path.
4. Add gameplay queries and entity visibility filtering.
5. Add Option B or C only after profiling and after deciding exact terrain/sight-blocker semantics.

## Specific Optimizations If Keeping Current Renderer Path

If the current renderer algorithm is retained temporarily:

- Spatially bucket blockers by FOW/grid tile, so blocker collection is `O(revealers * local_bucket_contents)` instead of `O(blockers * revealers)`.
- Build a blocker list per revealer or draw only blockers intersecting that revealer's sight radius.
- Replace `SIGHT_DISTANCE` with per-unit sight radius.
- Use `GL_R8` render targets instead of RGBA for masks/history/result.
- Batch revealer sight circles with instancing instead of one draw setup per revealer.
- Track dirty revealers and skip FOW updates when no revealer moved across a fog cell.
- Keep `FOW_RT_HISTORY` as a max/OR state, but mirror it in CPU state for gameplay.

These are still second-best compared with simplifying the architecture.

## Open Questions

- Should FOW be authoritative on the server, client-only, or server-authored but client-rendered?
- What exact gameplay states are required: visible, explored, masked, detector, radar, last-known buildings?
- Do trees/buildings block vision, or only terrain/sight-blocker doodads?
- How should high ground work for ground units, air units, and scanner-like abilities?
- What grid scale is acceptable: 128 world units, 256 world units, or tied to pathing cells?
- Is smooth visual interpolation required, or is Warcraft-style cell stepping acceptable initially?

## Sources

- Blizzard, [StarCraft II Game Guide: Map Elements](https://news.blizzard.com/en-us/article/4546768/game-guide-map-elements)
- Liquipedia, [Fog of War](https://liquipedia.net/starcraft2/Fog_of_War), [Sight](https://liquipedia.net/starcraft2/Sight), [Sight Blockers](https://liquipedia.net/starcraft2/Sight_Blockers)
- Vinyals et al., [StarCraft II: A New Challenge for Reinforcement Learning](https://www.davidsilver.uk/wp-content/uploads/2020/03/starcraft_compressed.pdf)
- RogueBasin, [FOV using recursive shadowcasting](https://www.roguebasin.com/index.php/FOV_using_recursive_shadowcasting)
- Albert Ford, [Symmetric Shadowcasting](https://www.albertford.com/shadowcasting/)
- Layered Fog of War docs, [Getting Started](https://gandoulf.github.io/enzo-resse/FOW/book/book/getting_started.html)
