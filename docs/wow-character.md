# WoW Character Display

## General Rules

- Do not fix WoW character clothing, hair, or appearance bugs by hardcoding one model path, one race, one item texture set, or one group of M2 skin sections in engine code. Character appearance is data-driven by M2 skin section IDs plus DBC display records.
- For player/NPC character models, inspect `CharSections.dbc`, `CharHairGeosets.dbc`, `CharHairTextures.dbc`, `CharStartOutfit.dbc`, `ItemDisplayInfo.dbc`, and `HelmetGeosetVisData.dbc` before changing renderer policy. Local DBC files live under `DBFilesClient` in the WoW MPQs and can be inspected with `build/bin/mpqtool`.

## DBC Parsing

- Some classic-era DBCs have a logical field count larger than `record_size / 4`; for example local `CharStartOutfit.dbc` reports 41 fields with 152-byte records. Parse DBC records by validating the file envelope and checking each accessed field against `record_size`, not by rejecting the whole file when `field_count * 4` exceeds `record_size`.
- `ItemDisplayInfo.dbc` carries item model names/textures, geoset groups, flags, helmet visibility, and eight character texture component slots. In the local classic-era 23-field layout, texture components start at field 14; in the documented 25-field TBC/Wrath layout, they start at field 15. The component slots map to: upper arm, lower arm, hand, upper torso, lower torso, upper leg, lower leg, foot.

## Skin Sections and Geosets

- M2 skin section IDs are grouped by hundreds. Character renderers should select one variant per relevant group at draw time or through a variant cache keyed by appearance/equipment, not by throwing away sections at model-load time. Loading all batches preserves future per-entity equipment changes.
- Do not infer visible geosets from non-empty component textures. WoW keeps default character geosets (gloves, boots, ears, sleeves, legs, robe, pelvis) visible unless item geoset groups override them. The `whoa-master` component path documents defaults in `ComponentData.hpp` and applies them in `CCharacterComponent::GeosRenderPrep`.

## Component Textures

- Component texture names in `ItemDisplayInfo.dbc` are stems, not full archive paths. Resolve them under `Item\TextureComponents\<slot-folder>\` and try gender-specific suffixes (`_M`, `_F`) before universal (`_U`).
- The whoa-master character component rectangles are documented in 512×512 atlas space. Classic body skins such as `Character\Orc\Male\OrcMaleSkin00_00.blp` may be 256×256, so scale component paste rectangles to the actual destination body texture size before compositing. Otherwise all right-half slots (torso, pants, boots, feet) land outside the texture and silently disappear.

## Equipment and Actor State

- The current packed WoW `equipment` bytes are local slot item indices, not raw item IDs. Treat each byte as an index into a WoW-owned 256-entry item list selected by race, gender, and slot, with index `0` meaning empty. Keep the game state packed with `Wow_PackEquipment(...)` rather than widening entity/player state for preview gear.
- Grounded WoW actors must use the same one-dimensional yaw path as Warcraft III entities: game code writes `entityState_t.angle` in radians, the client interpolates it with `LerpRotation(...)`, and grounded M2 rendering consumes `renderEntity_t.angle`. Do not put player/creature yaw into `entityState_t.rotation`; `rotation` is reserved for static object/model transforms that genuinely need three axes.

## Reference Links

### DBC Field Layouts

- TrinityCore `ItemDisplayInfo.dbc` field layout: https://trinitycore.info/files/DBC/335/itemdisplayinfo
- WoTLK Modding Wiki `ItemDisplayInfo`: https://wotlkdev.github.io/wiki/dbc/ItemDisplayInfo
- getMaNGOS TBC `ItemDisplayInfo` field list: https://www.getmangos.eu/wiki/referenceinfo/dbcfiles/mangosonedbc/ItemDisplayInfo-r7649/
- `wow_dbc` parser crate notes for vanilla/TBC/Wrath DBC schemas: https://github.com/gtker/wow_dbc

### WoW Client References

- WoWee (C++ WoW client, Vulkan renderer, Vanilla/TBC/WotLK): https://github.com/Kelsidavis/WoWee
	— Full protocol client with M2/WMO/ADT rendering, skeletal animation, particle emitters, shadow mapping, SRP6a auth. Reference for: M2 animation state machine, ADT terrain streaming, WMO rendering, particle/aura effects, character geoset selection.
- OpenWow (C++ WoW 1.12 open source client): https://github.com/World0fWarcraft/OpenWow
	— Reference for: Vanilla protocol, M2/WMO/ADT loading pipeline.
- Wowser (WoW in the browser, JS/WebGL, WotLK 3.3.5a): https://github.com/wowserhq/wowser
	— Reference for: auth/realm/world packet flow, asset pipeline from MPQ to WebGL.

### WoW Tools and Libraries

- Warcraft Arena Unity (WoW arena combat system, Unity + Photon Bolt): https://github.com/Reinisch/Warcraft-Arena-Unity
	— Reference for: spell system architecture (auras, effects, periodic damage/heal, absorb, dispel, CC types), unit frame layout, action bar keybinding, arena map setup.
- Everlook (cross-platform WoW model viewer, C#/OpenTK/GTK): https://github.com/WowDevTools/Everlook
	— Reference for: M2/WMO/BLP/ADT/MPQ browsing and export pipeline, OpenGL rendering of WoW formats.
- libwarcraft (C# all-in-one WoW format library, Vanilla–WotLK): https://github.com/WowDevTools/libwarcraft
	— Reference for: BLP, MPQ, DBC, MDX/M2, WDT, WDL, WMO, ADT read/write implementations and field layouts.
- warcraft-rs (Rust CLI + crates for WoW file formats, 1.12–5.4.8): https://github.com/wowemulation-dev/warcraft-rs
	— Reference for: wow-mpq, wow-blp, wow-adt crate APIs; cross-version format differences.

### WoW Terrain/World Tools

- WoWmapview (original 3D WoW terrain viewer, C++): https://wowmapview.sourceforge.net/
	— Foundational ADT/WMO/M2 reverse-engineering reference; ancestor of Noggit and most format knowledge.
- Noggit3 (WoW 3.3.5a terrain editor, C++/Qt, Lua-scriptable): https://github.com/wowdev/noggit3
	— Reference for: ADT terrain read/write, WMO placement, M2 doodad placement, chunk-level map editing.
