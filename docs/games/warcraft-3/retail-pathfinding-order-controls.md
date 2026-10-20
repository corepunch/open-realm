# Queue admission and owner-change controls (ORDER-02.2)

## Evidence and scope

The recovered Claude worktree `agent-a7a1cc22776ff3841` contained the unfinished
`t_order022.c` regression and six probe/input sources. The separate runtime
archive contained a proposed engine patch, static contracts and completed native
captures. Recovery starts from main `1e1194cbb`; none of the old worktree's
tracked files are applied wholesale.

`retail-order022-live-1.27.json.gz` retains the literal JSONL and CRLF preload
bytes from two observed repeats and one unhooked control for each of two arenas.
There are 42 scenes and 1,524 public markers. The failed UI repeat2 is retained
as a rejected attempt: its loading helper timed out and no preload was produced.
It contributes no behavioral evidence. The four successful observed streams have
contiguous sequence numbers and balanced callback entry/exit stacks; each stream
matches its own preload exactly. OS input delivery changes some event times.
Public event ordering and payloads agree across both repeats and the control
when only `tick` and `age` are removed; sample records remain literal evidence,
not a claim of identical scheduling or numerical trajectories.

The probe/capture/map-builder/helper source hashes match captured provenance.
`retail-order022-contracts-1.27.json.gz` preserves the recovered static contracts;
371 cited instruction guards match game.dll SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
Descriptive names and interpretations are recovered evidence, not debug symbols.

## Ownership transfer

Original `698ce0` first checks whether the owner changes. At `698d68..698d9b`,
an active unit constructs immediate Stop851972 with the **old owner**, then
calls `680320(mode1, dispatch1)`. Widget-state bit1 or admission-state bit0x100
instead selects `673e80`, canceling successors without replacing the head.
Owner event publication follows admission. The idle, walking, attacking and
three-order Shift-chain scenes all show the synchronous Stop event while the
old owner is still visible, followed by the owner event with the previous owner
and an empty current head.

`G_SetUnitPlayer` must own this Stop. Move's former walking-only silent cleanup
missed idle notifications and left non-Move heads alive. The recovered fix uses
`S_IssueStopOrder` directly: ownership transfer has already selected the core
producer and must not re-enter public ability/upgrade validation through
`unit_issueimmediateorder`. Move still unlinks its requests and publishes the
new movement class through its existing owner-change events.

`G_CancelUnitOrderSuccessors` distinguishes the current engine representations:
an executing head lives outside the pending ring, while removal/construction
suspension retains its head at ring entry0. Cancellation preserves that entry
and its identity, notifying only the removed successors. Same-owner calls do
nothing. Deferred-release state is checked through save/load and final release;
this engine regression does not constitute an additional native live witness
for every removal/construction composition.

## Repair interception

Original `680320` calls the current ability interceptor `69b2f0` before queue
replacement. A positive interception in mode1 reaches `673e80` at caller-return
`680383`; it does not dispatch a new head or issue an order event. Repeating
concrete Repair or Smart on the current working target retains the same public
Repair head and internal task while canceling the queued Move. The untouched
control dispatches its queued Move when Repair completes. Both repeats and the
unhooked control preserve these event contracts.

The generic `ABILITY_ORDER_INTERCEPTED` result therefore cancels successors and
returns success silently. Repair continues to own target/alias validation and
the distinction between approach and established work. The previous Payoff122
statement that interception retained the FIFO and published a new target event
was incorrect for these native witnesses.

## Construction and production survive user-head retirement

The first full recovered checkpoint was rejected: `make test` exited2 with
9,998 native tests, four failed assertions across34 JUnit reports, and2,957
passing Python tests. Both editions failed the existing constructing-Altar
self-link checks. The code path was Stop -> `unit_stand` ->
`unit_prepare_stand`, whose unconditional `build=NULL` erased state belonging to
construction/production before `G_SetUnitPlayer` could cancel and refund it.

A real paid `SP_TrainUnit` admission reproduced six orphan/refund/Food failures
across fresh and saved continuations. A real `G_QueueHeroRevive` admission with
non-stock Misc costs reproduced six further failures after ownership transfer:
the Hero retained its revive owner and paid amounts, and the old player did not
receive the refund. An initial revival reproducer misspelled
`ReviveLumberLevelFactor`; that rejected admission is a fixture failure and
contributes no behavioral proof. The corrected test proves successful paid
admission before asserting the failed cancellation/refund.

