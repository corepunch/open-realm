# Reachable function and branch inventory (BASE-03.1)

**Repair after the 2026-10-20 closure audit:** the version2 fixture now includes
all **21,306 individual conditional jumps** from **7,380 internal functions**;
19 import leaves have no instructions. Each branch records its function, address,
exact instruction bytes, mnemonic, taken edge, fallthrough and remaining behavior
task. Every branch is assigned to its function's open owner; none is certified as
executed or unreachable by a function-level label. BASE-03.1's inventory criterion
passes integration acceptance (9,974 native tests and2,912 Python evidence tests,
actual full-checkpoint exit0). All linked behavior remains in the open backlog.

The former function-only fixture could not establish closure: its validator
checked totals and function owners, and the metrics source truncated function
`6f4725f0`. The complete exporter now emits all 462 branches of that function.
Failing-first tests reproduced acceptance of missing/duplicate branches, altered
instructions/edges/functions, incorrect task links and inherited function oracle
claims. The repaired validator rejects them, as well as altered export identity
or bytes. The old function-only schema still returns `closure_verified: false`
and cannot close the leaf. **Inventory closure never certifies engine parity.**

The reachable function inventory records: the direct-call and resolved-virtual
closure of the **163 BASE-01 entry points** (player UI, JASS, Captain AI, target,
axis/forced-position roots and all BASE-01.4 native entries) contains **7,399
functions with 21,306 conditional branches**. Every function has one status and
one owner: 639 covered (an oracle guards or executes an address inside the body),
605 partial, 5,678 uncovered, 475 excluded out-of-scope categories and 2
documented unreachable bodies. Branch linkage is explicit: 2,939 branches lie
inside oracle instruction-guard ranges. 4,456 computed call sites whose receiver
class cannot be derived statically are E2E-05.2 rows, so the reachable set is a
lower bound for indirect dispatch. No TODO leaf is added.

Every row other inventories had assigned to BASE-03.1 is reassigned
from the reachable evidence: 309 virtual slots, 58 globals and 132 fields of the
[field inventory](retail-pathfinding-fields-inventory.md), 98 corpus exclusions,
12 arithmetic rows and BASE-01.4 row B014-71. Validate:

```sh
python3 tools/ghidra/audit_wc3_pathing_reachable.py --binary /path/to/game.dll --report /tmp/reachable.json
python3 -m unittest discover -s tests -p test_wc3_pathfinding_reachable_inventory.py
```

The verifier requires unique rows, a guarding oracle for covered functions, an
out-of-scope category for excluded ones, open owners for every row and unresolved
site, every BASE-01.4 native entry among the roots (one registered AI native
without a Ghidra function is an owned `undefined_roots` entry) and branch totals
equal to the rows. Version2 additionally matches every individual branch to the
complete export, independently decodes its x86 conditional jump edges, verifies
per-function completeness (including zero-branch functions), and checks each
open task link. `--binary` verifies every branch instruction against the pinned
retail DLL. Generators, the exported call graph and cached responses are
archived under `/GitHub/wc3-analysis/runtime/claude-base031/`.

## Totals

- Roots: 167 (BASE-01.1:6, BASE-01.2:9, BASE-01.3:6, BASE-01.5:2, BASE-01.6:2, BASE-01.4:142).
- Reachable functions: **7399**; conditional branches **21306** (in scope 19872); instructions 293602.
- Branches inside oracle instruction-guarded ranges: **2939** (14.8% of in-scope branches; 86.6% of branches in covered functions).
- Computed call sites in expanded functions: 5490; resolved R2 668, R3 337, R1 import 23, R1g static pointer 6; unresolved 4456.

## By status

| status | functions | branches | guarded branches | instructions |
|---|---:|---:|---:|---:|
| covered | 639 | 3394 | 2939 | 44996 |
| excluded | 475 | 1434 | 0 | 15760 |
| partial | 605 | 3706 | 0 | 45518 |
| uncovered | 5678 | 12765 | 0 | 187162 |
| unreachable | 2 | 7 | 0 | 166 |

