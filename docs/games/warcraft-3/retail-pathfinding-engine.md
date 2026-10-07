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
indexed by [ORDER-01.6](retail-pathfinding-todo.md#order-01--arrival-and-failure-dispatch),
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
| ORDER-01.11 | `s_repair.c` concrete Repair/Renew/Restoration owners | Completed current-head admission/work/interception, pending activation, completion/interruption and saved target lifetime; [payoff122](#repair-families-own-admission-work-and-pending-activation) |
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

## Patrol uses a two-endpoint public continuation

**S/L plus engine regression, Payoff124:** issued Patrol is851990, while retail
expands it into the active two-endpoint public order851991. The former remains
the issued-order event identity. The engine previously returned851990 from
`GetUnitCurrentOrder`; `S_IssuePatrolOrder` now owns this expansion. The named
`WC3_ORDER_ID_PATROL` and `WC3_ORDER_ID_PATROL_TWO_POINTS` identify the issued
command and active endpoint order. The owner accepts endpoints, not a caller's
order ID to conditionally translate. A new
production-order regression fails four assertions before the fix and passes
through temporary Attack, rejected enemy Repair, enemy death and replacement
Move. Existing native/UI Patrol regressions now assert the captured active head
instead of equating it with `OrderId("patrol")`; their Save117 continuation passes.

The7-subject fixture runs240 public timer samples. Cases cover undisturbed
reversal, automatic actual attacks followed by KillUnit/RemoveUnit, a damage
callback replacing Patrol with Move and removing the enemy, rejected Repair,
Stop, subject retirement/recreation, and a terrain-blocked endpoint. Both
complete original captures repeat all1,707 ten-word public records, digest
`cecfb6e2e6874b57c3bd8cf379f3f74f7abd6eafba63d82a35dceb90e8f6c5ec`.
Their33 ordered continuation transitions also repeat exactly, digest
`131e86d1491fa9bd5476b6a2ca8dac36adcaf92fcffdc495eb5bfde38f086760`.
Actual damage-source handles establish combat, independently of animation.
Enemy loss preserves851991; nested Move851986 survives the old owner's unwind.
The initially attempted dead-target Attack is an accepted retail replacement,
so exploratory capture124a is not the rejected-order witness. Accepted captures
124b/c use a hostile Repair rejection instead.

| Original owner | Verified behavior |
|---|---|
| `5fdff0 Move_CreatePatrolTasks` | Read primary48/50 and alternate5c/64 from the retained public order. Prepend a point task and d0175 continuation task. |
| `692010 Unit_PrependTwoPointTask` | ECX Unit; seven stack arguments; RET1c. Store both points in the task, then prepend it. |
| `689d40 TaskTwoPoint_SetArguments` | ECX task; eight stack arguments; RET20. Task payload38/40 and54/5c preserve the two endpoints. |
| `5fff70 Move_AppendPatrolContinuation` | ECX Move, stack4 event, RET4. Read the task's return point and retained outward point; create and append the successor. |
| `690fa0 OrderPoint_CreateWithContinuation` | ECX command, EDX player; six stack arguments; RET18. Return the new two-endpoint order. |

**Creation order is reversed by prepend.** The physical d016b point task runs
before d0175; the return order is appended when that leg finishes, before the
old public head drains. The33 actual before/after snapshots preserve the old
head, increment public count and set the new tail. Treating static factory-call
order as execution order incorrectly puts the return append before outward
movement. Unit virtualec is `685da0 CUnit_GetOwnerIndex`, not an order factory.
The true Unit vtable base is6fb77eb0;6fb77f34 is its84 entry address.

Two additional genuine Shift-P input captures use the owned Winelib helper,
without game-memory writes. A queued Patrol behind Move captures its origin at
activation near the completed Move endpoint, not at the earlier click. A Patrol
queued behind Patrol remains ahead of the first leg's internally appended return:
the native continuation grows public count2→3 without replacing its head.
Both captures repeat all3,624 exported words. These producer witnesses remain
external `/GitHub/wc3-analysis/runtime/payoff124/patrol-queue-{a,b}.jsonl`; the
versioned `wc3_patrol_queue_probe.j` and `--point-order-key patrol` reproduce them.
They establish what the next engine queue integration must preserve.

The saved Ghidra readback includes83 partial layouts/423 explicit ABIs, the
96-byte `WC3PatrolTaskPrefix` and104-byte `WC3PatrolEndpointsPrefix`.
`MapPathfinding.java` persists the recovered functions and the ordering caveat.
The frozen lifetime verifier checks complete producer provenance, issued/active
IDs, callback sources, endpoint swaps, append ownership and blocked recovery;
its negative controls reject missing or altered evidence.

The bounded lifetime capture uses the original Human02Interlude wrapper as the
map-builder input:

```sh
python tools/frida/make_wc3_pathfinding_map.py --base "$WC3_SOURCE_MAP" \
  --scenario patrol_lifetime --output "$WC3_DATA/Maps/PathPatrolLifetime.w3m"
DISPLAY=:94 WAYLAND_DISPLAY= WINEDEBUG=-all \
WINEPREFIX=/home/lofcz/.local/share/open-realm/wine-pathfinding-re \
/home/lofcz/.local/share/uv/tools/frida-tools/bin/python \
  tools/frida/trace_wc3_pathfinding.py --data "$WC3_DATA" \
  --map 'Maps\PathPatrolLifetime.w3m' --seconds 75 --samples 10000 \
  --profile-events --motion-events --velocity-events --task-events \
  --metadata-events --patrol-lifetime-events --x11-display :94 \
  --continue-at 30 --continue-after-start 3 --output /tmp/patrol-lifetime.jsonl
```

`WC3_SOURCE_MAP` must be the pinned original wrapped campaign map;
`WC3_DATA` must contain the hash-checked1.27.1.7085 game. The capture requires the
owned Xvfb/Frida environment described in the retail diagnostic workflow.

Validation: Classic/TFT order lifecycle35 tests/527 assertions each; existing
public Patrol native/UI2 tests/45 assertions each; production and test builds.
The fresh strict lifetime corpus entry verifies both captures. Full route/task
composition, precise blocked/combat movement and queued continuation save/reuse
remain **ORDER-01.18**. This payoff closes no checkbox and claims no complete
Patrol numerical or combat-trajectory parity.

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
passes 134/134 with source fingerprints unchanged. Manifest SHA256
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
port, then passes all40 exact cases. The wider public numeric subset passes 136
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

The query-local index in `wc3_pathing_placement.h` scans unblocked row/column bits in the original ring order.
`__builtin_clzll` and `__builtin_ctzll` return signed `int`; both ternary branches must use signed arithmetic before
storing the nonnegative index. Using `63u` in just the reverse branch caused GCC's `-Werror=sign-compare` to reject
every pathfinding probe that included this header in CI. `tests/test_wc3_placement_index.py` compiles with
`-Wall -Wextra -Werror` at `-O0`/`-O2`, suppressing only unused static helpers from the included headers, and uses UBSan
to compare 12,000 indexed/scalar placements plus broad-witness pruning. Run it with
`python3 tests/test_wc3_placement_index.py`; `make test-pathfinding-tools` checks the probe consumers with and without
`BZ_WC3_FINE_TRACE` at both optimization levels.

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

## Authored repulsion reaches idle engine units

The engine now applies authored `repulse`, `repulseParam`, `repulseGroup` and
`repulsePrio` through Move. A generic ability owner-update hook runs after the
scheduled movement callbacks. Repulsors retain newest-first membership and
alternate owner-list parity before updating every other member, including idle
units. Each visit consumes cooldown first, admits the retained fine-coordinate
endpoint against terrain/live occupancy excluding self, then accumulates
eligible neighbors and applies retail damping, deadzone and cap. Near overlap
consumes exactly one draw from the saved pathfinding-owner random state.

`wc3_pathing_repulsion.h` uses the complete original initializer's scalar words
and the ordered software arithmetic. `verify_wc3_pathing_repulsion.py` executes
16 settings rows and 1,024 full category-producer cases with the authentic CUnit
vtable. Its 600 pair and 105 tail slices match production C exactly; these are
arithmetic slices, not a complete original scheduler/query/application replay.
The first seed12345 overlap's pending vector and next random state also match
through the real engine `RunFrame` scheduler.

Three actual JASS/scheduler regressions pass72 assertions in RoC and TFT:
overlapping idle Gryphons separate without receiving an order, overlapping
Footmen remain stationary, Save66 restores the identical numerical continuation,
owner transfer replaces policy/membership, paused sources remain stationary,
and public removal retires membership after deferred reclamation. A custom
`hRPL` SLK row selects configuration1, group19 and rank3, separating only its
matching group while the coincident stock Gryphon remains stationary.

Save66 includes pending vectors, packed policy/cooldown, active membership,
relocated next/head pointers and owner-list parity. The pathfinding random state
and fine pose/clock continue through the existing schema. Original-proximity
cell-chain order, stamps, runtime category15 semantics and extra disable
producers remain explicitly open; the current area index visits actors once,
so this port does not establish multi-neighbor accumulation/draw-order parity.
This is the next separation integration boundary, not a claimed complete WC3
pathfinding replacement.

Artifacts under the local analysis root: `repulsion-kernels-corpus-final-261001/`,
`runtime/repulsion-ghidra-readback-261001.json`, and the versioned repository
`retail-pathfinding-repulsion-1.27.json`. Ghidra now retains440 names,31 layouts,
173 verified fields and170 explicit prototypes, including the owner getter and
fine-point application contract. The misleading historical15fc70 cap comment
and6803f0 virtual-call interpretation are corrected.

Validation: the required umbrella suite passes; forced release builds pass
46,097 assertions in2,222 WC3 cases in each fixture variant. All137 pathfinding
tool tests pass. Original/C pair/tail words match atO0 andO2 twice. The first
umbrella attempt rejected a stale types-fixture checksum after annotation;
that metadata was corrected before the accepted rerun. The release game build
and repository checks are recorded with the engine change.


## Adaptive search reaches long-distance Move

Engine payoff19 replaces the long-location routing gap with ordinary retail
adaptive search in `wc3_pathing_adaptive.h`. Requests beyond the fine48-cell
range now use the2x-fine base and three clear/mixed parent levels. The port
retains first-lookup node representatives, side subdivision order, cardinal/
corner gates, both stored sizes, the asymmetric size2 predicates, integer
Newton distance, heap ties, nearest partial endpoints and reconstruction words.
The next coarse leg is refined with the existing fine search and live occupancy;
Move retains that local turn through its ordinary ability lifecycle.

`verify_wc3_pathing_adaptive.py --engine-library` compares every route coordinate
word, result/point count, charged pop and created-node count against full original
`162cb0` requests. All356 size1 and356 size2 cases agree atO0/O2 and repeated
execution. The four known size2 original/reference reachability differences
remain visible; this port preserves them. A separate budget8 comparison has356
exact original/C results at each optimization level, including partial routes;
its268 reachability disagreements concern a graph reference with unlimited work,
not a production-C mismatch.

The real engine regression issues public JASS Move on a64x64 fine map with an
off-axis wall gap, confirms a retained adaptive turn, checks every sampled point
and reaches the existing retail arrival range without snapping to the target.
A Save66 round-trip reproduces180 subsequent ticks' position, heading, velocity
and order words exactly. A second regression alternates all four lanes and both
sizes, edits terrain through public JASS, changes the walk route while retaining
the untouched fly route, and checks legal fine handoffs. The prior winding
corridor regression now expects the new adaptive turn and verifies its legality.
The two added lifecycle cases pass1,310 assertions in each WC3 fixture variant.

The four static classifications are derived cache state, keyed by the shared
routing bake epoch; module shutdown releases their allocation. Save66 and the
wire contract are unchanged. Engine search retains the existing2,048 charged-pop
bound. Original5,000-budget request producers, special/warp edges, dynamic
classification/exclusion, original allocation/stamp lifetime, exact hierarchy-to-
fine route progression and whole retail movement trajectories remain open.
Static-only interaction/resource queries still use their existing routing path.

Accepted original/C artifacts under the analysis root:
`adaptive-engine-corpus-final-261001/`,
`adaptive-size1-engine-o0-repeat-261001.json`,
`adaptive-size2-engine-o0-repeat-261001.json`,
`adaptive-budget8-engine-o0-261001.json` and
`adaptive-budget8-engine-o2-261001.json`.
Ghidra now retains456 descriptive function names; the added predicates and
integer-distance comments are saved and read back in
`runtime/adaptive-engine-ghidra-readback-261001.json`. The31 layouts,173 verified
fields and170 explicit prototypes are unchanged.


Validation for payoff19: the required umbrella passes with coherent release
artifacts. Forced release WC3 suites pass47,408 assertions in2,224 cases for
both RoC and TFT; the production release executable builds without C warnings.
All137 pathfinding tool tests, ability coverage audit, backlog ID/count checks,
document links and `git diff --check` pass. The initial broader run caught the
old winding-corridor expectation; it is now updated to the new legal route.


## Retained fine routes reproduce a complete retail detour

Engine payoff20 fixes a concrete route-to-motion mismatch. Original168870
initializes a new destination-first fine curve at count-2;167ce0 exposes that
raw successor. Original167070 keeps it until the predicted native position is
within0.49 fine cells, then165e60/167bf0 select progressively farther visible
successors. The engine previously smoothed immediately and reconsidered the
turn every whole cell. Ordinary location detours now retain the full fine chain,
initial index and subsequent progress through the Move ability. A retained
curve continues when the final goal becomes visible; it is not replaced by the
generic flow field or by a fresh direct-goal shortcut.

The versioned `retail-fine-route-trajectory-1.27.json` freezes all34 ticks of a
controlled original singleton wall detour: fine positions, velocities, heading
words and route indices. The new corpus entry `fine-route-trajectory-reference`
replays the original twice and compares all68 steps to that reference. The
actual engine test creates authored `hRTE` through compiled JASS, issues the
public Move order and executes the real Move callback with the same1/32 elapsed
input. All34 position/velocity/heading steps agree exactly, including the
turn-induced stops and natural final arrival. Active curve indices agree;
the final inactive storage lifetime is deliberately not equated with retail's
pooled release. Saving at tick12 and restoring reproduces the remaining22 ticks.

That whole-motion regression exposed two additional production errors: a
turn-induced stop discarded the old-velocity step, and its stored heading could
remain above2pi. Scheduled Move now commits that previous step before publishing
zero velocity and wraps the stopped facing through the verified scalar helper.
The first-point regression and complete word trajectory both failed before
these corrections. The broader routing regressions retain their lane, blocker,
partial-route and generic-field checks while expecting the raw first successor;
a dynamic blocker is inserted after progress produces a segment with interior
samples, because the original length<=1 sampler has none.

Save67 serializes native curve points, count, index and mask. Its process pointer
is runtime-only and rebuilt by the reader; bounds and finite-point checks reject
invalid payloads. Earlier formats are rejected by the exact-version guard.
Move owns curve cleanup on public actor removal, reload, test reset and module
shutdown. Wire messages, JASS snapshot7 and the callback roster are unchanged.
The scheduler lifecycle regression exercises pause/resume, Stop, replacement
and deferred public removal. Stock radius producers, fresh open-line steering,
nonzero-origin whole-route words, actual original primary-owner cadence, dynamic
yield/retry, adaptive refill and original buffer growth remain required. This
controlled motion match does not close those broader gates.

Accepted original artifact under the local analysis root:
`fine-route-trajectory-corpus-accepted-261001/corpus-results.json`.
Ghidra retains456 names,31 layouts,177 verified fields and170 explicit prototypes.
The four new fields type the fine/adaptive point-buffer pointers and counts in
`WC3PathPrefix`. Saved readback:
`runtime/route-progress-ghidra-types-261001.json` and its repeat. The schema
preflight now permits canonical pointers to separately checked refined layouts;
incompatible offsets, field identities and byte extents still refuse mutation.

Validation for payoff20: the required umbrella and production release build pass.
Forced release suites pass47,805 assertions in2,228 cases for both RoC and TFT;
all138 pathfinding tool tests pass, including the frozen-to-engine word-table
check. Ability coverage, backlog counts/IDs, new document links and diff checks
pass. The first release attempt exposed an unused search-definition warning
from including its implementation in the common game declaration header; it
was stopped, the existing bounds were extracted into `wc3_pathing_limits.h`,
and the complete final release checks passed without C compiler warnings.
The first lifecycle fixture's misplaced SLK row was corrected and its authored
turn/window values are now asserted. Failed attempts grant no accepted evidence.


## Native route inputs survive world projection

Engine payoff21 fixes the next complete-motion mismatch rather than adding a
standalone numeric helper. The controlled detour's native inputs are unchanged
while the engine publishes into four maps with origins `(0,0)`, `(-256,-256)`,
`(-2048,512)` and `(0.125,-19.25)`. After resetting each case's clock and waypoint
pool,174 position/velocity/heading word assertions failed. The first divergence
occurs when the callback subtracts a world waypoint from a rounded published
world position; subsequent fine motion inherits that different velocity.

Original16a790 obtains the predicted native vector from05bdd0 and passes it
directly to16fbd0. At16fd3b,16fbd0 subtracts its native waypoint and native source
before16f630 computes heading. That decision has no world-origin input. Move now
carries its valid published `sampled_pose` into the route query; unchanged world
publication words validate the alias. Explicit world-only geometry still has its
own input path. Initial fine reconstruction, retained-point distance, segment
sampling and route direction consume the native source without inverting world
publication. World coordinates remain the engine's visible waypoint and actor
position interface. Manual world writers invalidate the alias through the
existing pose/publication contract.

The actual compiled-JASS Move regression now passes1,364 assertions: all34
original fine position/velocity/heading ticks at each origin and each22-tick
saved continuation, with the same active indices and natural arrival. This
proves the original native-input contract under translated engine publication;
it does not claim four new live retail scenes or a different original clock
producer. The original reference remains the twice-replayed controlled detour
from payoff20. Full primary-owner cadence, additional footprints/lanes, adaptive
refill and dynamic yielding remain open. Runtime query fields do not change
Save67, the semantic JASS snapshot or the wire format.

Ghidra saves the native coordinate contract on05bdd0/16fbd0 and now explicitly
types16fbd0's ECX mover and nine stack words, including its source/destination
vectors. Instruction readback confirms `RET0x24`. The schema retains31 layouts,
177 verified fields and171 explicit prototypes;456 descriptive names are
unchanged. Saved readback under the analysis root:
`runtime/native-route-source-ghidra-types-261001.json` and
`runtime/native-route-source-ghidra-readback-261001.json`.

Accepted regression artifacts: `/tmp/wc3-route-origin-fixture-corrected-red.log`
records the174 genuine failures after fixture correction;
`/tmp/wc3-route-origin-native-first.log` records the1,364-word acceptance.
The earlier loop experiments reset clocks after order submission or retained
an old waypoint pool; those invalid fixtures are rejected, not engine evidence.

Validation for payoff21: forced release RoC/TFT suites pass48,828 assertions in
2,228 cases each. The required release umbrella and production executable build
pass without C compiler warnings. All138 pathfinding tool tests, ability coverage,
backlog counts/IDs, document targets and diff checks pass. The diagnostic O0 TFT
run had one additional branch assertion and also passed; the forced release
artifacts above are the final matrix. No additional retail replay is claimed:
the unchanged original fixture is reused for its native-input contract.


## Primary owner clocks reproduce the complete detour

Payoff22 joins the previous retained-route and native-coordinate ports to the
actual engine frame scheduler. The original15aa80 controlled singleton wall
case advances054190 six times with the authentic5ms word `3ba3d70a` before each
owner pass. Original161040 supplies the elapsed interval; it is not replaced
with host.03 or the earlier1/32 test input. Two independent complete executions
match every clock, elapsed, fine position, velocity, heading and active curve
index through34 motion ticks. The original performs45 owner passes including
the supplied fresh callback atclock0 and nine visual settling ticks.

The engine test creates the authored hRTE unit through compiled JASS, submits
public Move, supplies the same fresh callback atclock0, then runs
`globals.RunFrame()` every30ms. No test writes callback clocks or publishes
positions during this trajectory. All509 assertions pass, including all active
motion and clock words, natural order completion and the22 remaining ticks
after loading a tick12 save. The existing engine implementation already agrees;
this closes the combined scheduling regression without inventing a production
change. Save67 and network contracts are unchanged.

Frozen reference: `tools/ghidra/fixtures/retail-primary-owner-route-1.27.json`.
Accepted fresh repeat: `primary-owner-route-corpus-accepted-261001/corpus-results.json`
under the local analysis root. The strict corpus now has214 entries,87 oracles
and76 frozen fixtures. Ghidra persists the expanded15aa80 contract; its saved
readback is `runtime/primary-owner-route-ghidra-readback-261001.json`.

This supplied starting phase does not establish initial public order-admission
phase parity. Stock profile producers, other clocks, shared groups, live wall
scenes, rendered unit presentation, adaptive refill and dynamic yielding remain
open. SCHED-02.5 names exactly this bounded composition.

Validation for payoff22: forced release RoC/TFT suites each pass49,337 assertions
in2,229 cases. The required release umbrella and production executable build
pass without C compiler warnings. All139 pathfinding tool tests, corpus hashes,
backlog counts/IDs, document targets and diff checks pass.


## Adaptive handoff uses retail route length

Payoff23 fixes the initial long-route handoff. The engine previously selected
a visible coarse point, then limited its distance to48 fine cells. Original
165d10/167ae0 instead starts below the current reverse-chain index and sums
software scalar edge lengths until ten accelerator units. Index zero is never
inspected by that scan. Original167d70 mode zero selects the resulting coarse
point times two when the index is nonzero; index zero selects the path's current
destination. Visibility is not this initial coarse selection policy.

The production route helper now implements that recovered scan, including its
sentinel result without executing a portal. The ordinary hierarchy producer
cannot emit that sentinel. The game handoff consumes its ordinary result before
calling the existing fine search. A48-case engine regression failed64 assertions
before the change. It now matches all2,936 original fine-route coordinate words
across open, solid-wall and gapped-wall64-cell maps, four footprint classes and
four traversal lanes. It also checks retained fine count and initial index.

The compiled-JASS public Move regression uses the solid wall: the local fine
destination is native `(15.5,33.5)`, and its first point is `(4.5,5.5)`. The public
order keeps the final `(59.25,59.75)` destination. An earlier public fixture used
a clear diagonal through the gap and incorrectly expected the engine to enter
its obstruction route; that expectation is rejected. It provides no public
producer or new engine-defect evidence.

Original full165ae0 route buffers are frozen in
`tools/ghidra/fixtures/retail-adaptive-handoff-1.27.json`. Two independent original
runs agree exactly. All1,456 complete original167ae0 selector outputs and marker
flags match production C atO0 andO2. The accepted strict two-entry run is
`adaptive-handoff-corpus-accepted-261001/corpus-results.json` under the analysis
root. Its manifest now has216 entries,89 oracles and77 fixtures.

This is the initial ordinary handoff port. Coarse buffer retention across later
fine refills, dynamic obstruction/yielding, budget admission, primary owner
composition and portal execution remain required. Save67 and network contracts
are unchanged. Ghidra records the integrated167ae0/165d10/167d70 contracts so
that this policy does not need another investigation.

Saved Ghidra readback: `runtime/adaptive-handoff-ghidra-readback-261001.json`.
Validation for payoff23: forced release RoC/TFT suites each pass52,430 assertions
in2,231 cases. The required release umbrella and production executable pass
without C compiler warnings. All140 pathfinding tool tests, ability coverage,
corpus hashes, backlog counts/IDs, document targets and diff checks pass.


## Retained coarse progress refills the fine route

Payoff24 preserves the original adaptive point buffer and current index instead
of treating each fine endpoint as a new global route request. Original165f10
measures approach in accelerator coordinates against.49 accelerator units. It
runs before fine progression. A supplied source.48 accelerator units left of
the selected waypoint therefore consumes the coarse point and invalidates/refills
the fine route, although it remains.96 fine cells from the fine endpoint.

The engine previously retained that endpoint under its separate.49-fine-cell
threshold. All32 initial routes built correctly, but64 subsequent route-count
and index assertions failed. The production owner now stores coarse points and
index, applies the original coarse squared-distance predicate first, consumes
167ae0's reverse-chain selection and refills the fine buffer from the retained
coarse destination. Static revision, goal, radius and lane changes invalidate
the coarse cache. Move frees both buffers on removal, load and shutdown.

The controlled original replay advances the owner counter ten ticks and resets
fine work at each supplied admitted boundary. It executes the complete original
165ae0 chain; it does not simulate the full owner scheduler. Open/gapped maps,
four classes and four lanes match all1,800 native fine words after consumption,
with unchanged original coarse buffers. This matrix reaches coarse index zero;
larger retained nonzero next indices and physical retail trajectories remain
required. First experiments used a one-tick denied boundary or carried the
previous fixture's counter into a new case; those are rejected admitted controls.
The accepted reference is `tools/ghidra/fixtures/retail-adaptive-progress-1.27.json`,
with a fresh repeat under `adaptive-progress-corpus-accepted-261001/`.

Save68 serializes both curve buffers and indices. Serialized process pointers
are ignored; count/index limits, finite points and truncated tails are checked.
The coarse revision is runtime state. Reload reconstructs the saved terrain and
obstacles, then binds restored curves to that world's new bake epoch. Before
that correction the long Move save regression changed760 later motion words.
Afterward the real RunFrame long Move repeats all180 saved continuation frames,
retaining coarse count/index and reaching the original order goal. Semantic
JASS snapshot7 and network contracts remain unchanged.

The strict corpus now has217 entries,90 oracles and78 frozen fixtures. Ghidra
retains the integrated165f10/167ce0 contracts. Dynamic blockers, yielding,
denied admission timing, larger route index transitions, other clock producers
and portal execution remain open under the existing backlog.

Saved Ghidra readback: `runtime/adaptive-progress-ghidra-readback-261001.json`.
Validation for payoff24: forced release RoC/TFT suites each pass54,459 assertions
in2,232 cases. The required release umbrella and production executable pass
without C compiler warnings. All141 pathfinding tool tests, ability coverage,
corpus hashes, backlog counts/IDs, document targets and diff checks pass. Invalid
fine/adaptive extent, index, nonfinite and truncated-tail tests reject all eight
payloads and retain no serialized process pointers.


## Public pathing toggle separates query from occupancy

Engine payoff25 fixes a public behavior gap, rather than adding another unused
helper. The registered `SetUnitPathing` native is `6f215540`. Its false branch
gets the embedded mover bridge through the actual CUnit vtable `6fb77eb0` slot
`b8` (`6864d0`), reads the **current occupancy category** through `05ac60`, then
publishes that category with query zero through virtual `15c` (`699200` →
`05c7e0`). The true branch restores the authored query and category through
slots `164`/`160` (`678b50`/`678b60`). The occupancy and query are separate inputs;
clearing the query does not make the unit invisible to other movers.

Two newly generated `pathing_toggle` maps invoke false/true/false/true at ticks
10/20/30/40. Both complete owned, observational Frida captures contain all four
native enter/publication/leave brackets: stock Footman category stays `ca`,
object category stays `010000ca`, and its own query alternates zero and
`02000002`. Native brackets preserve position and current order. The travelling
Footman crosses the wall at x=-1936/y=-560 while its own query is zero. All182
scalar motion decisions and183 velocity, position/clock and facing commits
match the production C arithmetic; the independently repeated raw decision and
commit sequences are identical. Accepted evidence:
`pathing-toggle-corpus-accepted-261001/corpus-results.json`; raw captures are
`runtime/pathing-toggle-{first,repeat}-261001.jsonl`. The strict scenario checker
rejects lost brackets, category/mask substitution, receiver/identity changes,
script transition loss, order replacement and a missing straight wall crossing.

The engine regression first fails four assertions: a disabled blocker vanishes
from another mover's route query; a disabled mover keeps its retained detour;
that route remains active; and it never crosses the blocked wall cells. The fix
retains ordinary occupancy/category and target identity when `no_pathing` is
true. Move's own route-line policy bypasses geometry, and its direct branch
ignores an existing fine curve while disabled. Re-enabling pathing rebuilds a
collision-aware route from the current source. Existing precise step collision
already ignored collisions for the disabled mover and retained disabled
neighbours; it did not need a second collision representation. Legacy generic
geometry mask zero still means walking: this public toggle does not alter that
separate compatibility contract.

The public JASS/frame regression passes261 assertions, including an actual
blocked-cell crossing, a pre-existing detour, resumed collision-aware return,
and240 exact position/velocity words after save/load while disabled. Target
perimeter regression expectations now preserve the disabled target's category.
Save68, JASS semantic snapshot7 and the wire layout are unchanged: `no_pathing`
already has a persistence contract and no runtime field was added. This proves
the bounded ordinary Move policy and saved continuation; it does not claim the
engine's complete public trajectory equals the live capture. Placement,
blocked command destination admission, repulsion/fallback ownership, other
profiles and native retained-buffer lifetime remain separate work.

Ghidra now retains460 names,31 partial layouts,177 fields and175 explicit
prototypes. All four new native/bridge functions have verified receiver/stack
storage and saved descriptive comments. Readbacks:
`runtime/pathing-toggle-ghidra-types-261001.json` and
`runtime/pathing-toggle-ghidra-readback-261001.json`. Corpus inventory now has
220 entries:90 original-code oracles,102 archived audits and28 live contracts;
78 repository fixtures remain hash checked.

Validation: forced release RoC/TFT suites each pass54,720 assertions in2,233
cases;142 pathfinding tool tests pass. Full `make BUILD=release test` and
production `openwarcraft3` builds pass without C compiler warnings. Ability
coverage, backlog counts/IDs/areas, local links and `git diff --check` pass.


## Disabled query reaches public placement

Engine payoff26 continues the public toggle through its placement consumer.
`SetUnitPosition` → `698050` → `653510` → `67f490` → `6515f0` obtains its query
from actual CUnit vtable `158`, `685ef0` → `05ac30`. This getter resolves the
bridge identity, reads the owned query path at canonical+a8 and returns its
`+9c & 00ffffff`; an absent path returns zero. It is distinct from the authored
getter `678b50`. Vtable `168` (`6742f0`) obtains the cached authored collision
radius through `674280` with ECX=output/EDX=rawcode. Query zero changes neither
the footprint nor the `654060` same-terrain-level acceptance callback.

Two fresh owned `pathing_position` maps toggle true/false/true/false/true before
five `SetUnitPosition` calls at ticks20/30/40/50/60. Enabled requests for
(-1936,-560) and(-1935.875,-560.125) land at(-1936,-592); disabled requests retain
the exact requested coordinates, despite the same four blocked wall cells.
All native/profile brackets retain categoryca and object010000ca. The two
captures contain65 exact public getter/bridge queries, five exact world
position commits and seven complete placement searches. Two additional
five-attempt queries belong to Stop's embedded recovery after restoring pathing
inside a blocked footprint; the public point admission still uses32 attempts.
All searches, candidates, masks, radius, level callback, result and output words
match production C and repeat. The preceding33 motion decisions and33
velocity/position/facing commits also match C exactly and repeat.

The engine public-native regression reproduces six raw destination failures.
`G_FindUnitPlacementPosition` now passes the current zero mask when
`no_pathing` is set, and its cell callback preserves valid footprint bounds
without applying the generic geometry API's legacy zero-as-walk default. The
same-level callback and authored footprint remain active. Normal routing
continues to normalize its generic masks before constructing the graph. The
regression passes65 assertions through public SetUnitPosition/Loc, both toggle
directions, all five observed destinations and a disabled save/load continuation.
Save68, JASS snapshot7 and the wire layout remain unchanged.

`verify_wc3_pathing_placement.py --zero-mask-only --reference` independently
executes576 complete original calls at288 inputs: four classes, two output
forms, three budgets, repeated modes0/7, three source positions and four terrain
maps. **Terrain is still authored02 when the query is zero**; the sealed-map
controls do not erase obstacles to manufacture passability. Outside fine-source
controls retain bounds rejection. C reproduces the complete original outputs,
and a damaged frozen reference is rejected. This isolated matrix has a null
callback; the repeated public capture separately proves the supplied level6
acceptance callback, not arbitrary bridge overlays or outside-map clipping.

Frozen fixtures are `retail-zero-query-placement-1.27.json` and
`retail-public-pathing-position-1.27.json`. Accepted strict replay:
`pathing-position-corpus-accepted-b261001/corpus-results.json`; raw witnesses:
`runtime/pathing-position-{first,repeat}-261001.jsonl`. The public verifier's
new mode rejects substituted toggle receivers/categories, zero-mask changes,
missing brackets, endpoints, Stop velocity and source provenance changes. Its
pre-existing six-placement mode retains its original frozen contract.

Ghidra retains463 descriptive names and178 explicit prototypes, with31 layouts
and177 fields. Saved readbacks:
`runtime/pathing-position-ghidra-types-261001.json` and
`runtime/pathing-position-ghidra-readback-261001.json`. The strict inventory now
has224 entries:91 original-code oracles,104 archive audits and29 live contracts,
with80 hash-checked repository fixtures. Broader actor forms, outside-map public
clipping, bridge/terrain-level rejection, full engine clock parity and separation
are not closed by this ordinary ground placement result.

Validation: forced release RoC/TFT suites each pass54,785 assertions in2,234
cases; full release umbrella and production builds pass without C compiler
warnings, as do144 pathfinding tool tests, ability coverage, backlog counts/IDs/
areas, local links and `git diff --check`.


## Stop recovers an embedded mover with a bounded query

Engine payoff27 ports the separate Stop recovery that FOOT-04.4 identified in
blocked public placement. Actual `171340` receives a nonzero stack14 recovery
limit, then calls `170080` with the current query mask, callback/context and
policy2. The recovery helper predicts native fine pose, temporarily suppresses
its own spatial object, tests the endpoint footprint, and only searches when
that point is blocked. A successful `14a1e0` point/ring query applies its admitted
fine point through `05c820`. Its return value is **source was blocked**, not
search succeeded: an exhausted five-attempt query returns1 and leaves pose
unchanged. Clear or in-bounds queryzero returns0 without a placement search.
Instruction readback verifies RET14/RET1c and the explicit receiver/stack slots.

Two fresh owned `stop_recovery` maps invoke public Stop on a clear travelling
unit, an enabled unit embedded in the four-cell wall, a disabled embedded unit,
and an enabled unit embedded in an eleven-cell square. Each command performs
two recovery calls. The eight result words are0/0/1/0/0/0/1/1: first enabled
wall recovery moves(-1936,-560) to(-1936,-592), the settled second call is clear,
the disabled unit stays put, and both dense-square searches exhaust the five
attempts. All three searches retain callback654060/context6, radius3f780000,
endpoint mode and supplied query mask. Complete cell visits/results and output
words match production C. The recovered native fine commit and exact published
world pair match the new production fine-coordinate writer; all24 public getter
words match C's noncommitting prediction and the native results. The preceding33
motion and33 velocity/position/facing commits match and repeat exactly.

The engine regression first fails both embedded-recovery destination assertions,
before and after save/load. Stop now asks the Move owner to recover its stopped
fine pose. Public32-attempt placement and Stop's five-attempt recovery share the
same footprint/object/terrain-level admission, with separate limits. Recovery
preserves native fine precision rather than converting an admitted point through
world coordinates. Queryzero and failed bounded searches stay in place. The
public-native regression passes29 assertions, including save/load, both toggle
policies, exhaustion and Stop during map release; all196 movement tests also
pass. Broader actor forms, bridge overlays and outside-map public recovery remain
unverified. This does not change Save68, JASS snapshot7 or the wire contract.

The strict verifier is `tools/frida/verify_wc3_stop_recovery_trace.py`; its frozen
witness is `retail-public-stop-recovery-1.27.json`. It rejects missing calls,
changed receiver/masks/limits/callbacks, wrong helper statuses, substituted fine
outputs, movement during exhaustion, getter changes and source provenance.
Accepted replay: `stop-recovery-corpus-accepted-261001/corpus-results.json`;
raw witnesses `runtime/stop-recovery-{first,repeat}-261001.jsonl`. The inventory
now has227 entries:91 original-code oracles,106 archived audits and30 live
contracts, with81 hash-checked repository fixtures. Ghidra retains464 names,
180 explicit prototypes,31 layouts and177 fields, with saved readbacks
`runtime/stop-recovery-ghidra-types-261001.json` and
`runtime/stop-recovery-ghidra-readback-261001.json`.

Validation: forced release RoC/TFT suites each pass54,814 assertions in2,235
cases;145 pathfinding tool tests, full release umbrella and production builds,
ability coverage, backlog counts/IDs/areas, local links and `git diff --check`
pass without C compiler warnings. Full retail owner phases, all Stop callers and dynamic
multi-mover recovery remain separate from this bounded public result.


## Successive long refills select the original fine index

Engine payoff28 addresses ROUTE-03.3's remaining nonzero following-index case,
explicitly split into ROUTE-03.4. Thirty-two complete original165ae0 requests on
128-cell open/gapped maps retain their entire coarse buffers through five
admitted fine refills each. Open chains consume5→4→3→2→1→0; gapped chains
consume9→8→7→2→1→0. All initial/refill fine buffers, indices and selected outputs
match the engine across four classes and four lanes. Source positions remain
controlled at .48 accelerator units from the current point, with supplied ten
owner ticks and reset fine work between requests. This verifies repeated route
composition, not full physical retail owner motion or denied-request timing.

The larger fixture reproduces160 engine index failures. Original1489a0 latches
fine+d0 (`observed_obstruction`) whenever a queried perimeter cell rejects.
Original14ad50 clears it before the request;166e90 uses index0 when the flag
remains clear, otherwise initializes the next parent index. **A blocked cell
anywhere in expansion matters**, even when the reconstructed route is clear.
The union of each class's neighbor masks covers its complete perimeter, so the
C search latches the same flag when any expansion edge rejects.480 complete
original/C requests match that flag;192 frozen initial/refill flags also match
atO0/O2 with search reuse. The initial fine-index producer is now implemented,
replacing the previously unconditional count-2 engine policy. Earlier64-cell
handoff and refill buffers retain their original words; their previously assumed
indices now use the independently recorded original outputs.

The32-case engine regression passes 13,384 exact assertions after the fix. An
actual public long Move across an offset wall gap reaches the legal goal through
multiple coarse transitions. It preserves a nonzero coarse index across save/load
and repeats260 frames with exact world/native fine pose, velocity, heading,
current order and both route indices:3,027 assertions pass. All198 movement
cases pass. The change adds only process-local search scratch; Save68, JASS
snapshot7 and the wire contract retain their existing representation.

Frozen witness: `retail-adaptive-long-progress-1.27.json`. Accepted strict replay:
`adaptive-long-corpus-accepted-261001/corpus-results.json`, including the existing
handoff/progress references. The inventory has228 entries:92 original oracles,
106 archives and30 live contracts, with82 pinned fixtures. Four Ghidra comments
are saved and read back in `runtime/adaptive-long-ghidra-readback-261001.json`;
the existing31 layouts/177 fields already identify the fine obstruction flag.

Validation: forced release RoC/TFT suites each pass71,225 assertions in2,237
cases; full release umbrella and production builds,147 pathfinding tool tests,
ability coverage, backlog counts/IDs/areas, local links and `git diff --check`
pass without C compiler warnings. Dynamic
blockers/yielding, denied refills, complete physical retail long motion and portals
remain explicit separate tasks.


## Ordinary path defaults enable adaptive routing

Engine payoff29 closes BASE-02.7, explicitly adding the ordinary activation
policy discovered while reproducing budget-limited routing. Original166060
`Path_Activate` writes flags88=200000, fine/coarse indices=-1 and packed
limits84=019002bc:700 fine attempts and400 adaptive attempts. Path_Advance165ae0
calls165b60 before165c60; the adaptive stage has no48-cell admission cutoff.
Original1678f0 independently returns5000 for169840's group-owned path. Existing
accepted public captures (`pathing-position-first/repeat-261001.jsonl`) record
that actual sequence: group5000, member400, member fine700. Reusing those captures
establishes the producer distinction; this change does not claim a new live maze
capture or complete group-budget implementation. The144-distance branch in1689d0
controls retry counting and does not admit the adaptive stage.

The engine inherited PATH_ACCEL_MAX_DISTANCE=48 from its bounded nearby-routing
policy. On a64-cell maze whose goal is43 cells east and39 north, that gate skips
the original coarse route. With the original fine700 cap, successive partial
fine endpoints stop advancing beyond a wall, leaving the public unit oscillating.
The full original165ae0 oracle reproduces32 coarse/fine count/index failures in
eight engine cases. Ordinary live unit queries now build adaptive routes before
fine refinement and use400/700 attempt limits. Generic moverless queries retain
their existing bounded request policy. The local leg procedure is exposed as
G_BuildUnitMoveLocalRoute and used directly by the adaptive stage; it provides
an independent entry point for the isolated148100 fine contract without a
second routing implementation.

Two original winding-map patterns across four classes produce eight complete
coarse/fine handoffs. Every coordinate and both indices match the actual engine
world builder:1,276 assertions. Sixteen isolated original fine700/2048 controls
produce distinct partial endpoints and match the production C request kernel
atO0/O2; the actual local builder matches638 assertions. The public JASS Move
keeps its final destination(1512,1400), follows the unmodified four-wall maze,
refills successive legs and arrives. Save/load repeats600 continuation frames
with exact world/native position, velocity, heading, order and fine index:
7,112 assertions. All201 movement cases pass40,232 assertions. Save68, JASS
snapshot7 and the wire representation are unchanged.

Frozen witnesses: `retail-unit-default-route-1.27.json` and
`retail-unit-fine-budget-1.27.json`. Strict fresh replay is
`unit-default-corpus-accepted-261001/corpus-results.json`, including the prior
long-refill reference. Two new Ghidra roles and explicit ABIs are saved and read
back in `runtime/unit-budget-ghidra-readback-261001.json` and
`runtime/unit-budget-ghidra-types-261001.json`:466 names,182 prototypes,
31 layouts/177 fields/44 globals. Scheduling queues, charged-work reset cadence,
group-owned5000 composition and complete physical retail maze motion remain open.

Validation: forced release RoC/TFT suites each pass80,251 assertions in2,240
cases. The release umbrella, production executable build,150 pathfinding tool tests, ability coverage,
backlog counts/unique IDs/area totals, fixture hashes, local links and
`git diff --check` pass without C compiler warnings.


## Ordered moving waits reach ordinary Move

Engine payoff30 integrates original moving-blocker decisions and retained waits;
it explicitly splits ROUTE-05.3 from the remaining05.1/05.2 producer and cycle
work. Every completed leaf includes the production Move change, original/C
comparison, public-order regression and saved continuation. It does not close
full crowd trajectory parity.

Original168360 first clears the requester blocker identity, keeps its unsigned
prior delay and scans the supplied vector in order. It uses software squared
**committed velocity**, not authored movement speed. A candidate without a
resolved group, with zero speed, or with a resolved existing blocker is skipped.
Same group with bit8 clear, faster/equal candidate, or different player class
sets requester identity and `max(delay,4)`, then returns. Otherwise it sets the
candidate identity and `max(delay,20)` and continues; those earlier side effects
survive a later requester wait. A repeated candidate can therefore be skipped
because the first visit blocked it.

The shared production `wc3_pathing_yield.h` decision and countdown kernels match
all8,640 complete original single-candidate cases and25 enabled/disabled gate
controls. The original oracle also checks128 multi-candidate sequences using
real registered identity/group resolution. Original165ae0 checks internal flag
100000 before the delay: it freezes a disabled path; an enabled nonzero delay
consumes one eligible advance and returns1, including1→0. `SetUnitPathing(false)`
zeroes the query mask, which is a different contract from this disabled flag.

Two fresh owned Frida crowd captures record actual velocities, player classes,
group pointers/flags, ordered candidate identities, each chronological blocker
resolution, all actor before/after wait states and the countdown calls. Each
matches the production C decision kernel on2,503 complete runtime decisions,
including51 duplicate candidates,56 requester4 waits,10 peer20 waits and424
retiring delay calls. Actual group flags20000/30000 have bit8 clear. Exact
normalized repeat digest:
`dc0362228855a17aba4279a9581f9f8a176b8daf8a782f9d1b2d6bbd3fed05fc`.
Observer source and map hashes are embedded; map hash:
`3516ddb2377f3b34d53a76c6b08aafc9a8fcdfce2f0436cdc35697e270d4c027`.

Move now collects the next native route step using the existing four-class
entering-strip sampler, continuing across all cells and retaining duplicates
and terrain/null tokens in its32-entry vector. `S_ResolveMoveBlockers` owns the
ordinary engine cohort decisions and persistent4/20 waits. Waits stop
translation while turning still runs; the scheduler consumes one per eligible
ordinary Move call. New orders clear progress; actor reclamation clears other
waiter identities before edict reuse while preserving their remaining delays.
Save69 persists the counter and `F_EDICT` blocker reference. Older save layouts
are rejected; JASS snapshot7 and the network contract are unchanged.

The19-assertion ordered peer-policy regression uses distinct actual actors,
contrasting authored speed with committed velocity, preserving an earlier peer
wait, skipping an already blocked candidate and retaining a larger prior wait.
The124-assertion public regression issues two real JASS Move orders, collects a
moving blocker, saves with its identity/delay live, removes it through deferred
`RemoveUnit` reclamation and disables the query. Both continued runs consume
exactly four scheduled waits, then resume motion; all48 captured position,
velocity, heading, order and wait words repeat exactly after load. The missing
resolver was reproduced with a no-decision control before accepting the port.
All203 movement cases pass40,375 assertions.

Evidence under the report root:

- `runtime/moving-yield-oracle-first-261001.json` and `runtime/moving-yield-oracle-repeat-261001.json`.
- `runtime/moving-yield-ordered-first-261001.jsonl` and `runtime/moving-yield-ordered-repeat-261001.jsonl`.
- `runtime/moving-yield-ordered-strict-261001.json`.
- `moving-yield-corpus-accepted-v2-261001/corpus-results.json`.
- `runtime/moving-yield-ghidra-readback-261001.json` and `runtime/moving-yield-types-261001.json`.

Frozen asset-free original fixtures are `retail-moving-yield-1.27.json` and
`retail-public-moving-yield-1.27.json`; both production optimization levels are
checked. Missing group/resolver inputs, mutated states/writers/countdowns,
truncated captures and absent completion/provenance are rejected. Ghidra now
retains470 role names,191 verified prototypes,31 layouts,177 fields and44
globals; the four class collectors and blocker resolver have saved operand ABI.
The corpus contains233 entries:94 original oracles,108 archive audits and31
stronger live contracts, with86 pinned fixtures.

Remaining work is explicit: retail overlapping lazy cell-link insertion/stamp
order, group-bit8 and complete group-membership producers, caller restoration
and retry after a peer20 assignment, denied scheduler requests, oblique held
heading composition, multi-mover cycles and full stock crowd trajectories.
Engine cohort IDs represent the existing reimplementation group ownership;
the scalar decision port does not certify the original group producer.


The old collision fixture's lateral-distance comparison was an inference from
an earlier authored-speed slide policy. It pinned a peer after committing motion
but started its requester at rest; both variants now correctly wait. The fixture
now commits both actors and checks original4/20 identities and counters through
actual collision steering. A pinned peer still occupies the next step, so a
peer20 assignment alone does not prove immediate movement or the unported
caller retry. Independent save-field tests cover the wait counter, pointer fixup
and rejection of a misaligned blocker pointer.


Final validation: forced release RoC and TFT each pass80,425 assertions in2,244
cases; the collision encounter passes21 assertions and new save fields13.
The152 Python pathfinding checks, strict four-entry original/live corpus,
ability audit, backlog counts/IDs/area totals and local documentation links pass.
Umbrella `make test` and the release production `openwarcraft3` target pass
without C compiler warnings.

### Upstream synchronization after payoff30

Merged41 commits from `upstream/main` at `f7b34c7b63892f7ee53a9d4f2e8dad4229ee1f6b`.
Move retains the original pre-turn propagation gate, software native setters,
adaptive/refill state and ordered waits, while incorporating upstream's Stand
clip preservation and Walk resumption. Runtime propagation-window overrides
still require their explicit presence bit, including zero. The incoming
boundary-equality fixture now starts strictly inside its authored window;
equality is still a stop before turning. Incoming combat target/cooldown and
Cargo Drop state are persisted alongside movement through strict save70.
JASS snapshot7 and the network layout are unchanged.

Focused movement passes40,433 assertions in212 cases, native API24,199/342,
and save1,491/164. Forced release RoC and TFT each pass80,659/2,285; the debug
matrix with `WC3_DEBUG_BUILD=1` repeats those complete suites. Both umbrella
configurations and all three production targets (`openwarcraft3`, `opensc2`,
`openwow`) pass. An incoming SC2 filter's misleading-indentation warning was
fixed and its production target and umbrella checks repeated successfully.
The152 Python pathfinding checks,233-entry corpus admission and engine/menu
boundary audits pass. Merge validation artifacts are
`/tmp/wc3-upstream-{engine-full,umbrella,production,warning-fixed,debug-accepted}.log`.
The merge closes no additional research leaf:109 done/291 tasks,182 remaining.

### Waiting headings preserve the native caller

Payoff31 strengthens ROUTE-05.3's existing wait consumer. Complete original
`Mover_StepRoute`16fbd0 initializes its local destination from the final caller
goal. A nonzero165ae0 countdown leaves that vector untouched and returns1;
16fd3b then subtracts the predicted native fine source before selecting heading
and stopping speed. This differs from acquisition-time165c60, which retains
the current fine waypoint when it assigns a requester wait. Saved Ghidra
comments and source/destination operand storage now record both branches.

Two original replays cover1,008 full waiting callers across four native sources,
seven destinations, six facings, two turn rates and1/4/20 delays, without
replacing any routine. Every original speed/heading/countdown word matches
production C. Digest:
`c96d159d6bc411d7a66d44655df2041f455a27c846fe33a0db7c5d1e76d376ff`.
The frozen fixture is `retail-wait-heading-1.27.json`.

The engine's world subtraction reproduced eight failures at four map origins.
The native heading is3f08101b; rounded-world alternatives include3f081011 and
3f081019. Move now shares its retained prediction helper with repulsion and
forms the final-goal delta before world projection. The44-assertion regression
matches heading, stopped velocity, countdown1→0 and untouched native/world
pose. A224-assertion public JASS Move/save regression drives actual owner
frames, preserves four stopped oblique headings and resumes movement; all120
recorded state words repeat after load. Save70's existing native pose and wait
fields cover the change; neither serialized meaning nor the network changes.

Two fresh owned Frida crowd runs record424 complete16fbd0 countdown calls,
their nested165ae0 operands, actual parameters and outputs. Every call uses
the same final destination and native source before/after the gate, stops
speed and matches C heading exactly; normalized digest:
`033f2e18400b9ff2bb849d94961744acbeaee40f606f388c9a0fbfc840b3535c`.
The strict verifier rejects missing operands/provenance/completion, altered
inputs/outputs/gates/counters, truncation and observer errors. Asset-free tests
replay both original fixtures atO0 andO2. Sources and the owned crowd map remain
hash-pinned in `retail-public-wait-heading-1.27.json`.

Evidence under the report root:

- `runtime/wait-heading-{first,repeat}-261001.json`.
- `runtime/wait-heading-live-{first,repeat}-261001.jsonl` and `runtime/wait-heading-live-strict-261001.json`.
- `runtime/wait-heading-ghidra-readback-261001.json` and `runtime/wait-heading-types-261001.json`.
- `wait-heading-corpus-accepted-v2-261001/corpus-results.json`.

Ghidra retains470 named roles,31 layouts,177 fields,192 verified prototypes and44
globals. The corpus now contains237 entries:95 original contracts,110 archive
audits and32 live contracts, with88 pinned fixtures. This strengthens an
already completed leaf without inflating the backlog:109 done/291 tasks,
182 remaining. Original acquisition-time waypoint composition, overlapping
cell-link ordering, group-bit8/membership producers, peer retry, denied
admission and complete crowd/cycle trajectories remain open.

Final payoff31 validation: forced release RoC/TFT and forced debug with
`WC3_DEBUG_BUILD=1` each pass80,927 assertions in2,287 cases. The release
umbrella suite passes, including154 pathfinding Python checks. Forced release
and debug `openwarcraft3` builds pass without compiler warnings. The ability
audit,237-entry corpus inventory, exact four-entry fresh replay, backlog IDs/
counts/area totals and243 local documentation links pass.


## Payoff32: blocked fine-leg retry and retained coarse plan

`Move` now follows admitted original165c60 fine-progress retries: a nonempty
next-step blocker vector assigning peer20 without a requester wait restores the caller's final
goal, turns from its predicted native source, stops committed velocity, and
resets only fine count/index. The coarse count/index/points remain owned for
refill on the next thinker. Requester4 acquisition retains the fine waypoint;
peer20 acquisition reaches this retry branch. Empty admitted progress clears
retry/delay too, as original167070/1687e0 do. Move handles the held decision
before route-pending and legacy settle branches can bypass the velocity commit.

`wc3_pathing_retry.h` ports complete1689d0/167290 null-target scalar state.
**Source-minus-adjusted-goal** is intentional: software subtraction truncation
makes opposite operand orders differ at the144 threshold. Count2 consumes no
random draw when distance-squared<=144 and unsigned group count<=1; otherwise
1d62b0 takes bit22 of the owner's next word for7/8. Public GetRandomInt instead
uses multiply-high and is not this operation. Consumption retains count1 and
buffers for result4; all other counts clear only fine buffers and decrement for
result1. Production currently uses the admitted-progress result1 transaction;
index0 terminal/perimeter and direct-flow owner transitions remain open.
Engine cohorts supply group members until the original membership producer is
ported; this does not certify original groups or multi-unit cycles.

Complete original registered-group oracle336 initializations and2016 transitions
match production C, including owner words and native threshold boundaries:
`runtime/peer-retry-full-engine-first-261001.json`, sequence
`0365fb3e93fd856a73f2944858a0c465e802bad1bd862e712d4a74fd5a91cc09`.
The frozen `retail-peer-retry-1.27.json` runs at O0/O2 without assets.
The actual wall-detour regression first failed six movement/buffer assertions;
now20/20 pass. The public JASS order regression runs120 actual scheduler ticks,
then repeats after save/load:1467 assertions and1440 exact recorded state words,
including positions, committed velocities, facing, retry/order and owner PRNG.
Both actors resume, peer delay expires, and requester progress exceeds64 units.
Save71 persists retry_count through the normal field table and rejects save70;
network and JASS snapshot formats are unchanged.

Ghidra `WC3PathPrefix+98` is now typed retry_count, and1689d0/167290 have explicit
ECX-path/stack4-source/RET4 ABI. The472 names retain31 layouts,178 fields,196 prototypes and44 globals;
070850 source-minus-point distance and1d62b0 mantissa-range draws now have saved
roles and exact fastcall/stdcall storage, preventing renewed numerical confusion. This strengthens ROUTE-05.3 rather than
closing full cycle resolution, group-bit8, target perimeter or denied scheduling.


The broader regression suite exposed the remaining source-admission dependency:
a stationary16-radius actor at60,0 overlaps the mover's quantized fine footprint
at0,0 although their collision circles do not touch. Unconditionally retrying
all nonempty vectors makes this legacy admitted source refill indefinitely.
Payoff32 therefore ports **actual peer20 assignments on an admitted fine leg**;
other terrain/idle/ineligible vectors retain existing engine steering pending
original footprint admission/recovery. The existing idle-detour regression
remains required. This is explicit ROUTE-03 scope, not full165c60 parity.


Read-only Frida retry inputs now retain source/adjusted-goal words, actual
registered group size and owner PRNG before/after. Two complete owned crowd
captures each replay42 initializations and55 transitions, including eight
terminal count1 calls, with exact digest
`b22624f045c5916d77deae4f2c07ed815a42a577796e6851175d5eb89331068a`:
`runtime/peer-retry-live-{first,repeat}-261001.jsonl` and
`runtime/peer-retry-live-strict-261001.json`. Observer SHA256
`32c10a2c9cf70d0cff5bf6f480256f65ed004629e22548d5d13d9db21dfd71b0`;
map/creator identities remain the recorded crowd source. The corpus has240
entries,95 original contracts,112 archive audits,33 live contracts and90 fixtures.
The saved Ghidra ABI/field readback is
`runtime/peer-retry-ghidra-readback-final-261001.json`.


Final payoff32 checks: release and forced debug (`WC3_DEBUG_BUILD=1`) RoC/TFT
both pass82423 assertions in2290 engine tests, including the idle detour and
live retry field/version rejection. Forced release production and test binaries
build cleanly; `make BUILD=release test` passes the umbrella. All156 Python
pathfinding tests, the fresh four-entry retry corpus,240-entry inventory,
ability audit, unchanged109/291 backlog accounting and relative file links pass.
No engine/client/network boundary changed. Original full retry kernels are exact
within their supplied null-target inputs; production coverage remains the
peer20 admitted fine-leg transaction described above.

## Public spawn admission and initial mover pose

Engine payoff33 replaces public `CreateUnit`/`CreateUnitAtLoc`'s approximate
64-world-unit collision-circle spiral with the recovered32-ring, policy2 fine
footprint admission. `unit_create` delegates position initialization to Move;
Move preserves the fresh mover's scalar cancellation, publishes/supports the
accepted point and retains native fine coordinates for subsequent movement.
Item drops, cargo, summons and Way Gate placement still have separately tracked
producers; this port does not silently grant them the public spawn contract.

Two owned scene44 captures create eight ground Footmen beside stationary source
`(-1936,-976)`, at east offsets0,16,31,32,60,63.9999,64,96. Each brackets the
public create/getter calls, then issues an accepted Move for the new unit.
All300 samples and completion markers are present. Each capture contains eight
complete placement calls, sixteen position writes,104 predicted world queries,
56 public native calls and239 movement decisions. The searches use current mask
`02000002`, radius`3f780000` (31/32), callback654060/context6, limit32 and policy2.
Source/self identity and each first accepted candidate remain distinguishable.

| Requested world X | Accepted public XY | Retained native XY |
| ---: | --- | --- |
| -1936 | (-1968,-1040) | (162.5,63.5) |
| -1920 | (-1872,-1008) | (165.5,64.5) |
| -1905 | (-1872,-1008) | (165.5,64.5) |
| -1904 | (-1872,-1008) | (165.5,64.5) |
| -1876 | (-1876,-976) | (165.375,65.5) |
| c4ea0001 (-1872.0001220703125) | (-1872.25,-976) | (165.4921875,65.5) |
| -1872 | (-1872,-976) | (165.5,65.5) |
| -1840 | (-1840,-976) | (166.5,65.5) |

The fractional row exposes why placement alone is insufficient. Registered
native1fc930 calls64e650, which calls the unit factory677870 with flags502,1,-1.
Mover activation15ed40 seeds both fine axes from immutablece4584:
`c7fa0040`, **-128000.5**. The first05c200 world write has notify0 and maximum
cap`7f7fffff`; it computes requested-minus-sentinel then adds that delta back.
The accepted fine point`43257fff` first projects to world`c4ea0004`, then
cancellation commits fine`43257e00`, publishing world`c4ea0800`. A second notified
world write consumes that canonical pair. Inverting the original request into
an already initialized pose skips this observable rounding. Production
`wc3_grid_spawn_place` retains the original order of both writes.

`verify_wc3_spawn_trace.py` strictly checks owned binary/source/map identities,
completion/counts, created/source actor relationships, public getters and write
ordering. It replays footprint visits against the stationary actor's equivalent
class1 occupied cells, all eight endpoints, sixteen complete scalar commits,
104 read-only queries and239 movement decisions through production C. Both
captures have identical normalized spawn digest
`860c89180e22330fdba167e727887383550d82c8650f46c27d177e54cfef99b5` and motion digest
`5611670c6339529325e4ba121128e3768194c4b717606e7193478a5cf84a0368`.
The accepted reports are `runtime/spawn-admission-live-accepted-261001.json` and
the fresh corpus report; raw captures are
`runtime/spawn-admission-live-{first,repeat}-261001.jsonl`. The observer source is
`1fb3edeecf602d6e0b8611e93839b0446165f92b4536e4ea7c5b9307f3f95bdd`; map is
`bc2f1aaf9040bb6d4e67cdf13fc5db48ee622fd2e63492b5e11542898f9b819f`.
The frozen `retail-public-spawn-1.27.json` preserves raw evidence and negative
checks at O0/O2, including coherent input, sentinel, getter, commit and candidate
mutations after recomputing the sequence checksum.

The public engine regression initially reproduced35 failures. After porting,
all eight captured destination/native-pose pairs pass through actual JASS
CreateUnit/AtLoc. A custom authored collision row supplies the captured31-unit
footprint to the visible route fixture; an8-unit nonstock control must instead
select the closer class0 cell. The fractional actor saves immediately
after creation, issues public Move and runs80 real scheduler frames, then repeats
after loading:640 continuation state words match exactly and both runs travel
more than64 world units. Initial test source uses the original JASS expression
`-1936+63.9999`; rewriting it as a rounded decimal literal changes the producer
input. Save71, callback identities and the serialized meaning of retained fine
pose remain unchanged; no save migration or new network contract is added.

Ghidra persists three new roles/ABIs: `Jass_CreateUnit`1fc930 (cdecl five stack
arguments/EAX handle), `Unit_CreatePublic`64e650 (fastcall player/rawcode plus
three scalar-pointer stack arguments, RETc/EAX payload), and
`Mover_ActivateSpatialPose`15ed40 (thiscall context, RET4). The sentinel is a typed
named scalar, with instruction xrefs retained. Saved type readback is
`runtime/spawn-admission-types-261001.json` plus persisted role/constant check
`runtime/spawn-admission-ghidra-readback-261001.json`:475 roles,199 prototypes,31 partial
layouts/178 fields and45 globals. These descriptive names are evidence labels.

FOOT-04.6 closes this ordinary in-map ground public spawn producer and its engine
consumer. Original Loc, buildings/other factory flags, outside-map clipping,
bridge/rejected-level callbacks and full physical crowd/group cadence remain
FOOT-04.1/02, BASE-02, MAP and E2E work. Exact public spawn outputs do not certify
whole retail trajectories.

Final payoff33 validation: public spawn regression passes783 assertions across
the eight original endpoints and the nonstock footprint, including640 saved
continuation words. Optimized and forced debug (`WC3_DEBUG_BUILD=1`) RoC/TFT
both pass83,207 assertions in2,291 engine tests. Forced release production/test
builds and the complete `make BUILD=release test` umbrella pass. All157 Python
pathfinding checks pass, including ten negative variants at O0/O2. The fresh
three-entry spawn corpus,243-entry/91-fixture inventory, ability audit,
110/292 backlog counts and224 relative file links pass. Full logs are indexed in
`runtime/spawn-admission-validation-261001.json`; no shared engine/client boundary
or network format changed. The following section extends this initial-pose
evidence to actual post-spawn scheduled movement; broader source admission
and original group/cycle scheduling remain separate requirements.


## Clear public routes retain native waypoints

Engine payoff34 removes the ordinary location-order clear-line heading shortcut.
Original `Mover_StepRoute`16fbd0 runs `Path_Advance` even in a clear corridor;
it subtracts the predicted native fine source from the selected fine waypoint
before16f630. A fine search may observe a blocked neighbour while its forward
corridor remains legal, causing the retained initial index to be `count-2`.
Steering directly toward the final world destination loses that real waypoint
and also reverses the native-to-world publication before heading evaluation.
Move now enters its existing retained fine/adaptive route for a clear ordinary
location order. Interaction abilities still own their approach/range policies.
Disconnected static routes retain the existing component fallback.

The admitted fine leg also survives static sample-cell changes as its source
moves. Original165ae0/165c60 consumes progress and collects the next step;
it does not rerun a full static segment from every fractional source. The old
engine recheck rebuilt the route at the sixth fractional commit, replacing
its admitted final waypoint with a new parent. Move now uses the saved route's
terrain epoch/mask and retains the existing live occupancy check through
`G_UnitMoveFineRouteIsUnoccupied`, with native source/waypoint inputs.
Static edits still invalidate through the bake epoch. The broader original
nonempty blocker retry remains ROUTE-03: the existing full live peer segment is
explicitly marked as the remaining engine approximation, and its idle-insertion
regression remains required. No new saved field or callback is introduced.

Two complete scene44 captures with `--motion-events --velocity-events
--profile-events` retain239 movement decisions,247 velocity/position/facing
commits,247 predicted arrivals and eight final natural arrival stops. The eight
public CreateUnit lifetimes use stock Footman speed270 (fine cap`41070000`),
turn0.6 and authored60-degree propagation window. Original scene samples refer
to the stationary source, so its public X/Y markers do not describe the moving
actor; the moving trajectory comes from the raw commits. Mover addresses are
reused between actors, and markers delimit each lifetime.

The original fractional journey starts from native `(165.4921875,65.5)` and
initially steers to `(166.5,65.5)`, not final `(173,65.5)`. Its first requested
heading is0; commit publishes velocity`4106ff7b,00000000` and facing`3b8f1bc4`.
The old world shortcut produced79 failures in183 engine assertions. Retained
native routing matches all29 scheduled commits. The final committed world point
is approximately `(-1645.501953125,-975.993408203125)`; it integrates the previous
velocity and stops within the original arrival range without snapping to-1632.
The tiny alternating headings and sideways velocities are measured software
arithmetic, not noise to smooth away.

`public_spawn_move_matches_retained_retail_motion_and_resumes` drives actual
JASS CreateUnit/IssuePointOrder and the production RunFrame scheduler, comparing
clock/native position/velocity/facing against the frozen captured words. Each
initial clock/primary phase is a supplied measured control; this does not close
NUM-02.3's full original admission-phase producer. The fractional journey also
saves after its eighth commit and reloads for the remaining original trajectory.
The custom visible unit row supplies captured radius31 and speed270.

Scene44 retains Human02Interlude's terrain. The initial empty test map was a
valid control for the fractional journey, but its other trajectories diverged
when segment skipping crossed real blocked cells. The regression now supplies
the derived ground walk-bit rectangle x152..183/y56..71 from the pinned original
WPM, with original384×256 dimensions and origin`(-7168,-3072)`. This small control
is recorded in the fixture and checked against the C table. It does not claim
parity for unrepresented terrain cells. Altering heading arithmetic to fit the
empty-map control would have hidden a scene mismatch.

`verify_wc3_spawn_motion_trace.py` replays all original lifetimes and checks the
initial/next/terminal state chains, range publication and predicted arrival
against production C. Both complete captures share journey digest
`c9bdf0f74634db0555f2625776af7d97d8838997999efd5a6f8c37c6a593bf96` and velocity digest
`3c3033e448af26248d72c1b53d06e24a27afa0cdaaca034f87e55c4470d8e9f4`.
The frozen fixture and strict negative tests execute at O0/O2. Ghidra's existing
16fbd0 role retains this clear-route consumer finding; no new speculative ABI
or layout is needed. Save71 and network layouts remain unchanged: the existing
retained route/pose representation now receives the original movement decisions.


Final payoff34 validation: the public spawn-to-Move regression passes1,686
assertions, including all247 original commit rows and21 original saved
continuation rows. Release and forced debug (`WC3_DEBUG_BUILD=1`) RoC/TFT each
pass84,904 assertions in2,292 tests. Forced release production/test builds and
`make BUILD=release test` pass. All159 Python checks pass, including twelve
corrupt/incomplete lifetime variants at O0/O2; motion decisions are paired with
their own mover, old-vector speed and following requested heading. The fresh
three-entry producer corpus,246-entry/92-fixture inventory, ability audit,
110/292 backlog count, relative file links and diff checks pass. Logs are indexed
in `runtime/spawn-motion-validation-261001.json`. This strengthens ROUTE-01.3 and
NUM-02.3's partial evidence without declaring the initial clock producer or full
dynamic crowd replacement complete.

## Public timer admission reaches Move from zero

Payoff35 removes the supplied per-actor clock/phase from the eight public
spawn-to-Move journey checks. The engine starts at primary clock0/phase0 and
uses ordinary JASS TimerStart(0.1,true), RemoveUnit, CreateUnit and IssuePointOrder.
The existing scene44-derived WPM clip and stock-shaped31-radius/270-speed row
remain explicit test controls. Every one of247 committed clock, native position,
velocity and facing rows matches the original journey fixture.

Two engine bugs were reproduced. G_RunTimers subtracted100ms on every drain,
although RunFrame now drains before each5ms primary advance and at publication.
It now consumes actual elapsed simulation time once through a retained cursor.
Within a shared primary quantum, the engine also ran authored timers before the movement
owner. The captured original heap runs the owner first: births420/1620/2820
see70/270/470 completed owner updates. Running a fresh mover immediately at its
birth timestamp advanced three journeys one owner update early; changing that
relative order removes398 word/count failures in the complete regression.

Both new owned150-second traces use the unchanged map/source hashes. They
contain6000 completed primary advances,1000 owner callbacks,300 scenario samples,
eight public births and247 movement commits. Primary digest remains
`c74157bbda9aef31e4c3318fa5978575f25be617420529584a2f7f63e19055ae`;
movement digest remains
`c9bdf0f74634db0555f2625776af7d97d8838997999efd5a6f8c37c6a593bf96`.
The strict phase checker validates admission inside the primary source, both
position writes, owner counts and the phase sequence2/0/4/2/0/4/2/0.

Save72 serializes the timer cursor normally with the restored server clock.
Elapsed-time, repeated-drain, Pause/Resume, restart, destroy and callback-restart
tests cover the changed timer paths. A save during the fractional public journey
also resumes the authored timer and the later public actor lifetimes, matching76
additional original commits. Debug RoC/TFT each pass87,244 assertions in2295 tests. Ghidra
persists registered TimerStart216c70 and ScriptTimer_Start249ca0 with explicit
stack/register ABIs; the existing primary source and timer-drain comments retain
the new observed relative order. Total477 roles,31 layouts/178 fields,201
prototypes and45 globals.

This closes the ordinary scene's public admission producer. TimerStart still
converts scalar timeout to integer milliseconds: general scalar heap deadlines,
periodic rearm/zero-period policy, epoch handling and other producer profiles
remain required. The test's explicit remainder parentheses avoid the engine's
currently right-associated chained subtraction; full compiled expression grammar
and its pathfinding consumers remain separate work.

Validation: optimized RoC/TFT also each pass87,244 assertions in2295 tests;
forced debug/release production builds and the ordinary `make test` umbrella
pass. All160 pathfinding Python checks,249-entry/93-fixture inventory and the
three fresh strict phase corpus reports pass. Ghidra readback reports the
program saved with the four relevant roles/prototypes intact.

## Public oblique Move retains the singleton group destination

Payoff36 moves from supplied route phases to three ordinary public lifetimes:
CreateUnit, SetUnitMoveSpeed and IssuePointOrder run through a periodic0.1s
JASS timer, with engine clock/phase initially zero. The original stock-shaped
Footman row has radius31, speed270, turn0.6 and propagation window60 degrees.
Captured Misc bounds150..400 clamp the requested100 speed to150; the minimal
fixture's startup1..522 bounds are not silently substituted.

Scene45 starts actors at(-1936,-976),(-0.125,-0.125) and
(-6000.125,-1500.25), at timer ticks1/101/201. Public placement's scalar
cancellation is retained, including the near-zero(-0.25,-0.25) result. Two
complete owned170-second captures agree on6000 primary advances,1000 owner
updates, three births,684 movement decisions and689 velocity/pose/facing/clock
commits. Births occur after20/2020/4020 advances, at phases2/4/0.
A third read-only geometry capture preserves those same motion/primary words.
The strict journey digest is
`5724e6aad3914d60fe2b988fc61bbe4b84e117d8ccd2aa23a619147b9ac4c913`.

The missing engine layer was the group-owned adaptive plan. Original16ce10
requests it with5000 work, and1697a0 advances its ten-accelerator-unit selector
before167d70 publishes the group destination.16a790 passes the selected member
point at+18 into16fbd0; the member then builds its separate400-work adaptive
and700-work fine paths. A single public order therefore contains these stages:

| Journey | Group chain | First member destination | Natural zero commits |
| --- | --- | --- | --- |
| Baseline |4 points, selected index0|final174/91.5|commit205|
| Near zero |9 points, selected index1|233.5/113.5, then238.99609375/122.0078125|commits159/232|
| Large coordinates |6 points, selected index1|49.5/65.5, then61.48928125/69.984375|commits158/249|

Move now retains that group chain separately from the member's adaptive/fine
buffers. Intermediate arrival integrates the previous velocity, publishes
zero, advances the group destination after the commit, and refills the member
route on its next owner update. The issued final goal and user order remain
active. Both intermediate stops require this actual handoff; continuously
consuming the member's coarse points does not reproduce it. The three public
journeys initially exposed1533 word/count failures. The resulting engine
regression matches every original commit and repeats260 further original
commits after saves during the large-coordinate first leg and immediately
after its intermediate stop.

Geometry is explicit. The first diagnostic near-zero source was isolated in a
21-cell class1 terrain component, so its eight stationary commits and forced
arrival do not certify a natural long trajectory. Scene45 opens a24×36-cell
terrain corridor before starting the timer. Read-only1489a0 eligibility decoding
shows32 scenery-blocked cells remain inside it; terrain edits cannot remove
those object records. The regression supplies the observed terrain-plus-static
walkability bitmap. Omitting that scenery changed the first heading and created
a212-commit route in place of233. This proves the numerical/routing composition
under supplied scene geometry; it does not close the original scene-loading,
cell-link chronology or stale-hierarchy invalidation producers.

Replacement orders clear the retained group plan. The existing item-approach
regression caught an internal approach inheriting an older point destination;
cleanup belongs to Move's normal progress reset. Save73 serializes group
points/count/index/final native goal/radius, rebinds the rebuilt world epoch,
rejects72 and older formats, and rejects malformed/truncated group tails.
[The engine regression](../../../games/warcraft-3/game/tests/t_movement.c) and
[observed words/geometry](../../../tools/ghidra/fixtures/retail-public-oblique-1.27.json)
remain paired with the strict fresh corpus entry.

This is ordinary singleton point movement. Shared multi-member group ownership,
formation admission, all-member decision/commit phases, moving targets, general
timer deadlines, other profiles and arbitrary map/scenery producers remain
open. Ghidra persists478 roles and202 explicit prototypes, including the
instruction-checked165e30 selector wrapper and live group/member destination
annotations;31 layouts/178 fields and45 globals remain unchanged.

Validation: optimized and forced debug RoC/TFT each pass104,227 assertions in
2297 tests; forced release/debug production builds also pass. All162
pathfinding Python checks and the253-entry/94-fixture inventory pass. Fresh
`public-oblique-corpus-final-261001` passes all four requested entries,
including the third geometry observer. Persistent Ghidra names/prototypes and
saved program are read back in the paired runtime reports.


## Public group point orders admit twelve members

Payoff37 closes GROUP-01.3's public admission boundary. OpenRealm's name and
Loc group point orders previously walked the entire mutable JASS group; the
numeric forms always returned false. The new common dispatch snapshots at most
12 insertion-order entries, rejects removed/reused identities and executes the
ordinary order path. Numeric IDs resolve through the same order table; numeric
building rawcodes use the existing construction dispatch. Null groups/locations
and unrecognized orders return false. This changes no persistent layout or
serialized state; payoff37 preserved the then-current Save73. Payoff38 below advances it to74.

The scene46 map creates fourteen stock Footmen with mixed public speeds. At
periodic ticks10/80/150/220 it calls name/ById/Loc/ByIdLoc respectively and
queries every member's current order. Both complete owned captures show twelve
Move orders and two untouched idle units on every call:48 admissions and eight
exclusions. The callbacks first attach all twelve units to one nonnull CMoveReq,
then validate/admit those twelve in the same order. Captured callerflags0 and
resolved orderflags6 are retained; no numerical inference substitutes for the
original native wrappers. The admission digest is
`348c40f6e3e16f36b6601ad3bfbf89cc1e3dc5513a7f6ca596e876447aba7a81`.

Reports under the standard report root:

- `runtime/group-orders-live-first-261001.jsonl` and `group-orders-live-repeat-261001.jsonl`;
- `runtime/group-point-admission-repeat-261001.json`;
- `runtime/group-point-ghidra-types-261001.json`;
- `group-point-corpus-261001/corpus-results.json`.

`verify_wc3_group_point_trace.py` checks actual metadata/source/map hashes,
complete scene markers, every normalized request/callback phase/scalar word,
one shared request per call and every public member-order query. The frozen
`retail-group-point-admission-1.27.json` is independently pinned by the corpus.
Negative checks reject changed words, missing callbacks, different snapshot
order, independent requests, provenance changes and over-limit admission.
The engine test `group_point_order_forms_admit_twelve_members` reproduces the
original over-limit bug before the fix and passes all four forms afterward.

Ghidra now retains487 named roles,211 explicit prototypes,32 partial layouts,
186 fields and45 globals. New names cover the four public wrappers, CGroup
point producer, retained snapshot enumeration and its attach/admit callbacks.
The44-byte point context describes only proven fields; unresolved bytes remain
undefined. [Payoff38](#public-pair-movement-uses-a-shared-move-owner) subsequently integrates ordinary public shared movement; wider producer/eligibility/queue state remains GROUP-04.6. These captures hit the
intentional10000 motion-row cap; only admission is certified, and the final
30-second scene still has an active order. This is not arrival/trajectory
parity evidence.

Validation: release and forced debug RoC/TFT each pass104,228 assertions in
2,298 tests. Both production builds,165 Python pathfinding tests, ability
registration coverage,254-entry/95-fixture inventory and the fresh strict
admission corpus and ordinary release umbrella pass.


## Public pair movement uses a shared Move owner

Payoff38 splits GROUP-04.10 from the larger04.6 task. A public point batch now
reaches the registered order ability through `A_GROUP_POINT_ORDER` and a typed,
generation-retaining request. `Amov` owns the Move/Smart order names. The JASS
adapter snapshots twelve insertion-order entries; it contains no hardcoded
movement IDs or movement-specific dispatch. Other point orders retain ordinary
per-unit admission, including building rawcodes, spells, Patrol and Attack.
Move/Smart still use that admission for validation, Smart rally behavior and
issued-order events before accepted walkers join the physical owner.

Move retains each group's separate5000-work route, public final goal, selected
native point/heading and member rows. Members retain generation, native offsets
and destinations, requested speed/heading, arrival flags and forced-arrival
state. The owner stages every decision before publishing any velocity. Each
decision uses its own speed; eligible commits use the group's minimum speed.
Replacement/removal detaches members by generation with swap removal. Destroying
the JASS collection does not cancel physical movement. Final completion removes
members after commits; an intermediate shared point uses verified regrouping,
resets member paths and refreshes formation before the next owner visit.
Paused/stunned members retain their existing pause semantics; Walk/Stand clips
follow the actual movement request.

Scene47 creates two stock Footmen at `(-1936,-976)` and `(-1856,-976)`, facing
north, sets public speeds100/350 (retail clamps100 to150), and issues one
`GroupPointOrder("move",-1936,-720)` at periodic tick10. Two sequential owned
captures contain60 complete group visits,113 decisions,115 commits (60/55),
240 phase snapshots,6000 primary advances and601 public markers. Both units
arrive naturally with order0; the slow member ends at its retained left slot,
not the public group centre. The captures end normally without capped motion.

The initial formation heading is selected group point minus previous group
point, rather than the member mean. Initial native offsets are
`[-2.937499761581421,1.367880031466484e-9]` and its inverse. The left assigned
native destination `[160.5625,73.5]` is blocked; its fine search retains the
nearest endpoint `[161.5,73.5]`. After the fast member finishes, the survivor
keeps its original slot. Its final failed leg exercises retry2→1→4, a one-point
nearest chain, zero speed while turning toward the exact assigned destination,
and forced arrival on the following visit. These are runtime branch results,
not hardcoded tick positions or expected-output shortcuts in production.

The normal-frame engine regression starts clock/phase zero and uses public
creation, speed setters, the timer and group order. RED had1537 word/count/order
failures; the port matches all115 clock/position/velocity/facing commits.
The compared retained ordinary group/member words (flags, age/counter, selected point,
slots, destinations, requested speeds/headings and forced-arrival state) also
agree before the next owner visit. That stronger comparison exposed a one-bit
initial requested heading error hidden by velocity canonicalization: public
creation used host degree conversion. `unit_create` now consumes the observed
software multiplier `cd5444=3c8efa35`. Saves after commit30 and113 replay87
further original commits exactly. Format74
retains the physical registry through stable edict indices, generations and
bounded finite route tails; earlier versions are rejected. Corrupt owner,
member/reference/generation/duplicate and curve payloads fail visibly and free
partial allocations, even when a mapped record contains serialized addresses.
Network state and wire layouts are unchanged.

The fixture supplies the already observed scene45 combined terrain/static
geometry; the scene45 remote clear control does not intersect this route.
Arbitrary scenery construction, selected/AI producers, queued group activation,
all eligibility/shared-override bits, cooldown/target-speed policy, callback
mutation during complete physical motion and general crowds remain04.6/MAP.
Inter-group owner traversal and reused pool-slot ordering also remain unverified;
the bounded pair owns only one physical group. The current cap accumulator
uses zero as an unset sentinel; test whether a movement-disabled retained member
can reach this path before certifying that admission domain. Public speed0 by
itself is clamped to the configured minimum and does not prove a zero cap.
Existing-actor
`unit_createorfind` facing conversion is a separate producer and remains
unverified. Ordinary Attack point dispatch still has a pre-existing stale
`GetUnitCurrentOrder` result after Patrol; the ability-ownership regression checks
the actual Attack procedure and waypoint, without granting that query parity.
The long shared-route/pause engine regression checks lifecycle without granting
an additional original trajectory claim.

Artifacts under the standard report root:

- `runtime/public-pair-live-first-261001.jsonl` and `public-pair-live-repeat-261001.jsonl`;
- `runtime/public-pair-repeat-final-261001.json`;
- `runtime/public-pair-source-261001/` retains the exact capture sources;
- `runtime/public-pair-ghidra-{types,readback}-261001.json`;
- `public-pair-corpus-261001/corpus-results.json`.

`runtime/public-pair-repeat-startup-rejected-261001.jsonl` records an accidental
controller overlap at startup. It is diagnostic only; both accepted captures
ran to completion sequentially. The strict checker pins actual source/map and
capture metadata, all sampled words, every normalized phase/member/pose word,
all-member-before-commit order and natural final orders. It composes production
scalar/velocity/clock arithmetic. The separate engine test proves routing and
normal-frame integration; raw helper replay alone does not prove that.

Frozen [pair words](../../../tools/ghidra/fixtures/retail-public-pair-1.27.json)
and [engine trajectory](../../../games/warcraft-3/game/tests/retail_public_pair.h)
are synchronized by Python checks. Ghidra retains489 roles,213 explicit
prototypes,32 partial layouts/186 fields and45 globals. The newly typed
`16c6d0` chooses a predicted native source member with strict ties; preferred
path88.200000 and group200 bypass extensions remain explicitly unimplemented.
Nine saved role/prototype/comment readbacks retain this payoff's evidence.


Final payoff38 validation: forced release and forced debug with
`WC3_DEBUG_BUILD=1` RoC/TFT each pass108,320 assertions in2,302 tests. The public
pair regression passes3,957 assertions, including87 saved continuation commits;
the separate long-route/pause lifecycle and fourteen malformed physical-group
payload cases pass. Both production builds, the normal release repository suite,
168 Python pathfinding tests, ability registration audit and255-entry/96-fixture
inventory pass. Fresh strict pair replay and nine saved Ghidra readbacks pass.
The validation index is `runtime/public-pair-validation-261001.json`.
GROUP-04.10 closes; next runnable work remains GROUP-04.6's wider physical groups.


## Twelve-member public group retains fine admission and committed occupancy

Scene48 creates twelve stock Footmen in a4×3 arrangement with authored move
speeds100/200/300 (the first clamps to150), then issues one public point group
at timer tick10. Two corrected, independently owned retail captures repeat
all7,035 canonical phase/motion records and3,601 public samples exactly.
There are297 complete owner passes,1,941 decisions and1,953 velocity commits;
per-member commit counts are297/138/190/102/135/126/88/243/158/111/160/205.
All twelve public orders finish naturally, including retail partial-route
forced arrivals; these final positions are not proof that every member reached
the group centre.

The strict fixture and verifier are
`tools/ghidra/fixtures/retail-public-twelve-1.27.json` and
`tools/frida/verify_wc3_public_twelve_trace.py`. The production arithmetic replay
matches every captured commit and the6,000 primary advances/1,000 owner
callbacks. Separately, the whole-engine regression
`wc3_movement.periodic_public_twelve_members_match_retail` now matches all1,953
seven-word member/clock/native-position/native-velocity/facing records through
normal5ms server frames. Saves after the first12 commits (member11 queued) and
commit1,260 (partial-route recovery) replay2,634 further original commits.
Every member's public order ends at0 in each run. Frozen expectations were not
changed to accommodate any engine mismatch.

Production Move now consumes these original contracts:

- `16e250` adjusts formation offsets using a separate30-attempt adaptive
  distance query. `1627e0`/`163440` sum integer parent-edge lengths; this is not
  A* cost divided by12. Query size is an exponent. Four classes/four lanes and
  eight offsets on three supplied64-cell maps give384 exact original/C cases.
- Eligible group members acquire temporary fine-object40000000 before all
  decisions. Fine search suppresses these objects; endpoint queries retain
  them. Fine occupancy uses peers' last committed native poses, while published
  world positions may already contain predicted presentation samples.
- Ordinary class0 fine requests share an1100-work FIFO. Equality admits a full
  request; node pops charge the work, including the denied701st pop. Original
  `167fa0` resets work on countdown0 and reloads1. The first eleven requests
  charge1723 pops, leaving member11 queued for the next owner visit.
- `157610` initializes the owner visit counter to0x400; `15aa80` increments once
  per30ms owner update and reloads0x400 on unsigned wrap. `168910` requires ten
  visits between searches. FIFO denial and cumulative work below64 clear the
  fine request timestamp. These are owner visits, not elapsed milliseconds.
- Empty next-step candidate vectors still call `168360`, clearing stale blocker
  identity. Any nonempty admitted vector can trigger retry, including a
  stationary peer. A self-yield retains the fine waypoint on acquisition;
  later countdown visits retain the assigned destination.
- Reached partial endpoints consume retry, clear fine count/index and defer
  refill until the next owner visit. Refilling immediately shifted the request
  timestamp by one visit and first diverged at commit1,263. Whole-leg dynamic
  resampling and a second world-space slide search also changed original
  headings; retained fine routes now own their next-step steering.

Save75 persists owner counter, work/countdown, FIFO head/tail/count, intrusive
member links and request timestamps through the ordinary DDX `F_EDICT` fixups.
The real saved trajectory covers a pending queue and later recovery. Twelve
invalid FIFO graphs fail visibly; old Save74 is rejected. Map teardown,
replacement and unit reclamation clear links through Move. Spell approach and
all-class tiny-cell detour regressions now advance normal server frames, so
fine retry admission sees the real owner clock. This exposed the legacy stuck
guard's one-world-unit progress floor: speed2 steps at30ms could not beat it
within eight visits. The floor is now bounded by the actual step budget; the
full original retry/task replacement remains required.

The locked startup branch is also a production producer: original `29e300`
seeds owner random with0x77617233 before `1e9dd0` resolves twelve race
preferences. Four fixed Human/eight random preferences in this capture consume
eight owner draws, producing4273436052/209508436 before path retries. The engine
executes that preference loop; it does not burn a fixed number of draws or
install the captured final state. Independent seed captures repeat this chain.
Unlocked/lobby seed, the default lock-flag producer and canonical player
payload78 remain NUM-04.5; this witness explicitly supplies the observed locked
setup.

The claim remains bounded to an ordinary class0 public twelve-member group and
supplied scene terrain/static geometry. Other player classes and policy pools,
selected/AI producers, queued physical activation, all eligibility/override
bits, overlapping cell-link insertion order, arbitrary scenery/map construction,
inter-group scheduling and general crowd scenarios remain open. Matching forced
partial arrivals does not imply that all units reach the common centre.

Artifacts under the standard report root:

- `runtime/public-twelve-fixed-{first,repeat}-261001.jsonl`, immutable sources
  `runtime/public-twelve-source-261001/`, and `public-twelve-arithmetic-261001.json`;
- `runtime/public-twelve-seed-{first,repeat}-261001.jsonl` and
  `public-twelve-startup-evidence-261001.json`;
- `runtime/public-twelve-step-{first,repeat}-261002.jsonl` and
  `public-twelve-step-evidence-261002.json`: a read-only `16fbd0` route-step hook
  repeats all1,953 inputs/results and7,035 canonical rows. Extra trailing clock
  samples from capture duration are outside this separate step witness; the
  fixed capture's strict6,000-primary-advance contract is unchanged;
- `runtime/formation-adjust-engine-size-261002.json` and fresh
  `runtime/public-twelve-strict-fixed-261002/corpus-results.json`;
- `runtime/public-twelve-types-final-261002.json` and
  `public-twelve-ghidra-readback-261002.json`:493 persisted roles,35 partial
  layouts/201 fields,220 explicit x86 prototypes and45 globals, including11
  saved role/prototype/comment readbacks for this payoff.

The first strict formation runner attempt passed the twelve-member replay but
rejected an incorrect `--engine-library` argument; the actual refill tool uses
`--engine`. Its failed directory is retained separately; only the fresh fixed
runner's two verified outcomes certify this change.

The corrected scene excludes the inherited tick10 singleton order. Its first
attempt accidentally issued that order too, replacing member0 and creating
two physical groups; that capture is retained as
`runtime/public-twelve-mixed-order-diagnostic-261001.jsonl` with a separately
pinned source archive and is excluded from the parity fixture.

Final payoff39 validation: forced release and forced debug with
`WC3_DEBUG_BUILD=1` RoC/TFT each pass172,645 assertions in2,305 tests. Both
production builds, the normal repository suite,172 Python pathfinding tests,
ability registration audit and257-entry/98-fixture corpus inventory pass.
Fresh strict twelve-member and formation-distance outcomes pass. Eleven saved
Ghidra role/prototype/comment readbacks and all35 layouts/201 fields/220 explicit
prototypes pass. Test fixture declarations were moved before test functions;
the subsequent release RoC/TFT matrix retains the same exact result.
The validation index is `runtime/public-twelve-validation-261002.json`.
GROUP-04.11 closes. Next runnable work is SCHED-03/04: partition the ordinary
fine admission FIFO/work by player class and preserve class changes/reclamation;
GROUP-04.6 retains the wider physical group requirements.


## Ordinary fine search belongs to the unit player

Payoff40 ports the original sixteen player rows to the ordinary fine-search
consumer. Player0 exhaustion no longer holds Player1 requests in a global FIFO.
Each row retains work, reset countdown, head, tail and count. Move charges the
row selected at admission and applies the original cumulative-work<64 timestamp
clear there. Reclassification unlinks using the retained old class before
publishing the new class. Other three policy pools remain SCHED-03/04.

`G_SetUnitPlayer` sends generic `A_UNIT_OWNER_CHANGING` before publishing the new
player and `A_UNIT_OWNER_CHANGED` afterward. Move owns ordinary order
cancellation in the first message and class/repulsion publication in the second.
This follows native `215500 -> 698ce0`, cancellation at `698d92`, and mover bridge
`05c800 -> 168a80`; it does not put movement rules in the owner/food dispatcher.
Callbacks during cancellation see the old owner. Same-owner writes remain no-ops.

Two new independently owned `owner_change` scene9 captures use the original
Human02Interlude map with only its script replaced. Immutable observer sources
are `runtime/owner-class-stop-source-261002/`; captures are
`runtime/owner-class-stop-{first,repeat}-261002.jsonl`. Read-only Stop observation
adds native before/after pose/velocity/request words without calling target code.
All365 lifecycle rows repeat exactly, including17 motion commits, two synchronous
ownership Stops, class0 to1 and306 public markers through tick300. Stop preserves
committed fineXY/facing/cap and clears velocity/requested words. The frozen fixture
is `tools/ghidra/fixtures/retail-owner-change-1.27.json`; digest is
`9e172f97f6fe4d2d0253fbea04fda6ab47b298f68f6e14cbd54260dd21a40cd2`.
Trailing clock observers are complete but grant no new clock-parity claim.

The normal-frame engine regression starts at clock zero, creates the supplied
stock Footman movement profile, issues its point Move at timer tick10 and changes
owner at tick15. It compares16 directly observable motion commits and the exact
final Stop clock/nativeXY/native velocity/facing words. The seventeenth commit
and synchronous Stop share the1500ms server frame, so the frame exposes final
Stop state; original arithmetic replay separately checks every one of17 commits.
Saved continuations immediately before and after that frame retain cancellation
and the stationary final state through30 seconds. Static base-map geometry and
movement profile are explicit supplied fixtures; arbitrary scenery and other
ability ownership remain open.

Engine tests additionally cover every240 different-player transition, pending
middle/head removals for all16 rows, same-owner no-ops, actual local-route
admission/charging under independent exhaustion, and two pending requests in
every row through a real save/load. Save76 writes the sixteen recursive bucket
records, relocated edict links and each request's old class. Validation rejects
cross-row links, duplicate membership, wrong ownership/class, cycles, invalid
pointers/countdowns and disconnected tags. Earlier save layouts are rejected;
JSVM and network contracts are unchanged.

Fresh `runtime/owner-class-strict-261002/` passes the new live lifecycle contract
and the unchanged complete scheduler oracle:64 initialized buckets,448 policy
selections,1280 cadence checks,16384 FIFO operations,162 intervals,1024 queued
class changes and256 target-policy changes. Only ordinary fine queues have
production parity here. Ghidra now retains497 roles,35 layouts/201 fields and223
explicit prototypes; five saved owner/class roles read back in
`runtime/owner-class-ghidra-readback-261002.json`. No p-code was needed.

Final payoff40 validation: debug (`WC3_DEBUG_BUILD=1`) and forced release RoC/TFT
each pass189,902 assertions in2313 tests. Production debug/release builds, the
normal repository suite,175 Python pathfinding tests, ability registration audit
and258-entry/99-fixture inventory pass. Strict fresh scheduler/live contracts
pass. SCHED-03.4 closes; the original wider scheduling/traversal leaves remain
open. The validation index is `runtime/owner-class-validation-261002.json`.


## Selected ground Move uses the shared physical owner

Payoff41 connects ordinary selected-unit Move, including the existing SmartPoint
movement handoff, to the retained Move-owned group. Previously selection assigned
matching IDs to independent walkers; the new failing regression found no physical
group. For two through twelve unqueued ground candidates with the same static
pathing mask, `move_selectlocation` snapshots unit generations and delegates a
named Move request through generic ability order admission. Move owns the routes,
formation slots, all-member decisions, commits and cleanup. Shift, air/mixed
masks and larger selections retain their explicitly marked existing path pending
recovery of those producers; this bounded port does not certify their fidelity.

The new selected pair also consumes the unchanged public-pair fixture:115 exact
normal-frame commits and87 saved continuation commits. Two independently owned
original scene49 captures then exercise the actual player producer. The map keeps
the original Human02Interlude bytes except its script, enables its local Player3,
creates/selects two stock Footmen and waits for external Move input. The Frida
observer remains read-only. `wc3_ui_input.c`, built with native `winegcc`, uses
Win32 `SendInput` after requiring one visible Warcraft window belonging to the
controller-owned PID and verifying foreground ownership. The controller hashes
the reviewed C source, wrapper and linked Winelib library along with its observer
and map sources. X11 keyboard input opens Move targeting; diagnostic X11 mouse
attempts failed to publish an order and remain outside accepted evidence.

Live `6b9f70` receives player3, packet flags8, order851986 and the exact native
point words `c4f0f91c/c3ea6970`. It allocates ground/air requests, attaches both
candidates through `6b8c10` before either `6ba800` admission, then sorts its
nine-word rows and publishes both orders through `6b93a0`. Both members retain
one ground request. `89caf0(false)` clears the three extended formation flags;
the enabled-policy/physical-flag mapping remains open. The observed member order
is the supplied ordinary selection witness, not a claim about every selection
priority or callback mutation.

Each clicked lifetime has117 decision/commit owner visits,228 commits
(member counts117/111),226 scalar decisions and two natural order completions.
Both repeat every fine position, velocity and facing word; relative-motion digest
is `7e6be9b93f5e75f25f58573e0b77830658317507bc3b9a1eba6e4756b250a685`.
Absolute input clocks differ, so the frozen cases retain each separately. Normal
engine frames start at clock zero, create the supplied movement profile and
static scene, then issue selected Move at each observed admission clock. All456
clock/position/velocity/facing commits agree without replaying native decisions.
Saves after commit80 and224 reproduce304 additional suffix commits, preserving
Player3 fine ownership and the physical member lifecycle. This is deterministic
simulation with supplied input timing; external wall-clock input timing is not
claimed deterministic. Scene construction remains an explicit fixture boundary.

Formation tests retain the original frozen layout words through the queued
producer. Physical destinations follow the current shared coarse point/heading;
the normal-frame save regression checks their exact continuation. Speed and
status tests now check the separate physical commit cap. The status scene was
also corrected to contain both members and the destination inside its route grid
and to supply the recovered Footman turn/window profile; its single-unit and
fresh-cohort branches separately check application, actual movement and expiry.
No tolerance was widened.

Accepted artifacts under the standard report root:

- `runtime/selected-point-final-source-261002/`, including the input helper;
- `runtime/selected-point-final-{first,repeat}-261002.jsonl`;
- `runtime/selected-point-strict-final-261002/corpus-results.json`, which reruns selected
  input, public pair, twelve-member group and owner-change contracts freshly;
- `runtime/selected-point-types-final-261002.json` and
  `runtime/selected-point-ghidra-readback-saved-261002.json`:507 saved function roles,
  38 partial layouts/218 fields and230 verified x86 prototypes; ten new producer
  roles retain comments and native xrefs. No p-code was needed.

The frozen input/phase hashes and motion rows live in
`tools/ghidra/fixtures/retail-selected-point-1.27.json`; the strict checker also
verifies the generated engine header. GROUP-04.12 explicitly splits this ordinary
selection producer from04.6. Queue activation, air/mixed lanes, wider selection
priority, callbacks, enabled formation options, AI producers and arbitrary scene
construction remain required. Next runnable work is GROUP-04.6/ORDER: recover
Shift activation and retain physical ownership through queued point orders.

Final payoff41 validation: debug (`WC3_DEBUG_BUILD=1`) and forced release RoC/TFT
each pass200,146 assertions in2316 tests. Production debug/release builds, the
normal repository suite,179 Python pathfinding tests, ability registration audit
and259-entry/100-fixture inventory pass. All four strict fresh live contracts
pass. Save version76 is unchanged. GROUP-04.12 closes; the ledger retains118
completed tasks and186 open tasks after explicit splits. The validation index
is `runtime/selected-point-validation-261002.json`.


## Selected Shift Move retains request ownership through staggered arrival

Payoff42 ports the first selected ground Shift Move behind an active shared
cohort. Move now queues the common destination with one new request identity,
keeping the current physical group and order intact. Each member activates its
queued request only when its preceding leg completes. It first creates a
singleton; a later member with the same request, destination and movement mask
rebuilds a fresh cohort with the activating member first. Generation-retaining
member rows, the default truncated fine-distance40 admission and separate
all-member decisions/commits remain Move-owned. The ranked-formation regression retains its frozen world-point expectations
for independent current orders; its active-cohort branch now asserts the native
common point and shared new request identity instead of legacy per-unit offsets.
No numerical tolerance changes. The owner visits newer groups
first and freezes each pass's initial extent, so callback-created owners wait
until the next pass. Generic queue dispatch carries an owner-interpreted context
and delegates activation through the ability procedure.

Two complete owned scene50 captures use external Win32 Shift input and read-only
Frida instrumentation. The original Human02Interlude bytes are retained except
its script. The public timer issues the first pair Move at1 second; actual
selected player3 packet9 appends the common point without replacing either
current order. Both movers retain the new request in Unit240/244. The first
neighbor search returns no active match; the second returns the first mover's
new wrapper. Four callback candidates contain exactly one accepted nonself
match. `89c890/169620/16bdb0/16b7b0` distinguish canonical request candidates,
ready slots and active physical members; attachment alone does not activate a
member. The first peer finishes at6.479768, its singleton ticks at6.509766 before
the old main member finishes, and the rebuilt pair first ticks at6.539764.

Both journeys repeat every one of510 absolute clock/fine-position/velocity/facing
commits (250 main,260 peer),506 scalar decisions, four natural arrivals and261
owner passes. The three physical cohorts visit184,1 and76 times. Their common
motion digest is `a8020af581b9aa7fa0d83d1474baffae9e2ae9d73166306c3e6ededc32f03c14`.
The input clocks differ while the motion repeats exactly. A boundary input in
the first capture arrives after primary clock advance but before the due owner
counter increment; the engine test delivers that input through the ordinary
pose-publication import callback. It does not advance clocks or replay native
movement decisions. Both original clock and owner counter are asserted exactly.
Normal engine frames reproduce all1020 commits, with684 saved suffix commits
from saves before activation and immediately after cohort rebuilding.

Save78 records queued request contexts and physical group request identities.
The upstream merge's Save77 already combines retained player fine queues with
Stop guards and per-player disabled abilities; their behavior and serialization
remain present. New ability flags use separate symbolic bits, and queued Move
activation retains the upstream accepted-order cleanup hook. Earlier layouts
are explicitly rejected.

Accepted artifacts under the standard report root:

- `runtime/selected-queued-final-source-261002/`, with immutable observer,
  controller, script, map tool and reviewed Winelib Shift helper;
- `runtime/selected-queued-final-{first,repeat}-261002.jsonl`;
- `runtime/selected-queued-strict-confirmed-261002/corpus-results.json`, freshly
  rerunning queued selection, ordinary selection, public pair, twelve-member
  movement and owner-change contracts;
- `runtime/selected-queued-types-final-261002.json` and
  `runtime/selected-queued-ghidra-readback-final-saved-261002.json`:521 saved roles,
  42 partial layouts/261 fields and244 instruction-established x86 prototypes.
  Fourteen new roles retain comments and native xrefs. No p-code was needed.

`retail-selected-queued-1.27.json` freezes every phase/request hash, neighbor
record, owner order and motion word. The checker verifies the engine header,
complete provenance, numerical decisions and natural order cleanup. Five
asset-free tests reject neighbor identity, category, destination, wrapper,
self-match, accepted-result and missing-search mutations.

GROUP-04.13 explicitly splits this active-cohort first-Shift port from04.6.
Idle or mixed current groups, additional queued requests, other movement masks,
larger selections, wider spatial neighbor predicates, preferred-path/range90/
bypass policies, callback mutation and arbitrary scene construction remain
required. Next runnable work is04.6's additional/idle/mixed queued producers.


Final payoff42 validation: debug (`WC3_DEBUG_BUILD=1`, `wc3_*`) RoC/TFT each
pass213,974 assertions in2323 tests. The forced release production/test build
and normal repository suite pass; its all-test RoC/TFT runs each pass214,407
assertions in2360 tests, including the additional generic/client tests. All184
Python pathfinding tests, ability audit,260-entry/101-fixture inventory and five
fresh strict contracts pass. The Ghidra readback verifies fourteen new roles
and two updated player-producer roles after saving. GROUP-04.13 closes; Save78
and119 completed/186 open backlog leaves are synchronized. General inter-group
ordering after physical slot reuse remains separate scheduling work. The
validation index is `runtime/selected-queued-validation-261002.json`.


## Selected idle Shift starts the shared physical owner immediately

Payoff43 extends ordinary selected ground movement to Shift input when every
selected candidate has no active order or pending entries. The Move batch then
starts the physical owner immediately. An active shared cohort still follows
payoff42's deferred common-point request path; mixed/additional queued producers
remain explicit work. Previously the idle Shift path resolved per-unit slots and
started independent walkers. The native-word regression failed40 assertions
before the dispatch correction. Its completed comparison now matches456
clock/fine-position/velocity/facing commits and304 saved suffix commits through
normal engine frames and both natural order completions.

Two independent owned captures reuse the original scene49 script/map and send
actual native Win32 Shift input. Both player3 packet9 producers attach both
candidates before admission and publish one shared physical pair. Native
`693490` appends each empty current head (count0 to1); when the internal task
identity is unresolved it immediately dispatches `67abe0`. With an existing
internal task it instead publishes `d02a5`. Assembly retains ECX Unit and the
stack4 order pointer, both returns `RET4`; decompilation alone misattributes the
identity-setter receivers. The native signed count>500 and Unit5c bit100 gates
remain documented; the engine's pending ring cap16 remains an explicit bound,
not a claim about that original limit or the wider rejection/event graph.

Each lifetime has228 exact commits,226 scalar decisions,117 decision/commit
owner visits and two natural arrivals. Both repeat all absolute motion words;
relative digest is `1d3d1cccace13249fc2739dd761c6548dc1ab22afd7c12152440591974241703`.
The supplied input occurs near5 seconds. Low position bits differ from payoff41's
later unqueued input because software clock subtraction depends on exponent;
these words are retained separately. No tolerance or clock assertion is widened.
The existing selected-input engine setup is reused with explicit fixture tables
and the Shift flag; saved continuations retain Player3 ownership and membership.
Save78 is unchanged.

Accepted artifacts under the standard report root:

- `runtime/selected-idle-shift-source-261002/` and the two complete
  `runtime/selected-idle-shift-{first,repeat}-261002.jsonl` captures;
- `runtime/selected-idle-shift-strict-final-261002/corpus-results.json`, freshly
  rerunning idle Shift, active Shift, ordinary selection, public pair,
  twelve-member movement and owner-change contracts;
- `runtime/selected-idle-shift-types-final-261002.json` and
  `runtime/selected-idle-shift-ghidra-readback-saved-261002.json`:522 saved roles,
  42 partial layouts/262 verified fields and245 explicit x86 prototypes. The
  append role, ABI and rejection flag are persisted alongside both updated
  player-producer comments and their xrefs. No p-code was needed.

`retail-selected-idle-shift-1.27.json` pins input provenance, producer/phase
records, empty-head admission and every motion word. The same strict selected
checker verifies the distinct packet policy and rejects absent Shift or nonempty
current heads. Five new asset-free tests preserve the independent captures,
input-clock rounding distinction and exact generated engine header. GROUP-04.14
explicitly splits this idle producer from04.6. Next runnable work is additional
queued requests and mixed active/idle cohorts; wider movement masks, rejection
policies, callbacks and arbitrary scene construction remain required.


Final payoff43 validation: debug (`WC3_DEBUG_BUILD=1`, `wc3_*`) RoC/TFT each
pass220,116 assertions in2324 tests. Forced release production/test builds and
the normal repository suite pass; all-test RoC/TFT each pass220,549 assertions
in2361 tests. All189 Python pathfinding tests, ability audit,261-entry/102-fixture
inventory, six strict fresh contracts and saved Ghidra readbacks pass. Save78 is
unchanged. GROUP-04.14 closes;120 completed/186 open backlog leaves are retained.
The validation index is `runtime/selected-idle-shift-validation-261002.json`.


## Two pending Shift moves retain submission history and physical generations

Payoff44 extends selected ground Move to two pending Shift point requests behind
one active physical cohort. The engine keeps each FIFO entry's owner context,
but cohort acquisition compares the units' latest submitted request history.
Submitting another common Move changes that history before issued-order
publication; activating an older queued entry does not overwrite it. Request
identity allocation excludes retained history as well as active owners and
pending entries. Mixed active/idle selections and other producer lanes remain
separate work.

Two independent native Win32/read-only scene50 captures append current counts
1 to2 to3 without replacing either current head. Their input clocks differ,
but all555 absolute clock/fine-position/velocity/facing commits repeat exactly.
There are549 scalar decisions, six natural arrivals and295 owner passes per
capture, across five physical cohorts with184/1/76/10/24 passes. All four native
previous-request searches retain the SECOND submitted ground canonical identity
`[1305,1331]`, including the two searches that activate the FIRST queued goal.
Their results are0/1/0/1. Assembly `6b94e3/6b94ec` writes Unit240/244 before
`693490` appends the order at `6b9545`. Native rejection-side history and the full
queue-limit/event graph remain open; the engine pending ring remains bounded16.

The full journey also exposes two physical lifecycle errors. Freezing only the
slot-array extent lets a newly created owner reuse a lower slot and run again
in its creation tick. Move now freezes owner pointers plus64-bit creation
sequences, visits newest cohorts first, and excludes replaced generations.
Second, `16de50` selects and predicts its source during cohort creation and
stores the formation origin at group54/58 (`16ded2/dedd`). Deferring that seed
until the next owner pass changes low destination/velocity/heading bits when
the reused cohort includes a moving singleton. Move now retains the creation
origin. These fixes preserve the original phase boundary and numerical words.

Normal frames match1110 native commits and864 saved suffix commits. The shared
input adapter delivers recorded selection, Shift and both points at exact
primary-clock/owner-counter boundaries through ordinary pose publication. One
save precedes the later external click; the adapter uses player3's actual client
slot and restores those external input controls after load. The other save
follows the first cohort rebuild. No clock is advanced manually, no owner or
retail decision is replayed, and no comparison tolerance is widened. The old
additional-queue path fails six assertions; correcting history then exposes the
owner-generation and creation-origin failures before the complete comparison
passes.

Save79 retains latest submitted unit history, physical creation sequences and
the next sequence alongside Save78's queued contexts and group request records.
Zero, duplicate and out-of-range saved owner sequences are rejected. Prior
layouts including78 are rejected explicitly; upstream Stop and disabled-ability
contracts remain present.

Accepted artifacts under the standard report root:

- `runtime/selected-double-shift-buffered-source-261002/`, immutable controller,
  observer, scenario/map sources and reviewed native Move/Shift input helper;
- `runtime/selected-double-shift-light-{first,repeat}-261002.jsonl`;
- `runtime/selected-double-queued-strict-final-261002/corpus-results.json`, seven
  fresh contracts covering this journey, first Shift, idle Shift, ordinary
  selection, public pair/twelve and owner changes;
- `runtime/selected-double-queued-types-final-saved-261002.json` and
  `runtime/selected-double-queued-ghidra-readback-saved-261002.json`:522 saved
  roles,42 partial layouts/262 fields/245 explicit x86 prototypes. Six roles,
  their signatures/native xrefs and two refined fields are read back after save.
  New field evidence appends to existing annotations; layouts stay unchanged.
  No p-code was needed.

The controller buffers every JSON record and uses observed sample ticks for
input deadlines. Separate bulk clock events are explicitly disabled for these
captures because transport backlog delayed the second click past activation.
Every producer and movement clock remains frozen; previous clock-kernel
contracts remain in the corpus. Earlier loading/timing attempts are diagnostic
captures outside this accepted two-pending-request contract.

`retail-selected-double-queued-1.27.json` and the generated engine header retain
all words, request/member/phase hashes, five-cohort visit order and input
provenance. Five additional asset-free tests reject old history, wrong goals,
categories, self-matches, accepted results and missing searches. GROUP-04.15
explicitly splits this port from04.6. Mixed active/idle or unrelated current
orders, other masks, larger selections, broader neighbor/preferred/range90
policies, callbacks, rejection-side history and arbitrary scene construction
remain required.


Final payoff44 validation: debug (`WC3_DEBUG_BUILD=1`, `wc3_*`) RoC/TFT each
pass236,027 assertions in2325 tests. Forced release production/test builds and
the normal repository suite pass; all-test RoC/TFT each pass236,460 assertions
in2362 tests. All194 Python pathfinding tests, ability audit,262-entry/103-fixture
inventory, seven fresh strict contracts and saved Ghidra readbacks pass. Save79
and GROUP-04.15 are synchronized;121 completed/186 open backlog leaves remain.
The validation index is `runtime/selected-double-queued-validation-261002.json`.
Next runnable work is04.6's mixed active/idle and unrelated-current-order producers.

## Mixed active and idle Shift share one submitted request

Payoff45 extends selected ground Move admission to heterogeneous current state.
`move_queue_group_point` appends the same destination/context to each eligible
unit, records latest submission history, and starts an idle unit through the
ordinary FIFO activation hook before issued-order publication. An active unit
keeps its current head. Selection no longer requires all current physical owners
to match. Formation destinations are assigned at physical activation, not when
queuing the common point. Move owns these changes; the universal client is unchanged.

Four complete original scene51 captures use reviewed native Win32 Move/Shift
input and a read-only observer. Two early clicks give361 commits each: the idle
peer finishes before the original mover, and the later cohort search returns0.
Two later clicks give418 and370 commits: the still-moving peer joins the later
cohort, and its search returns1. All1510 absolute clock, fine-position, velocity
and facing commits match ordinary engine frames;1498 scalar decisions, twelve
arrivals and1365 owner visits are retained. Input clocks differ and the four
motion tables remain separate; no cross-input numerical equality is claimed.
Save79 reproduces1020 suffix commits across saves at commit200 and300.

The adapter samples recorded movement clocks because order admission also
publishes a pose. It still advances only ordinary5ms frames and supplies input
through the normal pose-publication boundary. No owner, clock advancement or
retail decision is replayed. Earlier shared/idle/two-pending input regressions
remain exact. The older queued-formation test now checks common points and
request history while retaining physical formation and saved continuation checks.
The broad suite exposed a scene-adapter lifetime bug: resetting entities did
not retire periodic timers before replacing the JASS VM. The adapter now destroys
its scene timers at boundaries; production timer semantics are unchanged. The
failed run and debugger backtrace are retained as diagnostic artifacts.

Accepted artifacts under the standard report root:

- `runtime/selected-mixed-shift-source-261002/`: immutable scenario, observer,
  controller, map and reviewed native input helper;
- `runtime/selected-mixed-shift-complete-{first,repeat}-261002.jsonl`;
- `runtime/selected-mixed-shift-late-first-261002.jsonl` and
  `runtime/selected-mixed-shift-late-confirmed-repeat-261002.jsonl`;
- `runtime/selected-mixed-shift-strict-final-261002/corpus-results.json`: eight
  fresh contracts, including all four mixed journeys and prior input/owner cases;
- `runtime/selected-mixed-shift-types-final-261002.json` and
  `runtime/selected-mixed-shift-ghidra-readback-saved-261002.json`:522 saved
  roles,42 partial layouts,262 fields and245 explicit x86 prototypes. Four
  heterogeneous-admission roles, signatures and native xrefs are read back.

One earlier capture lacks a valid trace-end and remains diagnostic. Another
late click hit the moving unit: its packet carries target identity[1108,1108],
so it is target Move evidence outside this ground-point contract. The strict
checker requires target[-1,-1] and rejects that substitution. It also rejects
non-Shift input, replacement of an active head, failure to start an idle unit,
wrong previous-request matching, self-candidates and incorrect join results.

`retail-selected-mixed-1.27.json` and `retail_selected_mixed.h` retain all four
journeys and provenance. Full unrelated-current-order, rejection/callback,
other-mask/air/larger-selection and arbitrary-scene contracts remain open.


Final payoff45 validation: debug RoC/TFT each pass256,436 assertions in2326
tests. Forced release production/test builds and the normal repository suite
pass; all-test RoC/TFT each pass256,869 assertions in2363 tests. All199 Python
pathfinding tests, ability audit,263-entry/104-fixture inventory, eight fresh
strict contracts and saved Ghidra readbacks pass. GROUP-04.16 closes with Save79
unchanged;122 completed/186 open backlog leaves remain. The validation index is
`runtime/selected-mixed-shift-validation-261002.json`. Next runnable work is
04.6's independent active owners, then its wider producer/eligibility contracts.

## Independent active owners accept the same pending ground request

Payoff46 strengthens GROUP-04.16 without adding another backlog leaf. Original
scene52 starts two singleton JASS point groups toward distinct destinations,
then submits one native selected ground Shift. Both distinct active heads are
preserved1 to2. At counter1240, the peer creates a new cohort (search0), then
the other unit joins it (search1). Common pending admission does not require
matching current physical group IDs. This confirms the production admission
change from payoff45 across independent active owners.

Two complete owned/read-only captures repeat all511 absolute motion commits
exactly:507 scalar decisions, four natural arrivals and440 physical owner passes
per capture. The existing engine implementation matches1022 normal-frame
commits and844 saved suffix commits at commit200/400, using Save79 unchanged.
The adapter retains entity creation order; normalized member0 is the peer,
because its newer singleton is the first native physical owner. It does not
change owner order or replay movement decisions.

The mixed-input checker is reused with an explicit independent-head policy.
It requires two distinct nonempty preserved heads, null-target ground Shift,
and the original0/1 cohort search with matching submitted histories. Four
additional asset-free tests reject replacement, duplicate heads, changed
counts, mismatched history and self-matches; every header word is checked.
Source/capture hashes, all request/phase records and both identical motion
tables are frozen in `retail-selected-independent-1.27.json`.

Accepted artifacts under the standard report root:

- `runtime/selected-independent-shift-source-261002/` and
  `runtime/selected-independent-shift-{first,repeat}-261002.jsonl`;
- `runtime/selected-independent-shift-strict-final-261002/corpus-results.json`,
  nine fresh selected/public/owner contracts;
- `runtime/selected-independent-shift-types-final-261002.json` and
  `runtime/selected-independent-shift-ghidra-readback-saved-261002.json`, four
  saved scene52 roles/signatures/xrefs;522 roles,42 layouts,262 verified fields
  and245 explicit x86 prototypes remain unchanged. No p-code was needed.

Current combat/work/cast heads, rejection/callback/limit graphs, mixed masks,
air/larger selections, repeated identical-goal policy and arbitrary scene
construction remain required. This bounded point-group composition does not
close GROUP-04.6 or certify those other current-order families.

Final payoff46 validation: debug RoC/TFT each pass271,444 assertions in2327
 tests. Forced release production/test builds and the normal repository suite
pass; all-test RoC/TFT each pass271,877 assertions in2364 tests. All203 Python
pathfinding tests, ability audit,264-entry/105-fixture inventory, nine fresh
strict contracts and saved Ghidra readbacks pass. GROUP-04.16 is strengthened;
no new task is added, and122 completed/186 open leaves remain. Save79 is unchanged.
The validation index is `runtime/selected-independent-shift-validation-261002.json`.
Next runnable work is TARGET-02.1 moving-target refresh and numerical pursuit,
with a live target-speed transition; wider GROUP-04.6 contracts remain required.


## Smart Follow tracks a moving target through a speed change

Payoff47 splits the bounded ground-unit producer from TARGET-02.1 into
TARGET-02.4. Original scene53 creates two friendly Footmen, issues public Smart
at tick10, starts the target's point Move at80, changes its speed at85, and
explicitly Stops the follower at300. Two independently owned, read-only Frida
captures contain the same1015 absolute position/velocity/facing/clock commits:
106 initial approach owner visits,861 persistent Follow visits and48 target
point-Move visits. The Smart user head survives the internal approach arrival.
The final Stop is authored input; this is not natural Follow order completion.

Move now owns the physical target cohorts, per-member arrival range, generation
checks, cached target destination and countdown. Normal engine frames match all
1015 commits without injecting native decisions or owner clocks. Save80 loads
at commits50/150/250 reproduce2595 suffix commits, including later ordinary
JASS point/speed/Stop callbacks. Structures and flight retain their existing
explicitly marked traversal pending the corresponding native producer evidence.

Two recovered policies have direct engine payoff. Target arrival is
`max(.49,(FollowRange+followerRadius+targetRadius)/32)` in fine coordinates:
scene53 publishes11.3125, or362 world units. Countdown reload uses the first
member's committed pose and cached destination, software scalar distance times
0.33 plus0.5, wrapped integer conversion, then clamps16..132 (flag400 adds165).
Destination replacement compares `floor(fine)>>1`: sampled target motion within
that bucket retains the old destination. Unconditional replacement originally
failed at commit341; retaining the bucket restores the entire journey. The
captured destination-delay timestamps are zero. Their wider delayed-change
producers remain TARGET-02.1 rather than being claimed by this witness.

The broad suite exposed a retired timer callback lifetime bug. Destroy stopped
the timer but retained a pointer into a freed VM program when the next scene
replaced that program. Save/load serializes retired allocated timer slots;
GDB with the standard JUnit environment found an invalid serialized handler.
`G_TimerDestroy` now clears the callback while cancelling its generation. The
loader also names an unresolved function field instead of only reporting a
level-state failure. The failing standard-run and GDB reports remain diagnostic
artifacts; passing reruns alone were not treated as proof.

Accepted capture/provenance inputs live under the standard report root:

- `runtime/follow-velocity-complete-source-261002/` and
  `runtime/follow-velocity-complete-{first,repeat}-261002.jsonl`;
- `runtime/follow-velocity-types-final-261002.json` and
  `runtime/follow-velocity-ghidra-readback-saved-261002.json`:527 saved function
  roles,42 partial layouts,262 verified fields and245 explicit prototypes;
- `retail-follow-velocity-1.27.json` and `retail_follow_velocity.h` freeze every
  motion word plus range/refresh/replan/completion policies and sampled markers.

The strict checker verifies complete observer extents, source hashes, repeated
absolute phases/owners/motion and explicit Stop. Six asset-free tests reject
missing collision radii, wrong countdown/clamp, same-bucket replacement,
persistent completion and lost bounded Stop, and compare every header word.
No p-code, prototype guesses, tolerance widening or game calls from the observer
were needed. An earlier experiment let Follow continue beyond the completion
marker; those extra commits remain diagnostic, outside the bounded fixture.
Target teleport/resize, visibility, kill/remove/reuse, wider timestamp producers,
combat chase and arbitrary map/mask composition remain open.


Payoff47 final validation: debug RoC/TFT each pass300,392 assertions in2328
WC3 tests. Forced release production/test builds and the normal repository suite
pass; all-test RoC/TFT each pass300,825 assertions in2365 tests. All209 Python
pathfinding tests, ability audit,265-entry/106-fixture inventory, ten fresh strict
contracts and saved Ghidra readbacks pass. The final strict reports are
`runtime/follow-velocity-strict-saved-final-261002/corpus-results.json`; the
validation index is `runtime/follow-velocity-validation-261002.json`.
TARGET-02.4 closes with123 completed/186 open leaves. Next runnable work is
TARGET-02.3 public target removal/death and reuse under the physical Follow port.

## Follow cancels synchronously before target pool reuse

Payoff48 closes TARGET-02.3. Scenes54/55 extend the preceding ground Smart
Follow scene: target Move at tick80, speed change85, public RemoveUnit or
KillUnit100, removal of the dead target105, replacement CreateUnit110,
explicit new Smart120 and bounded Stop300. Four complete read-only captures
(two per retirement mode) share948 absolute clock/position/velocity/cap/facing
commits and the digest
`cbb14147556d5f198adebba685a232c5de78400185c585d9236ddd80e8b9f696`.
They show actual reuse of the target mover address and public GetHandleId,
with a fresh canonical generation. The follower's user head clears before the
public retirement call returns, stays zero during ticks100..119, and changes
only on explicit Smart120. Per-case markers preserve the distinction between
a dead target awaiting removal and immediate RemoveUnit.

The baseline engine did not cancel Follow synchronously on death. Its resumed
journey diverged at commit348 despite matching clock, velocity and facing.
Move now receives the generic engine-owned death event and notifies its target
subscribers itself. Its target-removal handler detaches the physical group
before entering stand; merely clearing the target and waiting for the next
30ms owner pass left a stale physical owner. Gameplay behavior remains in Move;
the universal client and general Unit death machinery are unchanged.

`t_movement.c` drives both journeys through ordinary RunFrame and public JASS,
without injected owner counters or decisions:948 exact commits each, plus
1746 saved suffix commits each (3492 total). Saves cover early approach, the
idle interval after the replacement exists, and reacquired persistent Follow.
Save80 is unchanged. The preceding speed-change fixture retains1015 exact
commits and2595 saved suffix commits. Native Follow still ends by authored
Stop; natural user-head completion is not claimed. Other orders, queued/combat
parents, simultaneous reentrant callbacks, flight/structures and arbitrary
maps remain separately open.

Artifacts under the report root:

- `runtime/follow-target-{remove,kill}-reuse-{first,repeat}-261002.jsonl`:
  four complete original captures,948 commits each.
- `runtime/follow-target-reuse-source-261002/`: immutable13-input source archive.
- `runtime/follow-target-reuse-cmath-first-261002.json`:844 scalar decisions,
  3792 exact commits/owner passes and four actual native reuse witnesses.
- `runtime/follow-target-reuse-ghidra-readback-saved-261002.json`:
  saved death/loss/cleanup roles, signatures and xrefs. New death producer
  `679bb0` reaches `651010`, Move dispatch `5fda10`, target-loss `5ff490`,
  recovery `5fb190` and arrival cleanup `5fa7a0` before KillUnit returns.
  528 roles are saved;42 layouts/262 fields/245 prototypes/45 globals remain.
- `runtime/follow-target-reuse-strict-saved-final-261002/corpus-results.json`:
  fresh strict reruns of this contract and ten preceding public/owner journeys.
- `runtime/follow-target-reuse-validation-261002.json`: validation and current
  source fingerprints. Corpus inventory266 entries/107 fixtures/49 strict live
  entries;215 Python checks.

TARGET-02.3 closes with124 completed/185 open leaves. Next runnable work is
TARGET-02.2, public target teleport/resize through physical Follow owners.

Payoff48 final checks: forced debug RoC/TFT each pass349,559 assertions in2330
WC3 tests; forced optimized build and normal repository suite pass349,992
assertions in2367 WC3 tests per variant and the remaining repository suites.
All215 pathfinding Python checks and the ability audit pass; the fresh strict
runner completes11/11 contracts with current source hashes.

## Follow tracks public target teleports without premature point settling

Payoff49 explicitly splits public teleportation from TARGET-02.2 into02.5;
collision resizing stays02.2. Scenes56/57 teleport the target after point
arrival at tick100; scenes58/59 jump while its Move is active at tick90.
Each pair uses public SetUnitX/Y or SetUnitPosition to(-1600,300). The axis
setters retain the target's Move851986, so it returns toward(-1936,112).
SetUnitPosition stops that target order. The other unit's Smart851971 survives
both setters, and Follow ends only at the authored Stop300.

All four journeys have complete read-only native repeats. The two stationary
setter variants share1015 motion commits and digest
`0b6094b124cb56eb07840ff3c2a1e91622556e0a470ae1c30eb38a624ead8725`.
Moving-target axis teleport has1060 commits/digest
`cfe77e9583885789aad3ffb595f735aeab74fe3ec2e7d6ec3243aba452de5845`;
moving-target placement has1001/digest
`ad07cb2dcab4aa4a142e934f6b2774bb4cb7ae0b66a43a09987d7a5aabd438d8`.
Every owner phase, motion word, public marker, range and refresh record repeats.
Each journey retains58 target-refresh reloads. The new stationary fine target
174/105.375 is accepted at owner counter1368. Moving-target setters are accepted
at1334; axis travel already returns to172.2073059/104.8294907 by that sample.
Member replan timestamps1334/0 are subsequently witnessed at1351, still eligible
under the ten-tick gate. Denied changed-cell timestamp producers remain02.1.

The engine initially appeared to diverge at the teleport itself because the
regression read only the final frame state, after its JASS timer. A BZ_TESTS-only
observer now records scheduled Move commits, including singleton target steps,
turn stops and arrival stops, before timer writes overwrite them. It changes no
movement decisions. That stricter comparison exposed a real divergence at
commit407: after a turn wait, the legacy blocked-progress counter8 and stale
last-distance28.556 triggered Hold, even though the target was52.028 world units
from its destination. Native counter1378 resumed the retained point route.
Move now excludes scheduled retained retail point routes from that legacy settling
shortcut, leaving termination to its fine arrival and retry policies. Other
approach behaviors keep the existing settling rule.

All four normal engine journeys match4091 commits total and7872 saved suffix
commits. The saved checkpoints cover initial approach, the retained route after
teleportation, and persistent Follow. Axis travel uses completed-owner boundary
351 instead of350, which falls between two same-clock commits. The preceding
speed-change regression now also uses boundary251 instead of250: its strict
commit observer verifies2594 suffix commits. The earlier snapshot regression's
2595 samples included an already committed row present in the restored save;
the new boundary avoids counting it as newly executed movement. The preceding
death/removal cases retain1896 normal commits and3492 saved suffix commits.
Save format80 is unchanged.

Artifacts under the report root:

- `runtime/follow-target-{xy,position}-{first,repeat}-261002.jsonl` and
  `runtime/follow-target-travel-{xy,position}-{first,repeat}-261002.jsonl`:
  eight complete native witnesses,8182 total commits.
- `runtime/follow-target-mutation-source-261002/` and
  `runtime/follow-target-travel-mutation-source-261002/`: immutable source
  archives for the respective map producers. Per-capture metadata retains each
  exact producer hash; promoted repository sources include all four scenarios.
- `runtime/follow-target-teleport-cmath-first-261002.json`:2158 scalar decisions,
  8182 exact motion commits/owner passes,464 target reloads.
- `runtime/follow-target-travel-xy-hold-correct-gdb-261002.log`: original engine
  premature Hold breakpoint, distance, progress state and caller locals.
- `runtime/follow-target-teleport-engine-settle-fixed-261002.log`: seven exact
  Follow engine journeys and their saves in both variants.
- `runtime/follow-target-teleport-ghidra-readback-saved-261002.json` and
  `runtime/follow-target-teleport-types-final-261002.json`: saved setter,
  destination refresh, replan and point-task roles/signatures/xrefs.528 roles,
  42 layouts/262 fields/245 prototypes/45 globals remain.
- `runtime/follow-target-teleport-strict-saved-final-261002/corpus-results.json`:
  fresh strict contract plus eleven preceding public/owner journeys.
- `runtime/follow-target-teleport-validation-261002.json`: final checks/current
  source hashes. Corpus267 entries/108 fixtures/50 strict live entries;
  222 Python checks.

TARGET-02.5 closes with125 completed/185 open leaves. Next runnable work is
TARGET-02.2, an authored public collision-resize/morph producer and its range,
footprint and routing consumers. Visual scale alone cannot close this task.

Payoff49 final checks: debug RoC/TFT each pass487,322 assertions in2334 WC3
tests; the forced optimized build and normal repository suite pass487,755
assertions in2371 WC3 tests per variant and all remaining repository suites.
All222 Python checks, ability audit and267-entry inventory pass; the fresh
strict runner completes12/12 contracts with current source hashes.

## Follow retains active range and admits resized targets with half-edge approaches

Payoff50 closes TARGET-02.2 through original UnitAddAbility/Chaos, rather than
visual scale. Scenes60/61 clone hfoo with only collision `ucol` changed31 to63
or7. Original Chaos resolves replacement type from level-one `Cha1`/UnitID,
not DataA; `ucol` is unreal, not real. Wrong metadata produced either unchanged
type or a one-world-unit minimum mover footprint and is preserved as rejected
setup evidence. Public UnitAddAbility returns with the original type, and the
next authored tick observes the replacement. Target public handle1048700 and
canonical mover identity remain unchanged; radius words become1.96875 or.21875
fine cells. Active Follow keeps11.3125 fine arrival range and its original full
1015-commit motion, rather than forcing a radius/range recomputation.

Two fresh Smart150 requests are deliberate frozen same-clock inputs in each
fresh journey. Growth uses initial approach range `40e520f2` (7.1602716446 fine)
then persistent `41450000` (12.3125); shrink uses `41290000` (10.5625) for both.
Original Move_CreateTasksFromOrder5fd270 first calls05b580 to test predicted
center distance against the configured range including both radii. If already
near,05ae70 computes software sqrt(dx²+dy²), subtracts target then source radius,
clamps the edge distance to zero and converts it to world units. Initial task
range is half that edge distance; the later persistent task retains authored
FollowRange. The engine initially stopped at growth commit515 with the fully
configured range. The recovered approach rule restores every motion word.

The fifth journey retains Sca1's original Requires=Roch. Ability addition100
remains pending through149; public research unlock150 retains original type
inside that callback and151 observes63-world radius. Its full Follow motion
matches the unchanged-range control. Engine Chaos now owns the deferred work
through A_UPDATE, uses actual authored UnitID, research and requirement amounts,
rebinds the existing edict, and consumes the ability even for a same-type morph.
Removing the pending ability cancels resolution. Custom W3A requirements inherit
the actual parent profile; an explicit empty list clears that gate. RoC UnitID
and TFT UnitID1 fixtures cover non-stock radius59.25, inherited amount2 and
unchanged DataA37. No timer entity or new saved field is introduced.

All five ordinary engine journeys match5075 native absolute motion commits.
Save80 checkpoints50/350/700 restore9725 suffix commits, including pending and
research-gated ability state and both retained/fresh Follow owners. This verifies
the bounded stationary-target mutation during active pursuit; moving-unit radius
publication/occupancy, other body/locomotion families, combat/queued parents and
exact automatic Chaos callback-clock timing remain their owning tasks.

Artifacts under the report root:

- `runtime/follow-target-{grow,shrink}-v3-{first,repeat}-261002.jsonl`: retained
  range controls; each1015 commits and unchanged baseline motion digest
  `e85f0a94e7b22a0411d248dae105bd8f7ee96c14c9391c12108171a4e22c3869`.
- `runtime/follow-target-{grow,shrink}-v5-{first,repeat}-261002.jsonl`: two fresh
  reissues; growth digest `1e171ae90d8d4ae5b5224ae6712bc80bc264d2c61cd5eff0a4e22b69cacc8c11`,
  shrink `a1f1b632eb4c658810efab9d59b39a2f4d6c0f5ac43e579dfc5a43f87c87626c`.
- `runtime/follow-target-resize-gate-v3-{first,repeat}-261002.jsonl`: original
  Roch requirement and delayed same-identity radius change, each1015 commits.
- `runtime/follow-target-resize-source-v3-261002/`, `...-v5-261002/` and
  `runtime/follow-target-resize-gate-source-v3-261002/`: immutable producers.
  The retained control has an irrelevant Tcha research input; it is not the
  valid Roch gate experiment. Rejected earlier source/capture versions remain
  archived and are excluded from the strict accepted fixture.
- `runtime/follow-target-resize-fresh-engine-first-261002.log`: failing full-range
  initial approach; `runtime/follow-target-resize-chaos-lifecycle-first-261002.log`
  verifies consumption, requirements and cancellation (106 assertions/3 tests
  per variant). These lifecycle tests are engine regressions, not native pending
  removal captures.
- `runtime/follow-target-resize-ghidra-readback-saved-261002.json` and
  `runtime/follow-target-resize-types-final-261002.json`:539 saved roles,43 partial
  layouts/265 fields/245 explicit x86 prototypes/45 globals. Chaos prefix only
  assigns observed owner30, zero-based rank50 and cache54; no full-class size or
  new unverified ABI is asserted.
- `runtime/follow-target-resize-strict-final-261002/corpus-results.json`: fresh
  new contract plus13 preceding public/owner journeys;10150 exact motion commits,
  2160 scalar decisions and584 target reloads in the resize contract.
- `runtime/follow-target-resize-validation-261002.json`: final checks and current
  source hashes; inventory268 entries/109 fixtures/51 strict live entries.

TARGET-02.2 closes with126 completed/184 open leaves. Next runnable work is
FOOT-01.2: use the recovered public Chaos producer to resize a moving unit and
verify its own mover/routing/occupancy updates in parallel with engine behavior.

Payoff50 final checks: forced debug RoC/TFT each pass635,653 assertions in2342
WC3 tests; forced release and normal `make test` pass636,086 assertions in2379
WC3 tests per variant and the remaining repository suites. All238 pathfinding
Python checks, ability audit and268-entry inventory pass. Fresh strict execution
passes14/14 contracts with current source hashes and saved Ghidra readbacks.

## Moving radius changes retain point motion and scalar owner deadlines

Payoff51 closes FOOT-01.2 and NUM-02.9.1. A public point Move now survives
Chaos replacing its own moving unit type, updates collision-sized routing and
physical ownership, publishes the replacement's authored speed, and completes
with the original absolute motion words. This adds1179 ordinary engine commits
and4280 Save81 continuation commits per RoC/TFT variant. The bounded producer is
ground point Move; other locomotion and task families remain separate work.

Scene63 changes hfoo collision31 to63 during travel. Scene64 creates nine fresh
hfoo movers, changing each through a distinct authored Chaos alias to world radii
15.9921875/16/16.0078125,31.9921875/32/32.0078125 and
47.9921875/48/48.0078125. Native fine footprints become1/2/2,2/3/3 and3/4/4 cells.
Equality enters the next class. Each public handle and canonical mover generation
survives; the physical group changes. Scripted speed100 (retail minimum150) is
replaced by the new type's authored270. These are real public type changes,
not supplied expected poses or visual scale.

Original enabled delivery schedules a0.01 Chaos commit timer. The type commit
publishes radius through15fef0, then the retained point task is reissued after
another0.01 delay through5fd270/5ffb60. The engine retains both pending stages in
ability-owned state, commits the old velocity pose before rebinding, clears old
routing/occupancy, and reissues the preserved goal only after the task handoff.
Research notification is synchronous with the public tech publisher and schedules
the same commit delay. Pending state survives save/load. The observed delivery
is two primary5ms advances: adding0.01 once differs by one ulp at eight matrix
clocks. General delay arithmetic remains NUM-02.9, rather than being inferred
from this bounded delivery rule.

The90-second matrix exposed another production mismatch. Integer100ms public
timer countdowns and a fixed six-update movement-owner cadence eventually
separated from retail's scalar clocks. The owner first differed after41seconds.
Read-only053630 witnesses show periodic rearm uses the retained due deadline,
not the callback's primary clock. The public source token0.10 produces raw
3dcccccd; the owner's period is3cf5c290. The lower adjacent owner word is wrong.
Timer processing drains against the next primary quantum before publishing that
primary clock. Both engine cursors now use software scalar addition, retaining
the original domains and rearm words across load. Existing C timer callers keep
their millisecond API. Public getters, pause remainder, nonpositive/long inputs,
general heap tie order, catch-up and epoch-crossing producers remain open.

The complete native matrix has1061 commits (118 per case except equality48,
which has117), identical across two base captures and two read-only handoff
witness captures. The growth control has118 commits twice. A repeated research
control retains1015 commits. The accepted eight captures certify6510 commits,
4858 scalar decisions and40 public resizes. Each handoff capture also retains
3000 owner and899 public timer rearms. Secondary presentation clocks and spatial
cleanup counts remain archived but are outside the frozen deterministic domain.

Production regressions run the public JASS producer and ordinary engine frames.
Save81 checkpoints exercise pending enabled delivery, pending commit, the first
resized owner and late travel:254 saved growth commits and4026 matrix suffix
commits. The original supplied-clock spawn regression now also supplies the
corresponding original owner deadline; its old six-phase-only setup was no longer
a complete clock input. Its failing full-suite report is retained.

Artifacts under the report root:

- `runtime/moving-radius-source-v2-261002/`: immutable growth/research producers;
  `moving-radius-grow-v2-{first,repeat}-261002.jsonl` and
  `chaos-research-clock-v2-{first,repeat}-261002.jsonl` retain complete journeys.
- `runtime/moving-radius-matrix-source-v1-261002/` and `...-v4-261002/`:
  identical map inputs; v4 adds only read-only task/radius/timer witnesses.
  `moving-radius-matrix-v{1,4}-{first,repeat}-261002.jsonl` repeat the complete
  canonical motion digest
  `f420aeb7a123fc239627099a0a13c3f5135a4aeb8b76b95e588d2907c750411e`.
- `runtime/moving-radius-strict-final-261002/corpus-results.json`:25 fresh
  capture contracts pass with current production numeric sources, including all
  eight new captures. Inventory269 entries/110 fixtures/52 strict live entries.
- `runtime/moving-radius-ghidra-readback-saved-261002.json` and
  `runtime/moving-radius-types-final-261002.json`:546 saved roles,44 partial
  layouts/270 verified fields/246 explicit x86 ABIs/46 globals. Timer request
  deadline4, period8, clockc, flags10 and receiver18 have observed meanings;
  unassigned bytes and wider type-rebind ABI remain unclaimed.
- `runtime/moving-radius-full-debug-first-261002.log`: retained failing old
  supplied-owner-clock setup. `...-full-debug-v2-261002.log` passes690,372
  assertions in2344 WC3 tests per variant. `...-python-final-261002.log` passes
  all252 pathfinding checks, including dropped rearm/wrong period controls.

The next runnable task is FOOT-01.3: trace and port shared group maximum-radius
publication when the largest member changes size or disappears. Passage geometry
and the full reimplementation acceptance gates remain open.

Payoff51 final checks: forced release and full `make test` pass690,805 assertions
in2381 WC3 tests per variant plus the remaining repository suites. Ability audit,
269-entry inventory,252 Python checks and25 fresh capture contracts pass. The
backlog now has128 completed/184 open leaves, including the explicit split of
positive periodic scheduling from the remaining timeout/getter domains.


### Local group maximum and retained route footprint have separate lifetimes

Payoff52 ports FOOT-01.3.1 through actual public GroupPointOrder Move. Three
journeys grow the moving peer from31 to63, shrink the largest peer from63 to7,
or remove it. All831 complete normal-frame motion commits and730 owner footprint
states match original words per RoC/TFT variant. Four completed-frame Save81
checkpoints per journey resume2207 motion commits and2193 footprint states:
growth816/811, shrink832/827, removal559/555. Saved state includes the pending
Chaos stage, membership, live group maximum, retained route footprint and owner
counter. The save format is unchanged.

The initial local physical group has two members. Chaos preserves the peer's
public handle and canonical mover, then hands it to a new physical owner. The
surviving old group now has a lower live maximum; removal also leaves one member.
Original16c940 scans resolved live members when the group is unbound. However,
16ce10 only calls that getter and168be0 Path_SetFootprint when admitting a new
route. Existing path+b4 remains31 for the growth survivor and63 for the shrink
and removal survivors. The newly rebound peer samples63 or7. Recalculating a
live maximum must not invalidate a retained route solely because that maximum
changed. Move now updates the group maximum after membership pruning, while
G_UnitMoveGroupDestination retains the footprint until actual route admission.

Comparing owner states also exposed a counter/scheduler phase mismatch hidden
by the already matching motion words. Standalone point movers ran before the
counter advanced; retained cohorts ran after it. The generic ability owner
scheduler now dispatches A_OWNER_BEGIN before individual movement callbacks and
A_OWNER_UPDATE after them. Move publishes its counter and player-row budget in
the begin phase, then visits physical groups and repulsion in the update phase.
No movement rule enters the universal client or general lifecycle dispatch.

Twelve complete original captures use immutable v3 base and v4 read-only
footprint observers. All versions/repeats retain identical canonical phases:
3324 motion commits,3304 scalar decisions,2920 owner passes,20 actual routing
radius queries and1460 observed path+b4 samples. The fixture separates observed
cached footprint from the live maximum derived by the verified16c940 member
scan. Bound shared7c accumulation and other target/task families remain
FOOT-01.3; these unbound journeys do not certify them.

Artifacts under the report root:

- `runtime/group-radius-source-v{3,4}-261002/` and
  `group-radius-{grow,shrink,remove}-v{3,4}-{first,repeat}-261002.jsonl`:
  immutable accepted producers and full300-marker journeys. Earlier v1/v2
  setups accidentally issued a second singleton Move after the group order;
  interrupted/empty captures and all setup sources remain diagnostic archives.
- `runtime/group-radius-strict-final-261002/corpus-results.json`:26 fresh
  capture contracts pass with current production numeric sources. Inventory270
  entries/111 fixtures/53 strict live entries.
- `runtime/group-radius-types-final-261002.json` and
  `runtime/group-radius-ghidra-readback-saved-261002.json`:546 saved roles,
  44 partial layouts/271 verified fields/247 explicit x86 ABIs/46 globals.
  WC3PathPrefix extends monotonically to b8 to record scalar footprint+b4;
  all existing fields/types remain verified and unassigned bytes undefined.
- `runtime/group-radius-save-engine-first-261002.log`:48021 passing assertions
  per variant for the three complete journeys and twelve saved continuations.

FOOT-01.3's bound shared7c producer runs through Captain AI batching and requires
its own Move-owned shared publication layer; it remains open. The next runnable
engine comparison is FOOT-04.2's public blocked-goal lifecycle.

Payoff52 final checks: forced debug passes738393 assertions/2347 WC3 tests per
variant. Forced release and full `make test` pass738826 assertions/2384 WC3 tests
per variant plus the remaining repository suites. All263 pathfinding Python
checks, ability audit,270-entry inventory and26 fresh capture contracts pass.
The extra broad WC3 Python discovery report retains two unavailable external
ability-binary fixtures; the required pathfinding suite and repository tests are
green. Backlog129 completed/184 open leaves.

## Blocked point goals retain the click through retry and forced arrival

Payoff53 integrates FOOT-04.7 through an ordinary public ground Move. A Footman
at(-1936,-976), with authored31-unit collision and public speed100 clamped to150,
receives one Move to(-1936,-144). Twenty-five public terrain mutations block
that destination. Two independent unmodified retail journeys repeat all207
velocity/position/facing commits and205 motion decisions, including the final
stopped turns and natural order cleanup. The production engine now reproduces
all207 commits through normal five-millisecond frames without injecting poses,
routes, clocks, retry results or completion events.

The first engine mismatch was commit4: `unit_issueorder_now` corrected the user
waypoint to a nearby reachable point before Move admitted its route. Move now
retains the requested click. Its coarse group route, member adaptive route and
fine leg own their separate intermediate/adjusted destinations. This change
extended the exact prefix to177 commits. Passing the selected native fine goal
unchanged to fine search extended it to203; nearest-point correction had turned
the final failed search into a successful route.

Retail's group/member adaptive searches use5000/400 work, and fine searches use
700. The six complete captured searches produce5/4/25/2/6/1 route points. Both
failed final fine searches consume701 pops. Original166c30 publishes the failed
adaptive endpoint83.5/45.5 multiplied by2 as adjusted167/91, while retaining
requested163.5/91.5 separately. This is the retry operand, not a replacement
public click.

At owner1261, the reached partial fine endpoint consumes the null-target retry:
nearby singleton initializes2 without changing PRNG, returns1 with count1,
clears the fine index, retains the adaptive/group plan and defers refill until
the next owner. At1262, the rebuilt one-point fine route returns4 with count1
and its buffers retained. Original171060 sets mover D8 bit10000. The engine's
Move-owned `point_forced_arrival` preserves this state: it overrides range, while
the angular gate still turns at1263 and completes at1264. Translation remains
zero; neither endpoint nor destination receives a snap. Retail's can't-path
fallback clears both task/order heads and publishes ability flags16/unit flags513.
The engine ends the plain Move at the same final commit and stands normally.
Queued/target-specific recovery branches remain open.

Four Save82 checkpoints at6000/7110/7140/7170 milliseconds restore before the
partial leg, after retry1, after force, and during the final turn. All46 suffix
commits match the retail literal words per RoC/TFT variant. Save82 serializes
standalone point force; replacement and leaving Move clear it. Old versions,
including81, are rejected. The Hero save diagnostic now validates its own
reachable walking candidate rather than relying on the public waypoint being
rewritten. The existing speed/clock tests use larger synthetic worlds so their
1800-unit goals remain in-map; they exercise speed/clock rather than outside-map
policy.

Evidence is frozen in `retail-blocked-goal-1.27.json` and
`tests/retail_blocked_goal.h`. Reports under the external retail archive include:

- `runtime/blocked-goal-v2-first-retry-261002.jsonl` and
  `runtime/blocked-goal-v2-repeat-261002.jsonl`: full native trajectories, six
  searches each, retry/force/task cleanup and1000 primary owner callbacks.
- `runtime/blocked-goal-source-v2-261002` and
  `runtime/blocked-goal-source-provenance-final-261002.json`: immutable producer
  sources and exact archived source/map hashes. The initial Python-without-Frida
  setup failure remains a separate diagnostic.
- `runtime/blocked-goal-strict-final-261002/corpus-results.json`:27 fresh capture
  contracts pass against production numeric C; inventory271 entries/112
  fixtures/54 strict live entries. Secondary host/presentation subdivisions
  differ between captures; primary clock digest and canonical movement agree.
- `runtime/blocked-goal-ghidra-readback-saved-261002.json`:546 saved role comments,
  44 layouts/271 fields/247 explicit ABIs/46 globals, with no unsaved changes.
- `runtime/blocked-goal-save-engine-261002.log`:2575 passing assertions for the
  complete journey and four saved continuations.

This closes the bounded static blocked-goal producer only. Outside/overlapping
goals, target removal, other masks/classes, combat and queued fallback remain
FOOT-04.2 and their owning leaves. Full faithful pathfinding is still open.

Payoff53 final checks: forced debug full WC3 runs pass740971 assertions/2348
tests per variant; the subsequently added Stop/replacement inverse controls
pass2583 assertions per variant in forced debug. Forced release and full `make test`
pass741412 assertions/2385 WC3 tests per variant and the remaining repository
suites. All274 pathfinding Python checks, ability coverage audit,271-entry
inventory and27 fresh capture contracts pass. Backlog130 completed/184 open
leaves. The next runnable comparison is outside-goal bridge clipping.


## Outside point goals clip routing while retaining the public click

Payoff54 closes FOOT-04.8 for ordinary ground point Move. Public
`IssuePointOrder`/`IssuePointOrderById` admit the requested coordinates unchanged
into the point task. Original05b970 clips the routing coordinates to world
minimum plus four path cells and maximum minus four path cells before
subtracting the origin and dividing by32. In the captured world the bounds
are(-7168,-3072)..(5120,5120), cell32 and margin4, so the requested
(-7400,-976) remains the public click while the fine routing destination
is(4,65.5), world(-7040,-976). Instruction comparisons are strict: equality
retains the input word. The decompiler's unordered comparison spelling was
checked against COMISS/JA/CMOVA before implementing the helper.

Move now clips only its implicit group's routing goal. An admitted point route
owns its steering before generic flow-field reachability can replace the
public destination. The engine previously substituted(-3408,-976) and first
moved at1080ms, while retail started at1050ms. Clipping alone did not fix that
mismatch; preserving the admitted route's ownership does. Both original
outside-west captures now match all191 production position, fine pose, facing
and velocity commits through normal five-millisecond frames. No route, pose,
clock, retry or completion is injected. The eight captured searches retain
11/5/21/6/33/2/11/1 route points. Retry1 at owner1246 defers refill, retry4 at
1247 forces range acceptance, and1248 completes naturally.

Four Save82 checkpoints at5500/6630/6660/6690ms match47 saved suffix commits
per variant, including replacement and Stop inverse controls. The clipped
group goal and standalone forced-arrival flag already belong to the serialized
Move records, so this change needs no new field or save-format version.

The four-edge public matrix repeats12 lower/exact/upper neighboring inputs at
X=-7040/4992 and Y=-2944/4992. Each public order reports acceptance and retains
its raw task words while original routing operands match production C. The
original coordinate prefix additionally matches108 finite cases across three
origins, cell sizes16/32/64, both axes, both bounds and adjacent float words.
These prefix calls are separate from the complete32-cell public journey.
Outside placement, overlapping/removed targets, other masks/classes and
non-point task policies remain open.

The regression suite now distinguishes public click ownership from controlled
arrival-consumer arithmetic. Its retained native-pose test calls the actual
arrival consumer and preserves every frozen expected position/velocity word;
the new public journeys cover admission. Synthetic detour worlds leave room
for the recovered boundary margin, and the collision-sized blocked passage
asserts a partial route endpoint west of the wall while retaining the click.

Evidence under the external retail archive:

- `runtime/outside-west-v2-first-261002.jsonl` and
  `runtime/outside-west-v2-repeat-261002.jsonl`: complete motion, primary clocks,
  route/retry/cleanup and read-only original bound operands.
- `runtime/point-bound-matrix-v2-first-261002.jsonl` and
  `runtime/point-bound-matrix-v2-repeat-261002.jsonl`:12 accepted public edge
  inputs each. The earlier matrix without acceptance markers remains diagnostic.
- `runtime/outside-goal-source-provenance-final-261002.json`: exact immutable
  observer, script and map hashes for all four accepted captures.
- `runtime/outside-goal-strict-final-261002/corpus-results.json`:28 fresh live
  contracts plus the expanded original map oracle pass against current C.
- `runtime/outside-goal-ghidra-readback-saved-261002.json`:553 saved roles,
  44 layouts/271 fields/251 explicit ABIs/46 globals, with no unsaved changes.

`retail-outside-goal-1.27.json` and `tests/retail_outside_goal.h` freeze the
complete journey; `retail-point-order-clip-1.27.json` freezes the original
prefix cases. Seven new Ghidra roles connect public wrappers, dispatch and
raw point-task construction to the existing routing producer.

Payoff54 final checks: forced debug full WC3 runs pass743415 assertions/2349
tests per variant. Forced release binaries and full `make test` pass743848
assertions/2386 WC3 tests per variant plus the remaining repository suites.
The first forced recursive invocation inherited `-B` into the JASS dependency
check, whose required up-to-date assertion consequently failed; the normal
full invocation against rebuilt release binaries passes. All284 pathfinding
Python checks, ability coverage audit,272-entry inventory and29 fresh corpus
outcomes pass. Backlog131 completed/184 open leaves. Next runnable work is
the Captain AI shared-radius producer and its Move-owned engine consumer.


## Captain home recruitment and formation retries reach Move

Payoff55 ports captain formation ownership into `g_bot.c` and admits newly
recruited assault members toward an explicitly authored captain home through
production Move. The previous engine only reserved the roster; public
`StartCampaignAI` produced no movement commits. It also erased the home and
roster in `InitAssault`, then added every repeated typed quantity to a desired
counter. Keeping the roster without fixing that counter would leave retries
permanently short of an invented total.

Original `InitAssault` at9bc0f0/9c7b10 only sets attack-captain flag1.
`CaptainIsFull` at9b8a20 reads that flag. A typed recruitment shortfall clears
it in9c3550; a successful request leaves it unchanged. `CreateCaptains` really
creates replacement captain actors and resets their roster/flags. Engine
captains now retain home, roster and active goal across InitAssault, use the
same formation flag for fullness, and preserve the current member Move when
a retry merely counts an already recruited unit. The obsolete accumulated
quantity is removed. Size/empty queries still count live members separately.

Six immutable original captures repeat the stationary home, existing-roster
InitAssault, and full/shortage/retry predicates. Each produces178 physical
Footman commits plus two stopped virtual-actor commits. All1,080 captured
commits,1,062 scalar decisions and6,000 primary owner callbacks match
production arithmetic. This is complete native reference evidence, not full
engine captain parity.

The normal engine script starts through `StartCampaignAI` from the actual
100ms map timer and runs CreateCaptains, SetCaptainHome, InitAssault,
AddAssault, a shortfall and an InitAssault retry. Its first33 movement commits
match retail exactly; four Save82 checkpoints at1200/1500/1800/1995ms match48
additional admission suffix commits. The engine also naturally finishes home
travel and passes Stop, replacement Move and removal controls. No pose,
velocity, route or clock is injected. Saved evidence covers physical movement;
the bot VM/roster context is not a serialized AI continuation contract.

At2s, retail's membership callback9d9020 updates proximity counts and replaces
the recruit's virtual-target task with a private shared point request. That
replacement flushes its pose to clock1.9999990463256836 and zeros velocity
before the2.009997844696045 owner. Engine ordinary point Move first diverges
at commit33/2010ms: it integrates another10ms, producing a1.5-world-unit
position difference. The full178-row literal reference is retained in
`retail-captain-home-1.27.json` and `tests/retail_captain_home.h`; the regression
explicitly ends exact admission comparison before the handoff. The verifier
and corpus require `whole_engine_parity=false` and
`private_handoff_remains_open=true`. GROUP-03.4.1 remains open until the
complete stationary-home journey matches. The recorded first failed full
comparison remains `runtime/captain-home-first-port-261002.log`.

The initial Footman target range was first described as five radii.
[Payoff62](#private-captain-approach-range-is-independent-of-collision) corrects
that inference: authored124 plus collision31 happens to equal155. The
private point handoff uses minimum.49. The virtual captain has radius0,
category2, object-category0x01000002 and path-mask0x02000002, distinct from
Footman's radius31 and category202. Its actor range changes from500 to200
world units. CaptainGoHome moves this virtual actor and its followers, so a
per-unit point command does not implement that native. Moving virtual actors,
private range/task families, shared12+1 batches and their inverses remain
GROUP-03.4/FOOT-01.3.

Default CreateCaptains homes come from the native AI town object in9c5360/
9bb750; attack and defense use distinct town coordinates. That producer is
still missing in the engine. An unset home is now distinguished from an
explicitly authored(0,0) home, and unresolved default admission is logged
instead of sending recruits to an invented origin.

The owned native map configures Player0 as COMPUTER before AI-agent allocation.
Changing its controller after neutral-agent allocation cannot start the AI.
AI integer string natives are stubbed in this binary's AI binding, so probes
use literal markers and Boolean predicates rather than I2S. The map builder's
`captain_home` scenario packages `wc3_captain_probe.ai`; `--captain-ai` selects
an explicit alternate such as `wc3_captain_full_probe.ai` and records its hash
and adjacent AI source. A thirteen-member point diagnostic shows12+1 batches
and shared maximum63/32, but hits the10,000-event cap and is not accepted as
whole-motion evidence. Its source/capture stays diagnostic.

Evidence under the external retail archive:

- `runtime/captain-home-v2-first-261002.jsonl` and its repeat: authored home
  before InitAssault and one recruit's complete native motion.
- `runtime/captain-home-init-v1-first-261002.jsonl` and its repeat: literal
  size-one predicates before and after InitAssault.
- `runtime/captain-home-full-v1-first-261002.jsonl` and its repeat: Create false,
  Init true, successful recruit true, shortfall false and retained retry true.
- `runtime/captain-home-source-provenance-final-261002.json`: all six immutable
  observer, AI/map-script and packed-map source hashes.
- `runtime/captain-home-strict-final-261002/corpus-results.json`:29 fresh live
  contracts plus the original map oracle pass, retaining the explicit engine gap.
- `runtime/captain-home-ghidra-readback-saved-261002.json`:576 saved roles,
  45 layouts/287 fields/258 explicit ABIs/46 globals and no unsaved changes.

The captain prefix and full flag/native roles are persisted in Ghidra.
The member helper's verified ABI is ECX source unit, EDX opaque target,
stack4 nullable point,8 request array,c group-index pointer,10 member-count
pointer,14 policy,18 shared-enable and1c shared-wrapper, withRET1c. Target
requests pass a null point; treating it as a coordinate pointer caused an
initial diagnostic observer failure and is rejected.

Payoff55 final checks: full debug and optimized RoC/TFT each pass745,037
assertions in2388 tests, including the complete public AI admission regression
and existing movement/save controls. All299 pathfinding Python checks, the
ability coverage audit and30 fresh strict corpus outcomes pass. Inventory has
273 entries/115 fixtures/56 strict live contracts. Ghidra's saved readback has
576 roles/45 layouts/287 fields/258 explicit ABIs/46 globals. The initial
repository release run crashed in the host SDL3-backed SDL2 compatibility
library during a generic client input test; its retained core identifies that
stack. The full repository suite passes with the native SDL2 library used by
the debug runs (`runtime/captain-home-full-release-test-v2-261002.log`).
No assertion was removed or weakened. Full private handoff parity remains
explicitly open; backlog131 completed/185 open leaves.

## Stationary captain range callback and zero-radius occupancy

Payoff56: original `9d2f90` creates a category2 virtual actor
with zero radius. Fine class0 still covers one cell. Original follower routing
excludes that target; the private point task after `9d9020` includes it.
The readonly blocker capture `captain-range-blocker-261002.jsonl` confirms the
home actor at fine(163.5,91.5), category`01000002`/query`02000002`, causes12
object rejections in a701-pop limited private point search. Initial follower
search has no object rejections. Do not widen the stop radius to imitate this.

Actual singleton roster count changes the range from800 to825 world units
(`d3c7f0`, `d77f7c`=25). The four retained listeners at captain+74/+78/+7c/+80
use412.5/1225/825/1025 world radii and.5/.5/1/1-second periods. Canonical
region+48 is the event code, **not** the query's center. `15eac0` tests strict
squared distance using both predicted poses and the candidate's positive
radius. Equal boundary distance is outside. Region+1c retains its timer
request; the phase starts when the captain is created.

`0522e0` dispatches at the request's exact deadline then restores the primary
clock. Native creation word`3f7ffff0` produces deadline`3ffffff8`; engine
primary at2000ms is`3ffffff0`. Checking only the sampled clock fires5ms late
and moves the physical unit an extra.75 world units. Move's callback now
commits old velocity at the exact request clock before installing the point
task. The ordinary engine journey matches all178 original commits and778
saved suffix commits from eight states (1200/1500/1800/1995/2010/6255/6270/6300ms).
Stop/replacement/removal and captain recreation retain/release the virtual
target through Move-owned edict references. Save83 rejects older layouts and
persists logical actor ownership plus physical timer/task references; bot VM
execution is still process-owned. Moving captains, default AI town homes,
multiple recruits and broader range-event routing remain open.

Ghidra saved readback `captain-range-ghidra-readback-saved-261003.json`:590
roles,48 partial layouts,307 fields,260 explicit x86 ABIs,50 globals,
`unsaved=false`. New layouts distinguish16-byte listener bridge, canonical
region prefix and12-byte occupant rows.

The farther-source control starts at worldY=-1296 and enters on deadline
`403ffffc` (about3s), instead of the near case's`3ffffff8` (about2s). Both
original repeats retain identical complete physical/virtual phase hashes. The
engine matches250 additional native commits and1033 saved suffix commits.
An initial callback in the pose sampler ran before a due owner and differed
by16 fine-coordinate ULPs at3000ms. Dispatch belongs **after** the due owner
and before ordinary timer actions, matching the native event sequence.
`S_RunMoveTimers` owns the task transition; `S_PublishMovement` stays observational.

Reproduce the farther map with the bounded observer source archived as
`captain-range-far-source-v1-261003`, sourceY=-1296, and
`make_wc3_pathfinding_map.py --scenario captain_home --captain-source-y -1296`.
The native player must remain COMPUTER in config before AI agent allocation.
Read-only region probes are now retained in `wc3_pathfinding.js`; generic
observer counts do not enumerate these diagnostic range events. Their full
170-update timeline is independently checked by the captain-range verifier.

A further restore control stops the process-owned AI VM, restores the1200ms
physical snapshot, rebinds the saved logical captain actor and reproduces
another171/243 native suffix commits. Initial tests failed the runtime actor
link after load; rebinding belongs after edict restoration, while pre-save
validation must leave runtime bot links unchanged. In-range stale entity
references, duplicate actor ownership, invalid actor selectors and nonfinite
active callback state are rejected by the shared validator. The bot VM is
not needed for either complete physical journey. Total saved continuations
are now2225 commits across the two source positions.

Payoff56 final checks: debug RoC/TFT each pass770,701 assertions/2390 tests;
optimized repository suites pass770,702 RoC/770,701 TFT assertions/2390 tests,
plus generic repository checks with native SDL2. The invalid-reference fixture
uses memcpy for serialized index words to preserve optimized strict-aliasing
semantics. All309 pathfinding Python checks and31 fresh contracts (30 strict
live entries plus the original map oracle) pass. Ghidra's saved readback
remains590 roles/48 layouts/307 fields/260 ABIs/50 globals and`unsaved=false`.
The fresh upstream fetch is fully contained; no upstream merge remains.
Network contracts are unchanged; game save format is83. The next two-recruit
shared request is explicitGROUP-03.4.2 and has repeated native evidence; it
is not certified engine parity by this checkpoint.


## Stationary captain pair: private followers to shared arrival

Payoff57 composes the authored-home captain with two ground recruits at
world(-1936,-976) and(-1856,-976), both collision31 and requested speed100
(retail minimum150). Home remains(-1936,-144). Two complete native captures
repeat all185/184 physical commits and physical/virtual phase hash
`4e0dfd98da478aec7e8e02dd22d1f9e11809aceda0cc58f78ccedaf88ae93bd1`.
The production regression is
`wc3_movement.public_ai_pair_recruits_match_original_captain_home`, with
literal [369-commit input](../../../games/warcraft-3/game/tests/retail_captain_pair.h).

Initial admission creates **two separate singleton target-follow owners**,
not an initial shared formation. The AI-owned pool recruits the second-created
unit first; `9cf680` prepends its retained roster link, so the final roster is
first-created then second-created. Newest-first physical-owner visits therefore
commit in creation order. OpenRealm reverses fresh pool admission and prepends
its captain roster. Native owned-list ordering across edict reuse and ownership
changes still needs its actual producer; the explicit TODO is GROUP-03.4.4.

The first engine divergence was peer commit11 at1170ms: position/time matched,
but heading and velocity differed. A virtual captain must remain a **target
region** even while excluded as a blocker. Radius0 occupies one fine cell;
fine expansion observes that region and finishes at its perimeter. Omitting
`moveFineGraph_t.has_target` for the model-free actor admitted an extra route
point and aimed at the raw home instead. Existing fine reconstruction and
Follow routing kernels were correct; passing the actual target region restores
all later motion without adjusting headings or authored destinations.

Actual roster2 publishes membership radius850 world (800+25*2), plus each
candidate's collision radius in the predicted strict circle test. Both enter
on exact deadline`3ffffff8`. The first `9d9020` callback changes c4=0→1 and
publishes no point task; the second changes1→2 and gates one two-pass batch.
Both preparations share one wrapper/canonical request, count0 then1, policy1
and bindShared1. All152 shared footprint updates publish31/32 through shared7c
and pathb4. The final survivor retains this footprint after its peer finishes.

Move now persists logical roster count, recruitment index and entered state.
It retains independent follower groups until the all-entered gate, then uses
its existing shared point admission and formation. Eight saves at
1200/1500/1800/1995/2010/6000/6500/6525ms reproduce1648 suffix commits, including
restoration after the peer completes. A bot-free1200ms restore adds355 exact
suffix commits. Stop/replacement/removal before and after the handoff preserve
the peer's actor reference and allow immediate save. Move departure now
synchronously detaches its old group; previously Stop left an invalid captain
target owner until the next update. Captain recreation retains the retired
actor until both physical references end. Invalid count, duplicate/out-of-range
member indices, entry bits and stale references fail validation. Save84 maps
these fields and rejects prior layouts; network contracts are unchanged.

The readonly verifier is
[verify_wc3_captain_pair_trace.py](../../../tools/frida/verify_wc3_captain_pair_trace.py),
with [frozen producer/membership/footprint input](../../../tools/ghidra/fixtures/retail-captain-pair-1.27.json).
Rebuild the probe from the original interlude using
`make_wc3_pathfinding_map.py --scenario captain_home --captain-peer`; its
default AI source is `wc3_captain_pair_probe.ai`. Original captures use frozen
`runtime/captain-pair-source-v1-261003`. A premature continue key produced no
admitted AI journey and is diagnostic only; accepted captures continue at30s.

This pair does not consume a retry: native path94/98 stay0 throughout. Forced
pair retries, larger/mixed-radius batches, live attach/detach, moving captains,
default town homes and broader AI restoration remain explicit backlog work.
Saved Ghidra readback retains591 roles,48 layouts/307 fields,260 explicit ABIs,
50 globals and`unsaved=false`.


Payoff57 validation: full debug RoC/TFT and optimized RoC/TFT each pass794,650
assertions/2391 tests. Required `make BUILD=release -j4 test` also passes the
repository checks with native SDL2. All320 pathfinding Python checks,32 fresh
contracts and275-entry/117-fixture inventory pass. Saved Ghidra readback confirms
591 roles and `unsaved=false`. Fresh upstream is contained by the branch.


## Mixed captain pairs and blocked home retries

Payoff58 extends the complete stationary captain pair to Footman/Knight and to
a five-by-five blocked authored home. GROUP-03.4.3 owns this bounded extension;
larger rosters, moving captains and native owned-pool reuse remain open.

The mixed scene separately calls `AddAssault(1,'hfoo')` and
`AddAssault(1,'hkni')`. Creation order is Footman then Knight; physical follower
owner order and the later shared roster are Knight then Footman. Native radii
are31/32 and1 fine unit, and authored turn rates are0.6 and0.5. Both public speed
setters clamp100 to150. All185 Footman and184 Knight commits match normal engine
frames. The first attempted test inherited the Footman turn rate for the Knight:
its fine route already matched, but the first large turn diverged. The corrected
fixture explicitly authors the Knight rate. No numerical movement kernel change
was needed for this mixed scene.

Shared7c and cached pathb4 both publish32-world during151 owner visits. After the
Knight finishes, the last survivor publishes live31 while the retained cache
stays32. Eight saved prefixes resume1648 exact suffix commits; a bot-free restore
adds355, for2003 saved commits. The complete phase hash is
`b37868da1198d3aa05565fce168ad6750a5393841308363fdfb78ba30f79047f`.

The blocked pair exposes two production gaps. Native9d2f90 admits the zero-radius
virtual captain through ordinary point placement. Authored home(-1936,-144)
places the actor at(-2000,-240), fine161.5/88.5. Move now uses the existing
placement routine, and range membership tests this admitted actor rather than
the authored home. The later shared point request still carries the authored
home, so blocked destination routing and failure remain observable.

Original166c30 clears self and target spatial rectangles through15d360 before
adaptive admission, then rebuilds those rectangles and all three parents. The
rectangle is rounded into accelerator cells; unrelated edge terrain is cleared
temporarily too. The blocked target's base80/44 is mixed before admission.
Without exclusion the engine picks a different first fine leg and the peer
heads east. The engine now clears/restores these static traversal rectangles in
both group5000 and member400 admission. Point waypoints have no native spatial
target and must not receive this exclusion. Dynamic coarse cell-link composition
and general spatial dirty publication remain MAP-03.3.

All254 primary and270 peer commits now match, including two seven-count retry
initializations, shared random-owner changes, fourteen retry results, twenty
waiting steps, forced arrival at counters1310/1326, and ordinary task recovery.
The primary finishes first; the last peer retries with actual count1. Existing
fine/arrival/retry kernels handle this once admission geometry is correct.
Saved times1200/1500/1995/2010/7035/7050/8580/9060ms cover both sides of shared
admission, first retry, each forced arrival and the final survivor. These resume
2175 exact suffix commits; bot-free1200ms restore adds510, for2685 saved commits.
Save84 already retains the necessary physical owners, actor, retries and random
state. The full phase hash is
`b0a228243e654b451d11e5c8eb51c565691a7f5aa630f2a398c30bf4737e968b`.

Two uninterrupted owned captures repeat for each scene. The complete references
are `retail-captain-mixed-1.27.json` and `retail-captain-blocked-1.27.json`, checked
by the strict pair verifier against literal motion, callback nesting, footprint
publications and complete retry/buffer/cleanup lifecycle. Auxiliary cell watch
`captain-pair-blocked-v1-watch-261003.jsonl` distinguishes pre-edit clear and
pre-AI mixed classification; it does not justify deferred whole-map update
claims. Capture sources remain frozen in the external archive.

Current builder reproduces these scenes with `--scenario captain_home
--captain-peer --captain-peer-type hkni` or `--captain-blocked-home`. Accepted
captures keep their original source/map hashes. Ghidra persists the placement
and rectangle admission findings on9d2f90/15d360/166c30;591 roles,48 layouts/307
fields,260 explicit ABIs and50 globals remain saved with no unsaved changes.


Validation: full debug RoC/TFT and required release `make test` each pass850,914
assertions in2,393 engine tests, with repository checks passing.325 pathfinding
Python tests and34/34 fresh contracts pass; final corpus source hashes match.
Readback confirms saved Ghidra annotations. External
`captain-pair-extensions-validation-final-261003.json` pins the results/logs;
`captain-pair-extensions-strict-final-v2-261003/corpus-results.json` is the fresh
corpus report after builder changes. Both current CLI variants rebuild successfully.


## Captain owned-pool order survives transfer and reused slots

Payoff59 replaces the captain fill's reverse-edict scan with the native owned
pool's insertion order. Four public controls share the frozen Human02Interlude
terrain and authored home `(-1936,-144)`. Two complete captures per control
retain all motion, admission, range callbacks, retries and 1,000 primary owner
callbacks. [The fixture](../../../tools/ghidra/fixtures/retail-captain-pool-1.27.json)
and [verifier](../../../tools/frida/verify_wc3_captain_pool_trace.py) retain the
physical birth generations and complete literal journey; native process
addresses are not engine entity identities.

| Control | Public producer | Retail recruitment | Physical commits | Saved continuation commits |
| --- | --- | --- | ---: | ---: |
| Transfer | Older unit goes Player0→1→0 before AI starts | Older primary; newer peer remains idle | 178 | 805 |
| Same owner | SetUnitOwner(older,Player0,false) | Newer peer; older primary remains idle | 181 | 829 |
| Delayed reuse | Remove primary at0.5s; CreateUnit at2s, then start AI | Recreated primary in the reused lower slot | 179 | 1008 |
| Partial fill | Transfer, AddAssault(1), then AddAssault(2) | Retain primary; add peer and prepend captain roster | 369 | 1702 |

All 908 physical commits and 4344 saved suffix commits match production C through
normal 5ms frames. The transfer singleton has the same 178 literal words as the
existing home journey; choosing its actor was the engine defect. The partial
fill has 185 primary and 184 peer commits and one shared point batch after both
range callbacks. It does not duplicate the first recruit when the requested
quantity grows from 1 to 2.

`9c32d0` walks the owned head at28/2c and the canonical next link at24/28.
The owner transition `698ce0` publishes `d01a2`, then `9b9230` resolves the
current player's AI and calls `9c3660`. That insertion resolves the previous
head, initializes the new link with the unit and prior head, and publishes its
canonical identity14/18 as the new head. Assembly identifies receivers lost
by the decompiler's local allocation output; no p-code rewrite is required.
`9cf680` separately prepends the captain roster. Same-owner `698ce0` returns
without either operation.

Engine units now receive `own_seq` through their birth lifecycle and through
a genuine ownership change after owner-event publication. Captain fill chooses
the greatest eligible sequence, independent of entity number. Removing a unit
retires its record; reusing the slot assigns a fresh sequence. In the delayed
scene, native reuses the physical mover address with birth identity
`1110/1110→1109/1323`. The engine test exercises its real one-second allocator
quarantine and asserts the lower primary slot is live again; it never edits
`freetime` to manufacture reuse. The same-frame auxiliary reuse scene confirmed
retail selection but did not expose the engine bug, because its allocator had
not yet allowed lower-slot reuse.

Save85 maps both 64-bit insertion fields (`edict.own_seq`,
`level.next_unit_seq`) through the field schema. The round-trip crosses
`UINT32_MAX`, retains same-owner no-op state, then allocates the next sequence.
Validation rejects duplicate, out-of-range and missing live-unit sequences.
Eight saves per journey bracket recruitment, removed-slot state, private
followers, shared admission and final arrival. Continuations explicitly stop
the process-owned bot VM before loading; saved physical followers and owned
insertion order remain sufficient. The public timer restarts AI when loading
a pre-recruitment save. Save84 and earlier exact layouts are rejected.

Current builder exposes `--scenario captain_home --captain-peer
--captain-pool transfer|same_owner|reuse|partial`; frozen accepted sources and
maps remain external. Native files are `captain-pool-transfer-v1-*`,
`captain-pool-same-owner-v1-first-fresh/repeat-*`,
`captain-pool-reuse-v2-first-fresh/repeat-*` and
`captain-pool-partial-v1-*`, all under `runtime/` with suffix `261003.jsonl`.
The interrupted reuse attempt and overlapping preliminary same-owner run are
excluded. All eight accepted traces have uninterrupted complete numerical
rows,304 primary markers and 1,000 owners.

Ghidra saved readback verifies 593 roles,50 partial layouts/311 fields,263
explicit x86 ABIs and 50 globals with `unsaved=false`. Added structures describe
only the owned head and canonical link prefix; broader town/guard policy is
not inferred. GROUP-03.4.4 closes this ordering slice. Larger12+1 batches, live
attach/detach, moving captains and default town homes remain open.

Checkpoint59 validation passes the full debug and release RoC/TFT suites:
2,399 tests and 903,771 assertions per edition, 335 pathfinding Python tests,
35 fresh corpus contracts, and the ability-class audit. The full release
repository suite passes. External `captain-pool-validation-final-261003.json`
pins those logs and the saved Ghidra readback.

## Stationary captain three-member shared journey

Checkpoint60 extends the authored-home producer to three Footmen. Two frozen
`captain-three-v1-first/repeat-261003.jsonl` captures retain 304 primary markers,
1,000 owner callbacks and every numerical motion row. The physical birth order
is primary, peer, third; the owned pool walks newest first and captain insertion
prepends, yielding primary, peer, third roster order. Public AddAssault(3)
succeeds before independent private followers approach the retained virtual
actor. The third range callback at clock `3ffffff8` publishes one shared point
request; the first two only increase the entered count. Two-pass preparation
retains counts 0/1/2 and one shared wrapper/request.

The engine's previous two-member fallback diverges at physical commit 2,
1,020ms. Raising both stationary admission and saved-reference validation to
three members lets the existing shared-group implementation reproduce all
608 physical commits: 190 primary, 231 peer, 187 third. No numerical kernel or
expected movement word changes are involved. Eight saves at 995, 1500, 1995,
2010, 6390, 6420, 6750 and 7950ms produce 2,352 exact continuation commits.
The saves bracket recruitment, private following, shared admission and retry
progress. Restores stop the process-owned bot VM before loading, retaining
physical followers and captain references through Save85; no format change is
needed for the increased validated cardinality.

| Owner counter | Active members | Retry before → after | Result |
| --- | ---: | --- | ---: |
| 1236 | 3 | 0 → 6 | 1 |
| 1237 | 3 | 6 → 5 | 1 |
| 1247 | 2 | 5 → 4 | 1 |
| 1257 | 1 | 4 → 3 | 1 |
| 1267 | 1 | 3 → 2 | 1 |
| 1277 | 1 | 2 → 1 | 1 |
| 1287 | 1 | 1 → 1 | 4 |

Initialization chooses seven using the group owner's random state once. Member
completion does not redraw or reset that path budget. The terminal result sets
forced arrival `10000`; counter1288 completes cleanup. The retained engine
retry and active-cohort logic already has this behavior once captain admission
uses the correct shared producer.

[The fixture](../../../tools/ghidra/fixtures/retail-captain-three-1.27.json),
[verifier](../../../tools/frida/verify_wc3_captain_three_trace.py) and literal
engine reference pin both complete captures, producer/admission/range/lifecycle
sequences, primary clock and motion phases. The phase digest is
`7c238a17ed9ff210a8f62fa9bcabc27bb38bff803cfacc61898b3ac3178ada95`.
Host-time subdivision rows outside the primary domain vary with capture duration;
primary and physical motion sequences repeat exactly. Existing capped auxiliary
hierarchy/dirty/replan diagnostics do not certify complete publication. Builder
`--captain-peer --captain-third` reproduces the explicit three-birth scene;
accepted source trees and maps remain frozen externally.

Saved Ghidra annotations on `9d27c0`, `9d9020`, `1689d0` and `167290` record the
third-callback gate and shrinking cohort retry lifetime. Readback verifies
593 roles, 50 layouts/311 fields, 263 explicit ABIs and 50 globals with
`unsaved=false`. GROUP-03.4.5 owns this bounded extension. Thirteen recruits and
the 12+1 boundary remain GROUP-03.4.6; live attach/detach, moving captains and
default town homes remain open. Full debug/release RoC/TFT suites pass
2,400 tests/933,454 assertions per edition. The full release repository suite,
342 pathfinding Python checks and all 36 fresh corpus contracts pass. External
`captain-three-validation-final-261003.json` pins the verification logs.


### Stationary captain thirteen-member batch boundary

Payoff61 closes GROUP-03.4.6.1 with the complete homogeneous13-recruit home
journey. Frozen `runtime/captain-thirteen-source-v1-261003/` and
`PathingRE-CaptainThirteenV1-261003.w3m` create Footmen in a four-column grid:
`x=-1936+(i%4)*80`, `y=-976-(i/4)*80`, facing90, SetUnitMoveSpeed100
(clamped150). At one second the public AI calls AddAssault(13,hfoo).
`captain-thirteen-v1-first/repeat-261003.jsonl` each retain304 primary markers,
1000 complete owners,3647 physical commits and the entire uncapped numerical
phase/lifecycle stream. All phases, admission, ranges and physical words repeat.

The owned pool recruits in reverse birth order; captain roster prepending restores
birth order0..12. All13 enter callbacks run at clock `3ffffff8`; the first12
only incrementc4. The thirteenth callback prepares all13 before publishing orders:
request0 receives counts0..11, request1 receives count0. Both bind one shared
parameter wrapper. Native physical creation is12 then1, so the singleton batch
updates first. Treating that last member as an independent ordinary Move loses
this owner contract. Move now retains roster indices and explicitly creates both
bounded physical groups. Admission and Save85 validation accept13 stationary
references; the serialized format is unchanged.

Birth placement needs the original W3E support levels as well as WPM passability.
The reference supplies97x65 vertices at origin(-7168,-3072), independently hashed
expanded levels and1382 literal level runs. Runtime placement still computes the
actual legal locations; expected movement poses are never injected. The test also
runs normal map startup with the locked seed and original random-race preferences.
Its owner state before movement is4273436052/209508436. All57 retry transitions
match the native stream, ending591832212/1015043152; four become forced arrivals.

Replacement activation `166060` zeros path7c/80 admission timestamps. Move's old
reset only unlinked its fine request. One private follower retained timestamp1086,
which denied its new shared fine leg at1091 and first changed physical commit488.
Resetting the admission timestamp lets the normal router reconstruct the native
leg. The initial source-point hypothesis was tested separately and rejected:
unchanged production source admission matches the entire journey.

[The literal fixture](../../../tools/ghidra/fixtures/retail-captain-thirteen-1.27.json)
and `retail_captain_thirteen.h` retain all3647 commits, geometry, producer,
admission, range, lifecycle and1088 physical footprint samples. Physical commit
counts by birth are231/222/290/269/248/239/225/413/236/247/362/353/312.
The phase digest is `ab21c2cb20d9179cd3fdd1035bbee61b370a415b20b64bc9af161f2922a22629`.
`--captain-thirteen` rebuilds this explicit input scene. All3647 normal engine
commits and15768 saved suffix commits match across saves995/1500/1995/2010/
6390/7750/10000/13500ms. Restores retire the bot VM and retain physical owners;
pre-recruitment saves restart the public AI timer. The three-member control still
matches608 commits and2352 saved suffix commits, now checking the real PRNG too.

Saved Ghidra comments on9d16c0/9d27c0/9d11a0/9d1040/9d9020/166060 retain the
batch and fresh admission contracts. Readback verifies593 roles,50 layouts/
311 fields,263 explicit ABIs and50 globals, `unsaved=false`. Capped auxiliary
hierarchy/dirty events remain outside completeness. Mixed13 first/repeat native
captures are already complete and identical: the radius63 mover belongs to the
thirteenth singleton batch and both physical paths receive shared63/32. Porting
that cross-batch parameter lifetime remains GROUP-03.4.6.2; this homogeneous
closure does not certify it. Larger rosters, live detach/recruit, moving captains
and default town homes remain open.


Full debug/release RoC/TFT passes2401 tests and1127850 assertions per edition.
The required full release repository suite,350 pathfinding Python checks and37
fresh corpus contracts pass. `runtime/captain-thirteen-strict-final-v2-261003/`
records all fresh reports with134 matching final source fingerprints.
`captain-thirteen-validation-final-261003.json` pins logs and saved Ghidra readback.


## Private captain approach range is independent of collision

Payoff62 ports ordinary ground captain recruits' actual approach range into
Move. The complete mixed13 reference places one radius63 Footman in the final
singleton batch and twelve radius31 Footmen in the first physical batch. The
old engine5*collision/32 formula first stops the large recruit at commit1572,
4620ms. Original `9d86f0` instead calls the enabled attack maximum getter
`4985c0`: Footman range90 times software.6 plus70 gives124 world units.
`05a5c0` adds source collision and target collision before dividing32; the
virtual captain contributes0. Private arrival ranges are4.84375 and5.84375,
not a common multiple of each mover's radius. The older mixed pair's range100
recruit uses(70+.6*100+32)/32=5.0625; its previous engine assertion of5 was
also corrected from the retained original arrival observer.

Readonly `captain-thirteen-mixed-range-v2-first/repeat-261003.jsonl` each retain
304 markers,1000 owners,5462 physical commits,5194 decisions and5465 complete
velocity/arrival/route-step events. All14 authored-range calls repeat, including
one later reentry; the initial13 distinct units/attack objects report90 and124.
Runtime constants are70,600 and300. Auxiliary attack getter call counts differ
with host-time AI work; the captured scalar inputs and simulation movement are
stable. The first extra probe's hooks were accidentally inside the disabled
numeric-event option. That capture does not establish helper execution and is
excluded from this range contract.

Production Move stores the computed range in the existing physical member,
which Save85 already serializes and validates. It remains independent of later
weapon changes. Map-local UnitWeapons rows previously resolved straight to the
base table and ignored authored edits. Stable per-map merges now inherit
original-object edits before custom edits, preserve custom identity, and supply
range/enable bits to normal public spawning and restored data bindings. A
nonstock177.25/288.75 inheritance regression checks both slots, independent
original/custom rows, enabled-mask semantics and cleanup. The literal movement
scene supplies actual90/100 weapon inputs through object data; no pose, velocity,
route, clock or expected output is injected.

`public_captain_thirteen_mixed_matches_original_private_prefix` matches3471
commits through9000ms and17368 saved suffix commits across995/1500/1995/2010/
4000/5000/7750/8750ms. Homogeneous three/thirteen and owned-pool controls retain
whole-journey parity. The full mixed comparison now first differs at commit3478,
9030ms, when separate batch radii require the missing common shared owner.
The complete5462-row reference is retained in `retail_captain_thirteen_mixed.h`
and [the approach fixture](../../../tools/ghidra/fixtures/retail-captain-approach-1.27.json).
Its strict verifier explicitly requires `whole_engine_parity=false` and
`shared_owner_remains_open=true`. A bounded prefix is not full mixed closure.

Ghidra retains named range/attack/eligibility functions, two explicit ABIs
(RET8 and RET4), and the three runtime constants. Saved readback verifies597
roles,50 layouts/311 fields,265 ABIs and53 globals with `unsaved=false`.
`--captain-approach-events` exposes the reusable readonly Frida hooks.
No-attack/disabled attack, Hero/summoned producers, native40000000 policy,
Siege-roster bonus is verified by Payoff121 below; target-adjusted eligibility remains03.4.6.2.1.2.
Shared publication/cached footprint and full mixed recovery/cancellation remain
03.4.6.2.2/3.


Full debug/release RoC/TFT passes2403 tests and1336346 assertions per edition.
The required full release repository suite and357 pathfinding Python checks pass.
Fresh `runtime/captain-approach-strict-final-261003/corpus-results.json` passes38
selected contracts with135 matching final source fingerprints.
`captain-approach-validation-final-261003.json` pins validation logs and saved
Ghidra readback. Failed initial range-probe/corpus/test runs remain archived and
do not certify this final result.

## Shared captain parameters across unequal physical batches

Payoff63 adds a Move-owned shared parameter pool for the mixed thirteen-recruit
scene. Each all-entered publication gets a new 64-bit identity; both the twelve
Footmen and the final singleton large recruit retain that owner. Pool growth
and slot reuse preserve bindings by identity. Reentry allocates a new identity
rather than extending the lifetime of the virtual captain actor.

The owner pass publishes the previous minimum-speed accumulator, resets that
accumulator to FLT_MAX, clears the radius, and accumulates all still-bound live
mover generations before either physical batch routes. Routing samples this
shared maximum while each retained path keeps its own cached footprint. In the
first native phase both paths use63/32; after the large recruit completes the
live maximum drops to31/32 while the surviving path retains63/32. Native owner
identities `[1471,1941]` and `[1471,2193]` share a recycled slot but remain distinct
generations. The complete captures retain353 shared footprint observations and
325 original speed-publication observations across both lifetimes.

Captain point admission also replaces the old private approach through the
captured bridge05ca50 stop/recovery chain. Moving recruit7 is embedded at the
handoff: retail consumes its old velocity and admits166.5/78.5 before binding
the new request. Reusing only the ordinary point-order replacement misses this
relocation and first differs at movement commit3478. The engine now performs
that bounded recovery before captain shared admission. The captured group policy
also requires the66-tick classification-denial cooldown and member eligibility
after65 owner visits; preserving this state keeps the established blocked home
journey exact. Wider mover-special and projected-priority policies remain open.

The engine first private/shared phase matches **4560 absolute movement commits
through12000ms**, **127 shared footprint observations**, and **9618 continuation
commits from eight saves**. Save86 retains the shared pool, group binding and
cooldown, rejects stale bindings/nonfinite parameters/incorrect reference counts,
and releases a partially rejected pool atomically. Reference zero is valid until
the next owner prepass reclaims it. Version85 is rejected before restoration.
This is bounded engine parity: retail first starts a new private approach after
a captain range departure at12.03 seconds; the later16-second all-entered
publication and the rest of the5462-commit journey remain separate work.

The broad deferred-member-removal experiment did not resolve this callback and
regressed established control journeys, so it is not part of the implementation.
The engine keeps its existing member retirement while that composition remains
open. Complete native captures and their existing approach contract remain
unchanged; `retail-captain-shared-1.27.json` adds the generation/publication/
footprint contract, and `retail_captain_shared.h` is its literal127-row reference.
`verify_wc3_captain_shared_trace.py` checks both accepted original captures and
explicitly refuses a whole-engine or recovery-reentry parity claim.


## Captain range departure into a private approach

Payoff64 extends the mixed thirteen-recruit engine journey to **4867 exact
physical movement commits before15000ms**, with **7917 continuation commits
from eight saves** around the handoff and departure. The complete5462-row
retail reference remains unchanged. Move retains the one-second, creation-phase
range deadline while a physical follower runs its shared point leg. At the
outer-circle departure it consumes the old velocity at the exact deadline,
performs the bounded stopped-position recovery, and admits a new private
virtual-target owner. The newest physical owner runs before the remaining
shared batch. Its arrival range remains `(70+.6*90+31)/32 = 4.84375` fine units.

Two complete read-only captures,
`runtime/captain-thirteen-mixed-membership-v1-first-261003.jsonl` and
`runtime/captain-thirteen-mixed-membership-v1-repeat-261003.jsonl`, retain304
markers,1000 path-owner updates and5465 velocity commits each. All34 range-count
changes,38 callback begin/end observations and15 member-order calls repeat.
Thirteen calls are initial private admission; the later calls occur at4 seconds
and12 seconds. The4-second call already targets the captain and does not replace
that private task. The12-second call replaces the shared point order for
physical birth12. Canonical unit births are authenticated by task admission
between the corresponding mover publications, authored rawcodes and both
roster-order shared preparations; authored-range first-seen order is different.

Ghidra bodies9d05e0/9d8d50/9d8eb0/9d87d0 establish the counter and admission
chain. `9d05e0(mode1)` changes captainc4; mode0 changesc8. Initial outer-circle
registration through9d8d50 incrementsc8 and registersd01cd. Subsequent departure
is9d8eb0(mode0), which decrementsc8 and reissues target Move. At12 seconds the
raw clock is`413fffff`, owner counter1424, c8 changes13-to-12 and c4 stays12.
The callback consumes the old pose through05ca50 before the new12.03-second
owner. This is a range event, not a can't-path event or a member-sort fix.
The outer contact threshold is `(1000+25*13+31)/32`; the virtual actor's radius
is zero. Native group binding leaves`[1843,2123]`, passes through the unbound
state, and publishes private`[1820,2170]`.

`retail-captain-departure-1.27.json` pins both complete capture hashes and the
literal counter/callback/reissue/stop/binding contract. Its strict verifier
also reruns the complete existing numerical, primary-clock, authored-range and
shared-parameter contracts. Eight mutation tests reject wrong counts, deadlines,
births, generations, stopped velocity, arrival range and expanded parity claims.
Saved Ghidra readback retains604 roles,51 layouts/318 fields,270 explicit ABIs
and53 globals. Save86 already stores the actor and exact deadline; no serialized
field changes are needed. Validation now checks inactive shared followers'
finite deadlines, home coordinates and unique roster indexes as well. Engine
regressions reject six malformed shared-follower saves and preserve the last
physical reference across reissue after logical actor ownership is retired.

Full logical membership after individual physical tasks finish is still
GROUP-03.4.6.2.3.2. The engine does not yet publish the second shared generation
at16 seconds: its physical reference lifetime currently loses completed roster
members. That task must retain logical c4/c8 membership separately and reproduce
the full5462 commits and353 shared footprint observations. Dynamic roster
mutation and mixed cancellation remain their own explicit controls.


## Logical captain roster survives physical completion

Payoff65 completes GROUP-03.4.6.2.3.2 for the stationary, alive mixed thirteen-recruit
home scene: **all5462 retail movement commits,353 shared footprint observations
and11132 saved continuation commits agree**. The engine reproduces both12+1
shared generations, largest-recruit departure/reentry, and natural physical
cleanup through31000ms. This closes the scene's complete journey; moving captains,
dynamic roster changes and wider AI policy remain explicit tasks.

A physical Move completion releases its task reference but does not remove the
unit from the captain's logical roster. Move now retains that separate logical
actor reference, inner membership, outer membership, roster index and exact
creation-phase deadline. Completed members continue participating in range
notifications. Birth12 reenters the outer circle at15 seconds and inner circle at
native clock`417fffff` at16 seconds. The inner count becomes13, so the all-entered
request includes the twelve completed members and publishes a fresh shared owner.
Both groups initially use63/32; live maximum falls to31/32 at owner1561 while
cached footprint stays63/32. A retired logical actor is freed only after both
logical and physical references end.

The first full engine attempt stopped matching at the second publication.
Retaining logical membership moved the first failure to commit4908: birth8 turned
by+0.6 instead of-0.6. Its previous partial-route flag survived new task admission,
blocked projected classification and left no fresh fine route. Original166060
activation writes flags88=`00200000` and indices74/78=-1. Move's common progress
reset now clears partial state, counts and indices as well as the request timestamp.
That restores the complete original journey without changing the scalar kernel
or substituting a route from a capture.

The literal regression checks nine complete native range deadlines, including
exact inner/outer birth masks and counts after physical members finish. Eight
Save87 checkpoints at995/9030/11995/12030/15995/16000/16020/17000ms reproduce11132
suffix commits. Save87 adds logical actor and outer-membership fields to the
recursive schema and rejects version86 before world restore. Negative controls
reject invalid raw booleans, foreign or retired logical actors, missing ownership,
duplicate roster indices and nonfinite deadlines. Runtime bot VM state is not
serialized; Move's saved roster references independently reproduce movement.

The strict `retail-captain-reentry-1.27.json` contract authenticates both previously
frozen complete membership captures, all original movement/producer/range words,
both shared generations and every logical deadline. It hashes the departure and
shared fixtures and requires the generated footprint/membership header to match
literally. Historical prefix contracts keep their original limited scope.
Saved Ghidra readback has605 roles,51 layouts/320 fields,271 explicit ABIs and53
globals. The retained request values at captain+d4/dc and nullable-target request
refresh9d4600 are now typed; activation and membership consumers retain payoff65
annotations. Generic proximity traversal order and moving-target policy remain
outside this bounded witness.

Full debug/release RoC and TFT each pass2407 tests/1798757 assertions; the
required release repository suite and381 Python checks pass. Fresh
`runtime/captain-reentry-strict-261003/corpus-results.json` passes41/41 contracts
with138 matching source fingerprints. Inventory is284 entries/126 fixtures/67
strict live entries. GROUP-03.4.6.2.3.2 closes with141 completed/188 open leaves;
the next runnable task is mixed13 cancellation03.4.6.2.2.2.


## Largest captain recruit Stop before and after shared admission

Payoff66 verifies the existing Move implementation against two complete public
Stop journeys. Stopping birth0 at8.9 seconds, before shared admission, matches
**5458 complete native commits,353 shared footprints and11764 Save87 suffix
commits**. Stopping it at9.2 seconds, after admission, matches **5593 complete
commits,400 shared footprints and12572 suffix commits**. Both native repeats
agree. No further steering change is needed: payoff65's logical roster survives
Stop and the shared owner continues to serve the remaining physical group.

The after-admission Stop consumes the old velocity and releases the singleton's
physical members. Payoff67 corrects the binding retirement phase: the empty
singleton remains bound through the next shared prepass and retires in its
physical owner visit; the reference count then becomes1. That prepass changes
live maximum63-to31, while the surviving path keeps its cached63 footprint.
Later all-entered reentry creates a second owner generation in reused parameter
storage. Largest-only Stop preserves logical inner/outer membership; a later
captain publication can command that unit again. Two literal full-motion headers
and all753 footprint observations protect these behaviors in the engine.

Eight checkpoints per variant span immediate Stop, the surviving group, later
private departure and second shared publication. The engine checks synchronous
physical detachment, logical actor retention, native1/2 shared reference counts,
retained cached footprint, pool reuse and final natural collection in each
continuation. This extends production regression coverage without manufacturing
a new movement heuristic. Save87 needs no format change.

The old retry observer records two adjacent words from path+a4. They are not one
identity: original16631b loads+a4 as a fine-target record pointer and166325 checks
its live slot+38. The next word+a8 belongs to the separate blocker identity resolved
by1680f0 from+a8/ac. Both native cancellation repeats record the pointer's owner
through virtual captain movement-mask and velocity `fineObject` snapshots.
The verifier authenticates that relationship and replaces only the address with
its canonical mover birth; it preserves the blocker word and rejects foreign or
aliased records. No unexplained address is discarded. Ghidra now retains the
fine-target prefix, both path fields and the complete166310 ABI.

The early map is copied from the accepted short-name late map: only `war3map.j`
changes Stop tick92 to89; all other data/AI/object members remain byte-identical.
The regenerated archive listfile is recorded separately. One missing-map attempt
was interrupted and archived as rejected; directory/listfile packaging failures
preceded the successful variant. Neither supplies accepted live evidence.

The new strict cancellation contract authenticates four complete captures,
full producers/ranges/motion/lifecycle/shared state and literal C headers. It
explicitly leaves cancellation of the final binding open: natural cleanup and
largest-only Stop cannot prove every Stop/RemoveUnit/retarget reclamation branch.
GROUP-03.4.6.2.2.2 is split into those two separately reviewable controls. Full
debug/release RoC/TFT pass2409 tests/2199857 assertions per edition, the required
release repository suite and393 Python checks pass. Fresh
`runtime/captain-cancel-strict-261003/corpus-results.json` passes42/42 contracts
with139 matching source fingerprints. Saved Ghidra readback retains606 roles,52
layouts/323 fields,272 explicit ABIs,53 globals and no unsaved changes. Inventory
is285 entries/127 fixtures/68 strict live entries. Leaf03.4.6.2.2.2.1 closes with
142 completed/188 open leaves; explicit final-binding retirement is next.


## Final captain binding Stop and repeated AI initialization

Payoff67 fixes two concrete lifecycle mismatches. Two complete readonly retail
captures issue public Stop to all13 mixed captain recruits at9.2 seconds, then
call StartCampaignAI again at17 seconds. The engine matches all **3549 physical
position/velocity/facing/clock commits and12 shared footprint observations**.
All later physical motion remains absent, including eight saved continuations.

Original15aa80 publishes shared parameters through16c220 before accumulating
live member radii and visiting16c150 physical owners. Atcounter1331, after the
public Stop, the shared owner still has two references.16c150 prunes stale
members and retires empty physical groups. Counter1332, exact clock4113d4ca,
sees zero references and destroys the shared owner. The observer records identity
before the original call; afterward it is[-1,-1]. Engine Stop previously freed
an empty shared group immediately, collecting its owner one update too early.
Move now leaves an empty shared group bound until its physical owner visit.
Save88 accepts that live empty group, preserving the binding and retirement
phase. Unbound empty groups remain invalid. Unused pool capacity after load is
not an owner identity and need not match allocator capacity before load.

The first engine regression also reproduced3549 commits before failing on extra
movement at17 seconds. Retail's second AI call reads sources without executing
main again. Full9cbc00 assembly proves thiscall ECX AI/stack4 filename/RET4:
9c87f0 reads common.j, common.ai and the requested source on every call;
AI+248 nonnull skips9146f0 VM construction,90cb10 environment creation and
9c8d10 entry initialization. +24c stores the opaque environment. Engine G_BotStart
now retains the existing VM and script rather than queuing or performing a
restart. Missing repeated sources are diagnosed without destroying the old VM.

Save88 retains a player initialization bitmask separately from runtime bot VMs.
The eight checkpoints at9195/9200/9205/9230/16995/17000/17030/18000ms preserve
pending empty bindings, the zero-reference owner, collection and the later
no-replay gate. They produce no additional movement commits. Private AI VM
coroutines remain runtime-only; a saved initialized player without a VM emits
an explicit continuation diagnostic and does not execute main again. This is
not full AI coroutine restoration. Version87 and earlier formats are rejected.

The strict last-binding contract hashes both captures and their frozen source/
map metadata, all movement/producer/admission/range/lifecycle state, authenticated
pre-destructor identities, every shared publication and both public AI calls.
It requires the literal engine header. Mutation checks reject missing repeats,
premature collection, nonzero collection references, replayed main and broader
claims. Ghidra retains the typed AI prefix and initialization ABI, and annotates
owner, physical retirement and shared collection ordering. Saved readback has607
roles,53 layouts/325 fields,273 explicit ABIs and53 globals.

Full debug/release RoC and TFT each pass2410 tests/2236214 assertions. The
required release repository suite and404 Python checks pass. Fresh
`runtime/captain-last-binding-strict-261003/corpus-results.json` passes43/43
contracts with140 matching source fingerprints. Inventory is286 entries/128
fixtures/69 strict live entries. Final-binding RemoveUnit/retarget, new parameter
reuse after explicit cancellation, dynamic roster ownership and moving captains
remain separate.

## Public CaptainGoHome and moving virtual captain

Payoff68 implements the missing `CaptainGoHome()` AI native through the current
AI player's attack captain. `api_ai.h` and `g_bot.c` only dispatch; Move owns the
virtual actor, retained roster, point requests and idle-member handoff. The public
retail wrapper `9c40c0` resolves the attack captain through `9c64c0`, then jumps
to `9d2670`. It has no arguments and uses cdecl; the captain methods use ECX.

The owned readonly south scene stops all thirteen recruits at9.2s, repeats
StartCampaignAI at17s without restarting main, then changes the occupied home
from(-1936,-144) to(-1936,-1424) and calls CaptainGoHome at18s. Only the AI
member changes in the97228-byte map; the inherited terrain, units and wrapper
remain identical. Both uncapped captures reproduce the same initial6251 commits:
6058 physical commits and193 moving virtual-actor commits through23.8 seconds.
They also agree on398 shared footprints. The exact literal reference is
`games/warcraft-3/game/tests/retail_captain_go_home.h`; the public AI regression
runs ordinary simulation frames and matches11228 saved suffix commits across
eight checkpoints at18030/18100/19000/20000/21000/23000/23500/23700ms. Save88
already stores the relevant movement parameters, callbacks and references;
no save layout or network contract changes.

`SetCaptainHome` on an occupied logical roster authors the new request point
without teleporting the actor. Explicit GoHome admits a radius-zero, category2
virtual mover with the minimum effective roster speed (150world units/second in
this scene), turn0.4 and movement window0.1. This actor has no ordinary unit data
or health; movement must neither dereference UnitData nor prune it as dead.
Its fine object keeps `01000002`, path mask `02000002`, and publishes the moving
object flag through the same consumer as other movers. Treating it as permanently
stationary first diverged at23.07s when birth10 incorrectly avoided retail's
four-tick yield. The engine now preserves that moving-object distinction.

`9d44d0` retains the authored request in captain+d4/dc and its range in+e4,
while+58/60 hold the current point request. GoHome's retained range is500world
units; an all-entered callback at23s replaces the actor's physical request with
range200 while preserving500 for the idle-roster policy. `9d2ee0` compares
predicted world distance squared with `2*r*r+5000` for these ranges; it returns
normalized full EAX0/1 (`XOR EAX,EAX; SETNC AL`). The near-home wrapper `9d3480`
uses500. Initializers019720/019890/001c30 establish500/5000/2; reading the
uninitialized PE global bytes gives zeros and is not evidence of runtime values.
The near-home public regression checks that a64-unit home change followed by
GoHome leaves the actor and existing physical approach request in place.

Shared roster requests retain native12+1 batching. After the23-second callback,
identical active AI point requests keep their shared generation rather than
replacing it. Initial and moving generations are1471/1941 and1471/2237. The
native idle-roster path `9d8a90` calls `9cff90` with the retained point/range:
birth8 finishes at23.37s while the captain is still outside that request area
and starts a private approach; birth1 finishes after the captain has entered
that area and becomes idle. Comparing only the actor's tightened200-unit
arrival range incorrectly reissued birth1. First target refresh also preserves
the admission destination when both points have the same `floor(fine)>>1` cell.

The full moving journey is **not** certified. The first remaining mismatch is
a private birth8 fine retry at23.91s: retail produces three reconstructed points
`(159.5,55.5),(159.5,54.5),(160.3397827,54.5789986)` with result-1 and12 pops;
the engine retains a two-point partial route. Its source endpoint resembles
the prior23.82s pose; stale route/source publication is a hypothesis requiring
a direct consumer trace, not a confirmed cause. The engine regression ends
before that mismatch. The captures themselves close normally and their velocity
observers are uncapped, but their wall-time endings do not prove natural journey
completion. Autonomous occupied-home admission, retreat/empty-captain defaults,
larger rosters and other moving-captain policies remain separately open.

Evidence: `retail-captain-go-home-1.27.json`,
`verify_wc3_captain_go_home_trace.py`, and frozen archive sources
`runtime/captain-thirteen-mixed-cancel-all-go-home-south-source-v1-261003`.
The strict verifier authenticates both full capture hashes and observer/footer
counts, verifies their scalar commits and primary clocks, then compares only the
bounded engine motion/footprint reference. It rejects whole-journey claims.
Saved Ghidra readback contains613 roles,53 layouts/328 fields,281 explicit ABIs
and58 globals with `unsaved=false`.

Validation: full debug/release RoC/TFT pass2412 tests/2427132 assertions per edition.
All416 Python pathfinding checks and44 fresh corpus contracts pass with141
source fingerprints; inventory287 entries/129 fixtures/70 strict live entries.
The required release `make test` also passes.

## Changed captain destination resets pending waits

Payoff69 fixes the first remaining private follower mismatch from payoff68.
The extended31-second reference first reproduced birth8's mismatch at23.91s.
A direct engine breakpoint then showed `wait=20`, `retry=0`, `path.valid=0`,
old two-point partial fine storage and old adaptive goal165.540665/75.648605,
even though the member's destination had changed to165.502075/73.118240.
The cached fine source was not a new prediction failure: the pending neighbour
wait bypassed the refill and kept the old route. This supersedes payoff68's
stale-source hypothesis.

Retail counter1821 begins that same member update with delay20. Both the coarse
and member replan predicates report changed1/ready1 with timestamps0/0.
`16fbd0` calls `168b80` before advancing the route/wait. `168b80` invokes
`168740` with mode-1, clear-retry1, unlink0 and clear-results1. Both buffers
reset; `1687e0` clears path+94 delay, path+98 retry and path+88 progress bit.
Timestamps, allocated storage and blocker identity are preserved. The member
then rebuilds its coarse route and its three-point partial fine route with12
pops. Its heading becomes `3fffd0f5`, speed remains zero, and delay is zero.
The accepted destination change must reach this reset before the engine's wait
gate; erasing blocker identity is not part of the native reset.

Move now compares the previous and new member destinations in native shifted
fine-cell buckets while publishing the member destination. A changed bucket
clears fine/adaptive counts and indices, partial state, retry and delay before
`move_group_decide`. This uses the existing destination-admission policy;
delayed readiness/timestamp producers remain TARGET-02.1. No captain-specific
branch, numerical kernel, save layout or client contract is added.

The same two readonly public GoHome captures now match7419 exact engine
commits through27.2s, including306 moving virtual commits and624 shared
footprints. The new literal header contains only the1168 motion rows and226
footprint rows after payoff68's prefix, so the previous reference remains
independently authenticated. Eight additional checkpoints at18030/23000/23820/
23905/23910/23940/25000/27000ms reproduce10606 exact saved suffix commits,
including before and after the accepted changed destination. The original
6251-commit regression also remains exact.

The next mismatch at27.27s is **one-point fine refill behavior**, not a different
formation destination. Both retail and engine retain birth4's adjusted destination
151/59. Retail's fine search toward161/47 exhausts701 pops with one reconstructed
point equal to the current source161.499832/49.912647. The native caller discards
that fine index, changes its adaptive index7→2, keeps retry6 and turns toward
the selected coarse waypoint while committing zero speed. The engine publishes a zero-buffer state and turns toward the final adjusted
member destination. Immediate retry consumption of the new one-point endpoint
is a hypothesis to check at that caller. This requires a separate refill/return
policy port; it is not evidence to change formation layout or fine A* ordering.
Neither27.2s parity nor closed captures prove the whole moving journey.

`retail-captain-retarget-1.27.json` and
`verify_wc3_captain_retarget_trace.py` retain both capture hashes, the original
GoHome fixture hash, counter1821's normalized20→0 witness, initial/tail header
composition and repeated full scalar/primary clock checks. Ghidra adds explicit
ECX/stack ABIs for168b80,168740 and1687e0 and updates their caller comments;
saved readback retains613 roles,53 layouts/328 fields,284 explicit ABIs and58
globals with no unsaved changes. GROUP-03.4.7.2.1 closes this reset slice;
GROUP-03.4.7.2.2 owns the one-point refill and remaining moving journey.

Validation: full debug/release RoC/TFT pass2413 tests/2627883 assertions per edition.
All429 Python checks and45 fresh corpus contracts pass with142 source
fingerprints. Inventory288 entries/130 fixtures/71 strict live entries.
The required release `make test` also passes.

## Partial fine refill and stopped coarse handoff

Payoff70 closes the one-point refill slice of GROUP-03.4.7.2.2. The public
all13 Stop/south-home/GoHome scene now matches7933 exact motion commits through
30 seconds, including327 virtual actor commits and812 shared footprints. Eight
checkpoints at23820/23905/23910/27000/27265/27270/27275/28000ms reproduce7310
exact saved suffix commits. The independent6251/7419-commit references remain
unchanged and exact after the upstream AI integration.

Three consumer errors were distinguished from formation or search policy:

1. At27.27s, birth4's formation destination151/59 already matches. Native
   counter1933 fine search toward161/47 exhausts701 pops against budget700,
   with712 nodes and one reconstructed point. That point is the exact source
   161.499832153/49.912647247, not the nearest cell centre161.5/49.5. The engine
   breakpoint confirmed the correct predicted source but the wrong point in
   its fine buffer. `G_BuildUnitMoveLocalRoute` supplied the nearest centre as
   the reconstruction goal; the correct pure kernel then overwrote the only
   source point. Supplying the requested goal retains the native fractional
   source. Multi-node partial routes retain their reconstructed nearest point
   unless its cell matches the actual goal.
2. `167070` handles fine index0 with a nonzero adaptive index by consuming an
   accelerated waypoint through `165d10`, invalidating the fine index and
   returning2. It preserves retry/delay. Counter1933 advances adaptive7→2 and
   retains retry6. The engine instead treated this intermediate endpoint as
   terminal retry, reducing6→5 and turning toward the final destination.
   Move now selects the retained adaptive buffer with the existing verified
   `wc3_acc_select` kernel, clears only fine count/index and retains its route
   allocations, goal, timestamps and retry state. Final adaptive0 still uses
   the separate terminal retry path.
3. At29.16s, counter1996 advances adaptive2→0 with a tiny heading error
   word941096960. `16fbd0` starts its stop argument at1 and clears it only for
   `Path_Advance` result0; result2 therefore stops even while facing the route.
   The first coarse-handoff port relied on the turn window and published
   moving velocity with a changed heading. Explicit stop preserves the
   consumed waypoint heading4027cd9c and zero speed, while integrating the
   previous velocity through the existing native pose/clock consumer.

The implementations remain in Move and the WC3 game-owned world query. No new
client contract, persistent field or format change is introduced beyond the
upstream merge's existing Save89 layout. This is ordinary partial/coarse
progression, with no birth-index or captain-specific production branch.

The provisional31-second test first failed only its footprint extent after
30.03s; those motion words still matched. The authored30-second `complete`
marker disables the Frida pair/footprint observer, while velocity capture
continues. Its last shared footprint counter is2024 in both captures. The
accepted engine reference ends at30 seconds and authenticates all812 observed
footprints. A closed uncapped capture or that marker does not establish
natural journey completion; a private follower is still active later. Longer
travel needs a fresh observation domain and remains GROUP-03.4.7.2.2.2.

`retail-captain-refill-1.27.json` and `verify_wc3_captain_refill_trace.py` pin
both complete capture hashes, both earlier fixture hashes, the exact-source
refill and explicit stopped-handoff witnesses, all three composed headers and
full scalar/primary-clock checks. The new header adds only514 motion rows and
188 footprint rows. Eighteen controls reject source snapping, altered search
budgets/goals, changed retry/index state, lost stop, missing repeats/virtual
motion and overstated extents. Ghidra saves the corrected reconstruction and
167070→165d10→16fbd0 consumer comments. Readback confirms613 roles,53 layouts/
328 fields,284 explicit ABIs and58 globals, with no unsaved changes.

Validation: debug and release RoC/TFT each pass2477 tests/2804132 assertions.
The required release `make test` passes, as do all447 Python pathfinding checks
and46 fresh strict corpus contracts with143 source fingerprints. Corpus
inventory is289 entries/131 fixtures/72 strict live entries. The saved Ghidra
readback and both earlier GoHome references remain green.

## Blocker removal owns static route invalidation

Payoff71 closes the existing MAP-03.5 and MAP-03.6 lifecycle tasks together.
`G_FreeEdict` previously cleared a static blocker without rebuilding its baked
footprint. Deferred public `RemoveUnit` also hid a building without publishing
that change to the static map. Both paths could retain an obsolete flow field
and adaptive classification. The regression reproduces six failures across
live destructibles and retained dead rubble: the removed cell stays blocked,
the old field still activates, and the next direct segment remains obstructed.

The game now uses the bake's existing static-entity predicate before removal.
Direct free rebuilds after the edict is cleared; deferred removal rebuilds as
soon as `RF_HIDDEN` becomes authoritative. Final reclamation of that already
hidden unit needs no second bake. `G_RemoveDestructable` delegates invalidation
to central free. Rebuilding from the mutable terrain baseline preserves other
objects at the same location and live bridge decks. Nonempty death textures
remain blocking until their owner is actually removed. The existing revision
invalidates flow fields and causes Move to rebuild all static adaptive lanes
on its next request. No saved state or client contract changes.

The dedicated public `blocker_lifecycle` JASS scene uses actual worker Harvest
orders, overlapping stock `LTlt` trees, a six-gold `ngol`, and an `LTg1` gate.
Two uninterrupted retail runs agree on all13 lifecycle snapshots and all376
motion decisions/382 velocity, position and facing commits per run. Four fresh
point orders per run execute fine searches after the new grids are published.
Seven nonnull pathing collections per run return from complete `650c00` with
`Widget+34 == NULL`; Ghidra identifies the real region retirement and Storm403
free before that store. This closes the former emulator's final-free exclusion
for these public lifetimes.

The watched16×16 fine patch is `[155,70,171,86)` on the original384×256 map.
Effective **static blue** occupancy includes terrain and eligible widgetc2
regions, excludes dynamic unitca regions, and respects the original lazy-link
visit order: mark an eligible object seen before testing link kind. A retired
newer link suppresses older active history for that same object. The observer
uses a local set and writes no game state. All four native hierarchy levels,
including an unchanged highest mixed parent, are compared separately.

| Public state | Blocked fine cells | Changed hierarchy cells by level vs baseline |
| --- | ---: | --- |
| Baseline | 80 | 0 / 0 / 0 / 0 |
| Both trees / first tree harvested with peer retained | 96 | 4 / 4 / 2 / 0 |
| Both trees removed | 80 | 0 / 0 / 0 / 0 |
| Mine with six gold | 144 | 16 / 4 / 2 / 0 |
| Mine depleted / finally removed | 80 | 0 / 0 / 0 / 0 |
| Gate alive / restored | 120 | 10 / 5 / 2 / 0 |
| Gate killed / finally removed | 80 | 0 / 0 / 0 / 0 |

Engine tests exercise direct live/dead-rubble free, death/restoration/removal
with overlapping footprints and independent terrain, real partial final gold
extraction with a waiting worker, warmed adaptive routing, and two overlapping
buildings removed through public JASS. The latter submits Move in the same
callback and reaches its goal through ordinary frames. Field activation fails
for obsolete generations before reclamation. The allocator-only mining fixture
must explicitly use `SVF_MONSTER`, like runtime units; otherwise its workers
become static blockers. Its next fine search also respects the ordinary request
throttle instead of requesting twice at the same pathing counter.

Frozen acceptance is `tools/ghidra/fixtures/retail-blocker-lifecycle-1.27.json`
and `live-blocker-lifecycle-captures-261003` in the strict corpus. Raw captures,
map and authenticated controller/observer sources remain under
`runtime/blocker-lifecycle-v2-{first,repeat}-261003.jsonl` and
`runtime/blocker-lifecycle-source-v2-261003/` outside the repository. The first
probe spawned the worker in terrain and failed depletion; it is rejected,
not a resource witness. The corrected scene approaches from the open south.
The observer freezes its own output/counts before its footer and kills only
its owned spawned process before detaching, avoiding cleanup stalls in hot
hooks. Attached processes retain their lifecycle.

Saved Ghidra readback adds `Destructable_EnterDeath`,
`Destructable_RestoreLife`, and `Widget_RetirePathing`:616 roles, unchanged53
layouts/328 fields,284 explicit ABIs and58 globals, `unsaved=false`.
General dynamic coarse publication and the complete producer inventory remain
MAP-03.3. This contract covers blue widget lifetimes, not every authored mask
or complete retail resource-worker motion in OpenRealm.

Payoff71 validation: required release `make test` passes; RoC/TFT each pass2481
tests/2804207 assertions. Debug affected suites pass:45 destructible tests/318
assertions,79 pathfinding tests/650 assertions, and the focused mining/removal
checks. The initial full debug run exposed an allocator-only idle-unit fixture
without `SVF_MONSTER`; the corrected helper mirrors runtime dynamic occupancy.
454 Python pathfinding checks and four fresh strict widget/lifecycle contracts
pass with145 source fingerprints. Backlog148 done/188 remaining; no task IDs
were added. This validates removal and next-request behavior, not a complete
OpenRealm reproduction of the retail worker trajectories.


## Authored widget creation preserves snapped pose and rotation

Payoff72 completes existing MAP-02.3. Retail `CreateDestructable` for stock
`LTlt` and `LTg1` at(-1936,-560), requested facing0, publishes(-1920,-512).
The shipped rows supply `fixedRot=270` degrees. OpenRealm previously parsed
that field as Boolean and published the unsnapped request: the rectangular
gate blocked the wrong axis. The new engine regression reproduces18 failures
before the creation fix.

`Destructable_CreateAuthored` (`6c0d90`) obtains the scalar degrees through
`Destructable_GetAuthoredFixedRotation` (`6c1770`, cached row+90), overrides
facing only when that value is nonnegative, and publishes through its widget.
`PathTexture_SnapWidgetPoint` (`22f410`) first calls
`Widget_ClampWorldPoint` (`04c330`). The latter's upper margin is32, established
by the actual CRT initializer001c70; it is not an arbitrary epsilon.
Snapping uses wrapped integer conversion, signed truncation to multiples of64,
and signed32/16 offsets selected by the rotated texture dimensions' parity.
Production scalar helpers match288 snap cases and27 lower/upper clamp cases
against complete original functions, including fractional negative coordinates,
odd/even rectangular dimensions, orientation and large finite inputs.

The game module now parses `fixedRot` as float degrees, retains the caller's
angle for negative values, clamps and snaps the public object pose before
linking. Fresh spawning and map-script placeholder activation share this
constructor policy. Generated-script binding compares the requested snapped
point against the preplaced snapped pose, including a hidden placeholder's
retained alive texture; an off-grid request must reuse the same object rather
than duplicate the blocker. Missing optional `fixedRot` columns explicitly default to-1
in the engine schema; the native stock witness uses authored270. No save,
network or universal-client layout changes are needed.

Two complete retail captures create the actual file-backed tree and gate in
both orders, then remove the first and remaining objects. All18 observed
snapshots agree: each contains1024 static fine masks and1360 classifications
across four lanes and four hierarchy levels. The32-cell patch is aligned to
its16-cell highest parents, preserving every child required to reproduce the
captured hierarchy. Baseline has340 ground-blocked cells, tree356 and
gate/both380. Independent terrain remains after both removals. Remove invokes
alive retirement and the authored death-widget stage before final retirement:
each run observes eight destroy calls, six nonnull collections actually freed.

The engine test loads minimal4x4 and20x4 TGA numeric fixtures at the archive's
actual `PathTextures\4x4Default.tga` / `PathTextures\Gate1Path.tga` paths,
uses the same map dimensions/bounds and captured terrain, and compares all fine
masks and all four cached hierarchy levels after every creation/removal. The
literal C arrays are checked against frozen native JSON, so changing the
engine does not regenerate its expected answer. The patch excludes dynamic
unit occupancy; arbitrary angle/colored mask decoding and walkable surfaces
retain their existing backlog scope.

Frozen acceptance is `retail-widget-overlap-1.27.json` and strict entry
`live-widget-overlap-captures-261003`. Historical controller/observer sources
are immutable under `runtime/widget-overlap-source-v2-261003/`; captures are
`runtime/widget-overlap-v2-{first,repeat}-261003.jsonl`. The canonical builder
now provides `--scenario widget_overlap_orders`; the observer's
`--blocker-patch-size 32` reproduces the aligned all-static-lane patch.
Default16 retains the earlier blue-only lifecycle contract. Historical source
hashes authenticate the external package, not the subsequently extended
canonical scripts. The canonical builder reproduces the historical map byte
hash515c0b0195401a765d64401b0e2f20424ced8a573b60bda49b3d6577385f1b3e.
Ghidra saves four new verified function roles,620 total;
existing53 layouts/328 fields,284 ABIs and58 globals remain unchanged.


Payoff72 validation:46 affected destructible tests/26589 assertions pass;
required release `make test` passes RoC/TFT with2482 tests/2830478 assertions
each, plus461 Python pathfinding checks. Strict C11 O0 compilation and five
fresh widget/lifecycle contracts pass. Saved Ghidra readback is unchanged after
save (`unsaved=false`). The backlog is149 done/187 remaining, with no added IDs.


## File-backed maps retain native hierarchy allocation

Payoff73 closes existing MAP-02.1 with two complete public file-backed map
loads. Read-only Frida observes `PathMaps_Load` (`04c860`) after it returns,
before object constructors: actual Storm279 load of `war3map.wpm`, CDataStore
header/payload deserialization, ordinary factories/registration, and complete
hierarchy initialization. The archive contains MP3W/version0,384x256 cells;
all98304 resulting native fine flag bytes equal the actual WPM payload's
original mask decoder. Source, binary, map and capture hashes authenticate
both runs. This is the full file branch, not the earlier supplied mask-loop
slice or no-file fixture.

The full capture exposes an engine allocation bug hidden by interior-only
comparisons. Original `15ab60` adds16 to the fine dimensions, truncates division
by2, adds1, then shifts for three parents. The engine previously rounded the
half dimensions to multiples of8. Original base/parent sizes are201x137,
100x68,50x34,25x17; the engine's old base was192x128. Original initializers zero
all allocation padding. Rebuild includes one cell beyond the fine rectangle
and its affected parents, while remaining allocation padding stays zero.
Treating that unused padding as blocked terrain produces different classes.

Move's cached static hierarchy now follows those dimensions and initialization
limits. The original base ground classification mask is06000006, distinct from
an individual mover's fine mask2; the other lanes are80,40 and4. Cache lookup
still uses the mover's lane identity, then the base classifier uses the correct
coarse mask. A dedicated no-fly cell test preserves fine ground admission while
asserting its mixed ground/fly coarse classes. The cache-isolation fixture now
uses a ground/float/amph wall and retains an independent pre-edit air result:
clearing ground2 cannot open a coarse mask6 passage while no-fly4 remains.
No recorded retail movement words or expected trajectories were regenerated.

The engine regression builds and reopens a real MPQ containing the numeric
WPM fixture, calls the production `CM_ReadPathMap`, and compares98304 movement
cells plus145848 lane classifications across every allocated hierarchy cell,
including borders and unused padding. It repeats through the no-file adapter
with the same decoded terrain. Literal numeric RLE inputs/outputs are checked
against frozen original JSON and the original WPM byte hash. This avoids
checking a cache against another copy of its own classification algorithm.

The new canonical observer flag is `--map-load-events`. Historical sources
are immutable under `runtime/map-load-source-v1-261003/`; two complete captures
are `runtime/map-load-v1-{first,repeat}-261003.jsonl`. Frozen acceptance is
`retail-map-load-1.27.json`, strict entry `live-map-load-captures-261003`.
Existing original no-file construction, all256 WPM byte decoders, coordinate
and widget contracts continue to pass. Generic image/failure/reload domains,
other map origins/corners and dynamic coarse publication retain their
existing BASE/MAP tasks; no new task IDs are added.

Ghidra now retains the24-byte CDataStore,108-byte common map header, eight
owner map/search pointers and typed `PathWorld_Owner` global. Explicit
DataStore read, constructor and file-loader ABIs are verified from assembly
and complete consumers. Saved readback has622 roles,55 layouts/357 fields,
288 ABIs and59 globals, `unsaved=false`.

Payoff73 validation: the full file/direct-map test passes509515 assertions;
required release `make test` passes RoC/TFT with2483 tests/3339994 assertions
each, plus470 Python pathfinding checks. Six fresh strict load/map/widget
contracts pass. The movement suite's2762164 original assertions retain its
recorded scalar/trajectory expectations; its isolated terrain fixture gains
one air-baseline assertion. Backlog150 done/186 remaining; no added IDs.

## Constructed map corners and padding reach engine regression coverage

Payoff74 closes existing MAP-01.2 and MAP-01.3 together. The map-construction
oracle now retains25 complete original no-file loads from the actual packed
terrain endpoint producer, including negative origins and non-power-of-two
fine dimensions4x4,16x24,32x20,68x36 and128x96. It uses the constructed game
origin, fine/proximity maps and four-level hierarchy without replacing them
before coordinate consumers. Proximity dimensions are(fine+16)/8+1; base
dimensions are(fine+16)/2+1 and parents are truncating shifts. Initial fine
and proximity cells retain00ffffff; adaptive storage starts zero and original
rebuild classifies only the clipped fine rectangle plus its inclusive border.

Every map exercises all four corners with both axes independently taking
predecessor/equal/successor binary32 words and rounded decimal offsets-0.1/+0.1.
All2500 complete04d870 calls retain exact fine words, wrapped floor integers,
actual cell/null decisions and separate original multiply32/add-origin inverse
words.675 inputs are accepted;1825 are rejected. At zero origin, the negative
least subnormal input80000001 scales to fine zero and is accepted in cell0:
a host comparison against the world bounds would reject it incorrectly.
An exact maximum is outside the fine map. These results come from complete
original consumers, not a geometric acceptance model.

The engine regression invokes its terrain query/write API and the actual
Move world/fine adapters against every literal row. It compares all41090
allocated hierarchy cells in all four lanes before edits, after restored
corner requests, after four-lane corner edits and after their reversal.
Original explicit rebuilds freeze the edited classes; the engine continues
to use its documented static bake invalidation policy. All original fine
cells and hierarchy cells restore exactly. The full original proximity
allocation/initialization is asserted; the engine retains its Quake2
BoxEdicts consumer instead of allocating a duplicate native proximity grid.
Dynamic coarse publication and retail storage lifetime remain their existing
tasks. No network or saved entity layout changes.

Frozen numeric expectations are in `retail-constructed-map-coordinates-1.27.json`
and `retail_constructed_maps.h`; Python checks keep every word, index, edit,
RLE run and map dimension synchronized. The existing oracle-map_construction
entry requires25 complete loads,2500 original/C corner cases,400 lane edits
and25 exact grid reversals. No new oracle, corpus entry or TODO ID is added.
Saved Ghidra622-role annotations now retain these constructor/edit contracts.
The focused production regression passes930530 assertions. Fresh strict
map_construction/maps/file-load contracts are in `runtime/map-coordinates-strict74/`.
See [corpus](retail-pathfinding-corpus.md#constructed-map-coordinates) and
[backlog](retail-pathfinding-todo.md#map-01--map-coordinates).

Required release `make test` passes RoC and TFT with2484 tests/4270524
assertions each, and473 Python pathfinding checks. Backlog152 done/184 open;
no added tasks. Artifact:runtime/map-coordinates-make-test74.log.

## Passage matrix covers lanes, footprints, corners and offsets

Payoff75 closes existing FOOT-02.1/02.2 in one chunk. The existing fine-grid
oracle's new `--passages` mode runs3200 original core searches, retained-stamp
repeats and complete fractional148100 requests. Fifty geometry cases cover
vertical/horizontal widths0..5, six L-corner widths in four rotations, six
touching-rectangle gaps, four map edges and four individual lane-bit walls.
Each combines four footprint classes, masks2/4/40/80 and offsets.125/.875,
.5/.5,.875/.125 and decimal.1/.9. The width sweep includes below/equal/above
every1/2/3/4-cell footprint diameter. Original cost, charged work, node
creation, complete/partial parent chains and C search agree on every case.

Each complete request also invokes original149370 twice on its source/goal.
Its verified ABI is ECX fine system, stack4 software XY pointer, stack8 mask
pointer, stack12 class, RET12 and normalized EAX result. The original native
consumer owns flooring and footprint bounds. Six thousand four hundred
endpoint results and all reconstructed route words are frozen;2720 pairs
have both endpoints admitted,1664 searches complete and1536 retain partial
chains. Native SEH scratch must be mapped; do not pass a null ECX. Decimal
source/goal inputs must be rounded to binary32 before comparison, rather
than comparing native binary32 outputs against Python binary64 decimals.

The engine compares all6400 endpoint decisions through its production Move
footprint API. For2720 admitted pairs, its complete local-route builder
compares every frozen complete/partial point, source, endpoint and failure
result. One-point nearest-source failures correctly produce no usable turn.
Blocked endpoint correction belongs to public admission and existing FOOT-04
tasks; this seeded-map matrix does not claim full public clock/steering or
dynamic object producers. Static touching footprints are represented by
their rasterized rectangles. No engine-versus-original matrix mismatch
remains, so there is no additional minimized counterexample or TODO.

The existing production implementation passes55776 new assertions. Numeric
fixtures are `retail-passage-matrix-1.27.json` and `retail_passages.h`; only41
unique rasters and864 shared interior route words are retained in the C
fixture. Python ties every C input, geometry, endpoint, route word and
result back to the original capture. Ghidra retains622 function roles,
55 layouts/357 fields,289 explicit ABIs and59 globals; the new footprint
consumer signature is saved. No network or saved entity layout changes.
See [corpus](retail-pathfinding-corpus.md#passage-matrix) and
[backlog](retail-pathfinding-todo.md#foot-02--passage-matrix).

Required release `make test` passes RoC/TFT with2485 tests/4326300 assertions
each and475 Python checks. The final strict endpoint/route-word digest
contract adds one Python check; all476 pass afterward, and all five fresh
strict variants pass with final source fingerprints in
`runtime/passage-strict-final75/`. No C changes follow the full suite.
Backlog154 done/182 open, no new IDs.

## Full queue composition preserves partial goal centres

Payoff76 closes existing FINE-02.1 and strengthens the already completed
FINE-02.2 integration. One48x48 static map naturally combines886 equal
queue keys, one cheaper closed-node reopening and190 stale records. The
complete original core, retained-storage repeat and fractional148100 request
produce1068 identical pops. Each frozen pop records unsigned key, node
identity, entry/current generation, charged work, g/h, parent, node state and
queue count. Production C at O0/O2 and the real engine local-route builder
match every word, full reconstructed route and repeated backing reuse.
No expansion, queue, relaxation or allocator call is stubbed; storage and
static input cells are provisioned. Public admission and movement clocks
are separate existing tasks.

A second complete original request uses the actual ordinary700 work limit.
The goal43/43 is already admitted when the denied next iteration charges
work701. Original148100 returns failure and a50-point nearest partial route
whose endpoint is43.5/43.5, despite the supplied click43.25/43.75. The engine
previously reconstructed against that click and replaced the partial centre
because both occupy the same cell. The regression reproduces exactly two
wrong endpoint words through the actual mover's700-work producer and fine
owner accounting. In world coordinates, the wrong endpoint was1384/1400
rather than1392/1392.

Move now supplies the native nearest-node centre to reconstruction for a
non-source partial chain. A one-node failure retains its precise original
source; the requested click remains available for refill/retry. Complete
routes and target exits keep their existing exact-destination policy. All
frozen expectations remain original words, including the complete queue
record and source/goal poses. The new regression also requires exactly701
charged owner work; a discovered goal is not successful until its valid pop.

A read-only pop observer exists only under BZ_TESTS/BZ_WC3_FINE_TRACE. Ordinary
production builds have no observer fields or branch. The strict corpus
runner enables the probe define only for queue-composition variants. Ghidra
now retains36-byte fine-node and12-byte heap-entry prefixes, and actual
fine-header stamp/table/count/budget/nearest fields:622 roles,57 layouts/
381 fields,289 explicit ABIs and59 globals. Unknown node+0x20 bytes remain
undefined. No edict, save or network layout changes.

Numeric acceptance is `retail-fine-queue-1.27.json`/`retail_fine_queue.h`.
Run existing `verify_wc3_pathing_grid.py --queue-composition` through the
strict runner's `oracle-grid-queue-composition-engine` variant. Frozen original
pop/route words are reasserted before C comparison. The original48x48
complete request costs990, equal to its independent Dijkstra reference;
this does not assume heuristic optimality for other scenes. The engine
regressions repeat25500 exact queue/route assertions and106 limited-work
assertions; all five current pathfinding acceptance tests pass1521427
assertions. See [corpus](retail-pathfinding-corpus.md#full-queue-composition)
and [backlog](retail-pathfinding-todo.md#fine-02--search-termination).

Required release `make test` passes RoC and TFT with2487 tests/4351906
assertions each, plus479 Python checks. Four fresh fine/partial/passage/queue
contracts pass with149 source fingerprints; the original queue/control also
passes production C at O0. The ordinary release game target builds with the
observer absent. Artifact logs are `runtime/fine-queue-{make-test,production-build}76.log`;
strict output is `runtime/fine-queue-strict76/`. Backlog155 done/181 remaining;
no added IDs. Original path-owned166e90 calls148100, so its partial centre
policy applies to the mover consumer as well as the direct request fixture.

## Fine stamp wrap preserves complete engine request state

Payoff77 covers existing FINE-03.2 and MAP-05.2 together. The existing grid
oracle seeds the fine request counter once at`fffe`; complete original148100
requests then execute the actual14ad50 ushort increment to`ffff`,0,1,2.
The terrain and all produced cell metadata remain allocated between requests.
Ground/flight/float/amphibious queries change lanes and footprint classes0..3.
Four fresh-metadata controls match result, charged work, nearest coordinate,
distance, every final node's XY/g/h/generation/parent/state and every fractional
route word. All1656 final nodes also match production C; work is706/180/180/180.
The original closed-list links are normalized to new/open/closed for comparison
with the engine's flat node representation; unknown node bytes are excluded.

Frozen`retail-fine-stamp-wrap-1.27.json` hashes the literal700-work companion
terrain in`retail-fine-queue-1.27.json`, avoiding another terrain fixture. The
numeric C fixture preserves every expected node and route. Actual
G_BuildUnitMoveLocalRoute runs the four requests twice over retained backing,
second time in reverse class order, and matches all23928 assertions. The engine
clears its sparse lookup on each request and reuses allocated nodes/heap; this
is a deliberately different storage representation with identical observed
results. Required release RoC/TFT suites each pass2488 tests/4375834
assertions;480 Python checks pass. No16-bit counter is added to gameplay state or save files.

Ghidra's typed fine prefix retains`search_stamp`at20; setup and node-lookup
annotations record the original mutation and wrap controls. One existing
oracle gains`--stamp-wrap`; strict entry`oracle-grid-stamp-wrap-engine`also
reasserts all frozen native node/route records. This proves the controlled
boundary sequence, not65,536 historical requests, metadata-maintenance timing,
node/heap growth or spatial repulsion stamps.

See [corpus](retail-pathfinding-corpus.md#fine-stamp-wrap),
[search storage](retail-pathfinding-search.md#complete-static-fine-grid-searches-and-stamp-reuse)
and [backlog](retail-pathfinding-todo.md#fine-03--fine-storage-lifetime).

## Adaptive reuse restores the original ground classifications

Payoff78 completes existing ACC-05.2 with a production fix. Full original162cb0
requests retain adaptive cell metadata and allocated nodes/heap across the actual
164c30 DWORD counter increment`fffffffe`,`ffffffff`,0..5. Four ordinary lanes
(ground/amphibious/float/flight) and both stored sizes1/2 produce six complete
and two disconnected-goal partial routes. All511 final semantic node records,
charged work and complete fractional route words equal fresh-metadata controls
and production C at O0/O2. Setup lane/size/warp words and every node warp tag are also
asserted. The counter is seeded once;3451 actual1625f0 lazy lookups first warm
all searchable cell identities. That supplied history is explicit, not a claim
about billions of historical requests or untouched metadata at a forced zero.

The frozen numeric fixture is`retail-adaptive-stamp-wrap-1.27.json`, with
native lane masks6/80/40/4. The actual engine G_BuildUnitMoveFineRoute compares
all expected coarse nodes/work/routes twice over one64x64 fine map, its padded
cached hierarchy and reused route buffers. Six complete/two partial requests
per pass change lane and stored size through the production invalidation path.
Owner work windows are supplied independently; scheduler cadence remains its
own contract. The two passes match8508 assertions.

The new checks exposed a separate restoration error in g_world.c. Initial
hierarchy ground classification used mask6, but temporary source/target
exclusion restored cells using fine-ground mask2. No-fly-only terrain therefore
became spuriously clear in the retained ground hierarchy after a request.
Original15d360/15cf80 controls on three map dimensions produce flag byte
`41 ->0 ->41`, preserving terrain, metadata and all parents. The engine
regression captures every lane/level before a real owned request and reproduces
eight changed source/target/parent classifications. Restoration now uses6,
matching initial construction. All8828 assertions pass; every cached class is
restored before subsequent routing. Fine-ground admission retains mask2.
Required release RoC/TFT suites each pass2490 tests/4393170 assertions;481
Python checks and six fresh corpus contracts pass.

Ghidra now retains623 roles,61 partial layouts/419 verified fields and292
explicit x86 ABIs. New adaptive search/cell/map/node prefixes distinguish the
DWORD cell stamp from the ushort node index and type the actual setup/reset/
creation operands. Unknown bytes remain undefined. One variant of the existing
adaptive oracle and strengthened existing map contracts preserve these checks;
no task IDs are added or split. Capacity, special-edge producers and complete
public scheduler/callback composition remain separate.

See [corpus](retail-pathfinding-corpus.md#adaptive-stamp-and-class-restoration),
[search evidence](retail-pathfinding-search.md#complete-adaptive-request-oracle)
and [backlog](retail-pathfinding-todo.md#acc-05--adaptive-storage-lifetime).

## Terrain-produced adaptive passages preserve the retail veto

Payoff79 closes existing **MAP-03.3, ACC-02.1 and ACC-03.2** together; no new
IDs or splits. The [producer/update inventory](retail-pathfinding-search.md#pathing-producer-and-update-inventory)
now maps spawn, movement, size/profile/pathing changes, construction,
removal/depletion, terrain writes, exclusions, gates and save/load to their
retail grids/timing, engine owners and remaining existing IDs. Inventory
completion does not imply all listed lifetime differences have been ported.

`verify_wc3_pathing_adaptive.py --terrain-producer` starts with an empty fine
map and applies **288 original04d870→054000 terrain setters**. These publish
72 fine cells with all four blocked movement flags. Full original15d360 then
builds the padded41/20/10/5 hierarchy, including2,206 exact class bytes. The
actual full162cb0 request still hits the reduced size2 east-boundary veto:
**38 charged pops,56 nodes, six-point partial route** in all four native lanes.
Ordinary occupancy at(18,13) returns1 while the east-side boundary predicate
returns0. The full original/C node and route states match. Thus supplied
unrelated-lane constants in the earlier synthetic fixture did not cause the
selected-lane failure; it survives actual terrain/classification producers.

The same oracle enumerates16^4 possible ordinary fine occupancy patterns,
retains one witness for each of54 attainable four-lane tuples, and executes
original rectangle/base/parent producers on all54. Ground6 contains flight4,
so27 of81 class0/1/2 tuples are impossible. Class3 is not emitted by full
ordinary rebuilding. Special markers/object eligibility and counterfactual
mixed-parent/forced-predicate controls are classified separately; see
[reachability inventory](retail-pathfinding-search.md#ordinary-classification-reachability).
The frozen producer fixture is `retail-adaptive-terrain-producer-1.27.json`;
`retail_adaptive_producer.h` stores literal original expectations.

The actual engine regression checks all54 class witnesses and every lane of
all2,206 hierarchy cells, then executes `G_BuildUnitMoveFineRoute` in all four
lanes twice over retained map/node/heap/route backing. **12,785 assertions**
compare every final56-node record, work, size and all six partial-route points,
and preserve the issued goal independently of that partial centre. This extends
the real routing regression surface without changing the retail veto or
inventing an algorithm correction. The existing implementation already agrees.
Payoff80 below resolves the ordinary public mover retry/failure journey;
map headers/backing and original empty fine occupancy are supplied explicitly.

Ghidra retains the class inclusion/rejection proof, producer-built veto and
producer ownership links in its existing623 named functions. Unknown bytes,
special records and complete gameplay branches remain untyped/unclaimed.
The new strict corpus entry remains `known-reference-difference`, expected
exit1, because the conventional reference graph can reach the goal.

The preceding upstream merge reaches13bcc8b7 and retains all native movement
state beside27 pooled sparse lifecycle records. Save90 writes pools before
shared/group movement and clears transient edict fields before rebuilding route
buffers. Combined pathfinding/save checks and full RoC/TFT runs pass2491 tests/
4393447 assertions per edition; normal production builds. The additional
producer fixture changes no network fields, saved state or production ABI.

Required final `make BUILD=release -j4 test` passes **RoC/TFT2,492 tests and
4,406,232 assertions each**, plus **482 Python checks**. Fresh
`runtime/terrain-producer79-strict-pinned/corpus-results.json` passes all three
selected contracts with149 source pins, preserving both known-difference
entries and the prior adaptive wrap control. The producer comparison also
passes at O0. The first repository run caught a stale expected oracle count;
updating the inventory regression to101/four known differences and rerunning
resolved it. No native mismatch was converted into a pass.

## Producer-built size2 passage reaches full retail failure

Payoff80 closes existing **ACC-03.3**. The reduced east-boundary veto is now
executed by an actual Footman clone through file-backed map loading, public
`CreateUnit`/`IssuePointOrder`, ordinary ownership, all route consumers and final
task cleanup. There is no supplied mover/path state or patched predicate.
`make_wc3_pathfinding_map.py --scenario adaptive_passage` preserves the original
campaign container and tileset tables, authors a flat17×17-vertex W3E at origin0,
a64×64 WPM with the reduced18 coarse blocks expanded to72 fine cells, empty
placement members, and an authored collision40 Footman clone. Retail's loader
publishes0xd7 for these WPM0xc6 cells and0 elsewhere; the traversal projection
and padded41/20/10/5 hierarchy agree with the prior producer-built witness.

The public source is(272,304), goal(1744,1776), initial facing90 degrees and
requested speed100 (the ordinary minimum makes the effective speed150).
The native group5000-work search reproduces **38 pops,56 nodes and the same
six-point partial plan**. Its intermediate selected fine destinations are
(26.5,26.5), then(34.5,50.5), then the preserved click(54.5,55.5).
Group progression, the member400-work accelerator and the member700-work fine
search are independent state machines. A coarse partial result is not itself
the final movement outcome, and consuming its index0 does not replace the click
with that partial endpoint.

| Search | Kind | Budget | Pops | Result | Route points |
| --- | --- | ---: | ---: | --- | ---: |
|1 | Group adaptive |5000 |38 | Partial |6 |
|2 | Member adaptive |400 |3 | Complete |3 |
|3 | Fine |700 |52 | Complete |19 |
|4 | Member adaptive |400 |4 | Complete |4 |
|5 | Fine |700 |225 | Complete |24 |
|6 | Fine |700 |6 | Complete |6 |
|7 | Member adaptive |400 |32 | Partial source |1 |
|8 | Fine |700 |701 | Partial |10 |
|9 | Fine retry |700 |701 | Partial source |1 |

At owner1492, the reached partial fine endpoint initializes retry2 and consumes
it to1, resets only the fine buffer and stops for that visit. Owner1493 refills,
returns terminal4 with retry1 retained and enables forced arrival. Owner1494
accepts the heading and retires the task/order. The terminal world position is
**(1220.7265625,1838.97265625)**, short of the click, with zero velocity. The
adjusted retry goal is the failed member accelerator's retained source,
(34.42889404296875,50.11320877075195) fine; the click remains independent.
No owner random draw is consumed in these two retries.

Two complete Frida captures repeat all nine searches/routes,434 speed/heading
and heading-error decisions,437 position/velocity/facing commits,6000 primary
advances,1000 owner callbacks,300 timer samples and final cleanup.
`retail-adaptive-passage-1.27.json` freezes this contract;
`verify_wc3_adaptive_passage_trace.py` rejects missing completion, changed
provenance, truncated routes, moved terminal position or surviving order heads.
Its literal C header is checked byte-for-byte against the frozen motion.

The actual engine test
`wc3_movement.public_size2_passage_matches_original_fallback_and_failure`
executes JASS admission and normal `RunFrame`, then six Save90 continuations at
1200/6000/11200/14055/14085/14115ms. **12,420 assertions** compare all437
uninterrupted commits and800 saved suffix commits, in both RoC and TFT. The
existing general Move implementation already matches; this adds a complete
engine regression rather than a scene-conditioned algorithm change. No save,
network or game-module ABI changes are required. Four release diagnostics that
used variables only inside the optional AI trace and one misleading test-loop
indentation were also corrected without changing behavior.

Reproduce the bounded original capture with the existing original campaign map:

```sh
python3 tools/frida/make_wc3_pathfinding_map.py \
  --base "$RUNTIME/Human02Interlude-original.w3m" --scenario adaptive_passage \
  --output "$WC3DATA/Maps/PathingRE-AdaptivePassage.w3m"
"$FRIDAPY" tools/frida/trace_wc3_pathfinding.py --data "$WC3DATA" \
  --map 'Maps\PathingRE-AdaptivePassage.w3m' --seconds 150 --samples 10000 \
  --motion-events --velocity-events --heading-events --clock-events \
  --task-events --profile-events --map-load-events --blockers \
  --watch-cell 18 26 --continue-at 80 --continue-after-start 2 \
  --x11-display :94 --output "$RUNTIME/adaptive-passage.jsonl"
```

The owned Wine loading flow needed an initial key before map initialization and
a second key after the observed script start. `--continue-after-start` provides
that second external key without guessing a second absolute deadline. Three
exploratory captures with incomplete246-sample or unsuccessful startup
outcomes are not accepted evidence. Only the two complete frozen captures are registered.
Presentation subdivisions vary; each closes its own observed counters while
primary clocks and movement repeat exactly. Limited spatial-clean/replan trace
samples do not certify their full chronology.

Ghidra's existing623 roles now retain the group/member/fine distinction and
these final consumers/counters. Other task families, dynamic overlapping object
chronology, portals and terrain-update timing remain their existing tasks. See
[search evidence](retail-pathfinding-search.md#ordinary-classification-reachability),
[corpus contract](retail-pathfinding-corpus.md#producer-built-passage-completion)
and the [backlog](retail-pathfinding-todo.md).

Required `make BUILD=release -j4 test` passes RoC/TFT **2,493 tests and
4,418,652 assertions each**, plus **491 Python checks**. Fresh
`runtime/passage80-strict/corpus-results.json` verifies the complete repeated
live contract and preserves the producer oracle's expected known-reference
difference. No incomplete capture is accepted.


## Overlapping targets retain fine-cell insertion history

Payoff81 implements existing FINE-01.6 in the game map owner and Move.
`G_PublishMoveSpatialObject` publishes committed fine poses, authored footprint
changes and public axis writes. Each active footprint retains a publication
rank per cell: the old/new intersection keeps its prior ranks, removed cells
lose membership, and newly entered cells prepend. This models the effective
kind0/1 chain produced by original `14e770` and resolved by `14e050`; sorting
entities by birth or by their latest movement cannot preserve intersection
history. `wc3_pathing_spatial.h` is the shared production mechanism.

Fine queries recognize the target only if its active link precedes every
eligible foreign blocker in that cell. Identity remains visible before a later
foreign rejection, while an earlier foreign rejection hides it. Terrain still
rejects before inspecting links. Temporary moving/cohort flags determine
eligibility without changing insertion order. Step-blocker collection also
uses this cell order and preserves its32-token cap and footprint traversal.
No scene coordinates or rawcodes enter these production algorithms.

`wc3_target_overlap_probe.j` authors three public collision40 Footman clones on
an empty64x64 file-backed map. It pauses the target and blocker, overlaps them
with `SetUnitX`, issues Smart, moves the target away and back, reissues Smart,
removes the blocker and issues Smart again. The watched cell confirms the
actual link order and lazy removal records. First, blocker precedes target:
701 fine pops exhaust the700 budget, producing a24-point partial route.
Reinsertion puts target first:39 pops terminate at node122 with127 nodes,
producing21 points despite one foreign blockage. After removal the same
termination uses128 nodes and no foreign blockage. Virtual category2 captain
links remain explicitly observed rather than mistaken for blocking units.

Two complete captures repeat **501 position/velocity/facing commits**,442
heading decisions, three full fine searches,300 timer samples and1,000 owner
callbacks. `verify_wc3_target_overlap_trace.py` validates both literal
trajectories, actual chains, task lifecycle, loaded terrain/hierarchy, scalar
arithmetic and primary clock; process pointers and host subdivisions are
normalized without dropping blocker masks, positions or hit counts.
Frozen literal data lives in `retail-target-overlap-1.27.json` and
`retail_target_overlap.h`. Runtime inputs are
`runtime/target-overlap81-explore.jsonl` and
`runtime/target-overlap81-repeat.jsonl`; both completed captures are accepted.

The engine regression
`wc3_movement.public_overlap_matches_original_link_order_and_saved_continuations`
drives the same public orders through actual5ms game frames. Both RoC and TFT
match all501 commits and **1,999 suffix commits across eight Save91
continuations**, with25,045 assertions per edition. The separate fine-route
regression covers both target/blocker orders. Ten malformed spatial-state
records are rejected; the valid serializer round-trip retains the counter and
cell ranks. These are historical Save91 results; [Payoff128](#spatial-load-rebuilds-membership-in-save-order)
replaces that load-order assumption and its affected suffix assertions. Map replacement, direct/deferred removal and edict birth clear
ownership; ordinary pose prediction does not reorder committed occupancy.

The composed spatial oracle compares **81,920 engine active-cell orders**
against the complete original rectangle writer across **1,280 updates**, with
unchanged, overlapping, disjoint, empty and clipped rectangles. It retains the
original dirty/full cleanup and separation checks. Existing fine-target chain
controls remain in `engine-fine-targets`. Fresh selected reports are in
`runtime/overlap81-strict/`.

Active ranks preserve ordinary query behavior; they are not native link-vector
indexes, retirement references or the shared DWORD query stamp. Those storage
and lifetime requirements remain MAP-05/06. Other category producers remain
FOOT-03, and stale adaptive terrain timing remains E2E-06.2. The saved Ghidra
map now has624 function roles, including verified fastcall
`SubtractRectangleIntersection` at `6f1d4ae0` and the actual strip-consumption
order used by the rectangle writer. See [spatial producer evidence](retail-pathfinding-separation.md#engine-active-cell-history)
and [strict corpus](retail-pathfinding-corpus.md#overlapping-target-producer-and-engine-history).

Validation: full release RoC/TFT each pass **2,496 tests /4,443,774
assertions**, plus **498 Python checks**. `runtime/overlap81-strict/` verifies
all three selected producer/target/live contracts. O0 and O2 both preserve
the81920 native active orders and repeated public arithmetic/clock words.

## Compiled expressions retain retail arithmetic and evaluation order

Payoff82 implements existing NUM-01.21 in the script producer that feeds Move.
The original engine parser recursively consumed another operator at the same
precedence on its right: `100-10-5` became `100-(10-5)` and returned95. A failing
public JASS regression reproduces that result. Both additive and multiplicative
parsers now accumulate a left expression, reading the right operand at the next
lower precedence. Parentheses and unary operators retain their own nesting;
operand procedures execute in their original left-to-right order. This applies
to ordinary scripts and does not depend on a scene, destination or unit type.

Ghidra identifies `JassVM_ExecuteBytecode` at `91b460`: each instruction is eight
bytes, with right/left/destination register numbers in bytes0/1/2, opcode in
byte3, and an immediate word at4. The execution prefix has the next instruction
pointer at20, limit at44, and register0 type/saved-type/value at68/6c/70; register
stride is28. Opcode17 promotes integers through `070d80`. Arithmetic opcodes
20/21/22/23 perform wrapping integer ADD/SUB/IMUL and signed IDIV, or call
`06fbb0`/`06fa90`/`06f9c0`/`06fcd0` with separate scalar locals. Assembly call
sites are `91bfa8`, `91c009`, `91c060` and `91c0e5`. Division by zero exits the
interpreter with status7. The integer overflow fault's surrounding VM lifetime
is not established; the engine reports it instead of executing undefined C
arithmetic. Full opcode/compiler grammar remains NUM-01.2/15.

JASS source arithmetic now uses the already verified software scalar helpers,
with truncating integer promotion and explicitly wrapping integer steps.
Galaxy source retains its existing host arithmetic. The parser attaches the
choice through internal operator names, so parsing Galaxy later cannot change
already compiled JASS functions. A mixed-language VM regression verifies that
`16777217-1.0-2.0` remains original JASS word4b800000 while Galaxy produces
4b7ffffd. Script comparisons are not the numeric oracle: the public integration
test saves raw intermediate and native-result words in the game's hashtable
and compares them from C against the frozen retail inputs.

Two complete read-only native captures authenticate27 compiled expressions
covering chained subtraction/division, mixed additive operators, precedence,
explicit parentheses, unary minus, typed integer/real intermediates, overflow,
fractional scalar cancellation/product and integer-to-real promotion. Ten
operand-side-effect markers establish actual evaluation order. Fractional
`0.3-0.2-0.1` is b2800000, and `0.1*0.2*0.3` is3bc49ba8. These are compiled
source expressions reaching public R2I, not calls to an isolated scalar model.
The function's static bytecode dispatch plus public operand observations prove
this arithmetic scope; no complete compiler or opcode inventory is claimed.

The real flat64x64 map uses the authored collision40 Footman clone. Its timer
receives expression `0.4/2.0/2.0`, exact word3dccccce; the literal `0.1` would
produce3dcccccc. At callback10, unparenthesized `tick-1-3*3==0` issues Move to
`1744.0-96.0-32.0,1776.0-128.0+64.0`: actual task destination1616/1712. The
native trace retains all three TimerStart calls, including the two ancillary
base-map timers. Move completes naturally, with432 exact commits of clock,
fine XY, velocity XY and facing. Both engine editions reproduce the whole
journey and1,825 continuation commits saved at995/1000/1005/3000/10000/13000/
14000/16000ms, including the expression callback before and after admission.
Save92/JSVM8 reject old token identities and execution semantics.

Frozen contract is `retail-chained-expressions-1.27.json`; C literals are
`retail_expressions.h`, checked by `verify_wc3_expression_trace.py`. Accepted
captures are `runtime/expression82-final-{first,repeat}.jsonl`, strict corpus
entry `live-chained-expression-captures-261003`. The preliminary19-expression
pair remains exploratory and is not substituted for the final27 cases. Native
source/hash/map provenance is pinned for each final run. Presentation-clock
subdivision and bounded spatial samples can differ with observation overhead:
the verifier retains each run's full recorded/count extents, then compares the
complete6000 primary advances,1000 owner callbacks, movement phases, searches,
operand order and public timeline against their common exact contract.
Optimized and unoptimized C both verify all recorded movement arithmetic.
Saved Ghidra annotations contain625 roles,63 partial layouts/429 fields,
293 explicit ABIs and59 globals.

Payoff82 validation: required release `make test` passes2,499 tests/4,466,637
assertions in each edition,506 Python pathfinding checks and106 Galaxy tests.
The raw expression test passes245 assertions; the whole movement/save test
passes22,617 assertions per edition. Three fresh strict corpus contracts pass,
including the200,330-case scalar engine comparison and the preceding overlap
journey. No new TODO IDs are added:164 done/172 remaining.

Bounded reproduction uses the normal engine test executable:

```sh
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test wc3_api.pathfinding_compiled_expressions_match_original_inputs_and_results \
  +com_frame_limit 100
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test wc3_movement.public_chained_expression_move_matches_original_and_saved_continuations \
  +com_frame_limit 100
```

Add `-tft` for the second edition. The native map builder's
`--scenario chained_expression` selects `wc3_expression_probe.j`; its
`wc3_expression_inputs.json` preserves every authored case. The strict corpus
runner can select `--only live-chained-expression-captures-261003` with the
external archive; a fresh output directory is required. See [corpus workflow](retail-pathfinding-corpus.md#compiled-chained-expressions-and-public-movement).

## Fine terrain edits retain regional hierarchy publication

Payoff83 implements the ordinary producer/update contract from MAP-03.3.
`SetTerrainPathable` changes fine terrain immediately and invalidates legacy
flow fields. It does not rebuild adaptive classifications or discard a Move's
retained group/member paths. A local obstruction causes the existing fine
retry mechanism to replan; a later footprint producer publishes its own region
from the current fine masks. Pending edits outside that region remain pending.
This temporal state is part of the simulation, not a disposable cache.

Two complete original public captures use an empty64×64 map, a collision40
Footman, three point Moves and two separate4×4 terrain patches. Both patches
are edited during the first active Move. At tick100 a tree is created/removed
over the first patch; tick200 repeats over the remote patch. Seven snapshots
retain every fine byte and all four traversal lanes at each observed hierarchy
level. Both repetitions match724 motion commits,721 steering decisions,
12 complete searches,6000 primary advances and1000 owner callbacks.

The first Move retains its9-node adaptive route through the terrain writes;
when the obstruction reaches its local leg, it executes a94-pop/156-node fine
retry. The first footprint publication changes the subsequent group/member
adaptive searches to10 nodes and a different intermediate destination. The
remote patch's classes remain zero until its own footprint publication. All
three public orders reach natural completion. A global rebuild on each terrain
write or each widget bake would erase this distinction.

`g_world.c` now initializes the hierarchy when the fine map is created, performs the original full post-main publication through `G_FinishMovePathingInitialization`, retains
four independently published lanes, and rebuilds only old/new static footprint
rectangles. Producer snapshots retain entity incarnation, texture identity and
contents, orientation, dimensions, surface policy and bounds. Comparing baked
bytes alone is insufficient: a footprint must publish even when terrain already
blocks its pixels. Ordinary unit pose updates publish fine occupancy separately
and do not rebuild static adaptive lanes. Legacy field generations still follow
the fine/static bake. Retained route revisions identify map lifetime rather than
that field generation.

Original15d360 clips the fine rectangle once. Base/parents independently visit
`floor(min/scale)..floor(max/scale)` inclusive; they do not recursively round
previous-level bounds. Widget world rectangles first obtain the additional
upper edge through05ee40. Missing parent children classify blocked. The engine
shares this reducer with request exclusions, including ground6 restoration.
Original construction tests now explicitly invoke their observed full refresh
after writes/reversal instead of attributing that refresh to the terrain native.

Save93 stores exact hierarchy dimensions and all published class bytes after
terrain state. Load validates shape, extent and classes before copying any
classes, restores static fine masks and producer snapshots, and rebinds saved
routes to the new map lifetime. It preserves pending terrain publication.
Malformed shape, class3/255 and truncated records are rejected. JSVM remains8;
formats39..92 are rejected by the current exact-version guard.

The actual engine JASS/RunFrame regression compares all724 commits and4164
suffix commits from eight saved states:1995/2000/2005/3000/9995/10000/19995/20000ms.
It also checks every observed fine/adaptive value at the public lifecycle
markers, including saves before, during and after each publication. The fixture's
minimal common.j omits the Walkability constant; the regression therefore passes
`ConvertPathingType(1)`, the same original enum value. Its script, movements and
geometry are independent inputs; no production branch recognizes this scene.

Saved Ghidra state has628 function roles,65 partial layouts/437 fields,
295 explicit ABIs and60 globals. The integer/scalar rectangle types and
15ba80 stdcall operand storage retain the upper-edge rules. O0/O2 strict native
checks pass. Reproduction inputs and exclusions are in
[the terrain publication corpus](retail-pathfinding-corpus.md#regional-terrain-publication-and-retained-movement).

```sh
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test wc3_movement.public_terrain_edits_match_original_regional_publication_and_saved_continuations \
  +com_frame_limit 100
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test wc3_save.adaptive_publication_rejects_invalid_shape_class_and_truncation \
  +com_frame_limit 100
```

Add `-tft` for the second edition. The native builder's
`--scenario terrain_cache` selects `wc3_terrain_cache_probe.j`.
Required full suites pass2501 tests/4,944,161 assertions per edition and514
Python checks. Seven fresh strict corpus contracts pass; both O0/O2 reports
check the complete repeated native captures. Payoff83 closes E2E-06.2 and
E2E-07.1/02 without adding tasks:167 done/169 remaining.

## Pathing update state and ownership contract

E2E-06.2 freezes the implemented ordinary contracts below. This is a bounded
handoff contract backed by the linked original evidence; READY gates still
require the remaining categorical, special, allocation, portal and reentrant
branches. Those extensions retain their existing IDs rather than inferred
behavior or a fallback that claims retail parity.

| State/operation | Transition, result and owner | Evidence and remaining limits |
| --- | --- | --- |
| Fine-map construction | Setup replaces terrain/storage, initializes all four padded adaptive maps and advances map lifetime. Generated main/bulk objects are followed by a full publication; later edits have no such global refresh. Padding starts zero; full update includes the clipped upper edge | [File-backed construction](#file-backed-maps-retain-native-hierarchy-allocation), MAP-01/02; ordered active unload/reload retires old movement owners before world replacement (Payoff129) |
| Terrain write | Accepted in-bounds edit changes requested fine flags synchronously; outside is rejected. Fine occupancy history and unrelated flags survive. Legacy fields become stale; retained adaptive publication and routes survive | [Public terrain natives](#terrain-pathing-natives-reach-the-engine), Payoff83 above; complete terrain producer inventory MAP-03.3 |
| Static footprint change | New fine masks are visible before the next query. Changed old/new footprint rectangles publish base and three parents, preserving other regions; independent overlap/death texture survives | [Widget lifecycle](#blocker-removal-owns-static-route-invalidation), [authored footprints](#authored-widget-creation-preserves-snapped-pose-and-rotation), Payoff83; wider eligibility table remains FOOT-03 |
| Ordinary moving pose | Commit elapsed old velocity/fine pose and clock, then publish linked occupancy history. Predicted presentation samples do not publish occupancy or alter hierarchy | [Fine insertion history](#overlapping-targets-retain-fine-cell-insertion-history), NUM-02.3/FINE-01.6; allocator/stamp storage remains MAP-05/06 |
| Path request | Group adaptive budget5000, member adaptive400, fine700; source/target exclusion is scoped to admission and restores ground6/current terrain. Returned length high bit records a nearest partial route; nonempty partial is usable | [Reduced passage](#producer-built-size2-passage-reaches-full-retail-failure), [reuse](#adaptive-reuse-restores-the-original-ground-classifications); capacity/error recovery FINE-03/ACC-05.1, nested/reentrant scopes MAP-04 |
| Deferred fine admission | Owner work uses strict `>1100` veto, independent per-player FIFO/countdown and ten-owner-visit interval; below64 accumulated work admits the fast retry. Denial retains request state and resumes through owner dispatch | [Twelve-member ownership](#twelve-member-public-group-retains-fine-admission-and-committed-occupancy); other scheduler buckets/eligibility SCHED-01/02/04 |
| Retained path progression | Group and member coarse chains have distinct storage/indices; local fine exhaustion/refill retains the coarse owner. .49-cell progression/arrival uses original scalar words. Terrain writes do not force wholesale replacement | [Group point lifecycle](#public-oblique-move-retains-the-singleton-group-destination), [coarse progress](#retained-coarse-progress-refills-the-fine-route), Payoff83; portals and dynamic yielding remain ROUTE/GATE |
| Owner update | Primary5ms scalar source dispatches periodic path owner, shared speed publication/reset, shared radius accumulation, all-member decisions then commits. Cohort creation sequence prevents same-pass reuse | [Clock ownership](#primary-clock-reaches-move-and-predicted-positions), [group phases](#public-pair-movement-uses-a-shared-move-owner); wider callback mutation SCHED/ORDER |
| Completion/replacement/Stop | Previous velocity commits before zero/final completion; public order owns its task chain. Empty bound groups survive their physical owner visit; the following shared prepass collects zero references. Replacement frees owned route buffers | [Final binding lifecycle](#final-captain-binding-stop-and-repeated-ai-initialization), ORDER/GROUP; broader destruction/reentrant branches retain their named tasks |
| Save/load | Persist semantic state and generations, restore typed references after entities/pools, retain separately published hierarchy and fine history, rebuild only process-owned backing, then rebind routes | [Save contract](save-load.md), Payoff83; cross-feature original retail save/load and active reload remain MAP-06.2/E2E-04.2 |

Numerical/PRNG structures are separately assigned E2E-06.1. Unsupported
movement masks log an error and fail admission; allocator failures are fatal
with a diagnostic. Corrupt current-format state is rejected, with no older
layout migration or silent route replacement. The engine's bounded work/storage
limits are declared in `wc3_pathing_limits.h`; they are not proof of original
allocation boundaries. Each linked fixture states its original result/status
and producer extent, rather than treating an empty route as universal failure.

## OpenRealm replacement interfaces and lifecycle boundaries

E2E-07.1/02 map the working replacement to the Q2 game-module boundary.
Gameplay inputs, order policy, path ownership, group scheduling and numeric
motion remain inside `games/warcraft-3/game/`. The universal client consumes
authored snapshots; these interfaces add no client or network gameplay policy.

| Owner/file | Replacement interface and caller |
| --- | --- |
| `skills/s_move.c` | `CAbilityMove`, `S_IssueMoveOrder`, `S_IssueFollowOrder` and group admission own validation, task dispatch, steering, retry, interruption and cleanup. `move_route_query` supplies typed world/fine geometry and mover/target to routing |
| `g_world.c` | `G_UnitMoveGroupDestination` owns the group adaptive plan; `G_BuildUnitMoveFineRoute`/`G_BuildUnitMoveLocalRoute` and `G_AdvanceUnitMoveFineRoute` consume member-owned buffers. Line/step blocker queries use committed active-cell history. `G_ClosestMovePathPoint` handles placement |
| `common/wc3_pathing_*.h` | Numeric/grid transforms, fine/adaptive graph kernels, route selection, segment predicates, spatial history and widget transforms consume supplied data. They do not own edicts, orders, public scripts or game clocks |
| `g_main.c` / `g_monster.c` | `G_RunFrame` owns the primary scalar source and timer/event order; `M_RunScheduledThinks` dispatches Move through shared lifecycle machinery. Ability-specific rules remain in Move |
| `g_spawn.c`, `g_destructable.c`, `g_utils.c` | Spawn/footprint reapply/death/restore/removal call the game `CM_BakeStaticObstacles`; it updates fine masks/fields and publishes changed regional static producers. Ordinary movement calls `G_PublishMoveSpatialObject` through pose commit |
| `g_world.c` / included `server/sv_routing.c` | Shared routing owns fine/static field storage and field jobs. `PATHMAP_SETUP_COMPLETE` is an include-local initialization hook; the WC3 game owns its hierarchy lifetime/publication. `G_FinishMovePathingInitialization` publishes the full map once after generated main/bulk setup; ordinary terrain callbacks retain regional timing. No WC3 policy is added to server exports |
| `g_save.c` | `WriteMoveRouteBuffers`, `WriteMoveSpatial`, typed pool/group/shared records and `WriteMoveAdaptive` serialize semantic state. `ReadMoveAdaptive` validates current dimensions/classes; `G_RebuildSavedMovePathing` rebuilds fine masks/producer snapshots without publishing pending edits |

`moveFineRoute_t` owns its three allocations: fine, member adaptive and group
adaptive points. A query borrows mover/target/geometry only for the call; it
cannot retain stack geometry or claim ownership of an edict. `S_FreeMoveRoute`
and `move_free_group_routes` release corresponding allocations; reset/load
release old process-owned buffers before reading new records. Saved counts,
indices, masks and points are validated before the restored owner can run.

`moveGroup_t` belongs to Move, independently of a JASS group collection handle.
Member unit plus incarnation guards prevent attachment to a reused edict.
Creation sequence, request ID, shared generation ID and target incarnation
retain their distinct meanings. `moveShared_t` uses a generation ID rather than
a pointer into its growing array; physical groups retain references, and only
the shared owner prepass collects the final unreferenced generation. Fine-work
queues retain typed unit references, FIFO links, countdown and charged work.

The fine map owns terrain and baked/current masks. Static producer snapshots
borrow texture identity and hash current pixels; they do not free textures or
persist resource pointers. Map replacement resets snapshots and hierarchy
backing and advances the runtime lifetime. Ordinary edits keep that lifetime.
Hierarchy search node-index scratch is disposable, but published classification
bytes are authoritative across saves because they may intentionally lag terrain.
A saved continuation therefore rebuilds masks and resource bindings while
restoring these classification bytes verbatim. This distinction implements the
ownership design rather than treating every derived allocation as reloadable.

`G_GetMoveAdaptiveStateSize`, `G_GetMoveAdaptiveMapSize` and typed byte-state
get/set functions are private game/save interfaces. They neither export native
retail object addresses nor enlarge engine/network entity contracts. Current
Save93 rejects incompatible streams; load does not synthesize missing state.
Remaining original full teardown/allocator, cross-feature save and nested-query
lifetimes retain MAP-04/05/06 and E2E-04 IDs. Their unresolved behavior is not
filled by a compatibility bridge or scene-dependent choice.

## Timed speed changes, stationary turns and boundary restarts

Payoff84 completes existing MOVE-02.1/02.2 with one repeated public30-second
journey on the empty file-backed64×64 map. Seven ordinary point Moves combine
long oblique travel, timed speed150→250→400 changes, two moving retargets,
turns spanning the stationary movement window, two Stop/axis-boundary resets
and restarts, and the final natural arrival. Two native captures agree on all
763 commits,762 steering decisions,21 searches,300 timer callbacks, primary
clock/owner phases,24 event-boundary states and23 complete watched-cell chains.

A public speed request0 is **not** zero velocity for this profile: the getter
and cap publish the authored/default minimum150. Stationary phases are the
separate turn-window decision: several successive commits retain exact XY and
zero velocity while facing changes. Lowering a cap first integrates elapsed
old velocity, then clamps its vector without changing facing. A public retarget
first stops/integrates the old mover, then admits a new physical group; it must
not continue the old velocity through the new turn. Stop publishes zero and
clears the current order while retaining ordinary fine occupancy.

The two axis resets place the center at exact fine coordinates20,19 (world
640,608), retaining facing and a stationary active3×3 fine record. Both restarts
use that boundary position rather than rounding it to a cell center. The first
owner decisions, all later positions/facing and final zero commit match the
engine's existing general movement implementation; no scene branch or alternate
routing rule is required. The regression checks committed state separately from
predicted public GetUnitX/Y samples, and verifies every complete public marker.

The same real JASS/normal5ms RunFrame journey reproduces763 commits plus4877
suffix commits across eleven Save93 states:2495/2500/5495/5500/7540/11000/12000/
15000/18000/21000/22000ms. Saved continuations cover speed publication, point
replacement, stationary turns, Stop and both boundary restarts. The native
motion, state and chain literals are `retail-movement-lifecycle-1.27.json` /
`retail_movement_lifecycle.h`. Original allocation/refcount/stamp storage and
other movement lanes remain separate contracts.

This comparison exposed the engine's default `R2S` mismatch: it used host
six-place `%f`, while original2103b0 requests width0/precision3 from0701d0.
The game native now uses software fractional multiplication, add-half rounding
and carry, plus the original leading-integer/zero reduction for large words.
COMISS's unordered carry makes NaNs take that reduction; exact max-finite and
infinity emit `inf`. Signed zero omits its sign; negative nonzero underflow
retains it. No arithmetic is inferred from the decimal marker text.

Thirty-four original raw formatter controls execute0701d0 with the actual scalar
startup and arithmetic, delegating only its integer `sprintf` import. They
cover adjacent rounding/carry values, both zero signs, subnormal words,
integer/large boundaries, max-finite, infinities and NaNs. The engine calls the
real JASS native on those exact words. All324 public markers now compare
exactly, including predicted coordinates. R2SW width/precision and the complete
formatter/public numeric inventory remain NUM-01.2; these bounded controls do
not close that broader task. Save layout and JSVM version are unchanged.

Ghidra persists631 function roles and298 typed ABIs with the existing65
layouts/437 fields/60 globals. The new registrations, scalar-pointer adapters,
formatter fastcall registers/stack and exceptional comparison are annotated.
`verify_wc3_movement_lifecycle_trace.py` checks provenance, complete producer and
owner lifetimes, literal motion, occupancy, default formatter controls and the
primary clock. O0/O2 reports and the fresh three-contract corpus pass. See
[corpus reproduction](retail-pathfinding-corpus.md#speed-turn-and-boundary-restart-lifecycle).

```sh
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test wc3_movement.public_speed_turn_stop_and_boundary_restart_match_original_and_saved_continuations \
  +com_frame_limit 100
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test wc3_api.pathfinding_default_real_strings_match_original_scalar_formatter \
  +com_frame_limit 100
```

Add `-tft` for the second edition. Native `--scenario movement_lifecycle`
selects `wc3_movement_lifecycle_probe.j`. Both full edition suites pass2503
tests/5,007,391 assertions each;522 Python checks and the release production
build pass. The external `lifecycle84-validation.json` pins the commit, logs
and fresh strict/O0/O2 reports.


## Region callbacks observe committed movement and retain forced changes

Payoff85 uses the real `wc3_region_callbacks_probe.j`, an empty64×64 fine map
and a file-shaped17×17 flat W3E. Two complete original30-second lifetimes repeat
all629 movement commits,628 decisions,1000 owners,318 public markers,18 event
states and17 complete watched-cell chains. The ordinary point Move enters at
callback tick45 and leaves at54; the second entry at135 teleports, issues a new
Move and then leaves; entry234 removes the unit. No later original commit
belongs to the removed mover.

Ghidra's16bc10 prepass samples each member's predicted fine position for16fa00
region transitions before routing/decisions. Actions remain queued: the
same-clock owner commits occupancy first, authored timer callbacks run, then
enter/leave actions observe the published pose. The original05fcf0 registration
and05fa10 public query share the world-to-fine transform and floor. Rectangle
endpoints include their containing cells; they are not continuous world bounds.
Eighteen executed original05fcf0 controls freeze negative/fractional endpoints,
three world origins and both record selectors through the actual scalar/floor
routines. Only the terminal record consumer is observed in those controls;
original region allocation and lazy stamp lifetime are not emulated by them.

The engine's old continuous rectangle and presentation-frame baseline reproduced
86 movement/event-state failures, including a callback teleport before the
corresponding original commit. Move now retains its independent region sample,
publishes transitions from the physical owner and uses fine-cell region
membership. Occupancy and support height are published before actions; explicit
position writers retain the same region baseline and callback-produced new
orders. Save94 serializes the region sample/valid bit. Predicted presentation
poses do not consume region transitions between owner passes. The literal
regression compares all public strings, raw committed words, public world/support
queries, event primary clocks and watched-cell occupancy, then repeats saved
continuations around enter, teleport and removal.

The native map's flat support is level2/height8192, producing world Z0. The test
constructs that actual W3E shape; the generic harness's level0 terrain has a
separate negative height and is not this scene's geometry. Bridge/water support
transitions with UI clamps remain MOVE-03.3. Region geometry mutation, original
allocation/stamps, reentrant filters and other movement lanes retain their
existing backlog scopes. There is no scene-dependent engine branch.

Ghidra saves638 function roles,67 partial layouts/440 fields,306 explicit ABIs
and60 globals. Region bridge/list prefixes, native scalar pointers, registration,
query, cell producer and active-region collector are annotated so future work
can extend the same system.

The complete comparison also finds an idle axis-writer clock gap: after natural
arrival, SetUnitX/Y still commits the original current clock even with zero
velocity. The engine now updates that retained clock without fabricating motion.
A second reproduced gap appears only with10/25/50ms server-frame batches:
callback teleport's leave action was delayed by one primary quantum. The
simulation now drains callback-produced region event chains before advancing,
with queue order retained. Only newly started JASS actions run in that drain;
unrelated already-yielded coroutines, including zero-duration sleeps, are not
resumed again. The same public native journey matches every primary callback
clock across all four frame sizes. Its eight Save94 checkpoints are4495/4540/
13500/13540/13570/23395/23400/23430ms, with3418 saved suffix commits including
three batched-frame continuations. Existing region tests now use independent
fine cells rather than their old continuous-rectangle assumptions.

The focused production regression is:

```sh
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test wc3_movement.region_callbacks_match_original_teleport_remove_and_saved_continuations \
  +com_frame_limit 100
```

Add `-tft` for the second edition. The complete required release suites pass
2505 tests/5,054,904 assertions per edition and530 Python checks. Fresh region,
movement-lifecycle and numerical corpus contracts pass3/3; the independent
O0/O2 comparisons pass. See [corpus reproduction](retail-pathfinding-corpus.md#region-callback-lifecycle).
The external `region85-validation.json` pins the commit and verification logs.


## Pathing queries and scripted pause preserve distinct owners

Payoff86 covers existing MOVE-04.1/04.2 using `wc3_movement_bypasses_probe.j`
on a flat64×64 fine map. A radius40 mover travels toward1712,1712 around a wall
at fine columns24/25, rows0..47. Timed public pathing toggles, pause, fractional
axis displacement while paused and resume precede natural arrival. Two complete
original captures repeat773 motion commits,769 decisions,13 route searches,
1000 owners,310 public markers,10 boundary states and9 complete watched-cell
chains. The raw motion digest is
`0ac103a7407f0df2de2e5140f9947500b53c023f23ad0f33512efc5eed0e7e02`.

`SetUnitPathing(false)` changes the current fine collision query to0, while the
mover's occupied category remains010000ca. It retains the acquired coarse/fine
route. Later local refinement uses query0 and can cross terrain; restoring the
query does not synchronously rebuild every cached route. Normal segment and
step checks own the subsequent blocked-step retry. The engine previously
switched immediately to direct steering and conflated three policies in one
route mask. Move now retains independent coarse `group_mask` and member
`adaptive_mask`; the current fine query is separate. The fine graph accepts
query0 rather than normalizing it to ground2. Generic flow-field defaults keep
their separate existing contract. No coordinate or scenario selects a policy.

Original20f540 `PauseUnit` sets Unit5c bit21 and enters697770/688d30. The latter
increments Unit54 suspension depth and pushes current public head d0005/851973.
The ordinary Move remains beneath it, but its physical group/path is released
and a Stop commits zero velocity. Axis writers while paused update the retained
pose/clock without restarting travel. Original206610 `IsUnitPaused` reads bit21;
it returns false synchronously on unpause while the suspension head remains
current. Original6977c0/678590 decrements suspension depth and schedules69c620
with actual cd53a4 delay0.01. The original point task reactivates two5ms primary
quanta later and constructs routes from the displaced position. Its public
order identity survives; the physical group generation changes.

The Move-owned engine pause transition now releases physical routing, publishes
the suspension head and retains the point order and resume deadline. Normal
`S_RunMoveTimers` reactivates that order after the captured delay; replacement,
Stop and removal clear pending resume state. Other order families, nested
suspensions and reentrant native mutation remain outside this single-point
capture. No speculative public string name is assigned to the suspension ID.
Save95 retains both hierarchy masks, the suspended point order and deadline.

The real JASS/RunFrame regression
`wc3_movement.pathing_pause_and_displacement_match_original_and_saved_continuations`
compares every raw motion word, marker string, public support/primary-clock
state, pause query and ordinary occupancy. Eight snapshots at3995/4000/8495/
8500/10995/11000/11500/13500ms reproduce6171 saved suffix commits; saved3995ms
continuations also use10/25/50ms frame batches. Earlier lifecycle tests now
assert the recovered retained detour and released physical pause route while
still checking wall crossing, saved motion and eventual group arrival.

Ghidra saves644 function roles,67 partial layouts/441 fields,312 explicit ABIs
and60 globals, including suspension depth and all six pause entry/exit roles.
The strict paired checker rejects incomplete producer, pause query, route,
occupancy or owner lifetimes. Fresh O0/O2 comparisons and the three-contract
corpus pass. See [reproduction](retail-pathfinding-corpus.md#pathing-toggle-pause-and-displacement-lifecycle).
Native binaries, map archives, decompilations and raw captures remain external.


Both required edition suites pass2506 tests/5,131,664 assertions each, plus538
Python checks. `bypass86-full-test-final.log` records the final successful run;
the earlier failing log retains the disproved implicit-zero and inactive-cache
expectations. The external `bypass86-validation.json` pins the final commit,
production build and strict/O0/O2 reports.

## Movement modes select routing policy independently of spatial membership

Payoff87 covers MOVE-04.3 with `wc3_movement_modes_probe.j`: a radius40
singleton crosses a wall during ordinary Move, acquires Chaos flight, teleports,
receives another Move, returns to ground through Chaos, teleports again and
finishes another Move. The wall's `0xc6` flags block both ground and flight.
Two independent native captures repeat737 commits,734 decisions,15 searches,
309 public markers, nine boundary states and all ownership phases. Readonly
queries of the mover's own fine-record chains repeat643 observations. An empty
watched wall cell is not evidence that a flight mover lacks spatial membership.

`685db0` maps authored movement types to path selectors; `05c6f0` resolves the
bridge's path and `168c00` writes its top two flag bits after resetting buffers.
Flight has class3, query`04000004` and category`01000000`. Category zero still
has an active class-sized fine rectangle. Ground has class0, query`02000002`
and category`010000ca`. Spatial publication must preserve flight links while
ground collision collectors exclude them. Native allocator/live/refcount words
remain in external captures but are not claimed as engine allocation parity.

Class and adaptive enablement are independent. `165b60` tests path88 bit200000;
when clear, it retains one adjusted-destination waypoint for the fine search.
`16e430` excludes such members from accelerated group-lane selection, and
`16de50` clears the group's adaptive enablement when none qualify. Both flight
orders perform fine-budget700 searches directly, including partial results.
Returning to ground restores group5000/member400/fine700 requests. The first
engine mismatch was commit101: retained pose/time agreed while ground-style
hierarchy selection changed velocity and facing. Flight now uses ordinary fine
routing with the full destination, independently of distance.

Chaos and Move's retained-head reactivation own primary-clock deadlines.
Dispatching only at `monster_think` made saved10/25/50ms server frames postpone
those callbacks and consume the old velocity for extra quanta. The generic
`AB_PRIMARY_TIMER`/`A_PRIMARY_TIMER` procedure hook runs after a due path owner,
before public timer/event actions, on every5ms quantum. Concrete abilities own
their pending-unit filters and state transitions. Other persistent abilities
retain their existing per-frame update contract.

Native Unit position retains support Z separately from fly-height presentation.
The fixture compares this against `s.origin.z - unitinfo.FlyHeight`; the engine
snapshot already includes the authored presentation offset. This capture does
not establish fly-height interpolation or arbitrary morph/order equivalence.
Raw binary/map/decompilation/captures remain external under `runtime/mode87-*`.

The final focused engine check passes80,971 assertions:737 complete original
commits and6534 saved suffix commits through ten checkpoints and5/10/25/50ms
frames. All309 markers, nine fine rectangles/profile states and retained
primary clocks agree. Required whole-suite verification is recorded separately.

Additional type-rebind producer inventory (engine source, not a claim of retail
parity for these families): `s_raven.c` owns Crow Form's ordered rebind;
`s_stone_form.c` owns Stone Form's rebind/inverse; `s_militia.c` owns Call to
Arms and timed return; `s_campaign_abilities.c` owns campaign Avatar;
`s_requested_abilities.c` owns Metamorphosis and its timed `morph_end` inverse.
`g_building.c` additionally rebinds a building during upgrade. All converge on
`G_TransformUnitType`'s old/new-type lifecycle notifications. Their wider active
order/cast behavior belongs to existing ORDER-01.13/E2E tasks. ORDER-06.6's
single relevant transition is the fully captured Chaos witness; no new IDs.

Both required edition suites pass2507 tests/5,212,644 assertions each and546
Python checks (`mode87-full-test-final.log`, exit0). Production build also exits0.
The earlier full log retains the two synthetic adaptive controls disproved by
ordinary flight policy: they now force adaptive queries through an ordinary
caller in each supplied lane, while the new public flight regression exercises
native disabled-adaptive policy. `mode87-validation.json` pins the final commit,
strict fresh corpus and O0/O2 reports. Save96 changes spatial-history meaning;
network messages/layouts are unchanged.

## Temporary speed modifiers publish through their applying owners

Payoff88 composes MOVE-01.2 through actual retail Slow and creep Bloodlust,
negative/positive public removal, a reverse point order, and the caster's
stationary blocking footprint. Two complete captures from the same authored
map repeat594 commits:590 for the travelling Footman clone and four for the
Sorceress. They also repeat588 decisions,13 route searches,317 position/order
markers,317 buff/speed queries and eight removal-filter results. The complete
motion digest is `a3d23b222276c50ec37a83811fdc00e585623c409d1a78d45ba4fc0e160405bf`;
the590 main-mover commits have digest
`4662111a60688c8f9a74cb98cb0176bd2d96088c8490877618e13641aa656f01`.

The original `CAbilityMove_ComputeEffectiveSpeed` reports base270 and multiplier
`3f800000`, then Slow's `3ecccccc` produces the minimum-clamped150. Removing
Slow restores270. Bloodlust's `3fa00000` produces337.5, and removing it restores270.
These are captured raw scalar words, including the subtraction before the lower
clamp. The native public getters observe `Bslo`/`Bblo` level1 while attached.
The buff prefix's separate words70/74 are **not** inferred to be public levels.

The previous engine had empty `UnitRemoveBuffs`/`UnitRemoveBuffsEx` natives,
ignored status slots in `GetUnitAbilityLevel`, discarded applying ability
identity in the older status helpers, and read Bloodlust/Slow speed data from
stock rawcodes even when a different authored alias applied the buff. Status
application now retains the applying rawcode and source incarnation. The
concrete Slow/Bloodlust procedures supply public buff policy; the shared native
filter dispatches that query and runs the existing inverse before clearing the
slot. The owner receives `A_STATUS_REMOVED` after the wipe and notifies Move to
publish the new effective cap. Move owns old-pose integration and numerical
velocity clamping. No spell-specific rules enter the client, monster dispatcher,
or movement routing machinery.

Original48eb10 traverses the attached ability identity list and restarts after
each removal. Positive/negative select their corresponding virtual predicates;
magic and physical selectors are independent requirements. Both tested buffs
are magic, automatically removable, non-aura and non-timed-life. Neither
`UnitRemoveBuffs(false,false)`, the opposite polarity, nor physical-only Ex
removes them. The magic/negative/automatic Ex call removes Slow, and ordinary
positive removal removes Bloodlust. Unknown engine status families are retained
with an explicit diagnostic: the new policy does not invent their classification.
Remaining classification and spell-order families keep ORDER-01.13/E2E scopes.

`retail_speed_modifiers.h` contains the590 main commits and the literal public
states. The owning-engine regression replays the two **captured spell application
callbacks** through `Aslo`/`ACbl` procedures; caster windup, resource timing and
its four steering commits are not claimed as engine parity. Its stationary
occupancy is real: the reverse fine search encounters31 object hits against the
caster, whose native address is normalized only after proving its fine-object
and mover identity. Public Move, teleports, buff removal/query, routes, scalar
timers and all subsequent commits run through normal engine behavior.

All590 main velocity/position/facing/clock commits and public markers match,
with4356 exact suffix commits across nine Save97 checkpoints and5/10/25/50ms
server frames. Save97 keeps applying ability/source semantics and rejects older
formats. Paired arithmetic comparisons pass at O0/O2; fresh strict contracts
include the complete modifier witness and prior movement-mode/numerical controls.
The saved Ghidra mapper retains655 roles,68 partial layouts/450 fields,323 ABIs
and60 globals. It records Unit1dc's ability head, Unit1ec's cached Move and a
38-byte buff prefix ending at rawcode34, preserving all earlier field evidence.

Raw captures, map/source provenance, Ghidra readback and test logs remain under
`runtime/modifier88-*` in the external report root. The final validation record
is `runtime/modifier88-validation.json`. Both required edition suites pass2508
tests/5,276,169 assertions each, plus553 Python checks; production build passes. This closes the temporary modifier
contract; combined effect-stack precedence, other speed/turn producers and the
independent spell-casting scheduler retain their existing tasks.


## Extended captain travel ends in a retained visible Follow owner

Payoff89 extends GROUP-03.4.7.2.2.2's combined group/footprint observation
from30 to120 authored seconds. The original13 births, all-Stop input, terrain,
object modifications and embedded captain AI are unchanged. An external member
manifest proves that only the sampling completion condition changes300→1200;
all17 map members are retained. The new map hash is
`c1cf8f1bc0c1b0118afd38919d4e2ff50cf8dd59dcbd222c0768fb3209f716be`.
Both owned captures complete and reproduce the previously accepted7933 motion
commits/812 footprints exactly, followed by the longer domain's total10986
commits/865 shared footprints and identical normalized decision/commit group
states. Native addresses are normalized only after checking each row's embedded
mover pointer against its resolved physical/virtual actor.

The earlier assumption that this scene must eventually reclaim every task is
incorrect. Birth8's private target request has flag1, and its visible virtual
captain leaves the unseen counter0. Original16c390 therefore skips completion,
even after member10000 marks arrival. The final physical group retains137217,
completion counter0 and a zero-velocity, in-range follower. The other physical
point tasks and the virtual point task finish. This is a natural standing
Follow lifetime, not an unresolved retry or a missing cleanup to synthesize.
Original05a5c0→16dc30 produces the persistent flag;16c390 permits completion only
when that flag is clear or unseen count exceeds32. The backlog's natural
completion requirement is corrected to preserve this verified terminal state.
Target loss, later orders and wider captain policies keep their existing tasks.

The scalar owner timer also disproves an indefinitely fixed six-quantum
cadence. Its first later seven-quantum interval is visible after about35 seconds.
The engine already retains and saves the native scalar deadline; the verifier
now checks that deadline through the longer domain rather than assuming modulo6.
The authored120-second completion has24010 complete primary advances plus the
completion-boundary advance and4000 owner callbacks. Its matching engine
boundary is120050ms. Neither a nominal24000-quanta cutoff nor the old30-second
observer extent certifies this journey.

`retail_captain_go_home_complete.h` freezes the original commits/footprints.
Normal engine frames reproduce all10986 commits, all865 footprints and27703
saved suffix commits at5/10/25/50ms. Eight Save97 checkpoints preserve late
arrivals, shared-owner retirement and the retained standing Follow; final public
heads, target ownership and saved group validation are checked. The owning
production algorithm already reproduces these later states, so no scene-based
routing or forced retirement is added. The paired O0/O2 numerical checks also
verify all14688/14719 raw captured commits; host-duration commits after authored
completion are outside the repeated group/primary domain.

Saved Ghidra annotations retain655 roles,68 layouts/450 fields,324 explicit
ABIs and60 globals, including the typed completion gate and its captured
persistent-owner evidence. Raw maps/captures/source/member proofs and logs remain
under external `runtime/captain89-*`; `captain89-validation.json` pins final
verification. Full engine/corpus validation is recorded there when complete.


## Fine storage growth and capacity preserve search results

Payoff90 separates the original fine search's identity capacity from work and
heap storage. `147600` configures2,048-node growth and1,024 open-slot growth;
slot0 is the heap sentinel. `147af0` refuses a new identity at32,768 nodes with
`FFFFFFFF`, but still resolves an existing stamped cell. Refused neighbors do
not immediately abort the search: the queue drains normally and `148100`
returns its nearest partial route. There is no inferred allocation-failure
result or scene-specific workaround.

`verify_wc3_pathing_storage.py` executes the unchanged original constructor,
containers, memory-block wrapper, metadata append, search and reconstruction.
Only the external Storm401/405/403 imports receive host allocation storage;
reallocation deliberately moves the block and preserves its bytes. The five
complete requests cover:

- A2,048-work request that charges2,049 attempts, creates2,147 nodes and grows
  node capacity from2,048 to4,096.
- A disconnected-goal request that naturally reaches32,768 identities, charges
  63,069 attempts and returns a partial route. New-cell refusal, cached identity
  and outside-cell rejection are checked before any reset.
- Forward/reverse short requests that return success with108/109 nodes and
  43/44 attempts over the retained large allocation.
- Original `14dfc0` compaction followed by another complete short request. The
  metadata pool has32,769 links across the preceding requests: it is distinct
  from the per-request node limit. All links enter the original free list;
  the next108-node request reuses those slots without another allocation.

Both O0/O2 production probes compare every semantic node record, charged work,
result, capacity and fractional route word. Two read-only Frida captures pin
and repeat the actual fine constructor and first node/open growth observations.
That live witness does not claim saturation or whole-trajectory parity.
The numerical fixtures are `retail-fine-storage-1.27.json` and
`retail-fine-storage-live-1.27.json`; all binaries and raw captures stay external.

The engine retains growable node/open backing instead of deriving both limits
from its synchronous work budget. Fine identities use the recovered32,768 cap;
new identities fail normally while existing lookup remains valid. Sparse lookup
is kept below half occupancy. Heap entries can grow independently, including
stale entries. Neighbor expansion preserves values/indices across relocation;
both fine and shared adaptive consumers reacquire node pointers afterwards.
`G_FreeMovePathCache` releases the scratch backing on map replacement/shutdown.
Ordinary700/2,048-work policy and search ordering are unchanged. Adaptive node
capacity policy remains ACC-05.1; sharing host backing does not close it.

Actual `G_BuildUnitMoveLocalRoute` repeats the partial growth request and both
short continuations with original node digests and route words. The core engine
regression additionally consumes all five native requests through saturation,
recovery and release. The focused pathfinding suite passes1,909,333 assertions
in11 tests. Saved Ghidra now retains660 roles,69 layouts/468 fields,331 ABIs and
60 globals, including the dynamic-table prefix, fine growth/capacity fields and
seven verified constructor/container/lookup prototypes. Full-suite and strict
validation are recorded in external `runtime/fine90-validation.json` after the
commit is validated.

## Fine setup and caller outcomes preserve the published source

Payoff91 completes FINE-04.1/04.2 together. `167ce0` admits a path-owned fine
request and returns its selected point even when the underlying search fails.
`165ae0` then consumes progress, target perimeter or retry. Failure is therefore
a route result that Move must retain, rather than an instruction to discard the
whole request. Original unchanged callers cover all four footprint classes:

| Input | Original fine result and caller consumption |
| --- | --- |
| Same integer cell | Exact fractional goal; zero nodes and zero work; caller consumes its one-point buffer |
| Blocked source cell | Fine setup accepts the source unchanged; perimeter edges determine whether it can escape |
| Blocked goal |700-work cap charges701 attempts and retains the nearest partial route |
| Disconnected goal | Queue exhaustion retains the nearest route, independently of the work cap |
| Zero work budget | One charged attempt, two admitted identities, exact fractional source-only route; consumption clears fine and initializes retry |
| Suppressed special target | Perimeter identity ends the search despite suppression; caller restores the target counter and retains the endpoint mismatch flag |

`retail-fine-public-results-1.27.json` freezes24 full167ce0/165ae0 cases plus four
retained-search controls. Same-cell setup returns before stamp, class and
obstruction initialization. After a blocked-goal request it preserves all three
while clearing node/open counts and charging zero work. It does not resample a
footprint. These controls prevent an apparently helpful reset from changing the
actual caller contract. Outside-source setup and full physical recovery remain
separate scopes.

Production `wc3_fine_build_route` now owns that full setup/reconstruction
contract separately from A*. `G_BuildUnitMoveLocalRoute` and its adaptive entry
preserve an already published native owner pose; generic geometry queries keep
their existing source admission. The adapter emits the exact source when no
nearer node exists, retains one-point admitted routes, charges actual work, and
records endpoint mismatch independently of search success. The actual local
adapter compares16 original cases with488 assertions; the full pathfinding
category passes12 tests/1,909,821 assertions. C also compares all24 caller buffers,
indices, work/node counts and mismatch flags at O0/O2, including the distinct
zero-budget and target-object policies.

Two complete read-only140-second Frida captures repeat12 actual Move orders.
All292 complete fine requests match production C at both optimizations. Four
same-cell requests allocate zero nodes and charge zero work. Replacement orders
recover a blocked source before fine setup, while terrain edits under an
already moving owner produce refills from its still-blocked fractional source.
These are different producer paths; the fine adapter must preserve its input.
The checker reconstructs every authored terrain transaction and compares every
result, route word, work and node count. This is full-request parity, with no
claim that the captured velocity/recovery trajectories are fully reimplemented.
`wc3_fine_results_probe.j` and the `fine_results` map-generator scenario retain
the authored producer; binaries/maps/raw captures remain external.

Saved Ghidra retains660 roles,69 layouts/468 fields,335 explicit ABIs and60
globals. Verified ECX and RET28/24/8/8 contracts type the full builder, setup,
request wrapper and selected-point getter. Native reports, paired captures,
map/member/source proofs and validation logs stay under external
`runtime/fine91-*`; `fine91-validation.json` records final checks and commit.

## Adaptive storage grows beyond the fine identity limit

Payoff92 closes ACC-05.1 without adding IDs. `14f570` gives adaptive nodes and
open entries2,048-element growth, while the fine open table grows by1,024.
The adaptive `CPaWarp` container is a distinct fixed256×12-byte allocation with
zero growth; Way Gate allocation/exhaustion remains GATE-04.1.

Unlike fine `147af0`, adaptive `163ef0` has no32,768-node admission cap. It
stamps the cell and stores the low16 bits of the physical node count before
appending. `1625f0` returns that ushort. Complete unchanged original searches
on a producer-reduced512×512 checkerboard/wall hierarchy give:

| Budget | Charged work | Physical nodes | Node capacity | Result / route points |
| ---: | ---: | ---: | ---: | --- |
| 400 | 401 | 452 | 2048 | partial /32 |
| 2048 | 2049 | 2118 | 4096 | partial /73 |
| 5000 | 5001 | 5100 | 6144 | partial /118 |
| 10000 | 10001 | 10136 | 10240 | partial /172 |
| 40000 | 40001 | 40241 | 40960 | partial /378 |
| 65535 | 65250 | 65538 | 67584 | success /423 |

The last success is an original index-alias consequence: new identities65536
and65537 look up as0 and1. The latter aliases the original goal across the
otherwise disconnected wall. Three actual cached lookups prove
65535/65536/65537→65535/0/1. C retains this behavior through the general ushort
metadata store; it does not recognize the fixture or substitute a route.

Five complete `166c30` wrappers retain route indices, adjusted endpoints,
partial bit20000000, admission success and request time100. The400-work
follow-up matches the first request over67,584 retained physical slots. An
explicit original enqueue/pop prefix over real search nodes then crosses
2,048/4,096 open-slot boundaries (including sentinel), drains all4,096 entries
with exact unsigned keys/generations/ties and retains6,144-slot backing. The
next complete public request clears that prefix and reproduces the original
400-work route. This prefix is a storage control, not a live crowd producer.
Storm imports receive host memory; no native OOM claim is made.

Production Move replaces the fixed adaptive level array and inherited fine
identity assertion with growable retained storage, keeps adaptive open growth
separate from fine growth, and releases both arrays at map teardown. Adaptive
and group route/save bounds now cover all65,536 ushort parent identities;
physical node backing may exceed that extent. The existing serializer shape
is unchanged. A maximum-extent route payload round-trip and corrupt extent
rejections pass. A real5,000-work group adapter followed by the separate
400-work owned adapter compares every original route word and all final node
words by a pinned FNV64, repeats over retained backing and checks teardown:
636 assertions pass. The old engine's1,024-slot adaptive heap fails the frozen
capacity control. O0/O2 matches all six complete searches and five wrappers,
all final nodes, grown queue order and recovery.

Two bounded read-only45-second live witnesses independently repeat the fixed
index allocation, original adaptive constructor/sentinel and first node
allocation. They do not certify live saturation or full motion. All six fresh
strict contracts pass, including existing fine-storage/public-result and
numeric contracts. Both editions pass2,514 tests/5,674,131 assertions each,
plus571 Python tests; production builds pass. Saved Ghidra retains660 roles,
69 layouts/478 fields,335 explicit ABIs and60 globals. Evidence and limitations
are frozen in `retail-adaptive-storage-1.27.json` and
`retail-adaptive-storage-live-1.27.json`; raw captures/reports stay external.

## Way Gate exhaustion retains ability-owned allocation

Payoff93 implements the bounded native Way Gate identity pool in CAbilityWarp.
The original04e510 allocator scans map-owner availability bytes1..255, marks
the first free byte and returns zero on exhaustion. Original04e210 increments
active count only on a transition; duplicate enables do not change it. The
04e4c0 release clears availability, decrements count only if active and disables
the record. IDs zero and greater than255 are rejected by active/getter bridges.

The complete original CPaWarp constructor and bridges execute unchanged, with
supplied map/path owners and Storm backing. All256 initial allocations,
duplicate activations, public active bridge reads, invalid/duplicate releases
and refills17/255/0 are frozen. The shared production allocator matches all259
allocations and complete availability bytes at O0/O2.

Ability41c270 stores the allocation result at6c even when zero. Setters/getters
do not silently retry. The43b840 ability still updates alternate presentation
when04e210 rejects ID zero. Engine initialization retains this attempt, exposes
zero/false getters for an exhausted gate and separates animation from actual
activation. Removing and readding Awrp permits a new attempt. Payoff96 corrects the original engine release boundary: semantic removal
retains the reservation until deferred ability cleanup. Same-callback
replacement allocates another ID; next-callback replacement can reuse it.
Ordinary later cleanup remains idempotent. Availability derives from ability
ownership instead of introducing a second independently serialized registry.
Save98 retains this state and rejects duplicate identities or active/configured
zero IDs. The public engine tests cover all256 gates, deferred removal/reuse,
no implicit retry, ability recreation, duplicate ownership and two save/load
round trips.

Two read-only65-second public witnesses create256 gates, set destinations,
activate/query each, remove IDs17/255 and create three more. All776 semantic
events repeat exactly:259 allocations,259 activations,256 queries and two
releases. The accepted v3 producer was archived under the movement-bypasses
filename; the committed capacity JASS is byte-identical and pinned. The strict
checker requires the complete producer tail, PE/map/source provenance and an
installed footer. No explicit completion marker was hooked. Source overlap,
existing route revalidation after ID reuse and automatic portal movement remain
open; these captures do not certify those behaviors.

Ghidra has673 saved roles,72 partial layouts/489 fields,341 explicit ABIs and61
globals. Native/live fixtures are `retail-waygate-pool-1.27.json` and
`retail-waygate-pool-live-1.27.json`; raw captures, producer/member proof, native
reports, Ghidra readback and validation logs remain under external runtime/gate93.

Both required edition suites pass2,516 tests/5,676,211 assertions each, plus
575 Python tests. Production builds and six fresh strict contracts pass.
`runtime/gate93-validation.json` pins the final logs and committed artifact.

## Way Gate overlap publishes ordinary routing history

Payoff94 ports source markers and their parent classifications into the production
adaptive hierarchy. Original04e360 normalizes world corners, subtracts map origin
and scales by32. Original15c000 floors the scalar rectangle and adds one to both upper bounds;15bf60 clips it to
fine bounds;15c030 overwrites base byte6 over inclusive floor-scaled ends. The
three parent publications do not rebuild base terrain classes or expose pending
terrain edits. Marker-bearing clear children force class2; blocked children
retain class1. Parent marker bytes remain zero.

Two stock400-by400 sources at(512,512) and(768,768) are created and removed in
both orders. Creation overwrites the overlap. Removal unconditionally zeroes
the whole rectangle; it does not restore a surviving source's overwritten bytes.
Inactive gates still publish sources. Exhausted ID zero also publishes at
creation, but its cleanup skips publication. CAbilityWarp retains authored half
extents at initialization and uses the current source position at removal,
including removal after its ability registry row is gone.

The unchanged world bridge, empty fine producer, marker writer, parent reducers
and64 complete warp-disabled adaptive requests execute over supplied storage.
All four lanes and both selected sizes compare every route word and final node
word. The shared production stamp/reducer also matches20736 native parent
controls, including blocked marked children and clipped missing children.

The production regression first failed210 assertions. The final port passes1622
assertions through real ability creation, semantic removal and ordinary group
route entry points. Eight complete publication states and64 complete engine
requests match the frozen original route/work/node fixtures. Save99 preserves
the marker plane and the exact published hierarchy: reconstructing markers from
live gates would incorrectly restore erased overlaps. Two save/load checkpoints
retain that erasure and reproduce subsequent route state.

Two bounded public JASS witnesses repeat all eight complete marker planes and
all2206 hierarchy class bytes. The checker requires the explicit tick9 producer
completion, installed footer and pinned PE/map/source hashes. The accepted
producer was archived under the capacity probe filename; the committed overlap
probe is byte-identical. These witnesses certify source publication and ordinary
routing effects; automatic portal traversal remains separate.

Ghidra retains677 saved roles,72 layouts/491 fields,345 explicit ABIs and61
globals. Native/live fixtures are `retail-gate-markers-1.27.json` and
`retail-gate-markers-live-1.27.json`; raw captures/reports and saved readback stay
under external runtime/gate94.

An asymmetric boundary control caught the upper-bound plus-one hidden by the
stock square footprints. Twenty-four complete native publications cover unequal
negative origins, reversed corners, clipping, zero extent and odd fine upper
bounds at IDs1/255/0. The world rectangle ABI is minY/minX/maxY/maxX, while the
map origin words6c/70 are X/Y. Engine coordinates keep their normal XY order.

Both required edition suites pass2,517 tests/5,677,836 assertions each, plus
579 Python tests. Production and six fresh strict contracts pass. Saved Ghidra
readback confirms all677 roles,72 layouts/491 fields,345 ABIs and61 globals
with no unsaved changes. `runtime/gate94-validation.json` pins final validation
and the committed artifact. Existing GATE-03.1 closes;153 tasks remain.

## Way Gate special edges reach retained Move routes

Payoff95 enables portal edges in production member, group and formation-distance
adaptive requests. The source marker and incoming edge ID are distinct bytes:
native node20 copies the base source marker when warp is enabled, while accepted
165220 writes the parent's source ID to child23. Ordinary accepted relaxation
clears23. Base expansion visits ordinary cardinals/corners first, then the active
special record. Its cost is exactly1; heuristic, nearest-node and heap tie policy
remain ordinary. This does not imply an admissible heuristic or optimal routing.

Reconstruction emits the reached point, then `c7fa0001`/software-float-ID sentinel,
then its parent. Exact source/goal endpoints retain their original positions.
Distance163440 deliberately checks the **parent's** incoming23: that contributes2
and incrementsA0; other edges round their geometric fine distance individually.
The jump itself can therefore contribute geometric distance. Replacing this with
a conventional portal-cost sum would diverge. All4608 unchanged original route
and distance requests match every route word,ten-word semantic node,work count,
partial endpoint and warp count. Controls cover four lanes,two sizes,six budgets,
three exits,warp off/on,active off/on and eight overlap publications. Supplied
empty64x64 fine storage and padded41/20/10/5 headers are explicit preconditions;
these search controls contain no emulator hooks.

Consumer165d10 checks only the immediate predecessor sentinel and truncates its
software integer ID toAL. Current activebit0 decides whether to warp, but the
exit comes from retained `points[index-2]` multiplied by base scale2. Retargeting
a record does not mutate an existing route. Placement failure retains the index;
inactive or successfully consumed crossings subtract2, then run ordinary mode0
selection. All2016 native/C consumer controls cover byte wrap,execute0/1,record
flags0..3,two route shapes,changed record destination and placement success/failure.
Owner lookup168d10 and physical placement168f00 are explicit stand-ins in those
controls; the full public engine test validates placement separately.

Group165e30 selects reverse1, then reverse0 when it encounters a sentinel. The
group can therefore publish the destination beyond a portal while each member
retains its own entry/exit path. The production group regression reproduces
native four-point cached and five-point fresh plans through real ability creation,
retarget and group planning. Portal exit admission uses six fine placement rings,
bounded adaptive distance<=32 with budget24 and warp enabled. Payoff97 corrects
the formerly misassigned32/24/6 argument meanings; this flat unblocked journey
did not distinguish them. Its callback runs
before ring-centre conversion and does not use public SetUnitPosition's terrain
level callback. Successful placement integrates the old velocity at current time,
commits the software position delta and notifies regions without replacing the
Move order. The crossing owner visit holds the pre-crossing goal vector, turns
using that vector and publishes stopped movement before the next fine refill.

The public producer creates one stock `nwgt` and a collision8/speed270 Footman,
issues Move,retargets during approach,issues a fresh order and then disables a
cached crossing during a third order. Two completed owned-game captures repeat
407 decisions,410 complete commits,11 searches/11 routes,six consumers,two
record writes and two successful warps. Cached exit is fine54.5/55.5; fresh exit
is39.5/45.5; disabling skips the pair and walks to the goal. All410 production
clock/position/velocity/facing commits match exactly. Ten Save99 checkpoints
before/after crossings,retargets and disable reproduce3094 continuation commits;
the full regression passes35076 assertions.

The first engine replay exposed another general scheduling error: third JASS
`.05` deadline is `3e199999`, before owner deadline `3e19999a`, although both
mature within the same primary quantum. Blanket owner-before-all-timers delayed
Move one visit. RunFrame now drains strictly earlier scalar script timers and
their events before the owner. Equal-deadline registration/mutation,catch-up and
zero-period policies remain existing NUM-02.10. Two extra read-only timer captures
repeat1065 rearm observations through explicit tick400 completion; host-duration
idle tails differ and are excluded from that timing comparison. These timing
captures support the scheduling fix but are not accepted full-heap contracts.

Frozen native/live fixtures are `retail-gate-edges-1.27.json`,
`retail-gate-consumer-1.27.json` and `retail-gate-traversal-live-1.27.json`.
The search fixture deduplicates complete outputs without dropping semantic words.
Python checks replay all controls atO0/O2 and reject altered/incomplete public
witnesses. Committed traversal JASS is byte-identical to the archived producer;
the engine test supplies common.j's missing `PLAYER_NEUTRAL_PASSIVE` constant and
its main entry. Synthetic gate art omits its build-only texture,which does not
block walking in the native class plane. Raw PE/data,captures,member proofs and
validation reports remain external under runtime/gate95.

Saved Ghidra readback confirms685 roles,72 layouts/493 fields,352 explicit ABIs
and61 globals with no unsaved changes. EntryECX is not a receiver for168f00;
its verified ABI is stack4 fineXY/RET4. An older04e360 role comment had map-origin
axes reversed; it now agrees with the correct X6c/Y70 structure and engine.
Paired group warps,blocked/outside exits,gate destruction,ID reuse,chained routes
and public destination getter quantization remain separate requirements.

Both required editions pass2519 tests/5,712,939 assertions each,plus582 Python
tests. Production and five fresh strict contracts pass. The first full run
crashed in the system SDL2 compatibility layer's standalone input test; the
isolated28-assertion control and full suites pass with native SDL2,the same
validation library used previously. `runtime/gate95-validation.json` pins final
logs,environment and the committed artifact. Existing ACC-04.2/GATE-02.3 close;
151 tasks remain. The subsequent lifetime witnesses are not part of this closure.


## Way Gate destruction preserves deferred ID ownership

Payoff96 closes GATE-02.2 and GATE-04.2 together. Two completed public JASS
journeys each repeat all movement, pool, marker and route observations exactly.
Both destroy an approached gate, complete ordinary walking, then start another
cached gate route. The replacement timing distinguishes the allocation boundary:

| Replacement | Allocation IDs | Motion commits / decisions | Searches / routes | Warps |
| --- | --- | --- | --- | --- |
| Inside the same timer callback as RemoveUnit | 1,1,2 | 514 / 512 | 8 / 8 | 0 |
| In the next timer callback | 1,1,1 | 299 / 297 | 7 / 7 | 1 |

A cached route names ID1. Same-callback replacement takes ID2 while ID1 remains
reserved; after cleanup the consumer skips the inactive old record and walks.
Next-callback replacement takes the released ID1, so the consumer revalidates
its current active bit and crosses to the **old cached exit54.5/55.5 fine**.
The new record's destination19/22 and new source768/512 do not rewrite that
retained route. Each witness has three allocations/activations/destination
writes, two releases, five complete marker publications and four route consumers.
All256 availability bytes and complete marker planes are retained, not summaries.

The first production regression diverged at movement commit297: releasing the
reservation from `A_UNIT_REMOVING` let same-callback replacement take ID1 and
warp. `CAbilityWarp` now releases at `A_UNIT_REMOVE`; explicit ability removal
still releases immediately. Deferred-free entities remain ineligible for engine
gate queries. This changes ability-owned lifecycle, not the generic scheduler.
The exhaustion test checks no availability before cleanup, no implicit retry,
and a successful ID17 attempt after cleanup and ability recreation.

A separate read-only native boundary diagnostic, `gate96c-remove-boundaries.jsonl`,
confirms both RemoveUnit calls return with the identical used-ID1/active pool.
It is one completed capture, not a repeated parity contract. Release43b8b0 comes
later through `Unit_RemoveAllAbilities`48e860 (return site48e898), queued unit
receiver690490 and scheduled event bridge060ca0 (return site060d12).
`JassNative_RemoveUnit`210c10 resolves a stack handle, calls virtual84 and
`Unit_QueueRemovalTasks`694690 with1,1. That queue retires current order chains,
suspends movement, appends d015a/d0164/d0156/d0178/d0144 and action0, then
67df00 dispatches. The two flags' broader meanings remain unassigned. Ghidra
saves three descriptive roles and the instruction-established cdecl/plainRET
and thiscall/RET8 signatures; readback is688 roles,72 layouts/493 fields,
354 explicit ABIs and61 globals, with no unsaved changes.

`retail_gate_lifetime.h` supplies all813 original motion words and the exact
public producers to the production JASS/Move world. Ten checkpoints per journey
span removal, both replacement times and arrival. They reproduce2692 immediate
and757 delayed continuation commits. The existing410-commit cached/fresh/disable
journey and3094 saved commits also pass after sharing the scene harness.
`retail-gate-lifetime-{same,reuse}-live-1.27.json` pins four external raw captures,
observer/map/PE provenance, exact semantic rows and producer hashes.
`verify_wc3_gate_traversal_trace.py` retains its existing traversal contract and
accepts the additional fixture-declared pool/publication events. Python mutation
checks reject missing releases, changed availability, marker planes, route words,
consumer indices, motion words, incomplete tails and altered provenance.

Validation: both complete Classic/TFT runs pass2520 tests/5,755,638 assertions
each;586 Python checks,production and five fresh strict corpus contracts pass.
`runtime/gate96-validation.json` pins the logs, supporting evidence and commit.
Boundary diagnostic SHA256 is
`63ec39a3d23b38c573d7b5c32a6cfe305c1eae17bcacddd64d7a9cc48dbc5ab5`.
Its archived observer adds read-only hooks at210c10 and43b8b0 to the pinned gate
observer; no native writes or replacement implementations. These single-member
witnesses do not establish all deferred deletion scheduler boundaries, getters
inside RemoveUnit callbacks, paired groups or blocked exit behavior.


## Blocked gate exits preserve stopped retry state

Payoff97 implements the shared placement and route-progress rules exposed by
GATE-01.2. Two complete public journeys repeat all original movement, searches,
route consumers and gate pool/marker states. One blocks only the requested
exit cell; the other blocks650 cells around it. Both then issue fresh orders
with positive and negative outside-map destinations.

| Exit witness | Motion commits / decisions | Searches / routes | Warp attempts | Saved suffix commits |
| --- | --- | --- | --- | --- |
| Single blocked requested cell | 568 / 563 | 15 / 24 | 1 successful | 4747 |
| Surrounded exit | 712 / 708 | 13 / 13 | 8 rejected | 5591 |

The first regression failed at nearby-placement commit41 and surrounded-exit
commit39. Original16ec00 passes distancefilter32, querybudget24 and **six
placement rings** to16ecc0. Earlier unblocked witnesses accepted the first
candidate and did not distinguish these parameters; the engine and old Ghidra
comments had incorrectly assigned32 rings/filter24/budget6. The corrected shared
placement query now admits the nearby point54.5/54.5 fine, while the surrounded
exit never reaches a valid footprint within six rings.16ee00 runs only after
footprint acceptance; no callback is invoked in this rejected case.

Two further movement rules explain the remaining failures. An empty or denied
admitted fine refill stops and turns toward the held destination, as16fbd0 does;
falling back to the previous heading diverged at the first post-warp empty refill.
Retained coarse progress is checked before every fine refill, even with an
invalid fine index. Native165b60/165f10 retries the surrounded crossing after
its wait. All eight failures preserve cached crossing index2 and emit no warp;
the old engine instead rebuilt a fine leg and started walking toward its entrance.
These rules apply to production admitted Move routing without scenario-specific
branches. Generic ability routes without an admitted fine destination retain
their existing caller policy. The first full suite exposed that boundary; it
also caught tests that advanced G_RunEntities/direct thinkers while leaving the
fine-request owner clock frozen. Collision and queued-construction witnesses
now drive RunFrame. Collision fixture units publish SVF_MONSTER like real unit
spawn, so the production map bake does not stamp their own cells as static
obstacles. Assertions still require detouring, stationary blockers, and the
queued construction lifecycle.

`retail_gate_exit.h` supplies every native motion word and byte-identical public
JASS producer, with the two common.j constants and main entry supplied by the
engine harness. Ten save checkpoints per journey cover admission, wait/retry,
unblocking and outside orders. All1280 commits and10338 restored continuation
commits match clock, fine position, velocity and committed facing exactly. The
earlier gate95/96 trajectories and saved continuations remain exact.
`retail-gate-exit-{near,sealed}-live-1.27.json` pins the four completed original
captures and full semantic streams; empty loading attempts are excluded.
The surrounded pair uses individually pinned60/90-second host capture windows.
The inherited shadow-map member was oversized for these flat maps; accepted
captures completed after the retail loading dialog was dismissed. Later group
fixtures supply the correct4096-byte member. Loading failures are never parity
evidence, and raw archive/map hashes preserve that distinction.

Ghidra saves the corrected formal argument names, complete fastcall16ecc0 and
16ee00 signatures, and the44-byte placement context with all eleven assigned
fields. Saved readback is688 roles,73 layouts/504 fields,356 explicit ABIs and61
globals with no unsaved changes. Merged-upstream Classic and TFT each pass
2539 tests/6,061,924 assertions, with593 Python checks. Production builds and
eleven fresh strict gate contracts pass. This closes GATE-01.2; activation
threshold equality remains separate.


### Group gate continuation and consecutive portal admission

Payoff98 records two complete two-member public GroupPointOrder journeys. Both
units cross the active gate, Stop/reset, then retain the cached crossing after
WaygateActivate(false). The open map walks and regroups; a full-height WPM
NOWALK column31 makes that disabled edge the only crossing, so each member
approaches a partial endpoint and completes the ordinary retry/forced-arrival
lifecycle. Repeats retain every consumer, request, route, pool/marker state and
motion word, with explicit creation-order member indices. Raw mover addresses
are mapped per capture; unknown, swapped or missing identities fail verification.
No route/request ordinal or numerical word is normalized.

Production matches610 open and635 blocked commits plus5750 and6025 continuation
commits from twelve saves per journey. An apparent early terminal mismatch was
a harness ownership error: prior tests had consumed the engine owner's random
stream. Native raw retry-init/result rows showed the initial4273436052/209508436
owner words and every later draw. The harness now calls G_InitLockedMapRandom
with the map's four fixed/eight random race preferences. Retries consume that
same production stream; this is not a fixture-specific production seed. Invalid
149/179-millisecond checkpoints were corrected to145/175 on the five-millisecond
harness schedule. Unwritten saves and stale reports remain rejected evidence.

Payoff99 adds a two-gate chain with both active, A off/B on, A on/B off and both
off. Native first crosses to35.5/39.5 fine, then acquires a new member coarse
route whose source already lies inside the second entrance.165b60 checks165f10
immediately after acquiring that route, before any fine request. The former
engine built a one-point fine leg and attempted movement at commit43 instead
of crossing. G_BuildUnitMoveFineRouteStatus now propagates this coarse progress
status; the same helper checks retained routes. The caller stops and turns
for the admission visit, preserving normal translation and retry policy.
All575 chain commits and4564 continuation commits match after this fix.

The unchanged162cb0/1627e0 multi-gate oracle compares512 complete requests:
chain/equal-distance alternatives, both publication orders, four active states,
four lanes, two footprint sizes, budgets10/400 and warp off/on. Full ten-word
node records, route points, work, distances and eight publication states match
production kernels. Equal single-A and single-B routes each cost58; both active
selects ID1 at58 independently of publication order; ordinary walking costs59.
Native rectangles are stored Ymin/Xmin/Ymax/Xmax; the C publication API accepts
XY bounds. Original physical placement and owner lookup are not stubbed in the
public trajectories; controlled search supplies empty fine storage/map headers.

`retail_gate_group.h` and `retail_gate_chain.h` carry complete native motion and
byte-identical public producers. Strict live fixtures pin six completed captures
with correct4096-byte flat-map shadow members. Map/raw hashes remain external.
Saved Ghidra comments retain immediate coarse checking and multi-edge distance
scope;688 roles,73 layouts/504 fields,356 ABIs,61 globals and no unsaved changes.
Merged-upstream Classic/TFT each pass2539 tests/6,061,924 assertions, with593
Python checks, production builds and eleven fresh strict contracts. These
results close GATE-02.4/GATE-03.2/GATE-04.3. GATE-01.1 remains open.


### Adaptive eligibility survives flight birth and changes on type rebind

Payoff100 fixes the unconditional AI_FLYING adaptive bypass in both member and
group routing. Retail path88 bit200000 is an independent policy. Path_Activate
166060 defaults it on; unit initialization68a060 enables nonstructures before
publishing movement class. A newly created authored flyer therefore retains
class3/lane6 adaptive routing and can traverse a Way Gate. The first production
regression diverged at motion43/5100ms, using direct flight while retail turned
toward its coarse gate leg.

Type rebind670950 behaves differently. Instructions670ddb/670de2 compare the
new movement enum at Unit+1fc with2 and record SETZ. The later call0594a0 disables
adaptive routing for that flying type; ground rebind restores it.0594a0 resolves
the canonical bridge's owned path, resets its buffers, then sets/clears bit200000
without touching class bits.05c4d0 applies the same write to a direct path and has
no direct callers. Captain9d6e60 explicitly passes movementType!=2; this does not
mean captain selector2 is a flying movement type. Structure/construction flags
in68c190 remain a distinct predicate; the public parity scene here uses mobile
nonstructures and does not claim a complete construction-class table.

Move now stores adaptive_disabled independently of collision-layer flags,
initializes it on unit birth and updates it after type rebind. Group planning
and member refinement consult that state while retaining their authored lane
and current fine-query mask. Save100 introduced that policy and rejected the prior
Save99 layout/version; current Save101 retains it and rejects earlier formats
after adding marked waypoint ownership and expanded order queues. No client
state or scenario-specific production branch is added.

A complete public script contrasts ground birth, authored flight birth, a ground
unit transformed to flight and restoration to ground. Three crossings and the
transformed flyer's direct-route control repeat all383 motion commits. Production
RunFrame matches every clock, fine-position, velocity and facing word; twelve
saves reproduce2811 continuation commits. Earlier ground/flight/ground movement
controls still match737 commits and6534 saved suffix commits. Earlier blocked
exit journeys remain exact. The corrected producer clears each removed-unit
variable; stale handles in earlier exploratory producers logged serializer
diagnostics and are not the committed save fixture.

The shared wc3_acc_in_range comparison includes equality at software scalar
0x3efae148 (0.49 accelerator units) and rejects the next positive source scalar
at origin0.320 complete original165f10 cases cover two origins, +/-2 source ULPs,
index0/2, force0/1, all active-bit combinations and placement failure/success.
Complete165d10/166030/165200/167ae0 and scalar calls execute unchanged; owner lookup
and physical placement are explicit stand-ins. Production range and consumer
kernels reproduce every output at O0/O2. Success consumes the cached crossing,
invalidates the fine index and returnsAL1; disabled crossing consumes it without
placement and returns0; failed placement returns2, preserves both indices and
raises delay to20. Production propagation now retains that failure status2.
The sole direct165b60 caller supplies force0; force1 is not claimed as a public
movement producer.

Ghidra saved readback verifies690 roles,73 layouts/505 fields,359 explicit ABIs
and61 globals, with no unsaved changes. UnitOrdersPrefix now includes the proven
movement enum at1fc; both policy setters and165f10 have explicit instruction-backed
signatures. Fourteen fresh strict contracts pass, including prior gate kernels,
all complete gate journeys and the earlier movement-mode captures. The required
full editions each pass2540 tests/6,093,939 assertions and595 Python checks pass.
Debug/release WC3, SC2 and WoW production builds finish without warnings. Source
fingerprints remain unchanged through the full run; final evidence is recorded
in runtime/gate100-final-validation.json. GATE-01.1 closes, completing all eleven
gate leaves without claiming whole-pathfinding fidelity.


## Shared retry and overlap draws preserve complete movement

Payoff101 follows two authored ground movers with repulse enabled through exact
initial overlap, public grouped Move, active gate crossing, Stop/reset, disabled
sole-edge wall routing and scripted pause. The locked map initializes the normal
owner producer with four fixed/eight random race preferences. Its observed
4273436052/209508436 state is asserted after that producer; production never
installs the captured state or burns a fixed number of random words.

The thirteen exact-overlap direction calls and thirty-five retry initializations
consume48 consecutive owner draws. Both members share this stream. The91 retry
transactions retain source/adjusted-goal words, member count, owner visit counter,
retry count/result and both owner words before/after. Nonzero-count decrements
and terminal4 consume no draw. The final owner is3870341697/146278604. Each direction
consumes low23 draw bits and the retail trigonometric table; retries select bit22
for7/8 before decrement. The frozen original1,408-query oracle covers eleven seed
prefixes, including signed-boundary/full-word seeds; unsigned accumulation wraps
modulo2^32 and the four byte-offset cycles wrap independently at188/212/236/244.
Separate693710 per-unit/TLS streams and unlocked/lobby/default-lock producers
remain NUM-04.5/06, not part of this shared path-owner contract.

The integrated regression exposed three production errors:

- Public grouped admission stopped an embedded idle unit through69a840/05ca50
  before binding its cohort. Native first member8.5/9.5 recovers to8.5/8.5, while
  the next member retains8.5/9.5. Engine recovery was restricted to captain
  handoffs. S_IssueMoveOrder now owns the shared public admission recovery;
  internal order_move remains a separate behavior path.
- Separate_Update1702f0 checks requested speedc0 after consuming cooldown. Positive
  speed clears retained displacement and installs cooldown7;16e830 excludes
  candidates with nonzero c0. Counter1041 therefore damps the stopped member's
  retained vector without adding its moving peer. Engine Move previously treated
  every unpaused repulsor as idle. Its admitted velocity publication and turn/retry
  stops now provide this movement gate. The claim is the admitted finite Move
  domain, not unverified nonfinite inputs or unrelated behavior families.
- Unit_CanUseSeparation66fc50 rejects counted suppression198, suspension54 and
  scripted pause5c.200000, among other flags. Scripted pause retires the repulsor;
  resume recreates its zero state at the list head. Engine pause previously left
  an inactive source on the alternating list. The inverse and saved paused
  membership now execute through public JASS and RunFrame.

Production matches all1361 complete motion commits,830 scheduled separation
visits and91 retries. Eleven checkpoints reproduce12667 motion continuation
commits, retaining random state, repulsion vectors/cooldown/list parity, physical
cohort state and admitted fine/coarse routes. The earliest save115ms precedes
first overlap draw120ms, after the public group handle exists. An exploratory
95ms save crosses a changed JASS group-registry shape and is rejected by the
existing envelope contract; it is not counted as a passing continuation.

Two fresh final captures use the reusable read-only
wc3_pathfinding_random_movement.js extension through --random-movement-events.
The original core observer stays byte-identical to prior gate captures. The
strict fixture preserves254 full candidate-query observations, thirteen direction
outputs, all retry-init/result rows and all owner/motion/gate streams. Only wall
time and declared heap addresses are normalized; creation-ordered member mapping
is mandatory. Earlier exploratory forks and unbounded post-completion routes
are external diagnostics, not replacements for this stopped/paused producer.
Full multi-neighbor spatial traversal remains SEP-02/03.

Saved Ghidra readback verifies694 roles,73 layouts/506 fields,364 explicit ABIs
and61 globals. It retains the unit separation predicate, counted suppression
inverse, public stopped-recovery wrapper and movement-speed filter evidence.
Final `make test` passes Classic and TFT, each with2544 tests/6,386,799 assertions,
plus598 pathfinding-tool Python checks. Eighteen fresh strict contracts pass against
the final adaptive-header implementation. This closes NUM-04.1/02 for the shared
overlap/retry owner; NUM-04.5/06 retain the separate startup/TLS-stream scope.
The movement performance work and remaining scaling target are documented in
[WC3 performance](performance.md#october-4-literature-shortlist-exact-retail-simulation-at-scale).

## Coarse and fine contention retain independent player FIFOs

Payoff102 closes SCHED-04.1/02. The controlled flat64×64 fine scene contains a
wall through columns32/33 and96 Footmen, alternating between two allied owners.
Each unit receives an independent public Move at producer ticks10,100 and190;
all300 samples complete. Independent captures `scheduler-contention-102-c.jsonl`
and `scheduler-contention-102-e.jsonl` in the external1.27 runtime archive agree
on all25,902 ordered scheduler observations through completion. The frozen
[records](../../../tools/ghidra/fixtures/retail-scheduler-contention-1.27.jsonl.gz)
and [certificate](../../../tools/ghidra/fixtures/retail-scheduler-contention-1.27.json)
retain the target/source hashes, actual work and complete FIFO snapshots.
`verify_wc3_scheduler_trace.py` rejects incomplete samples, missing search work,
changed budgets/times/queue ends/reset cadence and different ordered repeats.

Ghidra's typed `WC3PathSchedulerBucket` and `WC3PathPrefix` remain authoritative.
The saved `OpenRealm_coarse_budget_contention` tag links admission168310,
unlink1686a0, update167fa0/167310, coarse166c30, fine166e90 and route162cb0.
Each player has four independent buckets:

| Policy | Bucket selector limit | Owner work limit | Reload | Reset period |
|---|---:|---:|---:|---:|
| Group coarse | 5000 | 800 | 3 | 4 owner passes |
| Priority coarse | 2000 | 300 | 2 | 3 owner passes |
| Member coarse | 400 | 900 | 2 | 3 owner passes |
| Fine | 700 | 1100 | 1 | 2 owner passes |

The bucket selector limits are defaults, not proof of every producer’s search
quota. This scene’s group/member/fine requests actually use5000/400/700; priority
producer mapping remains open. Target-scheduling flag04000000 can change a
bucket without changing the path-owned limits (`Path_SetTargetScheduling`168ab0).

Unsigned work greater than the limit denies admission; equality admits. The
search charges its actual popped nodes afterward, so work can overshoot. A
nonempty queue admits only its head. Repeated denials retain insertion order
without duplicating requests. Coarse admission first checks its10-pass request
interval, then unlinks a previous fine request, then acquires its own bucket.
Denial clears the coarse timestamp. A completed coarse search with fewer than32
pops also clears it; fine clears its timestamp when cumulative bucket work is
below64. Adaptive setup can return a one-point route without invoking the inner
search; this is genuine zero work, not missing instrumentation.

The first owner pass admits42 group searches and queues6 per player. It then
executes42 member-coarse and42 fine wrapper requests per player. Repeated
exhaustion denies24 group attempts for each owner and2214/2266 fine attempts.
The maximum observed admitted FIFO wait is3 owner passes for either coarse
owner,17 for player0 fine and14 for player1 fine. Eight coarse queue episodes
per player reach admission;443/561 fine episodes do so. Twenty fine episodes per
player leave their queue through independent unlink/replacement. One player1 fine request remains queued at the30-second producer boundary,
with one observed owner pass of waiting. It is admitted in the captured suffix;
the certificate reports the boundary state rather than using that later drain
to claim an empty queue at30 seconds. These measurements establish
bounded waits for this fixed producer, not a universal bound for arbitrary
workloads. The earliest denied request remains ahead of later arrivals.

The engine previously gated only fine searches. `G_UnitMoveGroupDestination`
and the member adaptive consumer now acquire Move-owned coarse requests before
searching and charge actual work afterward. Their intrusive queues provide O(1)
admission and removal, without scanning units. Requests reside in stable edicts
or heap-owned physical groups. Independent time/rank/work state survives
save109; validation rejects bad policy/owner, duplicate ranks, impossible work
or countdown, inconsistent counts and live links. Restore rebuilds process
pointers from authoritative insertion ranks. The existing geometry-storage
fixture now advances actual owner-budget callbacks instead of jumping the clock
while retaining exhausted work.

`retail_coarse_and_fine_contention_matches_captured_owner_window` compares517
actual scheduler transactions over five owner passes:7,225 assertions cover
admission, work, request times, queue head/tail/count and countdown. This is
paired with the real public96-unit JASS Move/normal-RunFrame regression and883
save/continue assertions for poses, velocities, orders, waits and RNG. That
public regression verifies engine continuation; the captured scheduler window
is the exact retail comparison. The full96-unit movement trajectory is not
claimed exact to retail by this chunk.

`wc3_path_scheduler=retail` retains the recovered limits/cadence. The authorized
`responsive` policy freezes a queue-sized coarse service grant at every owner
boundary; fine retains its existing deterministic service grant. It changes
movement start times under contention. The public1024-unit regression still
starts all units by200ms and reproduces5,130 saved continuation assertions.
This scheduling change does not establish the0.8ms pathfinding performance target.
Priority/non-unit producer mapping, mutation during active native traversal,
counter wrap and the broader retail fidelity gaps remain open.

The extended read-only observer changes historical source hashes. Earlier gate
and random-consumer certificates now verify their original, hash-addressed source
bytes under `tools/ghidra/fixtures/sources/`, rather than insisting that every
future observer remain identical. New Scheduler102 observer/controller/map-maker
sources are frozen the same way. No older expected observation is changed.

Fresh original-x86 scheduler execution also passes64 bucket initialization,
448 policy selections,1280 cadence checks,16,384 FIFO operations,162 interval
cases,1024 class transitions and256 target-scheduling transitions. These isolate
native primitives and complement the live producer evidence; they do not map
unobserved priority/non-unit producers.

## Target priority keeps the group's search quota

Payoff103 corrects the group producer's choice of scheduling bucket. Native
`PathGroup_RequestRoute`16ce10 is the sole static caller of
`Path_SetTargetScheduling`168ab0. It resolves the group target and updates the
priority flag **before** bypassing construction for a retained coarse route.
Changing that flag unlinks previous scheduler membership and clears the fine
selection flag, preserving the request timestamps and path-owned search limits.
An unchanged flag does not unlink. Group preparation separately copies the
class nibble from its first member, even when another member supplies the source.

Read-only captures `scheduler-target-103-a.jsonl` and
`scheduler-target-103-b.jsonl` repeat the complete stopped Smart/Follow producer
on `PathingRE-FollowVelocityComplete-261002.w3m`. They agree on all2254 ordered
scheduler, search and producer records, including1015 target-policy calls.
The six group searches are:

| Owner counter | Policy | Actual popped nodes | Path-owned quota |
|---:|---|---:|---:|
| 1058 | Target priority | 4 | 5000 |
| 1164 | Target priority | 2 | 5000 |
| 1291 | Point ordinary | 9 | 5000 |
| 1300 | Target priority | 3 | 5000 |
| 1317 | Target priority | 3 | 5000 |
| 1334 | Target priority | 15 | 5000 |

Priority's initialized selector2000 therefore does **not** limit this group's
search to2000 nodes. Its owner work allowance is300, versus800 for ordinary
group searches. Five member-coarse searches retain400 and six fine searches
retain700. Actual popped nodes, work deltas, timestamps, flag updates, FIFO
operations and reset cadence are checked independently before comparing repeats.
All300 producer samples and the authored Stop/completion are mandatory.

The [frozen records](../../../tools/ghidra/fixtures/retail-scheduler-target-1.27.jsonl.gz)
and [certificate](../../../tools/ghidra/fixtures/retail-scheduler-target-1.27.json)
pin both complete capture hashes, the map, producer and original observer
generation. The exact generated JASS producer is retained as a hash-addressed
source. Reproduce the verification with:

```sh
python3 tools/frida/verify_wc3_scheduler_target_trace.py \
  /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/scheduler-target-103-a.jsonl \
  --repeat /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/scheduler-target-103-b.jsonl \
  --output /tmp/wc3-scheduler-target.json
```

The engine's Move-owned `S_SetMoveCoarseTarget` cancels the old coarse FIFO
without resetting its clock, then changes the policy before retained-route
reuse. `G_UnitMoveGroupDestination` admits and charges the selected bucket while
retaining the5000-node group quota. Deterministic responsive grants and saved
work validation now budget for that actual quota, rather than the priority
selector. No saved layout changes; save109 remains current.

The production query producer also excludes `SVF_MOVE_WAYPOINT` from its resolved
target. These edicts carry point-order coordinates; treating their pointer as
a real target would incorrectly put ordinary point moves into the priority pool.
Captains and real Smart/Follow targets retain their original pointers.

The new blocked-wall regression failed four of its five reached assertions
before implementation and now checks successful target routing while the
ordinary budget is exhausted, exact queue removal on a target-policy change,
retained-route reuse and unchanged timestamps. The complete public Smart/Follow
regression additionally checks both scheduling policies and live validation,
alongside its existing raw-word motion and three saved continuations. Strict
Python mutations reject truncated timelines, changed sources, search quotas,
priority/class flags, work, timestamps, bucket policy, FIFO ends and cadence.

`MapPathfinding.java` now replays the previous contention annotations and these
target producer/consumer findings. It runs successfully in the active pinned
Ghidra program, mapping694 functions without changing prototypes; saved comment
readbacks match the replayable descriptions. The class15 inventory records
bridge callers6cf5e0,6cfe00,6d30c0 and6d3190, all explicitly disabling adaptive
routing before publishing class15. Unit activation68a060, owner change698ce0
and captain virtual actor9d2f90 use their owner class instead. The four class15
objects' concrete runtime identities and complete public trajectories remain
unverified; SCHED-03.1 is not closed merely by fixing its target-priority slice.

Focused validation follows the authorized batched full-suite cadence: Classic
and TFT each pass all12 public target journeys with364082 assertions. The new
wall/queue-policy regression passes14 assertions; the captured517-transaction
window, public96-unit contention/save case and1024-unit responsive/save case
remain passing. All seven scheduler Python checks pass, including the strict
mutation tests for both producer generations. Production and test binaries
build successfully. The full-suite checkpoint remains payoff102; this chunk
does not claim another full validation or the unfinished performance target.

Final `make test` passes Classic and TFT, each with2664 tests/7,227,304
assertions, and612 pathfinding-tool checks. Production and test builds pass.

## Non-unit path producers share scheduler class15

Payoff104 closes the producer inventory in SCHED-03.1. Ordinary units publish
player ownership at activation and owner transfer; captain virtual actors publish
the captain's player. Group paths copy the class of their first individual
member, even when another member supplies the representative pose. Resolved
unit targets select priority coarse policy; point-order waypoints do not.
Individual/member adaptive requests and fine requests retain their separate
policy buckets. The four explicit non-unit producer sites are now identified
from original MSVC RTTI, constructors and complete function bodies:

| Producer | Owning runtime classes | Mover bridge |
|---|---|---:|
| `6f6cf5e0` | `CArtilleryLine` | `+f0` |
| `6f6cfe00` | `CMissileLine` | `+d8` |
| `6f6d30c0` | Shared `CBulletPath`, `CMissilePath`, `CMissileSpiderAttack` point initialization | `+78` |
| `6f6d3190` | Shared target initialization of those classes | `+78` |

These initializers disable adaptive routing and publish class15 before starting
their request. The line producers explicitly publish profile0/0. The shared
initializers take profile parameters from their caller; those values must not
be assumed zero for every caller. A failed artillery target/speed guard returns
before class publication. The resulting path uses the same scheduler row as
player15, independently of its launching player's presentation/damage ownership.

Two complete, read-only `scheduler-nonunit-104-b/c.jsonl` captures use actual
public attacks and agree on550 ordered scheduler/search/marker observations.
Five Crypt Fiend `Aspa` attacks create `CMissileSpiderAttack`, identified at runtime
by vtableRVA `b0fc4c`. Each calls the shared target initializer through `6d3b77`,
then publishes class15 through `6d3224`. Each physical group copies class15 from
its individual member. All five fine requests use row15/policy3, quota700,
radius0, four popped nodes and19 constructed nodes; there are no coarse searches.
The exact query starts at raw `40e3c000/41e00000` (7.1171875,28), targets (12,28),
and terminates at the target's occupied region with destination-first points
(10.5,28.5), (9.5,28.5), (8.5,28.5), then the unchanged source. Three artillery
entries fail their pre-publication guard; they are not counted as class15 paths.

The [frozen stream](../../../tools/ghidra/fixtures/retail-scheduler-nonunit-1.27.jsonl.gz)
and [certificate](../../../tools/ghidra/fixtures/retail-scheduler-nonunit-1.27.json)
pin both completed capture hashes, map, generated JASS and observer sources.
`verify_wc3_scheduler_nonunit_trace.py` rejects incomplete timelines, altered
producer identities, classes, quotas, radius, routes, work, timestamps, FIFO
state and reset cadence before comparing the complete ordered repeats.

Move now owns projectile initialization and resolves the fine scheduler class
independently of the network player. Attack, Death Coil and bolt producers use
that initialization. Fine admission, coarse admission and saved fine queue
validation use the same class resolver. Adaptive-disabled, zero-radius projectile
queries reach the existing fine algorithm without unsupported adaptive-lane
conversion. No extra pool or rawcode exception is introduced; save109's layout
is unchanged. This establishes scheduling ownership and route construction;
it does not claim a complete reimplementation of Aspa's secondary projectile
motion, timers, attack effects or every missile subclass's public trajectory.

The regression originally failed owner-budget admission and adaptive setup.
It now drives a real `fire_rocket` projectile through the production fine route,
compares every captured route word/work charge, exhausts the shared class15 row,
saves/restores its FIFO alongside a real player15 unit, removes the projectile,
and admits the surviving request. It passes32 assertions in both Classic and
TFT. Each schema also passes the ownership/fine queue checks,188 combat tests,
Death Coil, Defend and in-flight artillery save coverage. Ten focused scheduler
Python tests pass. Production/test binaries build; this is the second implementation
commit after the last full checkpoint, under the authorized twelve-chunk cadence.

`MapPathfinding.java` replays698 mappings, including the four new producer names
and class-copy evidence. `VerifyPathfindingMissileClasses.java` follows all five
vtable locator/type-descriptor chains and checks the persisted producer mappings
without mutation. Both scripts run successfully against the pinned Ghidra
program, which is saved. Static producer mappings and observed runtime searches
are labeled separately so these facts remain reusable without overstating parity.

## Work-counter wrap preserves unsigned admission

Payoff105 closes SCHED-03.3's work-counter boundary. Ghidra disassembly identifies
the original post-search fine slice `166fcd..166fdd`: `ADD` to bucket work,
unsigned compare of that cumulative result against64, then conditional clear
of path `+7c`. Coarse slice `166db3..166dc5` adds the same way but compares the
latest search's charged work against32 before clearing path `+80`. Neither
addition saturates or widens its stored result.

The original-x86 scheduler oracle now executes those exact instruction slices,
without patching their code or substituting search calls. It supplies explicit
synthetic pre-charge counters, charged work and request timestamps, then runs
the complete original admission function. All2560 cases cover16 classes/four
policies, zero/ordinary/high-bit counters, carry through `UINT32_MAX`, and both
threshold edges. Admission and intrusive FIFO results agree with a second run
starting at the clean post-charge counter. For example, adding64 to `ffffffe0`
wraps to32: fine clears its timestamp; coarse retains1037 because64 is not below32.
All four policies then admit from that clean unsigned work value.

These are deliberately **synthetic work-state boundaries**, not a claim that a
public retail battle reaches overflow. Retail admission rejects work above its
allowance before the next bounded search. The observed stock search limits
5000/400/700 and the known budget+1 cutoff keep work far below32-bit overflow;
even a full unsigned16-bit path-owned search limit cannot bridge that gap.
The previous complete public contention and non-unit captures remain the
production witnesses for budget ownership and actual charging. Owner-clock
rollover, a different state variable, remains SCHED-01.2.

The [frozen original-code report](../../../tools/ghidra/fixtures/retail-scheduler-work-1.27.json.gz)
pins the binary, oracle source and generated engine input header. Replay with:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_scheduler.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --report /tmp/wc3-scheduler-work.json \
  --header /tmp/retail_scheduler_work.h
```

The engine regression consumes the captured results through Move's production
charge/admission operations in all64 buckets and compares wrap with clean
admission. Existing unsigned charge behavior matches; it needs no special
wrap case. The engine payoff is closing the save-state hole found during this
audit: fine FIFO validation previously accepted unreachable `UINT32_MAX` work,
while coarse validation already enforced an admission-plus-one-search bound.
Fine validation now permits the actual allowance plus701, including responsive
grants, and rejects larger stored totals using a widened comparison. Both save
and load use this validation. Save109's representation remains unchanged.

The new save regression failed four assertions before the fix. It verifies the
inclusive maximum through real write/read, rejects the next value and
`UINT32_MAX`, then verifies a clean save, in both retail and responsive modes.
Focused Classic/TFT checks also cover original-code charge/admission boundaries,
real public contention/save continuation and responsive crowds. Twelve focused
scheduler Python tests pass, including frozen-source and header provenance.
Production/test binaries build. `MapPathfinding.java` includes the exact
instruction boundaries, cumulative versus local threshold and synthetic scope;
the replayed Ghidra program is saved. Full validation remains on the authorized
batch cadence; no performance-target completion is claimed.

## Task transitions retire every local scheduler request

Payoff106 closes SCHED-03.2 with public queued-head mutations, original scheduler
operations and production Move/save regressions. Admission `168310` does not
traverse a queue or invoke gameplay callbacks: it checks unsigned work and the
current FIFO head. Queue mutations occur between admissions/owner passes.
Mutation from a callback during the separate group/member iteration remains
SCHED-02.4; this leaf does not certify that different operation.

The read-only observer now resolves `Unit_AdmitOrder`'s `+164` bridge identity
through registry `d68610`, checks the resolved mover's `+14/+18` identity and
records its `+a8` path. Initial public order submissions bind all96 actors to
actual requests. This matters: stopping an arbitrary unit can leave every queue
unchanged because that unit was never waiting. The first exploratory producer
selected such units and was not accepted as evidence; the completed producer
mutates the actual first three queued fine paths.

Two complete40-sample public producers start96 allied Footmen behind the same
reduced-map wall and submit independent point Moves. At tick11 player0's fine
FIFO begins with actors52,50,48,46 and has21 entries. Public Stop of52 removes
only52; reissuing its Move leaves existing survivors unchanged. `SetUnitOwner`
of50 removes the next old-row head before publishing class1; its replacement
admissions use player1. `RemoveUnit` of48 removes the next head, which is never
admitted again. The remaining18 requests retain their order; actor46 receives
the next player0 fine admission. Snapshots cover both players and all four
policies. The next tick's snapshot has no pending requests, and the final
producer synchronously stops/pauses every survivor before marking completion.

The [frozen certificate](../../../tools/ghidra/fixtures/retail-scheduler-mutation-1.27.json)
and both compressed streams retain16,748 exactly repeated ordered actor,
mutation-boundary, class, target, admission, unlink, reset, request, search and
full fine-result observations. The verifier checks every raw admission count,
head/tail/survivor list, cleared unlink links, actual popped-node charge,
request timestamp, search quota and fine result association. Source hashes pin
the public JASS producer, controller, map maker and read-only observer; their
original bytes are archived. Repeat comparison normalizes process addresses
and wall-clock timestamps while retaining simulation clocks and route words.
Replay fresh captures with:

```sh
python3 tools/frida/verify_wc3_scheduler_mutation_trace.py \
  /tmp/scheduler-mutation-c.jsonl --repeat /tmp/scheduler-mutation-d.jsonl \
  --output /tmp/scheduler-mutation.json
python3 -m unittest discover -s tests -p 'test_wc3_pathfinding_scheduler*.py'
```

The engine regression exposed a real lifecycle gap. Generic behavior
transitions call `move_cancel_displacement`, which previously unlinked only a
fine request. A mover waiting on a member coarse search could stop while its
request remained the queue head, denying subsequent movers. Changing reset
alone did not fix Stop because the Stop transition does not require a new
route activation. Retail `PathGroup_StopMembers` (`16c5d0`) commits zero member
velocity/current facing and calls policy-independent `168800`; the existing
original-code480-case oracle already covers this cancellation in all four
policies. The new public capture supplies the actual queued lifecycle witness.

Move now owns one O(1) local cancellation operation for fine and both coarse
request records, used at behavior transitions, progress reset and old-owner
cancellation. It retains independent budget work and survivor order. Removal
continues through the existing route-owned coarse cleanup. No queue scan,
additional saved state or save-layout change is introduced.

The public member-coarse regression failed11 assertions before the correction;
its fine counterpart already passed and was retained as coverage. Both now
create actual units and point orders, stop a pending head, save/load its two
survivors, deny out-of-order admission, admit the next head, reissue/requeue,
change owner and remove the request. They pass66 assertions per Classic/TFT
schema. Focused validation also covers real96-unit contention/save continuation,
responsive queues, ownership/removal, corrupted coarse saves, non-unit class15,
the captured owner window and public Stop/restart numerical continuation.
The scheduler Python checks pass16 tests, including complete frozen repeats and
rejection of missing or altered evidence.

`MapPathfinding.java` updates six existing function mappings, including the
actual public actor binding, callback-free head admission and policy-independent
Stop cancellation. The script is replayed, all six comments are read back and
the pinned Ghidra program is saved. This is the fourth implementation chunk
after the last full validation checkpoint; full validation remains on the
owner-authorized twelve-chunk cadence.

## Mover retirement cancels tasks before releasing storage

Payoff107 closes ORDER-06.5. Two complete public JASS producers create96 allied
Footmen, submit independent point Moves, then kill and remove actual queued
heads and traveling movers. The read-only observer binds public order indexes
to canonical mover/path identities before selecting pending actors52/50 and
moving actors94/95. Both45-sample captures agree on22,137 ordered scheduler,
search, task, Stop and lifecycle records, including14 retirement boundaries.
The [frozen certificate](../../../tools/ghidra/fixtures/retail-mover-retirement-1.27.json)
pins the binary, both complete streams and archived producer/observer sources.

`Unit_BeginDeathTasks` (`679bb0`) and `Unit_QueueRemovalTasks` (`694690`)
call `Unit_CancelAllOrders` (`673610`) before constructing the death/removal
tasks. The cancellation walks the user chain with balanced releases, clears
head/tail and zeroes its count; internal replacement tasks remain live. Native
`Mover_StopAndInvalidatePath` (`171340`) clears displacement, detaches the group,
sets destination `c7fa0040,c7fa0040` (-128000.5 fine), clears both route counts,
invalidates both indices and unlinks the local scheduler request. The sampled
pose remains unchanged at these public boundaries. Pending head retirement
preserves every survivor, work total, countdown and owner clock.

These are separate lifetimes:

| Boundary | Orders / displacement / member binding | Unit / mover / local path storage | Detached group / group path |
|---|---|---|---|
| KillUnit return | Retired | Live | Live until next owner visit |
| RemoveUnit return | Retired | Live until deferred removal | Live until next owner visit |
| Next removal sample | Retired | Released | Released |
| Corpse retained at tick40 | Retired | Live | Released |
| Corpse removed, sampled at tick41 | Retired | Released | Released |

Canonical lifetime queries must use the right identity. `Unit+c/10` identifies
an agent wrapper: `061320` resolves it, rejects a retired wrapper at `+20`, then
reads its payload at `+54`. Treating the wrapper as a Unit falsely reported
immediate unit destruction in an exploratory observer. Mover group `+9c/a0`
is also a slot/generation pair, not a raw pointer. Accepted captures resolve
these through `054530` with domain bounds, active slot marker and generation
checks. Failed exploratory generations are not frozen evidence.

The engine regression exposed immediate-removal omissions: deferred hiding
left commands, member bindings, velocity and pending work alive. General
removal now clears the order queue/head and retires the old behavior callback;
Move handles `A_UNIT_REMOVING` and
retires its physical task synchronously. Leaving Move invalidates route counts,
indices and cached validity while retaining allocated route storage. Death
already uses the same lifecycle transition; the new checks catch its stale
route state. Deferred free still owns actual storage release.

The existing retained-route lifecycle regression caught a scheduled walking
callback executing between semantic removal and deferred free with its cleared
goal. Removal now unregisters that callback through the ordinary behavior
membership writer. Existing occupied-goal and group-ownership tests also now
assert the corrected boundaries: final pose retains the partial endpoint while
active routes are invalid, and replacement leaves an empty group until its
owner visit rather than freeing it immediately.

Ordinary empty physical groups previously freed immediately, whereas native
`16c150` prunes and releases them at the next owner visit. They now retain
their allocation and any group-owned coarse request until that boundary, just
as shared groups already did. Save validation accepts this valid empty owner;
no representation or save-version change is needed. The added round trip
reproduced12 validator failures before the fix. The earlier immediate-removal
and route-state regressions also failed before their owning corrections.

Native group storage still contains its one stale member row at public return,
even though the mover's group identity is invalid. The engine's live-binding
array compacts immediately, but must retain the physical owner's lifetime.
This leaf verifies that lifetime, not an identical private array layout; the
broader member-storage replacement remains GROUP-04.6.

The production-path matrix covers KillUnit/RemoveUnit, independent/group orders,
fine-pending/member-coarse-pending/traveling states, queued successor cleanup,
FIFO survivors, corpse storage, deferred release and saves before empty-owner
retirement. It runs in Classic and TFT. Replay evidence checks with:

```sh
python3 tools/frida/verify_wc3_mover_retirement_trace.py \
  /tmp/mover-retirement-c.jsonl --repeat /tmp/mover-retirement-d.jsonl \
  --output /tmp/mover-retirement.json
python3 -m unittest discover -s tests -p 'test_wc3_pathfinding_mover_retirement.py'
```

The shared scheduler-operation verifier retains the earlier mutation oracle's
exact digest and independently checks searches, charging, raw queue counts,
timestamps and reset cadence in both scenes. Negative retirement tests reject
missing completion, source mismatch, changed orders/velocity/routes/pose,
premature storage release, leaked owners, corpse/group conflation and changed
survivor lists. `MapPathfinding.java` adds673610 and updates eight existing
mappings; all nine comments are replayed, read back and saved in Ghidra.
Callback-driven traversal mutation, interruption during completion and the
broader persistent retail group replacement remain their existing open leaves.
Final focused validation passes89535 assertions in25 tests per Classic/TFT
schema, including the530-assertion retirement matrix. Broader Classic movement
and API runs cover325 and348 cases respectively; their only failures were the
three former cleanup-state assertions corrected and rechecked above. The four
retirement and16 scheduler Python checks pass. Production/test targets build
and `git diff --check` passes. This is the fifth implementation chunk since
full checkpoint102; full validation remains on the authorized twelve-chunk
cadence.

## Mixed authored ranks install before formation layout

Payoff108 closes FORM-01.2. The public `formation_ranks` map creates six
Footman clones with authored ranks `0,1,2,3,0,1`, issues a group Move, changes
member0 to rank3 through the actual Chaos ability, issues a second group Move,
and separately creates a fresh rank3 unit. Two complete40-sample producers
repeat all191 retained rank, bucket, layout, center, mean, trig and radius
observations. The observer calls no game functions and writes no game memory.
Frozen captures/certificate are `tools/ghidra/fixtures/retail-formation-ranks-1.27*`;
producer/observer/controller/map-builder sources are archived by SHA256.

`652620` captures the creation data block, then calls virtualCC with its
DWORD`+4c`. The `698750` thunk delegates to `05c3f0` and `171070`, which installs
rank bits in mover`+d8`. `16cb80` reads the low rank nibble into sixteen buckets,
preserving group membership order. The first group's selected buckets contain
`[0,4]`, `[1,5]`, `[2]`, `[3]`. After Chaos, the same mover identity has rank3:
the buckets contain `[4]`, `[1,5]`, `[2]`, `[0,3]`. The freshly created final
unit independently installs3. The absence of an explicit getter in the
common `670950` decompile is insufficient evidence that rebind preserves rank;
the public capture disproves that initial hypothesis.

Move now owns `movement.formation_rank`, installed from the constructor's
captured UnitData before initialization callbacks. Construction and real type
rebind refresh it; rebinding row pointers alone does not. Both group-routing
and selection-layout paths consume that installed field rather than looking
up live metadata. Save110 persists it and validates the sixteen-bucket range;
Save109 is rejected. The allocator-only unit fixture explicitly installs the
same profile. This is a per-instance value, not a reference into mutable rows.

The public layout also exposed a numerical error hidden by the older synthetic
oracle. DLL initializers `004180`/`004190` tail-jump into software decimal parser
`070de0` with ASCII`"5.5"` at `a91ea8`, filling `d541bc`/`d541c8` with
`40b00001`, rather than the C literal's `40b00000`. Read-only live global evidence
in `retail-formation-spacing-1.27.json` confirms both flag20 branches use that
rank gap, with exact2 width and2.5 depth padding. Intermediate public rows and
mean repeat before rotation. Using the recovered default corrects five offset
words; all twelve first-layout offset words now match retail exactly. The
older865-case supplied-scalar corpus explicitly injects IEEE5.5 into retail
memory, so its C probe now supplies that environment through
`wc3_formation_layout_gap`; production uses the DLL-initialized value.

The actual engine regression failed all twelve offset coordinates when a row
pointer changed an already installed rank. It now passes, including common
in-place type rebind, save/load and rejection of invalid installed ranks.
Focused Classic and TFT runs each pass15 tests /90,455 assertions, covering
mixed groups, ranked selection/Shift, growth/shrink/removal, ground/flight
rebind, mover retirement, save-version rejection and constructor stage/version/callback regressions. Seven Python tests cover
frozen repeats/provenance/negative evidence and both the new public and old865
layout corpora at `-O0`/`-O2`. `MapPathfinding.java` is applied, eight function
comments and four global labels read back, and `game.dll` saved in Ghidra.

This closes mixed-rank installation and bucket selection. Full post-rebind
formation-to-motion equality, spacing-policy producers, larger-group layout
and expiry remain separately tracked. No performance target is claimed here.

Reproduce with `tools/frida/make_wc3_pathfinding_map.py --scenario formation_ranks`
and `trace_wc3_pathfinding.py --profile-events --task-events --velocity-events`.
Require the complete40-sample timeline and footer; verify both original reports
with `verify_wc3_formation_rank_trace.py CAPTURE --repeat REPEAT --output REPORT`
(`--header` regenerates the C witness). Raw accepted reports are
`runtime/formation-ranks-108-d.jsonl` and `runtime/formation-ranks-108-e.jsonl`
under the pinned1.27 evidence root. Earlier108a omitted its custom ability and
was discarded;108b/c were diagnostic captures preceding the centering probes.

## Ordinary and Alt formation ticks retain the actual UI policy

Payoff109 closes FORM-03.1 by composing layout refresh, per-member destination
admission, projected held-member classification, all decisions and velocity
commits in one real, unblocked six-member owner tick. The engine previously
skipped projected classification for ordinary groups and cleared the held bit
inside each member decision. This was observable with mixed authored ranks:
ordinary members incorrectly behaved like a formation-bypass order.

The original `formation_policy` map creates six Footman clones, with installed
ranks `0/1/2/3/0/1`, collision31 and speeds150/150/150/350/350/350. A reviewed
owned Winelib helper presses Move and clicks client pixel560/230 at producer
sample30, optionally holding Alt. These are OS inputs; the Frida observer
never invokes a game function or edits game memory. Four independent captures
(a/c ordinary, b/d Alt) finish all120 public samples and a terminal footer.
Their absolute admission clocks differ. Only the first complete owner tick
is asserted to repeat: every layout offset, admitted member destination,
held/eligible flag, speed, heading, velocity and committed scalar matches.
The engine uses each capture's exact input and commit clock words, with no
subtraction, rounding or observer-provided decisions.

| Original stage | Contract recovered and consumed in Move |
|---|---|
| `6b9f70` point dispatcher | Ordinary packet8; Alt packet18 (hex). Alt additionally prepares a third canonical request. |
| `89caf0` options wrapper | Packet10 Boolean calls canonical setters `16dc50/16dcb0/16dc70`, in order. False leaves0; true installs2,6,e. |
| `16bdb0` physical admission | Copies canonical request100 flags into physical group80. |
| `16b2f0` projected classification | Project predicted positions with negative group heading; expand by fine radius+1; strict backward minimum-to-tail sort, then rank sweep. Ordinary first tick holds rows1/2/3/5. |
| `16c250` member decisions | Physical bit2 skips classification; temporary exclusion is frozen before every decision. Alt keeps all six moving; ordinary holds four. |
| `16fd90` held step | Turn toward the member destination with stop1, and unlink the mover's scheduler requests. Preserve member200000 through this decision. |
| `169b50` / `16c580` commits | Bit8 denies shared speed-cap selection. Ordinary held200000 also denies sharing in this particular tick; requested and actual speeds must be distinguished. |

`move_group_classify` uses fixed twelve-row scratch storage. Its quadratic
comparison count is bounded by the retail physical cohort limit, and it never
scans the world or allocates. Equal ranks cannot trigger either hold predicate,
so uniform cohorts take a linear rank check and skip software trig, projections
and sorting entirely. This uses a property of the recovered algorithm, not a
particular unit type. It works from installed ranks and predicted
positions, not rawcode cases or the six fixture-specific hold outcomes.
Classification now runs for every eligible ordinary physical group, with the
existing partial-route/adjusted-member cooldown gate and bit2 bypass. A held
member turns and cancels local fine/coarse requests instead of performing a
fresh route query. All member decisions precede all commits.

The universal client reports physical Alt as an optional `alt` token on
`point` and `smartpoint`, alongside the existing `queue` token. The game
interprets it only while dispatching the point callback; Move copies the
result into the accepted group's policy. Direct JASS point orders do not
synthesize this UI modifier. Save111 clears transient `menu.order_alt` and
retains the existing serialized physical flags. The ordinary2..12 same-lane
UI cohort path is covered; larger selections, air/mixed lanes and target
commands are not certified by these captures.

A fifth completed diagnostic uses Shift+Alt and two actual clicks. Its first
idle cohort has policye, its first later singleton still has e, and four later
singleton reconstructions have0. `5faaf0` constructs a fresh request without
calling `89caf0` or copying the old options. Therefore this chunk does not
blindly put Alt bits into every queued reconstruction. Queued Alt's original
request lifetime and full motion equality remain unresolved under FORM-01.3;
this diagnostic is not claimed as a complete queued replay match.

Spacing bit20 is a separate canonical policy. Its setter `16d7e0` has no
static code xrefs; scanning the pinned image also found no absolute preferred
entry pointer. This does not prove complete indirect unreachability. Neither
ordinary nor Alt inputs call it. `16dc90` writes another independent bit10,
with static callers at05a65a,05bb6a and89cd99. FORM-01.3 remains open for the
remaining policy producers; no extra TODOs were created.

Evidence is frozen in `tools/ghidra/fixtures/retail-formation-policy-1.27*`,
with five filtered stage streams, full source provenance and a Ghidra readback
of twelve function names/comments/xrefs. The saved Ghidra project and
`MapPathfinding.java` contain the same evidence. The verifier rejects missing
producer boundaries, modified source/input hashes, missing option calls,
changed owner identities and inconsistent layout/decision/commit stages.
`selected_formation_policy_matches_original_complete_owner_tick` drives real
construction, selected Move and normal ability owner updates; all456 native
scalar stage words agree across the four captures. Geometry is checked too.
`selected_point_retains_formation_toggle_policy` covers actual command dispatch,
owner updates and saving the policy. The first regression failed3 policy
assertions; the full-tick reproducer then exposed missing classification.
Client input round-trips fail before the fix and cover none/Alt/Shift/both.
Focused Classic/TFT checks run per chunk; this is not another full-repository
validation checkpoint.

Reproduce the map with `make_wc3_pathfinding_map.py --scenario formation_policy`
and use `trace_wc3_pathfinding.py --seconds 100 --samples 30000 --profile-events
--task-events --motion-events --velocity-events --x11-display :94 --continue-at
65 --continue-after-start 5 --point-click-at 30 --point-click 560 230
--point-click-sample-ticks --point-input-helper OWNED_HELPER --point-native-key`,
adding `--point-click-alt` for Alt. Map loading still requires the usual
`--data`, `--map` and `--output` arguments. The optional queued diagnostic adds
`--point-click-shift --point-click-extra 40 630 300`. The bounded source
captures are `runtime/formation-policy-109-a.jsonl` through e under the local
1.27 analysis report root. Generate the engine oracle with
`verify_wc3_formation_policy_trace.py A B C D --header OUTPUT`.


## Blocked formation slots adjust after classification

Payoff110 closes FORM-03.2 with two completed ordinary UI Move captures on a
flat map containing one blocked base cell at13/10. The six mixed-rank members,
source positions, speeds and reviewed Winelib input are unchanged from109.
The blocked cell covers only the first assigned destination; the group centre
and the other five assigned slots remain clear. The observer reads original
state and never invokes game functions or edits game memory.

Both captures finish120 public samples and a terminal footer. Each contains195
route-owner passes,954 scalar decisions and960 velocity/position/facing commits.
The first route starts with no coarse points/index-1, builds two points/index0,
and every194 later request retains that plan and the formation point. Exactly
six member-destination adjustments occur, all within the first decision phase;
none occurs during the cached visits. The complete streams are frozen, including
search and task events, rather than retaining only successful first-tick rows.

The native order is `16c250` classification, then each `16a790` decision's
`16e250` refreshed-slot adjustment, then all commits. The original first
classification holds members1/2/3/5. The blocked first slot changes from raw
fine41d31f87/41ae8e70 to25/21 and receives40000, while the four held members
remain200000. At the next cached visit, `169b00` sees the retained adjusted
flag and denies classification: `16c250` installs cooldown66 and `16c150`
decrements it after committing. The formerly held members are released.

Our engine previously adjusted every slot in `move_group_route` before
classification. That made the first visit see40000, seed the cooldown early
and release four members one visit too soon. The new production regression
fails six assertions before the fix. Move now retains previous member flags
through layout/classification and adjusts a refreshed slot at its own decision
boundary. Cached visits keep destinations, without rerunning their admission
queries. Existing bounded layout and accelerator algorithms remain the owners
of geometry and fallback; no fixture-specific destination is hardcoded.

The exact cached replay then exposes16 low-word failures: fine route direction
and blocker queries can borrow `sampled_pose` from a previous presentation
sample. Native `16a790` instead passes the current owner-clock prediction from
`05bdd0` to `16fbd0`. These queries now receive a stack-local predicted fine
source. This preserves the committed pose/time origin and avoids converting
through world coordinates. All360 compared layout/destination/flags/pending
motion/committed pose words now agree across both captures' fresh and first
cached ticks. A save after the first tick retains adjusted40000 and cooldown0;
loading reproduces the second native tick exactly. The regression uses actual
construction, selection Move and ability owner updates with supplied native
absolute clocks. It does not inject native destinations, decisions or routes.

The velocity observer's `requested[1]` is mover+c4, a signed heading delta,
whereas its `heading` is the actual commit argument. `173720` computes this
wrapped delta. The strict verifier checks all stored deltas and shared speed
caps separately from pending member headings. All954 scalar decisions and960
commits per capture additionally match a freshly compiled production C scalar
probe. This arithmetic replay does not certify the entire engine journey;
the engine comparison is explicitly bounded to the initial two owner ticks.
The first two normalized ticks repeat after excluding absolute clock words;
full later clock/trajectory equality is not claimed.

Artifacts: `retail-formation-blocked-1.27*.jsonl.gz`, its JSON certificate,
archived producer/observer sources and `retail-formation-blocked-ghidra-1.27.json`
in `tools/ghidra/fixtures`. The saved Ghidra readback contains eight function
names/comments/xrefs and36 existing path/group field mappings, with
`unsaved=false`. `MapPathfinding.java` retains the same ordering, cache and
current-source contracts, including the recovered wrapped-delta role.
The strict checker rejects lost stages, changed source hashes, changed cached
routes, early adjustment, different fallback, missing decisions, changed caps,
heading deltas and late commits. The generated native engine header is checked
against both validated traces.

Reproduce with `make_wc3_pathfinding_map.py --scenario formation_blocked` and
109's ordinary bounded input command, pointing at the generated blocked map.
Raw captures are `runtime/formation-blocked-110-a.jsonl` and b under the pinned
1.27 report root. Generate the header using
`verify_wc3_formation_blocked_trace.py A B --header OUTPUT`.

Validation: the Classic and TFT movement suites each pass331 tests and
4,458,567 assertions; the formation Python checks pass15 tests. Production and
test targets build, and Ghidra is saved. This is focused movement validation,
not the full repository batch checkpoint. No save format or network layout
changes, extra task IDs or closure of wider regroup/crowd/moving-target cases
are claimed.

## Public moving formations retain a twelve-member request boundary

Payoff111 closes FORM-02.3 and fixes destination ownership during group admission.
`G_IssueGroupPointOrder` borrowed its caller's point through all member orders.
When that point aliases a moving entity's position, stopping the first member
commits its predicted position and changes the point seen by later members.
The new regression reproduces21 incorrect destination words in eleven- and
twelve-member Move requests before the fix. Generic dispatch now takes a
bounded stack copy of the request and its point before calling an ability or
individual order owner. There is no allocation, deferred admission, ID reservation
or change to candidate order. The same entry-time point reaches all retained
members, including when a nested order changes the caller's storage.

The native `23acd0` copies X/Y into its canonical request at23ad5c..23ad67 and
into the stack context at23adb2..23adc3. Its two `22ed70` calls explicitly push
limit12: all `233da0` attachments precede all `234160` admissions. The canonical
request's candidate and readiness arrays also have12 entries. Member backing
storage itself is dynamic: `16c060` appends44-byte rows and `16da80` fills their
11-word templates. Growing that storage does not establish that the formation
layout's12-slot temporary buckets or count-indexed capacity tables support
larger cohorts. The public caller supplies the required bound.

Two completed read-only captures use public individually issued Move orders,
then public GroupPointOrder at fixed timer ticks10/20/30/40 with collection
sizes11/12/13/25. They attach and admit11/12/12/12 members, respectively. The
prefix stays in insertion order; candidates beyond12 are not substituted or
ordered in a second cohort. Before each public group call,11/4/3/3 admitted
members respectively have nonzero committed velocity. Admission stops and
commits those movers before layout. Both runs finish45 samples and a terminal
footer without observer errors or stream caps. Each full stream has37,718 rows.
All94 formation offset words, the supplied layout poses, and installed ranks
repeat exactly. A freshly compiled production scalar probe matches all188
words across the two captures.

The engine regression drives the registered JASS GroupPointOrder producer for
all four collection sizes, verifies admitted prefix identity and point words,
and retains the existing order, velocity and clock of members beyond12. It
then executes production group routing/layout and compares the native heading
and94 offsets. Its explicit input boundary is the native admitted pose at
layout entry. Moving velocity is supplied with zero elapsed time to test
stopping at that boundary; earlier engine/native trajectories are not claimed.
The separate mutable-point regression exercises a nonzero elapsed interval.
It also checks generic Patrol dispatch without assuming that Patrol commits
Move's old velocity at the same stage. This scope does not close formation
refresh triggers, larger AI roster policies or full moving-target journeys.
Selections above12 remain the existing engine extension; the retail public
JASS limit is not an instruction to discard those user selection orders.

Reproduce the bounded native scene with:

```sh
python3 tools/frida/make_wc3_pathfinding_map.py --base BASE.w3m \
  --scenario formation_boundary --output DATA/Maps/PathingRE-FormationBoundary111.w3m
DISPLAY=:94 WINEPREFIX=OWNED_PREFIX FRIDA_PYTHON tools/frida/trace_wc3_pathfinding.py \
  --data DATA --map 'Maps\PathingRE-FormationBoundary111.w3m' \
  --seconds 100 --samples 30000 --profile-events --task-events \
  --motion-events --velocity-events --x11-display :94 \
  --continue-at 65 --continue-after-start 5 --output CAPTURE.jsonl
```

`BASE.w3m` is the
existing Human02Interlude base with the flat64-by64 fine-grid fixture; DATA and
OWNED_PREFIX designate the isolated retail1.27 installation and Wine prefix.
Frozen full streams, checksums, original sources and the saved ten-function
Ghidra readback are `tools/ghidra/fixtures/retail-formation-boundary-1.27*`.
`MapPathfinding.java` includes the append/fill names and copied-point/limit
contract. Readback has `unsaved=false`; no prototype was guessed from a
misaligned decompiler parameter list.

Validation passes10 focused engine tests and929 assertions per Classic/TFT
schema, production/test builds,19 formation Python checks and the engine
boundary audit. The Python verifiers include changed-source, missing-boundary,
nonmoving-input, membership, point, pose, offset and truncation negatives.
This is a focused chunk, not the full repository checkpoint. No saved or
networked layout changes are required.


### Fixed-tick formation mutation and regroup preserve cached state

Payoff112 uses the public producer `wc3_formation_refresh_probe.j` on a flat
64×64 fine-grid map with a wall at x16/y0..23. Six Footmen have ranks0/1/2
and radius31. At script tick10 (100-ms timer) they Move to1728/512; tick20
adds Chaos `AF03`, resizing the first member to63 and rank3; tick30 removes
member1; tick50 gives the surviving collection a new point1536/1792. Two
completed read-only Frida captures repeat the full sequence and all34 layout
offset words. The certificate, full compressed streams, frozen sources and
saved Ghidra readback are `tools/ghidra/fixtures/retail-formation-refresh-1.27*`.

| Observation | Original owner counter | Effect |
|---|---:|---|
| First route |1058| Six-member layout; coarse count10/index7 |
| Size change at tick20 |1092| Same mover receives a private one-member owner; old physical group retains five members and its cached offsets/destinations/coarse route |
| Removal at tick30 |1125| Old group retains four members; no layout rebuild |
| New goal at tick50 |1191| New five-member physical owner, coarse count5/index2, fresh layout |
| Regroup advance |1456| Age265, completion0; index2→0, counter reset and a second five-member layout |

The last row is **not a timeout witness**: one arrived member plus nearby
remaining members yields status0. Timing is simulation state, not Frida wall
milliseconds. Native owner visits occur every30ms; the recorded clock words
are retained in the certificate. The first mover's morph route is distinct
from the surviving collection's unchanged route. Removing a member does not
recenter those survivors immediately. A fresh point order is a new producer;
it is not an in-place edit of the existing physical owner.

`16d660 PathGroup_InitializeMemberOffsets` clears flags28 and initializes
only offsets c/10 during creation, before mover resolution. It must not be
confused with `16d6a0 PathGroup_ResetMemberRoutes`: regroup reset clears each
member's fine/adaptive buffers and retry/delay, plus member flags, while
retaining offsets and destinations until `16d990` computes the next layout.
`16d8e0 PathGroup_SetHeadingFromPointDelta` sets10000 even for zero displacement,
but preserves the old heading in that case. These helpers and their xrefs
are saved in Ghidra and reproduced by `MapPathfinding.java`.

The complete original-x86 oracle exports648 `16c4f0` boundary cases,48 refresh
cases,1792 regroup-status cases and32 held member decisions with both prior
arrival-bit states. No original instructions are replaced. Timeout limits
are99 for ordinary groups,198 for policy100 after the initial stage and396
for policy100 with20000. The comparison is **strictly greater**; the counter
increments only on a failed regroup with an arrived member. Starting at0,
a continually eligible wait advances on visit101/200/398, respectively
(3.03/6.00/11.94 simulation seconds at30ms per visit). This is not an absolute
wall-clock deadline, and lack of arrived members does not accumulate this wait.

The engine had two concrete decision bugs: arrival10000 accumulated even
when a cached destination ceased to be reached, and held-member early arrival
left queued fine/coarse work linked. Move now replaces the bit per decision
and unlinks held requests on both turn and arrival. Nine failing assertions
reproduced premature coarse advancement, stale queue heads and the resulting
wait-state change before the fix. Production regressions exercise public
axis displacement, pending queues and all648 native regroup boundaries. The
same public mutation script tests cached layout/route retention, new-owner
retargeting and save/load between removal and retargeting.

This closes FORM-04.1's fixed-tick point-goal, membership and size mutation
contract. It does not establish every moving-widget target refresh producer,
route failure or portal transition; those remain in TARGET/GROUP and FORM-04.2.
The bounded numerical comparisons preserve the current recorded domains;
they do not claim complete retail pathfinding or a new performance result.

### Cached single fine points bypass refill admission

Payoff113 corrects the engine adapter's `count < 2` cache rejection. Native
`167ce0 Path_GetFineWaypoint` tests unsigned `index < count`; count1/index0
is valid. `167070 Path_CheckFineWaypointProgress` owns consumption through
its squared-distance gate. A far cached point can therefore remain usable
when the player's fine-search bucket cannot admit new work. Rejecting it
unnecessarily reenters refill/admission and can stop the mover.

The new regression calls production `G_AdvanceUnitMoveFineRouteStatus` with
one/two/five-point cached routes across all four footprint classes. Twelve
of180 assertions fail before the fix, all on count1. The adapter now uses
nonzero count plus unsigned index validity, retains the selected point and
lets the existing distance check retire the endpoint. This change neither
allocates another route nor modifies admission limits or saved layouts.

The complete original `165ae0 Path_Advance` oracle runs576 combinations:
12 cache/index states × four footprint classes × enabled0/1 × disabled0/1
× delay0/1/4. It retains a one-point adaptive route at index0 and supplies a
fine bucket with1101 work, preventing new fine admission. The original
context builder, blocker collector and request registry execute unchanged.

| First applicable condition | Result | Delay | Output / route |
| --- | ---: | --- | --- |
| Disabled100000 |100000| Frozen | Held goal; cache unchanged |
| Nonzero delay94 |1| Decrement once | Held goal; cache unchanged |
| Unsigned fine index below count |0|0| Selected cached point; cache unchanged |
| Empty/exhausted/negative index |2|0| Admission denied; held goal and cache unchanged |

Every row leaves fine work1101 unchanged. Enabling adaptive routing does not
invalidate an already retained adaptive point. Disabled100000 is an internal
path gate; public `SetUnitPathing(false)` is a different operation. Empty and
negative indices are unsigned-invalid even when their signed values appear
below count. Alternate index initialization remains documented at168870;
this matrix does not manufacture evidence of its public producers.

Two complete new read-only Frida runs of `fine_results` reproduce292 requests
and20 ordered terrain edits each, exactly matching the older scene91 words.
Both include all four zero-work same-cell one-point controls, blocked sources
and moving terrain across radius.25/.75/1.25/1.75. Production C reproduces all
584 search results, work counts, node counts and route words. These runs
establish **public single-point creation**, not the far-source one-point
lifetime supplied to the oracle. ROUTE-04.1 remains open for that reachability
and its other producer combinations; no new TODO is added. Terminal,
queued/perimeter, yielding and warp lifetimes retain their existing tasks.

Frozen original tables, complete compressed streams and the four-function
Ghidra decompilation/comment/xref readback are
`tools/ghidra/fixtures/retail-cached-fine-route-1.27*`. Captured source generations
are preserved under `fixtures/sources/` by SHA256. `MapPathfinding.java` records
the exact unsigned cache rule and full-caller gate precedence; game.dll was
saved after mapping. The first live attempt ended after only four of12 cases
because its loading-screen dismissal was premature; it is excluded. Complete
runs use the same isolated Wine prefix/display with320/240-second limits and
loading-screen continue at150/100 seconds, respectively.

Reproduce the focused accepted corpus with:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/run_wc3_pathfinding_corpus.py \
  --binary DATA/game.dll --archive /GitHub/wc3-analysis --output FRESH_DIRECTORY \
  --only oracle-cached-fine-route --only live-cached-fine-route-controls-261006
```

Classic and TFT each pass89 pathfinding tests/334701 assertions plus the
public retained-route lifecycle test/32 assertions. Python tests replay the
new compressed streams against production C at O0/O2 and reject provenance,
truncation and word-history changes. This is the first implementation chunk
after the successful merge checkpoint; validation is focused, without a new
full-suite or performance claim.

### Public single-point lifetimes preserve cached motion

Payoff114 authenticates the full public lifetime behind payoff113's cache fix.
The first four cases of the completed scene91 Frida captures already contain
this evidence: a single-unit point order from world257/289 to287/319, for each
of collision radius8/24/40/56. Fine source8.03125/9.03125 and goal8.96875/9.96875
are in the same cell, but their distance is greater than.49. Thus this is an
active one-point route, rather than an immediately consumed endpoint.

Each lifetime makes two one-point coarse requests followed by exactly one
fine request. Fine setup emits count1/index0 with no expansion or charged
work. Seven subsequent arrival evaluations and velocity commits complete
the move without a second fine request. The last commit has zero velocity;
the unit stops at the original range boundary, without snapping to the goal.
Two completed captures repeat every route, arrival and clock/pose/velocity/
heading word. Their full twelve-case streams remain pinned and checked;
this supplement certifies the first four movement lifetimes only.

The production constructor had an independent `count < 2 && !fine_target`
rejection. It incorrectly made a valid route depend on whether the caller
already supplied native destination coordinates. Sixteen assertions fail
when production local routing derives that destination itself. Removing the
restriction gives prepared and derived destinations the same count1 contract;
the existing complete/partial rules, placement, admission and index selection
still apply. Together with payoff113's cache fix, count1 now survives both
creation and advancement.

The full game regression runs the same four type profiles and periodic JASS
producer from a zero primary clock. It reproduces28 original commits and
continues from saves at1030,1090,9030,17030 and25030 simulation milliseconds.
Each saved active route retains its point, count1/index0, partial flag and valid
waypoint. The five suffixes account for91 additional commits. All time,
position, velocity and heading words match the uninterrupted native control.
The engine fixture ends after case3, before the separate blocked-source
experiments; its script includes the stock positive-integer modulo calculation
and a test `main` calling the unchanged initialization function.

`retail_same_cell_motion_114.h` contains the producer and engine words;
`retail-same-cell-motion-1.27.json` freezes the complete public route/arrival/
motion supplement. The new corpus entry `live-same-cell-motion-captures-261006`
checks that supplement against both existing full raw captures, plus all584
production search results in the full scene. Strict negative checks reject
extra refills, missing commits, changed indices, arrival inputs, movement words
and altered producer/header literals. The compressed streams and content-
addressed sources remain the accepted payoff113 artifacts; no capture is
relabeled as a new run.

Saved Ghidra annotations/readback cover165ae0,166e90,167ce0,167070 and168870.
The latter's mode0 formula is `count-1` only when count equals1, otherwise
`count-2`; mode1 uses `count-1`. Empty fine/coarse buffers therefore initialize
to unsigned-2/-1, respectively. This corrects the old comment's overbroad
`count > 1` shorthand. Clear-obstruction fine setup selects0 directly.
`MapPathfinding.java` records the public cache producer and the fresh-route fix;
`retail-same-cell-motion-1.27-static.json` reads back `unsaved=false` and xrefs.

The far-source single-point producer is now certified; every other public
cache/alternate-index combination remains ROUTE-04.1. Denied-bucket composition
still comes from the complete original controlled oracle, rather than this
uncrowded public scene. Queue/perimeter/yield/warp producers remain their
existing tasks. No new TODO, saved layout or performance result is claimed.

Focused validation passes90 pathfinding tests/334773 assertions per Classic/
TFT schema, all338 movement tests in both schemas (over4.47 million assertions
per run), and the final public save/motion regression's1280 assertions in
both schemas. Classic's broader run preceded the additional direct restored-
route assertions; its final focused regression includes them. The25 Python
checks, two selected fresh corpus entries, engine boundary audit and WC3/SC2
production plus WC3 test builds pass. This is implementation commit2 since
the full merge checkpoint; the full repository suite is not repeated here.

### Exact public-game differential adapters

Payoff115 closes E2E-08.1/08.2 with runnable adapters and an encoded baseline,
using the existing accepted payoff113 Frida captures B/C. It does not relabel
those captures as new runs or close further movement-fidelity tasks.
`tools/wc3_pathfinding_differential.py` compares their first four same-cell
lifetimes with a fresh journal from the actual game module's
`public_same_cell_routes_retain_single_points_and_saved_motion` regression.
The first exporter regression failed because the game produced no journal;
the new test-only observer reads production state after movement commits.
Expected fixture rows are still regression assertions, never exporter inputs.

The version1 input contract is `retail-differential-inputs-115.json`. Both
producers use world bounds0..2048, fine cells32, the64×64 ground grid, the same
blocked columns24/25 through row47, radius8/24/40/56, requested speed150,
turn.6 and authored propagation window60. The effective turn/window words and
fine collision radii are independently checked against live observations and
engine instance reads. Native WPMc6 and engine ground bit02 encode the relevant
ground policy differently; these are declared translations, not mask equality.
The native clones derive from hfoo and engine movement fixtures from hRTE;
this contract specifies their movement inputs, not every unit statistic.

The periodic producer creates an actor at272/304 facing90, then on tick10 of
each80-tick lifetime sets world257/289 and issues point Move287/319. Its100-ms
JASS timer runs over5-ms primary frames and30-ms owner updates from primary
clock0/epoch0/span300. Comparison ends before case4 at32000ms. The native
script continues through12 cases; the engine fixture ends after four. The
common prefix is identical; the blocked-source suffix is outside this report.
Existing script/header tests verify the stock modulo helper and test-entry
transformation, rather than silently changing the producer.

Reports retain all28 ordered `velocity-commit` events, including four stopped
commits. Identity `[producer lifetime, actor ordinal]` distinguishes successive
births even when addresses or edict slots are reused. Each event contains:

| Field | Exact representation |
| --- | --- |
| `sequence` | Consecutive encounter ordinal; no sorting or realignment |
| `actor` | Lifetime0..3 and ordinal0 |
| `clock` | Binary32 time, unsigned epoch, binary32 span |
| `position` | Two committed fine-coordinate binary32 words |
| `velocity` | Two fine-velocity binary32 words |
| `heading` | Binary32 radians |

Native sampling is after16fe20 returns. Ghidra confirms15f7e0 first integrates
old velocity through1603d0, then1606e0 changes velocity and may notify. The
zero-speed branch finishes its heading assignment before the return sample.
Engine observation uses the existing Move test callback after its committed
state. World velocity is converted with the production software divide32,
not host float division. Saved readback and decompilation are in
`retail-differential-adapter-115-static.json`; `MapPathfinding.java` records the
boundary and the independently verified three-word clocks.

Simulation tolerance is zero: exact unsigned words, types, inputs, identities,
event kind, ordering and cardinality. Signed zero and a one-bit difference
fail. Missing or extra events fail; booleans and JSON floats are not uint32.
No resampling, clipping, epsilon, address sorting or missing-event repair is
permitted. Failure reports include a JSON pointer, expected/actual words in
hexadecimal and decimal, and the event identity/ordinal. Display is capped at32
differences while the total failure count remains exact. Presentation fields
are limited to `wall_ms`, `swap_intervals_ms` and `image_sha256`, and are explicitly
uncompared. Provenance can differ across platforms and builds. Neither exempt
channel establishes visual parity; simulation fields cannot be inserted into
presentation to extend its allowed schema. This baseline's declared output
surface is committed motion, not full task, route, RNG or fog state.

Run focused comparisons after building the test game and fixture assets:

```sh
make -j6 openwarcraft3-tests
python3 tools/wc3_pathfinding_differential.py run --output /tmp/wc3-diff-classic-115
python3 tools/wc3_pathfinding_differential.py run --tft --output /tmp/wc3-diff-tft-115
```

Each output directory must be new. It contains the actual engine JSONL and
log, both normalized retail reports, the engine report and `comparison.json`.
The command rejects nonzero engine exits, failed assertion totals even when
the engine exits zero, missing/partial journals and mismatches. It hashes the
binary, loaded test-game library, Move/test sources and fixture MPQs for
provenance. Save files use the private journal path, avoiding collisions with
other adapter runs. `retail`/`engine` subcommands normalize individual streams;
`compare expected.json actual.json output.json` returns nonzero on differences.
`retail-differential-baseline-115.json` encodes the accepted B report.

Classic and TFT journals independently match both accepted captures:28 commits
each, plus the existing five save continuations/91 suffix commits checked by
the full game regression. Strict mutation tests flip every exported simulation
word, change identities/order/input clocks, remove or add events, change
signed zero and types, and introduce forbidden presentation fields. Negative
runner tests reject a zero-exit failing test, incomplete journals, changed
profiles and stale output directories. Focused differential, fine-results and
corpus tests pass; the full repository checkpoint remains on the agreed
roughly-dozen-chunk cadence.


## Authored timer timeout survives scheduling and pause

Payoff116 ports a verified timer input contract directly into gameplay.
`TimerGetTimeout` now returns the authored scalar retained at `TimerStart`,
rather than converting the integer scheduling duration back to seconds. The
existing saved `scalar_timeout` owns this value; no save-layout change is needed.
C integer timer producers refresh it from their own supplied duration before
public scalar producers replace it with the exact original word. Reads remain
constant time and require no conversion or allocation.

Retail public216c40 resolves the timer then calls233d50, which directly copies
script timer+48 into its scalar output. A missing handle returns zero. Its
result does not depend on the current request, effective scheduling interval,
expiration or paused remainder. Public elapsed216be0 and remaining216c10 use
232500 and233190 instead; these are distinct contracts, not aliases for timeout.
All public wrappers return raw words in EAX under cdecl/plainRET. The three
core functions take the timer in ECX, scalar output at stack4, return that
pointer in EAX and execute RET4.

The genuine public producer `wc3_timer_inputs_probe.j`, scene116, runs13
one-shot inputs on the flat64×64 map. It reads all three getters immediately
and on40 subsequent100-ms ticks, pauses all13 timers at tick11 and resumes them
at tick22. Short, zero, negative, adjacent and large inputs are retained:

| Source literal | Compiled authored word |
| --- | --- |
| `-0.1` | `bdccccce` |
| `-0.001` | `ba83126f` |
| `0.0` | `00000000` |
| `0.0001` | `38d1b718` |
| `0.004999999` | `3ba3d708` |
| `0.005` | `3ba3d70a` |
| `0.005000001` | `3ba3d70d` |
| `0.099999994` | `3dcccccc` |
| `0.1` | `3dccccce` |
| `0.100000001` | `3dcccccd` |
| `299.99997` | `4395ffff` |
| `300.00003` | `43960000` |
| `1000000.25` | `49742404` |

Use the actual compiled words, not a host decimal parser's interpretation.
The preserved timeout is also consumed by a separate one-shot callback:
`SetUnitMoveSpeed(actor, TimerGetTimeout(GetExpiredTimer()) * 2000.0)`, followed
by public Move from272/304 to640/512. That creates an observable numerical
movement regression rather than a getter-only comparison.

Two complete read-only Frida streams each contain5145 raw records,1600 public
getter calls,2037 scalar getter calls and70 committed movement records. Their
normalized4831-event histories repeat exactly: callback/marker ordering,
encounter identities, authored/effective values, embedded method RVAs, all
three primary clock words, fine pose/velocity and heading. The raw B/C streams
are frozen as `retail-timer-inputs-1.27-{b,c}.jsonl.gz`; the fixture pins producer,
map and observer hashes, and content-addressed source archives preserve the
captured generations. Capture A used a constant movement speed and is retained
locally as exploratory evidence, outside this movement contract.

`verify_wc3_timer_trace.py` rejects changed raw hashes/provenance, incomplete
or duplicate footers, missing terminal markers, errors, missing/extra/reordered
events and altered scalar words. uint32 validation rejects JSON floats,
booleans, negative values and overflow. Pointer/handle values normalize by
encounter, never by address sorting. Non-timer/non-commit diagnostics are outside
the normalized contract; no whole simulation claim is implied.

A first public scalar regression fails against the old engine getter. The
full gameplay regression also fails when only the old getter is restored:
19 assertions fail by the third observed commit. With the fix, actual normal
5-ms server frames reproduce every one of the70 native movement records. Four
saves at80/450/1100/2200 ms resume165 further exact suffix commits. The script
also records41×13 timeout values into an ordinary JASS hashtable; all533 words
remain exact after each continuation. Its test-entry/unit fixture/SaveReal
transformations and every C motion literal are checked against the native
producer and raw fixture. Classic and TFT each pass13030 assertions in this
regression, plus the focused native timeout, integer-restart, queued-pause,
callback-restart and timer/dialog save tests. These paths stay synchronous and
use the existing deterministic timer/Move owners.

The original-code oracle executes whole233d50/232500/233190, including original
virtual running-clock methods, on243 supplied states (729 getter calls).
It verifies output words, preserved registers, RET4 cleanup, output canaries
and unchanged timer/request/clock backing. Runtime scalar initialization uses
original registered startup. Independent no-request, canceled and running
states are kept separate. O0/O2 C ABI checks compare the software subtraction
helper only; they do not certify complete engine elapsed/remaining getters.

Ghidra now retains `WC3EventClockControlPrefix` and
`WC3ScriptTimerScalarPrefix`, eleven explicit getter prototypes, descriptive
function names, comments and xrefs. Replayed `MapPathfindingTypes.java` and
`MapPathfinding.java` mappings are saved; fresh decompilation and readback with
`unsaved=false` are frozen in `retail-timer-getters-116-static.json`. Unknown
bytes and complete object size remain unspecified.

This evidence also identifies the remaining work in NUM-02.9.2/02.10/02.11.
An uncanceled request uses its event clock's virtual remaining/elapsed getters;
otherwise remaining copies cached+50 and elapsed subtracts it from authored+48.
Effective duration can include unsigned16-bit full-span counts plus residual,
and the minimum scheduling interval is a distinct word `38d1b717`. The large
input `49742404` initially returns remaining `49742403`. Running elapsed uses
remaining and effective duration, including both software subtractions at zero.
Pause stores the current remaining without changing the authored timeout;
resume can restart an expired timer's effective period while preserving that
original timeout. Due callbacks observe temporarily installed timer deadlines,
which differ from the primary clock read by the movement observer. Host elapsed
milliseconds are not a valid replacement for these rules. Those engine getter,
long/epoch and heap mutation ports remain open; the new captures do not close
this broader checkbox.

## Counted timer requests use their own scalar clock

Payoff117 closes NUM-02.9.2 and NUM-02.11. The pending getter/epoch scope above
is superseded by a production implementation and complete public trajectories.
NUM-02.10 still owns general shared-deadline callback mutation and catch-up.

Original registered startup002170 initializes `Timer_SegmentQuantum` atd3c894
to120 (`42f00000`), independently of the primary clock's300-second epoch.
EventClock_StartCountedRequest0602a0 converts the timeout through the original
wrapped signed-integer helper, divides by120 and stores the low16-bit count.
Original modulo supplies residual18. Counted requests initially schedule120;
a zero-count request uses its residual clamped to minimum `38d1b717`.
Registration increments clock50 and stores the unsigned sequence in request14.
The raw deadline is software-add(current timer time, effective period); creation
does not normalize it across an epoch prematurely.

Remaining composes `(remaining_segments-1)*120`, the current raw request
remaining, then the residual, in the original software operation order.
Running elapsed subtracts that result from the effective counted duration;
expired/paused elapsed instead subtracts frozen remaining50 from authored48.
This explains both the one-bit loss in a tiny effective period and the large
input `49742404` initially returning remaining `49742403`. Pause stores the
actual getter word before cancellation. Resume rebuilds an effective request
from that frozen word while retaining the authored timeout and callback.

Original0522e0 temporarily installs each due request's time during dispatch.
Public native calls made inside that callback therefore use its borrowed timer
clock, including nested synchronous JASS calls and timer-expire conditions.
Yield releases that borrowed context. The engine now carries this explicitly
rather than reading host milliseconds or the primary movement publication.
At a wrap,054050 drains the old span first;052170 software-subtracts the span
from every queued raw deadline and increments the epoch, then dispatch resumes
in the new domain. A paused remainder does not age or participate in rebasing.

Two complete read-only native117 captures each contain13,692 public getters,
17,328 original scalar getters and320 actual movement commits. Their40,551
normalized events agree, including the embedded timer clock, control flags,
counts/residual, request deadline/period/serial and original method RVAs.
The public scene pauses all13 inputs at11/290 seconds and resumes at22/305.
At299 seconds TimerGetElapsed sets Move speed; at305 TimerGetRemaining sets
another Move speed. Actual movement crosses the natural300-second epoch.
No observer writes or code substitutions produce these values.

The full game regression runs ordinary5-ms server frames. An ordinary JASS
hashtable exports all351×13×3 getter words; all match the native producer.
Every320 committed fine pose/velocity/heading words matches too. Saves at
11,000/22,500/299,500/300,500/305,500 ms replay1,113 exact suffix commits.
Classic and TFT each pass342,896 assertions after the explicit epoch check.
The shorter116 scene now checks all three getters as well:34,350 assertions,
70 native movement records and165 saved suffix records. Before the getter and
clock rewrite, its added elapsed/remaining checks failed2,775 assertions.

The additional original-code oracle runs the registered120 initializer from
poisoned storage, whole0602a0 construction and virtual core getters for all13
actual compiled public inputs.208 whole original heap insert/pop entries verify
deadline order and unsigned sequence ties;26 whole052170 deadline rebase words
match original software subtraction. Backing/free-list storage is explicitly
supplied, without replacing any executed code or claiming public callback
reachability for those controlled calls.

The engine uses an indexed scalar deadline/sequence heap: O(log n) admission,
restart and cancellation; O(1) next due lookup. C millisecond producers retain
their existing ascending-slot observations in a sparse two-level bitset, whose
live iteration observes newly admitted later slots. Empty public-timer ticks
therefore no longer scan every allocated timer. Epoch rebasing still performs
the genuine O(n) queued-deadline adjustment at the300-second boundary.
Save113 persists logical counted requests, registration sequence, publication
clocks and borrowed callback contexts (JASS snapshot9). Queue storage is derived
and rebuilt on load; prior formats are rejected. This is a numerical/scheduling
improvement, not a claim that the remaining global frame-budget target is met.

Ghidra mappings now include11 further constructor/control/heap/rebase functions,
explicit operand-backed prototypes, request serial/data and control receiver/
flags fields, the heap prefix and the registered quantum global. The saved
readback has `unsaved=false` in `retail-timer-epoch-117-static.json`; function
names, comments, xrefs and layouts are reproducible through MapPathfinding.java
and MapPathfindingTypes.java. Frozen raw captures, sources and scenario-derived
engine literals are pinned by corpus checks and mutation regressions.

## Timer callback mutations preserve heap order and deferred release

Payoff118 closes NUM-02.10 and the encompassing NUM-02.9 timer producer.
The original0522e0 dispatcher enters0542d0, which invokes the request receiver
before rearming the same uncanceled periodic request through053630. Its
registration serial survives ordinary periodic rearm. Cancellation, explicit
restart and resumed authored follow-up have distinct request lifetimes.

ScriptTimer240350 broadcasts expiry conditions/actions before the direct handler.
Pausing from an expiry condition does not suppress that current direct call.
Resume's80272 event enters2403f0 afterward: it starts the CURRENT authored timeout,
periodicity and callback, even if the first resumed callback paused or restarted
itself. Segmented requests retain that resume phase until their actual expiry.
The engine executes this control flow instead of approximating it with elapsed
milliseconds or making periodicity snapshots decide the follow-up callback.
Queued C producers still reject actions from canceled generations.

JassTimer1fd1d0 invokes the agent's destroy virtual5c (0557b0). Public handle
lookup is withdrawn immediately; the receiver survives until its separate
primary-clock release request commits through04c2c0. This permits outstanding
zero-period scalar catch-up in the current timer quantum, with GetExpiredTimer
and public getters already returning null/zero. Deferred release is drained
after the full scalar quantum, including before-owner and after-owner work;
internal lifecycle destruction still cancels storage immediately. The engine
uses an intrusive pending list, O(number released), rather than rescanning all
allocated timer slots.

Equal raw deadlines use the unsigned registration sequence shared by public
timers and the path owner. Initial map timers precede owner registration; later
restarts can follow it. At the producer's exact.15 deadline (`3e19999a`), its
Stop/repoint callback precedes movement. Always forcing the owner first changed
four movement commits despite otherwise identical timers. The indexed heap
retains O(log n) admission/cancellation and O(1) next-deadline lookup.

The unmodified original-code oracle executes the whole dispatch/rearm/cancel
graph for11 controlled cases:164 callbacks and7 canceled requests, including
zero, negative, subquantum, periodic/one-shot and shared-deadline mutation.
Receiver inputs and backing storage are explicit controls; no executed routine
is replaced. Authored JASS effects are independently covered by two complete
read-only native118 scenes. Their2,986 normalized events agree:348 callback
records,1,044 public getters,675 scalar getter calls and175 actual movement
commits per scene. The ordinary JASS producer restarts, pauses, resumes and
destroys timers while also issuing real Move/Stop orders and changing speed
from public getter results.

Two additional25-second read-only observer windows repeat18 release/owner
events, including four release queues/commits and six owner rearms each. Their
observer footer is complete; their full producer lifetime is deliberately not
claimed. The separate135-second scenes provide the completed producer proof.
Frozen raw streams, exact observer/map sources, identity joins and original
request/clock words are pinned in the timer-mutation/release fixtures.

The engine test failed305 assertions before these changes. It now matches all
348 exported callback/getter rows and175 motion records; saves at5,95,145,255
and505 ms replay856 exact suffix commits. Classic and TFT each pass52,094
assertions. A separate test pauses from an expiry condition and saves a timer
after public retirement but before storage release. The earlier116/117 getter,
shared-pair, periodic spawn and region-callback movement regressions also pass.
Save114 stores resume phase, logical retirement flags and owner serial; pending
links/heap indexes are rebuilt. JASS snapshot9 is unchanged.

Ghidra saves the five new dispatcher/callback/destroy mappings, verified
prototypes, callback-holder/root-vtable layouts, comments and xrefs through
MapPathfinding.java and MapPathfindingTypes.java. The frozen nine-function
readback is `retail-timer-mutation-118-static.json`, with `unsaved=false`.
This closes the timer scheduling/mutation scope, not the remaining overall
retail pathfinding or frame-budget gates.

## Modal orders validate before replacing active movement

Payoff119 closes ORDER-01.14's concrete modal/metadata admission scope. Ordinary
public `defend`, `undefend` and runtime-added `ARal` point orders enter normal
mode1 admission. Accepted requests cancel the existing Move and its pending
successors; their instantaneous action retires to public current order0.
Repeated Defend directions, unavailable/unresearched Defend and invalid requests
reject before admission and leave the active Move intact. Repeated Rally point
orders are accepted. Removing ARal makes subsequent Rally requests reject.

The busy scene explicitly issues a second Move while Defend is active, then
attempts Defend again; it repeats this with undefend after travel has resumed.
The rejected direction preserves851986 and continued committed travel. These
controls supersede the earlier idle-head witness: final head0 alone cannot
distinguish cancellation from metadata interception.

Original69b2f0 offers virtual22c interception. Both the Defend and Rally tables
resolve that slot to4371a0 (`XOR EAX,EAX; RET4`), which returns false. There is
no positive interception in these scenes. Modal3fcda0 validates the explicit
on/off selector against ability flags20.80 before dispatch. Event8 passes through
423d20/5ef450 and the concrete Defend virtual330/334 handlers. The modifiers are
applied/removed through5c7ea0/5f4290; the immediate speed cap becomes433cffff,
the binary32 word just below189, from an authored base270 and DataC=.3. The
engine uses the recovered scalar operation order rather than a decimal speed
approximation. Original673e80 clears queued successors while retaining its
current head; its return is void, so incidental EAX is excluded from comparisons.

`CAbilityDefend` now owns registered explicit on/off orders, validates direction
and availability before cancellation, stores the actual authored alias/rank,
and publishes modifier changes synchronously. Disable/removal runs its inverse
without replacing a subsequent Move or pending queue. The command-card owner
applies the primary selection's explicit direction to controllable selected
units. Registered order rejection ends dispatch; the previous generic spell
fall-through could execute the opposite toggle after rejection. Shadow Meld's
existing Hide execution now enters through its own registered order handler,
retaining the actual stock/Akama alias and existing validation policy.

Rally's `S_IssueRallyPointOrder`/`S_IssueRallyTargetOrder` validate capability
before normal public-order cancellation. Authored/runtime ARal is sufficient
even on a mobile unit, and explicit removal disables the legacy capability
fallback. Direct command-card metadata setters retain their separate boundary.
These captures establish ordinary public native admission, not retail Shift/UI
timing or every instant/cast family. Existing BASE/ORDER-01.13 tasks own those
broader domains; see [Rally contracts](rally-points.md).

Three completed, read-only original1.27 scenes each have two exact repeats:
Defend84 public six-word records/13 movement commits, Rally40/13, busy-rejection
86/73. The exports include tick, current order, acceptance, cap and position.
Canonical identity joins retain original queue head/tail/count, flags, caller,
selector and scalar clock words. The verifier checks complete observer/producer
lifetimes, hash/provenance, record ordering and footer motion counts. Frozen
raw streams and content-addressed observer/map-builder sources are committed.
Rally D/E's capture source list omitted its probe filename; the recorded actual
map hash and an offline exact comparison of the embedded probe globals/functions
provide a separate `probe_embedding` certificate. This is not a retroactively
captured probe hash. Busy F/G includes all three producer source hashes.

The production JASS/frame path reproduces all210 public records and99 native
motion commits. Five saves per scene replay245 exact suffix commits in total;
Save114 is unchanged. Separate queue, alias-removal, command-card and Rally
capability regressions pass. The new owner regressions failed against the
committed implementation before the fix. Focused Classic/TFT validation covers
24 invocations, including existing Mana Shield, Hide, Raven, Ancient and Repair
orders. It does not substitute for the scheduled full-suite checkpoint.

MapPathfinding.java adds12 function mappings and updates69b2f0; the type schema
adds two partial prefixes and10 verified ABI declarations. Saved names,
comments, xrefs, prototypes, return instructions and layout readback are in
`retail-metadata-119-static.json` with `unsaved=false`. No original function
bodies or asset files are redistributed.

### Final captain binding cancellation, withdrawal and refill

Payoff120 closes GROUP-03.4.6.2.2.2.2.2 with three completed forty-second
public scenes, each repeated independently: RemoveUnit on all thirteen recruits
at9.2s, point retarget on all thirteen, and Stop on all thirteen. The removal
scene creates fresh recruits at9.9s and refills the existing captain at10s.
The controls call the same recruitment sequence again, preserving their original
recruits. This avoids the existing-VM StartCampaignAI entry guard. It also avoids
assuming that CreateCaptains makes old recruits available again: exploratory
recreation controls rejected those recruits even after a0.1s wait. Default-town
positioning and captain recreation remain separate GROUP-03.4 producer gaps.

| Scene | Full unit motion commits | Shared footprint observations | Shared generations |
|---|---:|---:|---:|
| Remove and fresh refill | 7,386 | 591 | 4 |
| Point retarget | 4,046 | 12 | 1 |
| Stop | 3,549 | 12 | 1 |

Stop and point replacement cancel physical tasks while retaining logical
captain membership. RemoveUnit first cancels physical movement, then withdraws
the logical member during deferred cleanup. Original9d5610 calls9d0650 with a
**signed integer** delta of-1, unregisters subscriptions and removes the roster
link. The actual count drops13→0 before fresh creation; recruitment later rises
0→13. Remembered size and formation state are independent. The engine now
removes the captain array entry and compacts logical encounter indices before
edict reuse. Partial removal preserves the remaining roster order. Save
validation rejects a count inconsistent with the retained logical references.

The two canceled physical batches keep the old shared owner's two references
through counter1331. The following prepass releases the bindings; counter1332
observes zero references and invalidates the old canonical identity. Refilling
creates a distinct shared generation, followed by target and point generations.
There are26 canonical unit lifetimes in the removal scene. The normalizer joins
motion by canonical births rather than recycled mover addresses.

The fresh journey exposed another missing branch in9d8eb0. An outer departure
updates the whole roster when `member_count / 10 < member_count - inner_count`.
At counter1457, thirteen members and only eleven inner members take9d16c0's
shared-target branch. Its two passes preserve roster order, skip already
captain-target heads, and prepare12+1 physical batches against one shared owner.
Policy0 omits100; target activation yields persistent1,800, extra-refresh400 and
target state1000. Unlike a private captain approach, these prepared requests
carry zero authored arrival range and activate the ordinary`0x3efae148` threshold.
The engine reuses the existing physical request implementation for both target
and point families. When all members enter, target ownership cannot satisfy the
point-family unchanged-order guard, even if the world goals coincide.

Public retarget successors remain privately scheduled physical requests,
visited newest first. The owning Move ability receives normal order admission;
internal two-pass captain admission supplies its own owners. Fresh path
activation resets **both coarse timestamps** as well as the fine timestamp.
Previously, the old shared path's time1325 delayed the successor at1331. The
regression first failed at9.21s on physical ordering, then9.24s on inherited
admission throttling. The full successor motion now agrees with retail.

`public_captain_lifetime_journey` checks14,981 original unit commits and615
live/cached shared footprints through40s. Five saves per variant, including14s
inside the new shared-target phase, replay9,160 exact suffix commits. The partial
withdrawal regression also checks invalid-count rejection and restoration.
Existing captain completion, largest-member Stop, final-binding Stop, GoHome
and retry continuations remain covered. Save114's layout is unchanged; derived
bot links are reconstructed without restoring a process-owned AI VM.

`retail-captain-lifetime-1.27.json`, its literal engine header and the strict
verifier freeze both repeats, all546 public records per scene, signed roster
updates, owner publications and complete numerical observations. The primary
clock has8,001 advances and1,333 owner callbacks in each stream. Footer totals
are pinned per capture: pre-scene loader clocks and attack queries vary, and
change-filtered replan/spatial diagnostics are not complete call inventories.
They do not dilute the complete movement/public producer certificate.
Content-addressed blobs preserve the actual map and observer generations,
including the probe's earlier comment wording.

Ghidra persists five new ABI declarations, eight function readbacks, names,
comments, return instructions and direct callers, with`unsaved_changes=false`.
MapPathfinding.java records the threshold, separate prepared/private ranges and
fresh admission clocks. The fixture retains the explicit exclusions: captain
recreation/default-town relocation, rosters above thirteen, AI VM restoration,
and whole-pathfinder fidelity. See [strict corpus](retail-pathfinding-corpus.md).

## Captain approach ranges retain the siege roster snapshot

Payoff121 corrects a stale Ghidra interpretation of9d72f0: Attack f4/f8 are
**damage types**, with3 meaning siege. Delivery types are dc/e0; missile2,
normal1 and instant4 are observed separately. The strict threshold is600.
Authored siege599/600/601 with acquisition1000 yields0/0/1. The original
Footman acquisition500 limits the effective range, so it cannot witness the
upper boundary without that authored acquisition change.
The older public captain journey fixtures inherit `hRTE`, whose minimal archive
has no weapon row and yields acquisition0. They now author Footman's acquisition500
explicitly; keeping that missing input would incorrectly clamp their range90 to0.

Two complete read-only public producer captures agree on seven range creations
and twenty roster predicate calls. Initial world ranges are429.3999938964844,
430,630.5999755859375,690,270 and690. The disabled siege700 member still sets
the captain6c.20 flag, but its enabled attack maximum iszero:70 plus the retained
200 bonus yields270. It has an Attack object; the unarmed300 fallback does not
apply. Removing both qualifying siege recruits clears the roster flag; a fresh
normal/instant700 recruit receives490. Its physical arrival range is521/32.
Address reuse in the seventh birth is tracked by canonical identity, not by
assuming a mover pointer names one lifetime.

Attack owns the effective-range and siege predicates. Move snapshots the roster
flag on attach/detach and consults it when creating a private approach. Existing
physical ranges retain their values after weapon writes or roster changes.
The new flag is persisted inSave115, independently of the current weapon
profiles. Focused engine checks cover all seven physical ranges, weapon edits,
three round trips, last-siege removal and malformed saved flag rejection.
AI VM restoration is excluded; post-load fresh admission exercises the existing
Move-owned prepared producer with the retained logical actor.

The literal fixture is`retail-captain-ranges-1.27.json`; both original streams
are compressed content-addressed repository inputs. Five saved Ghidra function
readbacks include the two new explicit ABIs, field layout and caller references.
`MapPathfinding.java` corrects the former missile label and records the narrower
verified scope. The strict live corpus contract compares complete range producer
records, not the full movement journeys. GROUP-03.4.6.2.1.2 remains open for
Hero/BTLF/native unit5c.40000000, suppression counters and target-adjusted ranges.

Focused validation: both Classic and TFT pass `wc3_movement.public_captain_*`
(22 tests,2,606,482 assertions each), `wc3_movement.public_ai_*` (five tests)
and `wc3_save.rejects_prior_save_versions` (including114). All182 captain Python
checks,19 corpus checks, eight metadata checks and16 timer checks pass. The
strict fresh corpus entry`live-captain-ranges-261006` verifies both original
captures; Ghidra readback matches the saved program with no unsaved changes.
The full repository suite follows the authorized batch cadence.

## Repair families own admission, work and pending activation

Payoff122 closes ORDER-01.11's current-order contract. Repair is not one generic
`repair` branch: Arep (Peasant/Peon), Aren (Wisp) and Arst (Acolyte) have separate
registered target and autocast commands. The concrete ability owns validation,
work interception, approach, completion and interruption.

| Ability | Target order | Autocast on/off |
| --- | --- | --- |
| Arep | repair852024 | repairon852025 / repairoff852026 |
| Aren | renew852161 | renewon852162 / renewoff852163 |
| Arst | restoration852202 | restorationon852203 / restorationoff852204 |

`restore` is not the Arst command. Earlier diagnostic captures using it or using
Repair for every race are excluded. Two complete four-race public captures,
`runtime/payoff122/repair-f.jsonl` and `repair-g.jsonl`, match all1,316 exported
records and the complete normalized admission/movement observation stream:
566 velocity commits per run. Every worker has all300 timer samples, through
natural completion. The frozen fixture retains raw health/position words; these
retail repeat matches do not claim engine repair-rate or approach-trajectory
numerical parity.

Original3ff160 uses software subtraction of max life and current life, rejecting
ordinary completed targets with less than1 missing life. Exactly1 is admitted.
A target becoming full during approach does not immediately retire the accepted
head: the worker continues to contact, where work completes. Smart approach
owns851971; replacing it with the concrete command changes the head. During
work,409630 has published the canonical target identity at owner138/13c.
Interception4371b0 can then accept same-target Smart/concrete commands without
replacing the old head, work timers or pending FIFO. The engine's new generic
`A_TARGET_ORDER_ADMIT` hook gives the registered/active ability that decision
before generic queue mutation. Issued target events still publish the accepted
command independently of the retained current head.

A valid autocast direction change interrupts Move or Repair and completes its
head to0. An already-selected direction, unavailable ability or wrong Repair
family rejects before clearing pending work. Internal construction's
`S_OrderRepair` entry does not invent a public head; autocast uses the owning
family's actual order name. The six Renew/Restoration toggle/target IDs now reach
ordinary native dispatch and the Repair target-selection UI through flat ability
registry rows.

Public RemoveUnit(target) keeps the Repair head synchronously, then the scheduled
owner completes it. Repair withdraws its target/goal immediately and saves this
retirement separately. Direct free followed by slot reuse is guarded by the
saved target spawn generation. KillUnit(worker) retires the head immediately;
a recreated worker starts idle. Save116 retains the generation and removed-target
state in the existing scalar buildwork pool. See [save contract](save-load.md).

The pending-order witness uses external owned Winelib SendInput, with no Frida
writes or calls into gameplay. `repair-queue-d.jsonl` and `repair-queue-e.jsonl`
select one local-player worker at a time, issue an ordinary Move, then press
Shift+R and click its damaged building at sample10/80/150/220. All four races
retain851986 while waiting, activate852024/852202/852161 after Move finishes,
and naturally complete to0. Each run contains1,208 complete public records and
four native inputs. Input delivery phases differ between repeats; each complete
stream has its own frozen normalized digest. Equality of the ownership sequence
is established; full queue trajectory equality between runs is not claimed.

Fixture setup matters: the source interlude's Player0 is not the local player.
Creating selected workers for Player0 produced no user Repair order despite
successful input delivery. The accepted fixture uses GetLocalPlayer(), a user
controller and expanded camera bounds. A first diagnostic also omitted the
`case=metadata_` prefix that enables exported hashtable observation. Neither
incomplete/ineffective input witness is used for closure.

Ghidra saves16 mapped functions, WC3RepairPrefix (0x1d0,12 known fields) and five
explicit thiscall ABIs. Saved instruction/type readbacks and MapPathfinding.java
retain target identities, concrete getters, completion threshold and common
work inverse. Unmapped prefix bytes and timer internals remain unspecified.

Reproduce the owned queue map with:

```sh
python tools/frida/make_wc3_pathfinding_map.py \
  --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --scenario repair_queue --output /path/to/w3/Maps/PathRepairQueue.w3m
winegcc -o /tmp/wc3_ui_input.exe tools/frida/wc3_ui_input.c
cp tools/frida/wc3_ui_input.c /tmp/wc3_ui_input.c
```

Use trace_wc3_pathfinding.py with an owned isolated display/server and the usual
`--metadata-events --task-events --motion-events --velocity-events --profile-events`.
Add `--point-input-helper /tmp/wc3_ui_input.exe --point-native-key
--point-order-key repair --point-click-shift --point-click-sample-ticks
--point-click-at 10 --point-click 512 330 --point-click-extra 80 512 330
--point-click-extra 150 512 330 --point-click-extra 220 512 330` for the1024x768
outer window used here. Keep both helper artifacts beside their launcher; other
window geometries require observing the building's actual client pixels.
The final probe sources and each capture's controller generation are pinned.

The verifier reads repository archives by default:

```sh
python tools/frida/verify_wc3_repair_trace.py \
  --fixture tools/ghidra/fixtures/retail-repair-orders-1.27.json \
  --output /tmp/repair-orders.json
python -m unittest discover -s tests -p test_wc3_pathfinding_repair.py
```

The engine regression first failed44 assertions against the old dispatch. Focused
Classic/TFT checks cover native calls, all three family procedures, real server
queue activation, completion, invalid replacement, internal approaches, work
interception, death, target/worker reuse and saved continuation. The normal full
repository suite remains on the authorized batch cadence. Repair costs/rates,
construction race strategies and full Move/target policy parity retain their
existing broader task scopes; this closure adds no new TODOs.

Final focused results per edition: `wc3_order_lifecycle.*`427 assertions/27 tests,
`wc3_api.current_order_repair_*`51 assertions, and `wc3_building.*`3,810
assertions/153 tests. Prior-version rejection passes214 assertions, including115.
Eight Repair evidence checks,19 corpus checks and55 Frida-controller checks pass;
the new fresh corpus entry verifies all four complete captures. The extra queue
fixture initializes a real JASS VM before calling RunFrame; calling RunFrame on
the earlier minimal no-VM unit fixture is invalid and was corrected in the test.


## Follow target loss preserves temporary combat ownership

Payoff123 integrates ORDER-01.17. Two complete read-only retail runs of
`PathFollowLifetime123d.w3m` export the same1,455 ten-word public records
(digest `8ca7e9a85f961a1256c03ef0dd1c0773f11ad4224d5a6efef8b899625f45607e`).
Seven independently observed subjects cover target Move versus Smart, RemoveUnit
versus KillUnit, combat, synchronous damaged-trigger removal, nested
TriggerExecute replacement, target/subject recreation and a healthy Follow
parent after enemy death. Damage-source handles prove that the observed Smart
attacks came from the corresponding subject. Paused friendly targets avoid
call-for-help combat contaminating that evidence.

Public target Move851986 ignores nearby automatic acquisition and the scripted
one-damage retaliation input. It completes synchronously when its target is
removed or killed. Smart851971 can enter combat while retaining its public head.
Losing its Follow target leaves the enemy fight active; enemy death completes
the old head. A healthy-target control retains Smart through enemy death instead.
During the damage callback, removing the Move target exposes head0; removing the
Smart parent during combat retains851971. Nested Move then publishes851986, and
the old callback/combat owner cannot overwrite that replacement on unwind.
Recreated subjects start with head0 and never inherit an old command.

Ghidra and actual stacks establish the missing RemoveUnit caller chain:
`210c10 -> Unit virtual84 ->6882e0 ->688300(1,0) ->651010`.
The stock Unit vtable's84 slot points to6882e0.688300 marks Unit20 retired bit1
before emitting target loss;679bb0 marks the death state before its separate
publication. Move5ff490 resolves canonical cc/d0 target identity, clears its
related d8/dc order through5fbfc0, unlinks target subscriptions through5fbea0,
then performs the existing recovery policy. Four explicit thiscall ABIs,
two newly named identity fields, the call-site label and saved annotations are
recorded in `retail-follow-lifetime-ghidra-1.27.json`, MapPathfinding.java and
the portable type schema. Post-save readback reports no unsaved program changes.

The engine first reproduced six stale-target reuse failures, four combat/queue
ownership failures, three acquisition failures and a save failure involving an
empty Follow group. Move now captures and saves its target's spawn incarnation,
checks it both on its scheduled walk and on combat resumption, and suppresses
automatic acquisition for an explicit target Move. Attack forwards parent loss
to Move without finishing combat; loss of the actual enemy completes/resumes
through Attack's existing owner transition. Pending orders therefore remain
queued during parent loss and activate once the active enemy owner completes.

When the last member detaches, an empty physical group drops its borrowed target
in constant time. It retains its allocations and admission position until the
normal later owner visit. This keeps saves valid between target removal and
deferred storage release without adding a scan of every physical group to each
removal. Save117 adds only the logical Follow incarnation field; older save
versions are rejected and network layout is unchanged.

Engine regressions also run a real damaged-trigger/nested-trigger callback,
actual server-frame deferred cleanup, target/subject slot reuse, healthy-parent
resumption, queued handoff and save/load before the deferred drain. The save
fixture supplies an authored UnitWeapons row because the generated test archive
has none; a transient alloc_test_unit row cannot survive metadata rebinding.

Reproduce the frozen evidence with:

```sh
python3 tools/frida/verify_wc3_follow_lifetime_trace.py \
  --fixture tools/ghidra/fixtures/retail-follow-lifetime-1.27.json \
  --output /tmp/follow-lifetime-report.json
```

The two runs have2,001 and2,059 velocity-hook observations. Their public records,
including sampled position words, match exactly; whole combat trajectories and
attack timing remain ORDER-01.10. Failed exploratory captures with attacking
friendly targets are not accepted evidence. This ownership leaf does not certify
the whole retail pathfinder.

Focused Classic and TFT checks each pass34 order tests/513 assertions,
188 combat tests/4,620 assertions, twelve existing exact public Smart Follow
journeys/364,088 assertions, prior-version rejection/217 assertions and
layout rejection/four assertions. Rebuilt production/test modules also pass the
127-test unit suite; its exposed Stone Form immediate-owner regression is fixed
separately in `c6b59f3a`. Nine Follow verifier controls,19 corpus controls and55
Frida controller controls pass. The fresh strict Follow corpus entry verifies
both raw captures; inventory/hash checks cover all355 entries, without claiming
a fresh execution of the entire corpus.

The batch checkpoint exposed two earlier fixture assumptions: passage admission
still rejected native count-one routes despite Payoff113/114, and the supplied
Bloodlust execution callback used rounded10.5 rather than its captured primary
clock10.499238967895508. The first run failed64 passage assertions and460
modifier state/marker assertions while every modifier motion commit still
matched. The passage matrix now accepts count-one results and compares their
literal route words. The supplied Bloodlust
completion now runs before the tick105 public observer, where the frozen capture
first sees the buff; it no longer invents a10.5-second spell request deadline.
The observed primary sample clock is not proof of that unobserved deadline:
using the primary clock as Slow's deadline changes its integrated position.
The existing supplied Slow completion remains unchanged. Neither spell's actual
cast-request timing is certified by this numerical movement fixture.
Expected markers, states and all590 motion rows remain unchanged. Production
timer ordering is unchanged; complete spell execution timing remains excluded.
Mismatch diagnostics now show both actual and expected public markers.

The full batch checkpoint passes `make -j6 TEST_JOBS=6 test openwarcraft3`
with the native SDL2 environment: Classic and TFT each run2,746 tests/
7,987,453 assertions, and the aggregate pathfinding Python target passes723
checks. Other aggregate engine/standalone suites pass; terminal exit is0.
The new strict Follow entry and the original speed-modifier entry each verify
from fresh outputs. Logs are retained in
`/GitHub/wc3-analysis/runtime/payoff123/full-test-fixed.log`.

## Target blockers return after fine-route construction

Payoff125 integrates the completed [ROUTE-02.2 handoff](retail-pathfinding-handoffs/ROUTE-02.2/HANDOFF.md).
The original selector167bf0 and next-step collector166140 suppress the requester
only. The target remains a blocker, even though fine-route construction166e90
temporarily suppresses both requester and target. The engine previously carried
target exclusion into these later consumers. Twelve assertions in the new
four-class target regression reproduced that mismatch before the fix.

The game now makes target suppression an explicit query-local construction
property. Retained-route selection, sampled segments, endpoint checks and
next-step collection observe target occupancy. Immediate selection after an
unretained fine build releases suppression and discards cells cached under the
construction policy. This changes neither saved state nor network layout and
adds no global scan or allocation to a query.

Four production regressions cover the target's selected waypoint and waiting
identity,11 capped peer compositions, successive insert/move/stop/remove edits,
and diagonal repeated objects with terrain/null tokens. Forty peers publish in
reverse allocation order, so the asserted first32 candidates cannot accidentally
pass using edict order. The resolver's persistent peer20 assignments and
requester4 wait are checked, including faster peers just beyond the cap.
The overlapping peer fixture starts at the already-admitted stage used by the
original oracle; public admission would correctly recover those peers out of
the overlap before collection.

The preserved complete original-code oracle reproduces72 strip/cap calls,
14 resolver compositions,288 waypoint selections, four edit sequences,
16 selection/collection contrasts and eight target cases. Frozen expected bytes
have SHA256 `1433827c28e1e59c74c5cb6863aad9b09f7d78ee72bdb0738715cc96abd611f6`.
A fresh run without observers matches all behavioral results; only its absent
write audit differs. The48-function static closure and write audit exclude
occupancy mutation during one selector call. Edits are therefore tested between
successive calls, rather than injected into an impossible callback.

Ghidra's eight producer/consumer annotations are saved and mirrored in
MapPathfinding.java. The strict corpus adds the hash-pinned original contract,
including all eight target cases. Classic and TFT each pass the four new tests
with1,336 assertions. This closes ROUTE-02.2 at S/O/C evidence level. It does not
claim a live retail crowd reaching32 candidates; runtime lazy-link chronology,
non-mover static payload lifetimes and full crowd trajectories remain in the
existing FINE/FOOT/E2E items.

Both editions also pass the affected349-test movement suite with5,195,065
assertions each. The fresh strict blocker entry verifies; all357 inventory/hash
contracts and20 corpus regression checks pass against the staged commit tree.
No full-suite checkpoint was repeated for this second implementation commit
after14357757. Logs are retained in `/GitHub/wc3-analysis/runtime/payoff125/`.

## Adaptive branch witnesses justify clear-interior pruning

Payoff126 integrates ACC-01.1, ACC-01.2, ACC-02.2 and ACC-04.1. The completed
handoffs supply a complete289-jump inventory with exact preconditions,
producer-built witnesses and marker rejection proofs. The frozen inventory
contains463 reachable outcomes,113 unreachable outcomes and two separate
out-of-map caller-domain outcomes. The handoff's earlier115-unreachable summary
included those two caller cases; its actual per-outcome classifications are
retained. ROUTE-01.2 and the ushort-alias regime remain separate.

All3,288 complete original requests cover the463 reachable outcomes and276
level-transition/clamp combinations. The engine already matched every result,
work count, node count, warp count and route word before this optimization.
That agreement is now enforced by production C regressions, with both literal
index resets and the engine's epoch/descendant-alias optimization. Ten rare
outcomes additionally compare the full ordered node tables: representatives,
level, g/h, parent, state and source/incoming marker bytes. They protect closed
head reopening, improved special destinations and marker-dependent corner
subdivision without encoding particular routes into production code.

The side-walker proof yields a direct engine improvement. A size2 interior
segment has just resolved a clear adaptive square; every base cell queried by
its interior predicate lies inside that square and is therefore clear. The
production side walker now omits those redundant base-cell reads. Boundary
checks, corner checks, subdivision order and end flags remain literal. Above
the uint16 identity range it retains the complete predicate, preserving the
existing alias behavior. Applying this shortcut to the shared predicate itself
would also skip necessary coarse-corner occupancy checks; the shortcut is
deliberately owned by the proven side-walker call site.

Marker regressions compare96 original reducer cases with and without markers.
They check the marker9 clear-parent chain through all hierarchy levels and its
erasure. Clear marked children can legitimately make a mixed parent; class11
and non-base markers have no producer. The preserved writer census and static
candidate scan support the handoff's writer analysis; the heuristic scan alone
is not a proof of absence. Object eligibility and the49-link producer domain
remain FOOT-03.

Cost regressions add336 ordinary/transposed requests, all188 goal costs,
135,981 integer-distance inputs and157 budget/nearest-node cases. Equal g keeps
the earlier parent. A partial endpoint is the strictly nearest discovered
representative, rather than the minimum-f node. The four asymmetric cost pairs
remain880/891,845/849,894/895 and905/920. Every observed successful route is
optimal over its own explored adaptive graph; the eight-neighbor base grid is
not an optimality oracle for that graph. Any-angle edges and encounter-dependent
representatives explain the observed grid differences.

The strict `oracle-adaptive-witnesses` entry replays all four original reports
and requires equality with their full frozen JSON. Its aggregate canonical
digest is `2e32a93a2622b3892d0eb7984161656429da155bfed54dd9fd574969da4e8cd5`.
The59 frozen disassembly/accepted-coverage inputs make this replay independent
of the private Ghidra cache path; relocating those input files does not change
original execution. Research scripts remain unchanged. No live or whole-world
trajectory claim is made by these algorithmic contracts.

Ghidra has31 saved annotations and26 instruction-verified explicit ECX/stack
prototypes. The portable schema and saved readback retain them. The same four
C regressions run inside the game module and through the fast target:

```sh
make BUILD=release test-wc3-adaptive-witnesses
```

The new tests pass328,872 assertions before and after the optimization. Frozen
expected words are exported by `generate_wc3_adaptive_witnesses.py`; `--check`
rejects a stale export. The fast target avoids rebuilding the complete game
module during pure adaptive-kernel iterations; world/owner tests still run in
the game when those contracts change. Save and network layouts are unchanged.

Focused Classic and TFT validation each passes56 tests/2,757,467 assertions:
the new kernel matrix, existing pathfinding and Way Gate suites, and14 exact
adaptive/gate movement tests. Five evidence/export/ABI checks pass. The complete
fresh combined oracle equals all four frozen reports; production and test builds
pass. This is implementation commit3 after14357757, rather than another full
suite checkpoint. Logs are retained in `/GitHub/wc3-analysis/runtime/payoff126/`.

The staged commit also passes all358 inventory/hash contracts and25 Python
corpus/evidence checks. Four selected strict original/C entries pass: adaptive
storage and marker producers remain exact, while the two documented size2
reference differences retain their expected status. The fresh combined report
passes the new strict manifest contract.

## Separation policy refresh follows channel and type lifecycle

Payoff127 closes SEP-01.3 using the completed [policy matrix handoff](retail-pathfinding-handoffs/SEP-01.3/HANDOFF.md)
and [producer handoff](retail-pathfinding-handoffs/SEP-01.2/HANDOFF.md). Fresh analysis of both original Frida captures
reproduces the entire frozen 36-case matrix byte for byte. All 6,035 JASS markers match the observer-free control and
the repeated observed run. Replaying the first capture through unchanged original instructions reproduces 2,626
update bodies, 2,272 pair contributions, 2,626 tails and 6,851 cooldown visits without differences. This is renewed
analysis of the preserved captures, rather than a new live launch.

The engine had two missing policy producers. Channel begin/end did not notify Move, so a channeling unit retained
its repulsor and could be displaced or push its neighbors. A type rebind also retained the previous repulsor's
selector/category/rank and pending vector. The spell processor now publishes `A_CHANNEL_STATE_CHANGED` after changing
its channel code, before cancellation callbacks; Move owns the inverse and replacement through the same ordered
initialization routine used by owner changes. A refresh during a live channel or pause remains disabled. Type-changed
dispatch replaces the policy after the new authored row is bound. No periodic entity scan is introduced.

`repulseParam` is a retail integer at UnitData+228. Both SLK loading and object modification now retain that integer,
and `wc3_repulse_policy` implements the three ordered setters directly. The old float representation rounded
16777217 to 16777216 before selection, losing bit 0 and selecting row0 instead of row1. The regression loads the
non-stock integer from metadata; ordinary17/1 selector, group and rank aliases remain covered. Instance/save/network
layouts are unchanged: this is a four-byte type-data field and the new message is appended to the dispatch enum.

The engine matrix isolates eligibility by giving the callback a known nonzero row0 contribution at close range.
This checks the candidate predicate before an inert selector or a deadzone could conceal acceptance. It covers
foot/fly/hover/amph/horse, cross air/ground pairs, owners 0/1/2/15, disabled peers, group aliases and asymmetric rank.
The frozen aggregate reasons are not a chronological event trace; runtime channel, rebind and pause transitions
have separate production lifecycle checks. Existing owner-change/removal/save tests cover those public producers.
Float is absent from the live map because it has no water; the instruction-verified filter has no movement-type
condition, but this matrix does not claim a float trajectory capture.

The strict `oracle-repulsion-policy` contract requires the entire original 65,536-case setter/inert report hash,
`2c6a72939d48400158bbd5e82848a78a68a34382da66422d54483d0c5d5006c8`, plus 24 independently frozen pair results
with recorded valid owner state. The original inert controls supplied unaligned RNG byte offsets, which no normal
owner seed produces. They remain unchanged in the original oracle contract; production C uses the same geometry
and vectors with the captured 4273436052/209508436 state. This avoids silently accepting an out-of-domain table
lookup as a gameplay requirement. Every zero-row selector 5–15 still consumes an overlap draw, stays eligible to
its peers and zeroes its retained vector without installing cooldown.

Thirteen Ghidra names/comments are saved and read back with no unsaved changes, and the corresponding mapper rows
are committed. The original Frida map builder, observer, controller, analyzers and original-code replay scripts
remain available beside their frozen handoffs. SEP-01.2 stays open for Mechanical Critter's flag60 bit 0 and counted
work/suppression producers; adding Amec alone is not evidence that its special category flag was set. Spatial
cell-chain ordering and full multi-neighbor trajectories remain their existing SEP-02/03/04 items. No TODOs split.

Pure packing/inert kernel checks can run without rebuilding the entire game module:

```sh
make BUILD=release test-wc3-repulsion-kernel
```

Validation logs and fresh original/capture reports are retained in `/GitHub/wc3-analysis/runtime/payoff127/`.
This is implementation commit 4 after full checkpoint 14357757; focused checks are used for this chunk.

Focused Classic and TFT validation each passes 498 tests/563,318 assertions: the five new lifecycle/matrix tests,
two kernel contracts, 86 metadata tests, 401 spell tests and four existing repulsion/shared-RNG movement tests.
Production and test builds pass. The staged corpus contains 359 valid inventory/hash contracts; the fresh policy
report passes its strict manifest checks, and 24 Python corpus/evidence checks pass. The pure kernel target
passes 263,464 assertions without a full game rebuild.

## Spatial load rebuilds membership in save order

Payoff128 closes MAP-06.2 from the completed [retail save/load handoff](retail-pathfinding-handoffs/MAP-06.2/HANDOFF.md).
Retail save compacts proximity and fine maps, then writes logical objects. Load
constructs a new owner/map/clock, restores the saved fields and prepends each
ordinary rectangle in object load order (`14d000 -> 14d380 -> 14d2e0`). Static
saved-cell handles also prepend in saved order (`14d0b0`). Live moving proximity
cell530 changes `[M,C0,C1,C2] -> [C2,C1,C0,M]`; the idle four-unit cell has a
different movement-era order but the same post-load order. This rule follows the
saved object stream; it is not a request to reverse every old chain blindly.

The full retained UI capture resumes four movers word-identically to the
uninterrupted control, with74/99/60/75 commits (308 total) from the matched save
point to arrival. Both observed and observer-free JASS suffixes agree with their
own control suffixes. Their whole files differ because the UI saved on different
ticks; that false comparison remains in the frozen report. The all-movers capture
also contains two unrelated loaded movers whose trajectories are not certified.
Scripted save without load preserves481 commits across the four tracked movers.
Failed JASS LoadGame and `-loadfile` attempts remain failed evidence in the retained
input bundle, not successful load witnesses. No new live run is claimed here.

`verify_wc3_pathing_spatial_save.py` reruns the original saved-rectangle insertion
stage with real owner/map/object constructors, four clipped/unclipped rectangles
and all six load orders:24 cases. The supplied decoded rectangles/reference
counts are inputs; this isolated oracle does not emulate the stream decoder.
Its complete chains and candidate orders must equal the frozen original output.
Separately, it reconstructs the entire unchanged MAP-06.2 report from16 retained
capture/marker files and requires byte identity (`0856f6f3...d8249`).
Fourteen mapping names/comments are saved and read back with no unsaved Ghidra
changes; signatures are recorded without inventing new calling-convention claims.

The engine now saves ordinary logical rectangles and reconstructs active fine
ranks through `G_LoadMoveSpatialObject` in the serialized object stream order.
It preserves saved rectangles without admission, pose prediction or RNG draws.
The derived intrusive broadphase still touches only each object's own cells;
no global sort or extra per-frame entity scan is introduced. Save118 removes
128 bytes of ranks per active object and the8-byte shared publication counter.
The game/network entity layout is unchanged. Older saves are rejected according
to the repository's exact-version policy.

`wc3_spatial_load` checks actual four-blocker order before and after save/load,
unchanged presentation relinks, an order-sensitive fine target observer, a real
subsequent leave/reentry, fractional/clipped rectangles at all four footprint
sizes (including a flyer), and four public Move orders resumed at three active
checkpoints. The four-mover engine test compares every frame's pose, velocity,
route cursors, wait/repulsion state, order, owner clock and shared RNG. It is an
engine preservation test, not a claim that this synthetic map recreates the full
retail scene. The live capture supplies the separate retail producer evidence.

The earlier Save91 overlap test asserted that every saved suffix matched a
capture that never loaded a save. That assumption fails after the target's
movement re-prepends its chain: the first changed motion appears at7230 after
loading the6000 checkpoint. The captured uninterrupted501 commits remain the
oracle. Five unchanged-order/removed-blocker checkpoints retain exact suffix
checks; the changed-order consumer has its own explicit regression instead of
an unsupported uninterrupted-trajectory assertion.

Classic/TFT each pass206 focused tests /652,348 assertions; the new spatial
suite contributes317,515 assertions. Because the correction changes every active
load, the broader movement suite was run once:349 tests /5,186,690 assertions
pass per edition. The union is553 tests /5,533,155 assertions per edition. The adjusted native overlap regression
retains501 original commits and1,163 exact saved suffix commits across its five
valid checkpoints. The unchanged strict corpus entry and25 Python checks pass
from the isolated staged tree; full-repository validation remains on the
authorized batch cadence. Logs: `/GitHub/wc3-analysis/runtime/payoff128/`.

Proximity query ordering, lazy records, native stamp alias/repair and periodic
maintenance remain SEP-02/03. Rebuilding compact active fine memberships does
not implement those outstanding contracts. Adaptive map state remains an
independent saved publication; load must not refresh pending terrain edits.

```sh
make BUILD=release -j6 openwarcraft3-tests openwarcraft3
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 build/bin/openwarcraft3-tests \
  -data build/tests +dedicated 1 +test 'wc3_spatial_load.*'
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_spatial_save.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/spatial-save-fresh.json
```

## Map replacement retires movement owners before world data

Payoff129 integrates [MAP-06.1](retail-pathfinding-handoffs/MAP-06.1/HANDOFF.md).
The complete retained read-only captures contain eight finished `RestartGame(false)`
cycles and one `ChangeLevel`, plus the ninth restart beginning. Every flow retires
all movers and158 region objects before map/owner destruction. Release compacts
100 proximity records and7,076 fine records to zero, frees link backing and cancels
maintenance requests. New loads construct fresh maps, storage, routes and script
state. Reused heap addresses do not imply retained state.

The first mover repeats at least43 exact position/velocity commits in every
completed restart. The second ChangeLevel map matches46 first-movement commits
and continues111 commits to arrival. Five restart cycles have differing final
teardown commits: partial integration depends on the wall-clock point where the
restart interrupts the frame. These differences remain in the unchanged frozen
report; they are excluded from the first-movement equality claim. Observer-free
controls match219 JASS markers across both ChangeLevel maps and the last restart
cycle. Menu-driven restart remains outside this evidence; the two JASS natives
produced these transitions. No new live run is claimed.

The previous engine path replaced map storage and reloaded typed unit rows before
`G_SpawnEntities` retired old level roots. It then reset `globals.num_edicts`
without freeing the old actors' dynamically owned movement curves or clearing
unreused actor slots. Clearing a typed pool does not release Move-owned curves.

`G_LoadMap` now calls game-owned `G_ReleaseLevel` before invoking the world loader.
It finishes borrowed-geometry jobs, closes script/AI roots, retires registries,
groups, fine/coarse queues, fine memberships and repulsors, frees every old actor
route, resets pooled/deferred state, and clears the edict arena while retaining
client-slot identities. It then drops generic routing frontiers, fields and map
geometry caches before zeroing level state. `G_SpawnEntities` only initializes the
new map. The release performs one bulk clear; it does not visit the entire map
once per retiring actor. No retail allocation sizes, addresses or wall-clock
teardown timings become gameplay constants.

`wc3_map_lifetime` loads real, source-controlled small MPQs through the production
`globals.LoadMap` entry. It observes the first loader yield before the old terrain
or unit rows are freed, requires all old route pointers/actor slots to be retired,
and compares public Move poses, scalar velocities, route cursors, wait/repulsion
state, owner clock and shared RNG across eight restarts. A different-sized map
loads twice against its own control. Twelve additional actors occupy slots absent
from the next map's script. A queued static frontier remains pending at reload;
worker and inline execution alternate. These are engine lifecycle regressions;
the separate retail captures establish the native reload producer contract.

`verify_wc3_pathing_map_lifetime.py` also executes nine original-code spatial-map
and owner release cases using original constructors, insertion/update, retirement,
map release and owner destructor. All objects retire first; links/cells reach
zero, maintenance requests cancel, and owner allocations disappear. The isolated
harness's external registry vector retains its own allocation; it is explicitly
accounted separately and does not claim a whole-process teardown. The portable
capture bundle reconstructs the full unchanged MAP-06.1 report and observer
controls. Five Ghidra function names/comments have been saved and read back,
with matching reproducible mapper rows. Native search-system allocation graphs
and UI menu dispatch are not executed by this isolated release oracle.

```sh
python3 games/warcraft-3/tests/fixtures/make_pathing_reload_maps.py
make BUILD=release -j6 test-assets openwarcraft3-tests openwarcraft3
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 build/bin/openwarcraft3-tests \
  -data build/tests +dedicated 1 +test 'wc3_map_lifetime.*'
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_map_lifetime.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/map-lifetime-fresh.json
```


Focused Classic/TFT each pass213 distinct tests /435,603 assertions, including
57,847 assertions in the actual reload regression. Spawn stage/versioned-defaults,
active save/load, script loading, both worker modes, route removal and pending
fine/coarse queue regressions pass. Thirty Python checks and the fresh strict
`oracle-map-lifetime` report verify from the isolated staged tree. Full-repository
validation remains on the authorized batch cadence. The pre-fix real loader
observed the old VM, active actors, all three route allocations and a queued
frontier at the first replacement yield; it failed2,900 assertions. Its final
harness cleanup initially detached terrain before clearing the area tree; that
separate cleanup crash was corrected before acceptance. No crash is used as the
reload-fidelity proof. Logs: `/GitHub/wc3-analysis/runtime/payoff129/`.


## Coarse exclusion consumes published object geometry

Payoff130 fixes flying-target filtering and repeated display-position geometry
conversion in coarse requests. The shared scope restores self then target before
returning to either group or member routing. Complete original scope/edit
reports reconstruct unchanged; engine regressions cover published rectangles,
alias/null boundaries and exact/partial/pre-acquire-denial exits. See
[published rectangle contract and evidence](retail-pathfinding-exclusions.md).
The broader MAP-04.1/02 tasks remain open.

## Authored movement profiles separate query, occupancy and hierarchy

Payoff131 closes BASE-02.1 with the full authored producer/lane/support-source
inventory and a shared immutable engine profile table. Zero fine query does not
disable a positive-speed Move owner; unbuild publishes category08. Member/group,
portal and formation routing retain independent coarse selection, including
Ensnare fine grounding with class3 retained. Complete original, compiled C and
controlled live evidence is retained; numerical support fixtures remain MAP-02.2.
See [authored profiles](retail-pathfinding-profiles.md).

### Ordered proximity composition reaches complete engine separation

Payoff132 closes SEP-02.2 with an independent Move-owned proximity index:
intersection links retain priority, entered cells prepend and queries visit
Y/X cells with first-encounter deduplication. Complete engine updates match all
3,055 frozen retail T visits and2,439 individual neighbor contributions across
nine mixed policy/radius/owner/rank clusters, including rejected applications,
fine publication, caps and cooldowns. Both full original pair/tail replays and
3,543 observer-free markers verify. Save119 stores logical proximity rectangles
and rebuilds both indexes in save order. Native physical storage/maintenance
and forced stamp hazards retain SEP-03/MAP-05. See
[ordered proximity evidence](retail-pathfinding-proximity.md).


## Complete overlap owner passes and constant-time retirement

Payoff133 closes SEP-02.3/SEP-04.1 using the completed retail handoffs. All20
units run through the actual owner scheduler:2,546 visits,1,882 neighbor calls
and1,476 shared RNG draws match both complete captures. Derived incoming-link
slots replace quadratic owner-list retirement without changing order or saved
state. A1,024-unit pause batch drops from524,800 owner visits to1,024, independently
measured with Frida. See [complete overlap integration](retail-pathfinding-overlap.md)
for blocked controls, endpoint/occupancy coverage, load validation and evidence limits.


## Object-category inventory reaches items and building fine records

Payoff134 closes BASE-02.2 with the original producer/payload/consumer table,
fresh original eligibility and composed-region execution, repeated/control Frida
checks and actual engine regressions. Items now own category18/query10/radius1
fine occupancy; buildings retain their independent mover record. Item creation,
position natives and drops use owner inputs for admission, including the carried
native's two publications. Pickup/drop publish before queries or saves can
observe stale membership. See [object categories](retail-pathfinding-categories.md).
FOOT-03.1/03.2 remain open for raw-link history,49-link hierarchy admission and
mixed static/dynamic encounter order.

## Raw spatial storage and timed maintenance

Payoff135 closes MAP-05.1 through the production proximity map: eight-byte raw
records, stable64-object pool blocks, exact131072-link growth/reclaim/reuse,
native stamps and ordered software-clock maintenance. Save120 stores logical
stamps and rebuilds compact links. Fine raw producers remain open. See
[storage integration and evidence limits](retail-pathfinding-storage.md).

The full batch checkpoint passes with native SDL2: 2,800 tests and9,223,538
assertions per Classic/TFT mode, plus 755 Python tests. All 537 staged evidence
hashes match; the fresh spatial-record differential and both preserved
fine-storage oracles pass. This establishes behavioral integration, not the
remaining IceCrown performance acceptance.

## Fine maps use retained records and ordered perimeter observations

Payoff136 replaces the engine's reconstructed fine-cell ranks/intrusive lists
with stable pooled raw records. Actual publication, step blockers, target
observations, search metadata, independent timed compaction and Save121 now
share the original encounter/stamp rules. Movement updates only changed strips;
neighbor expansion samples each perimeter cell once. SEP-03.1/03.2/03.3 and
MAP-05.3 close with original DLL, preserved Frida controls, saved Ghidra and
actual engine regressions. Static regions and the49-record hierarchy cap remain
FOOT-03.1/03.2. See [storage contracts and validation](retail-pathfinding-storage.md#payoff136-fine-map-integration).

## Raw-cell consumers preserve retail eligibility and stamp order

Payoff137 closes FOOT-03.1 with one production traversal for fine/segment/endpoint
admission, step blockers and hierarchy. Suppression/active state, target-before-
suppression observation,49 raw-record hierarchy examinations, collector suffix
stamps and clockwise lane/cell rebuild order now have test-first engine fixes.
50,112 original/model/C predicates and36 terrain cases per-O0/-O2 build preserve
every map/object stamp; unchanged controlled Frida evidence remains verified.
FOOT-03.2 owns actual sparse widget region publication/reference lifetimes. See
[consumer integration and limits](retail-pathfinding-categories.md#payoff137-exact-raw-cell-consumer-eligibility).

## Widget regions retain mixed lifetime history

Payoff138 integrates actual sparse widget region producers with ordinary fine
objects and all raw-cell consumers. Collection retirement emits no inverse
links; inverse rasterization dirties cells and preserves distinct reference
counts. Save122 retains sparse membership and cached raster inputs in owner save
order. See [widget region evidence](retail-pathfinding-categories.md#payoff138-widget-regions-and-mixed-lifetimes).


## Bridges preserve authored terrain (Payoff139)

MAP-02.2's completed LT06 handoff exposes a wrong engine dependency: bridge
support was allowed to erase walking restrictions from the terrain. Four
completed retail file loads (authored, repeat, blank, deckwalk) retain identical
fine-cell top bytes before and after widget creation. The authored deck cell
(32,20) remains `1b`; blank WPM remains zero despite water and cliffs in W3E.
The repeated observed run and the observer-free JASS control have the same
193 public markers. Original crossing witnesses detour with authored deep-water
WPM and cross when the saved WPM permits walking. These are retained retail
observations, not a new capture or an engine trajectory-parity claim.

`move_terrain_word` now reads `pathmap.terrain` directly. Move's pooled widget
regions remain a separate input to fine and adaptive consumers. The legacy
baked field still ORs authored widget restrictions into the terrain baseline;
bridge pixels cannot clear it. The inferred deck algorithm and the second
terrain allocation/copy are removed. Static stamping uses one entity pass,
with constant work per texture pixel rather than scans along both texture axes.
The remaining baked-field baseline copy and hierarchy work still cost real CPU;
this change does not establish the 4096-unit frame-budget target.

The test-first 256-byte engine regression fails320 assertions before the fix,
then passes2304. It checks the legacy point field, actual fine admission and all
four adaptive lanes at a deck cell. A second regression replays every terrain
byte from three unchanged retail file-load snapshots in the game-owned map,
then checks creation, Save122/load and death:36873 assertions. Its widget texture
is synthetic, and its inputs are decoded captured bytes; it does not pretend to
exercise MPQ loading or validate bridge-resource decoding. Existing crossing and
rotation tests now supply passable WPM explicitly rather than relying on the
removed bridge override.

The unchanged capture archive is
`tools/ghidra/fixtures/retail-bridge-terrain-inputs-1.27.json.gz` (four observed
JSONL/preload pairs and one control pair, all original file hashes retained).
`retail_bridge_terrain.h` is tied to those load snapshots by a Python regression.
The fresh `oracle-load-movement-masks-engine` check still executes original WPM
and image consumer loops and compares all256 production WPM outputs. It also
requires all16384 captured terrain cells,193 control markers and the saved
`PathMaps_Load` Ghidra annotation; damaged captures, missing observer completion
and a bridge-erased terrain bit are rejected.

Focused debug Classic/TFT checks pass370 tests /2532987 assertions per variant,
covering destructables, raw fine spatial queries, WC3 pathfinding, the shared
routing suite and save/load. Production release builds successfully; focused
release Classic/TFT checks pass347 tests /428881 assertions per variant. All37
Python map-load/corpus checks pass, and the fresh original/C oracle reports6144
WPM consumer cases and256 exact engine byte comparisons. No full-suite checkpoint is claimed
for this chunk. MAP-02.2 stays **open**: deck ray geometry, air-height production,
the amphibious refresh history and the complete supported-lane support fixtures
must reach the engine before closure. No additional TODO is introduced.


## Oblique reconstruction and direct owned consumption (Payoff140)

ROUTE-01.1 is closed with the completed reconstruction handoff integrated into
Move. Fresh unchanged original instructions execute2664 fine requests through
166e90/148100 and10656 coarse requests through166c30/162cb0, across all four
footprint classes and four hierarchy lanes. The actual reproducer uses36 regular
vectors (32 oblique and4 same-cell/short/cardinal controls), plus9 enclosed-goal
vectors:45 distinct vectors in their union. The handoff's old52-vector description
was inaccurate; the frozen requests and counts are unchanged. Two source fractions,
three ordinary goal fractions and four maps cover all route words and order.
Production C at-O0/-O2 matches5328 fine and21312 coarse comparisons with zero
differences; the independent float32 models also match every reconstructed word.

Fine points are destination-first cell centres, with exact source last and exact
goal first only when floors match. The72 same-cell bypasses store one exact goal;
96 fine routes are partial. Coarse classes0/1 produce stored size1 and classes2/3
size2. The size2 edge decrement applies at levels1..3 before adding1.25;6604
coordinate decrements are witnessed. All348 partial coarse destinations are the
**raw nearest-node centre +0.5**, without that decrement or1.25 offset. There
are2568 coarse bypasses (864 same-cell,1704 shared promoted node), with one exact
goal and no mismatch. The older synthetic size4 reconstruction rows remain
controls;166c30 does not produce size4.

The actual engine adapter regression replays200 literal request rows:40 fine
and160 coarse, covering all classes, lanes, fractional oblique endpoints,
complete/partial routes and bypasses. All2586 coordinate/count/index assertions
pass. The fixture is generated from the frozen original words and checked by
Python; tests reset the independent request budgets rather than mistaking FIFO
exhaustion for a reconstruction difference. The native lane order is ground2,
amphibious80,float40,flight4. Public order clipping, world transforms and owner
cadence remain the existing separate tasks, not additional reconstruction leaves.

The engine payoff removes three full-chain scratch copies from fine, member
coarse and group coarse advancement. `wc3FineVector_t` now aliases the existing
plain `vec2_t`, so consumers borrow the owned native-coordinate arrays with no
casts or intermediate representation. Route publication still copies a newly
reconstructed chain once, using `memcpy`. Point coordinates, traversal order,
gate sentinels and arithmetic are unchanged. The serialized instance layout and
Save122 payload remain identical. No capacity/growth change is hidden here;
ROUTE-01.2 still owns that behavior.

The1024-point regression fails three scratch-preservation assertions before the
change and passes all eight afterward. Advancement now costs only the points and
segments actually inspected by the selector, rather than also copying every point
in the stored route. Fine visibility tests and coarse selection still perform
genuine work. This removes an O(total route length) cost per advancement; it is
not a new4096-unit frame-time measurement or a claim that all routing is O(1).

Seven actual Ghidra functions have saved Payoff140 annotations, mirrored in
`MapPathfinding.java` and authenticated by
`retail-route-reconstruction-ghidra-1.27.json`. The fresh strict `oracle-routes`
entry preserves its9216 synthetic coarse controls and requires the full producer
corpus, zero engine differences,200 adapter rows and all seven saved functions.
No live Frida witness is claimed: reconstruction has no runtime-only producer,
and the handoff uses read-only original-code entry observers. Buffer/public
advance boundaries remain ROUTE-01.2 and the existing ROUTE/SCHED tasks.

Focused debug and release Classic/TFT checks each pass656 tests /7655695
assertions per variant: WC3 pathfinding, shared routing, movement (including gate
traversal and saved continuations), and save/load. Both production and test
modules build;24 Python evidence/corpus checks pass. The fresh strict route
contract passes and all570 staged source/fixture pins agree. This is the fifth
implementation commit since the successful Payoff135 full-suite checkpoint;
no new full-repository suite is claimed.

## Retained search history and route capacity (Payoff141)

ROUTE-01.2's frozen public-advance oracle contains264 scenarios and12 fine
growth controls. The new comparison observes every reached unchanged
148100/162cb0 build: **452 fine and152 adaptive requests** match production C
at O0 and O2 in result, work, semantic node count and every route word; fine
obstruction also matches. HEAD kernels differ on82 fine and16 adaptive builds.
These are supplied terrain/registry/bucket/storage controls, not a complete
actual-engine public-advance or simulation-clock comparison.

A source outside the map makes setup return0 after resetting work/counts,
without replacing the prior source/goal/nearest node identities or cell epoch.
Both build routines still search. Move now preserves that history, including
old work limits and fine footprint/mask/target parameters. Resetting semantic
node count does not erase the initialized backing used by reconstruction.
The source heuristic uses the new requested integer coordinates. Same-cell
setup preserves history too. A denied request cannot publish a new profile.
Suppression belongs to the current caller; the retained target observation
identity is separate, and no caller's stack pointer is retained.

Two prior fine goals followed by source(-3.5,10.25) produce the original22/19
point nearest chains with count0/work1 and the new exact source endpoint.
The actual adapter fails six assertions before the fix and passes96 afterward.
A separate denial/profile regression preserves the prior small footprint when
a larger-footprint request is refused. Ordinary requests retain the compact
hash lookup; exceptional keys are allocated only when setup0 needs identities
whose node positions could later be overwritten.

Fine, adaptive and group point stores now reserve in the original128-point
quantum and retain capacity on shorter refills. The twelve actual engine
growth cases fail24 capacity assertions before the change; afterward their
127/128/129-point boundaries, retained128/256 capacities and shorter11-point
refills pass. The200 oblique adapter rows also check backing reuse. Capacity
is derived: Save123 ignores it in both field tables and rebuilds it from
validated logical payload counts. Three-buffer payload and incompatible-save
regressions cover the changed layout. Refill does no allocation within the
retained capacity; no new4096-unit frame-time result is claimed.

Public outside-source reachability is now established: two completing Frida
axis-setter runs and an observer-free control each record137 markers and the
exact positions(-112,328),(2248,328),(328,-16), corresponding to fine
(-3.5,10.25),(70.25,10.25),(10.25,-.5). The frozen
[summary](../../../tools/ghidra/fixtures/retail-outside-axis-1.27.json) and six
compressed trace/Preload files are checked by
[route012_summarize.py](../../../tools/frida/research/route012_summarize.py).
A separate exploratory Move from negativeX reaches coarse/fine setup0 but
stops progressing after fine setup; it has no completed Preload and certifies
neither arrival nor a whole movement trace. That interrupted capture remains
at `/GitHub/wc3-analysis/runtime/payoff141/live-observe-first.jsonl`.

Focused validation passes **660 tests /7,656,364 assertions** for each of
Classic/TFT at debug/release, plus27 Python evidence tests. This is the sixth
implementation commit since the successful Payoff135 full-suite checkpoint;
the full repository suite was not repeated here.

The completed evidence, focused engine checks and saved twelve-function Ghidra
readback live under `/GitHub/wc3-analysis/runtime/payoff141`. The strict
`oracle-routes` entry now includes these buffers, both C kernel comparisons,
saved annotations and completing live setter controls. **ROUTE-01.2 stays
open**: complete actual-engine public-advance state/return comparisons and
post-load invalid-start history still need integration; cold invalid sources
and live movement beyond the interrupted setup are not certified. No new
TODO leaf is introduced.

## Independent coarse admission and fine cache (Payoff142)

Move now retains the original one-point coarse destination cache when adaptive
search is disabled. Publication precedes fine admission, so an interval or work
refusal preserves that cache. An exhausted coarse index acquires its replacement
independently of the fine index: a still-valid fine leg is consumed without a
second search or work charge. Actual coarse progress continues to invalidate
that leg. A denied coarse request returns the stopped status at that visit,
without a second admission attempt through the fallback builder.

The research handoff needed a consumer correction. Its original harness left
`6fd54194` at zero; unchanged `167ae0` therefore selected the first predecessor.
Retail initializer `0040d0` writes ten through `Math_FromIntegerTruncated`.
The new [initialized fixture](../../../tools/ghidra/fixtures/retail-route-consumers-initialized-1.27.json)
executes that original initializer. The old264-row fixture remains explicit
zero-BSS consumer evidence; its reached search-kernel comparisons remain valid
for those supplied requests. Engine ten-unit selection is unchanged.

The literal [consumer regression](../../../games/warcraft-3/game/tests/retail_route_consumers.h)
exports208 ordinary scenarios across two maps and four footprint classes. It
runs production Move steering, comparing READY/STOP semantics with original
return0/2, every fine/coarse point, count/index/capacity, work, FIFO membership,
request timestamps, retry/delay and direction words. The256 advances include
48 following controls; those48 are also checked after actual Save123 reload.
Native packed flags without an engine counterpart are not asserted by this
adapter, and the test seam does not pretend its internal enum is original EAX.

The pre-fix engine compiled separately against the corrected literal fixture
fails400 assertions; the fixed consumer passes20,292, including all48 saved
continuations. Initial zero-threshold expectations and one intermediate coarse-denial test
setup are retained as rejected development inputs. The final original run and
repeat each match264 scenarios/12 growth cases; all452 fine and152 adaptive
builds remain C-exact. Four new/extended Ghidra function annotations, including
the real initializer, are saved and read back. Evidence and focused logs live
under `/GitHub/wc3-analysis/runtime/payoff142`.

ROUTE-01.2 remains open for complete invalid-start consumer outcomes and
post-load initialized-search history. This chunk removes redundant work and
preserves retained state; it does not claim a new4096-unit timing result or
close the remaining retail-fidelity gaps.

Focused validation passes **661 tests /7,676,656 assertions** per Classic/TFT
configuration at debug/release, plus28 Python evidence tests. The fresh strict
`oracle-routes` report includes both archived controlled buffers and corrected
initialized consumers, their kernel comparisons, literal checks and saved
annotations. This is the seventh implementation commit since the successful
Payoff135 full-suite checkpoint; the full repository suite was not repeated.

## Invalid-start consumers and fresh search owners after load (Payoff143)

ROUTE-01.2 now covers the remaining56 initialized invalid-start scenarios,
including112 complete physical-member decisions. The unchanged original
`165ae0` runs with the actual coarse-distance initializer and explicit owner
PRNG input `12345678/0`. The engine test runs the real registered group member,
steering, step collector, blocker resolver and retry owner. Fine/coarse point
words, retained128-point capacities, indices, request times, work, delay/retry
and both raw RNG words agree. Outside negative/beyond-X visits stop, erase only
the fine leg and draw once each admitted visit; outside-Y visits retain the
previous chain without a draw. Blocked starts use the normal fine search.

Save/load required a separate engine fix. Ghidra virtual-table inspection shows
fine payload methods `148210/1481e0` serialize only the map handle and ushort
search stamp. Adaptive methods `162fd0/162da0` serialize map handles, warp table
and marker count. Neither saves query nodes, source/nearest identities or work
limit. Two complete UI-save/load Frida repeats observe new fine/adaptive owners
with source/nearest `ffffffff`, zero nodes and zero budget immediately after
load. The observer-free UI save/load control checks every unique public position
sample through tick300; repeated samples must agree with their original tick.

OpenRealm had preserved whichever query preceded `ReadGame`. Seven failing
regression assertions reproduce this unsaved history; `ReadGame` now releases
derived routing/search caches before reading the saved hierarchy and spatial
state. The saved unit-owned curves remain immediately usable without an extra
search. No saved layout changes. This is a load boundary operation and does not
add per-frame rebuilding or change creation/movement scheduling.

Artifacts: `retail-route-invalid-consumers-1.27.json` (56 frozen cases and actual
constructor controls), `retail_route_invalid_consumers.h` (literal expectations),
`retail-route-invalid-ghidra-1.27.json` (seven saved/read-back functions), and
`retail-route-search-load-1.27.json` plus its six compressed capture/public files.
Reproducers are `verify_route012_invalid_consumers.py`,
`export_route012_invalid_consumers.py --check` and
`route012_search_load_summarize.py`; the strict routes corpus invokes them.
Runtime reports: `/GitHub/wc3-analysis/runtime/payoff143/`.

Together Payoffs141–143 cover empty/partial/denied and unsigned index states,
initialized invalid starts, exact following advances,128→256 growth and retained
storage. Kernel comparisons alone were insufficient; the actual consumer and
load lifecycle are now covered. The cold out-of-map case with no preceding
accepted query has no established safe native return; this work does not claim
retail crash/hang emulation or full physical recovery. Impossible supplied
indices remain labelled consumer controls, not retail-produced states.

Validation: Classic and TFT each pass663 affected debug tests/7,681,648
assertions. Final warning-free debug/release modules each pass316 targeted
routing/save/consumer tests per edition (2,791,396 assertions), plus the sound
fixture check that exposed an undersized test handle. Thirty Python checks,
367 corpus entries/610 pinned files and a fresh strict routes report pass.
Release production `libgame.so` builds. Full repository validation was last run
at Payoff135; this is implementation commit8 toward the next12-commit checkpoint.
No constructor, frame-time or pathfinding-performance target is claimed here.

## Mixed crowds, individual physical owners and adjusted retries (Payoff144)

SEP-04.2 and SEP-04.3 now exercise complete real engine scenes through JASS
creation, orders, the primary clock, physical-owner scheduling, routing,
separation and public Stop. The fixtures retain the original scripts and map
cells rather than calling arithmetic helpers with captured outputs as inputs.

The mixed crowd includes ten units, two owners, three radii, three ranks,
three selectors and a repulse-disabled blocker against a terrain wall. Its
2,255 owner visits, 3,228 ordered neighbor contributions and 76 retries match
retail exactly. The ground control includes four enabled and four disabled
units, an unreachable closed ring, a reachable two-cell gap and Stop. Its
1,336 visits, 1,182 contributions and 66 retries match exactly. Comparisons
cover fine pose, vector, policy/cooldown, shared RNG, occupied rectangle,
attempted endpoint and admission. All 3,100 public JASS samples also match
order/owner exactly and four-decimal positions within 0.00015 world units.
Disabled units have no separation source but still block ordinary routing.
Candidate owner/rank filtering does not filter collision admission.

Three general contracts explain the failing regressions:

- Individual accepted point orders create physical singleton owners. Retail
  visits them newest first; the old engine entity walk reversed retry/RNG
  encounters. Each singleton now schedules its existing unit-owned route,
  while selected packets and captain cohorts retain staged group execution.
  Preparing a selected packet suppresses its provisional ordinary singleton;
  independent nested point orders retain their own admission. Direct unit
  think calls remain usable; scheduled frames execute each owner once.
  Move→Follow transfers its physical member before admitting the successor;
  switching within the same ability procedure does not necessarily invoke
  leave. The former owner remains empty until its ordinary retirement visit.
- `166c30` publishes the reconstructed coarse endpoint at path+24 independently
  of coarse index. Both fine-blocker and endpoint retries measure this same
  adjusted goal. At ground owner1083/unit4, source `(10.0289,12.6061)` fine,
  adjusted goal `(17,13)` and first coarse point `(8.5,6.5)` decrement2→1
  without RNG. Using the public click `(22.5,12.5)` spuriously draws6/7.
  The retained first coarse point supplies the existing exact scalar words;
  no additional adjusted-goal cache is introduced.
- `16fbd0` tests arrival/held before path advancement. An in-range member still
  turning must stop and unlink requests without another retry. This fixes the
  extra retry after ground leg2 unit2's terminal4 and applies to shared groups
  as well as ordinary movement.

Four `wc3_repulsion_crowds` regressions repeat both complete scenes, including
save/load mid-order (mixed6995ms, ground12995ms), for352,118 assertions. The
saved suffix agrees with the uninterrupted captures; this does not establish
universal retail spatial reinsertion order. Save format124 stores the physical
owner's `individual` execution mode, rejects invalid boolean/multi-member/
target/shared private owners and rejects the prior format.

`verify_wc3_pathing_crowds.py` freshly executes unchanged original separation
bodies from both handoff repeats, checks frozen sequences and observer-free
controls, and verifies a further pair of read-only physical-owner/retry-input
captures (2,135 normalized rows,1,616 public markers each). These snapshots show
eight singleton owners, units7..0 at owner1061, and the adjusted retry source
above. Saved Ghidra readback contains seven relevant functions; mapper comments
reproduce the evidence. The early `ground-owners-first` attempt was incomplete
because its observer dereferenced a retired null mover; complete null-safe
repeats supersede it. No observed game state was written by the extra observer.

Portable inputs and consumer expectations are
`retail-separation-crowd-inputs-1.27.json.gz`,
`retail-separation-crowds-1.27.json.gz` and
`retail-crowd-owner-live-1.27.json.gz`. Generate/check the literal engine header
with `tools/ghidra/research/export_separation_crowds.py --check`. Reproduce the
original checks with:

```sh
python tools/ghidra/verify_wc3_pathing_crowds.py \
  --binary /path/to/1.27.1.7085/game.dll --report /tmp/crowds-fresh.json
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test 'wc3_repulsion_crowds.*'
```

These closures cover composed displacement and enabled/disabled ground
controls. First blocking-object identity remains inferred from overlaps, and
four mixed-crowd blocked orders remain unfinished at tick302. Startup seed
production, remaining route producers and other open leaves are not certified
by supplying the captured initial RNG/parity to these tests. See the original
[SEP-04.2 handoff](retail-pathfinding-handoffs/SEP-04.2/HANDOFF.md) and
[SEP-04.3 handoff](retail-pathfinding-handoffs/SEP-04.3/HANDOFF.md).

Final targeted validation:1,038 test executions /6,604,212 assertions pass across
Classic/TFT debug/release. This includes both crowd/save suffix scenes, the
save suite and related Follow/captain/selected-pair cases; debug also covers both
pathfinding suites. The broader351-test Classic movement sweep preserves all
captured motion/RNG checks. Its three failures were one stale route-owner
assertion (physical singleton route lives on its unit); that corrected test
passes in all four configurations. Other test updates remove assumptions of
no ordinary physical owner or a permanently occupied group slot0, without
changing frozen numerical expectations. Builds are warning-free and the fresh
`oracle-crowds` corpus result is verified. Full repository validation remains
on the agreed approximately12-chunk cadence.


## Ground support refresh state (Payoff145)

Move now preserves the support refresh order recovered from original684480 and
66d780. All ground movement profiles, including FLOAT, accept a higher walkable
deck. AMPH selects the water maximum using the **previous** deep-water flag;
only afterward does refresh publish the current terrain classification. Deep
water is walk02 blocked and float40 clear, independently of bridge regions.
The engine reads one terrain byte rather than repeating two coordinate queries.

`S_RefreshUnitSupport` distinguishes ordinary physics from explicit commits.
An ordinary update queries height only if either coordinate changed by at least
.01; otherwise it retains height and flags while updating its sampled XY.
`M_CheckGround` forces a query for explicit setters, initialization and accepted
pose commits. This removes repeated terrain/deck scans for unchanged idle units
and preserves the first amphibious entry height until a real refresh. Cached XY,
validity and support flags survive the normal edict save (format125).

Tests first reproduced entry/departure lag, a save between the two refreshes,
ordinary idle physics after an axis writer, and FLOAT deck support. The former
boat test lacked a nonzero destructable identity, so its bridge registration was
silently rejected; the corrected fixture asserts actual surface publication.
The literal native ground matrix supplies48 original66d780 results for six
profiles, prior deep state, planar deck/no-deck and zero/nonzero fly offset. Each
is executed against all256 terrain bytes through production refreshes; expected
heights are exported from original-code execution, not calculated in the test.

A fresh Frida authored-map capture records3171 refreshes:1986 queries and1185
cached updates. Every query/no-query decision, cached XY/Z publication and
post-query deck/deep write agrees with the recovered contract. Its193 JASS
markers equal the prior observer-free control. The original support oracle
passes8068 cases. The portable capture and saved three-function Ghidra readback
are audited by `verify_wc3_pathing_support.py`; the native oracle now preserves
all rows and exits unsuccessfully on any mismatch.

**MAP-02.2 remains open.** The ground matrix supplies planar geometry and
integral water heights; it does not certify the current deck bounding-box
approximation, exact terrain/water sampling, structure multi-sampling or the
alternate flyer-height field. `GetLocationZ` also still uses terrain alone.
Those gaps require actual producer/geometry integration, not invented map-specific
height constants. See the [original handoff](retail-pathfinding-handoffs/MAP-02.2/HANDOFF.md).

Final focused validation passes2,248 test executions/10,906,462
assertions across Classic/TFT debug/release: support, saves, destructables, unit
lifecycle and Ensnare, with both optimized351-test movement sweeps. Production
and test builds are warning-free. The new `oracle-support` corpus entry passes,
and the exact staged inventory verifies369 entries/638 pins. Full repository
validation remains on the agreed approximately12-implementation-commit cadence;
this is the tenth implementation chunk since the Payoff135 checkpoint.

## Fine exclusions retain captured record counters

Payoff146 closes MAP-04.1. The fine request now increments its captured self and
target record counters separately and restores target before self. An alias is
incremented twice; an outer counter remains held. This replaces a query-local
boolean that failed to represent nesting and did not suppress hierarchy
participants. A null self also no longer disables target and bystander queries.

All45 supplied original spatial scenarios match six complete counter/fine-window
boundaries, five full hierarchy snapshots and final fine/coarse route words.
The new fine regression fails2,511 assertions before the change. Actual public
rectangle/pending-edit coverage remains Payoff130; these asymmetric rectangles,
category06 and outer counters are explicitly original oracle inputs, not a new
claim about public construction producers. MAP-04.2 still owns exit/recovery
composition. See [scope evidence](retail-pathfinding-exclusions.md#complete-fine-and-coarse-scope-matrix-payoff146).


## Zero-speed units retain independent separation owners

Payoff147 advances SEP-01.2 without closing its remaining counted-suppression and Mechanical Critter producer scope.
Retail's complete `66fc50` predicate reads authored `repulse`, channel-work bit20.40000000, signed suppression depth198,
suspend latch5c.100000, signed suspension depth54 and scripted pause5c.200000. It does not inspect authored movement
speed or whether the unit has an authored Move ability. Success returns literal1 at66fc88; the handoff's description
of returning the raw authored integer is incorrect. The unmodified predicate and original rawcode-cache lookup pass
432 cases including negative counter words and nonboolean authored integers.

The integer-authored `RS-SEP-01.2-immobile147b` map creates twelve units in six independent clusters, including
zero-speed ground/fly repulsors, ordinary moving-capable partners, an owner-transfer/pause/resume case and a disabled
control. Both read-only Frida captures match all859 JASS markers and normalized configuration/update sequences;
the observer-free control matches the same859 markers. Their2552 complete visits contain532 ordered neighbor calls;
every retained pair/tail replays exactly through the original code. These are actual public creation witnesses,
separate from the predicate oracle's supplied unit/cache inputs. The first capture never completed and is retained
as a failed experiment; the earlier real-tagged speed map is superseded by the integer-authored map.

Move incorrectly used `M_UnitMoveDisabled` to reject repulsor creation. Remove that condition while retaining the
existing pause/channel eligibility. The public CreateUnit→SetUnitOwner→PauseUnit→resume→save/load regression fails
ten checks before the fix and then passes all22 checks. Both editions pass the six policy tests/189 assertions.
No per-type exception, save-layout change or delayed state allocation is introduced. Public speed-zero units retain
separation membership even though ordinary movement remains unavailable. Complete engine replay of the captured
owner clocks and counted-suppression/Mechanical Critter lifetimes remains open; marker equality is retail observer
control, not a claim that the engine matches every captured public callback deadline.

Evidence: `tools/ghidra/fixtures/retail-separation-eligibility-1.27.json`,
`retail-repulsion-immobile-1.27.json.gz` and saved `retail-repulsion-eligibility-ghidra-1.27.json`;
`oracle-repulsion-policy` now re-executes the complete predicate and both captured arithmetic histories.
The existing thirteen-function Ghidra snapshot remains a historical prefix; new annotations are separately saved
and appended to `MapPathfinding.java`. Runtime captures/reports and failing/passing engine logs are under
`/GitHub/wc3-analysis/runtime/payoff147/`.

The batch checkpoint also exposed a stale near-home CaptainGoHome test. It
counted every group allocated during recruitment, although Payoff144 correctly
transfers the temporary point owner to a distinct Follow owner. The failure
reproduces with the pre-Payoff147 library (sequence0→2, live owner2). The
regression now records the recruited owner and issues two public GoHome calls;
both must retain that owner, actor pose and roster request without allocating
another group. No production captain behavior was changed to satisfy the test.

The suppression follow-up is saved at688d90/6785c0 and mirrored in the
mapper/readback: RTTI-backed ItemTeleport/ItemTownPortal and Channel work,
Neutral/Orc/Naga construction and EntangleCargo all call the same counted
producer with a unit receiver. Raw decompilations, caller/table observations
and saved readback are retained in the runtime directory. These observations
prepare the remaining SEP-01.2 ability integrations; they do not establish
public lifecycle parity or justify treating suppression as pause-only.

Payoff147 is the twelfth implementation commit after the Payoff135 full-suite
checkpoint. The original `make test` invocation completed the24 unaffected
JUnit suites (1,222 tests), but failed on the stale captain assertion and22
Python corpus checks reading concurrently changed research type hashes.
After correction, `make -j6 test-wc3-engine` passes both Classic and TFT:
2,840 tests and10,985,125 assertions each. The immutable staged snapshot passes
all764 Python tests; its one missing-build skip is resolved by the compiled
journal module (eight tests, no skips). The final mapping changes pass the
seven policy and22 corpus tests, and fresh `oracle-repulsion-policy` passes.
`checkpoint-final.json` and all logs remain under the runtime directory above.
This is a recovered full batch checkpoint, not a claim that the initially
failed umbrella invocation exited successfully. Focused validation resumes
for the next implementation batch. SEP-01.2 remains open;96 TODOs remain.


## Fine consumers retain counted self scopes

Payoff148 replaces endpoint, waypoint and blocker-collection self overlays with
balanced holds of the actual pooled record. Nested depths survive; targets stay
live and null-self collectors still emit terrain blockers. The 96 complete
initialized original consumer cases and observer-free controls drive the engine
regression, following 72 scope failures and 20 null-self failures. Fine-request
exact/partial/denied exits also retain counters and unchanged hierarchy state.
No persistent layout, allocation or map scan is added. MAP-04.2 remains open for
resolver/recovery/portal/group lifecycle composition; see
[consumer scope evidence](retail-pathfinding-exclusions.md#fine-consumers-hold-captured-self-through-their-queries-payoff148).

## Target ability classes retain their group gate policy

Payoff149 captures Cargo Drop class ownership into new approach/Follow groups,
then carries bit10 through group coarse routing. Custom aliases participate;
Meat Drop and Tank Drop Pilot do not inherit the policy from their shared engine
procedure. Two public retail repeats agree on1,743 semantic records and406
observer-free markers; four native coarse requests expose warp1/0/1/1 and
matching gate-marker presence. Both-edition focused regressions, saved group
policy and the existing Follow trajectory pass. FORM-01.3 and the wider flying
Follow producer remain open. See [target gate policy](retail-pathfinding-target-warp.md)
for raw evidence, reproducers, failed attempts and exact scope.

## Persistent groups match visible moving targets

Payoff150 implements the original target-speed commit policy and retains hidden
target sampling across save/load. See [group speed and visibility](retail-pathfinding-group-speed.md)
for exact guards, failing-first regressions, native oracle and reused Frida evidence.
Attack-owned shared-cap exemptions and broader target-loss policies remain open.


## Attack cap exemption and exact primary requests (Payoff151)

Attack owns its three-second exemption, pre-transform damage notification
(including zero damage), automatic acquisition and indexed deadline heap. Move
consumes it for shared cap, classification cooldown and regroup advancement.
The original remaining virtual18 corrects the rearm interpretation: at least
half a second elapsed. Save127 retains the deadline and serial; exact primary
request merging preserves owner ordering and fire-before-rebase. An actual
Move-cohort hit regression failed before implementation, then passes with
exact native velocity words and retained public Move. Original84 timer states,
72 notification-prefix states and1296 consumer calls are frozen;72 regroup
cases run through engine advancement. Fresh read-only retail repeats and an
observer-free control agree on89 markers and8 zero-damage notifications.
See [the implementation and limits](retail-pathfinding-group-speed.md#attack-owned-cap-exemption-payoff151).
GROUP-03.2 stays open for remaining producers/guards/public domains; no new
child TODOs. Full-suite cadence remains the Payoff147 checkpoint.

Focused Classic/TFT validation passes1,498 test executions/10,489,856 assertions
across movement, combat, save, timer callbacks and Chaos lifecycle. Seven new
Python evidence regressions and22 corpus checks pass. The staged inventory has
374 entries/689 pins; four freshly executed original/live contracts pass.

## Ordered ally help reaches moving peers (Payoff152)

A damaged unit now broadcasts help before positive-damage rejection, including
zero damage. Nearby eligible peers receive Attack's speed-cap exemption while
retaining their explicit orders. This fixes the delivered GROUP-03.2 unhit-peer
regression; the broadcasting victim need not own an Attack ability.

Retail `66e700` reads committed world position and selects `688060`'s radius.
The normal radius is authored `Misc/CallForHelp`; neutral owners select
`CreepCallForHelp` first. Query tokens `b/17/16/1f` intersect the victim's
HelpRequest mask with each helper's HelpResponse mask. Attack `49bb50` then
checks its engagement state and whether the source is an enemy. The enemy check
reads diplomacy directly, without the engine spell helper's extra enabled-slot
gate. Own-owner help permissions start enabled and remain explicitly revocable.

The spatial predicate is **not center distance alone**. Token `b` selects
`04c5a0 -> 05f230 -> 05ce60`, comparing retained mover positions against
`(query radius + candidate collision radius)^2`, with software scalar operations
and inclusive equality. The center-only `05d680` belongs to the other query
mode. The native helper at `(976,880)` qualifies for a source at `(800,800)` with
query radius173 and collision31; a center-only port incorrectly excludes it.
This discrepancy was reproduced before correcting the engine predicate.

Widget queries `05f010` scan X before Y and preserve newest effective insertion
and first-cell deduplication. Separation retains its verified Y/X traversal.
Move shares the existing proximity records between both consumers and selects
the appropriate traversal order. A two-cell production-damage regression fails
with row-first help and passes with column-first help. Candidate identities are
collected before ability dispatch; reusable buffers support nested queries
without overwriting the outer collection or mutating its active stamp traversal.
The query visits intersecting cells and candidates rather than scanning all
entities. No second spatial index is introduced.

Ordinary units arm one three-second source suppression request **after** all
recipient callbacks. The indexed Attack timer heap now contains cap and help
requests, retaining O(1) earliest lookup and O(log n) insertion/cancellation.
Save128 retains both requests' independent active/deadline/serial state and
rebuilds derived heap membership; Save127 and earlier layouts are rejected.
The actual zero-damage regression checks authored radius173, native recipient
order1/6/5/0, retained heads, an unarmed victim, suppression, saved restoration,
expiry and removal. Same-owner permission revocation has its own regression.

Fresh read-only `attack_alert152_probe.j` captures test all four directional
permission combinations with the opposite directions held enabled. Both
observations agree on209 markers and one helper admission; the observer-free
control has identical public markers. The native original-code callback oracle
executes192 constructed states through real owner, diplomacy, canonical mask
resolution and packet construction:12 reach notification and one reaches the
exemption entry. This is not192 engine comparisons or public guard reachability.

`attack_help152_probe.j` supplies map-local radii173/229. Two observed repeats,
a third pose observer and its repeat, and an observer-free control agree on190
public markers. The pose repeats preserve every relevant help/candidate/ally/
gate/notification/cap record, including canonical fine words. Six queries
produce30 candidate callbacks. Ordinary admissions occur at ticks10/41 and
110/141; neutral AI-owned admissions60/89 use0.5 seconds. Lookup counts outside
the help chain vary with wall-time execution and are not gameplay assertions.
The first friendly-source scene and the initial map with reserved JASS local
name `code` remain failed/negative artifacts, excluded from acceptance.

AI policy is recorded but not claimed implemented: native unit5c bit4 is set by
Town AI enrollment `9b9230`, not by Hero/building classification. It selects900
for non-neutral AI-owned sources and0.5-second suppression; neutral owners still
read the authored creep radius. AI self notification and `69c810` propagation,
full enrollment/removal producers, remaining acquisition/engagement guards and
packet-kind policies remain within GROUP-03.2. The ordinary source implementation
currently uses the three-second request; the neutral live timeline is evidence
for that remaining implementation, not an engine parity claim.

Payoff152 also corrects Payoff151's notification guard oracle. Original4935e0
reads unit**+5c** bits100000/200000; the old fixture wrote**+20** and therefore
failed to exercise those guards. Regenerating the72 cases changes four accepted
outcomes. The84 timer and1,296 group-consumer results are unchanged. The old
Ghidra snapshot remains historical; current annotations, the corrected fixture
and the new saved readback record the correction explicitly.

Validation: focused Classic/TFT movement, proximity, combat, save, timer and
Chaos checks pass1,534 test executions/10,516,718 assertions. Production and test
game targets build. Fifteen Python attack-evidence checks pass, including the
new negative controls. Twenty-two corpus tests and all three new fresh corpus
entries pass; the manifest contains377 entries and705 source/fixture pins. The
last candidate-mask/generation checks pass14 targeted Classic/TFT executions.
Ghidra saves16 function annotations, the corrected guard
comment and both native constant initializers; `MapPathfinding.java` mirrors the
new evidence. GROUP-03.2 stays open; no extra TODOs were created.


## Town AI enrollment selects the help policy (Payoff153)

The help policy belongs to retained Town AI membership, independently of the
private AI script VM. Original `1e9a40` creates Town AI for computer players whose
slot state is playing, and always for Neutral Aggressive. `26b0d0` retains its
canonical identity in player `2d4/2d8`. Eligible units join through `9b9230`, which
sets unit `5c.4`; targeted-as wards and dead units are excluded. Engine setup
publishes configured membership before map `main`, and public creation consumes
it without scanning the player roster. The one initialization pass also admits
preplaced units without changing their owned sequence. Non-unit widgets are
excluded even though their immutable data-row pointers are non-null.

`698ce0` performs a genuine owner transfer after the owner event: disable ability
AI mode, clear the membership bit and enroll with the new owner. Same-owner calls
do not perform those transitions. Starting or stopping an AI script does not
change the retained membership policy. Engine `AI_TOWN_OWNED` is distinct from
`ai_vm_initialized`; Save129 retains the availability mask and unit flags. An
already armed suppression request keeps its deadline and serial across transfer
and restoration. The saved availability mask rejects unsupported player bits;
Save128 and older layouts are rejected without migration.

Attack now selects initialized900 radius for an enrolled non-neutral victim,
otherwise authored CallForHelp. Neutral owners first select authored
CreepCallForHelp, regardless of membership. Suppression is0.5 seconds for enrolled
victims and3 seconds otherwise. AI damage delivers the self ally-alert before
the ordinary damage notification even during help suppression; an admitted AI
help query can additionally include the victim among its ordered recipients.
The existing indexed timer heap and spatial collector remain the owners of
these operations; no per-hit roster scan or per-unit timer allocation was added.

The new public retail scene contains eight creation/script/transfer/neutral
stages and48 zero-damage hits. Two complete read-only Frida repeats preserve
377 public markers,18 radius/arm pairs,30 candidates,57 ally notifications,
six enrollments and three owner calls. Their full normalized help streams match;
an observer-free control reproduces all public markers. With authored radii
173/229, computer0 uses900/0.5 before and after script startup, transfer to2
uses173/3, and transfer back restores900/0.5. Neutral12 uses229/0.5 while15
uses229/3. Genuine transfer during suppression retains the existing deadline.
A negative map control whose player0 remains neutral does not gain membership
merely by starting a script; that single capture is retained as supporting
negative evidence, not counted as a repeated acceptance witness.

Engine regressions were added first: the computer enrollment/transfer/save
case failed8 assertions and the neutral case failed3 before the port. A further
preplaced-unit/scenery boundary regression failed2 assertions before the unit
classification gate was corrected. All three now pass. The Classic/TFT bot,
movement, combat, save, map-script and Chaos suites pass1,818 test executions
with10,491,566 assertions. Production and test modules build. The22 help/exemption
Python checks pass; including inventory/metadata checks,52 Python tests pass.
Four fresh strict contracts (the original192-case ally oracle and three complete
retail repeat/control verifiers) pass. These are
focused checks under the requested12-commit full-suite cadence, not a full
repository validation claim.

Ghidra saves nine annotated functions plus membership, AI-record and player
factory fields. `MapPathfinding.java` and the type schema preserve the same
annotations; the readback fixture includes original assembly and producer/map
hashes. The lossless capture fixture supplies repeat/control verifier regressions.
The capture wrapper records its unused generic map-builder hash; the actual
153 builder, probe, AI asset and changed map members are independently pinned.

GROUP-03.2 remains open. The captured enrollment flag and AI record are distinct
native state; the wider AI-record lifecycle, per-ability AI-mode callbacks,
revive/temporary-captain and special re-enrollment producers, propagated `90/94`
counters, Town/captain reactions, engagement guards and remaining exemption
producers are not certified by these controls. Native player slot-state checks
are instruction-backed; the public scene covers configured computer0 and
neutral2/12/15, not all lobby or runtime controller mutations.
