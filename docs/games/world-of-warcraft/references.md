# References

## Local Source

- `docs/games/world-of-warcraft/readme.md`: target status and build/run commands.
- `games/world-of-warcraft/common/world_wow.c`: WDT/ADT path setup, terrain height cache, DBC map/safe-location helpers.
- `games/world-of-warcraft/renderer/wow/r_wowmap*.c`: ADT terrain, alpha maps, splats, doodads, WMOs, draw state.
- `games/world-of-warcraft/renderer/m2/r_m2.c`: M2 loader, skin sections, animation evaluation, character composite textures.
- `games/world-of-warcraft/game/g_model.c`: M2 animation metadata parsing for game-side movement.
- `games/world-of-warcraft/game/g_wow.c`: WoW game scaffolding, ambient creatures, packed appearance/equipment use.
- `tools/m2tool.c`: M2 inspection, DBC-backed player configuration, component texture path resolution.
- `tools/README.md`: `m2tool` and WoW install examples.
- `common/shared.h`: `Wow_PackAppearance`, `Wow_UnpackAppearance`, `Wow_PackEquipment`, and `Wow_UnpackEquipment`.
- `tools/mpqtool.c`: MPQ browsing and simple BLP metadata inspection.
- `tools/blp2jpg.c`: BLP1/BLP2 texture decoding paths used by local tooling.
- `tools/blpgen.c`: deterministic BLP2 fixture generator.

## Local External Reference

- `data/whoa-master/src/component/CCharacterComponent.cpp`: component texture creation, character base texture updates, geoset visibility prep.
- `data/whoa-master/src/component/Types.hpp`: component texture sections, geoset groups, item slots, attachment IDs.
- `data/whoa-master/src/component/Util.cpp`: character section, hair, and facial-hair DBC selection helpers.
- `data/whoa-master/src/db/rec/*Rec.hpp`: DBC record layouts used by whoa-master.
- `data/whoa-master/src/model/M2Data.hpp`: M2 arrays, tracks, materials, batches, cameras, bones, and related structures.
- `data/whoa-master/src/world/map/*`: map, chunk, doodad, object, liquid, and WMO-facing scaffolding.
- `data/WoWee/src/rendering/m2_renderer_particles.cpp`: Full particle/ribbon/smoke pipeline — `emitParticles`, `updateParticles`, `updateRibbons`, `renderM2Ribbons`, `renderM2Particles`, `renderSmokeParticles`. ~800 lines, no stubs. Includes model-specific tuning: floors emission rate against particle lifespan so flame emitters (torches, braziers, lanterns) don't visually disappear when authored M2 rates are too sparse. Interpolation helpers for `M2AnimationTrack`, `M2FBlock` (scalar, vec3).

M2 particle scale curves are fractional and sampled over normalized particle life. Encode them through
`m2_particle_encode_curve`; do not cast the authored values directly to the shared MDX byte curve. Near-zero classic
scale endpoints are valid shrink targets, not corrupt-data sentinels. WoW particle blend modes 3/4 also need
`m2_particle_blend_mode` because the shared legacy particle renderer's additive enum names have opposite semantics.
Particle vertical/horizontal ranges are radians: vertical range is cone inclination from +Z and horizontal range is
azimuth. A common torch value of `2π` means full rotation around the upward axis, not a linear XY spread magnitude.
- `data/WoWee/src/rendering/m2_renderer_internal.h`: Particle/ribbon emitter GPU structs, `M2Instance` particle state (accumulators, edge buffers), per-emitter gravity caching.
- `data/WoWee/assets/shaders/m2_particle.vert.glsl`, `m2_particle.frag.glsl`: Vulkan particle billboard shaders.
- `data/WoWee/assets/shaders/m2_ribbon.vert.glsl`, `m2_ribbon.frag.glsl`: Vulkan ribbon trail shaders.

## Open-Source Client Implementations

