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

## Public scalar inputs and decimal destinations

NUM-01.5/06 bridge the verified movement arithmetic into actual public JASS
inputs. OpenRealm previously implemented `S2R` with `atoi`, losing fractional
coordinates, and used host casts/libm for `I2R`, `R2I`, `Sin`, `Cos`, `Acos`
and `SquareRoot`. `api_misc.h` now uses the verified software scalars, retaining
each public wrapper's own domain guard. These changes affect WC3's native API;
SC2's separate Galaxy math adapters are outside this change.

The original registration at207490 installs these actual functions:

| Native | RVA | Input / return |
| --- | --- | --- |
| S2R | 211080 | encoded string handle / raw scalar word |
| I2R | 204c80 | signed integer value / raw scalar word |
| R2I | 2103a0 | scalar pointer / signed integer |
| Sin / Cos | 215d00 / 1f9580 | scalar pointer / raw scalar word |
| Acos | 1f75d0 | scalar pointer / raw scalar word |
| SquareRoot | 215d30 | scalar pointer / raw scalar word |

All seven registered wrappers are cdecl with stack4 input and EAX result.
`S2R` resolves its string through06e8d0, returns zero for absent/empty text,
otherwise calls070de0 (ECX output, EDX text, EAX output pointer, plain RET).
The parser accepts an optional leading sign, one decimal point and nine
significant digits, stopping at any other byte. It skips no leading whitespace
and recognizes no exponent notation. Leading zeroes do not count as significant
digits. Scaling uses071180's scalar multiplication/squaring of10 and original
070d80's truncating integer conversion, followed by scalar multiply/divide.
A host `strtof` would still miss the actual contract: `S2R("1936.25")` is raw
`44f20801` (1936.2501220703125), while the exactly representable host value is
`44f20800`. The negative coordinate is `c4f20801`; `"-144.125"` is `c3102000`.

`R2I` calls070170, saturating exponents >=158 to INTMAX/INTMIN according to
sign. It differs from raw wrapping070120. `I2R` truncates the integer
significand; INTMAX becomes2147483520. `Cos(0)` is `3f7fffff`.
`Acos` returns zero for values strictly outside[-1,1]; unordered COMISS does
not take the rejection branch, a distinction retained by the engine adapter.
`SquareRoot` returns zero when `abs(ScalarSubtract(input,0))` is strictly below
static `3a83126f` (~0.001), or input is negative. Equality executes071480.
The threshold belongs to the public wrapper, not the raw root helper used
by movement. Nonfinite raw-wrapper controls do not establish producer reachability.

`num-01.6-public-wrapper-engine-exact.json` records2,024 original parser calls
matching independent integer formulas and production C, plus12,210 executions
of the six registered numeric wrappers with raw synthetic inputs, preserved
input/guard words, nonvolatile registers and balanced cdecl stack. The isolated
parser supplies only ASCII `isdigit`; all scalar/parser instructions execute
unchanged. The separate live witness uses retail's installed sibling CRT.
Parser digest is `45fb304aa5ffa02919dae455d2e5ede75299567f71be7b9acb058374f7713556`;
wrapper digest is `1af7c716e12804fbb195cf546923f6411cb1be6dc73bae87adbf59dea713440c`.

`numeric_inputs` map scenario23 brackets calls with `PATHNUM` markers before
native argument evaluation. Both complete `runtime/num-01.5-inputs2-{raw,repeat}.jsonl`
captures contain67 native returns and25 nonempty original parser witnesses.
Inputs for real-valued natives come through explicit `S2R` text; integer inputs
remain actual integer values. The normalized raw sequence repeats exactly with
digest `cbeb498cdb8e4f0de7806f4be9cb44a321ea44d4b5aa1bad3d2a974429f62d2e`.
A real public Move task retains both parsed destination words above. Capture
metadata embeds hashes of observer, controller, probe, builder, case data and
the actual generated map; the fixture retains those historical hashes.
`verify_wc3_numeric_inputs.py` rejects missing/reordered markers, mismatched
input/output/helper text, changed provenance, counts and destination words.
The corpus pins both captures independently.

Public engine tests reproduced21 failures across41 numeric cases, then pass
all41 frozen words. The separate decimal/Move regression failed before the
parser change and now passes five checks, including issued coordinates through
`IssuePointOrder`. Asset-free C checks cover optimized/unoptimized parser and
integer conversions against independent models. Ghidra now persists289 names,
18 partial layouts,115 fields and57 explicit prototypes, including the byte
text pointer and public wrapper threshold. Report:
`num-01.6-ghidra-public-numeric-types.json`.

Full validation:83 pathfinding tool tests,37,056 assertions in2,140 engine tests
for each Classic/TFT run, and both WC3/SC2 production builds pass.
`num-01.6-public-corpus/corpus-results.json` passes123/123 with all recorded
source fingerprints unchanged through completion. Manifest SHA256:
`e013410a7e49451311ce5b872b543b6bba13f28f8a4495c142a9620622c07448`;
summary SHA256:
`849aa7407ec18ae6dea6d91790da46cbcd17fe04542b6a44bf136360179362f2`.

