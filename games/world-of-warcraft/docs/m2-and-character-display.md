# M2 And Character Display

## M2 Loading

The WoW renderer loads M2 models through `games/world-of-warcraft/renderer/m2/r_m2.c`. It handles classic and modern-ish header/view differences enough for current experiments:

- `MD20` / `MD21` payload lookup.
- M2 arrays for sequences, bones, vertices, textures, materials, attachments, and lookup tables.
- External `00.skin` files for modern geometry.
- Legacy embedded views for older models.
- Bone matrix evaluation and per-batch matrix palettes.
- Fallback model creation when data is missing or malformed.

The game side also reads M2 sequence metadata in `games/world-of-warcraft/game/g_model.c` so entity movement can select animation names through the game module.

## Character State Packing

Appearance and equipment are packed into snapshot fields:

```c
DWORD appearance = Wow_PackAppearance(skin, face, hair_style, hair_color,
                                      facial_hair, class_id, flags);
DWORD equipment = Wow_PackEquipment(upper_body, lower_body, hands, feet);
```

`appearance` stores small race/gender/model-local customization IDs plus class. `equipment` stores local slot item indices, not raw item IDs. Index `0` means empty; nonzero indices resolve through WoW-owned equipment lists and DBC-backed `ItemDisplayInfo` display IDs.

Do not widen entity or player state just to preview more gear. Keep the packed values and resolve display data inside the WoW renderer/tool path.

## DBC-Backed Outfit Data

Character display work currently uses:

- `CharStartOutfit.dbc`
- `ItemDisplayInfo.dbc`
- `CharSections.dbc`
- `CharHairGeosets.dbc`
- `CharHairTextures.dbc`
- `HelmetGeosetVisData.dbc`

`CharStartOutfit.dbc` maps race/class/gender to starter display IDs. `ItemDisplayInfo.dbc` carries item model names/textures, geoset groups, flags, helmet visibility, and texture component stems.

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

The renderer composes outfit components onto the base body texture for character models. The composition key is derived from `entity->appearance` and `entity->equipment`.

Important constraints:

- The base body texture is not always 512x512. Classic body skins such as `Character\Orc\Male\OrcMaleSkin00_00.blp` may be 256x256.
- Component paste rectangles documented in 512x512 atlas space must scale to the actual destination body texture size.
- Do not infer visible geosets from non-empty component textures. WoW keeps many default geosets visible unless item geoset groups override them.

## Skin Section IDs And Geosets

M2 skin sections are grouped by hundreds. Character renderers should select one visible variant per relevant group at draw time or through an appearance/equipment keyed cache. Do not throw away sections at model-load time; loading all batches preserves future per-entity equipment changes.

Current default visibility in the renderer includes section IDs such as `401`, `702`, and `1501` when no outfit is available, and applies outfit flags/geoset rules when DBC data is present.

## Grounded Actor Yaw

Grounded WoW actors use the same one-dimensional yaw path as Warcraft III/OpenWarcraft3 entities:

- game code writes `entityState_t.angle` in radians,
- the client interpolates it with `LerpRotation(...)`,
- M2 rendering consumes `renderEntity_t.angle`.

Do not put player/creature yaw back into `entityState_t.rotation`; that vector is reserved for static object/model transforms that genuinely need three axes.

