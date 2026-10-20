# Captain retreat with a moving roster and retail save/load (Captain473)

Captain473 adds two complete public retreat journeys and native retail save/load evidence to
the existing `GROUP-03.4.7.3` task.

In the first journey, a six-Footman attack Captain travels on `CaptainAttack`, and four
members are removed far from home. The second journey removes one more member while the
Captain is already retreating. Both scenes cover:

- per-removal home reevaluation and the retreat request
- member reissue
- the virtual actor stopping inside its range
- the goal event that clears retreat
- independent member arrival

Retail public getters and Captain snapshots are compared word for word throughout each
30-second scene. Retail UI saves were taken at five points, two of them inside the retreat
bit window. Each load resumes identically to the uninterrupted journey. This delivery does not
close the task.

## Recovered policy

`CaptainAI_UpdateMemberOrders` (`6f9d16c0`) runs after every point publication (`9d44d0`). Its
retreat branch applies when Captain flag bit2 (`6c & 2`) is set and either the follow identity
`114/118` is absent or order70 is `d0012` (`9d1715..9d173a`). The branch does not create
private follower batches or a shared point publication. It walks the logical roster in prepend
order. Every member without the `BTLF` timed-life buff class (`9d17c8`) receives
`CaptainAI_ReissueMemberOrder(unit, d0012, null, home_x/home_y)` at `9d1808` (return `9d180d`).
The point is the authored home `fc/104`, not the current request `54/5c`.

When `9d87d0` is called with a null target, it does not perform the Captain-target conversion.
It then handles the member's current head as follows:

- **`board` (`d004b`) head:** left unchanged.
- **Action-category-3 spell task (`unit+194 == 3`), `smart` (`d0003`) head or `attackonce`
  (`d0011`) head:** queued successors are cancelled, and the point order is appended after the
  current head.
- **`d0012` head with no resolved target:** retained without any point comparison (assembly
  `8901..890f`).
- **Any other head:** replaced through ordinary admission. This includes a follower head that
  targets the Captain bridge.

The virtual actor still receives the home request at range 500 and speed 500, so it stops at
its range predicate while the members walk all the way to the authored home. The actor's goal
event reaches the near-home GoHome. That GoHome clears bit2 because the range-entered count is
at least half the roster, and it publishes no new request.

A removal during retreat (scene b, tick 139) reevaluates home again, with result 1 and the
retreat flag retained. It republishes the actor's home request, which changes where the actor
stops. It reaches `9d87d0` for the survivor, whose identical `d0012` null-target head is
retained. Reissuing that member would restart its walk: a diagnostic engine build without the
retention diverges at tick 140.

`InitAssault` (`9c7b10`) sets Captain flag bit0. The engine represents this existing formation
flag as `botCaptain_t.full`, and the literal snapshots compare `policy_flags | full`.

## Engine change

`move_captain_publish_point` previously ignored the retreat bit. Because all roster members had
entered range, it published the shared point request, which moved the members as a cohort
toward the request — the wrong producer.

The new `move_captain_retreat_members` implements the `9d16c0` retreat branch and the
null-target `9d87d0` head cases listed above. It snapshots roster identities before any
admission callback runs. Replacement uses ordinary `S_IssuePointMoveOrder`, and the append case
uses ordinary queued `G_IssueUnitPointOrder`. No state is added, so the save layout is
unchanged. Integration uses the live Move group target with its incarnation
check. `G_GetActiveUnitOrder` is optional `WC3_SC2API` telemetry (introduced in
`7615fe7ce`), so gameplay must not depend on that accessor: it is absent in a
default build, and its packet shape does not prove a target still resolves.
The default module link is a required integration check alongside the strict
engine replay.

## Evidence

The scenes use the flat 2048 Shared315 arena with a passable WPM and stock Footmen only.

- **Map timer:** creates six Footmen at tick 10, starts the AI at tick 12, removes `u0..u3` at
  tick 135 (scene b also removes `u4` at tick 139) and completes at tick 300.
- **Public getters:** every tick records `GetUnitCurrentOrder/GetUnitX/GetUnitY` for every unit
  through `Preload`.
- **AI script:** runs `SetCampaignAI`, `CreateCaptains`, `SetCaptainHome(1,384,1664)`,
  `InitAssault`, `AddAssault(6,'hfoo')` and `SetGroupsFlee(true)`, then
  `CaptainAttack(1664,384)` after eight seconds.

The read-only observer hooks the `Preload` intern, the public coordinate getters and the
Captain producers. It invokes no game code and writes no game memory.

| Family | Observations | Control | Result |
|---|---|---|---|
| retreat473 | 3 (envs B, B, C) | 1 | 3,888 identical semantic records; all 2,037 public markers equal the control |
| retreat473b | 2 | 1 | 3,895 identical semantic records; all 2,037 public markers equal the control |

The only `Preload` differences are non-simulation lines: the `PreloadEnd` wall time and the base
map's random music choice. Two earlier launches produced no markers because loading never
advanced. Both are excluded, and their hashes and reasons are recorded in the spec.

