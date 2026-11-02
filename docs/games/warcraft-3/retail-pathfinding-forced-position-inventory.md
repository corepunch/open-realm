# Forced-position, teleport and pathing-bypass entry points (BASE-01.4)

BASE-01.4 is complete as an inventory: **71 entry-point rows** cover every direct
caller of the 16 recovered position, placement and pathing-profile sinks and every
widget/unit vtable slot `d8`/`dc`/`180` (plus `154`/`15c`/`1ac`) indirect call
site, walked to its public producer. The closed site universe has **303 original
call sites**: 165 are owned by rows and 138 are explicit exclusions (UI/glue
receivers, non-widget classes, missiles). Each row ID (`B014-NN`) is the
follow-up identifier for its path; no TODO leaf is added.

| Status | Rows |
|---|---|
| covered | 8 |
| partial | 25 |
| uncovered | 37 |
| unreachable | 1 (`B014-25`, no code or data references) |

Every partial or uncovered row is owned by one existing open leaf: ORDER-01.13
(23), SEP-01.2 (10), ORDER-01.12 (8), FOOT-04.2 (7), GROUP-03.4.7.3 (7),
ORDER-01.16 (4), TARGET-01.2 (2) and BASE-03.1 (1). Closing this inventory does
not implement or verify those paths. The three corpus exclusions BASE-01.4 owned
are reassigned to FOOT-04.2 (Stop285 cargo/dead/form producers), ORDER-01.13
(Blink stand-ins) and TARGET-01.2 (Way Gate threshold stand-ins).

The [inventory](../../../tools/ghidra/fixtures/retail-pathfinding-forced-position-inventory-1.27.json)
keeps each row's producer, native chain, sink, call sites, evidence, engine
status, owner and rationale, plus all 303 call-site instruction guards. Validate:

```sh
python3 tools/ghidra/audit_wc3_pathing_inventory.py \
  tools/ghidra/fixtures/retail-pathfinding-forced-position-inventory-1.27.json \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll
python3 -m unittest discover -s tests -p test_wc3_pathfinding_inventory_audit.py
```

The audit requires every enumerated site to belong to a row or one exclusion,
resolvable oracle/doc/closed-task evidence on covered rows, a static proof for
unreachable rows and a remaining open owner for every other row; with the
binary it re-checks every guarded instruction byte. Enumeration queries,
generators and cached Ghidra responses are archived under
`/GitHub/wc3-analysis/runtime/claude-base014/`. Ghidra names the shared helpers
`Teleport_PlaceUnitAtPoint` (`439490`), `Unit_PlaceAtPointWithFallback`
(`3f9c10`) and `Cargo_UnloadPassengerAtPoint` (`58d610`).

## Covered paths

| Row | Path |
|---|---|
| B014-01 | SetUnitPosition / SetUnitPositionLoc. Stop is admitted before the vslot180 placement (Payoff285/308/309/311). |
| B014-03 | SetUnitX / SetUnitY (BASE-01.5). |
| B014-04 | SetUnitPathing (live pathing-toggle contracts). |
| B014-10 | Public CreateUnit family constructor placement. |
| B014-12 | Map-load authored widget creation. |
| B014-13 | Item creation (Payoff134). |
| B014-21 | Way Gate traversal exit commit. |
| B014-22 | WaygateActivate. |

## Notable findings

* **Vtable slots differ from earlier notes.**
  - Widget_WritePositionAfterAdmission (653510) is vslot **0xd8** in all six
    widget vtables (CWidget, CSelectable, CItem, CDestructable, CCaptainAI,
    CUnit). Data ref 6fb77f88 is CUnit+0xd8, not +0x180.
  - Admission is vslot **0xdc**, not 0x184.
  - vslot 0x180 is SetPosition: 698050 on CUnit, 654d60 on
    CSelectable/CItem/CDestructable. The vslot ABI arities are 180 = 10 args,
    d8/dc = 11 args, 154 = 0 args.
