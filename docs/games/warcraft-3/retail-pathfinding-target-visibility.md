# Move target visibility and cached arrival

Payoff166 implements Move/Smart unit-target admission and cached hidden-target
arrival. The game now refuses an unseen target before replacing an active
order or appending a Shift successor. A running group keeps pursuing its last
sampled position while hidden. At that position it validates the target and
ends the Follow parent if validation fails; lifting fog does not revive it.

This advances TARGET-03.1/03.2. It does not close their broader visibility,
TargetLost producer, transient-state and policy compositions.

## Original contract

The original is Warcraft III 1.27.1.7085 `game.dll`, SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
Complete Ghidra function bodies were inspected and task-tagged annotations
saved and read back at the following addresses. `MapPathfinding.java` retains
these findings with earlier evidence.

| Owner | Address | Relevant behavior |
| --- | --- | --- |
| Target validation | `6f5fb940` | Null/dead or failed flags-0/mode-4 unit visibility returns `0xdd`; hidden unit returns cargo `0xa9` or `0xaa`, except its transient `0x800000` window. An ordinary non-unit widget returns zero without the visibility query; a hidden widget returns `0xaa`. |
| Order admission | `6f5fbad0` | A failed unit visibility query returns `0xba` before task mutation. Wider order/self/special branches remain open. |
| Target request | `6f05a5c0` | Stop/invalidate the retained local path before publishing a fresh target task. Preserve storage identity; discard counts, indexes and pending requests. |
| Member decision | `6f16a790` | While `unseen_counter` is nonzero, temporarily use a 0.49-fine-cell arrival range, then restore the authored range. |
| Completion | `6f16c390` | Persistent completion is suppressed while `unseen_counter <= 32`. Once eligible, the owning Move arrival handler validates the target. |
| Persistent arrival | `6f5fa7a0` | A valid target retains its Follow task; an invalid target unwinds it and permits the next order. |
| Approach arrival | `6f5ff8b0` | A valid target installs persistent Follow; an invalid target ends the approach. |
| Buffer reset | `6f168740` | Result-clear mask `0xcfffffff` retains the path's `0x01000000` warp-marker bit. |

Assembly is authoritative for the non-unit validation branch: the widget stays
in ESI while the optional unit bridge is held separately in ECX. The current
decompiler incorrectly renders the absent-unit branch as a null widget access.

## Repeated retail evidence

The delivered TARGET-03.2 archive has two observed loss runs and their public
control, plus two observed reacquisition runs and their public control. The
strict verifier pins every capture and preload hash, requires completed public
markers, checks observer/control equality and re-evaluates native policy
records. Six captures cover 17 loss policies, five reacquisition policies,
14,064 public markers and 754 hidden owner visits across observed repeats.

For the ordinary public fog-persistent scene, the engine fixture takes 238
unrounded native owner entries directly from the captured `gtick` rows. Each
entry has 25 words covering clock counter, refresh countdown, unseen count,
cached group destination, admission timestamps, route counts/indexes, scalar
position/velocity, retained arrival range and local retry/wait state. Empty,
unpublished destination columns are excluded from engine comparison; no
rounded report value is used as a numerical expectation.

Re-run the archive verification with:

```sh
python3 tools/ghidra/research/verify_target166_live.py \
  --expected tools/ghidra/fixtures/retail-target-visibility166-1.27.json \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --header games/warcraft-3/game/tests/retail_target_fog166.h \
  --output /tmp/target166.json
```

There was no new live retail capture in Payoff166. These are freshly verified
archived public captures; Ghidra inspection and engine execution are current.
Reproduction scripts and observers are kept in `tools/frida/research/`.

## Engine integration and save ownership

Move owns target status, admission, local-path invalidation and arrival cleanup.
The existing sampler already retained the hidden destination and countdown.
The new raw trace first failed at the approach-to-Follow transition, where the
old local cache survived. After clearing it at the actual task boundary, the
trace exposed premature hidden arrival: the engine used normal Follow range
and lacked the persistent 32-visit completion gate. Both are now implemented.

The public engine test creates and orders units through timed JASS natives,
starts fog through a real modifier and advances ordinary server frames. It
matches all 238 owner entries, then repeats the final 28 entries after saving
inside the hidden episode. Separate production regressions cover immediate
and Shift rejection without disturbing existing orders, hidden approach
completion, hidden persistent completion and no resumption after fog lifts.
Before the changes, the first three regressions failed 21 of 44 assertions;
the cold-save suffix subsequently failed because active fog was not restored.

Fog modifiers previously lived in VM-owned blobs while their active pointer
list lived outside the VM snapshot. Restoring a started blob never rebuilt its
membership. Save135 therefore gives modifiers stable game-owned records and
borrowed JASS handles. The save stream stores all records, including stopped
and script-unreferenced records, then active IDs in their application order.
The loader restores them before binding VM handles and does not reapply an
immediate Start operation. Aliases bind to the same record; destroyed handles
serialize as null. Registry storage and lookup are independent of active-list
order, and records stay address-stable when the registry grows.

Regression coverage also checks overlapping writes, unreferenced active
modifiers, stopped records, aliases, destroy/restart rejection, malformed
active IDs and Save134 rejection. Earlier save formats are rejected according
to the repository's current-layout policy.

## Focused validation

Classic and TFT each pass movement (386 tests / 5,654,221 assertions), API
(351 / 56,739), fog (6 / 213,649) and save (194 / 27,605). These are the
affected production suites; the full repository suite was not run for this
chunk under the authorized pathfinding checkpoint cadence.

## Remaining scope

The full archived policy matrix is research evidence, not a claim that this
chunk integrates every policy. Caller-specific flags 1/2, TLS/global visibility
producers, reveal fallback, transient blink/morph TargetLost ordering, point
reissue and broader attack/cargo/owner-change compositions remain open. The
multi-member blocked-arrival branch with the 20-visit and squared-distance-256
gates also remains outside this singleton implementation.

See [remaining task scopes](retail-pathfinding-todo.md),
[target refresh](retail-pathfinding-target-delays.md), and
[save/load](save-load.md).
