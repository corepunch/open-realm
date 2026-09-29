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
Facing reconstructed from velocity remains outside this comparison.

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

## Remaining fidelity work

Flow fields, route scheduling, group ownership, collision, repulsion and arrival
range policy are not replaced. Move's vector-heading arithmetic now matches retail;
its route and avoidance selection still use existing steering. Stored-velocity integration and elapsed-clock arithmetic have exact C/retail
evidence. Their complete engine lifecycle, world/fine coordinate conversion
and cross-feature trajectories remain open. Repeat equality
covers the observed helper sequence, not an unattached-observer control or all
deterministic state.

Next numerical integration: verify facing reconstructed from committed velocity, then connect
committed velocity/clocks to the [admission-to-owner baseline](retail-pathfinding-todo.md#work-next).
Extend the same C probe and exact-word comparator before changing those stages.
The full replacement still needs the [READY gates](retail-pathfinding-todo.md#ready--start-the-faithful-replacement).
