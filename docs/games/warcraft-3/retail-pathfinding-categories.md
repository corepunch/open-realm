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

## Payoff137: exact raw-cell consumer eligibility

FOOT-03.1 closes the consumer truth table and ports its gates to the engine.
`common/wc3_pathing_cell.h` owns one encounter walk for fine admission, hierarchy,
blocker collection and category union. `g_world.c` uses it for actual fine search,
segments, endpoint placement, next-step blockers and adaptive base rebuilds.
The ordinary owner adapter reads current authored categories and transient Move
flags; retained identity suppression/activation remains independent of the owner.
The pooled record gains a category word (40 bytes per object, not an edict field).
Ordinary saved identities still reconstruct their authored category/active state,
so Save121's serialized layout is unchanged.

The gates execute in this order: terrain/bounds, nonempty-cell stamp, metadata
skip, dead/active/previous-stamp eligibility, then stamp the object before record
kind, suppression and category tests. A removal therefore hides its older insertion
within the cell. Inactive records neither observe a target nor receive the stamp.
Fine target observation precedes suppression; ordinary mode ignores moving/group
flags60000000, while endpoints and collectors include them. The collector's32
tokens cap only appends; its eligible suffix still receives stamps. Region/item
payloads produce NULL tokens. Union keeps terrain's high byte and ORs low24
categories independently of the query/mode.

Hierarchy eligibility additionally requires REGION10000000 before stamping.
Its49-record limit counts every raw link, including metadata, removals and inactive
objects. The cap is a verified retail rule, not a new scheduling shortcut. Ordinary
movers never affect hierarchy classes. Rebuild order is coarse-cell row-major,
then06/80/40/04 lanes, each with TL/TR/BR/BL fine-cell queries. Stored lane
indices and saved hierarchy bytes retain their existing representation.

### Evidence and regression coverage

The existing category verifier now executes the actual production header through
`wc3_cell_query_probe.c`. **50,112 original/model/C cases per -O0/-O2 build**
compare results, target flags, token identities and every map/object stamp; the
frozen research matrix remains unchanged. Another36 original/C cases per build
check terrain-first rejection and category-union high bytes. Synthetic unknown
record kinds and forced stamp collisions remain labelled fixture controls.
The unchanged16 original mixed-producer scenarios and both complete Frida
captures still verify328 live consumer calls and60 observer-free public markers.
This chunk reuses those controlled captures; it does not claim a new live run.

A fresh complete original15d360 call freezes the terrain80-corner witness in
`retail-cell-hierarchy-order-1.27.json`: from stamp1000 the map finishes at1015
and clockwise object stamps are1012/1013/1014/1015. Saved Ghidra readback
`retail-cell-consumers-ghidra-1.27.json` preserves seven consumer/rebuild
annotations; the reproducible mapper includes each finding.

Three actual engine regressions first fail45 assertions for suppression, active
state,49/50 hierarchy eligibility and collector suffix order. A separate rebuild
ordering regression then fails four final-stamp assertions before changing its
traversal. All12 fine-spatial tests pass461 assertions afterward in Classic/TFT.
Focused Classic/TFT validation passes690 engine tests and7,984,720 assertions
per edition (movement, routing, save/load, categories and spatial consumers).
55 Python evidence/inventory checks pass, and a fresh strict
`oracle-object-categories` report is verified. This is implementation commit2
after the successful Payoff135 full checkpoint; the next full suite remains at
the authorized batch boundary. Production and test release modules build; all12
fine-spatial release tests pass461 assertions in each edition. Five alternating
12-mover/4000-scenery smoke runs have medians0.43ms in the prior release module
and0.42ms after this port (ranges0.36–0.46/0.38–0.51ms per100ms simulation
frame). This is a regression smoke check, not4096-unit or path-budget acceptance.
Run the category verifier command above and the engine filter
`+test 'wc3_fine_spatial.*'` to reproduce the focused checks.

### Complexity and remaining producer work

The traversal allocates nothing and scans the encountered chain directly. Fine,
collector and union cost O(raw links); hierarchy examines at most49 links. There
is no rank reconstruction, candidate sorting or global owner scan in a cell query.
The collector continues stamp work after capacity, as required by retail.