Exploratory `runtime/num-01.5-inputs-raw.jsonl` used long compiled decimal
literals and exposed a different producer contract: for example a long literal
written for0.6 reached Sin with raw `bf85635d`. This trace is retained as
an investigation artifact; it is not the accepted decimal-input fixture.
NUM-01.7 explicitly owns the original JASS literal compiler/parser and engine
integration. Remaining angle/power helpers, their constant initialization,
non-ASCII grammar/locale and broader public exceptional domains stay NUM-01.2/03.
Whole trajectory/cadence parity remains NUM-02.3.

## Public angle adapters

NUM-01.8 extends the scalar integration to `Asin`, `Atan`, `Atan2`, `Tan`,
`Deg2Rad` and `Rad2Deg`. These WC3 native adapters now use the same deterministic
scalar helpers as movement. Host Galaxy math remains separate.

| Public native | Registered RVA | Helper / public guard |
| --- | --- | --- |
| Asin | 1f8250 | 0703a0; strict outside[-1,1] returns0 |
| Atan | 1f8310 | 0705b0; reciprocal/range-reduced scalar polynomial |
| Atan2 | 1f8290 | 070530; both absolute scalar distances strictly below3a83126f returns0 |
| Tan | 216750 | 071590; original paired sin/cos followed by scalar division |
| Deg2Rad | 1fcda0 | scalar multiply by static3c8efa35 |
| Rad2Deg | 210480 | scalar multiply by static42652ee1 |

All wrappers are cdecl, returning a raw scalar in EAX. Single-real arguments
are pointers at stack4; Atan2 uses y/x pointers at stack4/8. Decompiler's earlier
fastcall suggestion for Tan was corrected from instructions. All four low-level
angle helpers use ECX output/EDX input, with Atan2's second input at stack4 and
RET4. The other helpers have plain RET and return the output pointer in EAX.

Asin reuses the original inverse curve and its near-one table. Its ordinary
branch negates the clamped fixed-point phase and subtracts3243f6a8 before
truncating integer-to-scalar conversion. Computing half-pi minus the completed
acos scalar would lose that operation order. Near one, positive/negative
inputs use separate half-pi subtraction/addition. The engine shares the
interpolation machinery with acos; the frozen acos/vector/velocity corpora
continue to verify every affected consumer.

Atan takes the absolute input, uses a reciprocal above1, then tests strict
range threshold3e8930a3. Reduction is
`(x + bf13cd3a) / (1 + 3f13cd3a*x)`, using scalar operations. The rational
polynomial is `x*(3f7ffff0 + 3e8415a6*x*x)/(1 + 3f17592e*x*x)` in that exact
operation order. Reduction adds3f060a92; reciprocal mode subtracts the result
from3fc90fdb. Negative-nonzero input bits then invert the sign. Atan2 consumes
the absolute scalar quotient, handles exponent-zero x with half-pi, then uses
pi subtraction and y sign correction. Signed zero does not trigger either
negative quadrant correction. The public wrapper's both-small guard is separate.

Tan consumes the paired helper directly. Its live half-pi example returns
4effffff, and its pi example returnsb0000001; these retain lookup/remainder
quirks rather than host transcendental results. Asin's public bounds and
Atan2's strict tiny-input guard are retained in `api_misc.h`; SC2's own math
adapters are not routed through these WC3 helpers.

`num-01.8-angle-engine-exact.json` records81,309 original/model/C calls across
the four distinct-storage helpers, including adjacent reduction/lookup inputs,
and24,420 original registered-wrapper raw calls across twelve natives. Public
producer reachability is separately established by the live cases; raw
nonfinite inputs and pointer aliases are not promoted to public-domain evidence.
Angle-helper digest:
`caba85090ffb76bb247fd0a43bf23e1661915cf5d77710299689a8923700f205`;
registered-wrapper digest:
`23c401d402844bdfa509d5eff151dcfe37c3cb4e88139b2d9aff0c5d9bc43251`.

Scenario24 `numeric_angles` supplies actual decimal text to S2R before each
angle-native invocation. Two complete `runtime/num-01.8-angles-{raw,repeat}.jsonl`
captures each record48 bracketed raw input/output pairs and ordinary point-Move
admission/completion. All raw pairs repeat identically:
`8dc9da2575dc2703abc75e88f941251beceb2c22537cba781423d13ecd08bee8`.
The fixture includes both Atan2 operand words, public boundary/zero guards,
static factors and source/map hashes. The analyzer treats absent sparse parser
counters as zero when no parser event is expected, and rejects nonzero counts,
operand reversal and other trace corruption.

The engine public regression reproduced25 failures across48 frozen cases;
all48 pass after integration. Together with the earlier decimal/scalar cases,
all94 public checks pass in both Classic and TFT. Independent integer models
also compare optimized/unoptimized C. Ghidra persists299 names,18 partial
layouts,115 fields and67 explicit operand-storage prototypes; twelve consumed
static constants have named typed labels and no direct write xrefs.
`num-01.8-ghidra-angle-types.json` records the persisted ABI. Historical
constant generation, pointer-alias producers, Pow and compiled-literal parsing
remain explicit NUM-01.2/07 work. Whole cadence/trajectory parity stays NUM-02.3.

