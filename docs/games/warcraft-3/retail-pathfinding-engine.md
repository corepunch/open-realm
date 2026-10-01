# Pathfinding engine integration

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
is greater than or equal to the window. Turning continues. Internal approaches retain this propagation-window decision;
ordinary public point Move uses the separate arrival contract below. `movement.turn_blocked` records this tick's
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


## Compiled JASS real literals

NUM-01.7 recovers the decimal-token producer at925260 separately from public
S2R's070de0. The lexer supplies NUL-terminated unsigned token text through+98.
The procedure returns token109 in EAX, writes the raw scalar to+24, and preserves
nonvolatile registers with plain RET. Parent lexer9249d0 invokes it at924e6f;
the observed return site is924e74. Unary minus is a separate source operation,
including negative zero and sign reversal after a wrapped negative prefix.

The integer prefix, fractional numerator and power-of-ten denominator all
accumulate modulo32. Original070d80 converts each wrapped signed integer.
Original06fcd0 divides the fraction, then06fbb0 adds the converted prefix. Every
fractional digit participates; S2R's nine-significant-digit policy does not apply.
The denominator may wrap to zero. These are observable retail outputs, not
host IEEE decimal conversion or arbitrary precision source constants.

| Source literal | Raw VM/native input | Observed consequence |
| --- | --- | --- |
| `0.6` | `3f19999a` | Ordinary fractional conversion |
| `0.59999999999999998` | `bf85635d` | Fraction accumulator wraps negative; host conversion to0.6 is wrong |
| `4294967297.5` | `3fc00000` | Prefix wraps to1 before adding0.5 |
| `2147483648.0` | `cf000000` | Prefix converts as signed minus2^31 |
| `-2147483648.0` | `4f000000` | Unary minus reverses the wrapped prefix |
| `0.00000000000000000000000000000001` | `7f000000` | Wrapped zero denominator follows retail scalar division |
| `1.00000000000000000000000000000000` | `40000000` | Equal zero fraction/denominator takes the scalar equality result1 |

The engine now uses `wc3_literal` for JASS real tokens. `TF_RETAIL_NUMBER` attaches
the numeric producer to the parsed source token; loading Galaxy into the same VM
cannot change earlier JASS literal behavior. Galaxy retains its existing host
number conversion. Unsupported host-strtod identifiers such as nan/inf report a
visible runtime error instead of entering the decimal producer. Full original
lexical grammar is explicitly NUM-01.15; integer tokens925210/925490/925350 are01.16.
These limits do not claim that retail rejects the same identifiers before that
experiment. The dormant `VM_Compile` text emitter has no callers and does not
execute this production path.

Two owned live captures repeat32 public R2I argument/output pairs and40 read-only
compiler output/token/caller observations exactly. Ordinary, `.5`, `0.`, unary
signs and long prefix/fraction/denominator overflow all pass real compilation.
Public native sequence digest:
`5a3109312428b4c29a88b873b8c9f330b3b45561943f7b676fa3f8ed76e89883`.
Compiler event digest:
`408b2c06ef30ef303bc5d05e4470b9245d6939e41fa54004e43d6f6fe3126288`.
Archive files `runtime/num-01.7-literals-{first,repeat}-raw.jsonl` have adjacent
exact observer/generator/input/map copies and embedded hashes. Frozen fixture:
`retail-compiled-literal-inputs-1.27.json`. Report: `num-01.7-live-literals.json`.

`verify_wc3_pathing_literals.py` executes4,064 original/model/C calls with lexer
write-region, untouched token text, return-token, stack and register guards.
Optimized/unoptimized C retain digest
`29ddca0c9fc50ddab40b92a23cb4b5da47e3f8baf4be167c0d907c708eb20a24`.
Reports: `num-01.7-literals-O0.json` and `-O2.json`. The engine reproducer fails
four of five stored raw words before the fix. It now preserves the exact compiled
Move destination and save/load constants plus later evaluations. A mixed-language
regression retains both original source policies after parser reuse. Prior native
regressions store actual results through production hashtables and compare frozen
C words, so expected values are independent of the literal parser under test.
No save-record fields or format bytes change; reconstructed tokens carry the
producer flag and the existing program hash includes it. Historical saves with
old token hashes are not claimed compatible.

Ghidra persists336 names,23 layouts/142 fields,100 explicit x86 prototypes and35
scalar globals. `WC3JassLexerPrefix` assigns only the verified scalar/text/line/
length fields; other bytes stay undefined. Report: `num-01.7-ghidra-types.json`.


Validation at this checkpoint: WC3 and SC2 production builds pass; full `make test`
passes37,854 assertions/2,160 WC3 cases in both fixture variants and91 pathfinding
tool tests. Fresh corpus `num-01.7-final-corpus/corpus-results.json` passes all142
outcomes (58 oracles,73 archive audits,11 strict live contracts), with every
recorded source fingerprint unchanged. NUM-01.7 is closed. Next numeric producer
experiment is01.16; remaining ownership inventory is01.2.


## Compiled JASS integer words

NUM-01.16 extends the same source-token policy to original integer token108.
The real and integer actions share lexer text+98, token lengthc4 and raw result
slot24; the slot also carries other token data, so its scalar prefix name is not
a claim that every result is real. Saved parent `JassLexer_ReadToken` at9249d0
records the action dispatch and original call sites.

| Action | RVA | Input/return ABI | Parent return site |
| --- | --- | --- | --- |
| Decimal | 925210 | ECX lexer; no stack argument; RET plain; EAX108 | 924e38 |
| Octal | 925490 | ECX lexer; skip first0; RET plain; EAX108 | 924e46 |
| Hex | 925350 | ECX lexer; prefix length1($) or2(0x) at stack4; RET4; EAX108 | 924e56/924e66 |

Every digit multiplies and adds into a wrapping32-bit accumulator. Original
hex accepts uppercase and lowercase digits; octal has its own action rather
than a guessed decimal interpretation. Unary minus applies after token
production and wraps32, including minimum signed integer negation. The engine
uses `wc3_integer_literal_bits` and reconstructs signed values from raw bytes;
unsigned subtraction keeps unary negation independent of signed-overflow
optimization. `TF_RETAIL_NUMBER` now selects the source policy for both real and
integer tokens. Galaxy retains its prior conversion, including in mixed VMs.

| Source integer | Raw I2R argument | Public I2R result word |
| --- | --- | --- |
| `2147483648` | `80000000` | `cf000000` |
| `-2147483648` | `80000000` | `cf000000` |
| `9223372036854775808` | `00000000` | `00000000` |
| `18446744073709551617` | `00000001` | `3f800000` |
| `$10000000000000001` | `00000001` | `3f800000` |
| `0x10000000000000001` | `00000001` | `3f800000` |
| `040000000001` | `00000001` | `3f800000` |

Two owned captures repeat44 actual source/I2R argument/result pairs and42
read-only integer compiler word/radix/prefix/token/caller observations exactly.
Native sequence digest:
`c6551118d93d2a33338ef9b808730307cda7f27d5e19f57bc77884fc80cacbb6`.
Compiler sequence digest:
`a0dcc682acd7366904c854a031bf1955c812944c5ccc952b898fe4e0c7b15fe6`.
Archive files `runtime/num-01.16-integers-{first,repeat}-raw.jsonl` have adjacent
exact source/map copies and embedded hashes. Frozen fixture:
`retail-compiled-integer-inputs-1.27.json`. Report: `num-01.16-live-integers.json`.
Full invalid-token grammar remains01.15; these valid source cases do not claim
that malformed octal/hex/exponent/identifier forms have matching admission.

`verify_wc3_pathing_integers.py` executes16,080 original/model/C calls with
lexer write-region, untouched text, token108, stack-cleanup and register guards.
Both C optimization levels match digest
`3affdc0f24c0d35efe665da0f857f4d02bdb034ba8ca517e9ab0a569f329fcb0`.
Counts:4,027 decimal,4,026 octal,4,015 dollar hex and4,012 prefixed hex. Reports:
`num-01.16-integers-O0.json` and `-O2.json`.

The engine reproducer fails nine of44 raw source words before the fix because
host `strtol` saturates at its own width. The fixed test checks both source
integer words and all44 public I2R outputs against independent frozen C values.
A Move/save-load regression retains a wide compiled constant as257, routes its
I2R value into the issued point, and re-evaluates a wide hex literal after token
reconstruction. Mixed-language regression fails before the port, then retains
JASS wrapping and Galaxy's existing conversion after parser reuse. Both source
kinds share one producer flag; no save-record fields are added. Existing source
program hashing makes historical token-policy compatibility explicit.

Ghidra persists340 descriptive names,23 partial layouts/142 fields,104 explicit
x86 prototypes and35 scalar globals. Parent and all three integer actions are
saved; report `num-01.16-ghidra-types.json`. These numeric source improvements
preserve the previous movement/velocity corpus; they do not establish full
retail pathfinding parity.


Validation at this checkpoint: full `make test` passes38,132 assertions/2,162
WC3 cases in both fixture variants and93 pathfinding tool tests. Fresh corpus
`num-01.16-final-corpus/corpus-results.json` passes all146 outcomes (59 oracles,
75 archive audits,12 strict live contracts), with every recorded source
fingerprint unchanged. NUM-01.16 is closed. The next producer experiment is
NUM-01.13, with the shipped CRT now analyzed and saved in the same Ghidra project.

## Public decimal byte grammar and CRT locale

NUM-01.13 closes the original default-locale byte classifier and its public
S2R producer. The engine's existing ASCII digit predicate is already correct
for this domain; the new regression preserves that result through authored
object data, public string natives, hashtable words and Move admission.
No unobserved locale behavior is substituted into the parser.

The exact sibling `msvcr120.dll` has SHA256
`86e39b5995af0e042fcdaa85fe2aefd7c9ddc7ad65e6327bd5e7058bc3ab615f`.
Preferred base is10000000; table/function addresses below are RVAs.

| Original CRT entry/state | Recovered contract |
| --- | --- |
| `isdigit`0f1d5 | Cdecl integer; ever-changed flag0 reads `pctype[C]&4`; nonzero calls `_isdigit_l(C,NULL)` |
| `_isdigit_l`12652 | Single-byte locale directly indexes signed-byte prefix; multibyte locale delegates to `_isctype_l` |
| `_isctype`8957d / `_isctype_l`895ac | Default global table versus explicit locale; `_isctype_l` direct-table range is only-1..255 |
| Locale context0f764 | ECX context, stack4 optional locale pair, RET4; explicit pair avoids TLS; null reconciles TLS/shared state and owns a temporary ownlocale bit |
| `_wsetlocale`132b8 / write1335c | A non-C requested locale sets the ever-changed flag1; returning to C does not reset it |
| Ever-changed globaldf7c4 / pctype globaldf858 | Captured native values0 and pointer toRVA1158 |
| Default localeinfodfa84 | Verified `mb_cur_max`+74=1 and `pctype`+90 points toRVA1158 |
| Table1058..1357 | 128 signed-prefix words plus256 byte words; digit bit4 only on ASCII48..57 |

Original070de0 promotes input bytes as signed `char`, so80..ff reaches
`isdigit` as-128..-1. All of those entries have digit mask0. It stops at the
first rejected byte while retaining the preceding scalar; the optional sign,
single decimal point and nine-significant-digit rule remain unchanged.
The entire384-word table digest is
`e7304be1d56c85907c3c409a5252fba8b5b51fa74aeb8fa4703b67e8427d7d3f`.

