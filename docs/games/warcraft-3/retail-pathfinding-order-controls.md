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
