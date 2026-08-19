# WMO and M2 Lighting Implementation Plan

Branch: `feature/wow-static-grass-height-atlas`

## Background

The last commit landed per-vertex baked MOCV colors, indoor group detection, and the
`uWmoIndoor` uniform that suppresses directional lighting for interior WMO batches.
That is the correct structure, but the shader formula and the raw MOCV data fed into
it are both wrong. Everything below builds on the existing batch infrastructure.

Reference implementations used for research:

- **WebWowViewerCpp** (deamon87) — most complete open-source WoW renderer with full
  GLSL shaders, MOLT doodad lighting, and MOCV fixup
- **Noggit3** (wowdev) — simpler, more readable; best source for the fixup algorithm
  and minimal shader

---

## Phase 1 — Fix MOCV shader formula and CPU fixup

**All work in this phase is in `r_wowmap_objects.c` and `r_wowmap_shader.c`.**

### 1.1 — Parse MOHD (root WMO header)

`Wow_LoadWmoModel` currently reads only `DHOM` (group count), `XTOM` (textures), and
`TMOM` (materials). Add:

```
chunk tag "DHOM" (MOHD, reversed):
  offset +0x00  DWORD  nTextures
  offset +0x04  DWORD  nGroups
  offset +0x08  DWORD  nPortals
  offset +0x0C  DWORD  nLights          ← nLights; also save for MOLT
  offset +0x10  DWORD  nDoodadNames
  offset +0x14  DWORD  nDoodadDefs
  offset +0x18  DWORD  nDoodadSets
  offset +0x1C  COLOR32 ambColor        ← {B, G, R, A} in file order
  offset +0x20  DWORD  wmoID
  ...
  offset +0x30  WORD   flags            ← bit 0x02 = flag_lighten_interiors
                                           bit 0x04 = flag_skip_base_color
```

Store `ambColor` and `flags` on `wowWmoModel_t` for use by the group fixup.

```c
// Add to wowWmoModel_t in r_wowmap.h
COLOR32 amb_color;   // MOHD.ambColor (BGRA → stored as RGB via Wow_Color)
DWORD   mohd_flags;  // bit 0x02 = lighten_interiors, 0x04 = skip_base_color
DWORD   n_lights;    // MOHD.nLights, used when parsing MOLT
```

### 1.2 — Extend group-header parsing to read batch counts

`wowWmoBatchDef_t` is the per-batch record (MOBA). The MOGP header also carries three
batch-count fields that split the batch list into three passes:

```
MOGP header (at chunk + 0x00, before subchunks):
  offset +0x00  DWORD  nameOffset
  offset +0x04  DWORD  descGroupNameOffset
  offset +0x08  DWORD  mogpFlags            ← already read for indoor flag
  offset +0x0C  float[6] boundingBox
  ...
  offset +0x30  WORD   transBatchCount      ← A: indices 0 .. A-1
  offset +0x32  WORD   intBatchCount        ← B: indices A .. A+B-1
  offset +0x34  WORD   extBatchCount        ← C: indices A+B .. end
  offset +0x36  WORD   pad
```

Store `trans_batch_count` per group so the CPU fixup knows the split point.

### 1.3 — Implement `Wow_FixMocvAlpha` (CPU fixup at VBO build time)

Run this **after** MOBA and MOCV are both parsed, before building the vertex buffer.