`verify_wc3_pathing_decimal_ctype.py` executes1,409 original classifier calls:
384 each for default `isdigit`, explicit-default `_isdigit_l` and `_isctype`,
plus257 direct-domain `_isctype_l` calls. Another1,020 original070de0 calls
cover bytes01..ff with four prefix/suffix shapes. All match the independent
integer scalar model and production C at-O0/-O2, retaining text/output guards,
nonvolatile registers, stack cleanup and observed signed argument promotion.
Classifier digest `24428fc4fc4d87a3878d9973d2dcc574b13aa5ed1189d4a5efc60d9d6b590c48`;
parser digest `ea16cf01eaa14b274f4b819e4668bd9b2d173f90f9643054b45c7b2ab472d87a`.
Reports: `num-01.13-decimal-ctype-o0.json` and `-o2.json`.

The live producer changes only the copied map's Footman `unam` object field
to the128 raw bytes80..ff. `GetUnitName` and `SubString` supply each byte to
public S2R. Two further calls parse Move coordinates with80/ff before a trailing
digit. Both complete captures repeat514 parser/native outputs and1,040 actual
CRT digit observations, including locale flag0, original table RVA1158 and
the exact parsed point-task destination.

| Public byte input | Result word |
| --- | --- |
| A byte80..ff alone | `00000000` |
| `12` + byte + `34` | `41400000` (12) |
| `.5` + byte + `7` | `3f000000` (0.5) |
| `-` + byte + `0.2` | `00000000` |
| `-1936.25` +80 +`9` | `c4f20801` |
| `-144.125` +ff +`9` | `c3102000` |

Frozen fixture: `retail-public-byte-inputs-1.27.json`. Strict checker:
`verify_wc3_byte_inputs.py`; report `num-01.13-live-byte-repeat.json`.
The full byte/classifier/native sequence digest is
`fab4d0567d4df888159c71243a4f682b0ab0470fe671c2be8d2fdeef91ccec44`.
Raw files are `runtime/num-01.13-bytes-move-{first,repeat}-raw.jsonl`, with exact
ten source files and map copies adjacent. `--byte-events` checks the loaded
CRT path, timestamp and image size before installing its read-only classifier
observer. The default byte producer is not inferred from formatted R2S text.

Wine can substitute its built-in CRT while reporting the sibling DLL's path.
The PE-header guard rejected that capture; it is retained as
`runtime/num-01.13-bytes-crt-mismatch-raw.jsonl`. In the owned isolated Wine
prefix, `AppDefaults\\war3.exe\\DllOverrides` sets `msvcr120=native`.
Earlier captures without this CRT-header guard are not retroactively certified
as native-CRT observations. The accepted probes use75-second process bounds
and send the loading key at30 seconds; they retain timer300 completion.

Other rejected producer attempts are retained in the runtime archive: raw
Latin-1 JASS source containing80..ff crashes during compilation; this is not
a public S2R result or a complete lexical rejection proof. WTS/GetLocalizedString
returns empty byte slices, while a direct `TRIGSTR_999999` literal remains the
reference text. Raw input capture exposes both substitutions. Use the verified
unit-name object field instead. Rejected attempts never become numeric evidence.

`wc3_shipped_crt.py` maps the original CRT and applies HIGHLOW relocations
without replacing code. The general numeric oracle now uses its actual
`isdigit` instead of an ASCII stub; all previous numerical digests remain fixed.
Alternative locale construction, TLS startup and `_isctype_l` Windows multibyte
services are excluded. No direct Game.dll locale-setting import/symbol was
found; absence alone is not a proof of dynamic unreachability.

Ghidra stores `/CRT/msvcr120.dll` in the same project. `MapPathfindingCRT.java`
verifies its executable hash, preserves incompatible fields, saves three
partial layouts/eight function roles/seven globals and the explicit context
ABI, then writes `num-01.13-ghidra-crt-types.json`. Undefined prefix bytes stay
undefined. The engine regression `pathfinding_decimal_high_bytes_match_retail_words`
passes2,054 assertions without a production behavior change. Full `make test`
passes40,186 assertions/2,163 WC3 tests in each fixture variant and96 pathfinding
tool tests; WC3/SC2 production builds pass. Fresh `num-01.13-validated-corpus/corpus-results.json` passes150/150 declared
outcomes (60 oracles,77 archive audits,13 strict live contracts), with all63
recorded source fingerprints unchanged. NUM-01.13 is closed; reachable alias
producer work remains NUM-01.12.

## Effective speed reaches actual movement

`unit_current_speed()` now consumes the same existing `unit_effective_speed()`
as selection-group minima. Previously individual stepping read only the raw
MoveSpeed override/authored base (plus Earthquake), leaving Cripple, Bloodlust,
Wind Walk, Slow, Purge, poison and movement auras disconnected from single-unit
travel. A group also capped a boosted member against its unmodified own speed.
The effect formulas stay in their owning abilities; Move applies their existing
composition once before selecting a group cap.

`wc3_spell.movement_statuses_change_actual_steps_and_expire` executes the owning
Cripple/Bloodlust procedures with non-stock37%/17% data, issues Move, and advances
the actual Move thinker. Four failures precede the fix. A200-speed unit now
travels12.6/23.4 units per current100ms frame; a220-speed peer shares12.6/22.0
formation travel, and expiration restores20-unit travel. The regression checks
step length because routing may adjust the final point to a legal cell center.
No saved fields or wire contracts change. This repairs an engine consumer gap;
full retail modifier ordering, clamps and32ms owner cadence remain unproved.

## Vector-heading operand relationships

Original `1d4c80` divides vector X by the separately supplied length into
`EBP-4`, then writes Acos into `EBP+c`. At Acos entry these are `ESP+8` and
`ESP+24`, respectively. They stay distinct even when the caller's final output
aliases vector X/Y or the length. `16f630` similarly computes its complete
heading error before the final output store. The raw Acos helper instead loses
a negative sign when its input/output pointers alias; this is a negative control,
not behavior to inject into the movement consumer.

`verify_wc3_pathing_heading_aliases.py` checks784 vector and4704 full heading
cases, ten direct Acos controls, guards, stack/nonvolatile registers, original
startup and independent generated-table arithmetic, against-O0/-O2 production C.
Word digest: `641aa3b6156fde2731578ffe95660150b98ea56700f8b83c7765b14fe486e2cf`.
Two actual turn captures retain191 nested heading chains (181 negative quotient
inputs),192 velocity/position/facing commits and exact repeat. Private captures:
`runtime/num-01.20-heading-alias-{first,repeat}-raw.jsonl`, each with adjacent exact
source/map archive; report `num-01.20-live-heading-alias-repeat.json`.
The strict verifier rejects changed source/map provenance, missing/reordered
observations, changed pointers, sequence linkage and quotient/output words.

Ghidra now retains354 names and119 explicit prototypes, including the scalar
helpers and these producer signatures. The33-function inventory records6898
references; name-only filtering is not reachability proof. Other trig, basic
arithmetic and power aliases remain NUM-01.17/18/19. This witness confirms the
existing heading implementation; it adds no new movement behavior.

Validation for this integration: full `make test` passes40329 assertions in2172
WC3 cases for both Classic/TFT plus97 pathfinding tool tests. The focused
movement/status regression passes22 assertions with DEBUG enabled and disabled;
WC3/SC2/WoW production builds and both boundary audits pass. Only the four new
heading entries were rerun in `num-01.20-status-final-corpus`; all four pass.

## Retail fine search drives nearby detours

Move now uses `wc3_pathing_fine.h` through the geometry adapter
`G_FindMovePathWaypoint`. The search uses the recovered row-major eight-neighbor
order,15/21 costs, piecewise integer heuristic, equal-key heap insertion/right
child removal ties, generation-tagged stale entries and cheaper closed-node
reopening. Queue attempts include stale pops and the rejected iteration after
the budget. A ready generic field previously replaced the mover's retained
turn; nearby reachable detours now retain the fine turn through travel.

