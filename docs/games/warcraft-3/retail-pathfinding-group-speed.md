# Persistent groups match visible moving targets

Payoff150 implements the target-speed branch of native `16b5c0` in Move.
After all member decisions, the requested speed is capped by the shared
maximum. A group with bit `0x800` can then reduce a positive speed above its
resolved target's maximum. The target must be moving and the group's unseen
counter must be zero. The member's predicted fine position must be strictly
inside `(arrival_range + 4)^2` of this visit's **sampled destination**.
Equality retains the original capped speed.

All calculations use the existing software scalar functions in fine cells.
The adjusted speed is target maximum times the native runtime-parsed `0.95`
word, `0x3f733334`. Replacing that with the host literal changes numerical
behavior. The helper is `wc3_group_commit_speed`; the actual Move owner applies
it before `wc3_velocity_update_world` and `unit_commit_motion`. Ordinary point
groups avoid this additional arithmetic.

## Sampling and persistence

Native `16cd30` queries visibility before testing the refresh countdown.
A hidden visit increments group `+6c`, retains the cached destination and
leaves sampling disabled. The existing countdown still advances toward zero.
A visible visit clears the hidden counter, but only countdown zero samples
the target's predicted position. Speed matching uses the sampled position even
when a route retains its older goal.

`G_FowPlayerCanTrackUnit` supplies the group callback's mode-4, flags-zero
query. Owned/shared-vision units can still be hidden by the vision cell;
rendering's ownership shortcut cannot implement this query. Virtual captain
actors retain the native no-unit-wrapper bypass. Caller-specific visibility
flags, notification/cancellation policies and delayed changed-cell admission
remain under TARGET-02/03.

Save126 stores `moveGroup_t.unseen_counter`. Saving during a hidden episode
preserves the counter, sampled goal and refresh countdown. Save125 and older
layouts are rejected before restoration. See [save/load](save-load.md).

## Verification

The moving-target regression failed two velocity assertions before the fix;
stationary and equal-speed controls passed. The fog regression failed eight
destination/countdown assertions. The final owner tests also cover a distant
target, a nonpersistent approach, hidden-episode save/load and reacquisition
before the countdown reaches zero.

`verify_wc3_pathing_group_speed.py` executes the original `16b5c0`, including
real registry resolution, clock selection, prediction and scalar arithmetic.
It stops read-only at the final velocity commit to capture the speed argument.
All **6,480 constructed cases** match the engine, including **96 adjustments**
and adjacent distance boundaries. Forced flags in this oracle are explicitly
not public-producer evidence. The same frozen words run in the C test suite.

The delivered read-only Frida lane-A captures independently retain **4,671
speed commits**, including **226 adjustments**. Their full numerical/event
streams repeat and all **147 public markers** equal an observer-free control.
`research/verify_group_speed_live.py` revalidates these archived inputs; this
is reuse of delivered research, not a newly launched capture. The observer
lacks elapsed prediction inputs, so this verifier does not infer distance
gates from committed poses or claim complete engine trajectory parity.

Focused Classic and TFT validation passes **550 engine test executions /
12,874,888 assertions**: the new policy and owner cases, existing public Follow
and Captain journeys, save, fog and pathfinding suites. Six Python evidence
checks reject changed speed words, hidden adjustments, incomplete captures and
corrupt controls. Ghidra notes are saved and mirrored in `MapPathfinding.java`.

GROUP-03.2 remains open for the Attack-owned exemption producers and their
classification/regroup consumers. TARGET-02/03 retain their wider policy
scopes. No child TODOs were added. See the [engine ledger](retail-pathfinding-engine.md)
and [target gate policy](retail-pathfinding-target-warp.md).