Status rules: **covered**: at least one corpus oracle has an instruction guard (fixture instructions map) or a verifier-script address constant (executed entry, hook or explicit guard) inside the function body; **partial**: MapPathfinding row, Ghidra plate comment, or an address inside the body cited by a non-inventory oracle fixture (<=3000 cited addresses; e.g. caller-return stacks), but no guard/script address; **unreachable**: row/plate documents producer unreachability and no oracle address lies inside the body; **uncovered**: in-scope (not excluded) with no row, plate or oracle address; **excluded**: excluded category (crt/import/render/sound/ui/net-transport/os-io/jass-vm/infra-alloc) and no row or plate; terminal

## By owner (open leaves only, at publication)

| owner | functions | branches | guarded | covered | partial | uncovered | excluded | unreachable |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| ORDER-01.13 | 2841 | 7003 | 476 | 67 | 125 | 2649 | 0 | 0 |
| FOOT-04.2 | 1176 | 3805 | 233 | 65 | 59 | 1052 | 0 | 0 |
| SEP-01.2 | 871 | 1666 | 464 | 88 | 73 | 710 | 0 | 0 |
| E2E-05.1 | 537 | 1763 | 112 | 20 | 42 | 0 | 475 | 0 |
| TARGET-01.2 | 301 | 954 | 268 | 56 | 70 | 175 | 0 | 0 |
| ORDER-01.12 | 300 | 1037 | 13 | 9 | 12 | 279 | 0 | 0 |
| GROUP-03.2 | 299 | 894 | 317 | 69 | 31 | 199 | 0 | 0 |
| ORDER-01.16 | 254 | 934 | 62 | 28 | 49 | 176 | 0 | 1 |
| GROUP-03.4.7.3 | 251 | 1460 | 483 | 57 | 41 | 152 | 0 | 1 |
| GROUP-04.6 | 122 | 451 | 236 | 61 | 24 | 37 | 0 | 0 |
| GROUP-03.4 | 94 | 132 | 22 | 12 | 21 | 61 | 0 | 0 |
| E2E-05.2 | 78 | 277 | 0 | 0 | 2 | 76 | 0 | 0 |
| ORDER-02.2 | 67 | 295 | 30 | 4 | 9 | 54 | 0 | 0 |
| MOVE-01.1 | 64 | 209 | 42 | 24 | 23 | 17 | 0 | 0 |
| NUM-03.2 | 53 | 210 | 131 | 34 | 6 | 13 | 0 | 0 |
| E2E-06.1 | 36 | 95 | 12 | 20 | 5 | 11 | 0 | 0 |
| NUM-01.2 | 29 | 66 | 9 | 5 | 9 | 15 | 0 | 0 |
| BASE-02.3 | 13 | 29 | 23 | 8 | 4 | 1 | 0 | 0 |
| NUM-01.12 | 8 | 25 | 5 | 8 | 0 | 0 | 0 | 0 |
| NUM-02.3 | 5 | 1 | 1 | 4 | 0 | 1 | 0 | 0 |

Owner rule order: open leaf most cited by the evidence text/oracle tasks -> BASE-01.4 forced-position row owner -> name/RTTI-class keyword table -> owner of the BFS predecessor (shortest-path caller) -> E2E-05.2. Excluded rows -> E2E-05.1 (it emits the excluded-branch list). Inherited owners are routing hints, not behavioural claims.

## By category

| category | functions | branches | statuses |
|---|---:|---:|---|
| game | 6618 | 18626 | uncovered 5447, covered 611, partial 558, unreachable 2 |
| unknown | 244 | 917 | uncovered 231, covered 8, partial 5 |
| ui | 215 | 795 | excluded 208, partial 5, covered 2 |
| render | 156 | 539 | excluded 135, covered 14, partial 7 |
| crt | 43 | 19 | excluded 32, partial 8, covered 3 |
| os-io | 38 | 71 | excluded 37, partial 1 |
| sound | 36 | 231 | excluded 18, partial 18 |
| infra-alloc | 20 | 96 | excluded 17, partial 2, covered 1 |
| import | 19 | 0 | excluded 19 |
| net-transport | 7 | 12 | excluded 6, partial 1 |
| jass-vm | 3 | 0 | excluded 3 |

