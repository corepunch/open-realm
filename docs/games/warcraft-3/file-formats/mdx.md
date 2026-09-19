# Warcraft III MDX Model Format

MDX (also written as MDLX) is Warcraft III's binary 3D model format. It stores geometry, materials, bones, animations, particle emitters, and more in a chunked binary layout.

## File Layout

An MDX file is a sequence of tagged chunks. Each chunk begins with a 4-byte FourCC identifier followed by a 4-byte chunk size in bytes. The top-level structure is:

```
MDLX                       ← magic / version header
VERS  <size>  <version>    ← format version (800 for WC3, 1000 / 1500 for Reforged)
MODL  <size>  <modelInfo>  ← global model info (name, bounds)
SEQS  <size>  [sequence]…  ← named animation sequences
GLBS  <size>  [globalSeq]… ← global sequence durations
TEXS  <size>  [texture]…   ← texture path list
MTLS  <size>  [material]…  ← materials (layer stacks)
GEOS  <size>  [geoset]…    ← geometry (vertices + faces)
BONE  <size>  [bone]…      ← skeleton bones
HELP  <size>  [helper]…    ← helper nodes (invisible pivot points)
ATCH  <size>  [attach]…    ← attachment points (e.g. "Overhead")
PIVT  <size>  [pivot]…     ← per-node pivot positions
PREM  <size>  [emitter]…   ← particle emitter v1
PRE2  <size>  [emitter2]…  ← particle emitter v2
RIBB  <size>  [ribbon]…    ← ribbon emitters
EVTS  <size>  [event]…     ← event objects (sounds, etc.)
CLID  <size>  [collision]… ← collision shapes
LITE  <size>  [light]…     ← light nodes
```

Not all chunks are present in every model. The Warcraft III loader (`games/warcraft-3/renderer/mdx/r_mdx_load.c`) dispatches on each FourCC tag.

## Node Hierarchy

All nodes (bones, helpers, attachments, emitters, event objects, collision shapes, lights) share a common **node header**:

| Field | Type | Description |
|-------|------|-------------|
| `size` | `DWORD` | Total size of this node record in bytes |
| `name` | `char[80]` | Human-readable name |
| `objectId` | `DWORD` | Unique node index (0-based) |
| `parentId` | `DWORD` | Parent node index (`0xFFFFFFFF` = root) |
| `flags` | `DWORD` | Node type and billboarding flags |

The `flags` field uses the following bitmasks:

| Flag | Value | Meaning |
|------|-------|---------|
| `Helper` | `0` | Plain helper (pivot only) |
| `DontInheritTranslation` | `1` | Ignore parent translation |
| `DontInheritRotation` | `2` | Ignore parent rotation |
| `DontInheritScaling` | `4` | Ignore parent scale |
| `Billboarded` | `8` | Always faces the camera |
| `BillboardedLockX` | `16` | Billboarded on X axis only |
| `BillboardedLockY` | `32` | Billboarded on Y axis only |
| `BillboardedLockZ` | `64` | Billboarded on Z axis only |
| `CameraAnchored` | `128` | Positioned relative to the camera |
| `Bone` | `256` | This node is a skeleton bone |
| `Light` | `512` | This node is a light source |
| `EventObject` | `1024` | Triggers events (sounds, effects) |
| `Attachment` | `2048` | Named attachment point |
| `ParticleEmitter` | `4096` | Particle emitter |
| `CollisionShape` | `8192` | Physics collision volume |
| `RibbonEmitter` | `16384` | Ribbon/trail emitter |

## Keyframe Tracks (Animated Values)

Animation data is stored as **keyframe tracks**. Each track is identified by a 4-byte tag that encodes the node type and the animated property. For example:

| Tag | Target | Property |
|-----|--------|----------|
| `KGTR` | Node | Translation |
| `KGRT` | Node | Rotation (quaternion) |
| `KGSC` | Node | Scale |
| `KMTA` | Material layer | Alpha |
| `KMTE` | Material layer | Emissive gain |
| `KP2V` | Particle emitter v2 | Visibility |
| `KP2E` | Particle emitter v2 | Emission rate |
| `KLAV` | Light | Ambient intensity |

A keyframe track record starts with:

```
DWORD  numKeys
DWORD  interpolationType   // 0=none, 1=linear, 2=hermite, 3=bezier
DWORD  globalSeqId         // 0xFFFFFFFF if not driven by a global seq
[key × numKeys]
```

Each key is `{ DWORD frame; <value>; [<inTan>; <outTan>] }` where the tangent pair is only present for hermite/bezier interpolation.

## Geoset (Geometry)

A geoset is one draw call: a set of vertices sharing the same material. Its sub-chunks are:

| Sub-chunk | Description |
|-----------|-------------|
| `VRTX` | Vertex positions — `float[3]` each |
| `NRMS` | Vertex normals — `float[3]` each |
| `UVBS` | UV sets — `float[2]` each |
| `PTYP` | Primitive types (always `4` = triangles) |
| `PCNT` | Primitive counts |
| `PVTX` | Vertex index list (triangle list) |
| `GNDX` | Bone group index per vertex |
| `MTGC` | Vertex count per bone group |
| `MATS` | Bone indices for each bone group |

The renderer builds a static VBO from `VRTX`/`NRMS`/`UVBS` and a matrix palette from the `MATS`/`GNDX`/`MTGC` tables for GPU skinning.

## Geoset Flags

The `flags` field of a geoset (`mdxGeoFlags_t`) controls render state:

| Flag | Value | Meaning |
|------|-------|---------|
| `Unshaded` | `0x01` | No lighting — use vertex colour directly |
| `TwoSided` | `0x10` | Disable back-face culling |
| `Unfogged` | `0x20` | Not affected by distance fog |
| `NoDepthTest` | `0x40` | Always drawn on top |
| `NoDepthSet` | `0x80` | Do not write to the depth buffer |

## Material Layers

Each material is a stack of one or more layers rendered in order (additive blending for particle effects, etc.). A layer record includes:

- filter mode (none, transparent, blend, additive, add-alpha, modulate, modulate2×)
- texture index (into the `TEXS` table)
- texture animation index
- flag bits (unshaded, sphere env map, two-sided, unfogged, no-depth-test, no-depth-set)
- animated alpha (`KMTA` track)

## Particle Emitter 2 (`PRE2`) Filter Modes

Each `PRE2` emitter carries its own `FilterMode`. It is presentation data and must be copied to every spawned particle; defaulting every emitter to additive blending changes smoke and other translucent effects dramatically.

| MDX value | Authored mode | Shared particle mode | Blend behavior |
|---|---|---|---|
| `0` | Blend | `BLEND_MODE_BLEND` | source alpha over destination |
| `1` | Additive | `BLEND_MODE_ADD` | source alpha + destination |
| `2` | Modulate | `BLEND_MODE_MODULATE` | multiply source/destination |
| `3` | Modulate2x | `BLEND_MODE_MODULATE_2X` | doubled modulation |
| `4` | AlphaKey | `BLEND_MODE_ALPHAKEY` | alpha test / coverage |

The shared particle renderer predates the MDX path and its `BLEND_MODE_ADD`/`BLEND_MODE_ADDALPHA` names have legacy semantics. `MDLX_ParticleBlendMode()` is therefore the explicit format-to-renderer conversion point rather than relying on enum ordinals. Invalid PRE2 filter values are diagnosed by the loader and normalized to Blend.

This matters directly for building-damage effects: their fire model can contain both emissive/additive flame particles and ordinary blended smoke. Treating every PRE2 emitter as additive causes grey smoke textures to brighten the framebuffer and appear pale or white.

## Ribbon Emitters (`RIBB`)

