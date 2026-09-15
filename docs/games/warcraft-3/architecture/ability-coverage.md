# Ability Coverage

This note tracks the current OpenWarcraft3 game-side ability registry against
the reference ability base-code list used for parity work.

See the [implemented ability verification review](../ability-verification-review.md) for the
September 2026 source audit, lifecycle fixes, and current coverage limits.

Primary local files:

- `games/warcraft-3/game/skills/s_skills.c`
- `games/warcraft-3/game/skills/*.c`

The practical parity list maps Warcraft ability base codes to concrete ability
type definitions. Unknown or unsupported codes may resolve to explicit local
stubs for recognition/passive coverage, but command-card discovery requires a
an explicit command or an executable `AB_SPELL` definition so a stub cannot create a dead button.

## Current Model

Campaign rawcodes in `CampaignAbilityStrings.txt` are registered through
`games/warcraft-3/game/skills/s_campaign_abilities.c`. Registry entries resolve to reusable `ability_t` handlers; each invocation carries its
actual rawcode in `abilityitem_t`, so `S_SpellData`, duration, unit, buff, and
target lookups read the campaign `AbilityData.slk` row. Shared execution
families cover campaign area damage, War Stomp, summons, timed statuses,
toggles, dispel, Battle Roar, and Storm Bolt without aliasing a standard
rawcode's gameplay data. The registry test in `game/tests/t_spell.c` checks every
campaign rawcode for a concrete command, executable procedure, and matching spell
code. Campaign-specific presentation, exact summon composition, Parasite
death spawning, and full three-form Storm/Earth/Fire behavior remain separate
follow-up contracts when their authored rows and runtime consumers are added.

OpenWarcraft3 uses a small Quake-style `ability_t` registry row and one `abilityProc_t` per behavior. Flags select command, spell, item, autocast and update paths; those paths send typed `abilityMsg_t` messages. `UnitAddAbility` and `UnitRemoveAbility` send `A_ENABLE` and `A_DISABLE` immediately. Stateful abilities answer `A_LEVEL`, and `S_RefreshAbilityLevel()` forwards the value through `A_LEVEL_CHANGED`. Command-card discovery uses `S_AbilityHasCommand`, so passive/no-op procedures do not create dead buttons. Concrete procedures explicitly call their TFT parent procedure for unhandled messages; no callback descriptor or runtime parent table exists.
`Aoar` (Healing Ward Aura), `Aabr` (Aura of Blight), and `Aarm` (Mana Regeneration Aura) are registered passive regeneration families. Alias rows such as `ACnr -> Aoar` and `ANre -> Aarm` are discovered on the owning unit and keep alias-authored area, targets, and DataA/DataB values. Percentage mode scales against the recipient's maximum HP/mana; same-family sources use the strongest value while `Aoar` and `Aabr` remain distinct contributors. Presentation effects remain separate work. See [Regeneration Auras And Fountains](../regeneration-auras.md).

The CommonAbility base codes use the subsystem that already owns their behavior.
`AEbu`, `AGbu`, `AHbu`, `ANbu`, `AObu`, and `AUbu` share the build command;
`ARal`, `Aatk`, and `Amov` share the ordinary rally, attack, and move commands;
`Atdp` and `Atlp` share cargo drop/load. `AEpa` is the Poison Arrows toggle and
reads its own `DataA` bonus in missile attack resolution. `Aloc` applies
unselectable, invulnerable, collisionless, no-pathing traits during unit spawn.
The five `Afih`/`Afin`/`Afio`/`Afir`/`Afiu` rawcodes share one passive procedure;
retail does not list these rawcodes in `UnitAbilities.slk`, so buildings synthesize
`CAbilityOnFireHuman` as an intrinsic capability. `G_SetHealth()` refreshes its derived level and
the `A_LEVEL_CHANGED` case owns health-stage and `UnitData.race` model selection. Hero revival and Hero identity
remain owned by their existing lifecycle systems, while their CommonAbility rawcodes
are explicit passive rows. ROC and TFT `ability_audit` rows match for this block;
`Adet` is an abstract base code absent as a standalone row in both archives.

Timed statuses remain generic `abilstatus[]` records. Their common duration and
expiration bookkeeping stays in `unit_updatestatuses()`; add apply, refresh, or remove
procedure cases only when a status record can resolve its owning ability unambiguously.