Excluded categories (terminal, owner E2E-05.1): `crt` C runtime / compiler support (memcpy, float conversion, exception/SEH, libm); `import` external DLL import (Storm/Kernel32/Miles/etc.); not game.dll code; `render` rendering/model/sprite/texture/lighting/weather presentation; `sound` sound/audio presentation; `ui` UI frames, menus, tooltips, glue screens and HUD drawing; `net-transport` network transport/Battle.net/Warden below the decoded order packet; `os-io` OS, file, preference, profile, archive and data-file IO; `jass-vm` JASS compiler/interpreter internals (natives are separate in-scope entries); `infra-alloc` generic allocator/string/hash/container infrastructure without game semantics. A function with a MapPathfinding row or plate is never excluded.

## Top uncovered high-branch functions

| address | name | branches | instr | depth | category | owner | example root |
|---|---|---:|---:|---:|---|---|---|
| 6f24de40 | FUN_6f24de40 | 56 | 779 | 3 | game | FOOT-04.2 | 6f1fb4f0 |
| 6f5d5540 | FUN_6f5d5540 | 53 | 692 | 5 | game | ORDER-01.13 | 6f642aa0 |
| 6f6c9ef0 | FUN_6f6c9ef0 | 52 | 496 | 2 | game | ORDER-01.13 | 6f210f90 |
| 6f402850 | FUN_6f402850 | 45 | 313 | 3 | game | ORDER-01.12 | 6f642aa0 |
| 6f252340 | FUN_6f252340 | 43 | 652 | 1 | game | FOOT-04.2 | 6f1fb4f0 |
| 6f697a10 | FUN_6f697a10 | 43 | 339 | 3 | game | GROUP-03.2 | 6f210f90 |
| 6f3fe7c0 | FUN_6f3fe7c0 | 36 | 265 | 1 | game | ORDER-01.12 | 6f425280 |
| 6f528110 | FUN_6f528110 | 36 | 344 | 2 | game | ORDER-01.13 | 6f1fb4f0 |
| 6f69e1c0 | FUN_6f69e1c0 | 35 | 205 | 3 | unknown | TARGET-01.2 | 6f1fb4f0 |
| 6f48d3d0 | FUN_6f48d3d0 | 35 | 207 | 6 | game | ORDER-01.13 | 6f642aa0 |
| 6f655f20 | FUN_6f655f20 | 32 | 276 | 5 | game | ORDER-01.13 | 6f642aa0 |
| 6f651c40 | FUN_6f651c40 | 30 | 235 | 1 | game | FOOT-04.2 | 6f215b00 |
| 6f212e10 | FUN_6f212e10 | 30 | 285 | 2 | game | FOOT-04.2 | 6f1fb4f0 |
| 6f6d6920 | FUN_6f6d6920 | 30 | 238 | 6 | game | ORDER-01.13 | 6f642aa0 |
| 6f73a380 | FUN_6f73a380 | 29 | 206 | 7 | game | FOOT-04.2 | 6f1fb4f0 |
| 6f64b760 | FUN_6f64b760 | 28 | 481 | 0 | game | ORDER-01.13 | 6f642aa0 |
| 6f048140 | FUN_6f048140 | 28 | 395 | 4 | game | ORDER-01.13 | 6f1fb4f0 |
| 6f48dcb0 | FUN_6f48dcb0 | 28 | 161 | 5 | game | ORDER-01.13 | 6f642aa0 |
| 6f215b00 | FUN_6f215b00 | 27 | 173 | 0 | game | FOOT-04.2 | 6f215b00 |
| 6f532c50 | FUN_6f532c50 | 27 | 257 | 3 | game | ORDER-01.16 | 6f1fb4f0 |
| 6f6e9270 | FUN_6f6e9270 | 27 | 320 | 4 | game | ORDER-01.13 | 6f1fb4f0 |
| 6f4ee390 | FUN_6f4ee390 | 26 | 278 | 4 | game | SEP-01.2 | 6f1fb4f0 |
| 6f6cf120 | FUN_6f6cf120 | 25 | 303 | 1 | game | ORDER-01.13 | 6f508da0 |
| 6f435be0 | FUN_6f435be0 | 25 | 262 | 2 | game | ORDER-01.13 | 6f40daa0 |
| 6f48bd70 | FUN_6f48bd70 | 25 | 125 | 6 | game | ORDER-01.13 | 6f642aa0 |
| 6f524b40 | FUN_6f524b40 | 24 | 214 | 3 | game | ORDER-01.13 | 6f1fb4f0 |
| 6f5f9a30 | FUN_6f5f9a30 | 24 | 248 | 4 | game | ORDER-01.16 | 6f1fb4f0 |
| 6f744750 | FUN_6f744750 | 24 | 186 | 7 | game | FOOT-04.2 | 6f1fb4f0 |
| 6f408580 | FUN_6f408580 | 23 | 325 | 0 | game | ORDER-01.13 | 6f408580 |
| 6f674310 | FUN_6f674310 | 23 | 150 | 2 | game | NUM-03.2 | 6f1fb4f0 |
| 6f651830 | FUN_6f651830 | 23 | 183 | 5 | game | FOOT-04.2 | 6f1fb4f0 |
| 6f656c10 | FUN_6f656c10 | 23 | 309 | 6 | game | ORDER-01.13 | 6f642aa0 |
| 6f3faf20 | FUN_6f3faf20 | 22 | 261 | 2 | game | ORDER-02.2 | 6f424cf0 |
| 6f6ae510 | FUN_6f6ae510 | 22 | 101 | 4 | game | ORDER-01.12 | 6f1fb4f0 |
| 6f3ff450 | FUN_6f3ff450 | 21 | 134 | 1 | game | ORDER-01.13 | 6f408990 |
| 6f565e70 | FUN_6f565e70 | 21 | 190 | 2 | game | ORDER-01.16 | 6f1fb4f0 |
| 6f496b80 | FUN_6f496b80 | 21 | 155 | 3 | game | GROUP-03.2 | 6f1fb4f0 |
| 6f5a0970 | FUN_6f5a0970 | 21 | 195 | 3 | game | ORDER-01.12 | 6f1fb4f0 |
| 6f6d3260 | FUN_6f6d3260 | 21 | 171 | 7 | game | ORDER-01.13 | 6f642aa0 |
| 6f651170 | FUN_6f651170 | 20 | 238 | 2 | game | ORDER-01.13 | 6f1fb4f0 |

