# Target destination delays and denied owner visits

## Contract

WC3 1.27.1.7085 `Path_CheckDestinationChange` (`6f167e40`) compares signed,
wrapped `floor(fine coordinate) >> 1` buckets. An equal bucket retains the
stored path destination even if the caller supplies different fractional words.
A changed bucket replaces it only when both unsigned request timestamp ages
are at least ten movement-owner visits.

Group paths request coarse routes only; their fine timestamp remains zero.
Each member has separate fine/coarse timestamps. A group waypoint publication
therefore does not imply that every member can replace its path immediately.
Arrival and held-heading decisions still consume the caller's new destination.
Only an accepted path replacement clears route counts, retry and wait delay;
arrival and held-member handling precede that replacement.

For target sampling, a changed but premature sample is discarded. The group
retains its old destination and reloads the refresh countdown. It does not save
the sample for admission at timestamp+10. A reload of 16 produces the next sample
17 owner visits later. Hidden-target policy and the wider order families remain
separate unfinished TARGET work.

## Denied coarse admission

`PathGroup_TickMovement` (`6f16c150`) handles a failed `6f16ce10` request by:

1. Updating the target-refresh countdown through `6f169680`.
2. Calling `PathGroup_StopMembers` (`6f16c5d0`) exactly once.
3. Returning without classification, regroup, coarse advance or layout.

Each member integrates its previous velocity and commits zero speed at its
current facing. Its individual scheduler request is unlinked; the group request
and physical owner survive. The destination is published before admission, so
refresh and save/load observe it even while the route is invalid. On successful
retry, the owner refreshes layout even when the selected point equals its old
point.

The engine previously skipped both refresh and stop on denial. It also reset
member buffers at slot publication and rebuilt paths for different fractional
destinations within the same bucket. Payoff164 fixes these ownership boundaries
in Move and the game-owned route query, without adding order-specific cases.

## Evidence and engine verification

The frozen [capture manifest](../../../tools/ghidra/fixtures/retail-target-delay164-1.27.json)
pins five completed archived Frida captures of the matching game.dll. Fresh
verification checks two repeated complete initial Smart approaches, their two
observer-free timeline comparisons, and two crowd failure observations:

- 235 raw owner states for the initial walled ground Smart approach, including
  member destination, numerical pose/velocity, route counts/indices, request
  timestamps, retry and wait state. Positions come directly from raw observer
  words, never rounded analysis-report floats.
- 4,166 denied visits per crowd repeat, each with exactly one stop and no
  regroup/advance/layout; 723 recovery episodes refresh layout on the next visit.
- Failure output `final_destination` is unwritten native stack data and is
  deliberately excluded from comparisons.

The production-frame engine journey reproduces the 235 approach states and its
final pose/velocity. A saved continuation repeats the last 85 states. Two further
regressions cover denied admission with old-velocity integration, queued-owner
save/load and recovery, and a public target teleport whose premature sample is
discarded for the complete 17-visit cadence, including save/load midway through
that countdown. All three regressions fail against the preceding implementation.

Reproduce the archived evidence without launching retail:

```sh
python3 tools/ghidra/research/verify_target164_live.py \
  --expected tools/ghidra/fixtures/retail-target-delay164-1.27.json \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --output /tmp/target-delay164.json
```

Run engine regressions with the generated fixture MPQ:

```sh
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 \
  build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test 'wc3_movement.delayed164*'
```

Add `-tft` for the TFT schema. These checks do not establish full crowd numerical
parity, multi-member denial, warp-marker classification or every target-order
and visibility lifecycle. TARGET-02.1 and FORM-04.2 remain open.

See also [movement integration](retail-pathfinding-engine.md) and
[composed blockers](retail-pathfinding-composed-blockers.md).
