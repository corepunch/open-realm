# Retail pathfinding field read/write inventory (BASE-03.2)

BASE-03.2 is complete: every field of all **115 recovered layouts (839 fields)**
has exactly one inventory row with its attributed writers, readers or
address-takers, retail save/load accessor, evidence anchors, engine counterpart,
status and owner. **427** fields are covered, **400** partial and **12** never
accessed in the bounded function set; every row that is not covered is owned by
one existing open leaf. No new TODO leaves are created. The inventory also lists
426 pathing virtual slots over 22 vtables, 179 globals and 599 accessed but
unnamed layout offsets. Closing this publication task does not resolve those
rows or certify the behavior they describe.

The [inventory](../../../tools/ghidra/fixtures/retail-pathfinding-fields-inventory-1.27.json.gz)
is a compressed JSON fixture. Validate it from the repository root:

```sh
python3 tools/ghidra/audit_wc3_pathing_fields.py --report /tmp/fields-inventory.json
python3 -m unittest discover -s tests -p test_wc3_pathfinding_fields_inventory.py
```

The verifier rejects any layout field that is missing, duplicated or added to
`retail-pathfinding-types-1.27.json` without an inventory update; covered rows
without a writer, a reader or address-taker and a resolvable anchor; stale
Payoff, oracle, closed-task or doc-heading references; and partial or uncovered
rows, slots, globals or prefix gaps whose owner is not a remaining open task.
When an owning leaf closes, its rows must be resolved or reassigned in the same
change. The scripts and cached Ghidra responses used to build the inventory are
archived under `/GitHub/wc3-analysis/runtime/claude-base032/` (`work/run_all.sh`).

## What was inventoried

| Item | Count | Source |
|---|---|---|
| Layout fields | 839 in 115 layouts | `tools/ghidra/fixtures/retail-pathfinding-types-1.27.json` (all layouts, no filtering) |
| Bounded function set | 1887 decompiled functions | 1637 MapPathfinding rows + 676 explicit prototypes (they overlap), 65 field-evidence citations, 141 vtable-slot targets, 42 callees that receive field addresses, 1 vtable constructor (`14fc40`) |
| Virtual slots | 426 slot rows over 22 vtables, 208 unique targets | PE RTTI: 15 `NIpse` pathing classes (all slots), CUnit/CWidget/CSelectable/CItem/CDestructable/CCaptainAI (25 pathing slots each), CAbilityMove (slots whose target is in scope) |
| Globals | 179 rows (6 table runs), plus 6 rejected candidates | 74 fixture globals, data referenced by pathing-family decompilations, data tokens in evidence text; Ghidra `get_xrefs_to` READ/WRITE |
| Prefix gaps | 599 unnamed offsets | Accesses to bytes the recovered layouts do not name, found while attributing fields |

Fields are grouped into families: map (map, accelerator, spatial, terrain and placement), path (route, registry, pool,
scheduler, owner), mover, group (group, member, shared, formation, Captain/Town), order (Unit order heads,
requests, agents, events, abilities, attack, patrol), and support (clocks, timers, PRNG, JASS, vectors).

## Status

A field is **covered** when the bounded set contains at least one writer and at least one reader or
address-taker. Accessors that only come through the shared `WC3CanonicalPrefix` base do not count. The field
also needs an anchor: a closed task, a Payoff, a corpus oracle that exercises one of its accessors, or a doc
section that mentions an accessor. It is **partial** when only some of that exists. It is **uncovered** when the
bounded set has no access and the layout evidence names no in-scope function.

| family | structs | fields | covered | partial | uncovered |
|---|---|---|---|---|---|
| map | 29 | 226 | 117 | 106 | 3 |
| path | 9 | 81 | 59 | 22 | 0 |
| mover | 10 | 49 | 29 | 20 | 0 |
| group | 13 | 95 | 60 | 34 | 1 |
| order | 38 | 279 | 123 | 148 | 8 |
| support | 16 | 109 | 39 | 70 | 0 |
| **total** | 115 | 839 | 427 | 400 | 12 |

Confidence of the 427 covered rows depends on where their accessors came from:

- **high (303):** both the writer and the reader come from Ghidra-typed field names.
- **medium (65):** both come from name-inferred receivers, indexed arrays, one-level callee use or unique embedding.
- **low (59):** at least one direction rests only on an instruction-level displacement match. That match is
  inside a function the field's own evidence names, so it is still targeted, but the offset itself is not proof.

| Other row kinds | covered | partial | uncovered | other |
|---|---|---|---|---|
| Virtual slots (426) | 108 | 2 | 308 | 8 absent (vtable shorter than slot) |
| Virtual slots, unique targets (208) | 63 | 2 | 143 | - |
| Globals (179) | 34 | 144 | 0 | 1 not pathing state (`6fce689c` CRT security cookie) |
| Fixture globals (74) | 26 | 48 | 0 | - |

## Owner distribution (every non-covered row; existing open leaves only, no new IDs)

