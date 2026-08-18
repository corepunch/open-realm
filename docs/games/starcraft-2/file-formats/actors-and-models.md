# Actors And Models

How SC2 connects placed units to rendered models via the catalog system.

Sources:
- https://sc2mapster.wiki.gg/wiki/Data/Actors
- https://sc2mapster.wiki.gg/wiki/Data/Actors/Unit
- https://sc2mapster.wiki.gg/wiki/Data/Models/Generic
- https://sc2mapster.wiki.gg/wiki/Model_Files

---

## The Actor System

Actors are the visual/audio representation layer for all game entities. They are abstract
component-like objects — not class-based — that handle:

- Attaching models to each other and to map positions
- Regulating movement, rotation, and flight arcs
- Controlling death animations and models
- Managing sounds: click, attack, birth, death, ambient
- Hosting particle systems, ribbons, lights, physics
- Communicating via message events (Actor Events — embedded trigger-like systems)

Every unit must have exactly one associated `CActorUnit`. More than one causes an editor error.

### Actor Types

70+ subtypes exist. Relevant ones for rendering:

| Type | Purpose |
| --- | --- |
| `CActorUnit` | Main visual actor for units; drives model, sounds, portrait, minimap icon |
| `CActorModel` | Standalone model (effects, doodads, projectiles) |
| `CActorMissile` | Projectile model with arc/travel behavior |
| `CActorDoodad` | Doodad model |
| `CActorSound` | Audio-only actor |
| `CActorLight` | Omni or spot light |
| `CActorBeam` | Beam/laser effect |
| `CActorSplat` | Decal on terrain |
| `CActorPortrait` | Portrait model for UI |

### Site Operations (SOps)

Define coordinate-system relationships between attached actors. Used to control turret rotation,
attachment points, height offsets, physics impact positions, and animation-driven positioning.
About 50 SOp types exist; `SOpAttachment` is the most common for placing effects at model attach points.

### Actor Events

Embedded trigger-like systems that respond to game messages (`Unit Birth`, `Effect Used`, `Create`,
`Destroy`, etc.) and send responses (play animation, create child actor, send message to another actor).
These are fully data-driven and do not require Galaxy script.

---

## Unit Actor (`CActorUnit`) — Key Fields For Rendering

Full field list: 145 fields. The subset relevant to a renderer:

### Model Fields

| Field | Type | Notes |
| --- | --- | --- |
| `Model` | Model ref | Primary `.m3` model for this unit |
| `Model (Build)` | Model ref | Model shown during construction animation |
| `Model (Editor)` | Model ref | Placeholder in terrain editor only |
| `Model (Placement)` | Model ref | Semi-transparent placement preview |
| `Model (Portrait)` | Model ref | Portrait UI model |
| `Death Actor Model` | Actor ref | Actor controlling the death model |
| `Death Actor Model Low` | Actor ref | Death model for low graphics settings |
| `Placeholder Actor Model` | Actor ref | Actor for placeholder display |

### Animation Fields

| Field | Notes |
| --- | --- |
| `Walk Animation Movement Speed` | Walk cycle speed in terrain tiles/second |
| `Animation Blend Time` | Global animation blend time; -1 uses engine default |
| `Animation Turn Duration` | Time for stationary turn animation |
| `Baselines` | Default standing/walking animation states |
| `Glossary Animation` | Animation shown in the unit glossary UI |

### Visual Identity

| Field | Notes |
| --- | --- |
| `Minimap Icon` | Texture shown on minimap |
| `Unit Icon` | Texture for control group |
| `Group Icon` | Texture for multi-unit group display |
| `Hero Icon` | Hero portrait (lower-right HUD) |
| `Wireframe - Image` | Health-bar wireframe overlay texture |
| `Wireframe Shield - Image` | Shield-bar wireframe texture |
| `Life Armor Icon` | Life armor icon texture |
| `Shield Armor Icon` | Shield armor icon texture |

### Status Bar Flags

`Status Bar Flags` controls which bars appear above the unit: Cargo, Duration, Energy, Life, Progress, Shields.

### Scale

| Field | Notes |
| --- | --- |
| `Scale` | Model scale (x, y, z) |
| `Auto Scale Factor` | Uniform scale multiplier |
| `Random Scale Range` | Random per-instance scale variation |

### Sounds

Separate sound slots for: Attack, Birth, Board, Click, Click Error, Construction, Highlight,
Movement, Pissed, Ready, Turning, What, Yes. Each has an individual `Sound` and a `Group Sound`
for when multiple units perform the action simultaneously.

---

## Model Catalog (`CModel` / `Data/Models/Generic`) — Key Fields For Rendering

Full field list: 127 fields. Relevant subset:

### Art

| Field | Notes |
| --- | --- |
| `Model` | Path to the `.m3` file |
| `Low Quality Model` | Alternate `.m3` for low graphics settings |
| `Scale Minimum` / `Scale Maximum` | Random scale bounds at instance creation |
| `Visual Radius` | Overrides bounding sphere for fog-of-war visibility |
| `Radius` | Model radius hint for attachment point queries |

### Animations

