# M2 And Character Display

## Contents

- [M2 Loading](#m2-loading)
- [Character State Packing](#character-state-packing)
- [Creation And Selection Data Flow](#creation-and-selection-data-flow)
- [DBC-Backed Outfit Data](#dbc-backed-outfit-data)
- [Component Texture Slots](#component-texture-slots)
- [Composed Character Texture](#composed-character-texture)
- [Skin Section IDs And Geosets](#skin-section-ids-and-geosets)
- [Grounded Actor Yaw](#grounded-actor-yaw)

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

The local Classic DBC maxima fit this 32-bit appearance contract. Face needs four bits (maximum `14`), while facial features need
five (maximum `16`), so facial-feature bit 4 uses the otherwise unused fifth face bit. Do not widen `entityState_t` merely to make menu
customization convenient; enumerate menu choices from DBC data and keep preview-only state outside snapshots if a future client format
exceeds the packed gameplay budget. See [`docs/wow-character.md`](../../wow-character.md#packed-appearance-width).

Do not widen entity or player state just to preview more gear. A menu that needs additional equipment owns that preview state and
draws the character/equipment pieces separately; persistent menu overrides do not belong in `m2Model_t` or the renderer API.

## Creation And Selection Data Flow

The creation preview and saved-character preview converge on the same renderer-owned DBC lookup:

```text
menu_dbc.c selection or ~/.local/share/world-of-warcraft/characters.xml
    -> packed appearance (customization + class)
    -> ui_xml.c renderEntity_t
    -> r_m2.c character draw
    -> r_dbc.c race/gender/class starter-outfit key
    -> CharStartOutfit display IDs + inventory types
    -> ItemDisplayInfo components/geosets
    -> composed character texture and visible M2 sections
```

Customization index zero is a real first variation, not a request for a bare body. Starter clothing is selected independently by the
race/class/gender key. Consequently, a valid base skin with no clothing should be investigated after the UI packing boundary: confirm
the starter record, display IDs, `ItemDisplayInfo` schema, and component composition in that order.

The XML save stores both `class` and packed `appearance`; keep their class values consistent. See
[`docs/wow-character.md`](../../wow-character.md) for exact local values, diagnostic commands, and bounded creation/select runs.

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
face, hair, and facial-hair fields, and applies all ten local Classic NPC
item-display slots. Creature textures follow the same direct-or-composed path
as player characters; there is no baked-texture shortcut.
Non-character creatures have no extra record and retain their ordinary M2 path.

The server CSV intentionally stops at `CreatureDisplayID`. `CreatureDisplayInfoExtra` is client-version appearance data and must not
be copied into `creatures.csv`; mounting client data and resolving the display's extra record keeps the model and its configuration
from the same authoritative version. In the local 19-field Classic schema, fields 8–17 are ten `NPCItemDisplay` values and field 18 is
the baked texture string offset, not an eleventh item.

Deputy Willem (creature `823`) is a useful end-to-end oracle: CSV display `2072` selects extra record `675`, whose appearance and item
displays resolve the Human Male model plus Stormwind plate component textures. If he is nude while this chain resolves, the failure is
downstream in `CharSections` base-skin lookup, `ItemDisplayInfo` field selection, or composition, not missing configuration in server
data. His nonzero skin color (`4`) specifically catches code that mistakes Classic's field 5 color for stock Wrath's field 9 color.

`CharSections.dbc` has two incompatible field orderings (Classic/TBC vs. stock Wrath); detect by sampling
field 4. Classic-era `ItemDisplayInfo.dbc` is 23 fields with texture components at field 14; TBC/Wrath layouts
use field 15. Full schemas and field-by-field layouts are in
[`dbc-reference.md`](dbc-reference.md#character-and-creature-tables).

Classic male hair is an explicit exception to the usual `CharSections` texture-string path. Section 3 still authoritatively selects
the race, gender, style, and color, but its texture fields are empty. Only after that exact row matches, derive replaceable texture
type 6 as `Character\<Race>\Hair00_<color:02>.blp`, taking the race from the model path and the color from the row. The default Human
Male color is therefore `Character\Human\Hair00_00.blp` (black), not the renderer's white initialization texture. Female hair rows
normally carry explicit strings. `CharHairTextures.dbc` describes hair-geoset flags and is not a filename lookup table.

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

For the complete `ItemDisplayInfo.dbc` field layout, the slot → geoset-group mapping, and per-group variant
tables (groups 4, 5, 8, 9, 12, 13, 15), see
[`dbc-reference.md — ItemDisplayInfo.dbc`](dbc-reference.md#itemdisplayinfodbc).

### Component Texture Layering

Body component textures are ordered layers. For `LegLower`, pants are priority 0 and boots are priority 2.
Composite pants first, then boots; transparent pixels in the boot texture reveal the pants texture below.
Collapsing the region to the last stem produces bare knees. Component texture presence never selects a geoset.

### Cape Texture Resolution

Read the cape texture stem from `ItemDisplayInfo.dbc` field 3, try `_M` / `_F` / `_U` suffixes, and search
under `Item\ObjectComponents\Cape\` then `Item\TextureComponents\Cape\`. Store separately from body component
textures. Activating the cape mesh (section 1502) requires setting `geoset[15] = 1` from
`outfit->cape_texture != NULL` — **not yet implemented** (TODO).

### Item Attachment Rendering

Head and shoulder items carry separate attachment M2s in `ItemDisplayInfo` fields 1–2. `M2_DbcAddDisplayInfo` stores
those stems on the outfit (`helm_model`, `shoulder_model[0..1]`); `M2_RenderItemAttachments` resolves them to archive
paths, loads them once per path, and renders them at the character's helm (attachment id 11) and shoulder (ids 5/6)
bones via `M2_AttachmentMatrix`. The parent's bone scratch is computed first, so all attachment matrices are resolved
before any recursive render overwrites it. See
[`dbc-reference.md — Item Attachment Models`](dbc-reference.md#item-attachment-models).

The renderer applies the same lifetime rule to overhead name/quest anchors. Immediately after drawing an entity, it resolves
the PlayerName attachment from the current bone palette and caches the world point under the complete pose/transform key.
The generic world-hover layout projection and later marker passes reuse that point instead of rebuilding the M2 pose. A bounded Human-start diagnostic recorded
only cache hits after scene initialization (steady one-second windows included 20 and 148 hits, with zero misses).

## Grounded Actor Yaw

Grounded WoW actors use the same one-dimensional yaw path as Warcraft III/OpenWarcraft3 entities:

- game code writes `entityState_t.angle` in radians,
- the client interpolates it with `LerpRotation(...)`,
- M2 rendering consumes `renderEntity_t.angle`.

Do not put player/creature yaw back into `entityState_t.rotation`; that vector is reserved for static object/model transforms that genuinely need three axes.
