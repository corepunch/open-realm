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

M2 skin sections are grouped by hundreds. Character renderers select one visible variant per relevant group from the current draw's
stack-local outfit. Do not throw away sections at model-load time; loading all batches preserves per-entity equipment changes.

Current default visibility in the renderer includes section IDs such as `401`, `702`, and `1501` when no outfit is available, and applies outfit flags/geoset rules when DBC data is present.

### Geoset Group Conventions (from WoWee/AzerothCore)

| Group | Purpose | Base Section | Bare Default | Notes |
|-------|---------|--------------|--------------|-------|
| 4 | Gloves | 401 | 401 (kGeosetBareForearms) | `401 + geoset` |
| 5 | Boots | 501 | 501 (kGeosetBareShins) | `501 + geoset` |
| 7 | Ears | 701 | 702 (helmet hides) | `700 + geoset` |
| 8 | Sleeves | 801 | 801 (kGeosetBareSleeves) | `801 + geoset` |
| 9 | Kneepads | 903 | 903 (narrow knees); 902 fallback | model-aware `903`, then `902` |
| 10 | Eyes | 1001 | 1001 | `1001 + geoset` |
| 11 | Eyebrows | 1101 | 1101 | `1101 + geoset` |
| 12 | Hair | 1201 | 1201 | `1201 + geoset` |
| 13 | Pants | 1301 | 1301 (kGeosetBarePants) | `1301 + geoset`; hidden by robe flag (0x4) |
| 15 | Cloak | 1501 | 1501 (kGeosetNoCape) | `1501 + geoset`; 1502 = kGeosetWithCape |
| 20 | Feet | 2002 | 2002 (kGeosetBareFeet) | Used by WoWee for bare feet mesh |

The local classic/TBC `ItemDisplayInfo.dbc` layouts provide `GeosetGroup[0..2]` in fields 7–9. The renderer stores these in `m2CharacterOutfit_t.geoset[group]` and `M2_CharacterGeosetVisible` selects the correct section variant.

For equipment, WoWee's local expansion layouts place `GeosetGroup[0..2]` at DBC fields 7–9 and item flags at field 10 (including the classic 23-field layout). `GeosetGroup[0]` is the primary mesh variant: pants use it for group 13, boots use it for group 5, and chest/shirt sleeves use it for group 8. `GeosetGroup[2]` is the robe/kilt variant for chest items. No verified equipment path changes group 9.

The shipped character models identify `903` as the narrow knee mesh and `902` as the wider mesh extending down the calf. The renderer therefore defaults group 9 to `903`, falls back to `902` only when a model lacks `903`, and otherwise resolves requested variants against the sections actually present in that race/gender model. Human, Orc, Dwarf, Night Elf, Gnome, and Troll models contain both forms; Tauren contains `903`; Scourge uses its legacy sub-400 section layout. Component texture presence never selects a clothing geoset.

Classic `CharStartOutfit.dbc` does not store one display ID per fixed equipment slot. Fields 14–25 are a display-ID array and fields 26–37 are its parallel `InventoryType` array. Resolve each display through its inventory type; values with type 0 are backpack/non-equipment entries and must not affect the character body.

Body component textures are ordered layers, not one final stem per atlas region. Follow whoa's `s_itemPriority`: for `LegLower`, pants occupy priority 0 and boots priority 2. Composite pants first, then boots; transparent pixels in the boot texture intentionally reveal the pants kneepad texture below. Collapsing the region to the last stem produces bare knees.
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