Full validation passes85 pathfinding tool tests and37,104 assertions in2,141
engine tests per Classic/TFT run; WC3 and SC2 production builds pass.
`num-01.8-angle-corpus/corpus-results.json` passes124/124 with all recorded
source fingerprints unchanged through completion. Manifest SHA256:
`8ce2ad3f3889607dabcc45d87d582eb32119d5c6c7475bb45c0422663525867f`;
summary SHA256:
`90450e97d996040caaf9c336d15d72f57bd6ea322d91b8e7501700676021895c`.

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


NUM-01.4 adds the complete paired helper071340. It uses ECX angle pointer,
EDX sine output, stack4 cosine output and RET4; sine is stored before cosine.
`num-01.4-paired-all-alias-numeric-exact.json` executes20,032 raw angles and
80,128 alias cases against an independent generated-table model and production
C, with adjacent quarter-turn inputs, signed zeros, exceptional raw words,
input/output aliases, shared outputs, guard words, nonvolatile registers and
stack cleanup. The distinct-angle output digest is unchanged from the earlier
inventory experiment:
`01131f854754268404722dcdae905cde29dcbb40228f0c30c877a346dd21c44e`.

Production `wc3_sincos` computes the phase once and reuses the same interpolation
as the singles. `wc3_velocity_update` consumes that pair. The test-first probe
fails before the paired API exists, then verifies both-O0 and-O2 words against
the independent model. This shares calculation without changing the requested
velocity formula or making a new clock/trajectory claim. The original paired
helper has verified formation/random-direction callers; its use here does not
claim that the original velocity kernel itself calls071340. The strict corpus
requires the paired case/alias counts and frozen digest in both numeric entries,
then replays the existing velocity/facing/position witnesses unchanged.

Ghidra now persists278 recovered names,18 partial layouts,115 fields and46
explicit x86 prototypes, including `Math_SinCosPaired` and scalar phase constant
`Math_TrigPhaseScale` atcd58d8. Remaining conversion/angle-helper comments retain
partial ABI/semantics and direct caller counts, with unverified public domains
explicitly excluded. Full remaining arithmetic inventory stays NUM-01.2; public
exceptional inputs remain NUM-03, and whole-engine timing remains NUM-02.3.