```c
static void Wow_FixMocvAlpha(BYTE *colors, DWORD color_count,
                              wowWmoBatchDef_t const *batches, DWORD batch_count,
                              DWORD trans_batch_count,
                              COLOR32 amb, DWORD mohd_flags,
                              BOOL exterior) {
    BOOL skip_base = (mohd_flags & 0x04) != 0;
    BOOL lighten   = (mohd_flags & 0x02) != 0;
    BYTE ambR = skip_base ? 0 : amb.r;
    BYTE ambG = skip_base ? 0 : amb.g;
    BYTE ambB = skip_base ? 0 : amb.b;

    /* split point: last vertex of the last batch-A batch */
    int begin_second = 0;
    if (trans_batch_count > 0 && batch_count > 0) {
        DWORD last_a = trans_batch_count - 1 < batch_count ? trans_batch_count - 1 : batch_count - 1;
        begin_second = (int)batches[last_a].last_vertex + 1;
    }

    if (lighten) {
        /* flag_lighten_interiors: only bake interior/exterior into alpha, leave rgb alone */
        for (DWORD i = (DWORD)begin_second; i < color_count; i++)
            colors[i * 4 + 3] = exterior ? 0xFF : 0x00;
        return;
    }

    /* Batch-A (transparent) vertices: pre-multiply rgb by (1 - alpha) */
    for (int i = 0; i < begin_second && i < (int)color_count; i++) {
        float a = colors[i * 4 + 3] / 255.f;
        int r = (int)colors[i * 4 + 2]; /* BGRA: +2 = R */
        int g = (int)colors[i * 4 + 1];
        int b = (int)colors[i * 4 + 0];
        colors[i * 4 + 2] = (BYTE)MAX(0, (int)((r - ambR) * (1.f - a) / 2.f));
        colors[i * 4 + 1] = (BYTE)MAX(0, (int)((g - ambG) * (1.f - a) / 2.f));
        colors[i * 4 + 0] = (BYTE)MAX(0, (int)((b - ambB) * (1.f - a) / 2.f));
        /* alpha left as authored for batch-A */
    }

    /* Batch-B/C vertices: additive ambient fixup + bake interior/exterior into alpha */
    for (DWORD i = (DWORD)begin_second; i < color_count; i++) {
        float a = colors[i * 4 + 3] / 255.f;
        int r = (int)colors[i * 4 + 2];
        int g = (int)colors[i * 4 + 1];
        int b = (int)colors[i * 4 + 0];
        colors[i * 4 + 2] = (BYTE)MIN(255, MAX(0, (int)((r * a / 64.f + r - ambR) / 2.f)));
        colors[i * 4 + 1] = (BYTE)MIN(255, MAX(0, (int)((g * a / 64.f + g - ambG) / 2.f)));
        colors[i * 4 + 0] = (BYTE)MIN(255, MAX(0, (int)((b * a / 64.f + b - ambB) / 2.f)));
        colors[i * 4 + 3] = exterior ? 0xFF : 0x00;
    }
}
```

The shader multiplies the result by 2 (see § 1.4), so the `/2` above and `*2` below
cancel — the convention matches WebWowViewerCpp.

### 1.4 — Fix the shader formula

Current (wrong — multiplicative):
```glsl
color.rgb *= 2.0 * v_color.rgb * (uSingleTexture != 0 && uWmoIndoor != 0 ? 1.0 : v_lighting);
```

New (additive ambient, matching actual WoW lighting model):
```glsl
if (uSingleTexture != 0) {
    /* WMO path: v_color.rgb is pre-fixed-up MOCV; add to base ambient then scale.
       v_color.w baked as 0=interior, 1=exterior by fixup; blend lighting accordingly. */
    float extBlend   = v_color.a;                        /* 0=interior, 1=exterior */
    float directTerm = v_lighting * extBlend;            /* exterior sun only */
    color.rgb *= 2.0 * (uWmoAmbient + v_color.rgb + directTerm);
} else {
    /* Terrain path: MCVT vertex color is already a full lighting multiplier */
    color.rgb *= 2.0 * v_color.rgb * v_lighting;
}
```

Add `uWmoAmbient` uniform (`vec3`, passed from the model's `MOHD.ambColor` normalised
to `[0, 1]`, set to `vec3(0)` for terrain). This lets the interior ambient tint from
the root WMO propagate without hard-coding a value.

Terrain path stays unchanged. The `uWmoIndoor` uniform becomes redundant once
`v_color.a` carries the blend — remove it or keep for a debug cvar.

**Important**: `VERTEX.color` is `COLOR32 {r, g, b, a}` and the shader reads
`i_color` as `vec4`. The alpha channel is currently discarded (`color.a = 1.0` at the
end). Change that to pass `v_color.a` through from `i_color` so the fixup-baked alpha
reaches the shader.

---

## Phase 2 — WMO-embedded doodads (SDOM / DDOM / NDOM)

**Biggest visual gap.** All furniture, torches, candles, and banners inside WMOs are
absent. Each WMO instance in `wowMapObjDef_t` carries a `doodad_set` index that
selects a named subset from the root WMO's doodad list.

### 2.1 — Parse root WMO doodad chunks

In `Wow_LoadWmoModel`, handle three additional reversed tags:

