# M2 And Character Display

## M2 Loading

The WoW renderer loads M2 models through `games/world-of-warcraft/renderer/m2/r_m2.c`. DBC ownership and character-data
resolution live separately in `games/world-of-warcraft/renderer/m2/r_dbc.c`. The M2 loader handles:

- `MD20` / `MD21` payload lookup.
- M2 arrays for sequences, bones, vertices, textures, materials, attachments, and lookup tables.
- External `00.skin` files for modern geometry.
- Legacy embedded views for older models.
- Bone matrix evaluation and per-batch matrix palettes.
- Fallback model creation when data is missing or malformed.

The game side also reads M2 sequence metadata in `games/world-of-warcraft/game/g_model.c` so entity movement can select animation names through the game module.

`m2Model_t` owns the `FS_ReadFile` image directly and stores a typed `m2File_t` view plus `base_offset`; MD21/12DM containers point
the view into their payload without copying it. The version is read before selecting and validating the classic or modern header.
Header array descriptors remain file offsets and are resolved through bounds-checked accessors when consumed; the loader does not
copy file arrays or duplicate their counts into runtime fields. The model adds only renderer resources absent from the file: draw
batches, bounds, and flags. Batch index/vertex indirection is prevalidated once before the tight vertex upload loop. Bone matrices use module-static frame scratch like MDX;
particles and ribbons emit into the renderer's global particle pool from the shared render clock, so neither needs per-model state.

Generic renderer model registration lives in `renderer/r_model.c`. It is the only model cache: case-insensitive filename lookup returns
the resident model or cached missing-model placeholder, reference release leaves the image resident, and registration-sequence cleanup
reclaims unreferenced models not touched by the current map registration. Textures remain separately registered renderer resources.

## Character State Packing

Appearance and equipment are packed into snapshot fields:

```c
DWORD appearance = Wow_PackAppearance(skin, face, hair_style, hair_color,
                                      facial_hair, class_id, flags);
DWORD equipment = Wow_PackEquipment(upper_body, lower_body, hands, feet);
```

`appearance` stores small race/gender/model-local customization IDs plus class. `equipment` stores local slot item indices, not raw item IDs. Index `0` means empty; nonzero indices resolve through WoW-owned equipment lists and DBC-backed `ItemDisplayInfo` display IDs.

Do not widen entity or player state just to preview more gear. A menu that needs additional equipment owns that preview state and
draws the character/equipment pieces separately; persistent menu overrides do not belong in `m2Model_t` or the renderer API.

## DBC-Backed Outfit Data

`r_dbc.c` owns each resident WDBC image and its immutable open-addressed index. FNV-1a32
hashes the lookup key and each slot stores one `int` source record number, not a
copied outfit or texture. Index capacity is a power of two sized at least
twice the record count, so outfit resolution can use stack-local state without
an appearance-result cache. `r_m2.c` consumes only resolved appearance, item-display IDs,
outfits, and texture paths; it never accesses DBC records directly.

Character display work currently uses:

- `CreatureDisplayInfo.dbc`
- `CreatureDisplayInfoExtra.dbc`
- `CharStartOutfit.dbc`
- `ItemDisplayInfo.dbc`
- `CharSections.dbc`
- `CharHairGeosets.dbc`
- `CharHairTextures.dbc`
- `HelmetGeosetVisData.dbc`

`CharStartOutfit.dbc` maps race/class/gender to starter display IDs. `ItemDisplayInfo.dbc` carries item model names/textures, geoset groups, flags, helmet visibility, and texture component stems.

Character-model NPCs keep their AzerothCore `CreatureDisplayID` in the existing
snapshot `class_id` field. The renderer follows that ID through
`CreatureDisplayInfo.ExtendedDisplayInfoID`, packs the extra record's skin,
face, hair, and facial-hair fields, and applies all nine classic NPC
item-display slots. Creature textures follow the same direct-or-composed path
as player characters; there is no baked-texture shortcut.
Non-character creatures have no extra record and retain their ordinary M2 path.

Classic-era local data has a 23-field `ItemDisplayInfo.dbc` layout where texture components start at field 14. Documented TBC/Wrath-style 25-field layouts start components at field 15. Code should pick offsets from the actual field layout and validate each access against `record_size`.

## Component Texture Slots

Item component texture stems map to eight body slots:

