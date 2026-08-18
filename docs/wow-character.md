# WoW Character Display

## Contents

- [Defaults And Starter Outfits](#defaults-and-starter-outfits)
- [Packed Appearance Width](#packed-appearance-width)
- [Character Creation And Saved Data](#character-creation-and-saved-data)
- [Creature Character Models](#creature-character-models)
- [DBC Parsing](#dbc-parsing)
- [Skin Sections And Geosets](#skin-sections-and-geosets)
- [Component Textures](#component-textures)
- [Equipment And Actor State](#equipment-and-actor-state)
- [Diagnostic Workflow](#diagnostic-workflow)
- [Reference Links](#reference-links)

## General Rules

- Do not fix WoW character clothing, hair, or appearance bugs by hardcoding one model path, one race, one item texture set, or one group of M2 skin sections in engine code. Character appearance is data-driven by M2 skin section IDs plus DBC display records.
- For player/NPC character models, inspect `CharSections.dbc`, `CharHairGeosets.dbc`, `CharHairTextures.dbc`, `CharStartOutfit.dbc`, `ItemDisplayInfo.dbc`, and `HelmetGeosetVisData.dbc` before changing renderer policy. Local DBC files live under `DBFilesClient` in the WoW MPQs and can be inspected with `build/bin/mpqtool`.

## Defaults And Starter Outfits

Character customization and starter clothing are separate data paths:

- A packed customization value of `0` means variation/color index zero for skin, face, hair style, hair color, and facial hair. It does
  not mean "no starter outfit." `CharSections.dbc` contains valid zero-index rows for those Human Male sections in the local data.
- `Wow_PackAppearance(...)` also packs the selected class. For example, `8388608` is class 1 (Warrior) with all customization indices
  at zero. The class is required because it participates in the starter-outfit key.
- Starter clothing comes from `CharStartOutfit.dbc`, keyed by `race | (class << 8) | (gender << 16)`. The local Human Male Warrior key
  is `257`; its record contains shirt, legs, and boots display IDs including `9891`, `9892`, and `10141`.
- Each display ID resolves through `ItemDisplayInfo.dbc`. Its component texture stems are composed over the base body texture. A nude
  character with working skin/hair can therefore mean the customization path is healthy while the outfit lookup or component columns
  are wrong.

The renderer owns outfit lookup in `games/world-of-warcraft/renderer/m2/r_dbc.c`; the creation UI must pass race/gender/class and
packed customization without inventing a parallel default outfit.

## Packed Appearance Width

Keep `entityState_t.appearance` as a 32-bit snapshot field for the mounted Classic data. The verified local maxima are skin color `18`,
face `14`, hair style `18`, hair color `9`, facial feature `16`, and class `11`. Their required widths are therefore 5/4/5/4/5/4 bits;
the remaining five bits hold appearance flags. Facial-feature bit 4 occupies the unused fifth face bit, so all valid Classic values fit
without widening the network contract. Values that were already valid under the earlier layout retain the same packed number.

A 64-bit appearance would require a new delta-field type and could add four bytes whenever appearance changes in a snapshot. Do not
widen it speculatively. If mounted client data ever exceeds this verified budget, keep full menu preview customization in UI/renderer
state and send only required gameplay state, or revise the contract with measurements and an explicit protocol change.

Customization choices and their valid IDs must be enumerated from `CharSections.dbc`, `CharHairGeosets.dbc`, and
`CharacterFacialHairStyles.dbc` for the selected race/sex. Do not encode fixed option counts such as five choices; DBC counts vary by
race, sex, section, skin color, and hair style.

## Character Creation And Saved Data

The `+menu_character_create` screen initializes Human, Male, Warrior and zero-valued customization in
`games/world-of-warcraft/ui/ui_dbc.c`. `UIWow_GetCharacterCreateAppearance()` packs those values for the model drawn by
`games/world-of-warcraft/ui/ui_xml.c`.

Saved characters live in `share/characters.xml`:

```xml
<Character name="Example" race="1" sex="1" class="1" appearance="8388608" />
```

Keep the XML `class` and the class bits inside `appearance` consistent. Character select renders the packed appearance; changing only
the XML `class` label can display one class name while resolving another class's starter outfit. Useful zero-customization Human values
include Warrior `8388608`, Mage `67108864`, and Warlock `75497472`.

## Creature Character Models

`games/world-of-warcraft/serverdata/creatures.csv` owns server creature templates and model selection, not client character
customization. Each model row carries a `CreatureDisplayInfo.dbc` ID. The generated game table sends that display ID through the
existing entity `class_id` field; `client/cl_view.c` exposes it to the renderer as `renderEntity_t.display_id`.

For a display backed by a player-race M2, the renderer resolves the rest from client DBCs:

```text
creatures.csv display_id
    -> CreatureDisplayInfo.ExtendedDisplayInfoID
    -> CreatureDisplayInfoExtra skin/face/hair + NPCItemDisplay fields
    -> ItemDisplayInfo component textures/geosets
    -> composed player-race model
```

Do not copy skin, hair, or item-display configuration into `creatures.csv`. That would duplicate client-authoritative appearance data
and become wrong when a different client data version is mounted. The CSV should continue to select only the authoritative server
model/display ID; the client DBC chain applies the matching configuration.

Deputy Willem is the reference case:

- creature entry `823` selects display ID `2072` in `creatures.csv`;
- `CreatureDisplayInfo` record `2072` selects extra record `675`;
- local Classic extra record `675` packs appearance `11796676` and ten NPC item-display fields;
- the nonzero items include `14964`, `7541`, `7223`, `7224`, `7225`, `7255`, `7698`, and `6255`;
- its skin color is index `4`, making it a useful check that nonzero `CharSections` colors resolve;
- with the Classic `ItemDisplayInfo` component base at field 14, these resolve 13 component layers including Stormwind plate chest,
  pants, and boots.

A nude character-model NPC and a nude player starter outfit can share the same downstream bug. If extra appearance and item IDs
resolve but clothing does not, inspect both versioned DBC schemas before changing server data: the base skin must resolve through
`CharSections`, then the equipment components must resolve through `ItemDisplayInfo`.

## DBC Parsing

- Per-table field layouts, the WDBC container format, and the packed appearance/equipment bitfields are collected in
  [DBC Reference](games/world-of-warcraft/dbc-reference.md).
- Some classic-era DBCs have a logical field count larger than `record_size / 4`; for example local `CharStartOutfit.dbc` reports 41 fields with 152-byte records. Parse DBC records by validating the file envelope and checking each accessed field against `record_size`, not by rejecting the whole file when `field_count * 4` exceeds `record_size`.
- `CharSections.dbc` has two ten-field layouts. Classic/TBC and HD-texture Wrath store variation/color at fields 4/5 and textures at
  6–8; stock Wrath stores textures at 4–6 and variation/color at 8/9. Field count cannot distinguish them. Probe field 4 from the
  mounted data: predominantly small values (0–15) mean variation-first; string offsets (normally greater than 50) mean textures-first.
- Local Classic male hair rows (`section = 3`) select style and color but deliberately leave all texture strings empty. After an exact
  race/gender/style/color row match, resolve replaceable hair texture type 6 from the archive convention
  `Character\<Race>\Hair00_<color:02>.blp`. Derive `<Race>` from the selected model path and `<color>` from the matched DBC row; do not
  substitute a fixed race/color or leave the renderer's white initialization texture bound. Female rows generally provide explicit
  strings. `CharHairTextures.dbc` contains hair-geoset flags, not replacement-texture filenames.
- `ItemDisplayInfo.dbc` carries item model names/textures, geoset groups, flags, helmet visibility, and eight character texture component slots. In the local classic-era 23-field layout, texture components start at field 14; in the documented 25-field TBC/Wrath layout, they start at field 15. The component slots map to: upper arm, lower arm, hand, upper torso, lower torso, upper leg, lower leg, foot. See `docs/games/world-of-warcraft/m2-and-character-display.md` for the verified per-slot field→geoset-group mapping and variant tables for groups 4, 5, 8, 9, 13, and 15.
- Do not choose the component base with a broad `fields >= 23` test: that maps the local 23-field Classic schema to the later field-15
  layout and shifts all eight clothing components. Use field 14 for layouts 22–24 and field 15 for layouts 25 and later.

## Skin Sections and Geosets

- M2 skin section IDs are grouped by hundreds. Character renderers should select one variant per relevant group at draw time or through a variant cache keyed by appearance/equipment, not by throwing away sections at model-load time. Loading all batches preserves future per-entity equipment changes.
- Do not infer visible geosets from non-empty component textures. WoW keeps default character geosets (gloves, boots, ears, sleeves, legs, robe, pelvis) visible unless item geoset groups override them. The `whoa-master` component path documents defaults in `ComponentData.hpp` and applies them in `CCharacterComponent::GeosRenderPrep`.

## Component Textures

- Component texture names in `ItemDisplayInfo.dbc` are stems, not full archive paths. Resolve them under `Item\TextureComponents\<slot-folder>\` and try gender-specific suffixes (`_M`, `_F`) before universal (`_U`).
- The whoa-master character component rectangles are documented in 512×512 atlas space. Classic body skins such as `Character\Orc\Male\OrcMaleSkin00_00.blp` may be 256×256, so scale component paste rectangles to the actual destination body texture size before compositing. Otherwise all right-half slots (torso, pants, boots, feet) land outside the texture and silently disappear.

## Equipment and Actor State

- The current packed WoW `equipment` bytes are local slot item indices, not raw item IDs. Treat each byte as an index into a WoW-owned 256-entry item list selected by race, gender, and slot, with index `0` meaning empty. Keep the game state packed with `Wow_PackEquipment(...)` rather than widening entity/player state for preview gear.
- Grounded WoW actors must use the same one-dimensional yaw path as Warcraft III entities: game code writes `entityState_t.angle` in radians, the client interpolates it with `LerpRotation(...)`, and grounded M2 rendering consumes `renderEntity_t.angle`. Do not put player/creature yaw into `entityState_t.rotation`; `rotation` is reserved for static object/model transforms that genuinely need three axes.

## Diagnostic Workflow

Inspect the local schemas and starter rows before editing code:

```bash
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\CharStartOutfit.dbc'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\CharStartOutfit.dbc' 24
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\CharSections.dbc'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\CharSections.dbc' 20
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\ItemDisplayInfo.dbc'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\ItemDisplayInfo.dbc' 3
```

For a character-model creature, follow its selected display and extra records:

```bash
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\CreatureDisplayInfo.dbc' 4000 | awk '$1 == 2072'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\CreatureDisplayInfoExtra.dbc' 5000 | awk '$1 == 675'
```

Run the exact creation screen with a bounded lifetime and optional screenshot:

```bash
make openwow
build/bin/openwow -data data/world-of-warcraft +menu_character_create +screenshot +com_frame_limit 20
```

For a saved-character/class test, update both `class` and `appearance` in `share/characters.xml`, run
`+menu_character_select +screenshot +com_frame_limit 20`, then restore the save. Diagnose in this order:

1. Confirm `UIWow_GetCharacterCreateAppearance()` packs the expected class and customization.
2. Confirm `M2_DbcStartOutfit()` derives the expected race/gender/class key and finds a row.
3. Print the row's display IDs and parallel inventory types (fields 14–25 and 26–37 in local Classic data).
4. Confirm `CharSections` uses the detected variation-first or textures-first schema and resolves the selected base skin.
   For Classic male hair, an exact section-3 match with empty texture fields must derive `Character\<Race>\Hair00_<color:02>.blp`;
   verify that path with `mpqtool` before changing the selected color.
5. Confirm each display ID exists in `ItemDisplayInfo.dbc` and select the component base from the actual schema.
6. Confirm component paths resolve and the composite draw is selected.
7. Remove temporary diagnostics after the bounded run and retain a regression test for the discovered schema/behavior.

Screenshot QA must check each independently visible data path: base skin/face, hair and facial-hair color, starter clothing, and
equipment geosets. Clothing appearing correctly does not prove that replaceable texture type 6 resolved; unresolved hair renders white.

See [M2 And Character Display](games/world-of-warcraft/m2-and-character-display.md) for the full renderer pipeline and
[Rendering Scene Workflow](rendering-scene-workflow.md) for general `+menu_*` and screenshot conventions.

## Reference Links

- TrinityCore `ItemDisplayInfo.dbc` field layout: https://trinitycore.info/files/DBC/335/itemdisplayinfo
- WoTLK Modding Wiki `ItemDisplayInfo`: https://wotlkdev.github.io/wiki/dbc/ItemDisplayInfo
- getMaNGOS TBC `ItemDisplayInfo` field list: https://www.getmangos.eu/wiki/referenceinfo/dbcfiles/mangosonedbc/ItemDisplayInfo-r7649/
- `wow_dbc` parser crate notes for vanilla/TBC/Wrath DBC schemas: https://github.com/gtker/wow_dbc