| Reversed tag | Normal | Content |
|---|---|---|
| `NDOM` | MODN | null-terminated doodad model filename block |
| `SDOM` | MODS | array of `SMODoodadSet` (name + start + count, 32 bytes each) |
| `DDOM` | MODD | array of `SMODoodadDef` (40 bytes each) |

```c
typedef struct {
    char  name[20];
    DWORD start;    /* first MODD index in this set */
    DWORD count;
    DWORD pad;
} wowWmoDoodadSet_t;   /* 32 bytes */

typedef struct {
    DWORD name_index;   /* byte offset into MODN blob */
    DWORD flags;
    wowVec3_t position;
    float     quat[4];
    float     scale;
    COLOR32   color;    /* color.a = MOLT index if flag_0x4 set */
} wowWmoDoodadDef_t;   /* 40 bytes */
```

Store on `wowWmoModel_t`:
```c
wowWmoDoodadSet_t *doodad_sets;
DWORD              num_doodad_sets;
wowWmoDoodadDef_t *doodad_defs;
DWORD              num_doodad_defs;
char              *doodad_name_blob;
DWORD              doodad_name_blob_size;
```

### 2.2 — Instantiate at WMO placement time

In `Wow_AddWmoInstance`, after setting up the instance matrix, iterate the selected
doodad set and call `Wow_AddDoodadInstance` (or a new `Wow_AddWmoDoodadInstance`)
for each `SMODoodadDef` in the set. Transform position/rotation/scale from WMO local
space to world space using the instance matrix.

The doodad quaternion is `(x, y, z, w)` in MODD; convert to a rotation matrix or
Euler angles matching the existing entity rotation contract.

### 2.3 — Track host WMO group for ambient lighting

When adding a MODD doodad, tag the resulting entity with the WMO's `MOHD.ambColor`
so Phase 3 can forward it as interior ambient.

---

## Phase 3 — M2 doodad ambient color from WMO (MOLT + MOGP)

Once Phase 2 places the doodads, they need correct ambient lighting.

### 3.1 — Parse MOLT (TLOM)

In `Wow_LoadWmoModel`, handle `TLOM` (reversed MOLT):

```c
typedef struct {
    BYTE     type;       /* 0=OMNI, 1=SPOT, 2=DIRECT, 3=AMBIENT */
    BYTE     use_atten;
    BYTE     pad[2];
    COLOR32  color;      /* BGRA */
    wowVec3_t position;  /* WMO local space */
    float    intensity;
    float    atten_start;
    float    atten_end;
    float    unk[4];
} wowWmoLight_t;   /* 48 bytes */
```

Store on `wowWmoModel_t`:
```c
wowWmoLight_t *lights;
DWORD          num_lights;
```

**Do not** add MOLT lights to WMO wall shading — they do not affect MOCV-shaded
geometry. They are solely for doodad and character lighting.

### 3.2 — Per-doodad MOLT lookup

When `SMODoodadDef.color.a < num_lights` and `flags & 0x4` is set, the doodad's
sun direction is overridden:

```c
// pseudo-code at instantiation time
if ((def->flags & 0x4) && def->color.a < model->num_lights) {
    wowWmoLight_t *lt = &model->lights[def->color.a];
    VECTOR3 light_world = TransformPoint(&instance->matrix, lt->position);
    VECTOR3 sun_dir = Normalize(Sub(doodad_world_center, light_world));
    entity->sun_dir_override = sun_dir;
    entity->interior_direct_color = ScaleColor(lt->color, lt->intensity);
}
```

### 3.3 — MOGP group ambient colour override

MOGP header offset `+0x38` (classic): 4-byte `replacement_for_header_color`. When
non-zero, it overrides `MOHD.ambColor` for doodads in that group. Parse alongside the
existing `mogpFlags` read and propagate to MODD doodads in that group.

---

## Phase 4 — Batch A/B/C render ordering and WMO transparency

### 4.1 — Separate opaque and transparent batches at load time

During `Wow_LoadWmoGroup`, split material builds into three pools:
- **B + C (opaque)**: `transBatchCount ≤ batch_index < batch_count` — draw first
- **A (transparent)**: `batch_index < transBatchCount` — collect for sorted pass

The existing `indoor` flag split already separates slots by indoor/outdoor. Extend the
slot key to also encode `batch_class ∈ {opaque, transparent}`.

### 4.2 — Per-material blend mode from MOMT

MOMT material record (64 bytes each), byte `+0x02`: `uint16_t blendMode`.

