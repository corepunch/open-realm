# OpenWarcraft Agent Guide

## Project Context

This codebase is inspired by **Quake 2** (id Software). The developer is deeply familiar with Quake 2's architecture and source code. **Quake 2 is the primary reference** for all lifecycle, state communication, UI control, movement, and entity patterns. Use **Quake 3** as a secondary reference for features Q2 lacks, such as client-side UI libraries or renderer module separation.

## Further Reading

| Topic | File |
|-------|------|
| Architecture, engine boundaries, struct/API discipline, network contracts | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Client camera samples, Euler snapshot, quat slerp | [docs/architecture/client.md](docs/architecture/client.md) |
| Server-selected presentation effects and generic effect contracts | [docs/architecture/server-selected-effects.md](docs/architecture/server-selected-effects.md) |
| Environment lighting samples, day-phase stat, per-game fill hook | [docs/architecture/environment-lighting.md](docs/architecture/environment-lighting.md) |
| Native game coordinate systems and axis migration | [AXIS.md](AXIS.md) |
| Server-authored UI payload design, limits, diagnostics, scrollbar postmortem | [docs/architecture/ui-payloads.md](docs/architecture/ui-payloads.md) |
| Client-managed gameplay windows, focus, z-order, dragging, modal input, text arenas | [docs/architecture/client-windows.md](docs/architecture/client-windows.md) |
| Test discipline, build & linking rules, MPQ fixture rules | [CONTRIBUTING.md](CONTRIBUTING.md) |
| Agent documentation capture, placement, templates, and indexing | [docs/documentation-guide.md](docs/documentation-guide.md) |
| Diagnostic tools (mpqtool, dbctool, mdxtool, ability_audit, profiler) | [docs/diagnostic-tools.md](docs/diagnostic-tools.md) |
| Vendored Lua 5.4 and the libxml2-free XML parser (`common/tinyxml.h`) | [docs/vendored-dependencies.md](docs/vendored-dependencies.md) |
| UI screen authoring, FDF conventions, ConsoleUI, stb_fdf.h | [docs/ui-authoring.md](docs/ui-authoring.md) |
| Test-suite performance measurement and parallel execution | [docs/test-performance.md](docs/test-performance.md) |
| WoW character display, DBC/skin-section/component-texture rules | [docs/wow-character.md](docs/wow-character.md) |
| WoW grass: active paths, wind-sway math, phase decorrelation, density formula, constants | [docs/games/world-of-warcraft/GRASS_TECH.md](docs/games/world-of-warcraft/GRASS_TECH.md) |
| WoW magic schools, damage types, buffs/debuffs, CC, status effects | [docs/games/world-of-warcraft/magic-and-effects.md](docs/games/world-of-warcraft/magic-and-effects.md) |
| WoW creature types, classifications, difficulty, aggro/threat; entity architecture (think-fn dispatch, spell table, spawn budget) | [docs/games/world-of-warcraft/enemies-and-creatures.md](docs/games/world-of-warcraft/enemies-and-creatures.md) |
| WoW spawn system, WorldSafeLocs DBC, per-race selection, server→client game commands | [docs/games/world-of-warcraft/spawn-and-teleport.md](docs/games/world-of-warcraft/spawn-and-teleport.md) |
| WoW area triggers, dungeon/instance map loading, warp command, pending teleport mechanism | [docs/games/world-of-warcraft/area-triggers.md](docs/games/world-of-warcraft/area-triggers.md) |
| WoW first-login race cinematics, DBC chain, M2 camera playback lifecycle | [docs/games/world-of-warcraft/cinematics.md](docs/games/world-of-warcraft/cinematics.md) |
| WoW FrameXML loading, inheritance, anchors, natural text sizing, unresolved geometry | [docs/games/world-of-warcraft/framexml-layout.md](docs/games/world-of-warcraft/framexml-layout.md) |
| WoW quest system, server-authored dialog, AzerothCore SQL extraction, quest commands | [docs/games/world-of-warcraft/quest-ui.md](docs/games/world-of-warcraft/quest-ui.md) |
| WoW weapons, classes, combat roles, specializations | [docs/games/world-of-warcraft/weapons-and-classes.md](docs/games/world-of-warcraft/weapons-and-classes.md) |
| WoW gameplay features: implemented vs missing (WoWee gap analysis), loot system details | [docs/games/world-of-warcraft/gameplay-features.md](docs/games/world-of-warcraft/gameplay-features.md) |
| Entity sound architecture | [docs/architecture/sound.md](docs/architecture/sound.md) |
| WC3 background music, `Music.slk`/skin lookup, `svc_music`, optional FFmpeg streaming | [docs/games/warcraft-3/music.md](docs/games/warcraft-3/music.md) |
| WC3 data model (SLK, unit stats, combat) | [docs/wc3-data-model.md](docs/wc3-data-model.md) |
| WC3 attack damage math, runtime modifiers, armor/type multipliers, projectile impact timing | [docs/games/warcraft-3/attack-damage.md](docs/games/warcraft-3/attack-damage.md) |
| WC3 JASS native coverage, callback contracts, state ownership | [docs/games/warcraft-3/jass-native-coverage.md](docs/games/warcraft-3/jass-native-coverage.md) |
| WC3 timer-dialog mission countdowns, stock FDF HUD, local visibility, save/load identity | [docs/games/warcraft-3/timer-dialogs.md](docs/games/warcraft-3/timer-dialogs.md) |
| WC3 leaderboards, counted-objective HUDs, per-player assignment, save/load identity | [docs/games/warcraft-3/leaderboards.md](docs/games/warcraft-3/leaderboards.md) |
| WC3 campaign map bulk runtime audit, per-map reports, commands | [docs/games/warcraft-3/map-audit.md](docs/games/warcraft-3/map-audit.md) |
| WC3 JASS group handle lifecycle, DestroyGroup slot reuse, save/load identity | [docs/games/warcraft-3/jass-groups.md](docs/games/warcraft-3/jass-groups.md) |
| WC3 campaign game cache, persisted Hero progression, `StoreUnit`/`RestoreUnit` | [docs/games/warcraft-3/campaign-game-cache.md](docs/games/warcraft-3/campaign-game-cache.md) |
| WC3 campaign/mission unlock persistence, retail `.w3p` distinction, selector visibility | [docs/games/warcraft-3/campaign-progress.md](docs/games/warcraft-3/campaign-progress.md) |
| WC3 game save/load format, entity pointer fixups, `F_CFUNCTION` C callbacks, and `field_t` synchronization | [docs/games/warcraft-3/save-load.md](docs/games/warcraft-3/save-load.md) |
| WC3 HUD texture/font indices vs names across `SV_Map` / save-load | [docs/games/warcraft-3/hud-media.md](docs/games/warcraft-3/hud-media.md) |
| WC3 fog states, scripted reveals, fog modifiers, shared vision, cinematic separation | [docs/games/warcraft-3/fog-and-cinematics.md](docs/games/warcraft-3/fog-and-cinematics.md) |
| WC3 environmental terrain fog / distance mist, `SetTerrainFogEx`, `DefaultZFog`, renderer handoff | [docs/games/warcraft-3/environmental-fog.md](docs/games/warcraft-3/environmental-fog.md) |
| WC3 simulation time of day, Dawn/Dusk data, JASS game state, sight/regen consumers | [docs/games/warcraft-3/time-of-day.md](docs/games/warcraft-3/time-of-day.md) |
| WC3 natural neutral-creep sleep, `canSleep`, sleep natives, wake rules, and known camp gaps | [docs/games/warcraft-3/creep-sleep.md](docs/games/warcraft-3/creep-sleep.md) |
| WC3 regeneration auras and Fountain health/mana alias/data flow | [docs/games/warcraft-3/regeneration-auras.md](docs/games/warcraft-3/regeneration-auras.md) |
| WC3 Barkskin modal autocast, authored armor status, expiry/death inverse | [docs/games/warcraft-3/barkskin.md](docs/games/warcraft-3/barkskin.md) |
| WC3 camera viewport/bounds, cinematic camera state, world-overlay clipping | [docs/games/warcraft-3/cinematics.md](docs/games/warcraft-3/cinematics.md) |
| WC3 triggered dialogue, gameplay/cinematic presentation split, message and transmission lifetimes | [docs/games/warcraft-3/triggered-dialogue.md](docs/games/warcraft-3/triggered-dialogue.md) |
| WC3 pre-rendered movies, optional FFmpeg backend, `PlayCinematic` lifecycle, campaign camera rows | [docs/games/warcraft-3/pre-rendered-movies.md](docs/games/warcraft-3/pre-rendered-movies.md) |
| WC3 W3I/W3R/JASS weather lifecycle, Weather.slk particle rendering, rain tails | [docs/games/warcraft-3/weather.md](docs/games/warcraft-3/weather.md) |
| WC3 alerts, minimap pings, eight-entry Spacebar recent-alert history, quick-position fallback | [docs/games/warcraft-3/alerts-and-minimap-pings.md](docs/games/warcraft-3/alerts-and-minimap-pings.md) |
| WC3 Quest journal, single-player Message Log, upper-button wiring, retained message history | [docs/games/warcraft-3/quest-and-message-log-ui.md](docs/games/warcraft-3/quest-and-message-log-ui.md) |
| Developer cheat commands, WC3 quest/trigger/objective/JASS/cinematic debugging and in-game console feedback | [docs/cheat-commands.md](docs/cheat-commands.md) |
| WC3 in-game Menu/F10 overlay, EscMenu panel flow, modal pause/input, leave/exit actions | [docs/games/warcraft-3/in-game-menu.md](docs/games/warcraft-3/in-game-menu.md) |
| WC3 Allies/F11 dialog, directional alliances, shared vision/control, draft/Accept semantics | [docs/games/warcraft-3/allies-menu.md](docs/games/warcraft-3/allies-menu.md) |
| WC3 Hero skill tree, skill points, rank requirements, learning UI, JASS progression | [docs/games/warcraft-3/hero-abilities.md](docs/games/warcraft-3/hero-abilities.md) |
| WC3 gathering, immobile units, construction HUD, overhead bars | [docs/games/warcraft-3/economy-and-unit-presentation.md](docs/games/warcraft-3/economy-and-unit-presentation.md) |
| WC3 neutral item shops, patrons, item stock/restock, purchase and pawn flow | [docs/games/warcraft-3/neutral-shops.md](docs/games/warcraft-3/neutral-shops.md) |
| WC3 Orc Burrow cargo, Load/Stand Down, occupant state, Burrow attack gating/scaling | [docs/games/warcraft-3/orc-burrows.md](docs/games/warcraft-3/orc-burrows.md) |
| WC3 gold/lumber gain transactions and world-space `+N` presentation | [docs/games/warcraft-3/resource-gain-text.md](docs/games/warcraft-3/resource-gain-text.md) |
| WC3 Human Call to Arms, Peasant/Militia pairing, timed in-place transform, Back to Work | [docs/games/warcraft-3/call-to-arms-and-militia.md](docs/games/warcraft-3/call-to-arms-and-militia.md) |
| WC3 Polymorph (`Aply`), Ply1-Ply5 data, reversible morph presentation, dispel/JASS/save behavior | [docs/games/warcraft-3/polymorph.md](docs/games/warcraft-3/polymorph.md) |
| WC3 selected-unit timed-status countdown bar, Bmil/BTLF eligibility, HUD data flow | [docs/games/warcraft-3/timed-status-presentation.md](docs/games/warcraft-3/timed-status-presentation.md) |
| WC3 `SetUnitVertexColor`, per-frame tint transport, explicit alpha-zero semantics | [docs/games/warcraft-3/unit-vertex-color.md](docs/games/warcraft-3/unit-vertex-color.md) |
| WC3 enemy/neutral selection, relationship colours, command authority, fog/death deselection | [docs/games/warcraft-3/selection-and-control.md](docs/games/warcraft-3/selection-and-control.md) |
| Client numbered control groups (`cl.groups`), WC3/SC2 binds, double-tap camera focus | [docs/games/warcraft-3/control-groups.md](docs/games/warcraft-3/control-groups.md) |
| WC3 Shift command queuing, per-unit FIFO orders, target revalidation, replacement/Stop semantics | [docs/games/warcraft-3/order-queue.md](docs/games/warcraft-3/order-queue.md) |
| WC3 point-order confirmation markers, transient feedback, support-surface grounding | [docs/games/warcraft-3/command-feedback.md](docs/games/warcraft-3/command-feedback.md) |
| WC3 issued target/point-order JASS events, order/target/point callback context, campaign tutorial compatibility | [docs/games/warcraft-3/issued-target-order-events.md](docs/games/warcraft-3/issued-target-order-events.md) |
| WC3 persistent Hero and idle-worker HUD shortcuts, event-driven invalidation, F1-F8 cycling | [docs/games/warcraft-3/unit-shortcuts.md](docs/games/warcraft-3/unit-shortcuts.md) |
| WC3 building menu, placement validation, Human construction and power building | [docs/games/warcraft-3/building-construction.md](docs/games/warcraft-3/building-construction.md) |
| WC3 race-specific construction strategies and remaining Human/Orc/Undead/Night Elf mechanic gaps | [docs/games/warcraft-3/race-mechanics.md](docs/games/warcraft-3/race-mechanics.md) |
| WC3 research queues, upgrade costs/requirements, player tech state, Blacksmith attack/armor effects | [docs/games/warcraft-3/research-and-upgrades.md](docs/games/warcraft-3/research-and-upgrades.md) |
| WC3 building damage fire thresholds, MDX Sprite attachments, race-specific effect families | [docs/games/warcraft-3/building-damage-rendering.md](docs/games/warcraft-3/building-damage-rendering.md) |
| WC3 area-based JASS doodad animation, show/hide, looping and non-looping sequence lifecycle | [docs/games/warcraft-3/doodad-animation.md](docs/games/warcraft-3/doodad-animation.md) |
| WC3 Build/Repair blocked-footprint approach routing | [docs/games/warcraft-3/build-repair-routing.md](docs/games/warcraft-3/build-repair-routing.md) |
| WC3 generic autocast hooks, command-button right click, Auto Repair nearest-valid acquisition | [docs/games/warcraft-3/autocast.md](docs/games/warcraft-3/autocast.md) |
| WC3 food/supply ownership, training reservations, provider lifecycle, upkeep income | [docs/games/warcraft-3/food-and-upkeep.md](docs/games/warcraft-3/food-and-upkeep.md) |
| WC3 Hero death persistence and Altar revival lifecycle | [docs/games/warcraft-3/hero-revival.md](docs/games/warcraft-3/hero-revival.md) |
| WC3 Rally producer state, Smart handoff, target lifetime, JASS getters | [docs/games/warcraft-3/rally-points.md](docs/games/warcraft-3/rally-points.md) |
| WC3 pathfinding, flow fields, collision-sized routing, unreachable lumber targets | [docs/games/warcraft-3/pathfinding.md](docs/games/warcraft-3/pathfinding.md) |
| WC3 unit altitude, `moveHeight`, water/bridge support surfaces, fly-height natives, projectile impact Z | [docs/games/warcraft-3/unit-altitude.md](docs/games/warcraft-3/unit-altitude.md) |
| WC3 Required Animation Names (`animProps`/`uani`), alternate forms, `AddUnitAnimationProperties`, tagged MDX sequence selection | [docs/games/warcraft-3/unit-animation-properties.md](docs/games/warcraft-3/unit-animation-properties.md) |
| WC3 resource-worker crowd routing and Human02 30-Peasant simulation | [docs/games/warcraft-3/worker-crowd-routing.md](docs/games/warcraft-3/worker-crowd-routing.md) |
| WC3 inventory, world-item lifecycle, item UI presentation | [docs/games/warcraft-3/inventory-and-items.md](docs/games/warcraft-3/inventory-and-items.md) |
| WC3 ability/item presentation art, effect edict lifetimes, JASS effect handles, consumable charge semantics | [docs/games/warcraft-3/ability-and-item-effects.md](docs/games/warcraft-3/ability-and-item-effects.md) |
| WC3 ability implementation from data, behavior, and focused evidence | [docs/games/warcraft-3/ability-implementation-plan.md](docs/games/warcraft-3/ability-implementation-plan.md) |
| WC3 Anti-Magic Shell Bams immunity vs Bam2 DataC absorption, Aam2/ACam aliases | [docs/games/warcraft-3/anti-magic-shell.md](docs/games/warcraft-3/anti-magic-shell.md) |
| WC3 Cyclone organic-enemy lock, BuffID fallback, and attack/move/spell gates | [docs/games/warcraft-3/cyclone.md](docs/games/warcraft-3/cyclone.md) |
| WC3 Volcano channeled waves, DataB/C/D/E mapping, stun vs channel duration | [docs/games/warcraft-3/volcano.md](docs/games/warcraft-3/volcano.md) |
| WC3 Pocket Factory timed producer, DataB Clockwerk unitCode, ANs1–ANs3 aliases | [docs/games/warcraft-3/pocket-factory.md](docs/games/warcraft-3/pocket-factory.md) |
| WC3 Rain of Chaos Inferno landings, DataA abilCode, not a Rain of Fire alias | [docs/games/warcraft-3/rain-of-chaos.md](docs/games/warcraft-3/rain-of-chaos.md) |
| WC3 Ancestral Spirit target rules, authored data, and same-edict corpse revival | [docs/games/warcraft-3/ancestral-spirit.md](docs/games/warcraft-3/ancestral-spirit.md) |
| WC3 Mana Shield toggle, authored damage/mana ratio, buff and order lifecycle | [docs/games/warcraft-3/mana-shield.md](docs/games/warcraft-3/mana-shield.md) |
| WC3 demo/TFT FOURCC-to-class extraction, registration offsets, generated references | [docs/games/warcraft-3/demo-ability-classes.md](docs/games/warcraft-3/demo-ability-classes.md) |
| WC3 player AI, Blizzard AI script contract, Human02 implementation plan | [docs/games/warcraft-3/player-ai.md](docs/games/warcraft-3/player-ai.md) |
| WC3 player AI architecture and implementation postmortem | [docs/games/warcraft-3/player-ai-postmortem.md](docs/games/warcraft-3/player-ai-postmortem.md) |
| WC3 client UI lifecycle, glue Birth/Stand/Death transitions, single-player campaign/mission/custom-game flow | [docs/games/warcraft-3/architecture/ui-flow.md](docs/games/warcraft-3/architecture/ui-flow.md) |
| WC3 campaign loading lifecycle, texture references, unused FDF art, cliff transitions | [docs/games/warcraft-3/loading-and-assets.md](docs/games/warcraft-3/loading-and-assets.md) |
| WC3 perf hot spots: MDX bone/geoset setup, shadow batching, client/server entity scans | [docs/games/warcraft-3/performance.md](docs/games/warcraft-3/performance.md) |
| WC3 RAM profiling: MDX buffer overhead, FDF pools, texture residency, loopback budgets | [docs/games/warcraft-3/memory.md](docs/games/warcraft-3/memory.md) |
| SC2 dropped-unit selection/control, shared WC3 routing, M3 picking and selection rings | [docs/games/starcraft-2/selection-and-control.md](docs/games/starcraft-2/selection-and-control.md) |
| SC2 HUD layout pipeline (sc2BaseFrame_t → uiFrame_t, layer IDs, stat bindings) | [docs/games/starcraft-2/hud-layout-pipeline.md](docs/games/starcraft-2/hud-layout-pipeline.md) |
| SC2 terrain/cliff rendering, normal welding, and material batches | [docs/games/starcraft-2/terrain-and-world-rendering.md](docs/games/starcraft-2/terrain-and-world-rendering.md) |
| SC2 Galaxy VM lifecycle, trigger execution, lookup indexes, diagnostics, and gaps | [docs/games/starcraft-2/galaxy-scripting.md](docs/games/starcraft-2/galaxy-scripting.md) |
| SC2 Galaxy objective/actor state, native ABI, scope cleanup, conversation catalog schema | [docs/games/starcraft-2/galaxy-presentation.md](docs/games/starcraft-2/galaxy-presentation.md) |
| SC2 Galaxy native inventory, archive extraction, audit limitations, implementation priorities | [docs/games/starcraft-2/galaxy-native-coverage.md](docs/games/starcraft-2/galaxy-native-coverage.md) |
| FS / VFS / MPQ loading stack, config/share dir resolution, SC2 vs WoW patterns, mmap ADT optimization | [docs/fs-loading-architecture.md](docs/fs-loading-architecture.md) |
| Config load order, cvar registry, `bind SHIFT+N` modifiers, `fs_basepath`/`fs_homepath`, `share/<game>/` + `~/.<game>/` layout | [docs/architecture/runtime.md](docs/architecture/runtime.md) |
| Code patterns that work well (file-shaped structs, table-driven parsing, pointer-walk parsers) | [docs/code-patterns-that-work.md](docs/code-patterns-that-work.md) |
| Launching UI/model scenes and maps from the command line; `make run-wow`, `make build-run-wow-*`, `make run-sc2` shortcuts | [docs/rendering-scene-workflow.md](docs/rendering-scene-workflow.md) |
| Release/debug builds, MSAA, GL/GLES backends, GLSL version (`GLSL=120/140/150`), shader dialect tokens, bone palette, video modes | [docs/build-and-renderer-platforms.md](docs/build-and-renderer-platforms.md) |
| Flatpak/Steam Deck packaging, portal-selected Warcraft data, XDG writable state, Steam shortcut integration | [docs/flatpak-steam-deck.md](docs/flatpak-steam-deck.md) |
| Shared model shader lighting and packed grass uniform contracts | [docs/architecture/model-shader.md](docs/architecture/model-shader.md) |
| Renderer backend: thin pipelines, root struct, GL state cache, MSAA/alpha-key, `r_stats` | [docs/renderer-backend.md](docs/renderer-backend.md) |
| `CS_SKY`, WC3 `SetSkyModel`, camera-relative sky models, sequence-0 animation, cross-game producer gaps | [docs/architecture/skybox-and-cs-sky.md](docs/architecture/skybox-and-cs-sky.md) |