## Top partial (documented, not guarded) high-branch functions

| address | name | branches | owner | evidence |
|---|---|---:|---|---|
| 6f4725f0 | FUN_6f4725f0 | 462 | FOOT-04.2 | oracle,plate |
| 6f9d16c0 | CaptainAI_UpdateMemberOrders | 102 | GROUP-03.4.7.3 | forced-position-row,map-row,oracle,plate |
| 6f66f050 | Build_CheckPlacement | 74 | ORDER-01.13 | map-row,oracle,plate |
| 6f4946b0 | FUN_6f4946b0 | 48 | GROUP-03.2 | oracle |
| 6f435f10 | ItemTeleport_BeginExclusiveCast | 41 | SEP-01.2 | map-row,oracle |
| 6f407d80 | FUN_6f407d80 | 38 | ORDER-01.13 | oracle |
| 6f59b210 | FUN_6f59b210 | 34 | ORDER-01.13 | forced-position-row,oracle |
| 6f9d4c20 | CaptainAI_QueryMovementSpeed | 32 | GROUP-03.4 | map-row,oracle,plate |
| 6f40be30 | ItemTeleport_EndExclusiveCast | 32 | SEP-01.2 | map-row,oracle |
| 6f34b150 | Audio_PlayLabel | 31 | E2E-05.1 | oracle,plate |
| 6f9d73c0 | CaptainAI_TestTargetEngagement | 30 | GROUP-03.4.7.3 | map-row,oracle,plate |
| 6f408ea0 | FUN_6f408ea0 | 30 | ORDER-01.13 | oracle |
| 6f9b8120 | FUN_6f9b8120 | 29 | ORDER-01.13 | oracle |
| 6f0af5e0 | Audio_AdmitInstance | 29 | E2E-05.1 | oracle,plate |
| 6f05e5c0 | FUN_6f05e5c0 | 28 | FOOT-04.2 | oracle |
| 6f070850 | Math_DistanceSquaredSourceMinusPoint | 27 | NUM-01.2 | map-row,oracle,plate |
| 6f9d87d0 | CaptainAI_ReissueMemberOrder | 27 | ORDER-01.13 | map-row,oracle,plate |
| 6f283c80 | FUN_6f283c80 | 27 | FOOT-04.2 | oracle |
| 6f6a74b0 | FUN_6f6a74b0 | 27 | TARGET-01.2 | oracle,plate |
| 6f1a42b0 | Sprite_SelectAnimationTokens | 27 | E2E-05.1 | oracle,plate |

