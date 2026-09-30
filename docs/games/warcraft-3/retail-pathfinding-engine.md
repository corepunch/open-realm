# Numerical pathfinding integration

## Implemented slice

`games/warcraft-3/common/wc3_math.h` implements the retail 1.27.1.7085
software scalar add, subtract, multiply, divide, square root, reciprocal,
sine/cosine, acos-based vector headings, angle normalization, speed/heading decisions and velocity/position
arithmetic. Integer significands, truncation, explicit sign extension and exponent
wrapping reproduce the game-specific arithmetic; host nearest-rounded float
operations are not equivalent. This does not replace ordinary shared vector math.

Move's `unit_turn_toward()` consumes the scalar heading update instead of
rotating a host sine/cosine vector. `SetUnitTurnSpeed` and `SetUnitPropWindow`
normalize their radians through the retail arithmetic. Turn speed retains the
retail minimum (`3a83126f`, approximately 0.001). `unitinfo.move_flags` records
explicit overrides, including a valid zero movement window.

With an authored or scripted window, translation stops when the **pre-turn** heading error
is greater than or equal to the window. Turning continues. The same decision
guards the close-goal Move snap. `movement.turn_blocked` records this tick's
decision and resets with order progress. Both values survive save/load. Save
format 55 rejects earlier layouts because scalar fields shift edict offsets.

Stock units now use authored `UnitData.propWin` degrees, converted and normalized
through the verified retail scalar path. Current native getters expose the effective
turn rate and radians window; default getters expose the immutable authored values,
including degrees for `GetUnitDefaultPropWindow`. Zero authored values retain their
meaning; they are not replaced by unrestricted movement.

## Evidence

