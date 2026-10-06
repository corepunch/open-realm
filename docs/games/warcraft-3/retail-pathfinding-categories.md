# Fine-map object categories

Payoff134 closes **BASE-02.2**, the object-category, payload and consumer inventory.
The engine now publishes items in the fine index, preserves a building's own mover
rectangle and uses item admission for creation, repositioning and drops. This is
an inventory closure with an engine payoff, not a claim that all object consumers
or public building/missile lifecycles match retail.

## Original owners and consumers

The target is game.dll **1.27.1.7085**, SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
The [BASE-02.2 handoff](retail-pathfinding-handoffs/BASE-02.2/HANDOFF.md)
and [frozen inventory](../../../tools/ghidra/fixtures/research/BASE-02.2-expected.json)
identify the actual producers, virtual slots, record words and evidence limits.

`14cf20` creates a spatial object with zero category/activity words. The independent
fine map is at path-owner+238; proximity uses owner+234. `15ed40` activates a
mover's own fine object. `05c7b0` publishes its category; `05c660` controls active
occupancy. Widget pathing textures instead create static region objects through
`064460` and `14cf70`. A building can have both its own mover rectangle and those
static regions. Its static footprint does not replace the mover's identity.

| Object | Active category | Payload / token | Fine search and segment | Endpoint and collector | Hierarchy |
|---|---:|---|---|---|---|
| Foot/horse/hover/float/amph unit | `010000ca` | Unit mover tag `60706375`; unit token | Queries `02/08/40/80`, excluding moving/cohort-transient objects | Same categories, including moving/transient objects | Ineligible |
| Flying unit | `01000000` | Unit mover | No category match; rectangle remains | No category match | Ineligible |
| Unbuild/Land Mine unit | `01000008` | Unit mover | Build query `08` | Build query `08` | Ineligible |
| None/unknown movement, including ordinary building own mover | `01000000` | Unit mover | No category match; identity remains observable | No category match | Ineligible |
| CaptainAI virtual actor | Initially `01000002`, then authored category with `08` cleared | Virtual mover; token not live verified | Published category match | Published category match | Ineligible |
| Item | `01000018` | Item mover tag `60706f73`; **NULL token** | Item `10` and build `08` | Item `10` and build `08` | Ineligible |
| Widget blue pathing region | `010000c2` | Widget agent; NULL token | Ground/water `02/40/80` | Same | Ground/water lanes |
| Widget red/green pathing region | `01000010` / `01000008` | Widget agent; NULL token | Item / build | Same | No supported lane match |
| Optional texture flight region | `01000004` | Widget agent; NULL token | Flight `04` | Same | Ground `06` and flight `04` |
| CTriggerRegion | `00000000`, static flag set | Region object | Inactive | Inactive | Inactive; raw links still count toward examination cap |

All values are hexadecimal. Every consumer requires the relevant active/live
record and category match. Suppression count or the high excluded bit wins over
category; ordinary fine checks additionally exclude moving/cohort flags. The
hierarchy requires static bit `10000000`. These predicates read no player owner
or alliance relation. The table's token column describes `148ad0`/`1480d0`, not
the higher-level yielding policy applied after collection.

The original inventory also identifies runtime unit profile writers for Ghost,
Burrow, Submerge, Tornado Wander, Wisp Harvest, Locust and pathing toggles. They
change category/query independently of movement type. Destructable creation
does not publish an own-mover category through its no-op profile slot; its
observable blockage comes from static regions. Missile supplied-category and
fine activity, item textures and live construction/mine/ward/Way Gate rows are
explicitly unproven here. Public construction remains E2E-02.2/06.2; this chunk
does not introduce another TODO to hide those limits.

## Item admission and engine integration

`CItem` vtable `6fb77aa8` has category/query slots `160/164` returning `18/10`.
Radius slot `168` points to `650ad0`, which returns runtime **world radius1**.
`65a8a0` publishes that category and radius; authored `selectionSize` is separate
presentation geometry. The engine owns these immutable facts in `g_items.c`.
The fine world adapter consumes the encountered owner's category, rather than
interpreting every object as UnitData. Collector output preserves an item's
NULL token. Category-zero building/flying owners keep their fine rectangles.