| Slot | Folder |
| --- | --- |
| Upper arm | `Item\TextureComponents\ArmUpperTexture\` |
| Lower arm | `Item\TextureComponents\ArmLowerTexture\` |
| Hand | `Item\TextureComponents\HandTexture\` |
| Upper torso | `Item\TextureComponents\TorsoUpperTexture\` |
| Lower torso | `Item\TextureComponents\TorsoLowerTexture\` |
| Upper leg | `Item\TextureComponents\LegUpperTexture\` |
| Lower leg | `Item\TextureComponents\LegLowerTexture\` |
| Foot | `Item\TextureComponents\FootTexture\` |

Component names in `ItemDisplayInfo.dbc` are stems, not archive paths. Resolve them under the slot folder and try gender-specific suffixes first:

```text
<stem>_M.blp
<stem>_F.blp
<stem>_U.blp
```

Use the gender suffix that matches the character model, then universal as fallback.

## Composed Character Texture

The renderer uses two paths for character body textures:

- An unmodified body renders its filename-cached source texture directly.
- A modified body is composed on the GPU into one shared temporary atlas, then
  rendered immediately with that atlas. The temporary target is
  `M2_CHARACTER_COMPOSITE_RESOLUTION` (256x256).

The temporary atlas is reused sequentially; it is valid only until the current
model draw finishes. It is not a per-appearance cache and must not be retained
by an entity or model.

Important constraints:

- The base body texture is not always 512x512. Classic body skins such as `Character\Orc\Male\OrcMaleSkin00_00.blp` may be 256x256.
- Component rectangles are authored in 512x512 atlas space and scale to the 256x256 temporary target.
- Do not infer visible geosets from non-empty component textures. WoW keeps many default geosets visible unless item geoset groups override them.

## Skin Section IDs And Geosets

M2 skin sections are grouped by hundreds: `section_id = group * 100 + variant`. Character renderers select one visible variant per relevant group from the current draw's stack-local outfit. Do not throw away sections at model-load time; loading all batches preserves per-entity equipment changes.

When no outfit is available the bare defaults are sections `401` (forearms), `702` (ears), and `1501` (no-cape back).

### ItemDisplayInfo.dbc Field Layout (classic 23-field)

| Field | Content |
|-------|---------|
| 0 | Record ID |
| 1–2 | Model name string offsets (male, female) |
| 3 | Cape texture string offset |
| 4–6 | HelmGeosetVisData[0–2] |
| **7** | **GeosetGroup[0]** — primary mesh variant |
| **8** | **GeosetGroup[1]** — secondary mesh variant |
| **9** | **GeosetGroup[2]** — robe/kilt leg variant |
| 10 | Flags (bit 0 = helm hides hair; bit 2 = kneelength/robe hides pants) |
| 11 | SpellVisualID |
| 12–13 | Additional IDs |
| 14–21 | Eight component texture string offsets (upper arm → foot) |
| 22 | Extra field |

### Slot → Geoset Group Mapping (`slot_geoset_group_map`)

For each equipment slot, the three DBC GeosetGroup fields drive these character geoset groups. All findings verified against the full 23 852-record classic DBC.

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

### Geoset Group Variant Tables

All variants verified by exhaustive DBC scan. `section = group * 100 + variant` throughout.

#### Group 4 — Gloves (`401 + geoset`, driven by legs field 7)

| geoset | section | mesh |
|--------|---------|------|
| 0 | 401 | bare forearms — default, texture painted on |
| 1 | 402 | glove mesh variant 1 |
| 2 | 403 | glove mesh variant 2 |
| 3 | 404 | glove mesh variant 3 |

**Classic DBC:** all 23 852 glove-slot items have field 7 = 0. Gloves are texture-only in Classic; sections 402–404 exist in models but are unreachable through `ItemDisplayInfo`.

#### Group 5 — Boots (`Wow_CharacterGeosetPick(501 + geoset, fallback 501)`, driven by boots field 7)

| geoset | section | mesh |
|--------|---------|------|
| 0 | 501 | bare shins — default, texture painted on |
| 1 | 502 | boot mesh variant 1 |
| 2 | 503 | boot mesh variant 2 |
| 3 | 504 | boot mesh variant 3 |

Fallback scan used because some boot mesh variants are absent from certain race/gender models. **Classic DBC:** all boot-slot items (including hardcoded display 27270) have field 7 = 0. Boots are texture-only in Classic.

#### Group 8 — Sleeves (`801 + geoset`, driven by chest/shirt field 7)

| geoset | section | mesh |
|--------|---------|------|
| 0 | 801 | bare forearms — default |
| 1 | 802 | short sleeve mesh |
| 2 | 803 | long sleeve mesh |

Classic chest items with visible sleeves set field 7 = 1 (288 records). Shirt items (slot 4) also drive this group via field 7.

#### Group 9 — Kneepads (`900 + geoset`, driven by legs field 8)

| geoset | section | mesh |
|--------|---------|------|
| 0 | 900 | DNE — no kneepads (default) |
| 1 | 901 | DNE — no kneepads (explicit none) |
| 2 | 902 | long kneepads |
| 3 | 903 | short kneepads |

**Important:** variant 0 and variant 1 are both DNE. `900 + geoset` is correct; `901 + geoset` would mis-map variant 2 → 903 (short instead of long).

**Classic DBC:** 809 legs items set field 8 = 1 (→ 901 DNE = no kneepads). Zero items set field 8 = 2 or 3. Sections 902/903 exist in all shipped human/orc/etc. models but no Classic `ItemDisplayInfo` record activates them. The renderer default `geoset = 0 → 900 (DNE)` is therefore correct for all Classic content.

#### Group 13 — Pants (`Wow_CharacterGeosetPick(1301 + geoset, fallback 1301)`, driven by legs field 7 or chest field 9)

| geoset | section | mesh |
|--------|---------|------|
| 0 | 1301 | short pants — default/fallback |
| 1 | 1302 | standard trousers |
| 2 | 1303 | robe/full-length leg coverage |

Fallback scan used because some pants variants are absent from certain race/gender models. Hidden entirely when `M2_CHAR_FLAG_KNEELENGTH` (DBC field 10 bit 2) is set.

**Classic DBC:** 288 legs items set field 7 = 1 (→ 1302); 21 set field 7 = 2 (→ 1303). Robe chest items (InvType 20) drive this group via field 9 (e.g. display 12646, Orc Warlock robe: field 9 = 2 → 1303). Starter pants such as display 9892 have field 7 = 0, falling back to section 1301.

#### Group 15 — Cape (`1501 + geoset`, driven by cape field 7)

| geoset | section | mesh |
|--------|---------|------|
| 0 | 1501 | no-cape bare back — default |
| 1 | 1502 | cape geometry |

**Classic DBC:** all 7 701 cape records have field 7 = 0. `geoset[15]` is never set from `ItemDisplayInfo`. Cape geometry (section 1502) must be enabled by deriving `geoset[15] = 1` from `outfit->cape_texture != NULL` in `M2_DbcAddDisplayInfo` — **not yet implemented** (TODO).

### Classic `CharStartOutfit.dbc`

Fields 14–25 are a display-ID array; fields 26–37 are the parallel `InventoryType` array. Resolve each display through its inventory type; entries with type 0 are non-equipment and must not affect the character body. No starter record includes InvType 10 (gloves) or InvType 16 (cape).

### Component Texture Layering

Body component textures are ordered layers. Follow the `s_itemPriority` table: for `LegLower`, pants are priority 0 and boots are priority 2. Composite pants first, then boots; transparent pixels in the boot texture reveal the pants texture below. Collapsing the region to the last stem produces bare knees. Component texture presence never selects a clothing geoset.
### Cape Texture Resolution

Cape textures are stored in `ItemDisplayInfo.dbc` field 3 (LeftModelTexture). Resolution follows WoWee's pattern:

1. Read texture stem from field 3
2. Try gender-suffix variants: `{stem}_M.blp`, `{stem}_F.blp`, `{stem}_U.blp`
3. Try under `Item\ObjectComponents\Cape\` and `Item\TextureComponents\Cape\`
4. Store separately from the layered body component textures

The cape geoset (group 15) must be set to 1502 (kGeosetWithCape) for the cloak mesh to render.

## Grounded Actor Yaw

Grounded WoW actors use the same one-dimensional yaw path as Warcraft III/OpenWarcraft3 entities:

- game code writes `entityState_t.angle` in radians,
- the client interpolates it with `LerpRotation(...)`,
- M2 rendering consumes `renderEntity_t.angle`.

Do not put player/creature yaw back into `entityState_t.rotation`; that vector is reserved for static object/model transforms that genuinely need three axes.