Stand now retires a transient worker target while preserving a construction
self-link, a training head, or an active revival head whose producer is the
standing unit. Their existing owners retain responsibility for removal,
cancellation and refunds. This changes cleanup ownership, not serialized field
layout or meanings. The focused gate includes the entire unit lifecycle suite
as well as the order lifecycle and new owner regressions. Broader construction
command/task parity remains with its existing TODO owner.

## Cast protection remains an implementation gap

The UI arena supplies a Channel clone `AQ22` of `ANcl`, with6 seconds of generic
casting time and8 seconds of follow-through. Original order+0x20 bit0x40 protects
the CHANNEL-to-EFFECT interval. An ordinary Move during that interval enters
`691c70(after_head1, dispatch0)` behind the existing spell. Shift instead follows
its normal append path and marks order bit4. Stop is exempt and interrupts.

After EFFECT, active channel work is protected only when another selected
recipient is not working (`ctx+3c==0`). Thus the mixed caster/Footman selection
moves the Footman immediately and retains the caster's spell until FINISH;
a single working caster is interrupted. Hold is also exempt. All seven finite
UI scenes are preserved and audited. **Their capture audit is not an engine
implementation claim.** Subgroup selection, arbitrary spell/task producers and
all-channeling selections require their own engine regressions and remaining
producer coverage before ORDER-02.2 can close.

Static order bits0x2/0x4 have no identified reader, while0x10/0x20/0x80/0x100 have
no identified producer in the recovered scan. These are search limits, not
unreachability proofs. Broader transient suppression belongs to ORDER-01.13;
Harvest/morph preserving interruptions retain their existing owners. The
original field/branch inventories remain inventories rather than runtime parity.

## Verification

`tools/ghidra/research/order022_admission_oracle.py` executes original `680320`
for128 dead/depth/mode/interception/head/drop-bit combinations and the original
owner-admission slice for12 widget/dead/old-owner combinations. Callee boundaries
are instrumented: the oracle does not execute replacement models of the retail
container or ability tasks. This isolates branch/ABI behavior; the live captures
supply genuine Repair interception, queue changes and public notifications.

The recovered two-test baseline failed11/63 assertions per edition. Extending
Repair to the real frame scheduler and saved continuations failed17/107 before
the fix; the initial recovered fix then passed107/107 per edition. The final focused gate passes101 tests /19,851 assertions in each edition,
including145 assertions in the original three new regressions and all98
existing order-lifecycle tests. The construction/production corrections then pass279 tests /71,777 assertions
in each edition: five new regressions with193 assertions, all98 order-lifecycle
tests and all176 unit tests. The transient-worker-target Stand test now uses a
separate target edict; a self-link represents construction and is covered by the
two ownership-transfer regressions. The target remains alive after the worker
releases its link. The second full checkpoint rejected that snapshot: the existing trained-unit
completion regression failed once per edition because the completed child still
retained its successor link. All five affected native-oracle gates passed, but
that does not override the full-suite failure.

Training now explicitly detaches the completed child's link after a successful
exit placement; the producer already retained the successor before revealing
the child. Failed placement preserves the queue. A new real-frame regression
completes both paid trainees, with a save between completions, and verifies
independent children, producer queue retirement and unchanged reserved food.
Its initial food assertion also exposed retained player statistics between the
two fixture scenarios; each scenario now initializes its own food total.
The six owner regressions and four existing trained-unit regressions pass
10 tests /255 assertions per edition. The complete focused gate passes
284 tests /71,839 assertions per edition, including all98 order-lifecycle and
176 unit tests.

The frozen source tree `3121617f2f8b4083494fa28cd4754c4c9edf8ea8` passes the
actual full checkpoint (`make test TEST_JOBS=16`, native SDL2): exit0,
10,004 native tests in34 JUnit reports, no failures/errors/skips, and2,957 Python
tests. Fresh Captain376, Captain473, Waygate377, Group012 and support/goal gates
all exit0. The full log, immutable JUnit copies, source identity and durable
exit status are archived under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/codex-takeover-20261020/order022-training-final/`.
Both rejected full checkpoints remain preserved. This accepts owner-change,
Repair interception and their construction/production cleanup corrections;
UI cast protection is separate unfinished work. ORDER-02.2 stays open.

Run evidence and engine checks from the repository root:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_order022.py \
  --binary /path/to/game.dll --report /tmp/order022-fresh.json
python3 -m unittest discover -s tests -p test_wc3_pathfinding_order022.py
```

The verifier refuses an existing report. `--evidence-only` omits engine runs and
reports that omission explicitly. Twenty-three rejection/contract tests check
source provenance, literal CRLF bytes, ownership/task retention, silent insertion,
Shift marking, callback completeness, inactive admission and the full branch
matrix. Save218's layout and serialized fields are unchanged by these admission
fixes; existing format rejection remains mandatory in the broader checkpoint.

