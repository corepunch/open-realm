# Warcraft III Build/Repair approach routing

Build placement and Repair target two different things: the gameplay target can
be a structure, a mobile mechanical unit, or a build-site waypoint, while
movement must stop at a legal interaction point. A structure centre is commonly
inside its authored pathing footprint and must not be used as a reachable flow
field destination.

Repair keeps the gameplay target in `ent->build`, but routing depends on target
geometry. Buildings route toward a collision-safe point beside
`building->pathtex`; models without a footprint retain the collision-circle
fallback. Repairable mobile mechanical units keep the live unit itself as
`goalentity`, so movement follows its current position rather than baking a
static approach waypoint. Build-site travel uses the worker collision radius
when requesting direct/flow steering. This mirrors the resource-gathering rule
that a blocked resource/structure remains the behavior target while movement
owns a separate legal approach boundary.

The Repair walk-to-work handoff uses the **current** target distance only. For a
building this is the current pathing-footprint distance; for a mobile mechanical
unit it is collision-edge distance. The worker must actually be within Repair
`Rng` (plus `DataE` when the unit's authored movement type is `float`) before
entering `stand work`; adding one future movement step to that comparison makes
the walk state hand off early while the work state still sees the worker out of
range. That creates a walk/work loop in which construction progress and repair
HP never advance and the worker animation appears to restart continuously.

Human construction has two separate contribution rules.  The primary builder
always advances construction at `1.0`.  Repair `DataD` is only the time ratio
for additional power-build workers; a zero/missing `DataD` must not reject the
primary builder.

Construction owns the building `birth` sequence while
`ent->construction` is non-null.  `AI_HOLD_FRAME` prevents wall-clock animation drift,
but `G_UpdateConstructionAnimation()` maps authoritative
`construction.progress` onto the authored birth frame.  Construction pausing
therefore freezes both progress and the visible model frame.


## Build approach and Human Repair work own distinct heads (Payoff293)

The public `IssueBuildOrderById` native (`6f206a20`) validates placement and
order eligibility before creating or admitting a new order. Failure leaves the
previous user chain intact. Accepted Build passes the structure's rawcode
unchanged into `Unit_CreatePointOrder` and `Unit_AdmitOrder` with mode1/dispatch1.
The structure rawcode is already the current head when the synchronous issued
point-order callback runs. A nested accepted Move replaces that head and remains
in charge when the outer Build native returns.

This is distinct from Human work. `HumanBuild_PrependRepairWorkOrder` at
`6f544090` retires the passed task through `6f418b40`, resolves the Build ability's
Unit30 owner, queries and relocates its committed position, creates a target
Repair through `6f69bd80`, then calls `Unit_InsertTransientOrder` (`6f691c70`).
The actual Repair command comes from the Repair owner (`6f4358a0`), not from a
Build-to-Move rawcode conversion. The original helper has only a virtual-table
xref at `6fb1832c`; its three arguments are the new building, prior task and a
forwarded unused word, with `RET12`.

Two read-only original runs and an uninstrumented control retain all737 public
markers each. The128 selected native boundaries per observed run agree exactly,
including canonical Unit/head identity pairs. Eleven scenarios cover five
construction strategies, invalid replacement, completion, replacement during
work, and a nested Move in the Build-issued callback. Human construct-start sees
the structure rawcode; the following sample sees Repair, which remains visible
through construct-finish. Orc, Night Elf and Naga retain the structure rawcode
while inside. An accepted Move then waits until their builder is released.
Undead's structure head ends with summon work while construction continues.
These observations establish head ownership; they do not establish every
construction duration or intermediate task field.

The engine now installs the accepted rawcode in `G_ExecuteBuildOrder` before
notifying accepted-order observers. Successful primary Human Repair publishes
its ability owner's order name. The public/native replacement paths clear old
queued successors only after Build has been accepted. Rejection preserves them;
queued execution continues to retain its own successors. The change adds no
per-frame scan, allocation or save-layout field.

Three failing-first production-path regressions cover five authored race
profiles, invalid replacement, independent accepted replacement cancellation,
nested issued callbacks, cold engine saves and completion cleanup. The minimal
archive lacks the complete stock race set, so profiles select production race
strategies using authored rows; this is not a claim of full stock-map parity.
Before the fix,29 of117 assertions fail; afterwards all117 pass. The evidence
verifier also runs the neighboring construction, order lifecycle/subscriber,
interrupt and save suites in Classic and TFT.

No historical retail expectation is rewritten. The new map removes the Undead
structure's authored prerequisites, including its blight placement requirement;
the earlier rejected Undead map is archived as a diagnostic, not an accepted
five-strategy proof. The repeated test-loop setup initially retained stale JASS
triggers across VM teardown; its crashing runs are also rejected. The accepted
regressions isolate nested callbacks in a single world and avoid stale triggers.

ORDER-01.16 remains open. In particular, engine construct-start notification is
still queued after Human Repair starts, while the original notification sees the
Build head first. Original pending Build beneath transient Repair, queued Build
publication timing, the exact Undead summon clock and broader refund/death/reuse
inverses remain unported. This patch makes no full work-journey, retail-save,
performance or broad task-closure claim.

Reproduce the evidence guards and focused engine checks:

```sh
python tools/ghidra/verify_wc3_pathing_build293.py \
  --binary /path/to/retail/game.dll --data build/tests \
  --report /tmp/build293-fresh.json
```

`Build293Evidence.java` saves function names, comments, assembly and xrefs;
saved readback is identical and `MapPathfinding.java` retains the mapping.
Raw captures, failed attempts, map assets, sources and test artifacts are archived
under `research/ORDER-01.16/payoff293/` in the1.27 report root.
See also [building construction](building-construction.md).
