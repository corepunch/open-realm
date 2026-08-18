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
