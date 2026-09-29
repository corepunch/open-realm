# Numerical pathfinding integration

## Implemented slice

`games/warcraft-3/common/wc3_math.h` implements the retail 1.27.1.7085
software scalar add, subtract, multiply, angle normalization, and speed/heading
decision. Integer significands, truncation, explicit sign extension and exponent
wrapping reproduce the game-specific arithmetic; host nearest-rounded float
operations are not equivalent. This does not replace ordinary shared vector math.

Move's `unit_turn_toward()` consumes the scalar heading update instead of
rotating a host sine/cosine vector. `SetUnitTurnSpeed` and `SetUnitPropWindow`
normalize their radians through the retail arithmetic. Turn speed retains the
retail minimum (`3a83126f`, approximately 0.001). `unitinfo.move_flags` records
explicit overrides, including a valid zero movement window.

With a scripted window, translation stops when the **pre-turn** heading error
is greater than or equal to the window. Turning continues. The same decision
guards the close-goal Move snap. `movement.turn_blocked` records this tick's
decision and resets with order progress. Both values survive save/load. Save
format 54 rejects earlier layouts because scalar fields shift edict offsets.

The stock authored `UnitData.propWin` producer/conversion remains to be
recovered. The code has an explicit TODO and preserves unrestricted stock
translation while integrating the verified scripted policy. Existing authored
turn-rate behavior remains the default without a scripted override.

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

## Remaining fidelity work

Flow fields, route scheduling, group ownership, collision, repulsion and arrival
range policy are not replaced. Heading **selection** still uses host `atan2` and
existing steering; retail uses software length/division/acos. Velocity
trigonometry, stored-velocity integration, clocks, world/fine coordinate
conversion and complete cross-feature trajectories remain open. Repeat equality
covers the observed helper sequence, not an unattached-observer control or all
deterministic state.

Next numerical integration: resolve the stock window producer, then connect
committed velocity/clocks to the [admission-to-owner baseline](retail-pathfinding-todo.md#work-next).
Extend the same C probe and exact-word comparator before changing those stages.
The full replacement still needs the [READY gates](retail-pathfinding-todo.md#ready--start-the-faithful-replacement).