* **Shared teleport helper.** `439490 -> 3f9c10` (admit at point, fallback
  point, vslot180 write, TargetLost dispatch, optional Stop) is shared by:
  - Mass Teleport, Item Teleport/Town Portal, Item Recall, Dark Summoning
  - Sanctuary/Preservation, Decouple
  - all morph form landings (CAbilityMorph vslot304 -> 546420 across 13 form
    classes)
  - transport unload fallbacks

  None of these placement results has retail evidence. Item Teleport has
  lifecycle captures only (audit327).
* **Unported abilities.** Item Recall (AIrt), Dark Summoning (AUds),
  Sanctuary/Preservation (ANsa/ANpr) and Tavern Awaken have no engine port
  (`skills/s_skills.c` TODO entries).
* **Hidden pathing bypasses.**
  - Gold-mine harvest workers switch to a ghost/bit10 profile and re-place
    neighbours (B014-37).
  - Ghost, Locust, Land Mine, Tornado, Wind Walk and forced-ground
    (Ensnare/Web/Roots) all publish CUnit vslot15c profiles without evidence.
  - AI `DisablePathing` sets Captain+6c bit4000. Its consumer was not located,
    and the engine stores the flag without using it.
* **Captain.** CaptainAI_UpdateMemberOrders also snaps the virtual actor onto
  a member pose (6f9d20f6, B014-17). This is an undocumented Captain teleport.
* **Engine divergence.** The engine's transport unload uses public
  `S_SetUnitPosition`, which issues Stop (`skills/s_cargo.c:252`). Retail uses
  the internal vslot180 path through `58d610`/`3f9c10` (B014-41).

## Ambiguous or bounded sites

* **6f5d6046** (Drunken Haze, vslotdc): the push count equals widget dc
  arity. It is excluded only on receiver evidence (factory missile class
  0x4d4e6468).
* **6f4b9b79** (Acolyte): the decompiler shows 5 arguments, but the
  disassembly shows 10. It is kept as a unit placement.
* **6f656342** (vslot1ac on item-owned identities): receiver class was
  inferred from layout, so it is excluded with lower confidence.
* **6f4dcb20**: referenced from CAbilityAuraPlague vslot284, but calls a
  helper named CAbilityChaosBase_*. The producer is taken to be the Aap1
  death spawn.
* **UI exclusions** (d8/dc/154/15c): based on module range, CFrame/glue caller
  vtables and 0–2 argument calls (see `pushcount.txt`).
* **Unenumerated virtuals:** callers of CUnit vslot80 (RestoreWorldPresence),
  CItem vslot40 and CUnit vslot16c were not enumerated. They are owned by
  BASE-03.1 as B014-71.

## Rows