`658c10` creates items through `058cd0` with query10, radius1 and **no support-level
callback**, before publishing the object. Public repositioning reaches shared
`6515f0`, which clamps the widget point, suppresses self, supplies the inherited
support-level callback and restores suppression. `G_FindWidgetPlacementPosition`
shares the existing deterministic 32-ring geometry with unit placement while
keeping these owner inputs explicit. Drops use item admission, not unit recovery.
Layered bridge-level production and full outside-map constructor clipping remain
FOOT-04.2/MAP support work; the new fixture proves the callback distinction with
authored terrain levels, not complete bridge parity.

Native `213940` (`SetItemPosition`) first resolves a carrier and calls `694ce0`.
That removes the inventory item through `5667d0` and publishes it at the carrier's
position through `65a340`. The native then calls `65a340` again for the requested
position. The engine preserves both publications and their ranks; it does not
apply the UI droppable gate to this native. Existing filled-Soul lifecycle rules
are separate from the ordinary carried-item regression.

Fine membership changes are authoritative at pickup/drop/reposition boundaries.
They cannot depend on a later query flushing dirty owners: save serializes logical
rectangles directly. Item logical values and rectangles use the existing Save119
layout; this change adds no saved pointer, field, network prefix or format layout.
The existing cell index visits local owners and retains O(1) per-owner publication
for the bounded four-by-four fine rectangle. There is no new global entity scan.

## Verification and remaining work

`t_object_categories.c` drives actual creation, native dispatch, fine segment and
endpoint consumers, collector, pickup/drop and save/load. It covers all authored
movement names, all six query masks, every player owner, moving-object endpoint
differences, category-zero building records, item NULL tokens, constructor versus
reposition support callbacks and both carried-item publication ranks. The older
proximity fixture's expectation that building classification retired the fine
record is corrected from the retail producer inventory.

Fresh original-code execution in `/GitHub/wc3-analysis/runtime/payoff134/` verifies
**50,112 eligibility cases**: 34,944 single-link, 15,120 ordered-chain and48 cap
cases, with zero mismatches. The16 complete composed-producer scenarios regenerate
the frozen FOOT-03.2 payload byte-for-byte. Reanalysis of both supplied FOOT-03.2 Frida captures
matches **164 consumer calls per capture** and all60 markers against the repeat
and observer-free control. These establish the retail inventory; they do not
turn the engine's current active-only representation into a complete raw-link
implementation.

**FOOT-03.1 and FOOT-03.2 remain open.** Hierarchy's 49-link examination cap counts
metadata, inactive, duplicate and removal links too. Mixed static/dynamic collector
ordering is not represented by combining an aggregate static byte with active
owner rectangles. Closing either task requires that full record-aware consumer
data flow, including history and cleanup, rather than an active-occupant shortcut.

The saved Ghidra readback is
[retail-object-categories-ghidra-1.27.json](../../../tools/ghidra/fixtures/retail-object-categories-ghidra-1.27.json).
`MapPathfinding.java` retains31 producer/native function mappings and now preserves
prior plate evidence when reapplied. The saved readback includes three partial
layouts/15 fields; the type manifest retains rectangle, owning-map, outstanding-record
and category/flag annotations.

The initial four-test regression run recorded42 failures before integration; its
compound JASS assertion also exposed an argument error, so final native checks use
separate getter assertions. An
additional boundary-only run then failed three assertions until pickup/drop
published synchronously. Final Classic/TFT release checks pass **945 focused tests
and7,791,459 assertions per edition**, including eight new category tests/321
assertions, all191 save tests and the complete overlap owner replay. Six evidence
integrity tests and21 corpus tests pass. Captured preload files contain CRLF; the portable bundle preserves original bytes
and verifies their hashes before analysis. The full repository suite is deferred
to the authorized batch checkpoint; this is implementation commit11 after14357757.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_categories.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/categories-fresh.json
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 build/bin/openwarcraft3-tests \
  -data build/tests +dedicated 1 +test 'wc3_object_categories.*'
# Repeat the engine checks with -tft; run the affected inventory, placement,
# movement, proximity, building and destructable suites for the chunk.
```

See also [authored movement profiles](retail-pathfinding-profiles.md),
[proximity ownership](retail-pathfinding-proximity.md) and
[the engine ledger](retail-pathfinding-engine.md).