| owner | fields | virtual slots | globals | prefix gaps | total |
|---|---|---|---|---|---|
| E2E-05.1 | 0 | 0 | 0 | 599 | 599 |
| BASE-03.1 | 132 | 309 | 58 | 0 | 499 |
| TARGET-01.2 | 53 | 0 | 8 | 0 | 61 |
| E2E-06.1 | 45 | 0 | 5 | 0 | 50 |
| GROUP-04.6 | 36 | 0 | 12 | 0 | 48 |
| FOOT-04.2 | 26 | 0 | 13 | 0 | 39 |
| NUM-01.2 | 2 | 0 | 28 | 0 | 30 |
| NUM-02.3 | 25 | 0 | 3 | 0 | 28 |
| ORDER-02.2 | 21 | 0 | 3 | 0 | 24 |
| ORDER-01.13 | 22 | 0 | 1 | 0 | 23 |
| GROUP-03.4.7.3 | 16 | 0 | 4 | 0 | 20 |
| MOVE-01.1 | 12 | 1 | 7 | 0 | 20 |
| ORDER-01.16 | 11 | 0 | 0 | 0 | 11 |
| BASE-02.3 | 6 | 0 | 0 | 0 | 6 |
| SEP-01.2 | 2 | 0 | 2 | 0 | 4 |
| BASE-01.4 | 3 | 0 | 0 | 0 | 3 |

The generator `work/build.py` checks every non-covered field, slot, global and gap row. Each must have an owner from the
remaining open leaves, excluding READY-*. The build fails if any row lacks one.

How owners were chosen:

1. An open leaf cited in the field's own evidence.
2. Otherwise a field-name rule: time/epoch/clock go to NUM-02.3, separation to SEP-01.2, random to E2E-06.1,
   shared speed to GROUP-03.2, vtable/identity/references to BASE-03.1.
3. Otherwise the struct default. Captain/Town go to GROUP-03.4.7.3, patrol/attack/range/waygate to
   TARGET-01.2, ability/buff to ORDER-01.13, build/repair to ORDER-01.16, move requests and groups to
   GROUP-04.6, placement and terrain to FOOT-04.2, map header domains to BASE-02.3, Blink to BASE-01.4.
4. Unrecovered virtual targets and unattributed objects go to BASE-03.1, the reachable function/branch inventory.

## Key findings

1. **Retail save/load for mover, path, group and shared objects runs through unnamed vtable slots.** The
   `CRlAgent`-derived classes save at +0x24 and load at +0x28. Three named classes prove the pattern:
   CPmRegion (`14d430`/`14d000`), CLrPathingSys (`148210`/`1481e0`) and CLrPathingAcc (`162fd0`/`162da0`).
   That makes the following targets the save/load pairs:
   - CPoPosCl (mover): `16f890`/`16f6e0`
   - CLrPath: `167780`/`167600`
   - CPrCluster (path group): `16ace0`/`16aa10`
   - CPrClusterGroup (shared): `16aec0`/`16ac90`

   None of these functions has a MapPathfinding row. Together with the named save/load functions, they give
   82 fields a retail save/load accessor, including the group's age, completion and cooldown counters, the path's retry, query-mask
   and flags words, and the mover's flags, group and remembered region point. The slots themselves are owned
   by BASE-03.1.
2. **RTTI maps each pathing layout to its class.** WC3MoverPrefix is CPoPosCl (constructor `14fa30`).
   WC3PathGroupPrefix is CPrCluster: constructor `14fc40`, and slots 0xc/0x10 are PathGroup_Activate/Destroy.
   WC3PathPrefix is CLrPath (`1657c0`) and WC3PathSharedPrefix is CPrClusterGroup (`14fd70`). WC3SpatialPrefix
   is CPmRegion (`14c1d0`). The two layout constructors `14fa30` and `14fc40` are not MapPathfinding rows.
3. **Principal CUnit pathing slots (vtable `6fb77eb0`):**

| slot | target | status | typed / untyped call sites |
|---|---|---|---|
| b8 | 6864d0 CUnit_GetMoverBridge | covered (Payoff184/257) | 72 / 80 |
| e4 | 66d780 CUnit_GetSupportZ | covered (BASE-02.1, MAP-02.2) | 1 / 6 |
| e8 | 68f390 CUnit_GetFlyHeightOffset | covered (BASE-02.1) | 1 / 1 |
| ec | 685da0 CUnit_GetOwnerIndex | covered (doc anchor only) | 79 / 22 |
| 158 | 685ef0 CUnit_GetCurrentPathingQueryMask | covered (doc anchor only) | 0 / 6 |
| 15c | 699200 CUnit_PublishPathingProfile | covered (doc anchor only) | 4 / 15 |
| 160 | 678b60 CUnit_GetPathingCategory | covered (doc anchor only) | 7 / 1 |
| 164 | 678b50 CUnit_GetPathingQueryMask | covered (doc anchor only) | 4 / 5 |
| 168 | 6742f0 CUnit_GetCollisionRadius | covered (Payoff312/313) | 3 / 2 |
| 180 | 698050 Unit_SetPositionWithAdmission | covered (Payoff285/308) | 7 / 4 |
| 184 | 679a80 (unnamed) | **uncovered**, BASE-03.1 | 0 / 1 |

   The other CUnit pathing slots that are unrecovered are c0, c4, c8, d4 and 178. Slot dc
   (`Unit_AdmitPosition`) is partial. For the widget classes
   (CWidget, CSelectable, CItem, CDestructable, CCaptainAI), these overrides are unnamed: b8, ec, 158, 80, 84,
   c0–cc, d4, e8 and 178. CItem's 160/164 and the shared 148/14c/15c/d0/dc/e4 are named and covered.