Binary identity and report-root conventions are in the [retail ledger](retail-pathfinding.md#binary-and-evidence-conventions).
Reports below are under `/GitHub/wc3-analysis/reports/pathfinding-1.27/`:

| Report | Verified scope |
| --- | --- |
| `numeric-engine-exact.json` | 65,316 original add/subtract/multiply calls match production C output words exactly; 21,772 per operation |
| `motion-engine-exact.json` | 13,824 complete `170880` calls and nine `062930` normalizer calls match C exactly; the surrounding existing motion corpus also runs |
| `runtime/motion-integration-open-exact.json` | All 182 captured open-ground decisions match C |
| `runtime/motion-integration-turn-exact.json` | All 191 captured turning decisions match C, including nine stopped updates |
| `runtime/motion-integration-turn-repeat-exact.json` | A second run has the identical normalized 191-decision input/output sequence |

Turning input: copied Human02Interlude, Footman at `(-1936,-976)`, facing east,
`SetUnitTurnSpeed(0.125)`, `SetUnitPropWindow(0.5)`, move north toward
`(-1936,-144)`. All 300 JASS samples and completion are required. The read-only
observer records raw words at `6f170880` entry/return, four thiscall stack
arguments and mover `+b4/+b8/+bc`. Ghidra assembly confirms the ABI.
Captures, map hashes, provenance and frozen observer/controller sources use
the `motion-integration-*` prefix.

The ordinary arithmetic oracle also reran its existing 200,330 helper calls
and composed checks. Only the three implemented operations are compared to C
there. A helper match does not establish a whole OpenRealm trajectory match.

## Reproduction

Run from the repository root. The probe compiles the **production header**;
it contains no second implementation. Reports record its library hash.

```sh
cc -std=c11 -Wall -Wextra -Werror -O2 -fPIC -shared -I. \
  tools/ghidra/wc3_pathing_engine_probe.c -o /tmp/wc3-pathing-engine.so
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_numeric.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --engine-library /tmp/wc3-pathing-engine.so --report /tmp/numeric-engine-exact.json
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_motion.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --engine-library /tmp/wc3-pathing-engine.so --report /tmp/motion-engine-exact.json
python3 tools/frida/make_wc3_pathfinding_map.py \
  --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --scenario turn --output /run/media/lofcz/ssd_external/Games/w3/Maps/PathingRE-TurnNew.w3m
/home/lofcz/.local/share/uv/tools/frida-tools/bin/python tools/frida/trace_wc3_pathfinding.py \
  --data /run/media/lofcz/ssd_external/Games/w3 --map 'Maps\PathingRE-TurnNew.w3m' \
  --motion-events --task-events --samples 500 --seconds 130 --continue-at 80 \
  --x11-display :94 --output /tmp/motion-turn.jsonl
python3 tools/frida/verify_wc3_motion_trace.py /tmp/motion-turn.jsonl \
  --engine-library /tmp/wc3-pathing-engine.so --scenario turn --report /tmp/motion-turn-exact.json
```

Start an isolated Wine/Frida server using the [RE setup](audio-retail-analysis.md),
with the pathfinding prefix, display `:94` and port 27046. Use fresh map/output
paths. Repeat the capture and add `--compare /tmp/motion-turn-repeat.jsonl` to
require the same normalized decision sequence. The checker rejects absent,
truncated, malformed or mismatched decisions and observer/controller errors.

Asset-free checks: `make test-pathfinding-tools`,
`make test-wc3-engine WC3_PATTERN='wc3_movement.scripted*'`, then `make test`.
The Python suite compiles C at O0/O2, compares seeded raw words to independent
integer models, freezes a live decision, and rejects deliberate trace mutations.
The normalizer retains retail quirks: positive float32 tau remains tau, negative
tau becomes zero, and positive zero becomes negative zero.
`t_movement.c` drives native setters, pre-turn stopping, equality, zero windows,
subsequent travel, close-goal arrival and save/load. Turn and arrival regressions
failed before the corresponding fixes. On this host `make test` needs native
SDL2 as documented in [test performance](../../test-performance.md#linux-sdl-compatibility-layer-text-event-crash).

## Generated tables and exact trigonometry

`generate_wc3_math_tables.py` independently generates the static tables into
`wc3_math_tables.h`, using 90-digit Decimal sine series and integer division.
The 1,025 quarter-wave entries are `floor(sin(i*pi/2048)*2^31)`, saturating the
positive endpoint to `7fffffff`. The reciprocal entries are
`min(2^23, floor(2^47/(2^23+i*2^13-1))-2^23)`. The subtraction of one from
the denominator is necessary: sampling the exact nominal significand misses 845
entries. Both generated tables compare byte-for-byte with the embedded retail
constants; the original build-time generator source is unavailable.

Sine/cosine first multiply by stored scale `4822f983`, truncate to a wrapping
integer phase, select a quadrant and interpolate the quarter-wave table with
its low 8-bit residue repeated four times. Converting that fixed-point value
back to a scalar truncates; hardware float conversion rounds. There is no host
libm call in these simulation helpers. Table addresses/hashes and exact C
comparisons are in `scalar-trig-engine-exact.json`: add/subtract/multiply 21,772
each, sine/cosine 21,668 each, square root/reciprocal 25,542 each and divide 21,732.
Raw exceptional inputs establish helper behavior, not public producer validity.

## Velocity and position integration

`wc3_velocity_update()` computes desired-minus-old velocity and adds that delta
back with retail truncation. It tests squared magnitude against `3456bf95`
(2e-7), then clamps with retail sqrt/reciprocal and scalar multiply. Cancellation
cannot be simplified to assigning the desired vector. `wc3_elapsed()` clears
fractional differences strictly below `38d1b717`, then adds the signed epoch
span; it does not clamp negative elapsed time. `wc3_integrate()` applies the
previous committed velocity before any requested velocity change.

`velocity-integration-engine-exact.json` compares 80 complete original velocity
commits and 2,384 position integrations exactly against C, alongside real
occupancy mutations and the existing motion corpus. Frida's optional
`--velocity-events` records the mover's prior time/epoch/position/velocity,
selected original clock and complete commit output. The fresh turning capture
`runtime/velocity-turn-raw.jsonl` contains 191 scalar decisions and 192 velocity/
position commits, including the final stop; all output words compare exactly in
`runtime/velocity-turn-exact.json`. The normalized translation hash is
`b1f3acf293d750142136d18534a9492330fcacd2fe7fc2cfa7d26f5aac6616f8`.
A second complete capture has the same decision and translation hashes in
`runtime/velocity-turn-repeat-exact.json`. Frozen observer/controller sources and
binary/map/capture hashes are in `runtime/velocity-integration-tools` and the
corresponding `*-provenance.json` files.
That historical report excluded facing; the committed-facing extension below closes that numerical slice.

The captured clock advances by approximately 0.03, with soft-add truncation
visible in adjacent deltas. The historical offline fixtures use a controlled
1/32 input; that value must not be presented as recovered live cadence.
`tools/ghidra/fixtures/retail-turn-velocity-1.27.json` freezes all 192 raw commits
without process addresses. Asset-free tests replay the entire fixture through
production C at O0/O2. The capture checker rejects missing/truncated commits,
invalid raw words and mismatched velocity, position or clock outputs.

Move now retains committed XY velocity and uses this arithmetic when computing
steps along the selected facing or avoidance heading. It still integrates at the existing engine frame interval
and uses world coordinates; an explicit TODO marks the remaining retail grid/
clock handoff. An oblique-step regression fails on the former host arithmetic
and passes with exact coordinate/velocity words. Stop, no-route, failed-step and
order-reset paths clear the velocity. Save format 55 includes the new state and
rejects format 54; a 12-step alternating-heading test matches uninterrupted versus
save/load-resumed position and velocity words exactly.

## Stock propagation window

Static producer evidence is in `stock-window-producer.json`: `6f6b0870` installs
`turnRate`/`propWin` descriptors; `6f66bf40` caches the authored values at `+1e0`
and `+1e4`; `6f69a690`/`6f691aa0` retrieve those words. `6f6785d0` multiplies
propagation degrees by stored radians-per-degree word `3c8efa35`, then unit
construction passes setup `+40/+44` through the same normalized setters used by
JASS. A stock Footman's 60 degrees become mover word `3f860a91`.
Native binding identifies `GetUnitDefaultPropWindow` as `6f203b20`; that wrapper
returns the cached authored word without conversion.

Copied-map `stock_turn` turns an unmodified Footman from east toward north.
`runtime/stock-turn-exact.json` requires all 300 samples and compares all 183
scalar decisions plus 184 velocity/position commits exactly. One decision stops
translation while facing changes by 0.6. JASS reports current window 1.047 radians
and default window 60.000 degrees. After explicit setters, the current fields
change to 0.125/0.500 while defaults remain 0.600/60.000.
`runtime/stock-turn-unattached/comparison.json` confirms all 306 marker strings
match a run with spawn/resume control and no attach or injected observer.
That control checks three-decimal JASS positions, not every internal raw word.

Engine regressions reproduced unrestricted initial movement, premature close-goal
arrival and wrong native getters before the change. The stock gate also exposed a
worker queue restart: selecting a legal passing direction cleared the queue before
the turn could complete, repeatedly restarting the wait. The existing chopper
regression still failed after 96 thinks. The counter now remains exhausted until
a step commits or the direct corridor clears; no passing direction is cached. Fixtures carry the
actual Peasant/Footman/Knight/Wisp turn and propagation data instead of zero rows.

## Exact vector headings

Move now selects its desired heading with the original software length/divide/acos
chain, and computes the shortest error with retail's current-minus-target operand
order. Host `atan2f` gave different heading words even for direction `(4,0.125)`;
the engine regression fails before this change and expects retail word `3d00f7e3`.
The vector-length and post-acos tiny guards are inclusive at `3727c5ac`; the final
error deadzone is strict at `3456bf95`. Opposite-heading signs retain retail's
represented-pi behavior.

`generate_wc3_math_tables.py` reconstructs the 1,020 consumed ordinary acos entries
with 90-digit Decimal arithmetic, plus all 138 near-one entries. The ordinary
branch ends at magnitude `3f7e8000`; its unreachable trailer is not copied into
the engine. Near-one sample indices 120–135 retain repeated represented-input
plateaus, followed by two zero endpoints. The generator is a mathematical
reconstruction; the historical table generator remains unavailable.
`acos-engine-exact.json` verifies all consumed table words and 20,810 original
acos outputs, including adjacent branch values, both signs and in-place calls.
Aliasing output with input changes the original negative-input sign behavior;
the C value-return API has no such aliasing contract.

`heading-chain-engine-exact.json` compares 1,287 original `16f630` results exactly,
including non-binary directions, all quadrants, near-cardinal directions and
adjacent tiny-vector/deadzone values. The frozen 441-case heading/error fixture
runs through production C at O0/O2 without retail assets. Frida's read-only
`--heading-events` captures raw vectors, prior headings and resulting errors.
`heading-stock-turn-exact.json` checks all 183 live errors, plus 183 motion
decisions and 184 velocity/position commits. Its raw trace is
`runtime/heading-stock-turn-raw.jsonl`. A second complete capture matches its
normalized heading, decision and velocity/position sequences exactly in
`heading-stock-turn-repeat-exact.json`; heading hash
`b391f4d1125b2965d6d83b7f93ea98d414d9d3eefa468e78f95a40acc9fc09d8`.
Frozen sources and capture/map/binary hashes are in `runtime/heading-tools` and
the corresponding `*-provenance.json` files. This verifies the observed numerical
chain; full route selection and whole-engine motion cadence remain open.

## Committed facing and remainder arithmetic

NUM-02.4 explicitly separates the facing exclusion from the already closed
velocity slice. Ghidra original`16fe20 →15f7e0 →160060` shows that positive
requested speed commits the heading reconstructed from the resulting velocity,
including cancellation and clamping. `160060` preserves prior facing when
`abs(soft(vx²+vy²)-0) <34d6bf95` (4e-7). Equality recomputes it. This threshold
is twice the earlier velocity-zero threshold`3456bf95` (2e-7); positive tiny
velocity can therefore survive while facing remains unchanged.

The regression uses world speed100, angle`.125` and zero old velocity. Exact
velocity words are`42c67084/41477a18`; original`160060` commits facing
`3dfffadc`, while the previous engine left requested`3e000000`. The assertion
failed before changing Move and passes afterward. A second failing regression
exposed the coordinate scale: the facing guard uses fine-grid velocity, with32
world units per fine cell. Move now converts accepted world velocity through
retail scalar multiplication before that guard. World speed`.016` retains the
prior facing; testing the squared world magnitude would incorrectly recompute it.
Only accepted candidates commit velocity/facing/position; rejected steps retain
the existing steering/collision policy.

Nonpositive requested speed normalizes the requested heading using the
`15ffd0/16fe20` remainder path. This differs from the `062930` turn/window
parameter normalizer. Original`070d20` subtracts the truncated integer word,
returning +0 for integral magnitudes at least2²³. Original`070fe0` multiplies
by the absolute divisor's reciprocal, takes that fraction, multiplies back, then
performs its sign-dependent correction. Preserve its negative correction; it
adds the **negative** absolute divisor at the original boundary. `wc3_fraction`,
`wc3_modulo` and `wc3_facing_angle` implement the verified value contracts.
Positive tau produces raw`35490fdb`, a small remainder, rather than zero or tau.
Signed zeros remain signed in the facing-angle path.

Evidence **S/O/C/L**, target/CRT hashes unchanged:

| Report under the report root | Exact evidence |
| --- | --- |
| `fraction-modulo-engine-exact.json` | 20,422 fractional and21,750 remainder calls, including aliases; independent integer models and C |
| `facing-chain-engine-exact.json` | 810 velocity-heading calls,44 facing-angle boundaries,140 complete velocity/facing commits and2,444 integrations |
| `facing-stock-turn-exact.json` | Earlier complete repeat captures: all184 velocity/position/facing commits,183 scalar decisions and183 vector-heading errors |
| `facing-stock-turn-fresh-exact.json` | Fresh complete Frida capture with the same exact counts and normalized hashes; compared with the earlier complete capture |

The frozen numeric fixture
[`retail-committed-facing-1.27.json`](../../../tools/ghidra/fixtures/retail-committed-facing-1.27.json)
contains810 input/result rows plus44 wrap cases. It includes adjacent input words
around the tiny-velocity guard, both signs and preserved prior headings. The
explicit vector`3a176b4c/39870e5f` produces squared word`34d6bf95` exactly,
and recomputes facing at equality. CI replays these and all192 frozen live
turn velocity/facing commits at O0/O2, compares seeded raw fractional/remainder
inputs with independent models and rejects a changed facing word in a capture.
The larger-speed original matrix uses exact C velocity words; host trig's
former1e-5 tolerance does not describe its recovered numerical contract.

Reproduce with the common compile/numeric/motion commands above; the motion
oracle's `--facing-fixture /tmp/facing.json` exports original numeric outputs.
For the stock live fixture:

```sh
/home/lofcz/.local/share/uv/tools/frida-tools/bin/python tools/frida/trace_wc3_pathfinding.py \
  --data /run/media/lofcz/ssd_external/Games/w3 --map 'Maps\PathingRE-StockTurn.w3m' \
  --seconds 130 --samples 300 --motion-events --velocity-events --heading-events \
  --x11-display :94 --continue-at 80 --output /tmp/facing-stock-turn.jsonl
python3 tools/frida/verify_wc3_motion_trace.py /tmp/facing-stock-turn.jsonl \
  --engine-library /tmp/wc3-pathing-engine.so --scenario stock_turn \
  --report /tmp/facing-stock-turn-exact.json
```

The fresh raw capture is`runtime/facing-stock-turn-raw.jsonl`, SHA256
`d21d4072db02f0b290bc19db72868bb8a866d5b861bbfe7d46e0e17451d3215a`.
Frozen observer/controller/checker/kernel/oracle sources and hashes are in
`runtime/facing-tools` and `facing-stock-turn-provenance.json`. The normalized
velocity hash remains`7a1d7783e9c680144b4ea61592d9604673b293e1871b6a992cbf709b65d6aa70`;
it already includes facing words, which the comparator now checks explicitly.
Raw exceptional arithmetic input tests do not prove public producer reachability.
Move still updates requested facing during its existing steering phase; these
accepted-step fixes do not claim a complete retail motion/clock state machine.

## Velocity guards in fine-grid units

The accepted-step adapter now converts old velocity, requested speed and maximum
speed from world units to32-unit fine cells before `wc3_velocity_update`, then
converts the resulting velocity back. The scalar cutoff belongs to the original
mover's coordinate system: squared fine velocity below`3456bf95` (`2e-7`) is
cleared by `1606e0`; facing uses the separate`34d6bf95` (`4e-7`) guard. Applying
the first guard directly to world velocity admitted motion retail clears.

A test-first Move regression at custom world speed`.01`, facing`.125`, position
320,320 failed four velocity/position assertions before this fix. It now keeps
zero velocity, the original position and facing. The existing`.016` case still
moves while retaining facing, proving the two thresholds remain distinct.
These tiny controlled speed values test the mover/adapter domain; they do not
claim public `SetUnitMoveSpeed` bypasses retail's native speed limits.

```sh
cc -std=c11 -Wall -Wextra -Werror -O2 -fPIC -shared -I . \
  tools/ghidra/wc3_pathing_engine_probe.c -o /tmp/wc3-world-velocity-engine.so
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_motion.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --engine-library /tmp/wc3-world-velocity-engine.so \
  --world-velocity-fixture /tmp/retail-world-velocity-1.27.json \
  --report /tmp/world-velocity-engine-exact.json
python3 tests/test_wc3_pathfinding_math.py
```

`world-velocity-engine-exact.json` checks **1,040** complete original `16fe20`
velocity/facing commits and their world adapter words, plus **3,344** original
position integrations. The matrix includes17 adjacent cutoff-speed inputs,
zero/nonzero old velocity, five headings, four maxima, and low/ordinary/high
speeds. Requested speed remains recorded at mover`+c0` even when velocity is
cleared; the fine occupancy moving bit is checked against actual resulting
velocity. Ghidra `1606e0` confirms this branch and its optional callback.

`tools/ghidra/fixtures/retail-world-velocity-1.27.json` freezes the original
world-adapted input/output words without retail assets. Both O0 and O2 probes
replay all1,040 cases and all192 recorded live velocity/facing commits through
the same world adapter used by Move. No saved struct or frame cadence changes.
This extends the integrated NUM-02.2/NUM-02.4 kernels; world-position clock
ownership, route choices, collision and complete cross-feature trajectories
remain open.

## Remaining fidelity work

Flow fields, route scheduling, group ownership, collision, repulsion and arrival
range policy are not replaced. Move's vector-heading and accepted velocity-facing arithmetic now match the verified retail kernels;
its route and avoidance selection still use existing steering. Stored-velocity integration and elapsed-clock arithmetic have exact C/retail
evidence. Their complete engine clock/position lifecycle and cross-feature trajectories
remain open; accepted velocity guards now execute at the verified fine-grid scale. Repeat equality
covers the observed helper sequence, not an unattached-observer control or all
deterministic state.

Next numerical integration: connect committed velocity/clocks to the [admission-to-owner baseline](retail-pathfinding-todo.md#work-next).
Extend the same C probe and exact-word comparator before changing those stages.
The full replacement still needs the [READY gates](retail-pathfinding-todo.md#ready--start-the-faithful-replacement).

## Active Move cohort speed

The original [callback completion journeys](retail-pathfinding-movement.md#callback-completion-surviving-cohort-and-empty-teardown)
re-resolve group ownership before selecting the minimum eligible member
maximum. OpenRealm previously kept the speed captured at selection submission:
a fast300 member stayed capped at100 after the slow member stopped, arrived,
received a replacement Move, died or was removed. A production order regression
reproduced all six failures, including runtime100→200 speed override,
before changing `s_move.c` (`/tmp/wc3-group-survivor-before-fix-valid-order.log`).

Move now assigns an explicit nonzero cohort identity to accepted simultaneous
selection orders. Speed queries compute the minimum effective speed of the
currently live, alive, actively walking members with that identity. Replacement
Move clears the previous identity through `move_reset_progress`; standing,
terminal hold, death and edict removal exclude the member from the next query.
The existing reserved destination is retained, matching the original fixed-goal
mutation witnesses. Single-unit orders retain their own speed.

Cohort identity is separate from `goalentity->secondarygoal`, which belongs to
route caching and uses a cyclic waypoint ring. Allocation skips zero and every
identity still stored by a live entity, including on uint32 wrap. Reusing a
freed unit edict initializes ungrouped state; an unrelated Move toward the same
point cannot inherit the old cohort. Tests cover independent groups at the same
endpoint, wrap, actual edict reuse and active-group save/load with post-load
Stop. Save format56 persists both per-unit identity and the level allocation
cursor and rejects55/older records.

This is a bounded group-lifecycle correction. Cohort speed queries currently
scan live edicts; the upcoming Move-owned retail member storage should replace
that scan together with the verified member flags, shared parameter owner and
separate decision/commit passes (GROUP-04.6). Queued selections still use the
existing per-unit queued speed value; cohort activation/handoff remains part
of that task. The engine still uses its existing simulation cadence and route
solver, so this change does not claim exact retail trajectories.

## Current point-order ownership

ORDER-01.4 fixes the ordinary point-Move current query. `GetUnitCurrentOrder`
previously delegated to historical `G_GetIssuedOrderId`: queued Smart displaced
the reported Move head before activation; Stop remained reported; natural
arrival retained the historical command. The public-native/server-frame test
reproduced all three failures in
`/tmp/wc3-order-01.4-current-head-before-fix.log` before changing production code.
The retained idle goal cache is not evidence of an active user order and is
left intact.

`edict_t.current_order_id` now represents the active user command separately
from pending FIFO entries, event snapshots and internal locomotion.
Move's `S_IssueMoveOrder` publishes the ID only when its requested goal and
ordinary walk behavior are installed. The public point Move/Smart path and
simultaneous selection path use it; pending insertion does not alter it.
Queued activation uses the same owner. Generic `unit_stand` retires the old ID
before starting a queued successor, and death retires it with the order queue.
Internal spell/interaction approaches still call `order_move` without inventing
a new public Move ID. The native reads this current field, never event history.

The regression uses public JASS orders and `globals.RunFrame`, verifies queued
Smart handoff, replacement/rejection, Stop, new-goal arrival and actual edict
reuse, and saves an active Move with pending Smart. It stops the actor before
restore, then verifies restored current Move and normal queued execution. The
selection-group round-trip also checks active command restoration and Stop.
Save57 includes an explicit `F_INT` descriptor and rejects56/older layouts.
The network protocol is unchanged.

The original oracle executes the registered cdecl native2039d0, including
1eef90 handle/type/canonical resolution and061320 head resolution, at all42
frozen singleton/FIFO states. It checks balanced Unit references, callee
registers, stack cleanup and the FS exception chain. Nine controls per scenario
cover null/low/unbound handles, stale Unit generation, retired/bad-tag Unit
wrapper, stale/retired order head and empty head with a nonzero count. All18
return zero. Each invalid data word is restored immediately, and a valid
query checks the retained head again. Both complete scenarios repeat identically:
124 actual native calls, with unchanged frozen movement/reclamation output.
Supplied existing VM handle-array backing is explicit; these controls do not
prove how invalid states arise in the full gameplay producer graph.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_order_tasks.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --producer-baseline --current-order-query --report /tmp/current-order-new.json
LD_LIBRARY_PATH=/tmp/wc3-sdl2-build make -j8 test-wc3-engine WC3_PATTERN='wc3_api.current_order_point*'
```

The strict corpus entry is `owner-current-order-query`.
This is bounded **O/C plus engine regression** evidence. It does not establish
full retail engine clock/route parity. The remaining command owners are
explicitly [ORDER-01.6](retail-pathfinding-todo.md#order-01--arrival-and-failure):
target Follow/Smart, Hold/Patrol/Attack, economy commands and ability/channel/
metadata orders do not yet maintain this field consistently. Their queries
can return zero or retain the previous point-Move ID; there is no guessed
historical-event fallback. That domain matrix must be traced, split and
integrated through its owning abilities before claiming whole-query parity.

Validation: `/tmp/wc3-order-01.4-current-head-validated-full-suite.log`
passed78 Python tool tests and36775/36775 engine assertions across2133 tests
in each WC3 schema. The first full run correctly rejected the stale53-oracle
inventory count; the declared new variant raises it to54. The fresh strict
corpus is121/121 at
`/GitHub/wc3-analysis/reports/pathfinding-1.27/order-01.4-current-head-corpus/corpus-results.json`
(manifest SHA256 `eaf61dba2a79609d699bd83970d07f9bde1f64ca1b6e83190c0cbe8ff9f07c8a`;
summary SHA256 `12d377b5b3e2a31b5bff4b2a815d641c520a4fb9625899e2ce5faa534da1b753`).
All recorded sources matched at completion. The current-query repeat digests
are `b9bdf5ae025230a63f64072fa79ad3460ea29b7d1f433983b681e517d8f414bd`
(singleton) and `e720414033bb01b16c4bd293c40443d0183784a3704b5d5e1945f28864c2b0f1`
(FIFO). Ghidra saved `Unit_CurrentOrderCommandLoad` at203a23 and EOL comments
at203a15/203a23 with the head/count distinction and complete native witness.
