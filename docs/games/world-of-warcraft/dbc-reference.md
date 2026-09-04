# WoW DBC Reference

This is the single reference for how `openwow` reads classic-era client databases (`.dbc` / `WDBC` files) and what each
table contains. It documents the binary container, the packed appearance/equipment values, the lookup chains, and every
DBC file the codebase or its references touch — populated where a field layout is verified and marked **TODO** where a
table is known to exist but not yet read or mapped.

Sources are distinguished:

- **code** — field offsets actually read by `games/world-of-warcraft/` (or the shared `sound/` module). Authoritative for
  what `openwow` currently consumes.
- **WoWee** — `data/WoWee/Data/expansions/{classic,tbc,wotlk,turtle}/dbc_layouts.json`, the local client reimplementation's
  field-name → column maps. Classic is the primary reference; TBC/Wrath noted for version deltas.
- **wotlkdev** — https://wotlkdev.github.io/wiki/dbc/ (3.3.5 auto-generated schema). Used for tables `openwow` does not
  yet read (sound/cinematic/hair) and for Wrath-era field shifts.

The same information is split across several places; this file is the index of record:

- Character pipeline and diagnostic workflow: [`docs/wow-character.md`](../../wow-character.md)
- M2 loader, geoset/component resolution: [`docs/m2-and-character-display.md`](m2-and-character-display.md)
- Loading order and tool commands: [`docs/data-loading.md`](data-loading.md)
- High-level format map and reference links: [`docs/file-formats.md`](file-formats.md)

## Contents

