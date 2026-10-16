# Public timed-life factory and movement admission

Payoff156 implements the public timed-life factory/lifetime slice of
[GROUP-03.4.6.2.1.2](retail-pathfinding-todo.md). The broader task remains open.

## Contract and ownership

`UnitApplyTimedLife` creates an independent buff on every application. Repeated
applications, including the same class, do not replace the earlier expiry.
Seven class IDs select specialized factories: BUan, BFig, BEfn, Bhwd, Bplg,
Brai and BHwe. Other IDs select BTLF; the requested unknown ID is not published
as an ability. Public class-level lookup is exact, while captain temporary-unit
classification recognizes the common timed-life ancestry.

The native marks the unit summoned, removes food-used participation without
changing food-made, and publishes the buff before returning. Summoned identity
persists after ordinary death. BUan, BFig and BHwe select removal after the
Death animation instead of an ordinary corpse window. BFig is **CBuffFigurine**,
not CBuffInfernal: live vtable b56f90 and RTTI disambiguate the classes.

Positive durations arm expiry; zero/negative durations create permanent markers.
Pausing retains each existing finite record's remaining scalar duration;
resuming rearms it. Removing the class kills immediately. Death/removal releases
all records and requests; a reused edict cannot inherit an expiry.

Move's captain range predicate now consumes all these class records. Its early
50-unit range remains independent of the retained physical range of an already
admitted member. Automatic Town AI enrollment is a separate, still-open producer.

## Implementation and scaling

The ability owns optional records in address-stable blocks of 64, per-unit
encounter lists and a growable deadline/registration-serial min-heap. Creation,
cancellation and expiry cost O(log N); finding the next deadline costs O(1).
There is no all-entity frame scan and no allocation from the script timer pool.
Exact class lookup/pause/removal traverse that unit's records, not all units.
Records are cleared on death, edict release and map/pool reset.

Save131 writes logical records, owning-edict indexes, deadlines, registration
serials and paused remaining durations. Pointer values, heap positions and pool
backing are derived and rebuilt. Save130 and older layouts are rejected.

## Original evidence

| Entry | Original contract |
| --- | --- |
| 2185e0 | Cdecl UnitApplyTimedLife; factory first, food-used inverse afterward |
| 48b930 | Unit receiver; class dispatch, virtual31c initialization, then attachment |
| 530750 | Common initializer; Unit248 bit4, notification d01a1, corpse-policy virtual360 |
| 52fe50 | Positive-duration progress clock versus permanent initialization |
| 532b50 | Embedded EventClock at buff+84, event d01c4, retained receiver |
| 5310f0 | Expiry event removes the buff through the Unit owner |
| 610650 | Town lookup and automatic temporary-captain enrollment; not integrated here |
| 2157d0 | SetUnitUseFood registration2092d6, authored food-used delta and flags64.300 |

Ghidra saves these functions, seven class getters, the partial timed-life layout
and explicit initializer/scheduler/native ABIs. `MapPathfinding.java` and the
canonical type fixture reproduce the annotations. Readback is under
`/GitHub/wc3-analysis/runtime/payoff156/types-report.json`.

Two completed Frida captures and an observer-free control reproduce **1121
public markers** exactly. Each observed run contains 20 factories, 20 common
initializations and 20 factory-owner returns. Frozen words and provenance are in
`tools/ghidra/fixtures/retail-timed-life-live-1.27.json`; the strict verifier rejects
missing completion, changed producers, incorrect event counts and changed pins.
Captures/maps are archived in `runtime/timed-life156` under the analysis report root.

## Regression coverage

The initial production regression failed 47 assertions against the committed
stub. Actual JASS creation now tests eight classes, unknown fallback, food and
captain ranges with non-stock authored values and save/load. Separate tests cover
permanent/short/duplicate applications, pause/resume, explicit removal and 4096
units plus 64 duplicate records without consuming script timer slots.

The complete retail marker timeline runs through normal server frames, including
a save/load while a buff is paused and duplicate classes coexist. Test rawcode
reporting normalizes the engine's byte-packed IDs to retail's printed integer
spelling; class-level and gameplay assertions are unchanged. The minimal fixture
uses Footman's exact Death interval **21333..24367**, extracted with mdxtool from
retail SEQS metadata, and its ordinary corpse policy. A stand-only model cannot
verify corpse removal, and rounding that interval to 3000 ms changes one sampled
tick. The test advances the ordinary FRAMETIME presentation cadence; the primary
clock still advances through its own 5-ms steps.

## Remaining fidelity work

This slice does not certify automatic captain enrollment, BUan/Bplg subclass
invulnerability/ability-state effects, generic buff-count/removal composition,
or whole movement trajectories. The original embedded EventClock segments long
durations into 120-second requests. The new owner currently uses direct deadlines;
long-duration segmentation and its pause/epoch compositions still require porting
before the full native lifetime contract is considered complete. The bounded
2/30-second evidence must not be presented as universal duration coverage.