**FOOT-03.2 remains open.** Real widget footprints are still baked into the
existing static fallback; the hierarchy also queries any published raw region
identities. Moving all widget category regions into retained sparse raster records
and preserving retire-versus-unraster reference counts is the next producer chunk.
This closure establishes the consumer table, not the complete mixed static/dynamic
publication lifecycle or a new 4096-unit performance acceptance. Region suppression
scopes and full world flag-query producers retain their existing MAP/FOOT owners.

## Payoff138 widget regions and mixed lifetimes

FOOT-03.2 is closed with engine producer integration. Move now owns a small
collection of stable pooled C2, item10, building08 and optional flight04 region
identities for each static footprint. Rotated pixels prepend one link for every
matching category, in native row/pixel/category order. Ordinary unit and item
identities remain independent. Fine queries, segments, endpoints, collectors,
category union and hierarchy all read the shared retained record map. Widget
blockage no longer short-circuits those encounters through a merged terrain byte.
Bridge support still modifies its independent terrain baseline; legacy flow
fields retain their baked mask. Native32-unit geometry takes the direct software
scalar path. Synthetic grids retain their existing scale.

`063e50` clamps each raster sample before conversion; `22e9c0` uses
(width−1)*16 and (height−1)*16 half-extents, software32 increments and a primary
axis reset at each row. A collection retired through063b40 makes its objects
immediately dead, adds no removal records and dirties no cells. Inverse raster
through650a70 instead prepends removal records. Removing an ordinary owner adds
its own inverse rectangle independently. Destructable death/restore and entity
free retire old regions; position/footprint changes inverse-raster the retained
previous pixels before publishing their replacements.

The [FOOT-03.2 handoff](retail-pathfinding-handoffs/FOOT-03.2/HANDOFF.md) supplies
16 original producer scenarios: two insertion orders, unit retire/move-away,
widget retire/unraster and two removal orders. The verifier freshly executes
unchanged original code, checks the frozen output, then repeats every seven-stage
sequence through production C algorithms at-O0/-O2. All112 states per build match
raw cell chains, per-object categories/flags/stamps/reference counts, map stamps,
link counts/free counts/dirty bits, and every fine/perimeter/segment/endpoint/
collector/union/hierarchy result. This includes the difference between five
retained references per retired widget region after dirty-only compaction and
zero after inverse rasterization. Two complete supplied Frida captures are
validated against each other and the observer-free60-marker control;328 consumer
checks remain exact. They are reused evidence, not newly recorded captures.

The actual game regression reproduced18 failures before implementation. Engine
regressions cover both insertion orders, all16 reference-count scenarios, real
destructable death/restore and sparse save/load followed by inverse raster. Fractional moves within the
same owner cell publish their changed pixel membership. A separate test-first
flight adapter correction retains ordinary active stamps before category misses.
Save122 stores compacted region cell memberships and cached normalized pixels,
not pointers or an enclosing solid rectangle. Owners load in entity save order;
ordinary objects precede that owner's region slots. Save transposes memberships
once in O(cells+links+objects), rather than scanning the map per region. Older
save versions are rejected under the repository's format policy.

Ghidra's seven producer annotations are saved and exported in
[retail-widget-regions-ghidra-1.27.json](../../../tools/ghidra/fixtures/retail-widget-regions-ghidra-1.27.json),
with matching reproducible MapPathfinding rows. Authored resource flag/decoder
inventory and unusual channel thresholds remain BASE-02.1; the native composed
fixture supplies decoded category bytes. Existing engine TGA inputs retain their
boolean red compatibility. Full layered bridge and uncommon public widget
producer coverage remain their existing tasks. This chunk makes no new4096-unit
performance or complete-map retail fidelity claim.

Focused debug validation covers742 tests and8,011,707 assertions per Classic/TFT
variant, including the complete349-test movement suite. Release producer,
destructable, save, category and performance suites pass. The twelve-mover/4000
scenery smoke measures approximately0.47ms per100ms simulation frame on this
host. Five alternating old/new smoke captures have medians0.51/0.52ms;
this difference is within run noise, and is not a4096-moving-unit benchmark. The strict fresh category corpus
checks both original/model/C consumer matrices and composed region lifetimes;
48 focused Python evidence/storage/corpus checks pass. Full repository validation
remains at the user-authorized batch cadence, three implementation commits after
the Payoff135 checkpoint.