## Covered functions with the lowest guarded-branch ratio (>=10 branches)

| address | name | guarded/branches | oracles | owner |
|---|---|---|---|---|
| 6f654090 | Widget_DisplaceOccupant | 0/17 | oracle-widget_masks,widget-footman-escape,widget-solid-failure | ORDER-01.13 |
| 6f070de0 | Math_ParseDecimal | 0/15 | numeric-engine-exact,oracle-basic352,oracle-numeric | E2E-06.1 |
| 6f22f410 | PathTexture_SnapWidgetPoint | 0/13 | oracle-widget_masks,widget-footman-escape,widget-solid-failure | FOOT-04.2 |
| 6f48cb80 | Unit_FindAbilityByClass | 0/23 | oracle-speed,oracle-speed-bonus-engine | SEP-01.2 |
| 6f064460 | RegionCollection_ResizeWithCategories | 0/13 | oracle-object-categories | SEP-01.2 |
| 6f14a1e0 | PathFine_FindPlacement | 0/12 | oracle-embedded-recovery-scope,oracle-point-placement,oracle-zero-query-placement | FOOT-04.2 |
| 6f5fe6a0 | FUN_6f5fe6a0 | 0/41 | callback-finish-open,callback-finish-wall,callback-reuse-missing-alias | ORDER-01.13 |
| 6f73c800 | FUN_6f73c800 | 0/12 | fine-route-trajectory-reference,motion-engine-exact,oracle-blocked-completion195 | FOOT-04.2 |
| 6f15d1c0 | PathMaps_ClassifyParent | 0/27 | adaptive-engine-size1,adaptive-engine-size2,adaptive-handoff-reference | MOVE-01.1 |
| 6f1625f0 | PathAcc_FindCellNode | 0/11 | adaptive-engine-size1,adaptive-engine-size2,adaptive-size2 | FOOT-04.2 |
| 6f1643d0 | PathAcc_ExpandBaseNode | 0/12 | adaptive-engine-size1,adaptive-engine-size2,adaptive-size2 | ORDER-01.16 |
| 6f148e90 | PathFine_TestAcceleratorCell | 0/15 | oracle-object-categories | MOVE-01.1 |
| 6f163370 | PathAcc_Size2EastOccupancy | 0/12 | adaptive-engine-size1,adaptive-engine-size2,adaptive-size2 | ORDER-01.16 |
| 6f1635b0 | PathAcc_Size2EastBoundary | 0/19 | adaptive-engine-size1,adaptive-engine-size2,adaptive-size2 | ORDER-01.16 |
| 6f206f00 | Jass_DispatchPointOrder | 11/11 | oracle-pause286,oracle-work239 | ORDER-01.13 |
| 6f207160 | Jass_DispatchTargetOrder | 13/13 | oracle-pause286,oracle-target-normalize218,oracle-work251 | ORDER-01.13 |
| 6f438680 | CAbilitySimpleSpell_BeginTargetApproach | 14/14 | oracle-spell267,oracle-spell268,oracle-spell269 | ORDER-01.13 |
| 6f5ffb60 | Move_HandlePointTask | 23/23 | fine-route-trajectory-reference,motion-engine-exact,oracle-attack-recovery223 | TARGET-01.2 |
| 6f670950 | Unit_RebindTypeRetainingPublicIdentity | 113/113 | oracle-morph374,oracle-morph375,oracle-morph380 | SEP-01.2 |
| 6f68a060 | Unit_InitializeFromCreationProfile | 90/90 | oracle-flight280,oracle-profiles | FOOT-04.2 |