| Value | Name | GL state |
|---|---|---|
| 0 | Opaque | no blend |
| 1 | AlphaKey | discard if alpha < 0.5 |
| 2 | Alpha | SRC_ALPHA / ONE_MINUS_SRC_ALPHA |
| 3 | NoAlphaAdd | ONE / ONE |
| 4 | Add | SRC_ALPHA / ONE |

Parse blend mode when building `materials[]` and store on `wowWmoBatch_t`:
```c
BYTE blend_mode;   /* MOMT blendMode, 0–4 */
```

### 4.3 — Two-pass WMO draw

1. **Opaque pass** (B+C batches, blendMode 0–1): render with existing state.
2. **Transparent pass** (A batches, blendMode 2–4): enable GL_BLEND, sort batches
   back-to-front by camera distance, draw. Sort can be a simple insertion sort on the
   small per-frame visible set.

---

## Phase 5 — Global WMO maps (dungeons and instances)

`Wow_LoadWdtTiles` reads `MPHD` and `MAIN` only. When `MPHD.flags & 0x01`, the WDT
is a global-WMO map: no ADT tiles exist, and the WMO + its placement are at the WDT
level.

### 5.1 — Read WDT-level MWMO / MWID / MODF

In `Wow_LoadWdtTiles`, after checking the global-WMO flag:
- Parse `OMWM` (MWMO) — WMO filename block
- Parse `DIWM` (MWID) — filename offsets
- Parse `FDOM` (MODF) — single placement definition

Call `Wow_AddWmoInstance` with the resolved path and placement.

### 5.2 — Suppress terrain draw for global-WMO maps

When the global-WMO flag is set in `wow_world.wdt_flags`, skip the terrain chunk
iteration in `Wow_DrawTerrainAndWmos`. The WMO and its doodads provide the full scene.

---

## Phase 6 — Portal visibility (MOPT / MOPR)

See `docs/games/world-of-warcraft/terrain-and-world-rendering.md` §"WMO visibility
direction" for the full design. High-level steps:

1. Parse `MOPT` (portal plane/vertex data) and `MOPR` (portal references per group)
   from root WMO.
2. Store on `wowWmoModel_t`. Add containment query: given a world point, return the
   interior group index (or -1 for exterior).
3. When camera is confidently inside an interior group, traverse only portals that
   intersect the clipped frustum. Fall back to per-group frustum culling otherwise.
4. Exterior groups remain visible whenever containment is ambiguous.

---

## Current state gap table

| Feature | Status | Phase |
|---|---|---|
| MOCV vertex colors parsed | ✅ done | — |
| Indoor flag from `MOGP.flags & 0x2000` | ✅ done | — |
| Separate indoor/outdoor batch slots | ✅ done | — |
| `uWmoIndoor` uniform toggled per batch | ✅ done | — |
| RNOM normals in vertices | ✅ done | — |
| MOCV byte order (BGRA) | ✅ correct (`Wow_Color` swaps B↔R internally) | — |
| **MOCV shader formula (additive, not multiplicative)** | ✅ done | 1.4 |
| **MOCV CPU fixup (ambColor subtract, batch A/B/C alpha bake)** | ✅ done | 1.3 |
| **`MOHD.ambColor` and flags parsed** | ✅ done | 1.1 |
| **`transBatchCount` / `intBatchCount` from MOGP header** | ✅ done | 1.2 |
| **WMO-embedded doodads (SDOM / DDOM / NDOM)** | ✅ done | 2 |
| **MOLT (TLOM) parsed → doodad sun direction** | ✅ done (parsed; MOLT-flagged doodads skipped pending entity extension) | 3.1–3.2 |
| **MOGP group ambient override** | ✅ done (parsed; spatial propagation deferred) | 3.3 |
| **Batch A/B/C render ordering** | ✅ done (two-pass: opaque then alpha-blended) | 4 |
| **MOMT blend mode per material** | ✅ done | 4.2 |
| **Global WMO maps (dungeons)** | ✅ done (WDT OMWM/DIWM/FDOM parsed; terrain suppressed) | 5 |
| **Portal visibility (MOPT / MOPR)** | ✅ done (TPOM/VPOM/RPOM parsed; exterior cull when cam inside) | 6 |
| Second MOCV (flag `0x1000000`) for tex-blend alpha | ❌ missing | future |
