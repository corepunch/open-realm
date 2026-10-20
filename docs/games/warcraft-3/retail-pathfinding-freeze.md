# Freeze-family availability and acquisition (Group359)

Orb of Freezing `AIzb` creates `CBuffFreeze` (`Bfre`). Freezing Breath `Afrz`
is a passive structure-hit ability that creates `CBuffFreezingBreath` (`Bfrz`);
the previous engine registry incorrectly treated it as active Frost Nova.
Both buffs disable attached ability execution, so a frozen candidate loses its
armed acquisition bit before the defending-priority override is considered.

The original common Stun initializer `530570` has different parameters for the
public producers. Freeze passes `1,0,0,1`, increments Unit `1c4`, and increments
attached ability `3c` and `40`. Breath passes `0,0,0,1`, increments Unit `1b8`
and ability `3c`, and leaves the order counters unchanged. Removal through
`52d050` reverses the same contribution. Public source removal preserves either
target buff. Freeze duration zero remains permanent until removal. The engine
now retains these counted owners, custom BuffID dispatch, independent Pause
ownership, timed expiry and cold-save inverses. Save204 adds the non-order
aggregate; each attached ability already saves its own counters.

The complete primary-vtable inventory follows RTTI inheritance and finds724
`CAbility` descendants:719 default-false virtual188 entries, four shared true
entries (`CBuffStun`, `CBuffFreeze`, `CBuffSleepStun`, `CBuffFreezingBreath`) and
`CBuffMagicLeashTarget`'s separate true implementation. Generic Sleep `BUsl`
and SleepPause are default-false; a public Sleep marker does not establish a
SleepStun lifetime. Constructors and pooled factory references for SleepStun
are retained in research, and its reachable public producer remains under
GROUP-03.2. This work does not infer defending priority from arbitrary engine
stun state.

Two observed runs and an unhooked control agree on all110 public markers and
2,141 normalized native records across12 acquisition scenes. A separate
Move/Attack experiment agrees on26 markers and34 native records. Existing
Move and Attack orders become zero when Freeze arrives; a new Move returns
false while frozen, and removal leaves the old order retired. A third pair and
its unhooked control preserve26 markers and34 native events on lethal hits:
Freeze attaches during the hit, then death removes it immediately. In total,
nine captures preserve162 public markers and2,209 native events. The engine
now applies the Freeze orb before weapon damage, allowing ordinary death
cleanup to release its owner. Other orb producers retain their existing phase. Normalization
removes process pointers while retaining identity equality, every captured
scalar, counter, event and ordering relationship. The evidence verifier checks
808 original instructions in15 whole functions and recomputes the724-class
inventory from the hash-locked original DLL. Ghidra comments are saved and a
readback export matches the applied export exactly.

The production regression first failed56/140 assertions per edition. A later
clean isolated baseline passes219/227 assertions in three tests per edition,
with eight genuine failures: disabled-source, player availability and authored
requirement gates plus two lethal-hit cleanup checks. Freezing Breath now
checks source execution, player availability, research and requirements before
applying its area effect; native58ff90/590110 perform the3c/virtual268 gates. The
passive Breath regression also failed before implementation. A subsequent
saved-priority failure came from allocator-only test weapon rows, which the
fixture archive does not contain: Frida instrumentation of the engine's own
ReadGame and priority entry points identified that setup error. Reattaching
those test weapon rows after cold load corrects the fixture without changing
production lookup behavior. Public retail probes initially used a nonexistent
lowercase Dreadlord rawcode; the accepted runs use `Udre`. These unsuccessful
setups remain archived and are excluded from accepted evidence. Moving Freeze before damage then exposed an
actual reaction bug: the disabled victim could counterattack and retain a target
that was subsequently removed, causing save validation to reject its Attack
subscription. The existing hit/source-removal/save regression reproduced four
failures; weapon reaction admission now checks the same positive Attack execution
counter. An old registry smoke test also required every orb to share one handler;
it now distinguishes Freeze's recovered native subclass. Authored pickup/drop
checks retain its inherited damage bonus in both data column schemas. An extended
fixture initially compared a canceled Attack head against its former priority
after a separate Pause/Unpause transaction; the corrected assertion checks
restored eligibility independently of the retired head. A lethal-hit fixture
also needed its own authored AIzb row in the Classic archive. The clean eight
failures above exclude these setup mistakes.

Run the complete original/engine contract with a fresh report:

```sh
python3 tools/ghidra/verify_wc3_pathing_group359.py \
  --binary /path/to/1.27/game.dll --report /tmp/group359-fresh.json
python3 -m unittest discover -s tests -p test_wc3_pathfinding_group359.py
```

The portable bundle contains the original map base, complete observations and
public preload files. The verifier rebuilds each map from the committed probes,
checks provenance, read-only hook completion and both repeats against the
unhooked public control, then executes the production tests in both editions.
The freezer accepts completed raw captures and does not derive engine expected
behavior. Arbitrary trajectories, remaining acquisition guards and other true188
producer lifetimes stay in the existing GROUP-03.2 leaf; no child TODO is added.

The final isolated Save204 production and test modules build successfully. Both
Classic and TFT pass328 directly affected tests and39,380 assertions. Seventeen
evidence rejection checks,38 corpus checks and12 exclusion-owner checks pass.