Abilities are discovered through the static `abilitylist[]` in
`games/warcraft-3/game/skills/s_skills.c`. Normal unit command buttons are shown only when the
unit ability alias resolves through `Units\AbilityData.slk` to a registered base
code. The command keeps the original alias when dispatching so per-alias SLK
data such as item heal amounts still resolves correctly. Hero abilities are
stored as `heroability_t` entries on the unit; `heroAbilList`, skill points,
`reqLevel`, `levelSkip`, maximum ranks, and the Research-button learn menu are
described in [Hero Ability Progression](../hero-abilities.md).

Registry entries must not be counted as implemented until their gameplay
consumer, authored data, and inverse behavior are covered. Use the
[Ability Implementation Plan](../ability-implementation-plan.md) to start from
the archive data and observable behavior, then add focused evidence for any
remaining uncertainty before adding coverage.

The old `a_unimplemented` registry marker is not an implementation strategy for
these entries. It may remain only as temporary audit scaffolding while a real
handler is being developed, and must be removed from an entry when that entry
is registered for gameplay.

Directly copying another engine's ability classes is not mechanical. The local
implementation should port behavior into flat C procedures, `umove_t` state
machines, existing edict fields, and data loaded from SLK/config tables.

## Local Registry

| Code | Local file | Status |
|---|---|---|
| `CmdStop` | `s_stop.c` | Registered, no `cmd`; order helper exists. |
| `CmdMove` | `s_move.c` | Implemented ground move command. |
| `CmdAttack` | `s_attack.c` | Implemented basic melee/ranged attack and projectiles. Explicit Attack may target friendly units and buildings; Smart/right-click attack selection remains unchanged. Building attack range is measured to authored pathing footprints so large blocked structures do not cause attackers to orbit their centres. |
| `CmdBuild` | `s_build.c` | Implemented build menu and placement flow. |
| `CmdHoldPos` | `s_holdpos.c` | Implemented Hold Position state; command-card and scripted `holdposition` orders share the same state transition. |
| `CmdPatrol` | `s_patrol.c` | Registered stub. |
| `CmdCancel` | `s_cancel.c` | Implemented UI cancel. |
| `CmdSelectSkill` | `s_selectskill.c` | Partial; candidate skill menu, next-rank Research UI, point/level gating, max-rank hiding, and authoritative learning are implemented. Skill-point and next-rank numeric overlays are implemented; multi-selection presentation remains. |
| `Ahar` | `s_harvest_lumber.c` | Partial worker harvest implementation. |
| `Amic`, `Amil` | `s_militia.c` | Partial; paired Hall/worker Call to Arms and Back to Work orders, footprint approach, Data A/B in-place Peasant/Militia transform, carried-resource return, per-unit `Bmil` expiry, and explicit worker auto-harvest resume are implemented. CommandStrings-keyed errors, ability sound/effects, and morph-animation polish remain. See [Call to Arms and Militia](../call-to-arms-and-militia.md). |
| `Amrf`, `Arav` | `m_unit.c` | Partial; script-issued `ravenform`/`unravenform` immediate orders select the matching stock transform ability by its AbilityData Data A / UnitID endpoints, transform the same unit in place, and rebind the target type's Required Animation Names. Full cast/morph timing, altitude transition, effects/sounds, automatic duration reversion, and command-card behavior remain. See [Unit Animation Properties And Transformation Forms](../unit-animation-properties.md). |
| `Arep` | `s_repair.c` | Partial; entity command, Smart Repair, Shift target order, ranged approach, completed-building DataA/DataB costs, paused Human power building, and nearest-valid Auto Repair are wired. Full target masks/naval and broader autocast policy coverage remain. |
| `Agld` | `s_goldmine.c` | Basic gold mine harvest loop. |
| `AHad` | `s_devotionaura.c` | Partial local-only aura effect; status application is stubbed. |
| `AHhb` | `s_holylight.c` | Partial target spell; validates target/range/masks, spends mana, starts cooldown, heals allies, damages undead enemies, and plays target art. |
| `AHtb` | `s_thunderbolt.c` | Partial target spell; projectile, damage, mana, cooldown, range, target masks, and stun status. |
| `ANfb` | `s_thunderbolt.c` | Partial target spell sharing Thunder Bolt behavior with Fire Bolt data/art. |
| `ANdc`, `SNdc` | `s_campaign_abilities.c` | Dark Conversion consumes the targeted unit, spawns the authored `UnitID` at its location, applies its authored buff, and publishes summon events. Human05's JASS conversion trigger owns the final zombie replacement, allegiance, and order. |
| `AHwe`, `AOsf` | `s_summon.c` | Partial no-target summon; reads unit id/count/duration, spawns owned timed-life units, and publishes unit/player summon events. |
| `AOmi` | `s_mirror_image.c` | Partial Mirror Image; no-target cast creates data-counted timed copies, marks them as illusions, copies visible Hero state, excludes them from persistent Hero shortcut/XP/revival behavior, and publishes summon events. Damage multipliers, dispel, shuffle, and complete visual semantics remain. |
| `AHbz`, `AUcs`, `ANcl` | `s_area_spell.c` | Partial point/channel spell family; Blizzard ticks area damage, Carrion Swarm applies a simple point blast, Channel opens cancel mode. |
| `ANch`, `AIco`, `Aeat`, `Ambt`, `Aroo` | `s_utility_abilities.c` | Partial utility behaviors: Charm ownership transfer, Eat Tree heal/remove, Moon Well transfer, Root toggle. |
| `AIhe`, `AIma`, `AImi` | `s_item.c` | Synchronous item use for heal, mana restore, and permanent life gain; successful charged uses decrement charges and zero-charge perishables are removed. |
| `AIda` | `s_item.c` | Scroll of Protection item-defense AOE: applies authored `Bdef` duration/area/armor bonus to allowed friendly targets and consumes the successful charged use. |
| Heavy/system abilities | `s_ability_stubs.c` | Registered explicit stubs for passive autocast, cargo, mine, shop, harvest variants, item passives, and stat/XP item families. |

