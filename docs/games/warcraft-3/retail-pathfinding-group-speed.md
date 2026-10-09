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

## Attack-owned cap exemption (Payoff151)

Attack now owns the three-second exemption deadline and its primary request
serial. Accepted pre-transform damage notifications set the exemption before
retaliation decisions, including zero damage. Automatic acquisition also sets
it; Attack Move now emits the existing acquisition event. An exempt member
releases the whole physical group's shared speed cap, denies projected
classification and allows regroup advancement when any member has arrived.
An exemption alone cannot advance a group with no arrived members.

The original control's virtual `+18` is `060ad0`, which returns **remaining**
time. Rearming requires `3.0 - remaining >= 0.5`, using software subtraction.
This means at least half a second has elapsed, not half a second remains.
The original timer oracle covers 84 states, including adjacent boundary words
and cancelled requests. Its corrected notification-prefix oracle covers 72 guard states;
these constructed inputs do not prove that every guard is publicly reachable.
Another 1,296 original calls cover the three group consumers. The engine runs
72 of their regroup cases through actual group advancement; the actual hit
regression checks exact velocity words, retained Move identity and cooldown65.

Attack maintains an indexed deadline heap: earliest query O(1), arm and cancel
O(log n). The timer clock merges its earliest request with script and spatial
requests by float deadline, then unsigned serial. Expiry runs at that deadline;
rollover drains before rebasing. Save127 retains active/deadline/serial and
rebuilds heap indexes. The regression covers saving an active exemption,
equal-deadline ordering relative to the path owner, rebasing, removal/reuse and
4,096 tied requests across unsigned serial wrap with interior cancellations.
Heap rebuild scans entities only during restoration, not each timer pass.

Fresh read-only Frida runs use `attack_cap151_probe.j` and
`attack_cap151_observer.js`. Eight public zero-damage hits produce three arms
(including two rearms), five retained deadlines and one expiry. The nominal
half-second hit is below the threshold in the actual clock words; the next hit
rearms. Both observed streams repeat and their 89 position/order markers equal
an observer-free control. The expiry hook sees an already-cleared request;
exact deadline ordering is established by the clock and engine regressions,
not inferred from that hook. Frozen lossless captures and the original fixture
are checked by `research/verify_attack_cap_live.py` and
`verify_wc3_pathing_attack_exemption.py`. Ghidra annotations are saved and
mirrored in `MapPathfinding.java`.


Focused Classic/TFT validation passes1,498 test executions/10,489,856 assertions
across movement, combat, save, timer callbacks and Chaos lifecycle. Seven new
Python evidence regressions and22 corpus checks pass. The staged inventory has
374 entries/689 pins; four freshly executed original/live contracts pass.

GROUP-03.2 remains open for ally-alert admission, remaining acquisition paths,
the `49e130` producer, complete notification-state guards and remaining
public target-speed domains. These are retained within the existing task;
no child TODOs were added. TARGET-02/03 retain their wider policy scopes.
See the [engine ledger](retail-pathfinding-engine.md) and
[target gate policy](retail-pathfinding-target-warp.md).


## Ally help uses ordered spatial recipients (Payoff152)

Zero-damage and ordinary damage now broadcast help before positive-amount
rejection. Move shares its proximity records with Attack, retaining separate
X/Y widget-query and Y/X separation traversal. The help predicate includes the
candidate's collision radius. Recipient callbacks run in native encounter order,
then the ordinary victim arms its saved three-second suppression request.
Peers retain their explicit orders while receiving the cap exemption.

Authored CallForHelp/CreepCallForHelp radii and both directional help permissions
are consumed; opposite-direction permissions cannot substitute. Same-owner
permissions start enabled and can be revoked. Save128 preserves independent
cap/help deadlines and serials. The first unhit-peer regression, radius/order
regression and traversal-order regression each failed before implementation.
Focused Classic/TFT checks and repeated native public controls pass.

The native AI/Town-owned flag,900 radius and0.5-second policy are separately
mapped evidence and remain to be integrated with their actual enrollment and
propagation producers. Native engagement/notification guards and other acquisition
producers also remain within GROUP-03.2. This does not close the leaf.

Correction:4935e0 reads guard bits from unit+5c. Payoff151's old oracle wrote+20;
the corrected72-state fixture changes four accepted outcomes. Its timer and
group-consumer fixtures are unchanged. See the
[full engine evidence](retail-pathfinding-engine.md#ordered-ally-help-reaches-moving-peers-payoff152).


## Town AI membership and help timing (Payoff153)

Configured playing computer slots and Neutral Aggressive retain Town AI
membership independently of the script VM. Creation and genuine owner transfer
publish that saved flag; same-owner calls and script startup retain it. Attack
uses900 radius for enrolled non-neutral victims, authored creep radius for
neutral owners, and0.5 versus3-second source suppression according to membership.
AI self alerts precede ordinary damage notifications and still run during help
suppression. Save129 retains configured availability, instance enrollment and
armed request words. A one-time preplaced-unit pass preserves owned sequence;
non-unit scenery, wards and dead units are excluded.

Three failing-first engine regressions now pass alongside both focused mode
suites. Two complete retail repeats and an observer-free control retain377
markers and exact enrollment/transfer/radius/delay decisions. Ghidra function
annotations and player/unit fields are saved and mirrored in the mapper/schema.
See the [engine evidence and remaining scope](retail-pathfinding-engine.md#town-ai-enrollment-selects-the-help-policy-payoff153).

## Explicit swings (Payoff199)

`49d130(...,1)` is now integrated for explicit target and ground weapon windups.
It calls the existing exemption producer before its cooldown test; mere chase
and non-firing ground holds do not grant it. Two fresh Frida repeats plus an
observer-free control cover melee, ranged Attack Once, targeted artillery and
Attack Ground. See [evidence and engine regressions](retail-pathfinding-engine.md#explicit-weapon-windups-release-the-shared-speed-cap-payoff199).
The `49e130` notification guards and broader captain domains remain open.
