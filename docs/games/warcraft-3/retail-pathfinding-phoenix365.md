# Phoenix fatal admission and Morph ownership

Phoenix365 implements another public producer inside existing SEP-01.2. It does
not close that leaf or add a TODO. The retail binary is the corpus-pinned
1.27.1.7085 `game.dll`.

The complete guarded death admission calls the Phoenix interceptor before the
unit death flag is committed. Low-life regeneration, public `KillUnit`, and
lethal `UnitDamageTarget` all start the private Phoenix morph order. The original
unit retains its identity and remains alive to public `IsUnitType(..., DEAD)`;
its life can be zero during the forward transition, and a registered death
trigger does not fire. Converting an already dead unit back to life would expose
a different callback and target-lifetime sequence.

The forward transition holds separation through the common Morph owner and
acquires independent invulnerability and spell-immunity counts. The authored
forward delay is `Dur + DataC`; stock values are 0.5 + 0.5 seconds. Completion
rebinds to the egg endpoint, restores maximum life, and releases those
contributions. Positive `HeroDur` owns the egg lifetime, with stock 10 seconds.
Expiration rebinds to the base immediately and starts the reverse Morph hold.
That direction acquires spell immunity without an invulnerability contribution;
its completion after `Dur` releases the contribution and restores maximum life.
Both transitions preserve independent owners rather than assigning booleans.

`Phoenix365Evidence.java` saved descriptive names and comments in Ghidra.
A second read-back exactly matches the saved export. The evidence guards 1021
instructions across 20 complete functions, including the pre-death dispatcher,
Phoenix veto, directional dispatch, subtype methods, shared Morph timer and
counter helpers. `MapPathfinding.java` records the six recovered entry points.

Two independently observed runs and one unhooked control cover each of the
stock timing map and a map adding explicit alive/death-event observations.
Together they preserve 1204 public markers and 1618 native events across six
scenes. The strict verifier reconstructs both maps, checks every original byte
guard and complete normalized event stream, and rejects changed capture
provenance, missing repeats, instrumented controls and incomplete runs. The
historical source files in capture metadata remain checked as captured.

Before implementation, the tightened production regression failed 40 of 96
assertions in each edition. It covers both AbilityData field schemas, all three
fatal admission sources, nonstock endpoints and durations, independent
invulnerability/immunity contributions, saves during the forward transition
and egg lifetime, and reverse completion with the original identity. Those red
logs are retained in the external `SEP-01.2/phoenix365` research directory.
The strict verifier passes 20 production tests and 3432 assertions in each
edition, including 330 Phoenix assertions. Fourteen evidence-integrity tests
also pass. Semantic save format206 persists the live-at-zero-life interceptor.

The first implementation check exposed an invalid RoC test fixture: rank1
DataB/C had been spelled `Data21`/`Data31`, which address later ranks of DataA.
The stock schema uses `Data12`/`Data13`. A debugger confirmed the TFT row had
DataC0.125 while the invalid RoC row had zero, explaining all18 timing failures.
Those diagnostic logs remain preserved; correcting the headers required no
engine behavior change. This fixture error does not replace the earlier
40 missing-owner failures.

The final isolated Save206 production and test modules build successfully. Both
Classic and TFT pass the20-test/3,432-assertion strict gate and all223 save tests
(67,987 assertions per edition). Fourteen evidence integrity checks,38 corpus
checks and12 exclusion-owner checks pass.