| Field | Notes |
| --- | --- |
| `Animation (Optional)` | Extra `.m3a` animation files; missing ones silently skipped |
| `Animation (Required)` | `.m3a` files that must be present or the model fails to load |
| `Required Animation Ex` | Additional required `.m3a` with flags |
| `Animation Speed` | Global speed multiplier for all animations |
| `Walk Animation Move Speed` | Walk cycle calibration (terrain tiles/second) |
| `Facial Controller` | `.m3a` or facial data file for lip-sync on portrait models |
| `Anim Aliases` | Maps animation names to groups (e.g. `"Stand"` → `"Idle"`) |

### Animation Events

`Events` on the model fire during specific animation frames:

| Sub-field | Notes |
| --- | --- |
| `Events - Animation` | Which animation triggers the event |
| `Events - Time` | Delay from animation start (seconds or frames) |
| `Events - Type` | Actor, Custom, Footprint Left/Right, Sound |
| `Events - Payload` | Action performed (spawn actor, play sound, etc.) |
| `Events - Attachment` | Attachment point for spawned objects |

### Variations

| Field | Notes |
| --- | --- |
| `Variation Count` | Total number of model variations |
| `Variations - Number` | Index assigned to a specific variation |
| `Variations - Probability` | Spawn probability for that variation |

### Tipability (terrain conformance)

| Field | Notes |
| --- | --- |
| `Tipability` | How far the model tilts on uneven terrain |
| `Tipability Blend Rate` | Rotation rate while conforming |
| `Tipability Pitch Maximum` | Max forward/backward tilt |
| `Tipability Roll Maximum` | Max side-to-side tilt |
| `Tipability Width` / `Length` | Area sampled for tipping calculation |

### Texture Declarations

Used for runtime texture swapping (team color, armor skins). Each declaration binds a
`Slot` name to a texture substring, letting Actor events swap textures by slot rather than path.

| Field | Notes |
| --- | --- |
| `Texture Declarations - Slot` | Slot name for texture swap reference |
| `Texture Declarations - Adaptions - Trigger On Substring` | Final word of texture filenames matching this slot |

### Selection And Display

| Field | Notes |
| --- | --- |
| `Selection Radius` | Multiplier for the selection circle |
| `Shadow Radius` | Blob shadow radius on low graphics |
| `Selection Layer` | Priority when drag-selecting overlapping units |
| `Occlusion` | Whether model fades when another model is in front: Hide / Show |

---

## `.m3` Model File Format

See [m3.md](m3.md) for the complete binary format spec.

Key points:
- Reference-table based (not chunked like MDX)
- `MD34` or `43DM` magic (little-endian)
- Every complex field is a `Reference { offset, nEntries, flags }` into the reference table
- Geometry: `m3Vertex_t` with position, packed normal, UV, 4×bone weight/index
- Skeleton: `m3Bone_t` array with parent index, rest-pose matrix, animated `AnimRef`s
- Sequences: `m3Sequence_t` with start/end frames and per-sequence animation data blocks
- Materials: `m3MaterialStandard_t` → `m3Layer_t` → texture path References

## `.m3a` Animation File

Supplemental bone animation data attached to a specific model. Adds animation sequences or
overrides bones for existing sequences without replacing the base `.m3`. Loaded after the
primary model; referenced via `Animation (Optional)` or `Animation (Required)` in `CModel`.

---

## Unit-To-Model Resolution Pipeline

```
Objects <Unit unitType="Marine">
    └─ CUnit id="Marine" in UnitData.xml
           ├─ actor reference → CActorUnit id="Marine"
           │       └─ Model → CModel id="Marine"
           │                      └─ Art: Model = "Assets\Units\Terran\Marine\Marine.m3"
           │                         Animation (Required) = ["Marine_Attack.m3a", ...]
           │                         Scale Minimum / Maximum
           └─ (fallback) guess "Assets\Units\Terran\<ID>\<ID>.m3" ONLY as last resort
```

Resolution order for catalog XML:
1. Base game XML from `base.SC2Assets` / `patch.SC2Assets`
2. Dependency mods in dependency order
3. Map-local `Base.SC2Data/GameData/*.xml`
4. Ordinary attributes: replace. Array attributes: append.

### Minimum Fields To Extract Per Catalog Type

**CUnit:** id, life/radius/mover/footprint, explicit actor reference (if present), resource flags.

**CActorUnit:** unit name/token, model reference, portrait/icon/minimap fields.

**CModel:** `.m3` file path, low-quality path, variation count, selection/shadow radius,
scale min/max, optional and required `.m3a` files, texture declaration slots.

---

## Implementation Notes For OpenWarcraft3

- The engine already loads `.m3` geometry in `games/starcraft-2/renderer/m3/r_m3_load.c`.
- The next step is catalog-driven resolution: parse `UnitData.xml` → `ActorData.xml` → `ModelData.xml` instead of guessing paths from unit IDs.
- Model variations should not be collapsed at load time; keep them as separate handles.
- `.m3a` files add animation sequences and should be layered onto the base model after loading.
- Texture declaration slots enable team-color swapping — needed once multiple player colors are rendered.
- `Walk Animation Move Speed` calibrates walk animation against world movement speed; use it to drive the animation time parameter during unit movement.