## Evidence-backed additions

`ANto` (Tornado) is registered as a `CAbilityWhirlwind` channel and reuses the
existing whirlwind thinker. ROC and TFT `ability_audit` rows both author a
40-second no-target ability with summon unit `ntor` and buff `BNto`; the local
thinker therefore follows the caster like `AOww` while retaining the normal
channel lifetime and periodic area-status path.

The selected neutral-hero contracts now also cover `ANms` (Mana Shield) at the
central damage boundary, `AHre` (Resurrection) through nearby ordinary friendly-corpse
revival, `ANbf` (Breath of Fire) through point-area damage, `ANdb` (Drunken
Brawler) through the existing critical/evasion hooks, `ANdh` (Drunken Haze) and
`ANdo` (Doom) through timed target buffs, `ANht` (Howl of Terror) through its
authored area buff, and `ANca` (Cleaving Attack) through the attack-hit hook.

The current selected-block implementation also covers `AHfa` (Searing Arrows)
through the missile attack hook, `AEar` (Trueshot Aura) through the ranged
attack bonus hook, `AOre` (Reincarnation) through the unit death/revival
lifecycle, `AOhw` (Healing Wave), `AOhx` (Hex), `AOvd` (Big Bad Voodoo), `AEsv`
(Vengeance), and `ANab` (Acid Bomb). `AOwd` (Serpent Ward) remains unresolved:
the ROC and TFT AbilityData archives contain no `AOwd` row, so no authoritative
summoned unit or level data exists for a faithful registration.

`a_train` exists in `s_train.c`, but training is currently handled by the
generic `Button` command path rather than by a registered ability code.

## Reference Parity List