| Tick | Retail result (retreat473) |
|---|---|
| 12 | Captain state 2, flags `1`, home and request (384,1664), range 500 |
| 30 | the periodic update has set flags `1001`; five members have entered |
| 32 | the sixth member enters range: a range-200 refresh that does not retain the request; c4=6 by tick 33 |
| 92 | `CaptainAttack`: request (1664,384), range 200, order70 `d0014` |
| 135 | four removals: `9d08e0` returns 0, 0, 0, then 1 at two members. Flags become `1003`, the home request is at range 500, and both survivors are reissued `d0012`/null/home from `9d180d` |
| 143 | goal event, then GoHome near home: flags `1003 -> 1001`; the actor stops at fine (23.1216, 41.4348), 491 world units from home |
| 190 | the survivors stop at home with public order 0 |

### Retail save/load

The save/load capture uses the unchanged R473a map. When the public tick reaches the save point,
the owned window receives F10, `s`, a save name and Return. The single-player menu pauses the
simulation, and the capture records the tick at which the pause happens. The first pass then
continues uninterrupted to tick 300. Next, F10, `l`, the first list entry and Load restore that
save, and the second pass runs from the saved tick to tick 300.

For observed runs, the following must hold for every record after the resume:

- Every public getter row equals the first pass.
- Every Captain policy record (goal events, periodic updates and their counters/clocks) equals
  the first pass.
- Every readable Captain snapshot equals the first pass.

Load reconstructs the native Captain objects. Snapshots through stale pointers are unreadable
until a producer reports the new object, and snapshots are matched by authored home. Every
first pass also equals the accepted uninterrupted fixture. For observer-free controls, the
second `Preload` segment must equal the first-pass suffix.

| Run | Pause after tick | State at save | Compared after load |
|---|---|---|---|
| sl-explore-2 (observed) | 142 | flags `1003`, actor moving at speed 500 | 2,054 public, 32 policy, 157 attack snapshots: equal |
| sl-control-2 (control) | 139 | inside the bit2 window | 1,127 markers equal the first-pass suffix |
| sl-explore-1 (observed) | 184 | members walking home | 1,508 public, 22 policy, 108 snapshots: equal |
| sl-observe-3 (observed) | 224 | after arrival | 988 public, 14 policy, 68 snapshots: equal |
| sl-control-1 (control) | 241 | after arrival | 413 markers equal the first-pass suffix |

UI key delivery under wine is not reliably timed. Pressing F10 at tick 136 paused the game at
ticks 142, 184 and 224 in different runs. The save points listed are the observed pauses, not
the requested tick. Retail save/load preserves the bit2 retreat flag, the moving actor request,
the periodic Captain phase and counters, and the members' ordinary home orders.

## Failing-first and validation

The `wc3_captain_retreat473` tests run the same public scripts (`Scripts\retreat473.j` /
`retreat473b.j` and `Scripts\retreat473.ai`). They compare 1,746 getter rows and 289 attack
Captain snapshots per scene. Each snapshot covers state, flags, bc, c0, c4, the request, range,
the actor's fine pose and the actor's velocity. Engine velocity is world-scaled, so the exact
`/32` value is compared.

| Engine state | Result (Classic and TFT) |
|---|---|
| Before the change | 20,780 of 20,795 assertions across 3 tests. The first divergence is u4 at tick 138 (row 772), three ticks after the shared point publication replaced the retreat reissue. Every earlier row matches: attack travel, removal policy and the retreat request. |
| Diagnostic build without identical-head retention | Scene b diverges at tick 140 (row 785) |
| Final | 10 tests / 156,882 assertions |

The final 10 tests are:

- one fresh run of each scene
- engine cold saves at ticks 120 and 138
- engine cold saves at all five retail UI save points (139, 142, 184, 224 and 241)
- a scene-b cold save at tick 140

The engine cold saves are not retail saves. The retail runs establish that native load resumes
the uninterrupted journey, and the engine continuation reproduces that same journey from each
save point.

Adjacent suites pass in both editions:

| Suite | Tests | Assertions |
|---|---|---|
| `wc3_movement.*` | 815 | 9,440,200 |
| `wc3_bot.*` | 178 | 5,416 |
| `wc3_save.*` | 235 | 68,300 |
| `wc3_order_lifecycle.*` | 98 | 19,703 |
| `wc3_captain376_types.*` | 6 | 83 |

`tools/ghidra/verify_wc3_pathing_captain473.py` replays the gzip bundle of every raw accepted
capture. It regenerates both engine literal headers byte for byte and checks the policy
boundaries above. It also requires the retail save/load family, including both retreat-window
saves. Finally, it guards 1,595 original instructions across eight functions and runs both
engine editions.

`tests/test_wc3_pathfinding_captain473.py` contains 18 tests. They reject changes to any of
the following:

- getter words or header words, in either family
- repeat consistency
- reissue caller or target
- the second-removal reevaluation
- an instrumented or missing control
- control markers
- record order
- post-load getter words
- either retreat-window save
- the control suffix
- source pins
- instruction guards

## Remaining in GROUP-03.4.7.3

The following remain open:

- combat-driven retreat and engagement journeys, and retail save/load of combat, help and
  transport states
- public witnesses for the append, spell-task and board branches of `9d87d0`
- the native b8 and c8 counters
- the remaining help and transport consumers