- [whoahq/whoa](https://github.com/whoahq/whoa) — unofficial open-source WoW 3.3.5a (build 12340) client in C++11. Targets Windows 10+, macOS 10.14+, Linux. Launches against extracted client data files (no live MPQ reading).
- [Kelsidavis/WoWee](https://github.com/Kelsidavis/WoWee) — custom open-source WoW client with Warden anti-cheat (Unicorn x86), SDL2, Vulkan, GLM, StormLib, OpenGL renderer, Native Linux. Covers Vanilla, TBC, WotLK. Most active client reimplementation.
- [Reinisch/Warcraft-Arena-Unity](https://github.com/Reinisch/Warcraft-Arena-Unity) — WoW-style combat/arena engine in Unity. Built on original custom-authored content (Mage spell kit, ScriptableObject-driven spells/auras, Unity URP/Netcode/Zenject). Not a client reimplementation — spells and VFX are hand-authored rather than extracted from original M2 particle/ribbon data. Non-commercial educational fan project.

## Server Emulators

- [TrinityCore](https://github.com/TrinityCore/TrinityCore) — the most active WoW server emulator (WotLK-focused), reference implementation for spell system, combat mechanics, auras, movement.
- [AzerothCore](https://github.com/AzerothCore/AzerothCore) — modular WoW server emulator (WotLK 3.3.5a), derived from SunwellCore/TrinityCore.
- [MaNGOS](https://github.com/cmangos) — lineage of server emulators (Zero for Vanilla, One for TBC, Two for WotLK, Three for Cataclysm).
- [wowemulation-dev/wow-patcher](https://github.com/wowemulation-dev/wow-patcher) — Rust patcher that redirects vanilla WoW client login/cert-verification to private servers.
- [wowemulation-dev/warcraft-rs](https://github.com/wowemulation-dev/warcraft-rs) — Rust crates for BLP, M2, ADT, WDT, WMO parsing.

## Format References

- WoWDev wiki home: https://wowdev.wiki/
- WoWDev `WDT`: https://wowdev.wiki/WDT
- WoWDev `ADT`: https://wowdev.wiki/ADT
- WoWDev `WDL`: https://wowdev.wiki/WDL
- WoWDev `WMO`: https://wowdev.wiki/WMO
- WoWDev `M2`: https://wowdev.wiki/M2
- WoWDev `SKIN`: https://wowdev.wiki/SKIN
- WoWDev `M2/AnimationList`: https://wowdev.wiki/M2/AnimationList
- WoWDev `DBC`: https://wowdev.wiki/DBC
- WoWDev `DB2`: https://wowdev.wiki/DB2
- WoWDev `DBChanges`: https://wowdev.wiki/DBChanges
- WoWDev `WDB`: https://wowdev.wiki/WDB
- WoWDev `Lst`: https://wowdev.wiki/Lst
- WoTLK Modding Wiki `ADT/WDT/WDL`: https://wotlkdev.github.io/wiki/theory/adt
- WoTLK Modding Wiki `M2`: https://wotlkdev.github.io/wiki/theory/m2
- WoTLK Modding Wiki DBC index: https://wotlkdev.github.io/wiki/dbc/
- Warcraft Wiki `BLP files`: https://warcraft.wiki.gg/wiki/BLP_files
- Warcraft Wiki `DBC`: https://warcraft.wiki.gg/wiki/DBC
- Warcraft Wiki `CASC`: https://warcraft.wiki.gg/wiki/CASC
- Warcraft Wiki `WDB files`: https://warcraft.wiki.gg/wiki/WDB_files
- getMaNGOS WMO file: https://www.getmangos.eu/wiki/referenceinfo/clientfiles/wmo-file-r20030/
- TrinityCore `ItemDisplayInfo.dbc`: https://trinitycore.info/files/DBC/335/itemdisplayinfo
- WoTLK Modding Wiki `ItemDisplayInfo`: https://wotlkdev.github.io/wiki/dbc/ItemDisplayInfo
- getMaNGOS TBC `ItemDisplayInfo`: https://www.getmangos.eu/wiki/referenceinfo/dbcfiles/mangosonedbc/ItemDisplayInfo-r7649/
- `wow_dbc` parser crate notes: https://github.com/gtker/wow_dbc
- warcraft-rs project: https://github.com/wowemulation-dev/warcraft-rs
- warcraft-rs BLP notes: https://warcraft-rs.readthedocs.io/en/latest/formats/graphics/blp.html
- `wow-m2` parser crate: https://docs.rs/wow-m2
- `wow-adt` parser crate: https://docs.rs/wow-adt
- `wow-wdt` parser crate: https://docs.rs/wow-wdt
- `wow-wmo` parser crate: https://docs.rs/wow-wmo
- `wow-blp` parser crate: https://docs.rs/wow-blp
- `wowdev/pywowlib`: https://github.com/wowdev/pywowlib
- `wowdev/WoWDBDefs`: https://github.com/wowdev/WoWDBDefs
- `wowdev/DBCD`: https://github.com/wowdev/DBCD
- `wowdev/wow-listfile`: https://github.com/wowdev/wow-listfile
- `wow.export`: https://github.com/Kruithne/wow.export
- mangos classic M2 notes mirror: https://github-wiki-see.page/m/Marzec737/mangos-classic/wiki/M2-files
- Archival `World of Warcraft Formats` PDF mirror: https://lasatmanstanding.wordpress.com/wp-content/uploads/2010/05/wow-formats-2.pdf

## WoW to Unity Model Pipeline

- [Kruithne/wow.export](https://github.com/Kruithne/wow.export) — current extraction/export tool (successor to Marlamin's WoW Export Tools). Extracts and converts WoW client or CDN files, supports Retail and Classic, previews M2/WMO models, exports OBJ/GLTF. Includes a Blender add-on for maps/models.
- [briochie/wow.unity](https://github.com/briochie/wow.unity) — Unity-side companion to wow.export. Shaders, asset postprocessors, and tools that configure materials and parse wow.export metadata (including doodad placements) so exported models/prefabs drop into Unity scenes with ~80% in-game look fidelity (Built-In or URP).
- [Selzier/wow.export.unity](https://github.com/Selzier/wow.export.unity) — fork of wow.export retargeted for exporting directly into the Unity Editor (Unity-aware export step, alternative to post-processing with wow.unity).
- [cplushplush/unity-wow-map-importer](https://github.com/cplushplush/unity-wow-map-importer) — Unity plugin for importing map tiles (terrain/zones) exported by WoW exporters.

Note: none of these tools export M2 particle/ribbon emitter data — they produce static geometry and textures. Particle/ribbon VFX must be reimplemented by hand on the target engine side.

## Combat and Game Design References

- `docs/games/world-of-warcraft/magic-and-effects.md`: Magic schools, damage types, multi-school system, buffs/debuffs, DoT/HoT, crowd control, status effects.
- `docs/games/world-of-warcraft/enemies-and-creatures.md`: Creature types/taxonomy, classifications (normal/elite/rare/boss), difficulty tiers, aggro/threat, NPC AI roles.
- `docs/games/world-of-warcraft/weapons-and-classes.md`: Weapon types, class weapon access, combat roles (tank/healer/DPS), all class specializations, primary/secondary stats.

### External Combat References

- Warcraft Wiki — Magic schools: https://warcraft.wiki.gg/wiki/Magic_schools
- Warcraft Wiki — Creature: https://warcraft.wiki.gg/wiki/Creature
- Warcraft Wiki — Weapon: https://warcraft.wiki.gg/wiki/Weapon
- Warcraft Wiki — Class role: https://warcraft.wiki.gg/wiki/Class_role
- Warcraft Wiki — Crowd control: https://warcraft.wiki.gg/wiki/Crowd_control
- Warcraft Wiki — Debuff: https://warcraft.wiki.gg/wiki/Debuff
- Warcraft Wiki — Threat: https://warcraft.wiki.gg/wiki/Threat
- Maxroll — Magic Schools: https://maxroll.gg/wow/resources/magic-schools
- Maxroll — Stats and Attributes: https://maxroll.gg/wow/resources/stats-and-attributes
- Liquipedia — Diminishing Returns: https://liquipedia.net/worldofwarcraft/Diminishing_Returns

## Grass Rendering References

- NVIDIA GPU Gems, `Rendering Countless Blades of Waving Grass`: https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass
- NVIDIA GPU Gems 2, `Inside Geometry Instancing`: https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-3-inside-geometry-instancing
- NVIDIA GPU Gems 3, `Vegetation Procedural Animation and Shading in Crysis`: https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis
- AMD GPUOpen, `Procedural grass rendering`: https://gpuopen.com/learn/mesh_shaders/mesh_shaders-procedural_grass_rendering/
- Epic Unreal Engine documentation, `Grass Quick Start`: https://dev.epicgames.com/documentation/unreal-engine/grass-quick-start-in-unreal-engine
- getMaNGOS `GroundEffectTexture`: https://www.getmangos.eu/wiki/referenceinfo/dbcfiles/five_dbc/GroundEffectTexture-r9780/

## Caution

WoW file schemas vary by client era. Validate every DBC field offset, M2 header/view layout, and ADT chunk assumption against the data version being inspected.