## Coding Style

- Follow the C coding style used in the Quake 2 source code (id Software style).
- Use the same patterns for module organization, data structures, and naming conventions as in Quake 2.
- Prefer simple, flat, and data-oriented design over complex object-oriented abstractions.
- Keep the code readable, compact, and close to the metal — minimize unnecessary indirection.
- **Split gameplay behaviors into their owning abilities.** An ability owns its orders, validation, state transitions, animation moves, timers, interruption/inverse paths, and cleanup. Movement is an ability too: reusable steering, stepping, routing, and support-height behavior belong to Move. Keep `g_monster.c`, `m_unit.c`, `g_ai.c`, and equivalent general-purpose machinery focused on lifecycle, dispatch, and shared mechanisms; do not grow them with individual ability rules or direct spell-specific update calls. Reuse or extend generic function-table hooks so the implementation stays in `skills/s_*.c`, following `s_raven.c` and `s_move.c`. Existing misplaced behavior is precedent to refactor, not a pattern to copy. See [ability ownership](docs/games/warcraft-3/ability-implementation-plan.md#ability-owned-orders-and-persistent-behavior).
- **Flat ability procedures.** Keep classname, procedure, behavior flags, target shape and optional order names directly in each `ability_t` registry row. `CAbility*` names are functions using `abilityProc_t`, not descriptor globals. Pass `abilityitem_t` (actual rawcode plus registry row) through typed `abilityCall_t` messages and read authored data/profile text by that rawcode. A concrete procedure handles its own switch cases and delegates unhandled messages by directly calling its TFT parent procedure, as a Win32 window procedure calls its default procedure; do not add runtime parents, inherited callback tables, descriptor objects, or compatibility bridges. `AB_SPELL` selects the shared cast processor; combine independent policies such as `AB_CHANNEL`, `AB_TOGGLE`, and `AB_AUTOCAST`. Register concrete AbilityData row IDs or implementation codes and necessary internal commands; never register abstract retail classes. Runtime indices are rebuilt with the registry. Run `python3 tools/wc3_ability_class_audit.py --format=coverage` to audit registration against the TFT registry; registration alone does not prove implemented behavior.
- **No hacks. No silent fallbacks. No hiding errors.** Every implementation must have a solid reasoning. If a shortcut is taken, mark it with `/* HACK: */` or `/* TODO: */` and explain *why* the proper fix is not yet possible. Never demote, drop, or silently skip unresolved data — instead, log it with `fprintf(stderr, ...)` so future debugging reveals the gap. Demotion (e.g. `FT_TEXTURE → FT_FRAME` for unresolved resources) hides the real problem; the resource should be found and resolved, not discarded. This rule extends to every stage of the rendering pipeline: when a resolved path fails to load (`R_LoadModel` returns NULL), loads the wrong type (not `ID_43DM`, missing `.m3`), or is dropped for any other reason, log it to `stderr` before the `continue`/`return` — do not silently discard. The same applies to map-load resolution failures (CTile, cliff, object models): log each unresolved item by name so the gap is visible in a bounded run without requiring a manual dump call.
- **Keep one representation for one shader concept.** Do not add zero-count modes, parallel fallback uniforms, or per-game exception branches when the normal array/struct path can represent the value. Add an exception only when the common representation is demonstrably impossible, and document that constraint at the branch.
- **Do not add fallbacks for behavior the authoritative data can resolve correctly.** Read the actual source of truth (MPQ/DBC files, map data, server tables, layout files, or format metadata) and fix the lifecycle or module boundary needed to consume it. For example, resolve an AzerothCore numeric map ID through the client `Map.dbc`; do not substitute a default map name, guess a path, or maintain a parallel hardcoded ID-to-name table. A fallback is acceptable only when the authoritative source genuinely lacks the value, and must be explicit, logged, and documented with the reason.
- **Never guess at a bug fix; reproduce it in a test first.** Confirm the root cause from a failing regression test, authoritative data, and the actual code path before changing production behavior. Recreate the gameplay situation in the test, including the relevant data, entry points, timing, and event order. Assertions are evidence; runtime logs and a game launch are not mandatory. Use targeted logs inside the test or CLI diagnostics when needed, then remove investigative traces. If existing fixtures or tools (`mpqtool`, `dbctool`, `mdxtool`) cannot answer the question, extend them rather than guessing or defaulting to another game launch. See [test-first verification](CONTRIBUTING.md#test-first-behavior-verification).
- **Never leave debug logs in production code.** The only `fprintf(stderr, ...)` calls that belong unconditionally in source are lifecycle messages ("loading map %s", "R_Init") and genuine errors/warnings. Investigative logs added while tracing a bug must be removed when the fix lands. If a log is still needed because the feature is in-progress, wrap it in `#ifdef <GAME>_DEBUG_<SYSTEM>` (e.g. `SC2_DEBUG_CUTSCENE`, `WC3_DEBUG_SLK`) so it is off by default and can be enabled without touching the code. Never commit bare per-call trace logs (e.g. printing every trigger fire, every unit order, every tile resolve) outside such a guard.
- **Use `git blame` when investigating history.** When a value, macro, or code path seems wrong, use `git blame` or `git log -p -S <pattern>` to find when and why it was introduced. The commit message and diff often explain the original intent, distinguishing a deliberate trade-off from an accidental value or copy-paste left-over.
- **Write as little code as possible.** Prefer smart tricks and reuse of existing code over writing new functions. When Quake code style leads to verbose vertical expansion, override it with denser, shorter forms: pack related statements on one line, use ternary/comma for conditional side effects, omit braces for single-statement bodies, and collapse trivial helpers into one-liners.
- For trusted binary game data, prefer memory-mapped/file-shaped structs with trailing arrays wherever possible. Read the blob, allocate/copy it as one block if ownership is needed, and point consumers at that struct instead of decoding, cropping, or post-processing into parallel runtime arrays.
- **Always use DDX-style schema tables to load structures from keyed, columnar, or tagged data** such as XML, FDF, catalogs, DBC, CSV, and chunked files. Define the grammar as data first (`{ name/column/tag, offsetof(struct, field), type, count/flags }`), then run one generic parser over it. Custom code is reserved for genuinely nested, repeated, versioned, or context-sensitive productions and must hang off the schema as a callback or explicit special production, not replace scalar field tables with `if`/`else` chains.
- **Table-to-Edict convention:** `edict_t` table pointer fields must match the exact SLK/DBC table name in PascalCase (e.g. `edict->ItemData`, `edict->UnitProfile`, `edict->UnitBalance`). Do not use lowerCamelCase aliases (`itemData`, `profile`). This enforces convention-over-configuration between table names, struct types (`ItemData_t`), `edict_t` fields (`ItemData`), and metadata macro entries (`M("iabi", ItemData, abilList, BZ_FIELD_CSTR)`).
- Use `bzFieldType_t` from `common/shared.h` for shared parser conversion/storage contracts. Keep pointer-valued `BZ_FIELD_CSTR` distinct from bounded inline `BZ_FIELD_CHAR_ARRAY`, and use explicit color channel-order types. Source readers still own byte order, token conversion, bounds, and accepted subsets. Shader descriptors follow the same table pattern but retain renderer-owned `uniformType_t`, because that enum specifies GLSL declarations and `glUniform*` ABI rather than file/text decoding.
- Prefer format-driven parsing when the data has a fixed syntax. Use `sscanf(text, "%f,%f,%f", ...)` instead of hand-writing character walkers, separator loops, and ad hoc token logic.
- Do not bury schema in manual `if`/`else` or `switch` ladders when a compact table can describe the same work. Treat descriptor tables as the parser grammar and single source of truth; adding an ordinary field should require one table entry, not a new branch.
- **Never write a `strcmp`/`strcasecmp` ladder to map a string to a value when a config table (array of `{ name, value }` structs + a lookup loop) could be used.** A table stays the single source of truth and can be shared across modules (e.g. `Wow_RaceNumber` in `games/world-of-warcraft/common/wow_character_utils.h`). Keep such tables in a game's `common/` header so renderer/game/ui all resolve the same mapping.
- **Always use FOURCC values for four-character tags/magics.** Never compare against raw four-char string literals (`Wow_TagEquals(tag, "TLOM")`, `memcmp(tag, "PGOM", 4)`). Define named constants with `MAKEFOURCC('T','L','O','M')` (or the reversed `ID_*` form in `common/shared.h`) at the top of the file or in the owning header, then compare `DWORD`-wise. Four-char literals are magic numbers in disguise: they scatter the same tag across modules and hide the endian/byte-order contract. When the on-disk tag is stored reversed (e.g. WoW chunk tags read as `"TLOM"` for `MOLT`), name the constant after the reversed bytes actually compared, or add a comment mapping it to the canonical tag.
- **Prefer designated struct initializers for aggregate values.** Initialize structs in one expression using `MAKE(type, .field = value, ...)` (or `(type){ .field = value, ... }`), including nested structs and arrays, instead of zero-initializing and assigning fields one by one. Leave omitted fields zero-initialized and preserve field names so the layout remains self-documenting.
- Do not use several booleans to represent mutually exclusive state. Define and pass an enum, then dispatch from that enum.
- Put pure, reusable local helpers in a small nearby utils header (e.g. `sc2_utils.h`) as `static` functions. Keep subsystem-owned helpers that touch globals or runtime state in the `.c` file that owns that state. Do not create a dedicated header for a single tiny helper — add it to the subsystem's existing shared header (e.g. `r_local.h`, `g_local.h`) instead.
- Follow a strict DRY rule: do not duplicate logic or repeat the same data literal in multiple places.
- When a transform is applied per-component (RGB/XYZ), run it in a loop over the components or through a small transform macro instead of copy-pasting N near-identical statements. Prefer operating on a whole struct/vector (`vec3`/`COLOR32`) over its individual fields (`x`,`y`,`z`); drop to per-field code only when the fields genuinely differ.
- **Always reach for the `shared/` math library first.** Before writing inline matrix/vector/quaternion math, check whether `Vector3_*`, `Matrix4_*`, or `Quaternion_*` already covers the operation. When a needed utility is missing (e.g. `Vector3_clamp01`), add it to the appropriate `shared/types/` header and `shared/source/` implementation rather than writing a one-off local function — the shared library is the canonical home for reusable math.
- Keep runtime structs concise. Group related fields; use anonymous structs for repeated shapes; prefer `DWORD flags` over many standalone `BOOL` fields.
- Declare a pointer + element-count pair as one unit with `ARRAY(type, name)` (defines `type *name; DWORD name##_count;`). Access the count via `ARRAY_COUNT(name)`, test emptiness with `IS_ARRAY_EMPTY(name)` (checks both pointer and count), and iterate with `FOR_EACH_ARRAY(type, it, name)` — or `FOR_LOOP(i, ARRAY_COUNT(name))` when the index is needed — never read or write `name##_count` directly.
- Keep schema and field tables in a single column: put one descriptor or field entry on each line so the table is easy to scan and compare with its corresponding struct. Keep array entries on separate lines too, except for arrays of numbers or other genuinely short entries where a compact row remains clearer.
- In C source files, declare all file-scope types and arrays before function definitions; keep the function implementations below those declarations.
- Test flag membership with implicit bool conversion: `flags & FLAG` not `(flags & FLAG) != 0`.
- Use `snake_case` for functions and variables, `ALL_CAPS` for constants and macros, matching Quake 2 conventions.
- Use the `BZ_` prefix for project-private compile-time macros, generated binding helpers, environment toggles, and namespaced constants.
- When fixing warnings for short, future-facing hooks (one-line static moves, extern declarations, placeholder assignments), prefer commenting them out over deleting them. Add a short comment explaining the warning and when the line should come back.
- For WoW UI code (`games/world-of-warcraft/menu/`), do not fail silently. Emit a clear `UIWow:` log when a required script, handler, renderer resource, or fallback path is missing.
- When a function has more than 3 parameters, group them into a dedicated input struct (`draw<Thing>_t` or `<thing>Params_t`).

## General Formatting

- Minimize vertical space. Prefer fewer, denser lines over many short ones.
- Keep C source lines at or under 120 characters.
- Keep function calls on one physical line; never split their argument lists across lines. Keep arguments short enough to make this practical, accepting a line longer than 120 characters when necessary.
- Prefer variable and field names of 8 characters or fewer; use a shorter 4-character name when it remains clear (`cur`, `old`, `rot`, `scl`). Preserve externally defined API and on-disk schema names when the spelling is part of that contract.
- When adjacent declarations share a type and can be packed without obscuring initialization, declare them on one line (`m2PoseTime_t cur, old;`).
- Single-statement functions go on one line: `int f(void) { return 0; }`
- Omit braces for single-statement `if`/`else`/`while` bodies.
- Keep control-flow keywords at the start of their own line. Do not write chained forms like `...; if (...)` on the same physical line.
- Add a short comment before each non-trivial function describing why it exists — the constraint or contract that isn't obvious from the signature alone.
- Add a trailing `//` comment to every numeric `#define`. State the units, the specific reason for the value, and what it controls. Format: `#define NAME VALUE // units; why this value; used as ...`. Bare names are never self-documenting for magic numbers — a reader should not have to grep callers to understand what `0.15f` or `24` means.
- When changing a constant's value, update its trailing comment to explain why the value changed — what measurement, constraint, or incident forced the new value. The comment must reflect the current value, not the old one.
- For any fallback, workaround, or partial implementation, prepend `/* HACK: */` or `/* TODO: */` and explain why.
- Extract grass rendering tuning and schema values into named `WOW_GRASS_*` defines at the top of the WoW renderer header; do not leave grass literals in generation code or hardcode asset/material values when MPQ data provides them.
- When providing a bug fix, add an inline comment at the fix site explaining why the fix is correct and what the original behaviour was.

## WinAPI-style Typedefs for Structs

- Struct names are ALL CAPS, short, and descriptive (e.g., `PORTRAITFOG`). No `_t` suffix.
- Use WinAPI-style `LP`/`LPC` typedefs for struct pointer types (`LPCPORTRAITDEF`, `LPRECT`).
- `LP` = long pointer (non-const), `LPC` = long pointer to const.
- Define both alongside the struct using separate `typedef` lines so `LPC` is `const struct *`.

## What to Avoid

- **Do not patch loaded UI layout in code.** SC2Layout and FDF files define the correct layout. When a panel's anchors seem wrong (e.g. off-screen positioning from a cross-panel reference), fix the anchor resolution in the layout system so it handles the case correctly — don't override anchors per-panel in game code. The layout data is authoritative; code must apply it faithfully.
- **Never duplicate a game's UI layout in C.** All geometry — positions, sizes, anchors — belongs in the game's own layout files: FDF for WC3, FrameXML for WoW, SC2Layout for SC2. For WC3: load the relevant FDF from the MPQ via `UI_EnsureFDF`, generate a binding header with `fdfbindgen`, and fill runtime data (textures, text, click handlers, visibility) into the parsed frame tree. Do **not** write project-owned FDF files in `share/UI/FrameDef/` — if a frame exists in War3.mpq, bind to the War3 original; if the frame type is native (portrait, command button, minimap, tooltip) and has no FDF definition in the MPQ, construct it in C with a static `FRAMEDEF`, `UI_InitFrame`, `UI_SetSize`, and `UI_SetPoint` using inline float literals. Do not declare named `#define` position constants for these inline values.
- **Do not ship project-owned WoW FrameXML.** Load the authoritative XML/Lua and inherited templates from WoW archives. If classic WoW creates presentation outside FrameXML, reproduce that runtime construction instead of authoring a parallel XML file under `share/Interface/FrameXML/`. Test fixtures may mirror native files under their original Blizzard paths; production may not load those fixtures or `OpenWarcraft*.xml` substitutes.
  **`UI_SetPoint` Y sign convention:** `UI_CopyFrameBase` encodes Y offsets without negation; the client negates them on decode. This matches WC3 FDF convention where positive Y = upward. To position a frame *below* a TOPLEFT anchor, pass a **negative** Y: `UI_SetPoint(f, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, x, -y_from_top)`. Passing positive Y for a downward displacement places the frame above the screen.
- Do not introduce helper variables just to name an intermediate result if the expression is already readable inline.
- Do not add blank lines between short, related statements.
- Do not split a declaration and its first assignment onto separate lines.
- Do not add null-pointer or function-pointer guards before calling cross-module API functions (`ui.*`, `re.*`, `s.*`, etc.). These are guaranteed to be set at init time.
- Treat every cross-module API entry point as mandatory. Call `ui.*`, `re.*`, `s.*`, and game/server import callbacks directly; never add `if (api.Func)` guards that mask an incomplete API table or hide an initialization bug. Do not introduce generic named-command callbacks such as `GameCommand`; use a dedicated numbered `svc_*` message and typed handler for each protocol event.

## WC3 UI Tooling — fdfbindgen

The `fdfbindgen` tool (built at `build/bin/fdfbindgen`) reads one or more FDF files and emits a C header that maps every named frame to a typed struct field. The generated headers live in two locations:

- `games/warcraft-3/menu/generated/` — screen-level frames used by the menu/glue layer (`ui/screens/*.c`).
- `games/warcraft-3/game/generated/` — in-game HUD frames used by `game/hud/*.c`.

**When to regenerate:** any time you add, rename, or remove a named frame in an FDF file that already has a corresponding generated header, run fdfbindgen to keep the header in sync. Do not edit generated headers by hand.

**How to run — glue/menu header from MPQ:**
```
mpqtool -mpq War3.mpq cat UI/FrameDef/Glue/MainMenu.fdf \
  | build/bin/fdfbindgen -prefix MainMenu -root MainMenuFrame \
      -load UI\\FrameDef\\Glue\\MainMenu.fdf - \
  > games/warcraft-3/menu/generated/main_menu.h
```

**How to run — in-game HUD header from MPQ:**
```
mpqtool -mpq War3.mpq cat UI/FrameDef/UI/InfoPanelBuildingDetail.fdf \
  | build/bin/fdfbindgen -prefix InfoPanelBuildingDetail -root InfoPanelBuildingDetail \
      -load UI\\FrameDef\\UI\\InfoPanelTemplates.fdf \
      -load UI\\FrameDef\\UI\\InfoPanelBuildingDetail.fdf - \
  > games/warcraft-3/game/generated/info_panel_building_detail.h
```

All generated headers bind directly to frames parsed from War3.mpq FDF files. Do not generate headers from project-owned FDF files — there are none; use C code instead for frames that have no MPQ equivalent.

Key flags: `-prefix <Name>` sets the struct and function prefix; `-root <FrameName>` selects which top-level frame becomes the binding root; `-optional-root <FrameName>` adds an additional root that is allowed to be absent; `-optional-children` suppresses missing-child errors for frames that may not appear in all archive variants (e.g. TFT-only frames).

## Tool Failures

- **If a tool fails repeatedly, stop and notify the user.** When a tool fails more than 2-3 times with the same error, do not keep retrying blindly. Inform the user what is failing and why, propose a workaround, and ask whether to continue or wait for help.

## Test Discipline

- **Verify behavior with automated tests instead of launching the game wherever possible.** Do our best to reproduce in tests the situation we would otherwise inspect in a gameplay session. Use the real production entry points and scheduler/callback path, deterministic simulation time, and representative fixtures; test the action sequence and observable result rather than copying the implementation into the test. Extend the harness or fixture when coverage is missing. A headless test runner may use the game binary without opening the game or playing through a map.
- **Game launches are an exception for a named coverage gap.** Before launching, identify the specific property that cannot be verified through tests or diagnostic tools, such as framebuffer output or platform input behavior. First try to make it testable. If a launch is still needed, exercise only that gap in a bounded run and turn any discovered gameplay failure into a regression test. Once the scenario is covered, use the test; do not repeat game launches just because code was rebased, moved, or rebuilt. See [verification workflow](CONTRIBUTING.md#test-first-behavior-verification).
- Verify every Warcraft III data/UI change in both ROC (default archives) and TFT (`-tft`). TFT archives override ROC
  paths and may replace whole FDF/string/data files rather than extending them, so cover both variants with fixtures and tests.

- **Every structural change must include or update tests.** When you add a function, change a behavior path, fix a bug, or modify a struct/API contract, check whether existing tests cover the change.
- **Audit every WC3 `edict_t` field against save/load.** Any field added, removed, moved, or retyped in `games/warcraft-3/game/g_local.h` must update the persistence contract in `games/warcraft-3/game/g_save.c` in the same change. Plain value fields are retained by the raw `edict_t` record; entity/client pointers must use a stable `fields[]` fixup, and process-owned pointers must be cleared and rebuilt explicitly. Add a focused ROC/TFT round-trip test for the field and an invalid-reference test when applicable; never rely only on the save header's `sizeof(edict_t)` check.
- **Engine/game-boundary diff check:** Before finishing any diff that touches `client/`, `common/`, `renderer/`, or
  `server/`, check the diff for two things: (1) any new enum member, macro, or struct field whose name contains a
  race/faction/unit/spell/franchise-specific proper noun, and (2) any new `#ifdef <GAME>` guard or hardcoded
  per-game command string added to a function that previously had none. Either one means the change belongs in
  `games/<game>/` instead — route it through the shared dispatcher's existing function-table hook (or add one)
  rather than branching inline. "Franchise-specific" includes generic-sounding resource and mechanic names that are
  unique to one game's design, such as WC3 Gold/Lumber, SC2 Minerals/Vespene, or WoW talent points; it is not limited
  to character, race, or spell names. If a new `common/`, `client/`, `renderer/`, or `server/` symbol is only consumed
  by logic in one `games/<game>/` directory, that is itself the signal to move it, regardless of how generic the name
  sounds. Run the engine-boundary check mechanically with `python3 tools/engine_boundary_audit.py`: it diffs those
  directories against `main` and flags new symbols matching the maintained per-game term list and new `#ifdef <GAME>`
  guards. Run it before finishing any diff touching those directories. See PR #412 (`EF_RESOURCE_GOLD_SOURCE` /
  `RESOURCE_GOLD_SOURCE_MIN_DISTANCE` in `common/shared.h` and `CL_BuildCursorTooCloseToGoldSource` in
  `client/cl_view.c`) for the kind of violation a visual scan missed, and see
  [docs/architecture/server-selected-effects.md](docs/architecture/server-selected-effects.md) for the reference
  pattern and worked examples of these mistakes and the fixes.
- **New code paths need new tests.** If you add an `if` branch, a new function, a new field, or a new cache/state machine, write a test for the new path and its inverse.
- **A revert must remove the whole feature's wiring, not just the part that failed.** Before finishing a revert, list every symbol the original commits touched and confirm each is either fully removed or still has a complete, live call graph. `git diff <revert>^ <revert>` must read as self-consistent on its own; it must not depend on a later commit to become sound again. Do not leave a production entry point, such as a JASS native or command handler, driving a state machine whose periodic scheduler hookup was reverted out from under it.
- **Periodic functions need scheduler-path tests.** Any function meant to run every frame or tick, such as a `G_Run*` function, queue drain, or periodic sweep, needs a test proving the real scheduler calls it. Run the actual per-frame entry point (`G_RunFrame()` or equivalent) enough times to observe the queued state resolve; calling the drain function directly only proves it works in isolation.
- **A function with zero callers outside `tests/` is a red flag, not a coverage win.** Grep for callers before finishing a diff. If the only call sites are under `games/warcraft-3/game/tests/`, either wire it into the real path or explain in the commit message why it is intentionally test-only.
- **Cache/state-machine changes double-test.** Test both cache hit and cache miss paths, and verify performance counters where tracked.
- **Run `make test` before committing code or executable fixture/build changes.** This umbrella target runs all test binaries: `test_openwarcraft3` (net + tool_common), `test-commands`, `test-server-net`, `test-sc2`, `test-wow-*`, `test-ui`, and `test-wc3-engine` (in-engine WC3 tests). Documentation-only edits need text, link, and diff checks, not builds, game launches, or the full test suite.
- **In-engine WC3 tests** live in `games/warcraft-3/game/tests/` and run headlessly against the generated fixture MPQ via `make test-wc3-engine` (or `build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test '*'`). They exercise production game code without an interactive gameplay session or a developer's installed archives.
- **In-engine WoW tests** run headlessly via `make test-wow-engine` (or `$(openwow-tests) +dedicated 1 +test '*'`). Standalone WoW tests (no game deps) run via `make test-wow-appearances/abilities/game/entities/ui`.
- **Compile affected code and run tests before finishing implementation work.** Start with the regression reproducer,
  verify it fails before the fix and passes afterward, then run `make test`. Build every game whose shared renderer path
  changed and test the affected contracts. A game launch is not a completion requirement; report any specific coverage gap.
- **Exercise the changed behavior, not just startup.** For order, networking, and UI logic, drive the real command,
  serialization, dispatch, and lifecycle paths from the test harness and assert the resulting state or output. A successful
  main-menu startup proves none of those contracts. If a visual/platform gap still needs a game run, reach the relevant
  connected state and capture the changed behavior, following the exception rule above.
- **Auto-quit the app with `+com_frame_limit N`.** When running a game for verification, pass `+com_frame_limit 100`
  (or similar) so it exits without manual intervention. Example: `make run-wow ARGS="+com_frame_limit 100"`.
- **`git blame` before changing existing struct/API fields.** Understand why a field exists and what trade-offs were made before changing it.
- **Do not disable a failing test.** Fix the code or fix the test — do not comment it out, add `SKIP`, or reduce its coverage.
- **"Pre-existing" failures and warnings are not an excuse.** If `make test` shows failures, fix them. Remove dead dispatchers still calling old harness macros (e.g., `RUN_TEST` in migrated suites), unwrap mock callbacks accidentally wrapped in `TEST()`, and ensure test fixtures are complete (e.g., load GlobalStrings.fdf when tests resolve string-table keys). If the build emits warnings in code you encounter during a task — even in files you did not change — fix them before finishing. A warning that was there before your change is still your responsibility to remove when you touch the surrounding code or discover it during a build.
- **Keep test assertions in sync with content.** When you update action bar names, inventory names, UI strings, or any other literal values the game writes to clients, update the matching `T_STREQ`/`T_EQ` assertions in the test suite immediately. A test that checks a stale string is a silent gap, not a passing test. Do not declare a task finished if `make test` shows any failures — find the root cause and fix it.

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full engine/game boundary, module structure, network state contracts, engine struct/API discipline, and UI rendering overview.

Key principles inline:
- Runtime modules communicate through function tables (`R_GetAPI`, `M_GetAPI`, game imports/exports).
- The server controls what the client draws via state bits in `playerState_t`. The client just reads them.
- Never hardcode game-specific asset names, animation names, or franchise-specific literals in engine code —
  including enum members, macros, and struct field names in `common/`, `renderer/`, `client/`, `server/`.
  A named boolean for one race/unit/spell (e.g. `RF_BUILDING_FIRE_UNDEAD`) does not belong in a shared enum
  even though the flags field itself is generic infrastructure. "Franchise-specific" also includes generic-sounding
  resources and mechanics unique to one game's design, such as WC3 Gold/Lumber, SC2 Minerals/Vespene, and WoW talent
  points. If a symbol added to a non-`games/` path is only consumed by one `games/<game>/` implementation, stop — move
  the resolution into that game directory and expose a generic field or callback instead, even when the name contains
  no obvious proper noun.
- Never use `#ifdef SC2`/`#ifdef WOW`/`#ifdef WC3` (or any per-game macro) to gate *any* code in shared engine
  files (`client/`, `common/`, `renderer/`, `server/`) — not just constants, but branches, command handlers,
  and logic blocks too. If code only matters for one game, it belongs in `games/<game>/`, reached through the
  existing function-table/vtable boundary (`re.*`, `gi.*`/`ge.*`, or a suitable gameplay interface; never `menu.*` for HUD).
  A shared dispatcher should stay entirely game-agnostic even when only one game currently drives it — other
  games simply never send that command, which is already harmless without a compile guard. Per-game constants
  live in `games/*/common/ui_constants.h` and resolve via the per-game `-I` include path.
- **Never widen `entityState_t` or `playerState_t` without asking.** These structs are network contracts — every byte change affects bandwidth, delta compression, and snapshot size. If you need more data in the entity state, discuss with the developer first. Use existing fields, renderer-side caches, or DBC lookups instead.

## Server-Authoring Pattern (Quake 2 STAT_LAYOUTS)

- `uiflags` bitmask hides layout layers: each bit corresponds to a `UILAYOUTLAYER` value. Server sets bits to hide layers, client checks `(1 << layer) & uiflags`.
- `client_ui_state` enum (`CLIENT_UI_GAME`, `CLIENT_UI_LOADING`, `CLIENT_UI_CINEMATIC`) controls broad client modes.
- Never hardcode game-mode-specific skip logic in the client. The server sets the appropriate flags; the client respects them.

## Mouse Input Architecture

- Mouse state is owned by the client: the `mouse` global (`mouseEvent_t` in `client/cl_input.c`) is the single source of truth.
- The UI library receives mouse events via `menu.MouseEvent(x, y, button, down)` — push-based, called during `SDL_PollEvent` in `CL_Input()`.
- Game-mode-specific mouse behavior lives in per-game `cl_input_<game>.c` files via the `CL_InputMode*` functions.
- Never create a separate mouse state struct in game UI code. Never poll mouse event state during draw.

## UI Module Boundary

- **The menu library is exclusively main-menu/glue UI. No gameplay exceptions.** Never add player/entity snapshots,
  selected-unit data, HUD update callbacks, gameplay image resolution, or in-game window APIs to `menuImport_t`,
  `menuExport_t`, or a menu screen controller. `UpdateUnitUI`, `UpdatePlayerState`, and `ResolveImagePath` were
  architectural violations, not precedents. Do not replace them with renamed or opaque gameplay callbacks.
- **All in-game UI is game-authored.** HUD, pause/options dialogs, inventory, tooltips, and transient windows are authored
  in `games/<game>/game/` through `svc_layout` / `svc_window`. The generic client owns drawing, hit testing, focus,
  local frame bindings, and window interaction. Resolve recipient-specific skin aliases in the game before registering
  concrete resource paths. If the protocol cannot express a feature, extend its generic contract with tests.
- **Presentation ownership is exclusive.** `CL_MenuActive()` gates menu drawing, keyboard/text/mouse input, lobby updates,
  and registered menu commands. Loading and `ca_active` forbid menu execution even when `key_dest == key_menu`.
  A retained library or cached menu screen does not grant permission to draw or interact. Test active/loading and
  disconnect/return-to-menu paths through real screen/input/command dispatchers.
- Do not add menu imports for loading-state polling, layout decoding, mouse polling, or map gameplay state.
  Loading presentation is game-authored and drawn by the client; only `CL_PrepRefresh()` may activate a prepared world.
- Run `python3 tools/menu_boundary_audit.py` alongside the engine-boundary audit. Removal of an ABI field must remove
  its producers, consumers, screen hooks, tests of the obsolete contract, and misleading authoring documentation.
- The menu library remains loaded, but its resources are shut down on loading and initialized again on return.
  Commands retain function pointers.
  Actual unloading requires explicit command unregistration, resource teardown, and safe reload tests; it is not
  necessary for exclusive presentation. See [menu/HUD boundary](docs/architecture/ui-system.md).

See [docs/ui-authoring.md](docs/ui-authoring.md) for FDF conventions, screen controller patterns, ConsoleUI, and stb_fdf.h.

## Missing Asset Placeholders

Follow Quake 2's pattern. Never fail silently, never crash, never log per-frame.

- **Registration always returns a valid handle.** `R_LoadTexture` returns `tr.texture[TEX_PLACEHOLDER]` on file-not-found. `R_LoadModel` returns an empty zeroed `model_t`. Callers never get NULL.
- **Log once per unique asset.** Use a static `last_missing` pointer to suppress repeated warnings for the same filename.
- **Cache the result.** Higher-level caches store the placeholder handle just like a successful load.
- **Do not add per-frame or per-draw warnings.**
- **Do not add local null-check guards for textures returned by `R_LoadTexture`.** The function guarantees a valid pointer.

## Command Conventions

- **Fast-forward bounded simulations explicitly.** `com_frame_limit` counts main-loop iterations, not 10 Hz server ticks. For AI and other long simulation diagnostics, pass `-com_fast_forward` (or `+set com_fast_forward 1`) together with `+com_frame_limit N`; each loop then advances one fixed `FRAMETIME` (100 ms) server tick without wall-clock pacing. Example: `build/bin/openwarcraft3 -data 'data/Warcraft III' -roc -com_fast_forward +map 'Maps/(2)Rivercross.w3m' +com_frame_limit 10000`. Do not use fast-forward for rendering, input, networking, or frame-pacing validation.
- **SDL display modes are opt-in diagnostics.** Pass `-vid_modes` (or `+set vid_modes 1`) to log the available SDL display modes during renderer startup; normal startup omits the list. This works in WC3, WoW and SC2 and is not saved automatically. Plural `vid_modes` controls logging; singular `vid_mode` selects resolution. See [video modes](docs/build-and-renderer-platforms.md#video-modes).
- The `+` prefix (e.g. `+map`, `+menu_main`) is for **command-line arguments only**. It tells `Cbuf_AddLateCommands` to strip the `+` and queue the command for startup execution.
- In code, use the bare command name when calling `Cbuf_AddText` or `mi.Cmd_ExecuteText`: `"map ..."` not `"+map ..."`.
- **Launch UI scenes directly** with a `+menu_<scene>` late command — there is no `+ui` command. Scene names come from the `menu_*` commands each game registers via `Cmd_AddCommand` in its `menu_main.c`: WC3 (`menu_main`, `menu_options`, ...) and WoW (`menu_login`, `menu_character_select`, `menu_character_create`, `menu_ingame`). Examples: `build/bin/openwarcraft3 -data 'data/Warcraft III' +menu_main`, `build/bin/openwow -data data/world-of-warcraft +menu_character_create`. See [docs/rendering-scene-workflow.md](docs/rendering-scene-workflow.md).
- **Use the engine screenshot command, not desktop capture.** `screenshot` writes `screenshots/shotNNNN.jpg` (JPEG, quality 90). `screenshot N` captures the Nth fully rendered frame after the command executes. From the command line, place it after the map selector and keep `com_frame_limit` larger than the delay:
  ```
  build/bin/opensc2 -data data/StarCraft2 +map Maps/TerranTest.SC2Components +vid_hidden 1 +screenshot 5 +com_frame_limit 10
  make run-sc2 ARGS="+map Maps/TerranTest.SC2Components +vid_hidden 1 +screenshot 5 +com_frame_limit 10"
  ```
  Do **not** remove existing screenshots before running — just read the highest-numbered file after the run. Read the JPEG with the `Read` tool and compare it visually against the reference image before reporting the task done. A black or empty frame means the renderer did not reach the game loop; increase `com_frame_limit` or reduce `screenshot N`.
- **Project-owned config/data layout.** Read-only game defaults ship in `games/<game>/share/config.cfg`, installed to `build/share/<game>/` by `make install-share` (a dependency of `make build` and every `run`/`build-run` target). Engine-wide assets (`fonts/`) stay in `share/`. Writable user state (`config.cfg`, `autoexec.cfg`, `characters.xml`) resolves to `~/.<game>/` on Unix and `%APPDATA%\<game>\` on Windows, falling back to `share/<game>/` when `$HOME` is unavailable. The game name is the `BZ_GAME` define in each `games/*/game.mk` (`warcraft-3`, `world-of-warcraft`, `starcraft-2`); resolve it with `FS_BasePath()` / `FS_HomePath()` / `FS_UserPath()` in `common/common.c`, or the `fs_basepath`/`fs_homepath` cvars. See [docs/architecture/runtime.md](docs/architecture/runtime.md).

## Domain

- This is a **real-time strategy game** (RTS), so game logic should account for unit management, pathfinding, resource gathering, building construction, and large numbers of entities — adapted from the Quake 2/3 entity/server model where applicable.

## Documentation Discipline

- **Documentation is part of the task, not optional cleanup.** Any fact that required looking through code, authoritative game data,
  runtime logs, history, issues/PRs, or external references must be written into reusable agent-facing documentation before finishing.
- Capture the answer a future agent would otherwise have to reconstruct: ownership and data flow, schema/field meanings, defaults and
  sentinels, lookup chains, version differences, exact diagnostic commands, confirmed root cause, and misleading approaches to avoid.
- When implementing or changing a feature, add or adjust agent-friendly documentation if the change introduces a new workflow, tool,
  convention, subsystem, or non-obvious constraint.
- Put durable documentation under `docs/`: shared workflows at `docs/`, engine architecture at `docs/architecture/`, and game-specific material at `docs/games/<game>/`.
- Keep AGENTS.md as a concise index and rule set. Detailed workflows and reference material belong in dedicated files.
- Keep documentation concise and actionable — prefer command examples and file paths over prose.
- **Populate docs as you go.** Do not leave findings only in conversation context, terminal output, or code comments. If no document
  exists for the subsystem, create one.
- Add every new dedicated document to the nearest table of contents (`AGENTS.md`, a game `readme.md`, or an architecture index) and add
  cross-links from adjacent workflow/reference documents when that makes the knowledge easier to find.
- Follow [docs/documentation-guide.md](docs/documentation-guide.md) for what to capture, where to put it, and the completion checklist.
- **Before finishing any diff that touches `common/`, `renderer/`, `client/`, or `server/`**, re-read every
  new enum member, macro, and struct field you added. If any symbol name contains a race, faction, unit, or
  spell name from a specific game's fiction, stop and move it into `games/<game>/` — see
  [docs/architecture/server-selected-effects.md](docs/architecture/server-selected-effects.md).

## GitHub Issues

- Before creating a GitHub issue, check available labels with `gh label list`.
- When creating issues, assign appropriate labels (e.g. `enhancement`, `warcraft-3`, `world-of-warcraft`, `renderer`, `ui`).
- If a needed label doesn't exist, create it first with `gh label create`.
- Keep issue titles at most 80 characters.
- Keep issues scoped to one game/title when possible.