| ID | kind | producer | call site | status | owner |
|---|---|---|---|---|---|
| B014-01 | forced-position | JASS SetUnitPosition / SetUnitPositionLoc (registered 6f20942f / 6f209443) | 6f21560a | covered | - |
| B014-02 | forced-position | JASS SetUnitPosition on structures, loaded cargo, dead/removed or support-rejected units ( | 6f21560a | uncovered | FOOT-04.2 |
| B014-03 | forced-position | JASS SetUnitX / SetUnitY (registered 6f209407 / 6f20941b) | 6f215951 | covered | - |
| B014-04 | pathing-bypass | JASS SetUnitPathing (registered 6f2096c3) | 6f215596 | covered | - |
| B014-05 | forced-position | JASS SetItemPosition (registered 6f20a2a3) | 6f2139a9 | partial | FOOT-04.2 |
| B014-06 | forced-position | JASS UnitRemoveItem / UnitRemoveItemFromSlot / UnitAddItem (carrier transfer) item publica | 6f694dab | partial | FOOT-04.2 |
| B014-07 | forced-position | CAbilityInventory internal item drops/handoffs (drop-at-point order, death/owner drops, pa | 6f540d09 | uncovered | ORDER-01.13 |
| B014-08 | forced-position | JASS ReviveHero / ReviveHeroLoc (registered 6f20987b / 6f20988f) | 6f695ba2 | partial | FOOT-04.2 |
| B014-09 | forced-position | JASS ShowUnit(true): CUnit vslot80 Unit_RestoreWorldPresence re-admits the current pose | 6f699d63 | partial | FOOT-04.2 |
| B014-10 | forced-position | JASS CreateUnit / CreateUnitByName / CreateUnitAtLoc / CreateUnitAtLocByName creation plac | 6f677aee | covered | - |
| B014-11 | forced-position | Other public creation natives: CreateCorpse, CreateBlightedGoldmine, PlaceRandomUnit, Rest | 6f1fb52b | partial | FOOT-04.2 |
| B014-12 | forced-position | Map-load/preplaced widget creation (W3U/DOO units, items, destructables) and world-start p | 6f6c20b4 | covered | - |
| B014-13 | forced-position | JASS CreateItem / PlaceRandomItem and internal item spawns (CItem vslot1ac constructor) | 6f65784b | covered | - |
| B014-14 | teleport | AI native TeleportCaptain (registered 6f9be33e) | 6f9d7aee | partial | GROUP-03.4.7.3 |
| B014-15 | teleport | AI native ResetCaptainLocs (registered 6f9be302) | 6f9d7aee | partial | GROUP-03.4.7.3 |
| B014-16 | teleport | Captain virtual actor placement: CaptainGoHome when roster empty/state1, CreateCaptains ac | 6f9d6f03 | partial | GROUP-03.4.7.3 |
| B014-17 | teleport | CaptainAI_UpdateMemberOrders snaps the virtual Captain actor onto a member pose | 6f9d20f6 | uncovered | GROUP-03.4.7.3 |
| B014-18 | teleport | Cargo passenger return recenters the retained attack Captain (CargoHold/Bunker/EntangleCar | 6f9d05bc | partial | GROUP-03.4.7.3 |
| B014-19 | pathing-bypass | AI native DisablePathing (registered 6f9be4f6): sets attack Captain flags6c bit4000 | 6f9b984c | uncovered | GROUP-03.4.7.3 |
| B014-20 | pathing-bypass | Captain virtual actor movement-type / pathing profile publication (radius0 actor, category | 6f9d3066 | partial | GROUP-03.4.7.3 |
| B014-21 | teleport | Way Gate traversal: accelerated route consumes a warp node and commits the portal exit | 6f165d79 | covered | - |
| B014-22 | teleport | JASS WaygateActivate (registered 6f20a0eb) | 6f219410 | covered | - |
| B014-23 | teleport | JASS WaygateSetDestination (registered 6f20a0d7) | 6f219533 | partial | TARGET-01.2 |
| B014-24 | pathing-bypass | MoveRequest_SetFormationBypass producers: zero/sentinel range target/point starts, facing  | 6f05c1b0 | partial | TARGET-01.2 |
| B014-25 | pathing-bypass | FUN_6f89ccc0 formation-bypass clear (no references) | 6f89ccd3 | unreachable | - |
| B014-26 | forced-position | Root (Aroo) placed point order: approach, facing, relocation then rooted footprint displac | 6f438c19 | partial | SEP-01.2 |
| B014-27 | forced-position | Burrow / Submerge reverse (unburrow, surface) re-placement (CAbilityBurrow/CAbilitySubmerg | 6f4c863a | partial | SEP-01.2 |
| B014-28 | pathing-bypass | Burrow / Submerge pathing category overrides (AbilityBurrow_SetUnitCategory / AbilitySubme | 6f4ba912 | partial | SEP-01.2 |
| B014-29 | forced-position | Morph landing/takeoff and form-change placement (CAbilityMorph family vslot304 -> 546420,  | 6f3f9d8e | partial | SEP-01.2 |
| B014-30 | pathing-bypass | Type rebind republishes pathing profile (Chaos CAbilityChaos_CommitUnitType, morph rebind  | 6f670e2f | partial | SEP-01.2 |
| B014-31 | pathing-bypass | Forced-ground mask overrides: Ensnare/Web/Entangling Roots buffs and morph detach (CUnit_P | 6f687810 | uncovered | SEP-01.2 |
| B014-32 | pathing-bypass | Ghost (Agho) pathing category override (AbilityGhost_SetUnitCategory) | 6f623c12 | uncovered | SEP-01.2 |
| B014-33 | pathing-bypass | Locust (Aloc) clear and Land Mine (Amin) publish pathing profile | 6f63c44b | uncovered | SEP-01.2 |
| B014-34 | pathing-bypass | Tornado wander pathing profile (CAbilityTornadoWander_PublishPathingProfile) | 6f59ce12 | uncovered | SEP-01.2 |
| B014-35 | pathing-bypass | Wind Walk (AOwk) buff apply/remove pathing profile (CBuffWindWalk vslots e0/e4 -> 580250) | 6f58028e | uncovered | SEP-01.2 |
| B014-36 | forced-position | Wind Walk end re-admission at current pose (CBuffWindWalk vslot33c) | 6f58e1c7 | uncovered | ORDER-01.13 |
| B014-37 | pathing-bypass | Gold-mine Harvest/Return worker ghost profile and neighbour re-placement (CAbilityHarvestR | 6f4278ba | uncovered | ORDER-01.12 |
| B014-38 | forced-position | Possessed/Blighted/Entangled gold-mine worker exit placement (vslotc dispatch 4e3420 -> 4c | 6f4c8930 | uncovered | ORDER-01.12 |
| B014-39 | forced-position | Acolyte Haunted Mine slot placement (CAbilityAcolyteHarvest vslotc -> 438190 -> 4b9910) | 6f4b9b79 | uncovered | ORDER-01.12 |
| B014-40 | forced-position | Wisp harvest tree enter/leave placement and pathing profile (CAbilityWispHarvest) | 6f5af037 | uncovered | ORDER-01.12 |
| B014-41 | forced-position | Transport unload at point / instant unload / stand down / corpse drop (Adro/Adri/Astd/Amed | 6f58d7b4 | partial | ORDER-01.12 |
| B014-42 | forced-position | Cargo hold unload-all on carrier death/removal (CargoHold/Bunker/EntangleCargo/DevourCargo | 6f59b35c | uncovered | ORDER-01.12 |
| B014-43 | forced-position | Devour cargo release (CAbilityDevourCargo vslot310) | 6f5e636e | uncovered | ORDER-01.12 |
| B014-44 | forced-position | Meat Wagon stored-corpse release for Cannibalize/Raise Dead/Animate Dead/Carrion Scarabs ( | 6f58d7b4 | uncovered | ORDER-01.12 |
| B014-45 | forced-position | Inside construction (Orc/Night Elf/Naga/Neutral Build vslot33c) worker absence placement a | 6f544066 | partial | ORDER-01.16 |
| B014-46 | forced-position | Human Build relocates the builder before prepending Repair (HumanBuild_PrependRepairWorkOr | 6f5440f5 | partial | ORDER-01.16 |
| B014-47 | forced-position | Undead Build unit work wait relocation (UndeadBuild_BeginUnitWorkWait) | 6f544428 | partial | ORDER-01.16 |
| B014-48 | forced-position | Structure creation by Build (Build_CreateStructureAtTarget / FromPointTask) and item-built | 6f54db2f | partial | ORDER-01.16 |
| B014-49 | forced-position | Widget footprint (re)application displaces occupants (Widget_ReapplyPathingAndDisplace vsl | 6f3f8a1a | partial | FOOT-04.2 |
| B014-50 | forced-position | Altar queued Hero revival placement (CAbilityQueue vslotc -> 40daa0); trained-unit exit us | 6f40de9f | uncovered | ORDER-01.13 |
| B014-51 | forced-position | CAbilityRevive Hero revival placement (vslotc -> 4250a0 -> 402160) | 6f402256 | uncovered | ORDER-01.13 |
| B014-52 | forced-position | Tavern instant Hero awaken placement (CAbilityAwaken vslotc -> 423a50 -> 3f8350) | 6f3f8443 | uncovered | ORDER-01.13 |
| B014-53 | teleport | Mass Teleport (AHmt, CAbilityMassTeleport vslot39c -> 40d760 -> 40af60 -> 439490 -> 3f9c10 | 6f3f9d8e | uncovered | ORDER-01.13 |
| B014-54 | teleport | Staff of Teleportation / Scroll of Town Portal (AIte/AItp, CAbilityItemTeleport/ItemTownPo | 6f3f9d8e | partial | ORDER-01.13 |
| B014-55 | teleport | Item Recall (AIrt, CAbilityItemRecall vslot418 -> 408580 -> 429e10 -> 439490) | 6f3f9d8e | uncovered | ORDER-01.13 |
| B014-56 | teleport | Dark Summoning (AUds, CAbilityDarkSummoning vslot39c -> 40d590 -> 40ab70 admission then 43 | 6f3f9d8e | uncovered | ORDER-01.13 |
| B014-57 | teleport | Staff of Sanctuary / Staff of Preservation (ANsa/ANpr vslot424 -> 6308a0/630760 -> 439490) | 6f3f9d8e | uncovered | ORDER-01.13 |
| B014-58 | teleport | Couple/Decouple (Archer/Hippogryph mount, Adec/Acoh/Acoa): Decouple vslot304 -> 4be9f0 ->  | 6f3f9d8e | uncovered | ORDER-01.13 |
| B014-59 | teleport | Blink (AEbl, CAbilityBlink vslot418 CAbilityBlink_MoveCaster; target validation 4ccd90 via | 6f4c961c | partial | ORDER-01.13 |
| B014-60 | forced-position | Animate Dead / Resurrection (incl. creep/item variants) revived-unit placement (vslot438 - | 6f4e67d8 | uncovered | ORDER-01.13 |
| B014-61 | forced-position | Reincarnation (AOre/ANrn, CEffectReincarnation vslot80 -> 50e870) rebirth placement | 6f50e912 | uncovered | ORDER-01.13 |
| B014-62 | forced-position | Soul Gem release (Asou, CAbilitySoulTrapped vslota4 -> 500030) | 6f500106 | uncovered | ORDER-01.13 |
| B014-63 | forced-position | Unstable Concoction impact (CMissileUnstableConcoction vslotbc -> 508da0) | 6f508f54 | uncovered | ORDER-01.13 |
| B014-64 | forced-position | Graveyard corpse creation/placement (Agyd, CAbilityGraveyard vslotc -> 5ef820 -> 5d0190) | 6f5d03df | uncovered | ORDER-01.13 |
| B014-65 | forced-position | Storm, Earth and Fire (ANef, CAbilityElementalFury / CBuffElementalFury) hide/restore cast | 6f5ea6b7 | uncovered | ORDER-01.13 |
| B014-66 | forced-position | Ensnare/Web missile landing relocates the target (CMissileEnsnare/CMissileWeb vslotcc -> 5 | 6f5f96d6 | uncovered | ORDER-01.13 |
| B014-67 | forced-position | Infernal summons (Inferno AUin / Dreadlord Inferno / Rain of Chaos ANrc) creation, landing | 6f62af15 | uncovered | ORDER-01.13 |
| B014-68 | teleport | Mirror Image (AOmi) caster/illusion swap and illusion creation (Unit_CreateIllusionCopy, W | 6f6415a8 | uncovered | ORDER-01.13 |
| B014-69 | forced-position | Disease Cloud on death (Aap1, CAbilityAuraPlague vslot284 -> 4dc9d0) spawned unit placemen | 6f4dcb20 | uncovered | ORDER-01.13 |
| B014-70 | forced-position | Other ability/summon unit-creation producers reaching the constructor (677870 callers): Se | 6f600f02 | uncovered | ORDER-01.13 |
| B014-71 | forced-position | Unexpanded virtual producers: CUnit vslot80 Unit_RestoreWorldPresence callers other than S | 6f699d63 | uncovered | BASE-03.1 |
