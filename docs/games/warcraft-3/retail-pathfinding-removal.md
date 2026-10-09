# Removal disables separation before target callbacks

Payoff204 advances SEP-01.2 without closing its wider counted-suppression audit.
Move now unlinks separation during `A_UNIT_REMOVING`, before surviving active
owners receive `A_TARGET_REMOVED`. Owner, type and pause refreshes consult
`G_IsDeferredFree`: its existing heap membership is checked against the unit's
spawn generation. A pending identity cannot recreate its repulsor. Unlink uses
the existing backlink index, taking constant time and preserving survivor order.
No new instance fields, saved layout, counter API or network contract are added.

The failing-first public `CreateUnit`/`RemoveUnit` regression initially failed
33 of 117 assertions. It covers head/middle/tail removal, callback observations,
public owner/pause refresh, type notification, duplicate removal, no removal RNG
draw, one unlink visit, pending save/load and final release. It passes in both
Classic and TFT. Each edition also passes all seven repulsion-policy tests
(306 assertions) and all 43 order-lifecycle tests (15,280 assertions). This is
focused validation at implementation commit 10 of the current 12-commit batch;
the full repository suite was not repeated for this chunk.

## Retail evidence and its limits

Original `game.dll` SHA256 is
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
Two complete read-only runs retain 143 native stages each; all 44 public markers
match an observer-free control. The authored `hREM` clone enables separation
with integer `urpo=2`, `urpp=17`, `urpg=17`, `urpr=17`. Native creation publishes
policy `0x10110000`; the normal and nested removal controls start with real
repulsors. A third unit supplies the initially paused control.

`694690` cancels orders, then `688d90` increments `Unit+198` and refreshes through
`693d50`/`1710e0`. `RemoveUnit` returns with depth one and null `Mover+ac`.
Inside the issued-point callback, owner transfer and `PauseUnit(true/false)`
retain that suppression and the original identity. The complete original
`66fc50` eligibility predicate is rerun against all 432 **unchanged** frozen
cases. Ghidra comments at `694690`, `688d90`, `6785c0` and `1710e0` are saved and
mirrored in `MapPathfinding.java`.

Retail's final inactivity task releases its count and briefly recreates a
repulsor before destroying the mover. Paused removal also releases its pause
count while cancelling old work, before acquiring the removal count. Both
internal intervals remain in the full captures. They are **outside this engine
integration**; broader counted work and retirement-event ordering remain open.
Duplicate removal, type refresh and saved pending removal are engine lifecycle
regressions, without a new public retail witness here. No full RNG stream,
trajectory, arbitrary callback admission or retail save/load claim is made.

The initial stock `hfoo` runs had disabled separation and cannot certify
repulsor retirement. An isolated environment C attach timed out before observer
installation. Those captures are retained and excluded, never used as passes.
Raw pointers and undefined upper argument bytes are normalized only for repeat
comparison; the frozen compressed fixture retains every original field.

## Reproduce

The complete captures, maps, construction metadata and failing-first logs live
under `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2/integration204/`.
The reusable probe, read-only observer, map adaptation and freezer are
`tools/frida/research/removal204_{probe.j,observer.js,make_map.py,expected.py}`.
First build the staged map with `group032_make_map.py --probe
tools/frida/research/removal204_probe.j`, then add the clone with
`removal204_make_map.py`. Preserve their generated JSON metadata. Capture through
`swing199_capture.py --task SEP-01.2/payoff204 --probe
tools/frida/research/removal204_probe.j --observer removal204_observer.js
--marker-prefix 'R204 '`, in isolated B/C only, with one observer-free control.

```sh
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 \
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_removal.py \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27 \
  --report /tmp/removal204-fresh.json
python3 -m unittest tests.test_wc3_pathing_removal
```

The report must be new. The runner rejects missing stages, disabled-only
controls, incomplete observations, stale raw pins, instrumented controls and
empty or failed game regressions. Existing numerical fixtures are unchanged.
