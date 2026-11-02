# Captain strategic structure destinations

`GROUP-03.4.7.3` continues in payoff373. The earlier Save209 selection contract
remains independently frozen. This work implements the `SuicidePlayer` producer
and the structure destination consumer reached after ordinary Captain selection.
Save212 persists the independent script-hidden owner. No TODO children are added.

## Retail evidence

`SuicidePlayer` callback `9c02d0` calls common admission `9cc850` with raw
selection flag80 enabled and flag4 disabled. Admission requires a Captain and,
when requested, the Captain's full bit. It does not reject an empty roster or
retreat state. It starts the independent selection owner; it does not immediately
issue Attack orders to members. The related registered `SuicidePlayerUnits`
callback `9c0360` uses flag80 disabled; that API remains outside this port.

After selection chooses a structure, `9d5e00` uses the authored stage point if
its X coordinate differs from the native -1 sentinel. Otherwise, reachable
selection (or raw flag100) and Town flag80000 call `9b8690`. `SetRandomPaths`
`9bfab0` owns this bit. Town initializer `9c7b20` sets defaults80181 for players
other than Neutral Aggressive12, so random paths default on for ordinary Towns.

`9b8690` builds a temporary graph from the existing adaptive ground level1 map.
It does not rebuild terrain or choose a random offset around the structure.
The observed input has100x68 cells, scale4 fine cells (128 world units), and
world origin(-7168,-3072). `CPaCell` word1 bits30..31 supply the ground class.

`9b7080` constructs the graph:

1. Convert class0 to `.`, class1 to `#`, and other classes to `?` in a256x256
   scratch plane.
2. Repeat four immutable native thinning tables until stable, then perform the
   in-place cardinal-pattern pass and identify junctions with the native
   neighbor-count table.
3. Collect junctions in row order and trace edges with the original cardinal
   and diagonal traversal order. Cardinal cost is169; diagonal cost is239.
4. Query the requested player's structures with native selector24/token2.
   Mark the nearest graph node within640, with the last node winning exact ties.
   Expand the marks over weighted graph distance<=2500.
5. Find the Captain's nearest node within640. Retain marked destination nodes
   connected to it through unmarked intermediate nodes. Source equal to target
   still requires an edge path, matching native traversal.
6. Prefer destinations within5000 world units of the selected structure. If any
   destinations exist, consume one further purpose13 random draw and publish the
   chosen graph coordinate. With no eligible destination, retain the original
   point and consume no additional draw.

Two independent Frida observations agree on all13600 input words, source
positions,65536 output grid bytes,67 nodes/1072 words, filtered visitation state,
and destination lists[3,8]. The unhooked control agrees on all331 public markers.
The first purpose13 draw selects the structure; the second chooses node8 and
publishes(-2304,256). Actual state transitions are preserved in the oracle:

| Boundary | Sum | Index |
|---|---:|---:|
| Before structure ranking |2851754017|1215875280|
| After ranking draw |809192828|1147973812|
| After destination draw |2186210856|1080072344|

Ghidra retains named functions, complete constructor/consumer decompilations,
and repeat/control notes. `MapPathfinding.java` tracks the constructor and
consumer separately from the earlier frozen selection rows.

## Engine implementation and verification scope

`wc3_ai_regions.h` implements the native transformation and graph traversal.
Its bounded iterative tracing preserves traversal order without making host
stack size depend on map corridors. Positive-weight neighborhood expansion uses
shorter-path pruning while retaining the native final marks. `g_world.c` lends
the existing cached ground level1 classes to the consumer, preserving hierarchy
ownership and delayed terrain publication. The temporary graph is released after
publication. Its temporary graph creates no persisted state.

The ordinary public producer regression was recorded before implementation:
17/21 assertions passed in each edition, with exactly four failures for the
missing selection request, raw80, selected primary, and selected state3. The
same failures appeared in a guarded immutable external baseline and the normal
compiled game module. All setup and admission guards passed.

Independent C kernel replay matches the entire native grid, graph, visitation
state, ordered candidate lists, both actual RNG states, and point bits. The
normal engine suite also contains this exact input regression. A further
production boundary replay loads the original classes through the existing
adaptive-state restore API, creates the three real structures at captured
positions, restores the Captain pose and captured ranking boundary, and runs the
actual selection timer and publication path. This is a publication-boundary
replay, not a full-map journey comparison. The six compiled regressions pass139 assertions per edition after the Save212
port. The earlier independent selection gate remains three tests/72 assertions
per edition. The strict contract retains3611 original byte guards, seven hooked
observations, three unhooked controls and1050 public markers across three scenes.

The portable oracle is
`tools/ghidra/fixtures/retail-captain369-regions-1.27.json.gz`.
`freeze_captain369_regions.py` checks independent inputs/outputs and public
control markers before freezing it. `emit_captain369_regions_test.py` generates
the C regression data from that oracle. The original masks can be re-exported
with `Captain369ExportRegionTables.java`.

Broader transport policy, Town preferred-primary overrides, and the remaining
Captain APIs stay in the existing leaf. Complete retail-map journey parity is
not established by the kernel or publication-boundary replays.

## Physical world absence and gameplay invisibility

The structure query uses Unit+5c forbidden bits0x118: world retirement0x8,
loaded cargo0x10 and death0x100. Gameplay invisibility is a separate owner. A
fresh public `ShowUnit(false)` structure survey excludes the retired Town Hall
from the actual source callback. Its two observations and control agree on332
public markers. A separate scene casts real Wind Walk on a Paladin, then hides
and shows it. Both observations and the unhooked control agree on387 markers.
The active buff and native invisibility depth remain present throughout. The
actual Captain query includes the invisible unit before hiding and after showing,
and excludes it during physical absence.

An additional387-marker observation records target-loss chronology. Wind Walk
notifies from68b7d8 with the world-present flags intact. `ShowUnit(false)`
notifies from688373 after publishing Unit+5c bit0x8 and the Widget hidden state.
Showing the unit clears physical absence without clearing its invisibility depth.
The viewer-relative public `IsUnitInvisible` result for the owner stays zero in
this scene; it is not used as an intrinsic invisibility-state assertion.

The engine now stores `script_world_hidden` independently of rendering and
invisibility owners. ShowUnit publishes this owner before target-loss callbacks;
showing restores physical presence while preserving active invisibility. Generic
render-hidden callers do not acquire this script owner. Source acquisition,
Captain query admission, Move target status and player-relative visibility use
physical presence where required. Save212 serializes the owner so cold loading
cannot restore query membership prematurely.

Before this port the six normal compiled regressions passed125/139 assertions
in each edition. The14 failures were four graph-source point/RNG mismatches,
one hidden-query admission mismatch, and nine real Wind Walk/ShowUnit/inverse/
save mismatches. All setup, accepted-cast, active-invisibility and cold-save
guards passed. After implementation the same139 assertions pass. The external
immutable module baseline additionally guards447 sizes/offsets and the loaded
module path/hash; normal compiled regressions remain the final gate.