## Virtual dispatch resolved inside the closure

424 (class, slot) pairs resolved by R2/R3. CUnit 6fb77eb0 has no RTTI subclass, so every CUnit slot resolves to one target; widget slots fan out over CWidget/CSelectable/CUnit/CItem/CDestructable/CCaptainAI; CAbility-family slots fan out by CHA (the ability dispatch table). Most-used slots:

| class | slot | sites | targets | example targets |
|---|---|---:|---:|---|
| CUnit | 0x8 | 137 | 1 | 6f0725b0 Agent_RegisterEventSubscriber |
| CUnit | 0xb8 | 104 | 1 | 6f6864d0 CUnit_GetMoverBridge |
| CUnit | 0xec | 100 | 1 | 6f685da0 CUnit_GetOwnerIndex |
| CWidget | 0xb8 | 21 | 5 | 6f6525c0 FUN_6f6525c0, 6f6584c0 FUN_6f6584c0 |
| CUnit | 0x10 | 19 | 1 | 6f071da0 Agent_DispatchPacket |
| CUnit | 0x64 | 15 | 1 | 6f686000 FUN_6f686000 |
| CUnit | 0x180 | 14 | 1 | 6f698050 Unit_SetPositionWithAdmission |
| CAbilitySpell | 0x314 | 9 | 1 | 6f41d2a0 FUN_6f41d2a0 |
| CUnit | 0x130 | 9 | 1 | 6f685270 FUN_6f685270 |
| CAbilitySimpleSpell | 0x34c | 8 | 12 | 6f4358d0 FUN_6f4358d0, 6f4358e0 FUN_6f4358e0 |
| CUnit | 0x15c | 8 | 1 | 6f699200 CUnit_PublishPathingProfile |
| CUnit | 0x160 | 8 | 1 | 6f678b60 CUnit_GetPathingCategory |
| CUnit | 0x164 | 8 | 1 | 6f678b50 CUnit_GetPathingQueryMask |
| CUnit | 0x100 | 7 | 1 | 6f66ed20 FUN_6f66ed20 |
| CUnit | 0xa0 | 6 | 1 | 6f697ff0 FUN_6f697ff0 |
| CUnit | 0x124 | 6 | 1 | 6f698a40 FUN_6f698a40 |
| CUnit | 0x1c | 5 | 1 | 6f6884e0 FUN_6f6884e0 |
| CUnit | 0x14c | 5 | 1 | 6f650c00 Widget_RetirePathing |
| CAbilitySleepAlways | 0x2f4 | 4 | 2 | 6f525b20 FUN_6f525b20, 6f525b30 FUN_6f525b30 |
| CAbilitySpell | 0x368 | 4 | 1 | 6f41dc40 FUN_6f41dc40 |
| CUnit | 0x80 | 4 | 1 | 6f699c00 Unit_RestoreWorldPresence |
| CUnit | 0x98 | 4 | 1 | 6f652e10 FUN_6f652e10 |
| CUnit | 0xb4 | 4 | 1 | 6f69d680 FUN_6f69d680 |
| CUnit | 0xd0 | 4 | 1 | 6f651590 Unit_ToggleSpatialExclusion |
| CUnit | 0x148 | 4 | 1 | 6f6501a0 Widget_ApplyPathing |
| CAbility | 0xcc | 3 | 20 | 6f3fa480 Ability_CountedAvailabilityEnabled, 6f3fa490 FUN_6f3fa490 |
| CAbilityImpale | 0x418 | 3 | 1 | 6f62ebb0 FUN_6f62ebb0 |
| CAbilityInterfaced | 0x2f4 | 3 | 26 | 6f42b740 FUN_6f42b740, 6f42b810 FUN_6f42b810 |
| CAbilityMorph | 0x318 | 3 | 11 | 6f43a860 FUN_6f43a860, 6f4efd50 FUN_6f4efd50 |
| CAbilitySimpleSpell | 0x314 | 3 | 1 | 6f41d2a0 FUN_6f41d2a0 |

