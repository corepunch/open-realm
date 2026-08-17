# WoW DBC Reference

This is the single reference for how `openwow` reads classic-era client databases (`.dbc` / `WDBC` files) and what each
character/creature table contains. It documents the binary container, the packed appearance/equipment values, and the
per-table field layouts as actually consumed by the code. It is not a list of every field Blizzard authored — only the
fields the current target reads, plus the layout hints needed to extend that set safely.

The same information is split across several places; this file is the index of record:

- Character pipeline and diagnostic workflow: [`docs/wow-character.md`](../../../docs/wow-character.md)
- M2 loader, geoset/component resolution: [`docs/m2-and-character-display.md`](m2-and-character-display.md)
- Loading order and tool commands: [`docs/data-loading.md`](data-loading.md)
- High-level format map and reference links: [`docs/file-formats.md`](file-formats.md)

## Contents

- [Why DBCs Instead Of `legs=2 arms=3`](#why-dbcs-instead-of-legs2-arms3)
- [WDBC Binary Format](#wdbc-binary-format)
- [Reader Ownership](#reader-ownership)
- [Packed Values](#packed-values)
- [Lookup Chains](#lookup-chains)
- [Character And Creature Tables](#character-and-creature-tables)
- [UI And Metadata Tables](#ui-and-metadata-tables)
- [Diagnostic Workflow](#diagnostic-workflow)
- [Known Pitfalls](#known-pitfalls)

## Why DBCs Instead Of `legs=2 arms=3`

WoW never stores a body-part breakdown per entity. A creature or character is described by a small set of integer
indices that resolve, through foreign keys, into pre-baked display records. Two idioms cover all of it:

1. **Foreign-key display records** (NPCs/creatures). A creature row carries a `display_id` into
   `CreatureDisplayInfo.dbc`, which links a model record and an optional `CreatureDisplayInfoExtra.dbc` record holding
   the character-like appearance fields. No per-limb values appear in the entity.
2. **Packed index bitfields** (players). Customization indices (skin, face, hair style, hair color, facial hair) plus
   class are packed into one 32-bit `appearance` value; four per-slot item indices are packed into one 32-bit
   `equipment` value. Each packed index is a variation/color index into a DBC table, not a literal geometry value.

`creatures.csv` (`games/world-of-warcraft/serverdata/creatures.csv`) is the clearest example of idiom 1: its
`model_idx`/`display_id` columns are the only appearance fields. The actual skin/face/hair/item breakdown lives in the
DBC chain behind that ID.

## WDBC Binary Format

Classic through Wrath client databases share one container layout. The header is five 32-bit little-endian words:

| Offset | Size | Field | Meaning |
|-------:|-----:|-------|---------|
| 0 | 4 | magic | ASCII `WDBC` |
| 4 | 4 | record count | number of records |
| 8 | 4 | field count | logical number of 32-bit fields per record |
| 12 | 4 | record size | physical bytes per record |
| 16 | 4 | string block size | total bytes of the trailing string block |

Records follow the header at offset 20; the string block follows the last record. Every field is a 32-bit word read
little-endian. Integer fields are stored directly; string fields store a byte offset into the string block (0 = null).
Field 0 is the record ID by convention and is the usual primary key.

Critical rule — `field_count * 4` can exceed `record_size`. Classic `CharStartOutfit.dbc` reports 41 logical fields
with 152-byte records. Validate the file envelope, then bounds-check each accessed field against `record_size`;
never reject a whole file for that mismatch alone.

## Reader Ownership

Two independent readers exist; neither parses DB2:

- **Renderer** — `renderer/m2/r_dbc.c` keeps each table as one resident `FS_ReadFile` image plus an FNV-1a integer
  hash index (`M2_DbcLoad` / `M2_DbcFindID` / `M2_DbcField` / `M2_DbcString`). Character/creature/outfit tables live
  here.
- **Game** — `game/g_wow.c` exposes `Wow_FindDbcRecord` and `Wow_DbcString` for map metadata, loading screens, and the
  creature model cache in `game/m_creature.c`.

The UI (`ui/ui_dbc.c`) reads its own small set (`ChrRaces`, `ChrClasses`, `CharBaseInfo`, `FactionTemplate`,
`FactionGroup`) for the character-create screen.

## Packed Values

### Appearance (32-bit)

Defined by `Wow_PackAppearance` / `Wow_UnpackAppearance` in `common/shared.h`. One `DWORD` in the snapshot.

| Bits | Width | Field | Source table |
|-----:|------:|-------|--------------|
| 0–4 | 5 | skin color | `CharSections.dbc` color index |
| 5–8 | 4 | face | face variation (classic max 14) |
| 10–14 | 5 | hair style | `CharHairGeosets.dbc` / `CharSections.dbc` |
| 15–18 | 4 | hair color | `CharSections.dbc` color index |
| 19–22 | 4 | facial hair (low 4 bits) | facial-hair style |
| 9 | 1 | facial hair bit 4 | shared spare face bit (classic facial IDs reach 16) |
| 23–26 | 4 | class | `ChrClasses.dbc` id; participates in starter-outfit key |
| 27–31 | 5 | flags | reserved |

Classic face IDs stop at 14, so the spare fifth face bit (bit 9) carries facial-feature bit 4. A zero-packed value is
`8388608` for Human Warrior (class 1, all customization indices 0) — index zero is a real first variation, not "bare".

### Equipment (32-bit)

Defined by `Wow_PackEquipment` / `Wow_UnpackEquipment`. Each byte is a **local slot item index** (0 = empty), not a raw
item ID; nonzero indices resolve through WoW-owned equipment lists to `ItemDisplayInfo.dbc` display IDs.

| Bits | Field |
|-----:|-------|
| 0–7 | upper body item |
| 8–15 | lower body item |
| 16–23 | hand item |
| 24–31 | foot item |

## Lookup Chains

### Creature / NPC

```text
creatures.csv display_id
  -> CreatureDisplayInfo.dbc (id = display_id)
       .model_id -> CreatureModelData.dbc  -> M2 model path, scale, collision width
       .extended_display_info_id -> CreatureDisplayInfoExtra.dbc
            skin/face/hair_style/hair_color/facial_hair  -> Wow_PackAppearance(...)
            NPCItemDisplay[0..10]  -> ItemDisplayInfo.dbc -> component textures / geosets
```

Creature NPCs keep their `CreatureDisplayID` in `entityState_t.class_id`; `r_m2.c` follows it through
`M2_DbcResolveCreatureAppearance`. Non-character creatures have no `CreatureDisplayInfoExtra` record and render via the
plain M2 path.

### Player character

```text
race | (class << 8) | (gender << 16) key  (race/gender derived from the model path)
  -> CharStartOutfit.dbc  -> starter display IDs + InventoryTypes
  -> ItemDisplayInfo.dbc  -> component texture stems + geoset groups
customization indices from packed appearance
  -> CharSections.dbc (skin/face/hair textures) + CharHairGeosets.dbc (hair geosets)
```

## Character And Creature Tables

Field numbers are zero-based. String fields are marked `(str)` and store string-block offsets.

### `CreatureDisplayInfo.dbc`

Consumed by `game/m_creature.c` and `renderer/m2/r_dbc.c`.

| Field | Content |
|------:|---------|
| 0 | id (== `display_id`) |
| 1 | model id → `CreatureModelData.dbc` |
| 2 | sound id |
| 3 | extended display info id → `CreatureDisplayInfoExtra.dbc` (0 = none) |
| 4 | scale (float) |

### `CreatureModelData.dbc`

Consumed by `game/m_creature.c` (`wowCreatureModelDataDbc_t`).

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | flags |
| 2 | model name (str) |
| 3 | size class |
| 4 | model scale (float) |
| 5–13 | unused by this target |
| 14 | collision width (float) — `radius = collision_width * 0.5` |

### `CreatureDisplayInfoExtra.dbc`

Consumed by `renderer/m2/r_dbc.c` (`M2_DbcResolveCreatureAppearance`). Requires at least 19 fields.

| Field | Content |
|------:|---------|
| 0 | id |
| 3 | skin color index |
| 4 | face index |
| 5 | hair style index |
| 6 | hair color index |
| 7 | facial hair style index |
| 8–18 | NPC item display IDs (up to 11, one per classic slot) → `ItemDisplayInfo.dbc` |

The 11 item slots map to shared outfit slots via `Wow_CharacterCreatureItemSlot`
(`common/wow_character_utils.h`): `{ head, shoulder, shirt, chest, belt, legs, boots, none, gloves, tabard, cape }`.

### `ItemDisplayInfo.dbc`

Consumed by `renderer/m2/r_dbc.c`. Classic local data is **23 fields** (record_size = 92).
Component texture stems start at **field 14** (25-field TBC/Wrath layouts start at field 15 — pick from
`record_size`, not a hardcoded base).

#### Classic 23-field layout

| Field | Content |
|------:|---------|
| 0 | Record ID |
| 1–2 | Model name string offsets (male, female) |
| 3 | Cape texture string offset |
| 4–6 | HelmGeosetVisData[0–2] |
| **7** | **GeosetGroup[0]** — primary mesh variant |
| **8** | **GeosetGroup[1]** — secondary mesh variant |
| **9** | **GeosetGroup[2]** — robe/kilt leg variant |
| 10 | Flags: bit 0 = helm hides hair; bit 2 = kneelength/robe hides pants group |
| 11 | SpellVisualID |
| 12–13 | Additional IDs |
| 14–21 | Eight component texture string offsets (upper arm → foot) |
| 22 | Extra field |

Cape texture (field 3) resolves under `Item\ObjectComponents\Cape\` and
`Item\TextureComponents\Cape\` with `_M` / `_F` / `_U` suffixes.
Body component stems (fields 14–21) resolve under `Item\TextureComponents\<slot>\` with the same suffixes.

#### Slot → GeosetGroup field mapping

For each equipment slot, the three DBC GeosetGroup fields drive these character geoset groups.
All entries verified by exhaustive scan of all 23 852 classic records.

| Slot | InvType(s) | field 7 → group | field 8 → group | field 9 → group |
|------|-----------|----------------|----------------|----------------|
| 0 none | — | — | — | — |
| 1 head | 1 | — | — | — |
| 2 shoulder | 3 | — | — | — |
| **3 chest** | 5, 20 | **8 (sleeves)** | — | **13 (robe leg coverage)** |
| **4 shirt** | 4 | **8 (sleeves)** | — | — |
| 5 belt | 6 | — | — | — |
| **6 legs** | 7 | **13 (pants mesh)** | **9 (kneepads)** | — |
| **7 boots** | 8 | **5 (boot mesh)** | — | — |
| **8 gloves** | 10 | **4 (glove mesh)** | — | — |
| 9 tabard | 19 | — | — | — |
| **10 cape** | 16 | **15 (cape mesh)** | — | — |

#### Geoset group variant tables

Section IDs follow `group * 100 + variant` throughout. Variant 0 means the DBC field was not set by
any item; variant 1 is the conventional "bare/default" mesh for most groups.

**Group 4 — Gloves** (`section = 401 + geoset`, driven by gloves field 7)

| geoset | section | notes |
|--------|---------|-------|
| 0 | 401 | bare forearms — default; texture painted on |
| 1 | 402 | glove mesh variant 1 |
| 2 | 403 | glove mesh variant 2 |
| 3 | 404 | glove mesh variant 3 |

Classic: all glove-slot items have field 7 = 0 — texture-only; sections 402–404 unreachable.

**Group 5 — Boots** (`section = Wow_CharacterGeosetPick(501 + geoset, fallback 501)`, boots field 7)

| geoset | section | notes |
|--------|---------|-------|
| 0 | 501 | bare shins — default; texture painted on |
| 1 | 502 | boot mesh variant 1 |
| 2 | 503 | boot mesh variant 2 |
| 3 | 504 | boot mesh variant 3 |

Fallback scan because some variants are absent from certain race/gender models.
Classic: all boot-slot items (including hardcoded display 27270) have field 7 = 0 — texture-only.

**Group 8 — Sleeves** (`section = 801 + geoset`, chest/shirt field 7)

| geoset | section | notes |
|--------|---------|-------|
| 0 | 801 | bare forearms — default |
| 1 | 802 | short sleeve mesh |
| 2 | 803 | long sleeve mesh |

Classic: 288 chest items set field 7 = 1. Shirt items (slot 4) share the same group via field 7.

**Group 9 — Kneepads** (`section = 900 + geoset`, legs field 8)

| geoset | section | notes |
|--------|---------|-------|
| 0 | 900 | DNE — no kneepads (default) |
| 1 | 901 | DNE — no kneepads (explicit none) |
| 2 | 902 | long kneepads |
| 3 | 903 | short kneepads |

Variants 0 and 1 are both DNE (no mesh). Use `900 + geoset`, not `901 + geoset` — the latter would
mis-map variant 2 → section 903 (short instead of long).
Classic: 809 legs items set field 8 = 1 (→ 901 DNE = no kneepads); zero items set field 8 = 2 or 3.
Sections 902/903 exist in shipped models but no Classic `ItemDisplayInfo` record activates them.

**Group 13 — Pants** (`section = Wow_CharacterGeosetPick(1301 + geoset, fallback 1301)`, legs field 7 or chest field 9)

| geoset | section | notes |
|--------|---------|-------|
| 0 | 1301 | short pants — default/fallback |
| 1 | 1302 | standard trousers |
| 2 | 1303 | robe/full-length leg coverage |

Fallback scan because some variants are absent from certain race/gender models.
Hidden entirely when `M2_CHAR_FLAG_KNEELENGTH` (field 10 bit 2) is set by a robe/kilt item.
Classic: 288 legs items set field 7 = 1 (→ 1302); 21 set field 7 = 2 (→ 1303).
Robe chest items (InvType 20) drive this group via field 9 (e.g. display 12646, field 9 = 2 → 1303).
Starter pants (e.g. display 9892, Human Warrior) have field 7 = 0 — fall back to section 1301.

**Group 15 — Cape** (`section = 1501 + geoset`, cape field 7)

| geoset | section | notes |
|--------|---------|-------|
| 0 | 1501 | no-cape bare back — default |
| 1 | 1502 | cape geometry visible |

Classic: all 7 701 cape records have field 7 = 0. `geoset[15]` is never set from `ItemDisplayInfo`.
Cape mesh (1502) requires deriving `geoset[15] = 1` from `outfit->cape_texture != NULL` in
`M2_DbcAddDisplayInfo` — **not yet implemented** (TODO).

### `CharSections.dbc`

Consumed by `renderer/m2/r_dbc.c` (`M2_DbcCharacterVariationTexturePath`). Two schemas exist, detected by sampling
field 4 (`m2_char_sections_layout` in `renderer/m2/r_m2_utils.h`):

| Layout | race | gender | section | variation | color | texture[0..2] |
|--------|-----:|-------:|--------:|----------:|------:|--------------:|
| variation-first (Classic/TBC, HD-texture Wrath) | 1 | 2 | 3 | 4 | 5 | 6–8 |
| texture-first (stock Wrath) | 1 | 2 | 3 | 8 | 9 | 4–6 |

### `CharStartOutfit.dbc`

Consumed by `renderer/m2/r_dbc.c` (`M2_DbcStartOutfit`).

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | key = `race | (class << 8) | (gender << 16)` |
| 14–25 | 12 starter item display IDs → `ItemDisplayInfo.dbc` |
| 26–37 | parallel `InventoryType` array (type 0 = non-equipment, skip) |

The local Human Male Warrior key is `257`; its record holds shirt/legs/boots display IDs `9891`, `9892`, `10141`.
No starter record includes InvType 10 (gloves) or InvType 16 (cape) — starter characters always show
bare hands (section 401) and no cape (section 1501).

### `CharHairGeosets.dbc` / `CharHairTextures.dbc` / `HelmetGeosetVisData.dbc`

Listed in the character/creature rule set (`docs/wow-character.md`); read the local schemas with `dbctool` before
changing renderer policy. `HelmetGeosetVisData` feeds `ItemDisplayInfo` fields 4–6.

## UI And Metadata Tables

Read by `ui/ui_dbc.c` (character-create) and `game/g_wow.c` (map metadata).

### `ChrRaces.dbc` (29 fields, 1.x)

Field 0 id, 1 flags (bit 0 = NPC-only), 2 faction, 4 male display id, 5 female display id, 15 client file (str),
17 name (str), 26 hair custom (str), 27–28 facial-hair custom (str).

### `ChrClasses.dbc` (16 fields, 1.x)

Field 0 id, 5 name (str), 14 filename (str, e.g. `WARRIOR`).

### `CharBaseInfo.dbc`

**2-byte records** (not 32-bit fields): byte 0 = race id, byte 1 = class id. `record_size == 2`, so field-count math in
the WDBC header does not apply the usual way.

### `FactionTemplate.dbc`

Field 0 id, 1 faction, 2 flags, 3 faction group.

### `FactionGroup.dbc`

Field 0 id, 1 mask id, 2 internal name (str), 3 name (str).

### `Map.dbc` / `WorldSafeLocs.dbc`

Read by `game/g_wow.c` for map directory/title resolution and spawn/safe-location lookup. See
[`docs/spawn-and-teleport.md`](spawn-and-teleport.md) and [`docs/cinematics.md`](cinematics.md).

## Diagnostic Workflow

```bash
build/bin/mpqtool -mpq data/world-of-warcraft/dbc.MPQ ls DBFilesClient
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\CharStartOutfit.dbc'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\CharStartOutfit.dbc' 24
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\ItemDisplayInfo.dbc'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\ItemDisplayInfo.dbc' 3
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\CreatureDisplayInfo.dbc'
```

## Known Pitfalls

- `field_count * 4 != record_size` on some classic tables; never reject the file, bounds-check each field.
- `ItemDisplayInfo` component offsets differ between the 23-field (14) and 25-field (15) schemas. Picking the wrong base
  shifts every clothing component.
- `CharSections` has two field orders; detect by sampling field 4 rather than assuming one schema.
- `CharBaseInfo` uses 2-byte records, not 4-byte fields.
- `equipment` bytes are local slot indices, not raw item IDs; index 0 means empty.
- Customization index 0 is a real variation; a zero `appearance` does not mean "no outfit".
- String fields are offsets, not inline text; offset 0 means null.