`RIBB` is not a geoset and not PRE2 tails. GhostWolf: *emitters that emit lines, which are all connected together to form a ribbon of quads* ([Hive MDX spec](https://www.hiveworkshop.com/threads/mdx-specifications.240487/)). Magos: the strip has length 0 until the node moves.

Each emitter is a node plus:

| Field | Meaning |
|---|---|
| `heightAbove` / `heightBelow` | Edge half-width along the node's local Y (`KRHA` / `KRHB`) |
| `alpha`, `color` | Vertex tint (`KRAL`, `KRCO`) |
| `lifespan` | Edge lifetime in seconds |
| `emissionRate` | Edges per second |
| `rows` / `columns` / `textureSlot` | Material UV cell (`KRTX`) |
| `materialId` | `MTLS` index (filter mode, unshaded, texture) |
| `gravity` | World −Z pull on live edges |
| `KRVS` | Visibility; default **1** when the current sequence has no keys |

ZigguratMissile (`uzg1`) authors three `BlizRibbon*` emitters parented to waving helpers, material 0 = `Textures\Ghost2.blp` Add Alpha. That is the decorated in-flight trail. PRE2 `BlizParticle02` is a Death squirt, not the trail.

Loader: `MDLX_ReadRIBB` in `r_mdx_load.c`. Trails are per `renderEntity_t.number` so concurrent missiles do not share one strip. `MDLX_GetModelKeytrackValue` already ignores keys outside the current sequence, so a lone `KRVS` key at Death frame 0 hides the ribbon in Death and leaves Stand/Birth at the default 1.

Trail lifecycle is engine-owned (`renderer/r_trail.h`, `renderer/r_trail.c` — the Q3 split: games own emitter state and materials, the engine owns edge history and strip geometry). The WC3 adapter (`r_mdx_ribbons.c`) only samples keytracks, computes the world edge, advances its per-model `trail_t` store, and converts the returned `trailVert_t` strip to MDX `VERTEX` (identity bone bind, face normal). Engine semantics: each trail carries its own `stamp` of the last `now_ms` it advanced. A second draw in the same frame (shadow/lights pass) gets `dt = 0` and only re-emits the strip; the guard is per-trail, not per-instance, so multi-emitter models advance each trail exactly once. A gap past `TRAIL_STALE_MS` (250 ms) with a live stamp clears the trail, which drops stale edges when an edict number is reused by a new missile or a culled entity reappears (a respawn inside the same tick still slips through). The emission accumulator clamps at 2 before emitting, so a hitch emits at most 2 edges instead of stacking `rate*dt` coincident ones. U is `age / lifespan` (oldest edge at the cell end), so adding or expiring an edge never rescales the strip's UVs; two fresh edges cover only a narrow slice, not the whole texture. Edge color/alpha is stored per edge, so animated `KRAL`/`KRCO` stay historic instead of repainting the whole strip.

Draw uses the MDX material layer (not the particle billboard shader) so Ghost2 unwraps along the strip. Tests: `renderer_model.mdx_ribbon_*` and `renderer_model.mdx_ribb_loader_*`. `mdxtool --dump-all` prints `RIBB` rows.

## Animation Sequences

Each entry in `SEQS` describes one named clip:

| Field | Type | Description |
|-------|------|-------------|
| `name` | `char[80]` | Sequence name (e.g. `"Stand"`, `"Walk"`, `"Attack"`) |
| `interval` | `DWORD[2]` | [startFrame, endFrame] in milliseconds |
| `moveSpeed` | `float` | Ground speed during the animation |
| `flags` | `DWORD` | `1` = non-looping |
| `rarity` | `float` | Random weight for stand variations |
| `syncPoint` | `DWORD` | Sync reference frame |
| `extent` | `Extent` | Bounding box + sphere for this clip |

Standard sequence names are: `Stand`, `Walk`, `Attack`, `Attack Slam`, `Attack 2`, `Decay Flesh`, `Decay Bone`, `Death`, `Dissipate`, `Portrait`, `Spell`, `Stand Channel`, `Stand Ready`, `Stand Work`.

## Related Source Files

| Source | Purpose |
|--------|---------|
| `games/warcraft-3/renderer/mdx/r_mdx.h` | All MDX struct definitions |
| `games/warcraft-3/renderer/mdx/r_mdx_load.c` | Chunk parser and model loader |
| `games/warcraft-3/renderer/mdx/r_mdx_render.c` | Per-frame skinning and draw calls |
| `games/warcraft-3/renderer/mdx/r_mdx_interpolation.c` | Keyframe track evaluation |
| `games/warcraft-3/renderer/mdx/r_mdx_particles.c` | PRE2 particle emitters |
| `games/warcraft-3/renderer/mdx/r_mdx_ribbons.c` | RIBB trail update and strip build |
| `renderer/r_trail.h`, `renderer/r_trail.c` | Engine trail history and strip synthesis (game-agnostic) |
| `games/warcraft-3/renderer/mdx/r_mdx_geoset.c` | Geoset draw and ribbon material pass |