| Code | Reference behavior | OpenWarcraft3 status | Notes |
|---|---|---|---|
| `AHhb` | Holy Light | Partial | Target selection, range, mana, cooldown, target masks, undead-only enemy damage, healing, and target art exist. Needs richer dispel/damage typing. |
| `AHwe` | Summon Water Elemental | Partial | No-target summon, level unit id, mana/cooldown, ownership, and timed life exist. Needs better placement/art/order polish. |
| `AHbz` | Blizzard | Partial | Point targeting and periodic enemy area damage exist. Needs shard visuals, channel cancellation details, building reduction, and max-damage rules. |
| `AHtb` | Thunder Bolt | Partial | Target selection, projectile, damage, stun, range, mana, cooldown, and target masks exist. Needs better cast animation timing. |
| `ANfb` | Firebolt via Thunder Bolt type | Partial | Shares Thunder Bolt implementation with Fire Bolt data/art. Needs better cast animation timing. |
| `Apxf` | Phoenix Fire | Stub | Registered passive stub. Needs autocast/projectile aura behavior. |
| `AOsf` | Feral Spirit | Partial | Warsmash-style recast replacement, single spawn point in front of the caster using authored Area, level unit id/count, ownership, timed life, summon events, and SpecialArt presentation exist. Summoned-unit classification is not yet exposed as a general WC3 unit-type flag. |
| `AOmi` | Mirror Image | Partial | No-target cast, mana/cooldown, data-defined image count/duration, illusion identity, summon event context, and campaign-script discovery exist. Needs retail damage multipliers, dispel/image shuffle, and richer visual behavior. |
| `Abun` | Burrow cargo hold | Partial | Per-holder capacity/target/range lookup, hidden+paused cargo state, death ejection, empty attack gating, Warsmash cooldown scaling, footprint-aware boarding range, and Battle Stations/Smart boarding exist. While occupied, the Burrow portrait remains on its dedicated layer and capacity-driven clickable cargo slots replace only the ordinary stat subsection. Remaining work is retail verification of health/mana slot decoration and other presentation details. |
| `Abtl` | Battle Stations | Partial | OpenRealm uses its data-driven AoE, busy-unit flag, and allowed unit type to choose the nearest eligible workers up to remaining cargo capacity; selected workers path to the Burrow and load on arrival. The bundled Warsmash source defines the order/error keys but has no `Abtl` implementation, so this auto-call flow is retail/data-derived rather than source-confirmed Warsmash behavior. Needs localized no-Peons command feedback and broader custom-map validation. |
| `Astd` | Stand Down | Partial | Occupied Burrows synthesize the stock Stand Down command even when the unit ability list omits `Astd`; empty Burrows hide it. Activation first applies normal Stop semantics so any persistent Burrow attack/order is retired, then unloads all occupants through the shared unstuck/unpause path. Needs explicit reference-style remembered-resource Back-to-Work state and remaining presentation parity. |
| `AEim` | Immolation | Stub | Toggle status exists; needs mana drain, periodic area damage, and caster buff art/rules. |
| `Aenc` | Entangled mine cargo hold | Partial | Reuses generic cargo capacity/Smart boarding, hides+pauses loaded Wisps, rejects loading while the mine is under construction, and drives first-through-fifth occupancy animation tags. Needs remaining retail presentation/target-error verification. |
| `Aent` | Entangle Gold Mine | Partial | Validates an ordinary live Gold Mine, spawns the authored UnitID overlay at the same location, hides/pauses the parent mine, and starts autonomous Night Elf construction without consuming a Wisp. Needs spell-effect/caster-state polish and localized failure feedback. |
| `Aegm` | Entangled Mine | Partial | Uses `Aenc` cargo occupancy, Warsmash-style round-robin income slots, the parent mine's finite gold pool, direct player income, depletion death, Wisp ejection, and parent restoration. Needs remaining presentation parity. |
| `Aeat` | Eat Tree | Partial | Tree target, self-heal, mana/cooldown, and tree removal exist. Needs rip/eat timing and buff art. |
| `Ambt` | Moon Well | Partial | Manual friendly replenish restores life first and then mana from the well pool using authored DataB/DataA ratios. Needs autocast, night-only regeneration, and water-level presentation. |
| `ANch` | Charm | Partial | Target ownership transfer, range, mana/cooldown, and max-level gate exist. Needs full target restrictions/order cleanup. |
| `AIco` | Item command using Charm behavior | Partial | Shares Charm handler; inventory alias-to-base dispatch is wired. |
| `AHca` | Cold Arrows | TODO | Needs autocast/toggle projectile modifier and slow buff. |
| `ANfl` | Forked Lightning | Partial | Unit-target bounce spell; starts at the selected unit, applies constant authored `DataA` damage to up to `DataB` alive enemy targets, and selects subsequent unvisited targets within `Area`. Projectile presentation and exact retail target ordering remain. The test fixture marks synthetic targets with `SVF_MONSTER`. |
| `Agld` | Gold Mine | Partial | Per-mine `Agld`-derived capacity/duration/max-gold, finite depletion, waiting workers, inside-miner protection, partial final trips, WORK occupancy animation, and shared Haunted/Entangled parent-resource ownership are implemented. Remaining gaps are mostly feedback/presentation parity. |
| `Agl2` | Overlayed Gold Mine | Partial | Shared parent-mine relationship keeps the underlying `Agld` entity as the sole finite resource pool, hides/pauses it while overlaid, and restores it on overlay death/removal. Exact visual/effect parity remains. |
| `Abgm` | Blighted Gold Mine | Partial | Build-on-mine overlays retain the parent resource pool; fixed Acolyte ring slots, miner-count-dependent direct income, teardown, finite-gold deduction, and Warsmash-style persistent `EffectArt` at each ring slot are implemented. Exact retail effect/model presentation beyond those authored effects remains. |
| `Abli` | Blight | TODO | Needs blight placement/spread and terrain interaction. |
| `Aaha` | Acolyte Harvest | Partial | Gather/Smart orders walk to an owned completed Haunted Mine, claim the nearest free authored ring slot, remain visible in Stand Work, unregister on retask/death/removal, and use Warcraft `CommandStrings` keys for wrong-owner/wrong-target/full-ring feedback. Remaining gaps are minor presentation/edge-case parity. |
| `Artn` | Return Resources | Partial | Drop-off eligibility is ability-driven (`Argd`/`Arlm`/`Argl` plus `Artn`-derived data), with nearest compatible selection, destroyed-target retargeting, explicit Smart-click return, and no-target return through the worker Harvest command; carried-resource state still lives in the harvest state machines. |
| `Ahar` | Harvest | Partial | Human worker harvest/return gameplay exists, including capacity clamping and same-forest resume after a dead remembered tree, but not as the same split data model as the ability data; command-card art/text does not yet model the carried-state presentation separately. |
| `Awha` | Wisp Harvest | TODO | Needs wisp-specific gather behavior. |
| `Ahrl` | Harvest Lumber | Partial | Shares the lumber harvest/return state machine with `Ahar`, but remains lumber-only: Smart, the Harvest command, and `autoharvestlumber` target trees; `DataA`/`DataB`/`Rng`/`Dur` are resolved from the worker's actual `Ahrl` (including derived aliases), and gold mining is rejected. Remaining parity is mostly exact target-error/presentation behavior and Undead tree-blight marking. |
| `ANcl` | Channel test | Stub | Opens cancel mode as a generic channel scaffold. |
| `AUcs` | Carrion Swarm dummy | Partial | Simple point-area enemy damage exists. Needs missile/line travel and art. |
| `AInv` | Inventory | Partial | Inventory storage and item use exist, but no `AInv` ability type/capacity/drop rules. |
| `Arep` | Human Repair | Partial | Entity targeting, Smart Repair, Shift target order, ranged approach, DataA/DataB completed repair, paused Human DataC/DataD power building, right-click toggle, and nearest-valid Auto Repair are implemented. Needs full target masks, naval bonus, and destructibles. |
| `Aren` | Repair | Partial | Shares completed-building Repair command/range/cost behavior, Shift target order, and nearest-valid Auto Repair; rejects construction. Full target masks/destructibles/naval remain. |
| `Arst` | Repair | Partial | Shares completed-building Repair command/range/cost behavior, Shift target order, and nearest-valid Auto Repair; rejects construction. Full target masks/destructibles/naval remain. |
| `Avul` | Invulnerable | Partial | Units with `Avul` spawn with damage immunity; damage guard is tested. Needs targetability/UI status polish. |
| `Apit` | Shop Purchase Item | Partial | `Sellitems` item buttons, authoritative item purchase, local-player resource charging, inventory handoff, shared item stock/restock, and pawnable sell-back are wired through the neutral-shop subsystem. The Apit ability procedure itself remains a marker; power-up acquisition and JASS stock mutation remain. |
| `Aneu` | Neutral Building | Partial | Neutral item shops expose a command card without granting control, use Aneu/Aall DataA as activation radius, deterministically choose a nearby owned Hero/inventory unit as patron, and present that patron's inventory. Explicit neutralinteract selection button/indicator and persistent per-player patron state remain. |
| `Aall` | Shop Sharing | TODO | Allied/shared neutral-building policy is not implemented; current patron resolution is intentionally local-player-owned only. |
| `Acoi` | Couple Instant | TODO | Test/special ability; low priority unless map data requires it. |
| `AIhe` | Item Heal | Partial | Inventory dispatch, selected-unit heal, target art and synchronous charge/consume rules exist. Shared item cooldown groups remain. |
| `AIma` | Item Mana Regain | Partial | Inventory dispatch, selected-unit mana restore, target art and synchronous charge/consume rules exist. Shared item cooldown groups remain. |
| `AIda` | Item Defense AOE | Partial | Authored area/duration/target mask applies `Bdef`; live status contributes its authored armor bonus to combat/HUD. Persistent buff-world-art ownership and shared item cooldown groups remain. |
| `AIat` | Item Attack Bonus | Partial | Passive item bonus updates temporary Attack 1/2 damage and HUD modifier; broader stacking/modifier framework remains. |
| `AIab` | Item Stat Bonus | TODO | Needs hero stat modifier system. |
| `AIim` | Permanent Intelligence Gain | TODO | Needs permanent hero stat updates. |
| `AIsm` | Permanent Strength Gain | TODO | Needs permanent hero stat updates. |
| `AIam` | Permanent Agility Gain | TODO | Needs permanent hero stat updates. |
| `AIxm` | Permanent Multi-stat Gain | TODO | Needs permanent hero stat updates. |
| `AIde` | Item Defense Bonus | Partial | Passive item armor modifier applies on inventory acquisition and reverses on removal, using the shared temporary armor bonus preserved across Hero Agility recomputation; this matches Warsmash Ring-of-Protection-style add/remove semantics. Broader armor modifier framework remains. |
| `AIml` | Item Life Bonus | TODO | Needs max-health modifier system. |
| `AImm` | Item Mana Bonus | TODO | Needs max-mana modifier system. |
| `AIfs` | Figurine Summon | TODO | Needs item summon behavior. |
| `AImi` | Permanent Life Gain | Partial | Selected-unit max-health/current-health gain plus synchronous charge/consume rules exist. Shared item cooldown groups remain. |
| `AIem` | Experience Gain | Implemented | Grants Data A XP through the shared Hero progression path and plays the target effect. |
| `AIlm` | Level Gain | Implemented | Converts Data A level gain to the target XP threshold and uses the shared Hero progression path. |
| `Acar` | Cargo Hold | Partial | Per-holder capacity, real-unit cargo storage, hidden+paused occupants, unload/death ejection, and loaded-state JASS queries exist. Needs mobile walk-into-range behavior and cargo HUD slots. |
| `Aloa` | Load | Partial | Same-owner, capacity, target-mask, allowed-unit-type, already-loaded, and footprint-aware range validation feed real cargo state. Smart boarding now walks compatible units into range; explicit mobile transport targeting still needs broader compatibility work and command errors. |
| `Adro` | Drop | Partial | Drops one occupant through shared unstuck/unhide/unpause cargo removal. Needs authored point semantics and cargo UI integration. |
| `Adri` | Drop Instant | Partial | Shares the current one-occupant drop path. Needs exact reference targeting/presentation semantics. |
| `Aroo` | Root | Partial | Basic rooted movement toggle exists. Needs alternate unit transform and build/move interaction. |

## Suggested Port Order

1. Finish the common spell plumbing: ability level data, mana/cooldown checks,
   target filters, point/unit/no-target command flows, and buff/status storage.
2. Complete one targeted spell end to end, preferably `AHtb` Thunder Bolt, then
   reuse that path for `ANfb`.
3. Bring `AHhb` to real behavior, replacing the current visual-only local
   implementation with target validation and healing.
4. Add no-target summon spells (`AHwe`, `AOsf`) once timed-life units are
   available.
5. Finish Repair target-category coverage and destructible/naval behavior, expand generic autocast beyond Repair's nearest-valid policy, and then generalize the remaining Wisp harvest variant (`Awha`).
6. Defer cargo, shops, inventory item modifiers, root/entangle mine, and passive
   autocast abilities until the underlying status, item, and transform systems
   exist.