- [Why DBCs Instead Of `legs=2 arms=3`](#why-dbcs-instead-of-legs2-arms3)
- [WDBC Binary Format](#wdbc-binary-format)
- [Reader Ownership](#reader-ownership)
- [Reading A DBC — The Schema Pattern](#reading-a-dbc--the-schema-pattern)
- [Packed Values](#packed-values)
- [Lookup Chains](#lookup-chains)
- [DBC Inventory](#dbc-inventory)
- [Character And Creature Tables](#character-and-creature-tables)
- [Ground Effect / Terrain Tables](#ground-effect--terrain-tables)
- [Spell / Visual Tables](#spell--visual-tables)
- [UI And Metadata Tables](#ui-and-metadata-tables)
- [Map / World Metadata Tables](#map--world-metadata-tables)
- [Sound Tables](#sound-tables)
- [Cinematic Tables](#cinematic-tables)
- [Version Differences](#version-differences)
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

Some tables are exceptions to the "32-bit fields" rule:

- `CharBaseInfo.dbc` uses **2-byte records** (byte 0 = race, byte 1 = class), so the field-count math does not apply.
- `CharStartOutfit.dbc` packs `race/class/sex/outfit` as four `int8` bytes inside field 1 (see its section).

## Reader Ownership

`games/world-of-warcraft/common/stb_dbc.h` is the single-header `static inline` reader included by every DBC consumer.
It has two layers: a **stateless parser** (`Stb_Dbc*` — header validation, little-endian reads, string/field access, ID
lookup, no I/O and no allocation) and a **stateful cache** (`Stb_DbcCache*` — lazy load + decode + FNV-1a index). The
cache takes a `stbDbcIO_t` function table so the header stays free of direct FS/allocator dependencies; each module
adapts its own handle (`ri` / `gi` / `uiimport`). Callers that need a one-shot read hand a resident buffer to `Stb_Dbc*`
directly; callers that need a decoded struct array go through `Stb_DbcCache*`. None parses DB2:

- **Renderer** — `renderer/m2/r_dbc.c` holds only structs + column→field schemas + thin typed finders; the cache
  (`m2_dbc_io` over `ri.*`) does the resident image, decode, and FNV index. Consumers read named struct fields, never
  raw column offsets.
- **Game** — `game/g_wow.c` decodes the spell-visual chain (`SpellVisual`/`Kit`/`EffectName`), `Map`, `LoadingScreens`,
  and `Spell` through the shared cache/schemas; `game/m_creature.c` maps `CreatureDisplayInfo`/`CreatureModelData`;
  `game/g_gameobject.c` maps `GameObjectDisplayInfo`. One shared `g_dbc_io` (`gi.*`) is exported from `g_wow_local.h`.
- **Common world** — `common/world_wow.c` reads `Map.dbc` and `WorldSafeLocs.dbc` for spawn/map resolution.
- **UI** — `ui/menu_dbc.c` decodes `ChrRaces`, `ChrClasses`, `FactionTemplate`, `FactionGroup` through the cache
  (`ui_dbc_io` over `uiimport.*`); `CharBaseInfo` stays a 2-byte-record special case.
- **Sound** — `sound/s_sound.c` loads `SoundEntries.dbc` for kit name/path lookup.
- **Engine common** — `common/common.c` resolves numeric map IDs through `Map.dbc` (`Com_WowMapPathForId`, WOW-only).

## Reading A DBC — The Schema Pattern

This is the **only accepted way** to add or extend a DBC reader. A file-shaped struct mirrors the consumed subset of a
row, a `stbDbcField_t` schema table maps DBC columns to struct fields, and `Stb_DbcParseRows` (or the lazy
`Stb_DbcCacheDecode`) fills the array. Consumers read named struct fields — never raw column offsets (`record + 14 * 4`,
`Stb_DbcField(&h, rec, 14)`), never a hand-rolled decode loop, never a `strcmp`/`if` ladder over field names. The
stateless `Stb_DbcField` / `Stb_DbcString` calls in [Reader Ownership](#reader-ownership) exist only for single-purpose
reads of a handful of fields (`Map.dbc` directory in `common/common.c`, `SoundEntries.dbc` in `sound/s_sound.c`); every
table read for structure — renderer, game, UI — goes through a schema.

### The recipe

1. Define a struct holding exactly the fields you consume; collapse a run of consecutive columns into one array field.
2. Define one `static stbDbcField_t const schema[]`. Each entry is `{ column, offsetof(Rec, field), type, count }`;
   `count` (default 1) maps consecutive columns to consecutive array elements, so eight component textures are one line
   (`{ 14, offsetof(Rec, component_texture), STB_DBC_STR, 8 }`).
3. Decode through the shared cache, look up by id (or by a named key column with `Stb_DbcCacheFindKey`), then read the row:

```c
typedef struct { DWORD id, geoset_group[3], flags; LPCSTR component_texture[8]; } m2ItemDisplayInfoRec_t;
static stbDbcField_t const item_display_info_schema[] = {
    {  0, offsetof(m2ItemDisplayInfoRec_t, id),                STB_DBC_U32 },
    {  7, offsetof(m2ItemDisplayInfoRec_t, geoset_group),      STB_DBC_U32, 3 },
    { 10, offsetof(m2ItemDisplayInfoRec_t, flags),             STB_DBC_U32 },
    { 14, offsetof(m2ItemDisplayInfoRec_t, component_texture), STB_DBC_STR, 8 },
};
static m2ItemDisplayInfoRec_t const *M2_ItemDisplayInfo(DWORD id) {
    int idx;
    if (!Stb_DbcCacheLoad(&item_display_info_dbc, "DBFilesClient\\ItemDisplayInfo.dbc", &m2_dbc_io)) return NULL;
    Stb_DbcCacheDecode(&item_display_info_dbc, item_display_info_schema, M2_COUNT(item_display_info_schema),
                       sizeof(m2ItemDisplayInfoRec_t), &m2_dbc_io);
    idx = Stb_DbcCacheFindID(&item_display_info_dbc, id, &m2_dbc_io);
    return idx < 0 ? NULL : STB_DBC_ROW(item_display_info_dbc, m2ItemDisplayInfoRec_t, idx);
}
```

`stbDbcField_t.type` is the shared `bzFieldType_t`. The DBC aliases deliberately expose only its supported subset:
`STB_DBC_U32` is `BZ_FIELD_U32`, `STB_DBC_FLOAT` is `BZ_FIELD_FLOAT`, and `STB_DBC_STR` is
`BZ_FIELD_CSTR` because DBC string-block offsets decode to resident pointers. `BZ_FIELD_CHAR_ARRAY` is for bounded
text copies such as SC2 XML and is rejected by the DBC decoder rather than silently treated as a string pointer.

The decode is idempotent (`Stb_DbcCacheDecode` returns early once `rows` is set), so callers can ask for a row without
worrying about double-decoding; each typed finder guards with `if (!cache.rows)` only when the schema must be picked
first (next section).

### Version differences: one schema per layout, dispatch on the header

When a table's layout shifts between client versions, keep **one schema per version** and a small dispatch function that
returns the right one. Key the dispatch on the field count the header actually reports (or, when the count is not a
reliable discriminator, probe a field like `CharSections` does with `m2_char_sections_layout`). Never use `#ifdef`,
never assume the classic layout, never keep a parallel hardcoded id→offset table.

`ItemDisplayInfo` shifts its vis/texture block one column right between the 23-field classic and 25-field Wrath layouts:

```c
static stbDbcField_t const item_display_info_classic_schema[] = { /* helm_vis @12, component_texture @14 */ };
static stbDbcField_t const item_display_info_wrath_schema[]   = { /* helm_vis @13, component_texture @15 */ };
/* Pre-23-field guard: pins every field to column 0 (id) so a malformed archive
 * cannot feed out-of-range columns into the struct. Unreachable for real archives. */
static stbDbcField_t const item_display_info_legacy_schema[]  = { /* all fields -> column 0 */ };

static stbDbcField_t const *item_display_info_schema(DWORD fields, LPDWORD count) {
    switch (m2_item_display_texture_base(fields)) {
        case 15: *count = M2_COUNT(item_display_info_wrath_schema);   return item_display_info_wrath_schema;
        case 14: *count = M2_COUNT(item_display_info_classic_schema); return item_display_info_classic_schema;
        default: *count = M2_COUNT(item_display_info_legacy_schema);  return item_display_info_legacy_schema;
    }
}
```

`m2_item_display_texture_base` (`renderer/m2/r_m2_utils.h`) collapses the field count to the one discriminator column
(`fields >= 25 ? 15 : fields >= 22 ? 14 : 0`). The schema is chosen once before the first decode and cached:
`if (!cache.rows) { schema = item_display_info_schema(cache.fields, &count); Stb_DbcCacheDecode(&cache, schema, count, ...); }`.
Consumers never see the version switch again.

### Anti-patterns

- Raw column reads in consumer code (`record + 14 * 4`, `Stb_DbcField(&h, rec, 14)` spread across a file).
- Two decoders for the same table with different offsets (the layout must live in one schema).
- A `strcmp`/`if` ladder to map field names or a hardcoded id→offset table — the schema table is the single source of
  truth, so a client-version change is fixed in one spot and cross-checked against `dbctool dump` / WoWee
  `dbc_layouts.json` (see [Diagnostic Workflow](#diagnostic-workflow)).

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

### Spell visual

```text
Spell.dbc .SpellVisualID
  -> SpellVisual.dbc  (precast/cast/impact kits, missile effect)
  -> SpellVisualKit.dbc -> effect slot probe order  (Special0 > Base > Left > Right > Chest > Head > Breath)
  -> SpellVisualEffectName.dbc  -> M2 path (.mdx/.mdl normalized to .m2)
```

## DBC Inventory

Status legend: **read** = field layout consumed by code; **partial** = only some fields known; **TODO** = known to
exist but not read or unmapped.

| DBC | Status | Consumer | Purpose |
| --- | --- | --- | --- |
| `CreatureDisplayInfo` | read | `r_dbc.c`, `m_creature.c`, `g_gameobject.c` | creature → model / extra / sound / skin |
| `CreatureDisplayInfoExtra` | read | `r_dbc.c` | character-like NPC appearance + 11 item display IDs |
| `CreatureModelData` | read | `m_creature.c`, `g_gameobject.c` | model path, scale, collision width |
| `ItemDisplayInfo` | read | `r_dbc.c` | item model/texture stems, geoset groups, flags |
| `CharSections` | read | `r_dbc.c` | skin/face/hair/facial-hair texture variants |
| `CharStartOutfit` | read | `r_dbc.c` | starter item display IDs by race/class/gender |
| `ChrRaces` | read | `menu_dbc.c`, `cinematics` | race records, cinematic sequence id |
| `ChrClasses` | read | `menu_dbc.c` | class records |
| `CharBaseInfo` | read | `menu_dbc.c` | valid race/class pairs (2-byte records) |
| `FactionTemplate` | read | `menu_dbc.c` | faction membership/flags |
| `FactionGroup` | read | `menu_dbc.c` | faction group names |
| `Map` | read | `g_wow.c`, `world_wow.c` | map id ↔ directory ↔ title ↔ loading screen |
| `WorldSafeLocs` | read | `world_wow.c`, `g_wow.c` | safe spawn locations + names |
| `LoadingScreens` | read | `g_wow.c` | loading screen texture path |
| `GameObjectDisplayInfo` | read | `g_gameobject.c` | game-object model path ↔ display id |
| `GroundEffectTexture` | read | `r_wowmap_grass.c` | terrain effect → doodad ids + weights + density |
| `GroundEffectDoodad` | read | `r_wowmap_grass.c` | doodad id → model path |
| `Spell` | read | `g_wow.c` | spell id → SpellVisualID |
| `SpellVisual` | read | `g_wow.c` | visual id → precast/cast/impact kits + missile |
| `SpellVisualKit` | read | `g_wow.c` | kit id → effect slots |
| `SpellVisualEffectName` | read | `g_wow.c` | effect id → M2 path |
| `SoundEntries` | read | `sound/s_sound.c` | sound kit id → file paths |
| `CharHairGeosets` | partial | — (WoWee map only) | hair style → geoset id |
| `CharacterFacialHairStyles` | partial | — (WoWee map only) | facial-hair style → geoset ids |
| `HelmetGeosetVisData` | read | `r_dbc.c` | per-race helmet hide-geoset masks |
| `CharHairTextures` | partial | — (wotlkdev only) | hair texture variants (obscure schema) |
| `SoundEntriesAdvanced` | TODO | — | advanced sound kit fields |
| `CreatureSoundData` | TODO | — | per-creature sound event slots |
| `SpellCastTimes` | TODO | — | spell cast-time index (via `Spell.CastingTimeIndex`) |
| `SpellRange` | partial | — | spell min/max range (via `Spell.RangeIndex`) |
| `CinematicSequences` | TODO | — | race intro sequence → cameras |
| `CinematicCamera` | TODO | — | flyby camera model + origin + facing |

## Character And Creature Tables

Field numbers are zero-based. String fields are marked `(str)` and store string-block offsets.

### `CreatureDisplayInfo.dbc`

| Field | Content |
|------:|---------|
| 0 | id (== `display_id`) |
| 1 | model id → `CreatureModelData.dbc` |
| 2 | sound id |
| 3 | extended display info id → `CreatureDisplayInfoExtra.dbc` (0 = none) |
| 4 | scale (float) |
| 6–8 | skin texture ids 1–3 (WoWee `Skin1/2/3`) |

`m_creature.c` reads 0–4; `r_dbc.c` reads field 3 to reach the extra record. WoWee classic confirms fields 1/3/6–8.

### `CreatureModelData.dbc`

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

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | race id |
| 2 | sex id |
| 3 | skin color index |
| 4 | face index |
| 5 | hair style index |
| 6 | hair color index |
| 7 | facial hair style index |
| 8–18 | NPC item display IDs (11, one per classic slot) → `ItemDisplayInfo.dbc` |
| 20 | baked name (str, WoWee `BakeName`) |

Fields 3–7 feed `Wow_PackAppearance`; the 11 item slots map to shared outfit slots via
`Wow_CharacterCreatureItemSlot` (`common/wow_character_utils.h`):
`{ head, shoulder, shirt, chest, belt, legs, boots, none, gloves, tabard, cape }`. All 11 columns (8–18) are decoded
through the schema (the `display_ids[11]` array entry), so the cape slot (index 10) is populated too.

### `ItemDisplayInfo.dbc`

Classic local data is 23 fields; component texture stems start at **field 14** (25-field layouts start at field 15 —
`m2_item_display_texture_base` in `renderer/m2/r_m2_utils.h`). Full classic layout (verified against the 23 852-record
local DBC, documented in `m2-and-character-display.md`):

| Field | Content |
|------:|---------|
| 0 | record ID |
| 1–2 | model name stems (str) |
| 3 | cape texture stem (str) |
| 4–6 | model texture / icon / ground-model ids |
| 7 | `GeosetGroup[0]` — primary mesh variant |
| 8 | `GeosetGroup[1]` — secondary mesh variant |
| 9 | `GeosetGroup[2]` — robe/kilt leg variant |
| 10 | flags (bit 0 = helm hides hair; bit 2 = kneelength/robe hides pants) |
| 11–13 | spell-visual / sound / `HelmetGeosetVisData` (12 = male, 13 = female) → `HelmetGeosetVisData.dbc` |
| 14–21 | eight component texture stems (str): upper arm, lower arm, hand, upper torso, lower torso, upper leg, lower leg, foot |

WoWee names fields 7/9 `GeosetGroup1/3` (field 8 unlabeled in their map but present); component texture slots match
(`TextureArmUpper`=14 … `TextureFoot`=21). Texture stems resolve under `Item\TextureComponents\<slot>\` with `_M` /
`_F` / `_U` suffixes.

#### Item Attachment Models

`model name` stems (fields 1–2) are separate M2 attachments, not body geosets. The head slot reads field 1 as the
helmet model and the shoulder slot reads fields 1/2 as the left/right shoulder models. `r_m2.c` resolves them to
`Item\ObjectComponents\Head\<stem>_<race><gender>.m2` (per-race/gender) and
`Item\ObjectComponents\Shoulder\<stem>.m2` (shared), then renders them at the character's helm (attachment id 11) and
shoulder (ids 5/6) bones. Deputy Willem's Stormwind guard set is the oracle: head 14964 → `Helm_Plate_B_01Stormwind`,
shoulder 7541 → `LShoulder_Plate_B_01` / `RShoulder_Plate_B_01`.

Attachment M2s use a replaceable object-skin texture (M2 texture type 2) with an empty filename. The client fills it
from the item's `model texture` stem (field 3 for head, fields 3/4 for shoulders), resolved to
`Item\ObjectComponents\Head\<stem>.blp` / `Item\ObjectComponents\Shoulder\<stem>.blp`. `r_m2.c` stores those stems on
the outfit and overrides the attachment's texture at render time via `renderEntity_t.skin`.

#### Slot → Geoset Group Mapping (`slot_geoset_group_map`)

For each equipment slot, the three DBC GeosetGroup fields (7/8/9) drive these character geoset groups. Verified against
the full 23 852-record classic DBC.

| Slot | InvType(s) | field 7 → group | field 8 → group | field 9 → group |
|------|-----------|----------------|----------------|----------------|
| 0 none | — | — | — | — |
| 1 head | 1 | — | — | — |
| 2 shoulder | 3 | — | — | — |
| 3 chest | 5, 20 | 8 (sleeves) | — | 13 (robe leg coverage) |
| 4 shirt | 4 | 8 (sleeves) | — | — |
| 5 belt | 6 | — | — | — |
| 6 legs | 7 | 13 (pants mesh) | 9 (kneepads) | — |
| 7 boots | 8 | 5 (boot mesh) | — | — |
| 8 gloves | 10 | 4 (glove mesh) | — | — |
| 9 tabard | 19 | — | — | — |
| 10 cape | 16 | 15 (cape mesh) | — | — |

#### Geoset Group Variant Tables

`section = group * 100 + variant` throughout. All variants verified by exhaustive classic DBC scan.

**Group 4 — Gloves** (`401 + geoset`, driven by glove-slot field 7): `0→401` bare, `1→402`, `2→403`, `3→404`. All
23 852 glove-slot items have field 7 = 0; gloves are texture-only in Classic (sections 402–404 exist but are unreachable
through `ItemDisplayInfo`).

**Group 5 — Boots** (`Wow_CharacterGeosetPick(501 + geoset, fallback 501)`, driven by boot field 7): `0→501` bare,
`1→502`, `2→503`, `3→504`. All boot-slot items (including hardcoded display 27270) have field 7 = 0; boots are
texture-only in Classic.

**Group 8 — Sleeves** (`801 + geoset`, driven by chest/shirt field 7): `0→801` bare, `1→802` short, `2→803` long.
Chest items with visible sleeves set field 7 = 1 (288 records); shirt items also drive this group.

**Group 9 — Legs / Kneepads** (`901 + geoset`, driven by legs field 8): `0→901` DNE (bare), `1→902` long,
`2→903` short. wowdev numbers these `09**: Legs {1: none (DNE), 2: long, 3: short}`; the decompiled `GeosRenderPrep`
(TBC 2.4.3) selects `901 + geosetGroup[1]` and falls back to `901` when the pants item carries no value. 809 legs
items set field 8 = 1 (→ 902 long); none set 2 or 3, so every Classic pants item either keeps the bare legs (no leg
mesh) or shows the long legcuffs. The starter legs (display 9892) and Deputy Willem's plate legs (display 7225) both
set field 8 = 0, so neither overrides the base leg mesh.

**Group 12 — Tabard** (`1200 + geoset`): `1→1201` DNE (no tabard), `2→1202` hanging tabard flap. The renderer sets
`geoset[12] = 2` whenever a tabard slot item is present, so wearing any tabard shows the tabard mesh.

**Group 13 — Pants** (`Wow_CharacterGeosetPick(1301 + geoset, fallback 1301)`, driven by legs field 7 or chest field 9):
`0→1301` short, `1→1302` trousers, `2→1303` robe/full-length. Hidden when `M2_CHAR_FLAG_KNEELENGTH` (field 10 bit 2) is
set. 288 legs items set field 7 = 1 (→ 1302); 21 set field 7 = 2 (→ 1303). Robe chest items (InvType 20) drive this group
via field 9 (e.g. display 12646, Orc Warlock robe: field 9 = 2 → 1303). Starter pants such as display 9892 have field 7 = 0,
falling back to 1301.

**Group 15 — Cape** (`1501 + geoset`, driven by cape field 7): `0→1501` no-cape, `1→1502` cape geometry. All 7 701 cape
records have field 7 = 0; `geoset[15]` is never set from `ItemDisplayInfo`. Cape geometry (1502) must be enabled by
deriving `geoset[15] = 1` from `outfit->cape_texture != NULL` in `M2_DbcAddDisplayInfo` — **not yet implemented** (TODO).

### `CharSections.dbc`

Two schemas exist, detected by sampling field 4 (`m2_char_sections_layout` in `renderer/m2/r_m2_utils.h`); WoWee
`detectCharSectionsFields()` uses the same probe.

| Layout | race | gender | section | variation | color | texture[0..2] | flags |
|--------|-----:|-------:|--------:|----------:|------:|--------------:|------:|
| variation-first (Classic/TBC, HD-texture Wrath) | 1 | 2 | 3 | 4 | 5 | 6–8 | 9 |
| texture-first (stock Wrath) | 1 | 2 | 3 | 8 | 9 | 4–6 | 7 |

### `CharStartOutfit.dbc`

Classic layout (read by `r_dbc.c` `M2_DbcStartOutfit`):

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | key = `race | (class << 8) | (gender << 16)` (four packed `int8`) |
| 14–25 | 12 starter item display IDs → `ItemDisplayInfo.dbc` |
| 26–37 | parallel `InventoryType` array (type 0 = non-equipment, skip) |

The local Human Male Warrior key is `257`; its record holds shirt/legs/boots display IDs `9891`, `9892`, `10141`.

Later (3.3.5) layout grows to 24 slots: `ID`, `RaceID`/`ClassID`/`SexID`/`OutfitID` (four `int8` in field 1),
`ItemID[24]`, `DisplayItemID[24]`, `InventoryType[24]`.

### `CharHairGeosets.dbc` (partial — WoWee)

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | race id |
| 2 | sex id |
| 3 | variation |
| 4 | geoset id |

Maps a hair-style variation to the M2 geoset that renders it. Listed in the character rule set
(`docs/wow-character.md`); not yet read by `openwow`.

### `CharHairTextures.dbc` (partial — wotlkdev)

8 fields; `0=id`, `1=race`, `2=gender`, `3=maybe race mask`, `4=the X in hair_XY.blp`, `5–7` unclear. Even the
wotlkdev auto-generated schema leaves these unlabeled. Treat as unmapped until inspected locally.

### `HelmetGeosetVisData.dbc`

Classic layout (6 fields). Each flag is a race bitmask (`1 << race`); a set bit hides the matching geoset group for
that race. Geoset groups follow wowdev `Character Customization` numbering (`group = section / 100`):

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | `hairFlags` → hide hair/head (group 0, sections 1–99) |
| 2 | `facialFlags[0]` → hide beard (group 1, sections 100–199) |
| 3 | `facialFlags[1]` → hide sideburns/earrings (group 2, sections 200–299) |
| 4 | `facialFlags[2]` → hide moustache (group 3, sections 300–399) |
| 5 | `earsFlags` → hide ears (group 7, sections 700–799) |

Section 0 (the base skin/face) is never hidden. Referenced by `ItemDisplayInfo` fields 12 (male) and 13 (female) in
the classic 23-field schema. `r_dbc.c` (`M2_DbcHelmetHideMask`) resolves the record for the model's race/gender into
`M2CHARACTEROUTFIT.helm_hide`, which `M2_CharacterGeosetVisible` applies per group instead of a blanket hair hide.
Deputy Willem's Stormwind helm (display 14964 → record 248 for a Human male) hides hair, beard, sideburns, moustache,
and ears (mask `0x8f`).

### `CharacterFacialHairStyles.dbc` (partial — WoWee)

| Field | Content |
|------:|---------|
| 0 | race id |
| 1 | sex id |
| 2 | variation |
| 6 | `Geoset100` |
| 8 | `Geoset200` |
| 7 | `Geoset300` |

Maps a facial-hair variation to up to three geoset ids. Note `RaceID` sits at field 0 here (unlike `CharSections`).

## Ground Effect / Terrain Tables

### `GroundEffectTexture.dbc`

Two 11-DWORD layouts are detected by resolving candidate doodad IDs through `GroundEffectDoodad.dbc`
(`Wow_GroundEffectLayout` in `r_wowmap_grass.c`).

| Field | legacy layout | modern weighted layout (wotlkdev) |
|------:|---------------|-----------------------------------|
| 0 | id | id |
| 1 | date stamp | doodad id 0 |
| 2 | continent id | doodad id 1 |
| 3 | zone id | doodad id 2 |
| 4 | texture id | doodad id 3 |
| 5–8 | doodad id 0–3 | weight 0–3 |
| 9 | density | density / coverage |
| 10 | sound | sound |

Field offsets in `r_wowmap.h`: legacy doodad field 5, modern doodad field 1, weight field 5, density field 9. Legacy
rows get uniform weight `1` for valid slots and skip `0xffffffff`; modern rows keep stored weights. The wotlkdev schema
(`ID`, `DoodadID[4]`, `DoodadWeight[4]`, `Density`, `Sound`) is the modern layout.

### `GroundEffectDoodad.dbc`

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | doodad path (str) |
| 2 | flags |

Model names resolve under `World\NoDXT\Detail\` and `.mdl` names normalize to `.m2`. `Wow_GroundEffectModelPath` probes
field 2 then field 1; the canonical model string is field 1 (wotlkdev `Doodadpath`).

## Spell / Visual Tables

### `Spell.dbc`

Read by `g_wow.c` for the `spell id → SpellVisualID` map only; the rest of the record is unmapped here but WoWee
documents the key columns.

| Field | Content (classic, 148 fields) |
|------:|--------------------------------|
| 0 | id |
| 1 | school |
| 4 | dispel type |
| 13 | targets |
| 15 | `CastingTimeIndex` → `SpellCastTimes.dbc` |
| 28 | power type |
| 29 | mana cost |
| 33 | `RangeIndex` → `SpellRange.dbc` |
| 40 | `DurationIndex` |
| 71–73 | `Effect[0..2]` |
| 80–82 | `EffectBasePoints[0..2]` |
| 115 | `SpellVisualID` → `SpellVisual.dbc` |
| 117 | icon id |
| 120 | name (str) |
| 129 | rank (str) |
| 138 | description (str) |
| 147 | tooltip (str) |

Classic/Turtle `SpellVisualID` is field **115**; TBC is **122**; WotLK (234 fields) is **131**. `g_wow.c` picks
`fields >= 200 ? 131 : 115`.

### `SpellVisual.dbc`

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | precast kit → `SpellVisualKit.dbc` |
| 2 | cast kit → `SpellVisualKit.dbc` |
| 3 | impact kit → `SpellVisualKit.dbc` |
| 8 | missile effect → `SpellVisualEffectName.dbc` |

### `SpellVisualKit.dbc`

Effect slots (each references `SpellVisualEffectName.dbc`):

| Field | Content |
|------:|---------|
| 0 | id |
| 3 | head effect |
| 4 | chest effect |
| 5 | base effect |
| 6 | left-hand effect |
| 7 | right-hand effect |
| 8 | breath effect |
| 11–13 | special effect 0–2 |

`Wow_ResolveKitPath` probes in priority order `{ 11, 5, 6, 7, 4, 3, 8 }` (Special0 > Base > Left > Right > Chest > Head
> Breath).

### `SpellVisualEffectName.dbc`

| Field | Content |
|------:|---------|
| 0 | id |
| 2 | file path (str) |

`.mdx`/`.mdl` extensions are normalized to `.m2`.

### `SpellRange.dbc` (partial)

| Layout | MinRange | MaxRange |
|--------|---------:|---------:|
| classic/Turtle | 1 | 2 |
| TBC/Wrath | 2 | 4 |

Referenced by `Spell.RangeIndex`; not directly read by `openwow`.

### `SpellCastTimes.dbc` (TODO)

Referenced by `Spell.CastingTimeIndex`. Layout not yet mapped.

## UI And Metadata Tables

### `ChrRaces.dbc` (29 fields, 1.x)

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | flags (bit 0 = NPC-only) |
| 2 | faction id |
| 4 | male display id |
| 5 | female display id |
| 15 | client file (str) |
| 16 | cinematic sequence id → `CinematicSequences.dbc` |
| 17 | name (str) |
| 26 | hair custom (str) |
| 27–28 | facial-hair custom (str) |

Verified: 9 records; Human row 0 field 16 = sequence 81, Orc row 1 field 16 = sequence 21.

### `ChrClasses.dbc` (16 fields, 1.x)

| Field | Content |
|------:|---------|
| 0 | id |
| 5 | name (str) |
| 14 | filename (str, e.g. `WARRIOR`) |

### `CharBaseInfo.dbc`

**2-byte records** (not 32-bit fields): byte 0 = race id, byte 1 = class id. `record_size == 2`, so the usual field-count
math does not apply.

### `FactionTemplate.dbc`

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | faction |
| 2 | flags |
| 3 | faction group |
| 5–9 | friend/enemy group masks (WoWee) |

`menu_dbc.c` reads 0–3; WoWee additionally maps `FriendGroup`=4, `EnemyGroup`=5, `Enemy[0..3]`=6–9.

### `FactionGroup.dbc`

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | mask id |
| 2 | internal name (str) |
| 3 | name (str) |

## Map / World Metadata Tables

### `Map.dbc`

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | directory name (str, `0 → Azeroth`, `1 → Kalimdor` in 1.12) |
| 3 | title (str) |
| last | loading screen id → `LoadingScreens.dbc` |

`g_wow.c` reads fields 0/1/3 plus the trailing field for the loading screen; `world_wow.c` uses field 1 for the map
directory. The `(race, class) → map` mapping is **not** here — it is AzerothCore `playercreateinfo`
(see `spawn-and-teleport.md`).

### `WorldSafeLocs.dbc`

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | map id |
| 2–4 | position x/y/z (float) |
| 5+ | area name (str) |

`CM_WowWorldSafeLocName` scans fields 5+ for the first valid string. wotlkdev confirms `ID`, `Continent`, `Loc[3]`,
`AreaName`.

### `LoadingScreens.dbc`

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | name (str) |
| 2 | file name (str) — the texture path `openwow` uses |
| 3 | has wide screen |

### `GameObjectDisplayInfo.dbc`

| Field | Content |
|------:|---------|
| 0 | id |
| 1 | model name (str) |
| 2–11 | sound ids |
| 12–17 | bounding box (float[6]) |
| 18 | object effect package id |

Model names are stored without extension; `g_gameobject.c` matches on the filename stem.

## Sound Tables

### `SoundEntries.dbc` (29 fields, 116 bytes/record)

| Field | Name | Type | Description |
|-------|------|------|-------------|
| 0 | ID | uint32 | primary key (kit id) |
| 1 | type | uint32 | 1=Spell, 2=UI, 3=Footsteps, 4=PropAmbience, … |
| 2 | name | string | display/lookup name |
| 3–12 | file[0..9] | string | up to 10 WAV/MP3 paths |
| 13–22 | freq[0..9] | uint32 | weight for random selection (0 = never play) |
| 23 | directoryBase | string | path prefix prepended to each file |
| 24 | volumeFloat | float | base playback volume |
| 25 | flags | uint32 | playback flags |
| 26 | minDistance | float | 3D min distance |
| 27 | distanceCutoff | float | hard cutoff distance |
| 28 | eaxdef | uint32 | EAX reverb preset index |
| 29 | advancedID | uint32 | linked advanced sound entry |

Full path = `directoryBase + "\" + file[n]`. `sound/s_sound.c` reads fields 3–12 for files and resolves
`directoryBase + file[0]`.

### `CreatureSoundData.dbc` (TODO)

Per-creature sound event slots. Fields (wotlkdev): `SoundExertionID`, `SoundExertionCriticalID`, `SoundInjuryID`,
`SoundInjuryCriticalID`, `SoundInjuryCrushingBlowID`, `SoundDeathID`, `SoundStunID`, `SoundStandID`, `SoundFootstepID`,
`SoundAggroID`, `SoundWingFlapID`, `SoundWingGlideID`, `SoundAlertID`, `SoundFidget[5]`, `CustomAttack[4]`,
`NPCSoundID`, `LoopSoundID`, `CreatureImpactType`, `SoundJumpStartID`, `SoundJumpEndID`, `SoundPetAttackID`,
`SoundPetOrderID`, `SoundPetDismissID`, `FidgetDelaySecondsMin/Max`, `BirthSoundID`, `SpellCastDirectedSoundID`,
`SubmergeSoundID`, `SubmergedSoundID`, `CreatureSoundDataIDPet`. Linked from `CreatureDisplayInfo` via `CreatureSoundData`
(see `docs/sounds.md`). Not yet read by `openwow`.

### `SoundEntriesAdvanced.dbc` (TODO)

Referenced by `SoundEntries.advancedID`. Layout not yet mapped.

## Cinematic Tables

### `CinematicSequences.dbc` (TODO)

10 fields: `ID`, `SoundID`, then 8 camera ids → `CinematicCamera.dbc`. Sequence 21 → camera 235 (Orc); sequence 81 →
camera 142 (Human).

### `CinematicCamera.dbc` (TODO)

7 fields: `ID`, model path (str, e.g. `Cameras\FlybyOrc.mdx`), origin `x/y/z` (float), facing. DBC strings use the
historical `.mdx` spelling; archive lookup resolves the corresponding `.m2`.

## Version Differences

Field shifts worth knowing when a new client archive is mounted (sources: WoWee `dbc_layouts.json`, wotlkdev):

| Table / field | Classic / Turtle | TBC | WotLK (3.3.5) |
| --- | ---: | ---: | ---: |
| `Spell.SpellVisualID` | 115 | 122 | 131 |
| `Spell` field count | 148 | ~? | 234 |
| `SpellRange.MinRange` / `MaxRange` | 1 / 2 | 2 / 4 | 2 / 4 |
| `ItemDisplayInfo` texture base | 14 (23 fields) | — | 15 (25 fields) |
| `CharSections` schema | variation-first | variation-first | texture-first (stock) / variation-first (HD) |
| `SpellItemEnchantment.Name` | 10 | 13 | 14 |
| `SpellItemEnchantment.ItemVisual` | 19 | 30 | 31 |

`CreatureDisplayInfo`, `CreatureDisplayInfoExtra`, `CharSections`, `ItemDisplayInfo`, `CharHairGeosets`,
`CharacterFacialHairStyles`, and the spell-visual chain (`SpellVisual`/`Kit`/`EffectName`) keep identical field indices
across Classic/TBC/Wrath in the WoWee maps.

## Diagnostic Workflow

```bash
build/bin/mpqtool -mpq data/world-of-warcraft/dbc.MPQ ls DBFilesClient
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\CharStartOutfit.dbc'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\CharStartOutfit.dbc' 24
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\ItemDisplayInfo.dbc'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\ItemDisplayInfo.dbc' 3
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\CreatureDisplayInfo.dbc'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\GroundEffectTexture.dbc' 1
```

Cross-check a suspected field offset against the WoWee maps before changing code:

```bash
python3 - <<'PY'
import json
d = json.load(open('data/WoWee/Data/expansions/classic/dbc_layouts.json'))
print(d['SpellVisual'])
PY
```

## Known Pitfalls

- `field_count * 4 != record_size` on some classic tables; never reject the file, bounds-check each field.
- `ItemDisplayInfo` component offsets differ between the 23-field (14) and 25-field (15) schemas. Picking the wrong base
  shifts every clothing component.
- `CharSections` has two field orders; detect by sampling field 4 rather than assuming one schema.
- `CharBaseInfo` uses 2-byte records, not 4-byte fields.
- `GroundEffectTexture` has a legacy and a modern weighted layout; detect by resolving doodad IDs, not by field count.
- `equipment` bytes are local slot indices, not raw item IDs; index 0 means empty.
- Customization index 0 is a real variation; a zero `appearance` does not mean "no outfit".
- String fields are offsets, not inline text; offset 0 means null.
- DBC spell-visual/effect-name strings use `.mdx`/`.mdl`; normalize to `.m2` before archive lookup.
- `Spell.SpellVisualID` column moves across expansions (115/122/131); key off `fields`, not a fixed column.