The wrapper now uses the [retail class shape](#retail-collision-classes-reach-routing-and-stepping) below,
while keeping nearest-ring endpoint correction. The [segment port](#retail-segment-sampling-and-waypoint-selection)
now replaces its initial farthest-visible-cell adapter with the original selection
loop. FINE-01.4 originally retained the prior ceil-radius shape. Search work stays bounded to
2,048 queue attempts for endpoints within48 cells per axis. One reusable game
scratch block is832KiB; its sparse hash avoids allocating or clearing one node
per map cell on each request. No saved edict or network layout changes. Direct
clear paths, known unreachable/adjusted interaction endpoints and distant routes
retain their existing handling. These adapters are explicit partial integration,
not proof of retail footprint admission, smoothing or full trajectories.

`fine-engine-exact-o2.json` and the fresh `oracle-grid-engine` corpus entry compare
all288 original static searches with the same production header: complete cell
chains, costs, charged queue work and allocated nodes match, including a second
C query per request. Asset-free tests repeat the frozen original results at O0/O2
and exercise original queue witnesses for equal ties, stale replacement and
closed reopening. The288 complete map searches themselves have no closed
reopening, so that evidence remains separate.

`t_pathfinding.c` first reproduced a generic route mismatch on the original
wall-gap chains. Its actual Move test then reproduced18 failed assertions across
three blocked detours when a ready field discarded the fine turn. Both changes
now pass; the fourth gap has a legal direct route and checks that direct path.
The test retains each waypoint after the mover advances. Existing collision
radius, distant-route, interaction, worker and unreachable regressions remain
required, alongside the full Classic/TFT suite.

Reproduce the original/C comparison with the compiled probe shown above and:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_grid.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --engine-library /tmp/wc3-pathing-engine.so --report /tmp/fine-engine-exact.json
```

Add `--fixture tools/ghidra/fixtures/retail-fine-grid-1.27.json` only when explicitly
regenerating the pinned original result fixture. Ghidra's heap and search-entry
comments link these consumers without claiming the remaining admission policy.

## Retail collision classes reach routing and stepping

The former `ceil(radius/cell_size)` shape required a3x3 square for every positive
radius up to one cell and a5x5 square above that. Original `14ad50` and complete
`16ee80` endpoint calls instead select class0/1/2/3 at0.5/1/1.5 fine cells,
covering1x1/2x2/3x3/4x4 cells. An even-sized square starts at
`floor(position) - size/2`; it is biased toward decreasing X/Y. The authored
collision scalar is a world value divided by32 by the original unit producer.

`wc3_fine_class/cover` now supply these bounds. WC3's world adapter uses existing
static obstacle prefix sums for constant-time endpoint tests. For a legal current
footprint, checking the neighbor square and both diagonal side squares is
equivalent to retail's entering perimeter strips: unchanged interior cells were
already legal. This static equivalence does not infer dynamic object eligibility.

Move consumes the shape in its nearby search, direct line, waypoint retention,
step validator, displacement failure check, selection destination reservations
and ordinary point-order correction. Destination correction still uses the
existing nearest-ring policy. The [segment port](#retail-segment-sampling-and-waypoint-selection)
now replaces Bresenham/corner sampling with original `168d30` sampling. Long shared
fields consume the same class bounds as described below. These partial
policies remain visible rather than being described as complete retail parity.

`foot-corridors-original.json` executes24 complete original searches, reuse
repeats and request reconstructions over widths0..5 and all four classes. Widths
1/2/3/4 respectively are the first passable corridor.
`foot-endpoints-engine-exact-o2.json` compares1,184 complete original static
endpoint calls with production C geometry, including class boundaries, map edges,
negative positions and each single blocked cell. Original dynamic84 cases also
run, with no C parity claim. The frozen endpoint fixture is asset-free and runs
at O0/O2. Fresh selected corpus results are in
`foot-engine-validated-final-corpus/corpus-results.json`. Ghidra saves356 descriptive
functions, including `149370/1492b0`, with existing119 prototypes unchanged.

The engine passage regression covers seven near-boundary radii with corridor
width below/equal/above each selected class. It first failed16 of42 geometry
assertions. The fixed test also issues14 fitting Move orders, preserves each
requested destination and advances the actual Move thinker. A pre-existing
wall-detour regression still used the superseded3x3 predicate; it now checks
the independently known four class1 cells against its raw fixture on every
frame and retains exact final arrival. No wire, edict or save layout changes.


## Long fields use the same class geometry as Move

`G_RequestMovePathField` derives half-open cell offsets with the same
`wc3_fine_class/cover` helper as fine routing and stepping. The shared field
builder consumes a generic `pathGridQuery_t`: min/max offsets and blocked mask.
Expansion, diagonal side checks, flow sampling, goal correction and cache keys
all use that query. WC3 owns the class policy; existing shared radius APIs retain
their symmetric ceil-radius geometry for other callers, including SC2.

`G_ActivateMovePathField` checks the cached query against the mover's actual
class and mask. The old 0.01 radius tolerance could incorrectly reuse a field
across a class boundary: radii0.499 and0.5 differ geometrically despite the small
scalar difference. Same-class radii can share a field; different bounds or masks
cannot. Static rebakes invalidate all generations as before.
`G_ClosestReachableMovePoint` also floods the mover's class graph, so an
unreachable-order fallback agrees with both the field and the Move validator.

The new winding-corridor regression first failed7 of14 assertions: a class1
mover could not obtain a long field through a two-cell L beyond the48-cell fine
search envelope. It now checks field reachability, advances the actual Move
thinker for eight ticks and validates the four covered cells against the raw
fixture. Pinching the passage to one cell invalidates the old generation;
class1 becomes disconnected while class0 still reaches the exact target.
The class1 fallback chooses the near-side49.5/5.5 cell, while class0 preserves
the exact100.5/15.5 requested destination.

A separate regression reproduced pending-job scratch corruption: a synchronous
closest-reachable source flood overwrote an incremental goal flood's prices and
queue, then the interrupted job published those prices under its original goal.
The original goal incorrectly had a nonzero flow vector. Source floods now
cancel the pending scratch job; its next request restarts correctly. Cached
completed fields remain separate. This fixes engine scratch lifetime, not an
inferred retail scheduler contract.

Two pre-existing tests encoded the superseded shape. The disconnected-Move
regression now checks all four class1 cells independently rather than invoking
the old3x3 predicate. The blocked-mine-entry regression formerly supplied only
the mine footprint; class1 can legally advance into interaction range there.
It now supplies a real separating wall and still requires visible workers,
zero occupants and a retained Harvest walk. Existing successful mine-entry,
return-resource, crowd/group, queued orders and save/load cases remain required.

Validation: the full Classic/TFT suite passes41,710 assertions in2,176 WC3 cases
per variant; all101 pathfinding tool tests and WC3/SC2/WoW production builds pass.
Fresh selected `long-field-validated-corpus/corpus-results.json` retains both
original/C footprint oracles; it does not represent a full157-entry corpus run.
Ghidra's saved `16ee80/1492b0` comments link the additional engine consumers.
There are no wire, edict or save layout changes. The shared SPFA field and
interpolated steering are still the engine algorithm, not the retail adaptive
hierarchy or full-trajectory replacement; FOOT-01.5 closes geometry consistency
only. The subsequent [segment port](#retail-segment-sampling-and-waypoint-selection)
now connects the all-class sampled segment and its waypoint consumer.


## Retail segment sampling and waypoint selection

`wc3_pathing_segment.h` ports `168d30` and its four directional footprint
consumers `149440/149630/149970/149cc0`. It uses the existing exact software
add/multiply/floor/integer helpers. Sample distance starts at1 and increases
by1 while strictly below the supplied length; unchanged cells are skipped.
Previous cell starts at0,0. Neither endpoint is queried by the sampler itself,
and lengths at most1 perform no cell query. Class0 tests the current cell, then
X/Y predecessor-side cells for a diagonal code. Larger classes test entering
rows/columns; diagonal codes use a horizontal strip of size+1 followed by a
vertical strip of size. They are not complete squares at every sampled point.

`wc3_segment_normalize` ports `168280`: compute software-float squared length
and sqrt, then multiply both components by the reciprocal only above length1.
The original cardinal3 vector yields length3.0000576973 and direction0.9999808669.
Using native sqrt or ideal direction1 changes the final sample. Sixteen original
normalizer input/output word triples match C. The full sampler comparison also
checks every composed sample coordinate, query order and early rejection.

`G_MovePathLineIsPathable` now uses the sampler for direct routing, cached-turn
retention and swept Move checks. It retains explicit full-footprint source and
destination admission before sampling. That admission is an engine adapter;
FOOT-04 still owns the original public clamping/exclusion policy. Coordinates
still enter through the current game's world-to-grid transform. The callback
uses static ground/flight bits; dynamic retail eligibility/suppression is not
inferred from this port. Move's separate live-unit collision policy still applies.

`wc3_segment_waypoint` ports the `167bf0` candidate loop. The next route point
is accepted unchecked; progressively farther points are tested until the first
failure, then the last accepted index is returned. `G_FindMovePathWaypoint`
reconstructs destination-first points from the verified search parents and uses
that loop. Its initial farthest-to-nearest visibility scan could choose past a
rejected nearer candidate and was not the original policy. The extra reusable
point array is128KiB; total static fine-search/selection scratch is about960KiB.
No saved/network fields change.

The asset-free `retail-sampled-segments-1.27.json` freezes43,244 complete original
static sampler calls across four classes, both masks, sixteen cardinal/oblique/
45-degree directions, seven interior/edge/corner positions and eight lengths.
It includes8,064 unchecked short-segment cases and both sides of length1.
Original calls use real cell/footprint routines without code replacements.
The result and queried-cell sequence digest is
`4c1235cbdf7aa53b3be95c43cb2ca12c5436514bb0db9231623e3326296746e4`.
C matches at O0/O2. The same oracle supplies123 complete `167bf0/165e60`
selection/commit calls on the reachable frozen fine chains, across all four
classes. It verifies the selected index/point and restoration of self suppression.
Those are supplied cell-centred chains, not evidence for all retail reconstruction
coordinates or public admission.

The engine first reproduced four missed first-sample strips, one per class,
with blockers outside both legal endpoint footprints. All four now reject.
The actual Move steering test uses original wall-gap selection indices11/6/2/0,
which select11.5/10.5,15.5/13.5,18.5/17.5 and19.5/19.5. Retention still passes after
the mover advances. The clear-waypoint regression now checks retail sampling
rather than the superseded Bresenham predicate. Existing actual detour, worker,
group, unreachable, orders and save/load regressions remain required.

The full Classic/TFT suite passes41,733 assertions in2,178 WC3 cases per variant,
plus103 pathfinding tool tests; the production WC3 build passes. Fresh selected
corpus results are in
`segments-engine-validated-corpus/corpus-results.json`: class0 baseline and new
all-class/C entry pass. No full158-entry rerun is claimed. Ghidra saves360 names
with119 existing prototypes unchanged and links both sampler and waypoint
consumers to their production helpers. Next integrate mixed dynamic eligibility
and target exclusion through the full fine request; shared adaptive routing and
whole trajectories remain separate required work.


## Idle objects affect nearby Move routes

FINE-01.5 puts live idle ground-unit footprints into location-order direct
checks, nearby fine searches, waypoint selection and retained-segment checks.
An idle unit ahead now produces a detour before local circle collision. Actual
Move order/think/step regressions pass for all four mover/object classes and
leave the idle unit fixed. A new object entering a retained segment invalidates
that turn and produces a fresh route.

Original `1489a0` blocks a live kind1 object when its active category bit is
set, its low24 category overlaps the query, and `flags40 & 8fffffff` is zero.
Normal search mode0 additionally excludes `20000000` and `40000000` objects.
Endpoint mode1 includes those objects. Original `1606e0` maintains `20000000`
from committed velocity at mover+80/+84; mover+88 is the speed cap, not the
current vector. Merely entering the walk animation does not set that bit.

Fresh read-only crowd captures establish these profile publications:

| Unit | Getter category/query | Fine object / owned path | Velocity commits | Starts / stops | Idle object hits |
| --- | --- | --- | ---: | ---: | ---: |
| Footman `hfoo` | `ca / 2` | `010000ca / 02000002` | 3,012 | 90 / 90 | 2,423 across179 per-request records |
| Hippogryph `hgry` | `0 / 4` | `01000000 / 04000004` | 1,661 | 9 / 9 | 0 |

Each capture contains18 getter pairs/publications, all velocity commits,
completed crowd markers and a successful observer end. Fine search counts
are56 and9. The new observer fields only read the occupancy pointer and flags;
no retail object or velocity is changed. `verify_wc3_profile_trace.py
--fine-objects` rejects truncated counters, missing identities, altered
velocity flags, omitted/unclassified hits and blocker records inconsistent
with published profiles. The captures do not observe the transient group
`40000000` flag; its existing composed producer evidence remains separate.

`verify_wc3_pathing_grid.py --objects` adds **192 complete original searches**,
192 retained-cell-metadata repeats and192 setup/search/reconstruction requests:
four classes, ground/flight masks, open/gapped static terrain and12 object-chain
variants. Variants include idle, moving, transient, suppressed, disabled,
unlinked, inactive, empty flight category, overlapping movers/idle objects in
both chain orders, mixed categories and a solid object wall. The actual original
linked records coexist with generated search-node metadata. The independent
reference checks reachability/cost; production C eligibility, entering-strip
geometry and search match every parent chain, cost, pop count and node count
at O0/O2. Frozen evidence: `retail-fine-objects-1.27.json`.

Game queries snapshot eligible idle-unit class rectangles. Overlapping objects
coexist; a moving object never erases an idle blocker. Direct/retained queries
use a conservative area-tree region derived from the sampled strips and largest
object cover. The1900-idle-unit direct query falls from0.19ms to below0.005ms
per call in the same benchmark. A regression keeps a class3 biased edge even
when its physical bounds miss the class0 segment. Fine searches retain a full snapshot because their detours can
leave the endpoint rectangle. Rectangles are sorted by their minimum X; each
cell only examines rectangles starting in its four-column range. Shared static
fields are untouched and retain their generations when a neighbour starts or
stops. Scratch stays in the game module; edict/network/save layouts are unchanged.

Interaction abilities keep their existing range/queue policy. Gold/resource
return collision queries still ignore peers; lumber still uses its local queue
and bounded pass. When a live object occupies the goal and the static field is
pending, a clear static corridor keeps collision-aware local steering rather
than pausing forever. The original nearest-node partial route is still
FINE-02.2; public self/target admission and target-exit semantics remain
FOOT-04/FINE-01.3. The engine adapter retains existing ground-unit eligibility
until BASE-02 supplies the complete authored category table. Water/amphibious
profiles and native pathing-disable producers are not certified by this slice.

Reports under the standard report root:
`fine-objects-engine.json`, `ground-object-policy-live.json`,
`air-object-policy-live.json`, and
`idle-objects-final-corpus/corpus-results.json`. The fresh selected corpus
passes4/4 entries (static baseline, mixed-object C comparison and both live
contracts); no full161-entry rerun or whole-trajectory match is claimed.
Ghidra saves360 names with prototypes unchanged and updates the occupied-cell,
velocity flag, profile publication/getter and fine-rectangle contracts.

Reproduce the original/C comparison with:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/run_wc3_pathfinding_corpus.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27 \
  --output /tmp/wc3-idle-objects-new-corpus \
  --only oracle-grid --only oracle-grid-objects-engine \
  --only live-ground-fine-objects --only live-air-fine-objects
```

The output directory must be fresh. Use the Unicorn environment for the runner;
the default system Python lacks Unicorn and is correctly rejected. Capture
metadata pins the builder/probe/controller/observer and map hashes; source copies
are archived alongside `runtime/{ground,air}-object-policy-first-260930.jsonl`.


Final validation: both Classic/TFT suites pass41,787/41,787 assertions in2,184
WC3 cases per variant, all106 pathfinding tool tests pass, the debug pathfinding
suite passes443 assertions in68 cases, and the production WC3 build passes.
The renderer suite also passes5,862 assertions after reproducing/fixing a
multi-statement macro that dropped higher ground layers when a lower texture
was missing; see [ground list lifetime](loading-and-assets.md#whole-map-ground-list-preserves-earlier-layers).


## Nearest partial routes survive blocked goals

FINE-02.2 ports the original nearest-node update from `14a560` and retains its
parent chain when `14a4c0` exhausts its queue or work budget. Distance is an
unsigned wrapped integer square; only a strict improvement replaces the
nearest identity. Equal-distance candidates retain the first admitted node.
The update occurs on fresh/list-invalid admission, before that node is popped.

`verify_wc3_pathing_grid.py --objects --partials` executes208 complete core,
metadata-repeat and setup/search/reconstruction scenarios, then **1,456 full
requests** at budgets0/1/5, one before/equal/one after the unrestricted pop count,
and2048. Four classes, ground/flight masks and mixed chains include an idle
object covering the destination and a full-height idle wall. There are925 failed
and531 successful requests. **177 failures already have the goal as nearest**:
its node was admitted, but the final goal pop was denied. Budget failure still
returns a partial route; it must not discard that chain or claim search success.

Original `148100` preserves the exact source when nearest is the start;
otherwise it reconstructs toward the nearest cell centre and updates its stored
adjusted destination. Original wrapper output/source/endpoints, nearest identity,
distance, parent chain, queue charge and allocation count are frozen in
`retail-fine-partials-1.27.json`. Production C matches all nearest chains and
metadata at O0/O2 with storage reuse, plus208 full chains. No public scheduler
budget-producer or unrelated target-exit behavior is inferred from these
supplied requests.

Location Move now uses a useful nearest chain even when the requested point
cannot be reached. A full-height wall of idle units first reproduced four failed
engine assertions: no retained path or approach turn. The same scene now selects
the original `(10.5,4.5)` turn, advances through actual Move thinks and retains
its original `(19.5,4.5)` order. Removing the wall through actor lifetime calls
lets that Move finish toward the unchanged destination. A nearest chain with
only the current cell still supplies no advancing turn; the existing local
collision/settling policy remains responsible there.

A completed static field that proves disconnection continues to own the existing
component fallback. Without that check, a valid partial fine turn postponed the
already-verified static fallback. Static-only query callers retain their prior
complete-route result contract; interaction abilities keep their existing policy.
No persistent actor, save or network layout changes.

Reports: `fine-partials-engine.json` and
`partials-engine-final-corpus/corpus-results.json`. The selected static baseline,
mixed-object C and partial-route entries pass3/3; no full162-entry rerun or whole
trajectory certification is claimed. Ghidra saves the nearest-admission,
termination and wrapper contracts alongside the existing layouts/prototypes.

Validation: both Classic/TFT suites pass 41,796/41,796 assertions in 2,185
WC3 cases per variant. All 107 pathfinding tool tests pass; debug and release
pathfinding each pass 452 assertions in 69 cases. The production WC3 build
and game/client boundary audits pass.

## Authored movement masks reach terrain and object queries

BASE-02.4 ports the stock movement profiles published by original
`690c20/690c80 -> 05c7e0`. One read-only copied-map capture creates seven stock
units; each emits two paired getters and publications, with enclosing Unit+30
rawcode and resolved mover identity/epoch. Retail UnitData.SLK supplies `movetp`:

| Unit | movetp | Category | Query | Owned path mask |
| --- | --- | --- | --- | --- |
| hfoo | foot | ca | 02 | 02000002 |
| hkni | horse | ca | 02 | 02000002 |
| hsor | hover | ca | 02 | 02000002 |
| hgry | fly | 00 | 04 | 04000004 |
| hbot | float | ca | 40 | 40000040 |
| uplg | amph | ca | 80 | 80000080 |
| halt | _ | 00 | 00 | 00000000 |

The engine previously queried02 for every non-flyer. Its shared cell predicate
also ignored40/80. The actual Move validation and nearby-route regression
reproduced ten failures, then passes for all six mobile types. Queries now honor
the complete supplied byte mask; the game selects40/80 from the authored row.
Flight remains mutable through AI_FLYING, preserving existing grounded-flyer
ability policy. Disabled movement retains its existing owner-level contract.

Original `04caba` derives amphibious80 when WPM bytes contain both02 and40.
All256 original single-byte outputs are frozen; the production WPM helper matches
movement bits at O0/O2. Map reading invokes it before publishing path cells.
The original6144 WPM and12288 image loops still pass, and a fresh engine oracle
compares all256 WPM bytes. Image decoding is not replaced by this WPM rule.

Original widget blue coverage creates categoryc2. Baked static footprints now
block walk/float/amph; SC2 supplies its existing02 policy through the same private
callback. Two failing water-footprint assertions now pass, including release
restoring all three lanes. Command-time unit occupancy now uses categoryca
rather than the encountered unit's own query mask. A ground unit consequently
blocks40/80, while a flyer with category0 blocks no query. The command-destination
regression reproduced all three mismatches before correction. The former
flight-blocking expectation is replaced by the captured zero-category contract.

FINE-01.2 extends complete mixed-chain composition to all four published masks:
384 original searches,384 metadata-repeat searches and384 full setup/search/
reconstruction requests, across all four footprint classes. Production C matches
cell routes, cost, work and allocation counts, including repeated storage at
O0/O2. The engine's fine/direct/retained queries use the mover's actual mask and
exclude disabled rows. Existing local collision ownership is unchanged.

Evidence: `runtime/movement-profiles-first-260930.jsonl`,
`movement-profiles-table.json`, `fine-movement-profiles-engine.json`,
`water-load-masks-original.json` and `water-masks-final-corpus/corpus-results.json`.
The four selected corpus entries pass. Source/map/capture hashes and compact
observations are pinned. These stock mask observations do not establish every
row's profile parser, support-surface transitions, bridge policy or a complete
boat/amphibious travel trajectory; BASE-02.1 retains those requirements.
Ghidra saves and reads back the three getter/publication contracts, retaining
360 function annotations and119 existing prototypes. No actor/save/network
layout changes. All112 pathfinding tool tests pass. Both Classic/TFT suites pass41,868 assertions in2,188 cases;
debug pathfinding passes524 assertions in72 cases, and WC3/SC2/WoW production
builds and both boundary audits pass.

## Exact fine route endpoints

ROUTE-01.3 ports original `147dc0` into the production nearby-route adapter.
Node centres use the already-verified truncated integer conversion and scalar
add-half. The destination-first chain then copies the exact source into its
last entry. It compares the first point's floored coordinates with the supplied
destination and copies that exact destination only when both match. A one-node
chain can consequently become the exact goal after source replacement.

The original oracle expands from480 diagonal chains to **3,840 eight-direction
chains**, lengths1..5, negative/positive origins and matching/nonmatching goal
cells. Original software arithmetic and append execute without stubs. Frozen
raw inputs/coordinate words in `retail-fine-reconstruction-1.27.json` match the
production helper at O0/O2 with repeated storage; the fresh original/C oracle
also matches. The existing9,216 coarse cases remain original/model evidence.

The engine regression first reproduced eight failures: all four classes turned
`(19.875,17.125)` into `(19.5,17.5)`. The route adapter now retains the admitted
fractional world destination when it selects the complete endpoint. Partial
requests supply the nearest cell centre to reconstruction, following `148100`;
they cannot substitute the original fractional goal or claim completion. The
idle-wall actual Move scenario now orders `(19.875,4.125)`, retains its centre
approach, advances, then resumes toward that same fractional order after actor
removal. It still respects the existing arrival tolerance; full retail arrival
range/heading behavior remains TARGET-01.2.

Source/destination admission continues through the existing engine policy;
this port does not certify public invalid starts, capacity growth, coarse
coordinates or complete world-to-fine numerical parity. Those remain
FOOT-04/ROUTE-01.1/01.2/NUM-02.3. The helper consumes valid internal parent chains
and the existing bounded route storage, without actor/save/network changes.
Reports: `fine-reconstruction-eight-directions-engine.json` and
`fine-reconstruction-final-corpus/corpus-results.json`; the fine endpoint and
existing partial-search entries pass2/2. Ghidra saves the reconstruction contract
alongside the existing360 function annotations and119 prototypes.

Validation: both Classic/TFT suites pass41,880 assertions in2,189 cases; all113
pathfinding tool tests pass. Release/debug pathfinding passes536 assertions
in73 cases; the production WC3 build passes. Both boundary audits are clean.
The first umbrella run overlapped a production link and an asset CLI saw an
incomplete shared library; the serialized rerun passes.

## Point Move arrival

TARGET-01.4 ports the actual zero-range point command into Move. Retail
`MoveBridge_StartPoint` (`05b970`, ECX Unit+164) receives eight stack arguments;
its last argument, index7, is a pointer to world arrival range. The actual
`Move_HandlePointTask` caller supplies zero. After division by32 the bridge
publishes the minimum `3efae148` (approximately0.49 fine cells) through
`1710a0` to mover+b0. On a32-unit WPM grid this is approximately15.68 world units.
This is independent of collision size, the step budget and `PropWindow`.

The complete `16e910` predicate subtracts source from target with the retail
software arithmetic, computes distance and vector bearing, applies the strict
`3456bf95` angular deadzone, and tests distance<=range (or force bit10000) plus
absolute heading error<=`3e4ccccd` (approximately0.2 radians). Forced range does
not bypass the heading gate. A perfect host bearing is not interchangeable:
for vector(.3125,0), the original computes bearing`3ba9540a`, approximately
0.005167489 radians. The original/C fixture covers this residual explicitly.

The read-only open-ground point witness records183 evaluations. Every source
is the next committed position, every stored position is the commit's previous
position, and the final commit integrates the previous velocity while publishing
zero new velocity. It stops short of the exact destination. A second owned run
repeats all183 evaluations/commits and182 ordinary motion decisions exactly.
`wc3_pathing_arrival.h` implements the predicate; `verify_wc3_pathing_arrival.py`
compares2342 complete original calls with raw C distance/error/range/result words.
The asset-free tests run this fixture twice at bothO0 andO2.

`move_point_arrival()` uses the existing engine frame interval to forecast the
previous velocity step. For active public Move/Smart heads with a waypoint goal,
it applies the fine-cell range and angular gate, validates the forecast pose,
commits that pose and publishes zero velocity. While in range but outside the
arrival heading tolerance it turns in place and retains the order. At completion
it uses the existing ability-arrival and stand/queued-order lifecycle. It no longer
snaps these point orders onto their goal. Existing internal approaches, active construction displacement, Patrol
and AttackMove retain their owning contracts until their producers are verified.

The four `wc3_pathfinding.point_move*` regressions drive actual order submission
and Move thinkers: nearby stop without snapping, a heading outside0.2 but inside
the propagation window, the final previous-velocity step, and saved velocity
with a queued successor. The first three failed against the earlier engine. Tests use the engine cadence; this does **not** certify retail
clock scheduling, complete world-to-fine coordinate conversion, earlier translation
phases, occupied-slot force/can't-path decisions or target-order arrival. These remain
NUM-02.3, TARGET-01.2/3 and the applicable FOOT admission tasks.

Evidence: `arrival-predicate-engine-final.json` and
`runtime/arrival-point-inputs-repeat-exact.json` under the report root. The new
`verify_wc3_arrival_trace.py` rejects missing/truncated inputs, publication and
commit records, wrong hashes, changed mover/force/range, altered raw results,
wrong predicted-pose pairing, missing final stop and a differing repeat.
The first capture (`arrival-point-inputs-first-260930.jsonl`) is explicitly rejected:
the original observer indexed the ninth stack slot instead of the eighth and
raised an access violation. It remains in the corpus with no certified evidence.
Use the verified/repeat captures, whose sources/maps are pinned in
`retail-point-arrival-inputs-1.27.json`.

```sh
python3 tools/frida/verify_wc3_arrival_trace.py \
  /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/arrival-point-inputs-verified-260930.jsonl \
  --compare /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/arrival-point-inputs-repeat-260930.jsonl \
  --fixture tools/ghidra/fixtures/retail-point-arrival-inputs-1.27.json \
  --engine-library /tmp/wc3-pathing-engine.so --report /tmp/point-arrival-repeat.json
```

Validation: Classic and TFT each pass41,926 assertions in2,193 cases; all115
pathfinding tool tests pass. Debug and release pathfinding each pass573 assertions
in77 cases, production WC3 builds, and both boundary audits are clean. The fresh
five-entry arrival corpus passes all expected outcomes, including the rejected
observer archive. Save remains60; no actor, network or serialized layout changes.


## Authored speed limits reach Move

MOVE-01.4 puts the ordinary nonhero speed producer into the engine. Public
`SetUnitMoveSpeed` now changes Move's runtime value, including explicit zero and
negative inputs. `GetUnitMoveSpeed`, step budgets and selection-group speed
selection share the profile/status consumer. `GetUnitDefaultMoveSpeed` reads the
immutable authored profile rather than the last setter. Movement-disabled units
ignore setters and return zero current speed.

The actual retail UnitMetaData defines `umvs`, `umis` and `umas` as integers.
Our typed UnitBalance stores scalars; map-object application previously discarded
integer modifications to those fields. An actual custom-unit CreateUnit/public
Move regression failed all three authored-profile assertions before the integer
to software-scalar conversion fix. The captured custom h001 clone now retains
speed237, minimum173 and maximum389 through normal map-row binding.

Original `5fc900` first clamps nonzero profile bounds to immutable1..522. Zero
profile bounds select Misc defaults unchanged; it clamps the effective value
against minimum, then maximum. Retail Misc supplies unit150..400 and
building25..400, with map overrides remaining authoritative. Missing data retains
the instruction-verified startup defaults1/522 and logs the missing key. These
are distinct from the game owner's additional bridge cap, observed as raw
`4402aaab`; its producer and exceptional composition remain open.

`verify_wc3_pathing_speed_limits.py` executes1820 original ordinary-tail cases
plus26 complete disabled getter gates. The production C header matches every
output word, including adjacent thresholds, zero/negative/inverted profile
bounds and defaults above522. Asset-free tests replay the frozen1846 cases twice
against both O0 and O2 builds.

Two owned75-second captures, `runtime/speed-inputs-first-260930.jsonl` and
`runtime/speed-inputs-repeat-260930.jsonl`, preserve source/map hashes and agree
on120 public native calls,26 speed publications,152 motion decisions and153
velocity/position/facing commits. Stock Footman setters clamp150..400 while its
default remains270; custom h001 clamps173..389 with default237; halt stays0 and
publishes no speed. The high setter reaches the moving owner's next committed
cap. **The low restoration occurs after arrival**, so it does not establish
low-cap integration or temporary-modifier restoration during travel.

`verify_wc3_speed_inputs.py` checks actor/mover identity and epoch, native
producer sequence, all bounds/default words, actual bridge division32, the high
setter's next commit, complete300 JASS samples and repeated raw commit digests.
Damaged-source, missing-record, changed-bound/default/epoch and wrong travel-cap
controls are rejected. Fixtures are `retail-speed-limits-1.27.json` and
`retail-public-speed-inputs-1.27.json`; the fresh five-entry corpus is
`speed-inputs-corpus-fresh-260930/corpus-results.json` under the local report root.

Actual engine regressions cover integer custom-profile binding, low/zero/high
public setters during Move, disabled-owner no-op, immutable defaults and saved
explicit-zero motion. Eight post-load steps reproduce position and velocity
words exactly. The saved bit uses the existing primitive override mask; save60,
JSVM7, actor/network layouts and field tables remain unchanged. The two existing
sub-bound velocity-guard tests now explicitly supply a small Misc minimum;
ordinary inputs correctly encounter the newly implemented bound first.

BASE-02.5 adds the missing stable UnitData map-row cache. The disabled custom
unit regression first failed because umvt never reached the created entity;
integer balance fields alone could not fix it. Original edits now serve as
custom inheritance sources, distinct rows survive further lookups, and map
cleanup releases them before rebinding stock data. Actual public CreateUnit
profiles exercise amphibious, floating and flying masks plus inherited turn
rate/window. This also makes authored custom-map movement settings reach normal
path queries instead of silently resolving the base unit row. Other unported
typed tables and full support transitions remain open.

Ghidra now persists380 descriptive functions,119 verified prototypes,143 fields
and43
scalar globals plus the registry pointer. The new native/profile/getter/startup
functions and separate immutable/default bound labels are saved and read back in
`speed-types-readback.json`. Upstream effect-stack ordering, hero-specific default
speed, special ability/type caps, immediate old-velocity integration/clamp on a low cap and
whole engine clock cadence remain MOVE-01.1/2 and NUM-02.3.


Validation: full Classic and TFT suites each pass42,005 assertions in2,197 cases,
with all118 pathfinding tool tests passing. Forced debug and ordinary movement
builds each pass2,811 assertions in167 cases per schema. The five new corpus
entries pass their declared outcomes; boundary/menu audits and relative links
are clean. Production WC3 builds. No whole-trajectory fidelity claim is added.


## Speed drops clamp existing velocity immediately

MOVE-01.5 makes public `SetUnitMoveSpeed` update the existing velocity before
the next Move think. An actual public CreateUnit/Move/setter regression first
retained the old roughly400-unit velocity after lowering the cap to150. Move
now uses the verified scalar normalization in fine coordinates, scales back
to world coordinates and leaves pose and facing intact. Raising the cap
preserves the current vector. Save/load reproduces the next eight position
and velocity samples word for word without adding serialized fields.

Original `15ff40` stores the fine cap, compares it strictly against the
software square root of the old vector and calls `15f7e0` with a zero delta
and no facing notification only when that length exceeds the cap. This
integrates old velocity through `1603d0`, then uses `1606e0/15fc70` to clamp
and update the fine object's moving bit. Original `071570` computes inverse
square root through the existing square-root/reciprocal helpers. The tiny
vector guard and small numerical overshoot from the original normalization
remain intact. Higher caps do not integrate or reset the vector.

`verify_wc3_pathing_speed_caps.py` executes the complete original function
with real constructed spatial maps, clocks and existing movers, without
function stubs. All756 cases match production C scalar composition, including
444 entering old-velocity integration. Signed and oblique vectors, zero/tiny
caps, both owner-clock domains, epoch changes and nonzero elapsed intervals
are covered. O0/O2 tests replay frozen outputs twice and independently verify
the world-coordinate adapter. This does not establish engine owner-clock
production or retail spatial-cache lifecycle parity.

Two owned75-second captures, `runtime/speed-drop-first-260930.jsonl` and
`runtime/speed-drop-repeat-260930.jsonl`, raise the stock Footman to400 at
tick20 and lower it to150 at tick30, both during travel. They agree on six
public calls, two publications, two cap transitions,127 motion decisions
and128 velocity/position/facing commits. The low transition has exactly
zero elapsed time; raw velocity changes from `[31c80000,4147ffff]` to
`[31160009,40960008]`, with pose/facing unchanged. The next normal commit
consumes that clamped vector. This closes the bounded immediate clamp;
temporary-effect restoration and nonzero-elapsed engine timing remain
MOVE-01.2 and NUM-02.3.

The strict verifier pins sources/maps, admission/completion,300 samples,
actor identity and immutable defaults, rejects altered clocks and unconsumed
vectors, and repeats all three raw digests. Fixtures are
`retail-speed-cap-transition-1.27.json` and
`retail-public-speed-drop-1.27.json`. The fresh five-entry checkpoint is
`speed-cap-corpus-final-261001/corpus-results.json` under the local report root.

Ghidra persists383 named functions,23 partial layouts,143 verified fields
and122 explicit prototypes; `speed-cap-types-readback.json` records the
saved cap, normalization and reciprocal-square-root ABIs. Existing scalar
globals and registry labels remain separate. Save60, JSVM7 and actor/network
layouts are unchanged.

Validation: full Classic and TFT suites each pass42,055 assertions in2,198
cases; all120 pathfinding tool tests pass. Forced debug and ordinary movement
builds each pass2,861 assertions in168 cases per schema. The fresh five-entry
corpus passes, and both boundary audits are clean.


## Flat bonuses retain their publication state

MOVE-01.6 registers and implements concrete `CAbilityMoveSpeedBonus` (`AIms`).
Its typed contribution query reads DataA through the actual authored rawcode.
The query visits every native owner and usable carried item, retaining
`max(0, contributions)`. Different aliases, reversed dominance, negative
values and duplicate Boots do not turn that maximum into a sum. Backpack
permission excludes carried item effects while preserving native owners.
Move adds the flat maximum with retail software arithmetic before the existing
status multipliers and authored speed limits. Upstream multiplier composition
is still outside this bounded port.

The public speed getter and an existing mover's cap are separate retail states.
Two repeated quiet captures show the Hpal getter changing270→330 with Boots,
staying330 with two copies, and returning270 when both are removed. Every
actual velocity commit still uses the original270 cap. A second repeated scene
shows `SetUnitMoveSpeed(270)` publishing330 with Boots equipped, a new Move
order publishing330 again, removal changing the getter back to270 while the
committed cap remains330, and a following setter restoring270 with the verified
immediate vector clamp. Engine Move now retains the last published flat bonus
and refreshes it on accepted Move orders and public setters. Inventory queries
remain current; inventory changes alone do not rewrite that saved Move state.

These captures use a RoC-format map with the active TFT AIms row. Extracted
original `war3.mpq` AbilityData supplies `Data11=40`; `War3x.mpq` supplies
`DataA1=60`, matching the actual contribution field and public outputs in this
run. Stock bspd's ItemData attaches AIms. The engine reads the table and schema,
including aliases, instead of hardcoding either40 or60. The source-table
hashes and selected row values are retained in
`retail-public-item-speed-1.27.json`. The original writer of ability+88 and
complete campaign/game-mode table selection remain required work.

The complete original speed oracle now compares126 attached-bonus compositions
with production C, preserving max-before-add-before-multiply-before-clamp order.
Frozen outputs replay twice at O0/O2. The quiet scene repeats ten public calls,
five compositions,101 decisions and102 velocity/position/facing commits. The
published scene repeats18 public calls,12 compositions,91 decisions and92
commits, including four cap transitions and three bridge publications. All
three raw digests match between each pair. The strict verifier rejects altered
source, maximum, attached identity, defaults and caps, including a cap change
without an observed publication. Broader effects and engine owner clocks are
not inferred from these records.

Actual public CreateUnit/CreateItem/UnitAddItem/UnitRemoveItem/Move/setter
regressions check both queried speed and physical steps. The first engine
fixture omitted the item's model field; it was corrected before accepting
evidence. A valid-fixture absence control then reproduces the missing-bonus
failures; restoring the implementation passes56 assertions. Additional alias
and transport tests pass12 assertions. Saving after the last Boots removal
retains the previously published60 contribution and reproduces eight following
position/velocity samples word for word. A later setter clears it and clamps
the vector. This private movement field advances W3SV to61, with exact-version
rejection covering60; JSVM remains7 and shared network contracts are unchanged.

Ghidra persists385 descriptive functions,24 partial layouts,144 verified fields
and124 explicit prototypes. The original maximum-list traversal, AIms getter
and typed bonus88 field are saved and read back in
`item-speed-types-readback.json`. The fresh seven-entry checkpoint is
`item-speed-corpus-fresh-261001/corpus-results.json` under the local report root.

Validation: Classic and TFT each pass42,128 assertions in2,200 cases, and all
122 pathfinding tool tests pass. Forced debug and ordinary movement checks
each pass2,929 assertions in170 cases per schema. The seven fresh corpus
entries pass, the concrete ability registry audit runs, and both boundary
audits and relative links are clean.


## Ranked formation layout reaches group orders

`wc3_pathing_formation.h` ports the complete original `16a5b0` geometry for
one through twelve members: authored rank buckets, the one/multiple-rank
capacity tables and radius threshold1.5, common row width, strict backwards
selection-sort ties, row/rank spacing, member-order mean and paired-trig
rotation. Every scalar operation uses the verified software arithmetic.
The supplied positions are fine-grid predicted positions; the pure helper
leaves clock ownership to its caller.

`verify_wc3_pathing_motion.py` now compares865 complete original layouts with
production C raw offset words. The144 existing uniform layouts remain checked
against their independent model. Another720 cover mixed radii .25/.5/1.5/2,
one/multiple ranks, four headings, unique/equal/nearly equal projections,
stationary/moving members, both owner-clock domains and differing epochs,
counts2/3/5/9/12, and both group bit20 states. Both spacing-global sets are
initialized to the same2/2.5/5.5 values in this binary. The final case freezes
the actual engine regression inputs. Repository fixture
`retail-formation-layout-1.27.json` retains inputs and raw outputs; O0/O2
production probes repeat every comparison and verify input immutability.
The thirteen-member probe rejects before reading member data or changing
output slots; this is an engine guard, not a claim about retail's larger domain.

`move_selectlocation` now feeds current committed fine positions, collision
radii and typed `UnitData.formationRank` into that layout. It assigns the exact
software-scaled offsets to both immediate orders and Shift-queued orders,
then applies the existing static destination admission and reservation policy.
The actual regression selects three radius16 units at(-640,-200),(-640,0),
(-640,200), with ranks0/0/1, and moves east to(1000,0). The old source-offset
heuristic fails all six target coordinates. Retail places the first two in a
front row at raw X44845555, Yc27fffff/427fffff; the rear member receives
X445caaaa,Y00000000. The regression asserts all target words, shared route
ownership, queued target words and unchanged active targets after Shift. All three
members advance toward their assigned rows, and eight further ticks after
save/load reproduce all96 pose/velocity words and retain the target words.

The engine currently derives its initial heading from selection mean to goal.
Original group heading production, clock prediction during submission,
subsequent refresh/held-member classification, and narrow-passage regrouping
remain FORM-01/03/04/05. Selections above twelve retain the prior source-offset
engine policy with an explicit bounded diagnostic, pending the original
caller precondition in FORM-02.3. This integrates proven geometry while keeping
those remaining contracts visible. No private entity or save layout changes.

Ghidra persists the five refined layout function comments, descriptive names
for rank-row layout, row sorting and mean calculation, and the partial
`WC3FormationRankBucket` a4-byte structure: count0, diameter94, row width98,
integer capacity9c and scalar capacitya0. The twelve-entry index and projected
position arrays remain unnamed gaps in this partial schema. Readback is
`formation-types-readback.json`; fresh original and production comparisons
are `formation-corpus-final-261001/corpus-results.json`. The preliminary
fresh run rejected missing comparison-count report fields; that reporting
omission was corrected before accepting the final run.


Validation: the complete Classic and TFT suites each pass42,267 assertions in
2,201 cases, with123 pathfinding tool tests. Forced debug and optimized release
movement checks each pass3,068 assertions in171 cases, including the139-assertion formation/queue/
resume regression. Ghidra readback now has25 layouts,149 fields,124 explicit
prototypes and43 scalar globals plus the original native registry. Boundary
and documentation-link audits are clean.


## Direct world coordinates preserve boundary cells

`wc3_pathing_coordinates.h` applies the original scalar subtraction and direct
cell scaling, and the separate multiply/add inverse. Game-owned routing no
longer divides a point by the full map extent and multiplies it by the cell
count. On a96×96 WPM with32-unit cells, world X435fffff (223.9999847,
just before224) belongs to cell6. The old adapter rounded the normalized
coordinate to7 and rejected the point against a wall in cell7; destination
correction also replaced its world word. The reverse adapter changed a legal
cell centre112 into111.999992 on a23-cell map.

The actual engine regression covers before/equal/after that wall boundary,
unchanged legal destination words, a one-ulp rectangle on the open side,
a real Move order advancing left, and correction to the only open cell(3,4)
in a23×23 map. Temporarily restoring the actual previous `g_world.c` reproduces
five assertion failures; the direct adapter passes all15. Investigation traces
and temporary production edits are removed.

The direct transform now serves endpoint checks, corrected cell centres,
idle object positions, segment inputs/pruning bounds, fine-search endpoints,
reconstructed waypoints and game-owned rectangle goals. Formation input
coordinates share the scalar helper. Cell dimensions still come from the
engine's authoritative world extents/pathmap dimensions; stock WC3 produces32.
Shared long-field traversal and nearest-ring search remain the engine policy.
No server/client API, private entity or save layout changes.

`verify_wc3_pathing_maps.py` executes576 additional complete original04d870
set/clear boundary pairs, covering23/96/160/288 square maps, three positive/
negative origins, both axes, and predecessor/exact/successor words around
zero, interior and final boundaries. A read-only hook at original070c80 records
the exact fine words. Original floor/integer conversion and actual054000
cell/null admission are asserted; low occupancy bits and all adaptive words
remain unchanged. The inverse separately executes original06f9c0 multiplication
by32 and06fbb0 addition of origin. The production probe matches all six raw
fine/cell/inverse outputs, with immutable inputs, twice at O0/O2.

`retail-world-grid-boundaries-1.27.json` freezes that matrix.
`world-grid-corpus-final-261001/corpus-results.json` freshly reproduces the
strengthened original map oracle and its new production variant. The original
11,664 edits,216 rebuild/reversals and30 clipped updates still pass unchanged.
The original inline divide-by32 uses an exponent adjustment after software
subtraction; the tested helper preserves its boundary words. Public metadata
construction, complete domain rejection, proximity/adaptive padding and all
corner producers remain MAP-01.2/3. Ghidra saves this bounded contract on04d870.


Validation: complete Classic/TFT suites each pass42,282 assertions in2,202
cases; all124 pathfinding tool tests pass. The first full run exposed the
inventory's old76-oracle assertion after adding the77th comparison; the count
was updated before accepting the final run. Boundary/link audits are clean.

Forced debug and optimized release routing runs each pass588 assertions in78
cases per schema. The production build succeeds with the same game-owned
conversion; no debug traces remain in the implementation.

## Retained fine pose reaches Move

`1603d0` updates native fine position at mover`78/7c`, using the velocity and
elapsed scalar before updating both spatial rectangles. Publishing world
coordinates multiplies by32 and adds the map origin with separate software
operations. Integrating directly in world coordinates changes the committed
words on nonzero origins. Reprojecting published coordinates each frame can
also discard fine-pose bits.

`verify_wc3_pathing_motion.py` now executes18 retained-pose sequences with16
commits each: three initial positions, origins `(0,0)`, `(-256,-256)` and
`(-2048,512)`, and constant/alternating oblique headings. Every interval uses a
controlled zero-elapsed `16fe20` velocity publication followed by complete
`1603d0` integration with supplied elapsed0.1. Spatial records and pose remain
live between steps. Original `06f9c0` and `06fbb0` compose the world inverse.
All288 fine/world results match production C;139 world results differ from the
old direct-world integration. The frozen
`tools/ghidra/fixtures/retail-native-pose-1.27.json` is compared twice at O0/O2.
The primary report is `native-pose-original-engine-first-261001.json`.

Move retains `movement.fine_pose`, previews candidate integration without
mutating it, and commits it only after collision admission. Explicit world
commits invalidate it. A changed published world axis is reprojected on the
next preview while an unchanged axis retains its fine bits. Save format62
persists the fine position and validity; no network struct changes are needed.
The actual16-step Move regression failed all32 world position assertions
before the port, while velocity/facing already matched. A positive-origin
case covers fine/world/velocity/facing words, eight-step save/reload, blocked
candidate preservation, changed-world repositioning and explicit snap
invalidation. Natural point arrival also consumes the retained pose before
publishing zero velocity; its previous forecast failed four of ten assertions.
Arrival geometry uses the map cell size: stock WPM32 retains the original
fine words, while one-unit synthetic routing maps keep their authored scale.

This is a numerical integration payoff with supplied elapsed time. The engine
still uses its existing frame cadence and new-velocity update phase;
NUM-02.3 owns original public clock production and old-velocity phase parity.
Public axis-setter geometry is now independently covered by BASE-01.5 below;
other forced-position producers remain BASE-01.4.
Ghidra's saved integration annotation records these boundaries.

Fresh `native-pose-corpus-final-261001` motion/original-C entries both pass.

Validation: complete RoC and TFT suites each pass42,599 assertions in2206
cases, plus125 pathfinding tool tests; forced release/native comparisons are
recorded beside the primary report. The production release build passes.
A separate warning cleanup corrects an existing six-anchor HUD array copy and
bounded string copies; its serializer regression preserves both axis arrays
and frame references.


## Public axis-position writes retain the next Move step

Retail `SetUnitX/Y` obtains the predicted world pair, replaces the requested
axis, then writes a delta through the native fine pose. Both fine axes are
reprojected, even when the public coordinate word is unchanged. A direct
world-axis assignment preserves hidden bits that retail deliberately changes;
the next Move step can then differ in fine and world coordinates.

The registered cdecl natives are `204100/204140` getters and `215900/215960`
setters. Unit virtual slotb8 returns the mover bridge. `058900` delegates the
read-only predicted query to `05a970`, using `161040` elapsed and `05bdd0`
fine prediction, then software multiply32/add map origin. `058810` calls
`05c200` with notification1. The writer subtracts origin/divides32, subtracts
the predicted fine pose, then `15f7b0` integrates old velocity plus that delta.
The setter leaves velocity, published cap, facing and the current Move intact.
`SetUnitPosition` at `2155c0` has a distinct Stop/placement path and is excluded.

The complete original-code matrix executes576 writes over18 retained-pose
sources, two axes, four input modes and four clock cases: zero/nonzero elapsed,
primary wrap and secondary domain. Every write is followed by an actual
original Move publication/integration, retaining its spatial and fine state.
All576 writes and576 next-step results match production C. The frozen
`retail-axis-position-1.27.json` checks every word twice at O0/O2.
Primary report: `axis-position-original-engine-next-261001.json`.

Owned scene36 repeats eight public same-word/fractional setters during Move
and after Stop:40 native calls,40 predicted queries and eight writes per
capture. Queries leave mover state unchanged; all writes preserve the velocity,
cap and facing words. C also matches the captured original elapsed time and
all167 movement decisions/velocity commits. Strict verification checks public
getter results, actor identity, case order, notification, completion, source
hashes and all300 samples; damaged/absent inputs are rejected. Captures
`runtime/axis-position-first-261001.jsonl` and `axis-position-repeat-261001.jsonl`
have identical normalized producer digest
`4686d7cec6b781cadc351bb39fa965e6215d172a520cfcd08e2554dee74ad5c6`.
The accepted report is `runtime/axis-position-original-C-repeat-261001.json`.

The initial OpenRealm axis-setter port used Move's `S_SetUnitAxisPosition`
with committed-pose geometry. The [primary clock port](#primary-clock-reaches-move-and-predicted-positions)
below extends the same verified delta/reprojection to current prediction.
Position linking, fog invalidation and position-change events remain in their
normal order; worker avoidance state, current order and goal are preserved.
The actual JASS/Move regression first failed32 coordinate assertions, then
passes128 checks including the next oblique step before/after save/load.
Save62 already owns the fine pose; no new saved or network fields are needed.

This initial port closed geometry at the engine's committed-pose boundary.
The following primary clock section extends getters/publication between callbacks;
the complete original route owner remains NUM-02.3. The retail captures show accumulated clock spans and varying
elapsed intervals; they do not justify substituting a guessed fixed32Hz step.
Ghidra saves397 names,133 explicit prototypes and the existing25 layouts/149
fields, including each native/bridge ABI and these limits.

Fresh strict outcomes are in `axis-position-corpus-final-261001`: both motion
oracles, both capture audits and the repeated public contract.

Validation: debug and forced optimized RoC/TFT suites each pass42,727
assertions in2207 cases;127 pathfinding tool tests pass. All five fresh
strict corpus entries pass, and Ghidra readback reports the nine new explicit
prototypes with no unsaved changes.

## Primary clock reaches Move and predicted positions

OpenRealm now schedules point Move on the measured retail owner cadence. The
private gameplay clock advances by the scalar word `3ba3d70a` (.005 seconds),
and Move runs after each six advances, during timer dispatch before the next
advance. The engine still emits its ordinary100ms snapshots. An accepted Move
first integrates the previous velocity from its last clock origin, then requests
the new velocity/facing. Between callbacks, publication predicts from the
retained fine pose without consuming that origin. A repeated .03 addition or a
new-velocity100ms step produces different coordinate words.

Owned scene37 (`clock_oblique`) supplies a quiet Footman Move from(-1936,-976)
to(-1600,-144). Both75-second captures contain6000 completed primary advances,
1000 owner callbacks,205 motion decisions,206 velocity/facing/position commits
and300 public samples. The source word, span300 and flags4096 remain fixed.
Owner callbacks see the sixth completed clock value before the seventh advance.
The final source call includes the1000th owner callback and scenario completion;
its advance observer is disabled by completion, but its source-end row still
records the actual next clock. This boundary is checked explicitly.

The primary sequence digest is
`c74157bbda9aef31e4c3318fa5978575f25be617420529584a2f7f63e19055ae`.
`verify_wc3_primary_clock.py` checks source/owner/advance order, actual C clock
words, source hashes, admission, all300 samples and repeated movement commits.
It excludes the presentation clock: host intervals and interleaving vary between
runs. That variation must not be described as primary nondeterminism.
Captures live in `runtime/clock-oblique-{first,repeat}-261001.jsonl`; the accepted
live report is `runtime/primary-clock-original-C-repeat-261001.json`.

The original-code oracle separately executes the complete primary producer with
empty original timer/request heaps, preserving the actual fine/spatial mover.
It supplies the observed5ms input and a controlled constant .125 heading every
six advances. It freezes6000 clock values,1000 old-velocity commits and300 world
queries in `retail-primary-clock-trajectory-1.27.json`. Another1944 original
controls cover paused flags, negative/zero increments, span boundaries, strict
residual cancellation and epoch overflow. Production C matches every word twice
at O0/O2. This isolates the clock and movement contracts; it does not emulate the
complete original route owner by substituting engine routes into the oracle.

### Recovered producer and persistent annotations

| RVA | Recovered role and ABI |
| --- | --- |
| `36aba0` | Registered primary event callback; adds14 to ECX and calls04c1a0; returns1. |
| `04c1a0` | Primary source, ECX scalar pointer; timer, gameplay request, auxiliary callback and unit clocks advance in order. |
| `04c0d0` | Presentation source, ECX scalar pointer; subdivides at global299. This is not the primary quantum. |
| `001e70` | Initializes `Simulation_PresentationAdvanceMaximum` atd3c844. |
| `054050` | Timer clock advance, ECX increment pointer/EDX clock. |
| `054190` | Gameplay request-clock advance, same fastcall ABI; flags bit1 pauses. |
| `0540f0` / `054230` | Auxiliary callback/unit-clock advances. |
| `0521f0` | Request deadline rebase; subtracts span from heap deadlines and increments epoch once. |
| `0522e0` | Timer drain temporarily publishes each due deadline during callback dispatch. |
| `04da30` | Loads the original path owner and tail-calls15aa80. |

All four advances perform software addition and a single epoch rebase. Residual
cancellation uses a strict comparison against `3556bf95`, distinct from elapsed
movement's `38d1b717` deadzone. The80-byte `WC3SimulationClockPrefix` names only
verified time40/epoch44/span48/flags4c fields. Timer/request containers remain
opaque. Ghidra saves408 names,144 explicit prototypes,26 layouts/153 fields and
44 scalar globals plus the ability registry. `primary-clock-ghidra-readback-261001.json`
records the applied types; the event producer's broader registration/scheduling
chain remains open.

### Engine lifecycle and evidence limits

Move owns pose prediction, native axis writes, speed-cap integration and pause
state. A higher speed cap does not consume the old pose origin; only a cap below
current velocity commits elapsed motion before clamping. Public axis writes now
consume the actual primary-clock prediction. A paused or stunned mover freezes
its last exact sampled fine pose, avoiding both continued prediction and a jump
on resume. Leaving Move consumes its current pose and clears prediction velocity.

Generic move callbacks select scheduled think, pose sampling and leave behavior.
The generic dispatcher prevents a newly activated queued Patrol from also taking
a snapshot-sized movement step in the same frame. Region comparison positions
are sampled before scheduled movement, so a Move crossing still reaches its
JASS entry action. Other ability owners and animations retain their existing
snapshot cadence; their retail timing has not been measured by this scene.

Save63 persists primary time/epoch/span,5ms cursor, six-step phase, pending owner
request, retained/predicted fine pose and mover time origin. Transient dispatch
flags and per-frame callback stamps are cleared on load. Save62 and older layouts
are rejected. The production point-order test saves at all three snapshot phases
and compares six subsequent frames word for word after restoration; pause,
resume, stun, Stop and region entry are exercised through the real owner/VM.
A test fixture originally retained trigger pointers after closing its earlier
VMs; the full-suite backtrace isolated that stale registry, and each independent
phase now retires its trigger/event/region registries before VM replacement.

The engine's fixed-input regression originally failed128 of224 coordinate
checks while all96 velocity/facing checks passed. With the port it drives the
real server-frame dispatcher over the full30-second original controlled
trajectory, comparing each fine/world/velocity/facing word. This proves the
measured movement kernel and publication phase. Adaptive waypoint selection,
local avoidance, group refresh, retry/task timing, other ability clocks and
forced `SetUnitPosition` placement remain separate open tasks.

Fresh strict outcomes are in `primary-clock-corpus-accepted-261001`: both motion
oracles, both clock archive audits and the repeated clock/movement contract.
The earlier corpus run used the system Python without Unicorn and is retained
as a failed environment run. Use `/GitHub/wc3-analysis/verify-venv/bin/python`
for executable retail oracles.

Validation: the required `make -j8 test` passes; debug and forced optimized
RoC/TFT suites each pass45,084 assertions in2209 cases, including2101 fixed
trajectory and255 public lifecycle assertions. All129 pathfinding tool tests,
all five fresh strict corpus entries, engine/menu boundary audits and the
production release build pass. Ghidra readback and explicit program save succeed.


## Target identity exits reach the engine

The fine search now stops at the original current node when a perimeter query
observes the requested target object. Suppressing that object from collision
checks does not remove its identity. The search samples the complete perimeter
and creates every legal neighbor before consuming the hit; it returns without
relaxing those neighbors or closing the current node. This matters for exact
work, node counts and later parent-chain reconstruction.

`verify_wc3_pathing_targets.py` executes2,304 original core searches and complete
setup/search/reconstruction requests: a single-cell target or full-height wall,
open or blocked target terrain, four movement masks, four footprints, budgets
0/5/700 and twelve object roles. Self/suppressed, target/both, moving, inactive,
unlinked and off-lane records are included. Two overlapping chains establish
that an earlier foreign blocker hides the target, while a target preceding the
blocker still reports identity even when the cell returns false. Terrain rejects
before any identity query. There are200 non-goal target completions; production
C matches cost, work, created nodes and full parent chains twice at O0/O2 with
reuse. The frozen fixture is `retail-fine-targets-1.27.json`.

The engine's `G_FindUnitMovePathWaypoint` now carries separate active ground-unit
target bounds through the same perimeter observer. Classes0/1 stop at
`(11.5,11.5)` and classes2/3 at `(10.5,10.5)` in the controlled request from
`(4.25,4.75)` toward `(19.25,19.75)` with target at `(12.25,12.25)`. These are
fine-grid fixture coordinates, not a retail public-order replay. The regression
failed16 coordinate checks before the adapter port; all48 assertions now pass,
including moving targets, no-pathing category rejection and retained fractional
point destinations. A target-completed route retains its approach-node centre;
only a route that actually reaches the requested goal substitutes its original
world words. No persistent or network state changes.

The sorted ground-rectangle snapshot still evaluates foreign blockers before
target identity. It does not preserve retail's overlapping link chronology.
FINE-01.6 explicitly owns that producer and public-order composition; building,
destructable and complete category publication remain BASE-02/FOOT-04. The exact
supplied-chain C matrix must not be presented as proof that these runtime
producers already match.

Ghidra persists the216-byte `WC3FineSearchPrefix` with six verified fields:
size classa0, maska4, targeta8, target-seencc, obstructiond0 and endpoint-moded4.
The occupancy, expansion and search functions have explicit operand prototypes
and durable comments for the exit timing. The saved readback is
`fine-target-ghidra-readback-261001.json` (27 layouts/159 fields,147 prototypes).
The corpus now has197 entries and68 frozen fixtures; accepted fresh original/C
results are in `fine-target-corpus-accepted-261001`.


Validation uses the [native SDL2 headless runtime](../../build-and-renderer-platforms.md#headless-sdl-input-regression-runtime)
because the installed SDL2 compatibility layer crashes on the unchanged synthetic
text-input fixture, independently of pathfinding. The required full suite passes;
RoC/TFT each pass45,132 assertions in2,210 cases. All130 pathfinding tool tests,
both accepted fresh target corpus entries and engine/menu boundary audits pass.


## Forced-position Stop reaches the engine

Public `SetUnitPosition`2155c0 resolves the unit and calls6803f0(1) before
vtable180 admits placement. `SetUnitPositionLoc`215620 resolves the location,
reads24/28 and delegates to2155c0. These cdecl natives have plain RET;
6803f0 uses ECX Unit, stack4 flags and RET4. The nested Stop admission clears
task/order/group identities, integrates old velocity at the current clock,
then zeros velocity while retaining cap/facing and the allocated path.

The owned `forced_position` scene38 brackets moving same-position, moving
fractional, idle same-position and Patrol fractional writes. Two75-second
captures contain300 samples,28 public Get/Set calls,60 position queries,
four Stop pairs and four placement commits, with no observer errors.
`verify_wc3_forced_position.py` checks native/query result relationships,
event ordering, actor and bridge identity, captured clocks, scalar outputs,
retired public orders and stationary samples after placement. All99 preceding
motion and velocity commits also match production C. The position digest is
`490e48e8f1e2c8c446bbda6cf536dbac780e4fcca19d8e33feed49a884c35917`.
The fixture `retail-forced-position-1.27.json` retains the actual observations;
O0/O2 replay and thirteen altered/truncated controls verify rejection.

Both public engine natives now delegate to Move-owned `S_SetUnitPosition`.
It replaces the active order, clears queued/group routing state and velocity,
admits placement through the existing legality adapter, and writes through
`wc3_grid_place`. This preserves the original fine reprojection and scalar
delta operation: a distant placement can differ from the requested world
word by one ULP. Region crossings, linking and fog invalidation retain their
normal publication paths. Stop's stand transition is optional, as it already
is in CreateUnit, for units without an installed movement lifecycle; existing
public blocked-placement/region tests reproduced the null-callback crash.

The actual public regressions reproduced57 failed assertions before the port.
Five Move/Patrol/paused/Loc modes now clear orders, FIFO and group state and
remain stationary after save/load. Four captured native before-Stop clocks,
fine poses and velocities produce exact original fine/world/time words through
public JASS writes. Together these two regressions pass167 assertions. They
prove supplied captured placement state, not the complete retail route owner.
Save format63 and network layouts are unchanged.

Ghidra saves411 descriptive names and150 explicit prototypes; existing27
layouts/159 fields and44 globals remain unchanged. Readback is
`runtime/forced-position-ghidra-readback-261001.json`. All three fresh corpus
entries pass in `forced-position-corpus-accepted-261001`.

Blocked/overlapping placement legality still uses the existing adapter and
remains FOOT-04. Gold-mine/cargo/dead actors, other forced writers and a live
SetUnitPositionLoc capture remain BASE-01.4. The Loc engine regression and
original wrapper disassembly do not claim that missing live capture.

Validation: required `make test` and forced release RoC/TFT each pass45,299
assertions in2,212 cases;131 pathfinding tool tests, production release build,
engine/menu boundary audits and all three strict corpus entries pass. Tests use
the [documented native SDL2 runtime](../../build-and-renderer-platforms.md#headless-sdl-input-regression-runtime).
No production C warnings are reported.


## Blocked placement reaches the engine

The ordinary CUnit vtable180 producer698050 enters653510, vtableDC67f490,
6515f0 and bridge058cd0. The bridge converts world coordinates to fine cells,
clips the permitted rectangle, and reaches owner16ecc0/fine14a1e0. Public
point placement uses policy2, limit32, radius31/32 for Footman, mask02000002,
and callback654060 with the requested support level. The callback truncates
fine coordinates and compares78bc90's terrain/bridge level to that context.
The terrain producer744040 reads the low four bits of a28-byte vertex record;
750100 selects `(cell+2)/4`, divided toward zero, rather than interpolation.

14a1e0 preserves a legal requested scalar pair. Otherwise it floors the
rectangle and expands half-open cell rings, counting the initial point in the
budget. 14b580 scans bottom/right/top/left, with explicit corner ownership and
first-success ordering. The accepted callback point contains integer scalars;
only afterward does ordinary output add one half to produce a cell centre.
Endpoint mode includes moving objects and restores the previous mode on every
return. This is different from the previous64-unit,300-candidate spiral.

`verify_wc3_pathing_placement.py` executes2,304 complete original calls over
1,152 cases: four masks/classes, open/single/square/sealed terrain, fractional
and negative points, integer/centre output, budgets1/5/32 and mode0/7 repeats.
Actual original scalar startup runs before negative-floor controls. Output
words, stack cleanup, endpoint mode and SEH restoration are checked. The
production C selector matches every endpoint; the compact frozen fixture also
retains original candidate counts/digests. This isolated matrix supplies a
null admission callback; it does not claim bridge or public wrapper parity.

Two owned scene39 captures each contain770 records without observer errors.
The actual34 terrain edits precede six public blocked destinations. Seven
searches repeat candidate order and footprint results; every observed search
accepts its first legal same-level footprint. `verify_wc3_placement_trace.py`
checks the original arguments, all observed footprint decisions against the
synthetic edited patch, raw C endpoints,78 predicted queries, six scalar writes
and33 motion/velocity commits. Public getter results and stationary samples
remain associated with the same resolved actor. A separate five-ring search
occurs inside Stop when the old point is embedded before the final32-ring
placement; its engine producer is explicitly FOOT-04.4.

Move's public position writer now uses `G_FindUnitPlacementPosition`. The game
adapter collects endpoint-eligible ground rectangles, including moving units,
and supplies terrain/object cells plus the authored terrain-level condition to
`wc3_fine_place`. All six captured public destinations initially differed in
both coordinate words; the actual JASS regression now passes26 assertions with
exact destinations. Existing public SetUnitPosition/Loc blocked-cell tests now
expect the retail cell centre272,240. CreateUnit and item drops retain their
separate placement producer, whose native parity remains open.

The initial regression installed the synthetic edited pathmap directly; the
[next integration](#terrain-pathing-natives-reach-the-engine) now drives these
same endpoints through actual JASS terrain edits. Bridge support-level
overlays, outside-map clipping, callback rejection controls and broader object
categories also remain open. The base authored terrain-level condition is
ported; no bridge-level approximation is claimed. Save63 and wire layouts are
unchanged.

Ghidra saves423 descriptive names and156 explicit prototypes. Two new partial
terrain prefixes bring readback to29 layouts/164 verified fields, with44 globals
unchanged, in `runtime/blocked-position-ghidra-readback-261001.json`. All four
fresh entries pass in `blocked-position-corpus-final-accepted-261001`.

Validation: required `make test` and forced release RoC/TFT each pass45,325
assertions in2,213 cases;133 pathfinding tool tests, production release build,
engine/menu boundary audits and all four strict corpus entries pass. Native
SDL2 is used as documented; production builds report no C warnings.

## Terrain pathing natives reach the engine

The engine previously discarded `SetTerrainPathable` and returned true for every
`IsTerrainPathable` query. These now read and edit the mutable fine terrain byte.
Despite its name, the retail query returns **blocked**, including outside-map
coordinates even when the enum selects an empty mask. `SetTerrainPathable`
inverts the passable boolean and changes only the selected bits of one32-unit
cell; it never re-derives the amphibious bit from no-walk/no-float edits.

Recovered public contracts are cdecl `205e90` (X/Y scalar pointers, pathingtype)
and `2148f0` (same plus passable). Cdecl `201630` returns only AL: enum0 selects
ff, enums1..7 select02/04/08/10/20/40/80, and other values select0. Query wrapper
`04e090` shifts this byte into the high word and delegates `04df50`, with the
low object-category mask zero. Thus public terrain queries ignore dynamic
objects and baked scenery footprints. Point conversion uses the established
software world-origin subtraction and direct32 scaling; mode and SEH restore
on both admitted and outside exits.

`verify_wc3_pathing_terrain_natives.py` executes complete public native chains,
without replacing imports: ten enums, thirteen seed bytes, both booleans and
four admitted/fractional/outside sources. All1,040 cases and3,130 original calls
retain unrelated cells, low occupancy words and endpoint mode. Frozen
`retail-terrain-natives-1.27.json` matches production scalar/flag helpers at O0
and O2 twice. Fresh `terrain-natives-corpus-accepted-261001` passes its strict
entry. Existing retail scene39 independently supplies34 actual terrain edits
and six resulting public placement endpoints.

Actual JASS engine tests reproduced61 failures across180 assertions before
the native port. The placement regression now builds its blocked patches
through JASS rather than replacing routing storage and retains all six exact
retail coordinate pairs. Further tests prove outside/empty-mask handling,
independent amphibious state, and terrain queries versus baked object blockers.
A live Move regression inserts a wall after the direct route is admitted,
proves the cached field is invalidated, follows15 legal detour steps, removes
the wall through the native and completes inside the recovered0.49-cell
arrival gate. Blight edits update the shared cell/dirty-row owner and terrain20 consistently.
Save64 stores mutable terrain before Blight and rebuilds static obstacles after
restoring entities. Its regression failed two assertions before persistence was
added; unrelated cells and all native bits now survive. Two movement save
fixtures now set the real spawned actor classification flag so rebuilding
static footprints cannot misclassify them as scenery.

Terrain mutation rebuilds the engine's existing baked obstacle map and
invalidates its legacy field cache. This is an explicit engine integration
policy; retail adaptive classification stays stale until its separate update
producer, whose exact scheduling remains MAP-03.3. No hierarchy or full edited
movement trajectory parity is claimed. Bridge levels and outside placement
clipping remain FOOT-04. Ghidra saves427 descriptive names,161 explicit
prototypes,29 partial layouts/164 fields and44 globals; readback is
`runtime/terrain-native-ghidra-readback-261001.json`.

Validation: required `make test` and forced release RoC/TFT each pass45,561
assertions in2,217 cases;134 pathfinding tool tests, fresh original-native
corpus entry, production release build and engine/menu boundary audits pass.
Native SDL2 is used as documented; production builds report no C warnings.

## Deterministic owner random state reaches public natives

Engine payoff17 replaces libc draws in actual `GetRandomInt`/`GetRandomReal`
with `wc3_pathing_random.h`, the same two-word state and arithmetic used by
retail overlap directions. `level.pathing_random` persists through Save65;
loading older formats is rejected before reconstruction. `SetRandomSeed`
seeds that owner then consumes one draw, matching214140's prefix. The full
693710 tail reseeds45 separate unit streams; those streams remain unported.
Its legacy `srand` side effect remains explicitly marked until those consumers
are replaced. This does not claim a complete global gameplay draw order.

`verify_wc3_pathing_random.py` executes1,408 complete original PRNG/public
integer/real/direction calls across11 seeds and11 public seed prefixes,
checking ABI, raw result words and both state words against production C.
The original scalar-table initializers run; no platform/TLS import is stubbed.
The frozen `retail-pathfinding-random-1.27.json` stores only derived words.
`random-owner-corpus-accepted-261001` independently compiles the engine probe and runs
this scope. `runtime/random-owner-ghidra-readback-261001.json` records saved
function prototypes and the two-word state structure.

Equal integer bounds and scalar differences below `3456bf95` consume no draw.
Integer draws use unsigned inclusive span and multiply-high; full signed-range
span wraps tozero but still consumes a draw. Both reversed-bound natives keep
the first argument as the result anchor. Real fractions use the low23 bits
with retail software scalar arithmetic. Exact overlap directions consume one
owner draw and the existing retail sine/cosine table.

The engine tests invoke compiled public JASS:550 assertions compare11 seeded
native query sequences to original words, plus save/load continuation and
range/reseed controls. The original engine crashed on full-width integer
bounds and failed both saved continuation words. Clear the hashtable registry
between seeded fixture programs: `reset_entities` does not retire old tables;
reading slot0 without clearing it silently tests the previous seed's output.
Default map seed production, separate per-unit reseeding and movement-wide
consumer order remain NUM-04.1/5/6. Repulsion scheduling and actual overlap
movement remain SEP-02.3; helper parity alone does not complete them.

Scene40 `random_owner` observes complete public seeding and48 interleaved
queries per seed:539 native returns, owner-before/after words and raw query
results match the frozen original/C oracle in both completed captures.
`runtime/random-owner-loaded-first-261001.jsonl` and its repeat are accepted;
`verify_wc3_random_trace.py` rejects damaged words or missing scheduled
completion. The full public seed tail returns without changing the verified
owner words, although its separate unit streams are outside this assertion.
The initial `random-owner-first-261001.jsonl` has no timer samples and is
unaccepted: its10s loading key preceded initialization at21s. The controlled
captures use35s/85s timing and both reach tick300. Observer/controller sources
are frozen under `runtime/random-owner-sources-261001/`.

Validation: `make test`, forced release RoC/TFT engine suites and the release
binary build; original/C comparisons run twice atO0/O2. The full engine suites
now contain46,022 assertions across2,219 cases per mode. Dedicated public
random tests have560 assertions. Network/snapshot contracts are unchanged.