Validation for01.4: full `make test` passes81 tool tests and37,010 assertions
in2,138 engine tests; WC3/SC2 production builds pass. Fresh strict corpus
`num-01.4-paired-corpus/corpus-results.json` passes122/122 and verifies unchanged
source fingerprints at completion. The velocity/facing/position expectations
remain frozen. Manifest SHA256:
`6061e98eac2f62b11d32aefad73e5650290632ce51ee884e8602adf4863b5ef5`;
summary SHA256:
`1c4f5ed3eea55779bd0942f1f9d533af29668130e3fad5878490596733d50912`.

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
indexed by [ORDER-01.6](retail-pathfinding-todo.md#order-01--arrival-and-failure),
with completed Follow/Hold slices and explicit remaining leaves below:
Patrol/Attack, economy commands and ability/channel/metadata owners do not yet
maintain this field consistently. Their queries can return zero or retain the
previous point-Move ID; there is no guessed historical-event fallback. Those
remaining leaves must be integrated through their owning abilities before
claiming whole-query parity; wider target lifetime composition remains01.17.

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


## Remaining command-owner inventory

ORDER-01.6 splits the current-query domain by owner. `2039d0` reads the live
user head; `680320` admits/replaces, `67abe0` classifies and dispatches it,
`679cc0` delivers to the order-owned ability, and `69b2f0` offers virtual22c
interception before replacement. These shared roots are **S**, not a complete
reachable producer graph; BASE-03.1 still owns that graph. In particular,
copying every accepted publisher ID into current state would misclassify
metadata, instant actions and internal locomotion.

| Leaf | Engine owner/entry point | Retail starting evidence and remaining contract |
| --- | --- | --- |
| ORDER-01.7 | `s_holdpos.c:S_HoldPosition` | Move d0019 creates Hold tasks at5fccf0; user head retires to0 while behavior persists |
| ORDER-01.8 | `s_move.c:S_IssueFollowOrder`, `order_follow_resume` | Move d0003/d0012 target branches5ff240/5fd270; public Smart/Move retain their IDs at rest |
| ORDER-01.9 | `s_patrol.c:order_patrol`, `order_patrol_resume` | Move d0017 at5fdff0; native point admission, endpoint reversal, combat resume and queue/save ownership |
| ORDER-01.10 | `s_attack.c:S_OrderAttack`, Attack Move/Attack Ground | Shared dispatch and Move d0016 at5fe1a0; distinguish public attack IDs from automatic sub-behaviors |
| ORDER-01.11 | `s_repair.c:S_OrderRepair` | Shared owning-ability dispatch; trace concrete retail repair owner, approach, completion and interruption |
| ORDER-01.12 | `s_harvest_lumber.c`, `s_goldmine.c` and race-specific resource owners | Smart/Harvest dispatch; inventory early-return admissions and resource-return/internal approaches |
| ORDER-01.13 | `s_spell.c` plus each concrete ability | Shared owning-ability dispatch; approach, execute, channel, inverse and instant ownership |
| ORDER-01.14 | Each metadata/toggle owner | Shared interception69b2f0; preserve an existing active head when a metadata action is accepted |
| ORDER-01.15/17 | Generic target lifetime dispatch, Move loss handling | Public healthy Follow loss is synchronous; combat-parent/direct-free/reentrant-generation/save composition remains01.17 |
| ORDER-01.16 | `s_build.c` and race-specific construction owners | Concrete admission, approach, work, interruption and queued build ownership; separate from Harvest |

`m_unit.c` routes public target orders through specialized owners before ordinary
Follow: item pickup, authored Smart, Acolyte/Gold/Lumber, return resources,
attack/destructables, target-owned interactions, cargo and repair. Point spells,
rally metadata, Attack Ground and Attack Move have separate paths. Harvest and
rally have early returns that do not consistently use the same publisher.
The FIFO calls these owners again at activation; it must not make a pending
entry current merely because the issued event was published. Generic
`S_UnitAbilityOrderAccepted` is a post-accept hook, not universal head activation.

Ghidra now persists **278 names,18 partial layouts,115 fields and46 explicit
x86 prototypes**. New names cover Hold creation/enter/leave, Patrol/Attack Move
creation, Smart target dispatch and the shared owner/interception roots.
Move20 bit200 is named in the existing Move prefix; instruction labels at
5ffb0f and5ffe8f distinguish retained behavior from current user identity.
`MapPathfindingTypes.java` permits naming undefined bytes only when every
existing field/type/offset/comment and total size is preserved. Actual mapper
rename/retype/omission controls all reject the change before resolution;
`order-01.6-ghidra-owner-types.json` is the final readback. This refinement does
not install inferred calling conventions or discard another analyst's fields.


## Current Patrol ownership

ORDER-01.9 integrates public point Patrol with command851991. The previous
native whitelist rejected Patrol, while the selected-unit UI called an internal
procedure without recording its current identity or using the player FIFO.
The test-first native/UI witnesses fail against the committed engine. Native
and selected-unit commands now reach `S_IssuePatrolOrder` in `s_patrol.c`; that
owner records the public ID only after Patrol owns the installed move. UI Shift
uses the existing FIFO and advertises queue support. Endpoint reversals retain
the ID, and queued activation records it when the prior command completes.

The public-native regression runs real server frames through repeated endpoint
reversal, rejected replacement, queued point state, save/load, Move replacement,
queued Patrol activation, Stop, death/dead rejection and actual edict reuse. The
UI regression issues the real `button CmdPatrol` command, queues behind Move,
advances server frames and then replaces the endpoints with an immediate click.
Its fixture initializes a JASS VM before entering the server scheduler; the
initial fixture crash was a null VM, not a production Patrol/HUD failure.

The existing repeated public order-lifecycle capture supplies actual no-enemy
reversal: tick150 starts at(-1968,-464), tick172 reaches(-1937.086,-155.213),
and tick179 returns to(-1946.351,-243.171). Current head851991 remains active.
The analyzer now requires approach within64 world units of the authored endpoint,
then return by more than64 units under that same head. Negative controls reject
stationary, outbound-only, truncated and wrong-head traces. The threshold admits
the observed coarse timer samples; it is not a recovered retail stop distance.
`order-01.9-patrol-reversal-audit.json` rechecks both complete captures and the
unchanged320-marker repeat digest. No new observer/map provenance is claimed.

Ghidra persists `WC3PatrolEndpointsPrefix`, a104-byte partial prefix containing
only command24 and scalar coordinates48/50/5c/64, plus the verified ECX Move /
stack4 event / RET4 dispatcher ABI. Labels5fe04a,5fe059 and5fe114 retain the
primary/alternate endpoint reads and d0175 return-task creation. Complete
original construction, queued-origin capture timing, blocked-route endpoint
policy and automatic combat/resume remain ORDER-01.18. This integration closes
public current ownership and no-enemy lifecycle, not whole Patrol trajectory
or numerical/clock parity.

Validation for01.9: the full normal suite passes80 tool tests and37,010 assertions
in2,138 engine tests, after correcting the stale unknown-order Patrol fixture.
All six current-order regressions pass257 checks with DEBUG_JASS, followed by
normal library restoration. WC3/SC2 production builds pass. Fresh strict corpus
`order-01.9-patrol-corpus/corpus-results.json` passes122/122 and rechecks source
fingerprints unchanged at completion. Manifest SHA256:
`e267f21ac2a21ca6d9fe397538d60cd5467fe3580e9231e976a7d2df2c5f5705`;
summary SHA256:
`b2ad131dd8491f5f67ccc866304f8c9d42fae1e55d30ae75f1be4632c5233871`.

## Current Follow and Hold ownership

**S/L plus engine regression:** the `order_lifecycle` probe uses actual public
JASS orders on stock hfoo in a copied Human02Interlude terrain. Both final
owned captures run130 seconds, send the isolated-display loading key at80,
then record all300 timer samples. All**320** timer/health strings repeat exactly,
SHA256 `a386b56ddd95c2154127315e65c3afe8b2869fe754db44458e0d67c83712df19`.
The hashes of the generated map and observer/probe/builder are recorded in
`fixtures/retail-order-lifecycle-1.27.json`. Those source hashes were recorded
separately after capture; they are not embedded in the historical JSONL header.

| Stage | Expected and observed current order |
| --- | --- |
| Point Move at10 |851986 |
| Hold at30 |0 immediately and through standing |
| Accepted Defend/undefend at60/65 |0; these samples do not prove metadata preserves a busy head |
| Smart ally Follow at80, target moved near at110 |851971 even while stationary |
| Target Move at120 |851986 even while stationary |
| RemoveUnit(target) at130 |0 synchronously in retail and the integrated healthy Follow engine path |
| Stop at140 |0 |
| Patrol at150, rejected unsupported replacement at170 |851991 |
| Hold at180 and automatic attack after200 |0; enemy life420.000→409.606 at220 |
| Target Move at230, KillUnit at240, rejected dead order at250 |851986→0→0 |

The final fixture explicitly researches Rhde and changes the copied campaign's
0↔1 PASSIVE alliance to hostile, with user/computer controllers. Earlier
controls had rejected Defend without research, or could not attack because
Human02Interlude config made players0/1 allies. The paused enemy remains
vulnerable; the health drop is required by the analyzer. These controls remain
in the report directory and do not certify the final combat witness.

OpenRealm's prior target Move/Smart path installed Follow without updating its
current head. The test-first public-native witness failed current-query
checks before the fix. `S_IssueFollowOrder` now records the actual command only
when Move owns the installed Follow; internal `order_follow_resume` keeps that
identity across moving/standing and opportunistic combat. Common completion
retires the head before queued activation, and death clears it. The regression
covers queued target activation, standing, resumed pursuit, rejected replacement,
Stop, saved Follow/pending point Move, target-loss handoff, arrival, death and
actual edict reuse. The later ORDER-01.15 integration below adds synchronous
healthy Follow target-loss handling while preserving deferred edict reclamation.


ORDER-01.15 adds a generic `A_TARGET_REMOVED` notification to the active move
procedure after `G_DeferFreeEdict` has marked the target semantically removed.
Move owns the response: only active Follow of that exact target clears its
follow/goal references and reaches `unit_stand`, retiring the user head before
pending activation. The FIFO now rejects semantically removed entity targets,
so a stale pending Follow cannot prevent a later valid point Move from starting.
The target stays allocated until the ordinary frame-end event/deferred drain;
no edict field or save layout changes are required (W3SV57 remains current).

The isolated regression tests Move/Smart × no pending command, pending point
Move, removed-target Follow then point Move, and prior point replacement. It
also checks unrelated and repeated RemoveUnit, immediate public queries,
next-frame reclamation and actual queued arrival. Against the committed engine
it failed32 of132 assertions; the integrated owner dispatch passes all132.
This is healthy active Follow coverage. Temporary combat-parent ownership,
direct free/death, callback reentry, generation reuse and save before drain are
explicitly split into ORDER-01.17. The existing repeated live tick130 current0
witness supplies the immediate public loss observation; it does not by itself
prove the complete RemoveUnit-to-target-loss caller graph.

Ghidra retains `Move_TargetLossUserHeadResolve` at5ff5d9:5ff5f0 resolves
Unit19c/1a0, while5ff618 calls the separate internal-task query69c6a0. The named
5ff490 handler comments record this distinction and the caller-graph exclusion.

Validation for01.15: full `make test` passes79 tool tests and36,965 assertions
in2,136 engine tests; current-order queries pass212 checks in both normal and
DEBUG_JASS builds, followed by normal-build restoration. WC3/SC2 production
builds pass. Fresh corpus `order-01.15-synchronous-corpus/corpus-results.json`
passes122/122 with unchanged source fingerprints at completion; summary SHA256
`11ef7ba0b1e4c2ddee651cc6cdd4b6f3271a2afce58ca254d10e0b63bcc0d46d`. Its manifest remains
`667324574a68516357cbcb401e48bca8cd79643e80a1b4b28ff463d5816c2391`.

Hold needs **no invented persistent851993 head**. The original dispatcher
creates d0148,d014e,d0177,d0144/action0, and d0177 sets Move20 bit200; d0178
clears it. The command completes while this behavior remains. The engine's
public Hold regression verifies head0, cleared pending commands, save/load,
automatic damage through `globals.RunFrame`, return to Hold after enemy removal
and replacement by Move. Its minimal attack fixture supplies UnitWeapons,
SVF_MONSTER/hostile owner and a nonzero authored damage point; omitted fixture
attack timing was corrected without changing unrelated production attack code.

```sh
python3 tools/frida/make_wc3_pathfinding_map.py --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m --scenario order_lifecycle --output /run/media/lofcz/ssd_external/Games/w3/Maps/PathingRE-OrderLifecycleNew.w3m
# Execute the owned trace command twice, each into a new JSONL path:
DISPLAY=:94 WAYLAND_DISPLAY= WINEDEBUG=-all WINEPREFIX=/home/lofcz/.local/share/open-realm/wine-pathfinding-re /home/lofcz/.local/share/uv/tools/frida-tools/bin/python tools/frida/trace_wc3_pathfinding.py --data /run/media/lofcz/ssd_external/Games/w3 --map 'Maps\PathingRE-OrderLifecycleNew.w3m' --seconds 130 --samples 1000 --task-events --x11-display :94 --continue-at 80 --output /new/orders.jsonl
python3 tools/frida/analyze_pathfinding_trace.py /first/orders.jsonl --scenario order_lifecycle --compare /second/orders.jsonl --output /new/orders-audit.json
LD_LIBRARY_PATH=/tmp/wc3-sdl2-build make -j8 test-wc3-engine WC3_PATTERN='wc3_api.current_order*'
```

The actual final artifacts are
`runtime/order-01.6-lifecycle4-{raw,repeat}.jsonl` and
`order-01.6-lifecycle4-repeat-audit.json`; corpus entry
`live-order-lifecycle-repeat` rechecks both complete inputs and their repeat.
Timer position/health text has three decimals. This is not raw float-position,
full engine clock/trajectory parity, complete Unit/VM construction, all target
visibility/lifetime paths or all metadata/spell/interaction-owner coverage.
Current state uses existing save57; issued-event context remains JSVM7 and
network contracts are unchanged.

Validation checkpoint for ORDER-01.6/07/08: normal full umbrella tests pass
(`/tmp/wc3-order-01.6-domain-validated-full-suite.log`,79 tool tests and
36833 engine assertions in2135 tests/schema); DEBUG_JASS and normal current-order
suites pass, and normal JASS is restored. `openwarcraft3` and `opensc2` production
builds pass. Strict `order-01.6-domains-validated-corpus/corpus-results.json`
passes122/122 with all recorded source fingerprints unchanged at completion.
Manifest SHA256 `667324574a68516357cbcb401e48bca8cd79643e80a1b4b28ff463d5816c2391`;
summary SHA256 `8466ca0aa96d2a8b72523d8441c3e7ca9743b187baba52d4fab6820facab49b3`.
The earlier scratch corpus rejected an unsupported dotted report-check key;
the final manifest checks the complete repeat object, preserving every assertion.


## Widget escape idle admission

The accepted widget escape in MAP-03.4 installs a real Move for an idle occupant,
then completes after thirteen original group ticks with its Footman query mask
produced by the original getters/bridge and retained throughout. A separate
seven-tick terrain-only control preserves the historical fixture. Its widget footprint remains registered ([original journey](retail-pathfinding-search.md#widget-produced-escape-through-arrival)).
The engine previously only set `movement.displacement_active` and a walk animation;
its stand thinker never consumed that target. A construction-margin fixture using
`G_DisplaceBuildOccupants`, `G_StartHumanConstruction`, a live JASS VM and normal
`globals.RunFrame` reproduces five failures, including zero travel after120 frames.

Move now starts an ordinary temporary Move destination for an idle stand with no
active user order and no Hold behavior. Existing Move/build walkers retain their
original behavior and destination and continue consuming the displacement target
as before. No issued-event publication, queue clearing, persistent field or wire
change is added. The new server-frame regression verifies escape arrival, current
Move head then head0, retained construction blocking and replacement public Move
canceling displacement and arriving at its replacement point. The existing worker
regression still reaches its later build destination.

This is bounded admission/lifecycle integration, not full widget-path parity.
The later stock-mask fixture closes MAP-03.4; the earlier terrain-only control
remains separate evidence. MAP-03.7 below now covers the engine's solid-footprint
failure and interruption cleanup. Original nearest-edge/jitter proposal generation
and RNG ownership remain MAP-03.3/NUM-04; no spiral geometry is promoted to retail parity.
Validation logs retain both failures in `/tmp/wc3-map-03.4-idle-{baseline,fixed}.log`
and the final margin baseline in `/tmp/wc3-map-03.4-idle-final-baseline.log`.


Validation: final `make test` passes85 pathfinding tool tests and37,126 engine
assertions in2,142 tests per Classic/TFT. The two construction displacement
regressions pass47 checks with `WC3_DEBUG_BUILD=1` after forcing recompilation
of the Move source, and pass again after rebuilding normally. WC3/SC2 production
builds pass. No save or network representation changes.
Fresh strict `map-03.4-widget-final-corpus/corpus-results.json` passes124/124
with unchanged source fingerprints. Manifest SHA256
`72b14dc041e398b499eedbe6dbd9e8db63bc8f5bbbad81844fccdf5c2b07c564`;
summary SHA256
`69fdc4610b6e8bfa4fe3615d6c5ed87079e7ebb3dfc518c2f428ef2af99fcb49`.
At that earlier checkpoint MAP-03.4 was still unchecked; the later stock-mask
checkpoint closes it. The terrain-only journey remains supporting bounded evidence.


MAP-03.4 stock-mask checkpoint: fresh
`map-03.4-stock-final-corpus/corpus-results.json` reproduces **133/133** declared
outcomes with unchanged source fingerprints at completion. Manifest SHA256:
`8a6a9e6aeeab8b7ad79e85fa024ba0a37f5775e8887b6836f8a11d828848efc8`.
Summary SHA256:
`39754dca2b4d64f4b966f407fef683b48d019f5f762fec08ad417917af61850d`.
Full ROC/TFT suites pass37,126 assertions in2,142 engine tests each;85 pathfinding
tool tests and both WC3/SC2 production builds pass. The original-mask extension
adds no engine representation or frame-cadence change. The earlier idle admission
fix remains active; inside-footprint escape integration remains MAP-03.7.


September30 upstream sync (`upstream/main` at`d21a0a1f`) combines its route
progress guard with the verified software-scalar velocity/integration path.
The guard applies to location orders. Applying final-center distance to ranged
interactions reproduced four failed lumber-dropoff assertions: a worker at the
flow endpoint reversed before reaching the blocked Town Hall's interaction
boundary. Existing interaction steering is retained there. The imported turn-lag
regressions now wait through the verified propagation window, assert no drift
while stopped and require the first admitted step to progress. Numerical raw-word
and velocity save/resume tests remain active. Position commits also synchronize
snapshot X/Y and retain worker-blocked-counter clearing from the two parents.
The combined raw layout uses save58 and rejects both parents' earlier formats.

Sync validation: full ROC/TFT suites pass37,248 assertions in2,155 engine tests
per schema,85 pathfinding tool tests and both WC3/SC2 production builds. The
movement subset passes1,530 assertions in163 tests per schema. Save rejection
covers the combined58 layout. Logs are`wc3-upstream-20260930-final-full-suite.log`
and`wc3-upstream-movement-fixed.log` under`/tmp`.


## Solid-footprint escape failure and cleanup

The retained solid9×9 widget produces an accepted original escape order with the
same stock Footman query2/categoryca/custom radius8 setup as MAP-03.4. Fresh
original search produces no initial route. All seven elapsed1/32 group ticks
retain the exact starting position and zero velocity. At the final tick original
`603110 → 5fb190(1)` recovers the can't-path result, drains the internal/user
queues through `5fa7a0`, and releases the group, path, task/order payloads and
wrappers after two release ticks. The original footprint stays active throughout.
This result does not authorize a collision bypass for units inside construction.
The original witness and frozen fixture are documented in
[solid-widget recovery](retail-pathfinding-search.md#solid-widget-cant-path-recovery).

The engine reproducer places the idle worker at`(0,-64)` inside its new9×9
construction. Before the fix,120 actual server frames leave the displacement
flag, current Move head and walk thinker live: three failing assertions. The
displacement branch returned before normal progress accounting. Move now applies
its existing progress budget when the origin is statically blocked, then retires
the failed displacement through the ordinary stand/queued-order completion edge.
The point validator and all static masks remain active. Margin escape, replacement
Move and Stop are exercised in the same two-geometry server-frame fixture;
terrain and a separate building remain blocked after completion/interruption.
No struct/save/network change is introduced.

This integrates the verified failure outcome and cleanup, with an explicit timing
limit: the engine currently has its existing10Hz progress budget, while the
original witness completes after seven1/32 group ticks. The source carries that
TODO, owned by NUM-02.3/SCHED; this does not claim complete retry-cadence or
trajectory parity. Stop already cancels displacement through `unit_setmove`;
no second cancellation mechanism was added.


MAP-03.7 validation: final umbrella tests pass85 pathfinding tool tests and
37,286 assertions in2,155 engine tests per Classic/TFT. Construction displacement
passes85 assertions in normal and forced DEBUG Move builds; normal is restored.
WC3/SC2 production builds pass. Fresh `map-03.7-final-corpus/corpus-results.json`
passes134/134 with source fingerprints unchanged. Manifest SHA256
`0a691e0657ae0affaf25860d725e2a7f0d08cf6d10a58486e2ac8f2cda98750a`;
summary SHA256
`94fc67963332bff10a955bbc43297951f5e607c78d00657650a4302195bf33fe`.
Logs are `/tmp/wc3-map-03.7-final-full-suite.log`, `wc3-map-03.7-debug.log`,
`wc3-map-03.7-normal-restored.log` and `wc3-map-03.7-production.log`.


## Public Pow and exact logarithm/exponential arithmetic

NUM-01.9 recovers the registered cdecl Pow adapter`20f990` and its scalar
`0710e0` helper. The adapter compares absolute software differences from zero
against raw`3a83126f`, strictly: small base plus negative exponent returns0;
non-small base plus small exponent returns1. Otherwise an exact nonnegative
integral exponent uses wrapped`070120` conversion and binary power`071180`.
Other exponents use magnitude-log`070f70`, scalar multiplication by the exponent,
and exp`070c20`. Negative bases with fractional/negative exponents therefore use
magnitude, unlike the former host `pow` call. Exponent-zero base is guarded inside
the noninteger branch, so public`Pow(0,0)` still reaches integer power and returns1.

The magnitude-log strips sign/exponent into a1.x mantissa, applies rational
`06fd50` through reduction`06ff20`, scales by reciprocal-ln2, adds a truncated
integer exponent, then multiplies by ln2. The rational curve uses
`t=(x-1)/(x+1)`, ordered software products and an explicit raw exponent increment
for numerator doubling. Exp splits four times the magnitude into a truncated
whole and fractional quarter-step, runs the five-term ordered polynomial, then
multiplies integer power of the original quarter-step constant. Negative input
uses the original reciprocal. All constants and operation order are retained;
production uses the same integer scalar operations as Move, with no host log/exp.

The original/model/C oracle verifies24,423 completed calls, including12,000
arbitrary raw log words, exceptional exp words, output aliases, stack guards and
nonvolatile registers. O0/O2 yield identical outcome digest
`7cf000cfa0a565663f3828dc3186a88efe463c7a9a27ea12494024ba0d13430e`.
Expanded raw testing corrected a reference-model mistake: original`0715c0`
truncates toward zero, whereas`070c80` floors. A wrapped scaled negative sub-unit
from raw`ff920e3a` distinguishes them; the C port already used truncation.
Ghidra saves329 descriptive names,22 layouts,138 verified fields,93 exact x86
prototypes and31 named globals, including all15 immutable log/exp operands.

Two complete owned Frida captures repeat40 actual Pow argument/output pairs
exactly. Arguments are real public S2R products; compiled literals remain
NUM-01.7. Capture start pins all observer/generator/input/map hashes; adjacent
archived files reproduce each hash. The strict native sequence digest is
`238d8a90f6f0153a8311e673b62cb1c3b41f7f9a6e00f8406baa1fb917160888`.
The frozen fixture is`retail-public-power-inputs-1.27.json`.
The engine public-native regression reproduces27 host-Pow mismatches before the
port, then passes all40 exact cases. The wider public numeric subset passes136
checks, including the new error/inverse case. Fixtures compare real constants;
an earlier scratch version used integer expected literals and was corrected
before accepting the baseline.

There is a distinct nonreturning helper domain: wrapped integer conversion can
produce a negative signed exponent, and original`071180` arithmetic-shifts it
until it stays`ffffffff`, never returning. The oracle records173 separate bounded
controls, including full registered Pow with exponent2147483648; it assigns no
numeric output. The C helper returns a failure status without changing its
output, and the native reports a visible runtime error instead of fabricating a
number or hanging the server. This is an explicit bounded integration limit:
retail VM watchdog/lifetime behavior is not observed and remains NUM-01.2. The
engine error case uses a runtime-produced exponent and verifies the next normal
Pow call still succeeds. Shared initialization producers, non-ASCII parser
locale and other arithmetic consumers remain in that inventory; attack's
separate armor `powf` consumer has not been promoted without its own retail
producer evidence. No save/network or movement-clock representation changes.


## Scalar rounding and shared startup

NUM-01.10/11 extend the independent scalar evidence without changing any frozen
trajectory. These recovered procedures use ECX output, EDX input, plain RET and
return the output pointer in EAX; output may alias input.

| Procedure | RVA | Exact contract |
| --- | --- | --- |
| Floor | 070c80 | Negative nonzero sub-unit becomes minus-one; both signed zeros become plus-zero; truncate positive fraction or increment negative magnitude; exponent at least150 copies the raw word |
| Ceil | 070700 | Negative nonzero sub-unit becomes plus-zero; either signed zero and positive sub-unit becomes one; truncate negative fraction or increment positive magnitude; exponent at least150 copies the raw word |
| Round | 071250 | ScalarAdd(input, immutable half atcd53f4), then Floor; preserve truncating addition before the floor |
| Truncate | 0715c0 | Exponent below127 becomes plus-zero, below150 masks fraction, otherwise copies raw word; distinct from Floor for negative fractions |

`wc3_math.h` now owns reusable raw-word floor/ceil/round primitives alongside the
verified truncate primitive. The production arithmetic bridge compares all four
against original calls. No gameplay call is changed from an unrelated host ceil
without its original producer evidence. Round is directly used by
`PathMaps_Load` at04c922/04c951 to construct map dimensions; authored integer map
bounds in existing fixtures remain unchanged. Ceil has only three recovered
direct callers,3d93f4/3d9448/3dc7f9, outside the movement caller graph. These sites
do not prove that synthetic exceptional helper inputs are public gameplay inputs.

Reports `num-01.10-round-startup-O0.json` and `-O2.json` each retain81,688
rounding calls:20,222 raw inputs and200 output aliases per helper. Both C compiler
modes match the independent integer models exactly, with digest
`0bab35aa844a75957939052470982389bcb928b6a619503fc5f56ee69c99f713`.
The existing paired, decimal, public-angle and power digests are unchanged. The
asset-free regression includes adjacent integer/half thresholds, both zero
signs, arbitrary raw words and both C optimization levels.

The shared minus-one, zero and one words are not DLL literals. Three no-argument
startup entries tail-jump to original070d80:

| Registration slot | Entry | Destination | Output word |
| --- | --- | --- | --- |
| a7cdb8 | 001dd0 | d3c740 | bf800000 |
| a7cdbc | 001a80 | d3c744 | 00000000 |
| a7cdc0 | 001b80 | d3c748 | 3f800000 |

Original CRT process-attach routine78ee55 dispatches the initializer array
`[a7cd48,a7ec94)` through `initterm`; these three adjacent slots run in the shown
order. The numeric and power oracles now execute each actual initializer from
poisoned storage and check neighboring words, returned destination pointer,
stack and nonvolatile registers. Original instruction writes are never replaced.
`num-01.11-power-startup.json` retains24,423 completed calls and173 bounded
nonreturn controls, with unchanged digest
`7cf000cfa0a565663f3828dc3186a88efe463c7a9a27ea12494024ba0d13430e`.
Other isolated movement fixtures may still supply these now producer-verified
values; this does not claim that their full CRT/heap lifecycle executes.

Ghidra persists the three helper names and three initializer names,99 explicit
x86 prototypes,22 partial layouts/138 fields and35 scalar globals. Report:
`num-01.10-ghidra-types.json`. This closes shared startup01.11, not initialization
of every numeric family. Remaining inventory01.2 must classify those producers;
actual alias reachability, CRT grammar/locale and nonreturning VM lifetime stay
01.12/13/14. Compiled literal parsing remains01.7.
