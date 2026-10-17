# Complete natural target contention

Payoff361 reproduces the original TARGET-02.1 map C scene0 through public JASS
timers and production frames: 36 independently admitted Smart followers pursue
one moving target around a wall. The target reverses twice; the first follower
and target stop, then all actors are removed. The engine compares all 22,772
actual follower owner decisions in original chronological order. Both Classic
and TFT pass the baseline with 1,001,733 assertions each, including a save at
counter1142 and an exact replay of its remaining suffix. Existing engine behavior
already matches; this change adds coverage without a fabricated engine fix.

The literal22-word snapshots retain actor identity, owner counter, refresh and
hidden countdowns, member pose, group/member cached destinations, request
timestamps and route counts/indices. The lite observer does not record member
retry words. Empty unpublished route storage/indexes have no destination meaning
and are excluded only until their corresponding route exists. Every other
recorded word is compared directly, without decimal reconstruction or tolerance.

The archived two retail repeats and a fresh read-only Frida capture match every
one of these rows. An observer-free control matches all544 public markers. As in
[the target356 audit](retail-pathfinding-target-audit356.md), the fresh map changes
only its invalid inherited shadow member; all gameplay members retain their
original hashes. The original map remains untouched.

The lite observer snapshots owners before membership pruning. An actual visible
owner visit decrements or reloads the refresh countdown; a retired pre-prune
snapshot leaves it unchanged. The extractor uses that distinction to retain the
22,772 live decisions. Public Stop/removal checks independently verify teardown.
This includes the original delayed group sample at1142: coarse admission1133
leaves its timestamp only nine visits old, so the changed target sample is
discarded until the next refresh. Later member delays retain renewal of old-route
timestamps under natural request contention.

Ghidra saves two owner notes and exports802 instruction guards across eight
complete functions; saved readback is identical. The portable verifier combines
these with the existing unit-producer inventory for1,382 unique original
instruction guards. It also verifies map repair, source pins, literal C rows,
original repeats, the fresh observation and the unhooked control before running
the production regression in both editions. Ten evidence tests reject altered,
truncated, floating, dropped, failed, unfinished or instrumented evidence.

Natural36-follower contention is covered completely for this scene.
[Payoff362](retail-pathfinding-target-masks362.md) completes the final
producer/mask audit and amphibious journey within the existing TARGET-02.1 leaf.
This does not imply complete trajectories for every possible geometry. Captain
and missile bridge callers retain their existing owner leaves.

The external archive is
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/TARGET-02.1/payoff361/`.

```sh
python3 tools/ghidra/verify_wc3_pathing_crowd361.py \
  --binary /path/to/retail/game.dll --report /tmp/crowd361-fresh.json
python3 tests/test_wc3_pathfinding_crowd361.py
```