Widest CHA fan-outs:

| class | slot | sites | targets |
|---|---|---:|---:|
| CAbility | 0x1c | 2 | 724 |
| CAbilityButton | 0x308 | 2 | 303 |
| CAbilitySpell | 0x1c | 1 | 272 |
| CAbilitySpell | 0x308 | 1 | 249 |
| CAbilitySimpleSpell | 0x1c | 1 | 220 |
| CAbilitySimpleSpell | 0x308 | 1 | 202 |
| CAbilitySimpleSpell | 0x424 | 1 | 92 |
| CAbilitySimpleSpell | 0x30c | 2 | 89 |
| CAbility | 0x1e4 | 2 | 73 |
| CAbility | 0xc4 | 2 | 55 |

Unresolved computed sites by reason: receiver field-unknown 1833, receiver ret-unknown 878, receiver other 523, non-vtable pointer 314, receiver param-unknown 300, receiver local 197, receiver this-unknown 186, register/stack call 181, receiver entry 22, receiver clobber 16, megamorphic .?AVCAgent@@ 3, global-pointer 1, megamorphic .?AVTRefCnt@@ 1, megamorphic .?AVCObserver@@ 1. These are listed per site in `inventory.json:unresolved_sites`; their targets are not in the inventory.

BASE-01.4 row B014-71 (owned by BASE-03.1: unexpanded CUnit vslot80 callers): R2/R3 resolve four CUnit+0x80 -> 6f699c00 Unit_RestoreWorldPresence sites: 6f215bd0 (FUN_6f215b00, JASS handle), 6f50013a (FUN_6f500030) and 6f5ea695 (FUN_6f5ea5a0) via the ability cached-owner idiom, 6f6956f4 (Unit_RestoreLifeState). No CItem+0x40 site was resolved inside the closure. The row stays uncovered (no oracle); proposed owner after BASE-03.1: FOOT-04.2 for placement and E2E-05.2 for the CItem self re-admission site.

## Limitations

- Unresolved computed calls (4456 sites) and global function-pointer writes mean the closure under-approximates indirect reachability; CHA over-approximates within a resolved family.
- Categories for unnamed functions are interpolated from compilation-unit anchors and module ranges; ~244 reachable functions are `unknown` and treated as in scope.
- Branch lists come from metrics.jsonl (truncated at 4000 characters for very large functions; flagged `branch_list_truncated`).
- `covered` means an oracle executes, hooks or guards bytes inside the function, not that every path is behaviour-verified; the guarded-branch ratio is the per-branch linkage.

## Reproduce individual branch publication

The checked-in `ExportReachableBranchRecords.java` iterates the complete body of
each reachable Ghidra function without a display limit. It reads instructions
only and checks the original executable SHA-256. `branches` in the version2
inventory is the current branch source; the old `branch_list_truncated` function
flag describes the original metrics source, not this complete export.

```sh
python3 tools/ghidra/generate_wc3_pathing_reachable_branches.py --functions /tmp/reachable-functions.txt
# In Ghidra, run ExportReachableBranchRecords.java on game.dll with arguments:
# /tmp/reachable-functions.txt /tmp/reachable-branches.jsonl
python3 tools/ghidra/generate_wc3_pathing_reachable_branches.py --export /tmp/reachable-branches.jsonl --output /tmp/reachable-inventory.json.gz
cmp tools/ghidra/fixtures/retail-pathfinding-reachable-inventory-1.27.json.gz /tmp/reachable-inventory.json.gz
```

A second fresh Ghidra export reproduces the checked-in JSONL byte for byte
(SHA-256 `ec85c45562e30defdc3cb2a1fbc2c3fc2045c6d4f50d790e8c2bda7a14fe0032`).
The compressed JSONL is published separately from the linked inventory so
removing a branch and adjusting totals still fails against the original export.
The generator rejects a different export pending review. All 21,306 instruction
bytes match the original PE; this proves identity and decoded edges, not that
both edges were exercised. E2E-05.1 still owns the combined evidence/missing-fix
matrix and READY still requires behavior acceptance.