See also [order queues](order-queue.md), [Repair admission](retail-pathfinding-engine.md#repair-families-own-admission-work-and-pending-activation),
[remaining work](retail-pathfinding-todo.md), and [save/load](save-load.md).


## Unmerged player-packet continuation

The separate `/tmp/open-realm-takeover-order022-ui` worktree now starts from
accepted commit `28e64f588`. Its current changes are **not merged or accepted**.
The original `6b8cc0` selector has been executed for10,560 input combinations
against its PE-verified bytes. The engine's pure selector header agrees at
both O0 and O2; real `6bccc0` also executes for8,191 selection masks up to12 units.
Head resolution and primary-subgroup queries are explicit boundary inputs;
this does not execute replacement models of the native order container.

Production Move and Attack point/target paths now call that selector. The cast
query derives protection from the existing saved phase/commit state, including
EFFECT callbacks and the concrete effect body. Work queries use the channel
owner, primary Human construction owner, and ItemTeleport's saved exclusive
contribution. A generic spell backswing is not classified as work. Player-only
replace/append/fallback modes keep script admission independent. Recipient and
wait decisions are retained before callbacks, with generation/control checks at
actual admission. No new saved state or Save218 layout change is introduced.

The built Move matrix passes836 assertions, including two authored timing
profiles, saves, Shift, Stop/Hold, all-working and scripted controls, successor
replacement, an immobile selected non-worker, and repeated targeted clicks.
The repeated-click fixture must press CmdMove again after its first completed
point exits target mode. A debugger confirmed that omitting this step leaves a
null callback and ignores the second point; that fixture failure remains
preserved and is not engine evidence.

New Attack point/target tests first fail140 assertions per edition. The
player-mode implementation then passes all3 UI tests /2,092 assertions and,
with five ItemTeleport and98 order-lifecycle controls,106 tests /22,032
assertions per edition (`takeover-ui-attack-fixed7.json`). Those results precede
the latest Smart changes and cannot certify them. Full checkpoint, source-pin
updates and broader UI/spell/item producer coverage are still required.

The exploratory Smart target test initially assumed Attack851983 after an enemy
right-click. That expectation was held unaccepted and checked against a fresh
retail arena. Two read-only observations and an unhooked control now agree on
all72 public head markers each, after removing only tick/age. Literal asset
preloads, marker timestamps and complete observer streams are preserved in
`retail-order022-smart-head-1.27.json.gz`, together with the actual authored map
and owned-window input helper. They establish these separate producers:

| Producer | Public/current head | Admission |
|---|---|---|
| JASS Smart on an enemy | 851971 | Synchronous, including target notification |
| JASS explicit Attack | 851983 | Synchronous |
| Real enemy right-click from idle | 851971 | Synchronous |
| Real enemy right-click during spell windup | 851971 | Silent insertion after the protected spell; later notification |
| JASS Smart during spell windup | 851971 | Replaces the spell synchronously |

The owned helper now supports an actual right mouse button. It remains confined
to the launched game's PID/window. Input pixel650,300 was calibrated from the
previous native Move capture's world point1277.12097,511.36835; target user-data2
in every issued event verifies the click actually hit the intended unit.
The two observations use research environments B/C; neither the owner's A
session nor its display is accessed. All three captures finish normally and
both observed callback stacks unwind to zero. These are public head/admission
contracts, not numerical trajectory or arbitrary spell/item queue parity.

The original test expectation851983 is corrected to native851971. The corrected
UI matrix plus a new real JASS/notification/save/next-frame regression then
reproduces34 actual stale-head failures per edition. Attack's accepted-order
hook previously omitted Smart, leaving the old spell ID (or idle0) while Attack
already executed. That hook now publishes Smart851971 only while Attack still
owns the physical task, preserving the existing guard against callback-installed
replacement owners. Automatic acquisition does not pass through this public
accepted-order hook and keeps its parent command.

The updated focused native/engine gate passes295 tests /75,523 assertions per
edition (`takeover-ui-native-smart-fixed11.json`). It includes six UI tests,
all owner/order/unit/trained-unit regressions and ItemTeleport controls. The
new14 evidence rejection tests reject an Attack ID substituted for Smart,
lost real right-click input, changed native head/task state, hooked controls,
broken provenance and altered literal bytes. A full UI checkpoint, source-pin
updates and the remaining callback/producer work are still required.

The cancellation continuation now drives the actual Attack target producer
while a spell is protected and an old Move successor is pending. A real
cancellation procedure callback frees/reuses the target or subject through
`SP_SpawnAtLocation`, including the allocator cooldown, or removes the target
without replacement. The replacement subject owns a separate Move777 queue as
a control. Each case also runs through cold save and natural spell completion.

The first fixture omitted the minimal UnitData ground category and map-startup
waypoint reservation; its23 failures and waypoint errors are rejected fixture
evidence. After correcting those preconditions, the valid test fails17
assertions; adding the replacement subject's own queue control fails18/140 per
edition. It reproduces capture of the new target generation after cancellation,
loss of a removed-target command, and old admission/cleanup touching the reused
subject's queue.

Packet construction is now shared with ordinary append but runs **before**
player fallback cancellation. The retained target number, generation, category
and issue-time point survive callbacks. Subject liveness/generation is checked
after cancellation, before insertion. Both full and suspended-successor queue
clearing stop when a callback changes the subject generation, so the old clear
cannot erase the new actor's queue. Singleton Move fallback uses this same
player admission path. No queue layout or saved field changes are introduced.
The strengthened callback regression passes155 assertions. The complete gate
passes296 tests /75,678 assertions per edition
(`takeover-ui-cancel-fixed16.json`), including all7 UI tests /3,605 assertions.

The selected-group pending-request suspicion did not reproduce. The expanded
callback regression drives single Move and selected Move with a protected caster
and idle peer, reuses the caster during successor cancellation and gives the
replacement its own Move777 queue. Both editions pass267 assertions, including
the surviving peer's singleton group, request/history identity, cold save,
natural arrival and release (`takeover-ui-selected-reuse17-*`). Although
`move_request_drop` sees the new live generation, `move_request_publish` filters
the old candidate through `move_request_member` before counting pending members.
Do not change that path based on the earlier suspicion.

### Selected target removal during synchronous publication

`retail-order022-selected-target-1.27.json.gz` preserves two complete read-only
retail observations and an unhooked control, their literal JSONL/preload bytes,
the actual map and owned-window helper binaries. Two selected Footmen receive
real right-click Smart and A+left-click Attack. The first issued-target callback
removes target user-data2 and creates user-data4 elsewhere. Both recipients
still publish a target event for user-data2, in the same order, with the first
recipient's new public head still visible in the second callback. Smart owns
851971; Attack owns851983. All30 notification-contract markers agree across the
three captures after removing tick/age. All84 public markers are retained;
sample and terminal trajectories are not compared or certified.

The observed `net-order` caller6ba4c9 lies in
`NetOrder_DispatchPointSelectionWithTarget`6b9f70. It resolves and retains the
target before selection, runs `NetOrder_AdmitPointTargetCandidate`6ba800 for all
recipients, sorts the rows, then constructs/publishes each order using the same
retained target. The target reference is released after publication. The two
packets in each observed scene retain identical original canonical target and
point words, including the packet published after the removal callback. Cached
decompilations are in `runtime/claude-order022/raw/` under
`/GitHub/wc3-analysis/`; the6ba800 export from this continuation is
`c_decompile_function_address-0x6f6ba800-selected-target.json`.

The matching real `G_ClientCommand`/JASS engine regression is
`wc3_order022_ui.selected_target_notifications_retain_original_target_after_callback_remove`.
It first fails2/19 assertions per edition (`takeover-ui-selected-target-red18-*`),
one each for Smart and Attack. Debugger traces identify the actual divergence:
Smart never publishes the peer event; Attack publishes a point event at the
old target position. Target callbacks use real `RemoveUnit`/`CreateUnit`; the
old target remains deferred and its incarnation stable. The current selection
snapshot captures recipient/mode only, and per-recipient validation runs after
the previous callback: Move rejects the newly hidden target and Attack converts
it to a point. This is a confirmed engine gap, not a fixture failure.

`G_PreparePlayerTargetOrders` now captures target packets and queries all
recipients before any publication. The later `G_IssuePlayerTargetOrder` uses
the retained admission/packet instead of repeating target classification.
Same-work interception remains a dispatch decision. Suspended and fallback
insertion use the already captured packet, including after cancellation.
`unit_activate_user_order` is shared with ordinary FIFO activation: it publishes
the user head and notification before physical task execution, preserving
callback replacement/Stop/removal guards. Selected publication may notify a
deferred original target; physical execution separately resolves its current
canonical lifetime and retained-point continuation. No serialized queue fields
or wire fields change. The native-backed regression passes19/19 per edition.
The full focused gate passes297 tests /75,809 assertions per edition
(`takeover-ui-selected-target-fixed20.json`); this is before the later feedback
fix and enlarged callback matrix.

The callback matrix additionally submits real Smart commands, singly and with
an idle peer, while cancellation frees/reuses the common target or source.
The reused source owns an unrelated Move777 queue. Retained-target lost/reused
cases execute toward the original point, preserve saved queue generations and
never follow/attack the newly created target. An actual serialization observer
checks the Smart acknowledgement and includes a positive indicator-packet
control. An initial extension omitted the peer's attack profile and assumed a
rejected singleton source-reuse command would acknowledge; that mixed red21
result is excluded. After correcting those fixture preconditions, the valid
red23 result fails4/644 assertions per edition: Smart flashes the replacement
target's ring in both single/pair and fresh/saved scenes.

`CMD_Smart` now captures the clicked target's generation and walkable policy
before dispatch. It sends a ring only for that still-live incarnation, and
reads the prepared Rally decision rather than a possibly reused source. The
callback regression passes644 assertions; all8 UI tests pass4,113 assertions.
The expanded target controls (UI, Rally, Waygate, destructables, mixed-selection
item pickup, building Smart actions, current-order and palette APIs) pass106
tests /78,109 assertions per edition (`takeover-ui-target-controls24.json`).
New headless UI arenas replace the absent server transport with serialization
observers/sinks throughout their lifetime, avoiding meaningless write-buffer
overflow output while keeping real gameplay dispatch and frame execution.

New17 evidence tests reject substituted targets, missing peer target
events, retired first heads, altered canonical packets, bad input/provenance and
hooked controls. The main ORDER022 verifier runs this audit before its engine
gate. Broader held-order, spell/item/Repair producers and remaining control bits
remain unfinished; the finite integration acceptance is recorded below.
This closes no TODO.

The first full candidate (`order022-ui-full25`, source81ca1f3) is rejected.
A direct run of the existing
`wc3_movement.smart_walkable_bridge_falls_back_to_clicked_ground_point`
test reproduces a null-goal assertion followed by its unguarded dereference
(red26, exit139). Preparing immediate Smart admitted the unsupported widget
before physical rejection, making `CMD_Smart` report success and skip its
clicked-ground fallback. The owned full run was deliberately terminated on
that confirmed regression; its actual make status is-15 after120.77 seconds,
with unchanged source. Its partial reports are not full acceptance evidence.

The existing walkable-surface candidate filter now applies before every
prepared dispatch mode. Attackable walkable debris keeps entity precedence;
non-attackable bridges retain the original clicked-ground fallback for both
immediate and Shift commands. All33 registered Smart movement controls and
the8 UI tests pass41 tests /460,954 assertions per edition
(`takeover-ui-bridge-controls28.json`). The core verifier includes these33
controls by exact registered name. An attempted interior `*smart*` wildcard
matched zero tests and is excluded: the engine runner supports trailing prefix
wildcards, and each generated exact selector must execute a nonempty test.
Native selector10560 cases, selection8191 masks and both-O0/-O2 production
predicate comparisons also run afresh through the main verifier's binary gate.

### Integration acceptance (2026-10-20)

The corrected frozen source tree is
`cb342a1a014b8605f2ed572938e2792a693d0306` (37 changed/new files over28e64f588).
`LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 make test TEST_JOBS=16`
finishes with actual exit0 after1,025.31 seconds:10,020 native tests across34
fresh JUnit reports, zero failures/errors/skips, and3,041 Python tests
(2,988 pathfinding tests plus53 other Python checks). The source remains
unchanged through the full run and all six subsequent affected native gates.
Acceptance artifacts and immutable JUnit copies are under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/codex-takeover-20261020/order022-ui-full28/`.

All native gates pass in both editions:

| Gate | Tests per edition | Assertions per edition |
|---|---:|---:|
| Captain376 | 53 | 3,330 |
| Captain473 | 10 | 156,882 |
| Waygate377 | 39 | 229,803 |
| Group012 | 20 | 1,146,726 |
| Supportgoal | 472 | 2,000,939 |
| ORDER022 | 330 | 533,027 |

The ORDER022 gate additionally executes128 original admission cases,12 owner
slices,10,560 UI selector cases,8,191 selection masks and both-O0/-O2 production
predicate comparisons, while checking371 original instruction guards and the
literal Smart/selected-target repeats and controls.

All37 accepted files are verified byte for byte when applied to main. Main's
production/test/app build succeeds without compiler warnings, and its fresh
ORDER022 gate again passes330 tests /533,027 assertions per edition. Only the
final documentation and its corpus fingerprints differ from the frozen tree.
Network fields and the Save218 format are unchanged. Wider producer/control-bit
coverage and concrete spell/item/Repair compositions remain in the open backlog.