4. **Core state that is still partial or uncovered:**
   - Mover `+70 time` and `+74 epoch`: readers and address-takers only. The MapPathfinding row for `161090`
     says "copy selected clock40/44 to mover70/74", and `15c6f0` stores through its last two parameters.
     Ghidra's caller and callee arities disagree there, so the inventory does not attribute that write
     automatically. Owner NUM-02.3.
   - Path route buffers `+40/+50/+60/+70` (fine/adaptive data and count): these live in embedded dynamic
     tables at +34/+54 and are written through table helpers. Owner BASE-03.1.
   - PathMember `+20 speed` and `+24 heading`: no writer found. PathMembersPrefix `+0c data` has no writer, and
     `+18 capacity` is never accessed. Owner GROUP-04.6.
   - FormationRankBucket counts and arrays (GROUP-04.6). Handle-registry bounds, free lists and generations
     (BASE-03.1).
   - GroupPointContext (GROUP-04.6). MoveSpeed `+30 unit` and `+74 multiplier` writers (MOVE-01.1).
   - SeparationConfig: a global table of 16 rows × 20 bytes at `6fd54398`. `Separate_InitializeSettings` writes
     it and `1702f0` reads `radius/minimum/cap` through the table base. No reader was found for weight or
     damping. Owner SEP-01.2.
5. **12 fields are never accessed in the bounded set:**
   - PathMembers `+18 capacity`
   - Order `+0c identity`
   - AgentWrapper `+00 vtable`, `+10 base_tag`, `+28 receiver_next`
   - PatrolEndpoints `+24 command`
   - MoveRequestNeighborContext `+1c request`
   - PathMapHeader `+14 identity`
   - WaygatePoolOwner `+4c edge_bound`, `+50 availability`
   - AgentRelationList266 `+00 anchor`, `+04 head`

   Each is owned by its struct or family default (see `inventory.json`).
6. **599 accessed but unnamed offsets.** WC3UnitOrdersPrefix has 75. These include `+158`, `+1b8`, `+1c4`,
   `+248` and `+280..288`, which the engine already models as `ethereal_depth`, `ability_disable_depth`,
   `ability_pause_depth`, `unit_classification` and `support_flags/support_point`. The types fixture does not
   name them. PathOwnerCounterPrefix has 143 (its 1344 bytes have only 13 named fields). CaptainAI has 45 and
   AttackRange 34. There are also base-header words `+4..+10` on every `CRlAgent` object. All of these are
   owned by E2E-05.1, whose leaf text is to list the remaining prefixes and flags.
7. **Globals.** The 48 partial fixture globals are mostly PE-initialized scalars with no WRITE xref: 28
   `Math_*` (NUM-01.2) and the Captain range constants (GROUP-03.4.7.3). The other 96 rows are unnamed `.data`
   words used by pathing code. They include `Mover_InitializeVisualPolicies`' 64-word table
   `6fd541e0..6fd542dc`, the separation table, `Unit_AdmitPosition`'s scratch `6fd70fac..b4`, and
   `PathFine/PathAcc` node scratch. The PE initial value of each row is recorded.
8. **Engine counterparts:**
   - Mapped: 171 fields to `games/warcraft-3/...` fields, with the basis stated (engine comment cites the
     native offset, or semantic match).
   - Saved by name in the `g_save.c` TF()/F() schema: 117 of the mapped fields.
   - Not saved by name: 53. Many are expected because the state is rebuilt or saved by custom code:
     `movePointRequest_t` is reconstructed, spatial records use the Save120/121 code, `members` is saved with
     `count` through a raw initializer row, and orders go through a pool entry.
   - Worth verifying: `edictMovement_s.velocity` and `.heading` appear in neither `movement_fields` nor any
     other table.

## Limits

- Attribution is bounded to 1887 functions. It is not whole-program, so readers and writers outside that set
  stay unknown. BASE-03.1 owns that gap.
- Name-inferred receivers come from the recovered name prefix (for example `PathGroup_`) and are applied only
  to an untyped ECX parameter. Instruction-level matches are restricted to functions the field's own evidence
  cites. Every accessor records its source, so these can be told apart from typed hits.
- A shared base offset (`vtable`, `identity`) is propagated as `base:WC3CanonicalPrefix` and kept separate from
  field-specific accessors. Array elements N≥1 inherit indexed accesses (`(&req->candidate0)[i]`) as
  `indexed-array-via`.
- Oracle links mean "this corpus entry cites an accessor function". They do not mean the oracle checks the
  field.
- Virtual call sites are matched by slot offset. Only sites whose receiver type is known count as typed. The
  rest are listed as possible matches.
