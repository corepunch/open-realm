# Retail pathfinding evidence: Routes, scheduling and target state

[Contract, current ledger and reproduction](retail-pathfinding.md).
Retail **1.27.1.7085** only; addresses and report paths use the conventions there.

## Route buffers and mover integration

Fine reconstruction `6f147dc0` follows parents into a reverse-ordered route,
preserving exact start and conditionally exact destination. Accelerated
reconstruction `6f162a30` also emits sentinel records for special edges. Producer: [Way Gate chain](retail-pathfinding-experiments.md#special-edges-are-way-gate-records).

## Reconstructed coordinates and request return values

Fine reconstruction emits every parent-chain node as `(x+0.5,y+0.5)`, from
the returned node back to the start. It replaces the last point with the exact
supplied start. It then compares floored XY of the first resulting point and
the supplied destination; only when both match does it replace the first point
with that exact destination. Thus a one-node chain first becomes the supplied
start and can then become the destination if they occupy the same fine cell.
This routine does not remove collinear points or perform visibility smoothing.

Accelerated reconstruction normally emits `(x+0.75,y+0.75)`. For accelerator
size class `+0x90 == 2`, the offset is instead 1.25. At levels above zero,
each coordinate independently decrements by one when it equals the last
integer coordinate of its aligned `2^level` square:

```
if sizeClass == 2 and level > 0:
    if x == ((x >> level) << level) + (1 << level) - 1: x -= 1
    if y == ((y >> level) << level) + (1 << level) - 1: y -= 1
offset = 1.25 if sizeClass == 2 else 0.75
```

A nonzero node byte `+0x23` appends `(sentinel,float(warpID))` after that node
in the reverse buffer and increments accelerator `+0xa0`. Unlike the fine
routine, the accelerated routine **unconditionally** replaces the last point
with exact start and the first point with exact supplied destination. This is
the endpoint replacement previously observed beside a Way Gate sentinel.

`verify_wc3_pathing_routes.py` passes **480 fine and 9,216 accelerated cases**
against original reconstruction, software-float helpers and container append,
with no code stubs. Fine cases cover chain lengths 1..5, positive/negative
coordinates and endpoint fractions across cell boundaries. Accelerated cases
cover size values 1/2/4, levels 0..3, XY 0..15, and middle-node tags 0/1/255.
The latter tags are synthetic reconstruction inputs, not proof that all those
gate IDs can be allocated in a particular game.

The emulator must supply initialized BSS constants. A fresh Frida capture at
map creation (`runtime/reconstruction-constants.jsonl`) directly measured
`6fd3c740=-1`, `6fd3c744=0`, `6fd3c748=1`, and
`6fd53a74=-128000.0078125`. Omitting the first initially made negative fractions
appear to use a different floor rule; that was an emulator initialization
error. With these observed values the original floor helper matches the model.
This 30-second capture is a constant-initialization witness, not a completed
movement scenario. The observer includes these values in its maps event.

Oracle: `verify_wc3_pathing_routes.py` → `route-reconstruction-oracle.json`.

Fine request wrapper `6f148100` clears the output buffer before setup. Its
return contracts are distinct from the node index returned by the search loop:

| Condition | Wrapper result | Output |
| --- | --- | --- |
| Start and destination floor to the same fine cell | 1 | One exact destination point, no search |
| Search returns a node index (including special-object early exit) | 1 | Reconstructed chain from that node; exact destination only if the cell matches |
| Search returns -1 and nearest node remains the start | 0 | One exact start point |
| Search returns -1 with a different nearest node | 0 | Reconstructed partial chain; stored destination `+0x78/+0x7c` becomes nearest cell centre |

The grid oracle now verifies **288 complete wrapper requests** after the core
and repeat searches, checking size selection, return value, route endpoints,
pop count and adjusted partial destination. On the solid wall, classes 0/1
return partial endpoint `(11.5,19.5)` and classes 2/3 `(10.5,19.5)`, all with
result 0 and 16 route points. The same-cell and zero-budget branches have
additional checks across all four classes; zero budget returns the start
point with result 0 and counter 1, while same-cell requests return the supplied
destination with result 1 and counter 0. Invalid starting-map coordinates and
the wrapper's caller preconditions remain to be recovered.

`6f165ae0` advances a persistent path: flag/countdown gates, accelerated progress,
then fine waypoint handling. `6f168870` starts accelerated consumption at the
last route element and normally fine consumption at the penultimate element.
`6f168b80` resets buffers when changing destination; `6f168be0` copies the scalar
footprint to `path+0xb4`.

Mover routine `6f16fbd0` accesses its path at `+0xa8`, copies footprint from
mover `+0x90`, advances the path, and hands off to `6f170880`. Group routine
`6f16ce10` uses a separate path at group `+0x3c`; helper `6f16c940` can choose the
maximum member footprint. Group and individual paths are separate. Formation goals and separation are
covered in the movement and separation evidence files.

Request interval `6f168910` uses owner counter `+0x538`, separate path timestamps
`+0x7c/+0x80`, and mode-specific minima. Admission `6f168310` additionally checks
shared work counters and a waiting list. `6f15aa80` increments that counter once per pathing update and calls
`6f167310`, which visits 16 classes × four admission buckets. Each 0x1c-byte
bucket has budget at `+4`, accumulated pops at `+8`, reset countdown at `+0xc`,
queue count/head/tail at `+0x10/+0x14/+0x18`. `6f167fa0` resets accumulated work
when countdown is zero and reloads countdown from ushort `+2`; otherwise it
decrements the countdown. Both request interval constants are 10 counter ticks. Original owner constructor `157610` initializes
counter538 to0x400, and `15aa80` reloads0x400 after unsigned wrap. Engine
[payoff39](retail-pathfinding-engine.md#twelve-member-public-group-retains-fine-admission-and-committed-occupancy)
now consumes the ordinary class0 fine bucket:1100 accumulated pops, reload1,
FIFO and ten-visit timestamps, all retained in Save75. Cumulative fine work<64
and FIFO denial clear the fine timestamp. [Payoff40](retail-pathfinding-engine.md#ordinary-fine-search-belongs-to-the-unit-player)
partitions ordinary fine work/FIFOs across all16 unit-player rows and ports old-row
removal before owner publication, with Save76 and exact public cancellation.
The remaining three policy pools are still explicit production gaps.

`6f165ea0` and `6f1686a0` append/unlink persistent paths using links at
`path+0x8c/+0x90`, with -1 endpoint sentinels and zero for unqueued paths.
Admission within the work limit accepts the queue head; exhausted work queues
the requester. Player/class bits `path+0x88[19:16]` select the 0x70-byte class row.
The ordinary unit producer assigns this row from **player ownership**, not a
ranked priority comparison. Registration at `6f2095a6/ab` binds `SetUnitOwner`
(`6fa9837c`) to native `6f215500`. That native resolves the player and passes its
byte `+30` to `6f698ce0`; the latter stores it in unit `+58` and calls `6f05c800`.
This wrapper resolves the mover, loads its path at `+a8`, and calls class setter
`6f168a80`. Group destination setup `6f16de50` copies the first member path's
low class nibble into the group path. Several non-unit producers explicitly use
row 15 (`6f6d30c0`, `6f6d3190`, `6f6cf5e0`, `6f6cfe00`); their complete type
semantics remain to be mapped. The 16 rows therefore partition work accounting,
with no cross-row priority ordering in the admission function.

The copied-map `owner_change` experiment confirms the native-to-path link at
runtime. A Player-0 Footman receives Move at tick 10; at tick 15, synchronous
`before_owner_change` / `after_owner_change` markers bracket exactly one
`6f168a80` call with argument 1. Its path flags change from `0x00300000` to
`0x00310000`. The JASS `GetOwningPlayer` check confirms Player 1. The same native
call changes current order from Move (`851986`) to zero at world
`(-1936,-904.004)`; all remaining samples through tick 300 stay there. This
establishes row ownership and observes order cancellation by the owner-change
operation; it does not attribute cancellation to the class setter alone or
measure competing players' work fairness. Capture:
`/GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/owner-change.jsonl`.
The Frida observer records bounded `scheduler-class` events, and the analyzer
requires the 0-to-1 transition inside the native's marker interval.

`6f168a80` first removes old queue membership through `6f168800`, which also
clears the fine-request flag, then replaces bits 19:16. Its machine expression
ORs the low **byte** of the argument shifted by 16 while clearing only four
bits; normal player inputs 0..15 are the verified domain, not arbitrary bytes.
The scheduler oracle now verifies 1,024 queued middle-node transitions across
all old/new rows and four bucket kinds, preserving other rows and repairing
both neighbours. This proves setter mechanics, not real-game queue pressure.

Path constructor `6f1657c0` and reset `6f166060` initialize per-request limits
`+84/+86` to 700/400. Group initializer `6f169840` replaces the accelerated
limit with 5000 from `6f1678f0`. Setter `6f168c60` writes the selected ushort;
mode 1 additionally removes queue membership. The alternate accelerated bucket's flag `0x04000000` is assigned by group
request `6f16ce10` from whether helper `6f16be60` resolves the handle pair at
group `+40/+44`. Its initial ushort 2000 is not evidence that selecting this
bucket changes the path's individual search limit; the flag setter does not
write either limit.

Group target setter `6f16d9d0` copies mover `+14/+18` into group `+40/+44`, or
stores `-1/-1` for null. It also clears group flag `0x1000`. `6f16db00` combines
this assignment with fixed destination XY at group `+4c/+50`. Group tick
`6f16c150`, called from owner update `6f15aa80`, obtains destination information
through `6f16cd30` and submits via `6f16ce10`. When the target handle does not
resolve, `6f16cd30` clears counters `+64/+6c` and uses the fixed XY. With a
resolved target it consults `6f15bb40`, can set flag `0x1000`, and otherwise
uses the current path destination. `6f16bfd0` terminates this branch when flag
`0x1000` is set and the target no longer resolves. `6f15bb40` dispatches the callback at map owner `+258`, passing target mover in
ECX and group in EDX (false if no callback). Initialization `6f1eab40` installs
`6f23a760` via `6f04dc90` / `6f15cc80`. That callback resolves gameplay objects
from the target and first group member and returns the inverse of
`6f66fdd0(target,0,4)` invoked on the member. The downstream visibility/target
policy is not yet fully resolved.

`6f168ab0` changes flag `0x04000000` only when its requested boolean differs;
on change it first unlinks the old queue and clears the fine flag. Repeating
the existing value preserves queue membership. An additional 256 original-x86
cases cover both values, fine/accelerated membership, small/large request
limits and all 16 player rows, including preservation of the request limits.

The copied-map `follow` run gives a direct group/individual comparison. One
Player-0 Footman receives a Smart target order at tick 10 toward a stationary
friendly Footman at `(-1936,-144)`. Original `6f16d9d0` records the target handle;
`6f168ab0` sets the corresponding group path's target flag. Admission for that
same path uses bucket offset `+1c`, shared work limit **300**, while the actual
accelerated search still has individual budget **5000** (four pops). The unit's
separate path then uses bucket `+38` / budget 400 (four accelerated pops) and
`+54` / budget 700 (25 fine pops). A subsequent group path uses the same target
handle and performs a two-pop, budget-5000 search. Thus a one-unit target order
already involves a group path; “group” does not imply a multi-unit selection.
The complete 300-tick trace ends at `(-1936,-503.527)`, current order `851971`;
the arrival threshold is recovered below; later follow-state transitions remain unresolved.

Evidence is `runtime/follow-retry.jsonl` and `runtime/follow-analysis.json` in
the external analysis directory. The first `runtime/follow.jsonl` is a failed
capture: the owned Xvfb display had exited; its controller failed at the Space
key. Restarting that display allowed the completed run above. The analyzer's
`--scenario follow` requires a non-null target, matching group path/target flag,
5000 request limit, target-bucket admission, and a matching actual search.
The bounded observer records 400 target-setter samples out of 1,625 calls;
this is sufficient for the demonstrated early transition, not a complete
history of every later group tick.

Startup instruction range `6f003d20..6f0040c0` calls constructor `6f1656d0`
16 times with identical settings. The following values are now verified by
executing that original initializer (stopping before CRT destructor registration):

| Bucket offset | Selection | ushort +0 | Reload +2 | Shared work limit +4 | Reset period |
|---|---|---:|---:|---:|---:|
| `+00` | Accelerated default | 5000 | 3 | 800 | 4 updates |
| `+1c` | Accelerated, flag `0x04000000` | 2000 | 2 | 300 | 3 updates |
| `+38` | Accelerated request limit `path+86 <= 400` | 400 | 2 | 900 | 3 updates |
| `+54` | Fine, flag `0x02000000` | 700 | 1 | 1100 | 2 updates |

Selector `6f1679c0` tests fine first, then the `<=400` case, then the
`0x04000000` flag. Fine request `6f166e90` sets the fine flag before admission;
accelerated request `6f166c30` removes an existing fine-queue membership through
`6f168800` before selecting an accelerated bucket. The reset period is reload
**plus one**, since reaching countdown zero and resetting occur on different
updates. Initialization zeros all counters and lists, so the first update resets.

Admission `6f168310` rejects only when unsigned accumulated work is **greater
than** the shared limit. Equality admits. It does not reserve expected search
cost: callers charge actual pop counts after searching, so one request may
overshoot the limit. With an existing queue, only its head may proceed; a new
request joins its tail even with available work. Repeat blocked requests retain
their position. Head admission unlinks it. Consequently the queue mechanism
alone allows head-of-line blocking if that head stops requesting and is not
removed by its lifecycle; this is not yet a demonstrated live starvation case.

Caller charging also differs: accelerated requests clear timestamp `+80` when
**that search's** pop count is below 32; fine requests clear timestamp `+7c`
when the **bucket's accumulated work after charging** is below 64. Admission
failure clears the corresponding timestamp. Clearing to zero is not an unconditional
interval bypass: `6f168910` still compares current counter minus timestamp with
10. When current counter is below its timestamp (including wrap/backward
movement), it substitutes `current-10` and admits immediately. Mode timestamps
are independent. Simulation seconds per update and real crowd fairness remain
unresolved.

`tools/ghidra/verify_wc3_pathing_scheduler.py` executes these original routines
without stubs. It verifies 64 initialized buckets, 448 selector cases, 1,280
bucket-update comparisons, 16,384 seeded FIFO/admission/removal operations and
162 interval cases including unsigned boundaries, plus 1,024 class transitions. Work values and requests are
synthetic; this does not execute movement or the callers' charging paths.
Reproduce with:

Oracle: `verify_wc3_pathing_scheduler.py` → `scheduler-oracle.json`.

## Route reset and stopped-path invalidation

`6f168740` takes `(mode, resetRetries, unlinkQueue, clearResults)` on a path.
Mode `-1` selects both route containers; 0 selects fine, 1 accelerated. It
removes all selected route entries through `6f14a760`, leaving allocated storage
and capacity intact, and sets selected indices `+74/+78` to `-1`. It always
clears path flag `0x00100000`. The other effects are independent:

| Argument | Effect when nonzero |
|---|---|
| resetRetries | Clears `+94/+98` through `6f1687e0` |
| unlinkQueue | Removes old scheduler membership and clears fine flag `0x02000000` |
| clearResults | Clears path result flags `0x10000000/0x20000000` |

Neither route timestamps `+7c/+80` nor destination coordinates are reset by
this routine. Destination setter `6f168b80` calls it with `(-1,1,0,1)`, then
copies XY into current destination `+1c/+20` and adjusted `+24/+28` coordinates. Its second
argument controls whether original coordinates `+2c/+30` are replaced too.
It therefore preserves queue membership and timestamps by itself. Callers
that require unlinking do so separately.

Mover stop routine `6f171340` clears the forced-arrival flag, processes movement
and optional notification work, then calls `6f168b80` with both coordinates
from `DAT_6fd541dc` and `replaceOriginal=0`. It separately unlinks the path and
sets flag `0x00100000`. Mover path setup `6f170aa0` also uses that sentinel, but
passes `replaceOriginal=1`, clears the target object, and sets the same flag.
This is distinct from the Way Gate route sentinel `-128000.0078125`.

`tools/ghidra/verify_wc3_pathing_reset.py` executes full reset/destination
routines without stubs: **6,912 reset cases** vary route mode, empty/nonempty
containers, all three option bits, queued/unqueued state and all player rows.
It compares every byte of the 0xb8-byte path against expected mutations, plus
queue head/count/tail. Another **18 destination cases** verify current-destination/adjusted
coordinates, optional original-coordinate replacement and preserved timestamps.
These cases use preallocated storage and valid single-entry waiting lists;
they do not execute the broader mover stop callbacks. Report:
`/GitHub/wc3-analysis/reports/pathfinding-1.27/reset-oracle.json`.

The completed `runtime/follow-shift-reset.jsonl` verifies the sentinel producer
and ordering. On the tracked individual path, setter calls returning to
`0x171415` (inside mover stop `6f171340`) write `(-128000,-128000)` with
`replaceOriginal=0` about **3.8 seconds before** the tick-80 target edit. They
preserve original `(163.5,91.5)` and clear route counts/indices. This occurs
when the follower reaches its first stand-off position, not as a consequence
of the later target jump. After that jump, the group setter returns to
`0x16ce73` and the individual setter to `0x16fce4`, both installing
`(163.5,99.5)` with `replaceOriginal=1`. Initial mover path creation uses caller
`0x170afa`, sentinel destination, and `replaceOriginal=1`.

The observer reads setter state immediately on return, **before** stop routine
`6f171340` separately unlinks and re-sets flag `0x00100000`; therefore the
setter event correctly reports that flag cleared. Subsequent mover setter
entry has it set. All 300 ticks and observer completion are present, and
`--scenario follow_shift --require reset` passes. The evidence identifies the
the stop routine's destination write. The movement evidence extends this through
completion packets, CUnit events and deferred release; full order handlers and
all stop-parameter combinations remain open.

## Destination changes and replan gating

`6f167e40` compares a proposed destination with the path's existing XY at
`+1c/+20`. Each float coordinate is floored (`6f070c80`), converted to signed
integer (`6f070120`), and arithmetic-shifted by the supplied count modulo 32.
Both mapped callers, group request `6f16ce10` and mover update `6f16fbd0`, pass
shift 1: two fine cells / **64 world units** per comparison cell. This is cell
membership, not a 64-unit distance threshold: crossing an aligned boundary by
an arbitrarily small amount changes the cell, while a larger move within it
can leave the comparison unchanged. Negative coordinates use floor and signed
shift, not truncation toward zero.

The function returns `changed` and separately writes `ready`:

| Condition | Return changed | Output ready |
|---|---:|---:|
| Both coordinates remain in the same comparison cell | 0 | 1 |
| Cell changes; both unsigned `owner.counter538 - timestamp` differences are at least 10 | 1 | 1 |
| Cell changes; either timestamp difference is below 10 | 1 | 0 |

It does not mutate path timestamps, destination, or the owner counter. It
compares **both** fine `+7c` and accelerated `+80` timestamps. Unlike request
interval helper `6f168910`, it performs no backward-counter repair; subtraction
wraps as uint32. Wrapper `6f167e20` combines the two results into one boolean;
no direct references to that wrapper were found in this program's current
Ghidra reference map.

In the group caller, only `changed && ready` makes `bVar2` true and updates the
path destination. A remaining accelerated route can otherwise continue to be
consumed. In the mover caller, a change of target occupancy object or explicit
force flag can also reset the destination, independently of this gate. Thus
these two outputs are not by themselves proof that a search will run; buffer
state, caller policy and scheduler admission are separate decisions.

`tools/ghidra/verify_wc3_pathing_replan.py` executes the full original function
without stubs. All **28,880** cases pass: 8,208 unchanged, 7,752 changed/ready,
12,920 changed/delayed. They cover both axes, negative fractional boundaries,
normal shift 1 plus shifts 0/2/31/33, independently recent fine/coarse timestamps,
zero counter, wrap and backward differences. The oracle checks that both the
path and counter remain unchanged. Report:
`/GitHub/wc3-analysis/reports/pathfinding-1.27/replan-oracle.json`.

The controlled `follow_shift` map preserves the friendly follow setup, then
calls `SetUnitY(target,112)` at timer tick 80 after the follower has settled at
`(-1936,-503.527)`. This is a **position jump**, not a continuously walking
target. JASS records the actual target position as `(-1936,112)`. The completed
`runtime/follow-shift.jsonl` / `runtime/follow-shift-analysis.json` show:

- Group path changes from `(163.5,91.5)` to `(163.5,99.5)` fine-grid destination
  at owner counter 1300, both timestamps zero, `changed=ready=1`.
- It performs a 16-pop accelerated search with budget 5000. The individual
  arrival predicate changes from arrived to out-of-range for the new target.
- The individual path's old destination is already sentinel `(-128000,-128000)`
  when its gate runs, so the earlier stop routine invalidated it, as the reset capture above proves.
  Do not attribute all update behavior to `6f167e40`.
- The individual path performs a 16-pop accelerated search with budget 400,
  then a 96-pop fine search with budget 700. At counter 1301 it is unchanged,
  `ready=1`, despite the fine timestamp now being 1300: the same-cell branch
  bypasses timestamp gating as predicted.
- The follower reaches `(-1966.158,-244.526)` and retains follow order `851971`
  through tick 300. The lateral deviation belongs to this map's resulting
  route; it is not evidence of a different follow-range formula.

All eight emitted replan observations match the oracle formula. The observer
emits only changes of destination/result state, not every call. The analyzer's
`--scenario follow_shift --require arrival` validates the target edit, later
search and ready cell change, all timer samples and observer completion.
The continuous-movement experiment below extends this; sub-cell target edits
remain open; the ordinary polling cadence is recovered below.

### Continuously moving follow target

`follow_walk` uses the same two Footmen, but at tick 80 issues an ordinary Move
to the target toward `(-1936,112)` instead of setting its position. Both units
request move speed 100 (the observed floor remains about 150). The probe records
300 target XY/order samples alongside 300 follower samples. Completed capture
`runtime/follow-walk.jsonl` and `runtime/follow-walk-analysis.json` pass
`--scenario follow_walk --require arrival`; maximum target sample displacement
is `14.998017535661306` world units, so this is incremental movement rather
than a position jump. The target remains at its original position before its
order and ends at `(-1951.584,110.461)`, order zero.

The active follower-group path updates its destination at owner counters
**1300, 1317, 1334, 1351, 1368**. Its five accelerated searches consume
**3, 3, 4, 6, 12** pops respectively, each with budget 5000. Its individual
path performs matching accelerated searches with budget 400 and fine searches
of **11, 23, 27, 28, 41** pops with budget 700. The target independently needs
one group accelerated search, one individual accelerated search (nine pops
each), and two fine searches (33 and 20 pops). They must not be counted as
follower replans.

All five follower-group change gates have timestamps `[0,0]` and return
`changed=ready=1`. The repeated 17-counter gaps and approximately half-second
wall-time spacing are therefore **not** evidence that the gate's minimum is
17; the verified minimum remains 10, and inexpensive accelerated requests
clear their timestamp. The distance-dependent refresh counter described below produces this cadence. The final follower destination is the target's actual stopped
position `(163.0129852294922,99.451904296875)` in fine-grid coordinates, not its
requested point `(163.5,99.5)`. The follower ends at `(-1942.204,-247.425)` with
follow order `851971`, about 358.009 world units from the target, consistent
with its 362-unit arrival threshold. Target point arrival independently uses
0.49 fine cells.

**Pooled-address evidence:** group path address `0x12e60210` first receives a
non-null target for the follower, then at the target's tick-80 Move is reused
with a null target. The continuing follower group uses `0x12e602cc`. Treating
an address as a permanent semantic identity incorrectly attributes the target's
nine-pop group search to the follower. The analyzer now associates each search
with the most recent `group-target` event for that path address. A regression
case verifies that reassignment to null stops counting that address as a
following group. This establishes reuse in the capture; full allocation/free
lifecycle and stable object identities still need mapping.

### Target-position refresh counter

Group `+64` is the refresh countdown. Destination sampler `6f16cd30` resolves
the target; if the target callback allows a fresh sample and the countdown is
zero, it sets `+64=-1`, sets group flag `0x1000`, and returns the target's current
fine-grid position plus group offset `+4c/+50`. While that countdown is nonzero
it returns the current path destination. If no target resolves, it clears
`+64/+6c` and returns the fixed group XY. The installed callback is an inverse
visibility query, described below.

After movement processing, group tick `6f16c150` calls `6f169680`. Ordinary
positive counters decrement once; zero stays zero. The special value `-1`
requests a reload. It measures the distance between the **first member's
position** and group path destination (`path+1c/+20`), in fine cells. Using
engine numeric helpers, it computes:

```
candidate = trunc(computedDistanceFine * 0.33 + 0.5)
reload = clamp(candidate, 16, 132)
if group.flags80 & 0x400:
    reload += 165
```

The coefficient at `6fd541b8` is initialized by `6f004130` from text `"0.33"`
at `6fa91ea0`; live float value is `0.33000001311302185`. The additive constant
`6fcd53f4` is exactly 0.5. These are deterministic engine arithmetic operations,
not a promise of identical rounding from host IEEE expressions at every
boundary.

The extra-delay flag has a recovered **captain AI request producer**:
`6f9d1040 → 6f89cca0(1) → 6f16dc00(1)`. The first routine allocates a
16-byte `CMoveReq` with the binary's `CCaptainAI.cpp` source string (line
`0x4be`), sets a point or target, and enables this option. The wrapper resolves
the request's `+8/+c` handle; `6f16dc00` sets/clears only bit `0x400` of the
resolved request payload's `+100` flags. Request-to-group handoff `6f16bdb0`
copies that entire word to group `+80` (`6f16be35..6f16be43`); no bit
translation occurs. The constructor helper has direct callers `6f9d27c0`
and `6f9d16c0`. This establishes a captain AI policy branch, not that every AI
order enables it. Its live activation and gameplay rationale remain untested.

Since the sampler runs before the countdown update, reload 16 produces samples
17 owner updates apart: sixteen intervening decrements, then another sample
on the next visit at zero. Ordinary reloads therefore give periods of 17–133
updates; flag `0x400` extends that to 182–298, assuming the group keeps updating
and the target callback allows sampling. This is independent of the search
admission buckets and the 10-counter destination-change gate.

Completed `runtime/follow-refresh.jsonl` adds entry/exit and two read-only
instruction probes. All **97** captured refreshes use the live 0.33 coefficient
and reload 16; all **95** intervals within unchanged target assignments are
exactly **17** counter ticks. Examples: computed distance 26.0004634857 yields
candidate 9, while 11.1878528595 yields candidate 4; both clamp to 16. The
`--scenario follow_walk --require refresh` analyzer passes and checks each
clamp/flag decision, without equating owner ticks to an assumed fixed duration
in simulation seconds.

`tools/ghidra/verify_wc3_pathing_refresh.py` verifies **4,002** reload cases and
**298** countdown cases. The reload instruction slice starts at `6f1696c8`,
after member-position conversion, with explicitly supplied fine-grid locals;
it executes all original distance, multiply, add and integer-conversion helpers
and stops at `6f169781`. The independent assertion covers clamping and the extra
flag using the observed integer candidate. It does **not** independently
reimplement the engine numeric helpers or the member-position resolver. The
countdown cases execute the full original routine. A further **12,288** cases
execute the full request flag setter and original request-to-group copy slice,
covering all low 12-bit flag combinations, preserved high bits, and Boolean
inputs 0/1/2. They verify the handoff primitives, not the entire captain AI
constructor or request lifecycle. No helper functions are stubbed. Report:
`/GitHub/wc3-analysis/reports/pathfinding-1.27/refresh-oracle.json`.

### Target visibility gate

Owner callback slot `+258` is installed by `6f1eab40` through
`6f04dc90/6f15cc80`; dispatcher `6f15bb40` calls `6f23a760` with target mover
in ECX and group in EDX. The callback resolves gameplay objects through mover
`+30`, wrapper `+54`, and `6f1d73b0`. Assembly at `6f23a7dd..6f23a7e4`
confirms the call is `6f66fdd0(firstMemberUnit, targetUnit, 0, 4)` (thiscall),
then negates its result. Ghidra's displayed prototype obscures the `this`
argument; interpreting the target as the receiver reverses the policy.

This is a visibility check relative to the **first member's owner**:

- `6f66fdd0` obtains the receiver's player via virtual slot `+ec`; the unit
  vtable `6fb77eb0` points that slot at `6f685da0`, which returns unit `+58`.
- It queries `6f1dd920(player, target, flags, 4, NULL)`, with flags normally
  zero and a global option able to add bit 0. If that query fails, it checks
  the target's per-player mask `(unit+148 | unit+14c)` through `6f699b20`.
- JASS registration `6f209b46/6f209b4b` maps `IsUnitVisible` to `6f2068d0`.
  That native calls target virtual slot `+fc` with `(player, 0, 4)`.
  The same unit vtable maps `+fc` to `6f66ee10`, whose assembly performs the
  identical query and mask fallback with the explicit player argument.
  `IsUnitInvisible` (`6f2064f0`) negates the same virtual query; detection is
  a separate native (`6f205fd0 → 6f68bae0`).

Thus, for successfully resolved unit objects, the routing callback returns
true when the target is not visible to the first member's owner. Destination
sampling then retains the current path destination and increments group
`+6c`, rather than reading the target's new coordinates. A false callback
permits the normal countdown-controlled sample. Missing/ineligible target
wrappers or missing resolved units return false; they are not an explicit
"not visible" result. The full visibility implementation includes unit flags,
player masks and fog-cell state; this finding must not be reduced to distance
or invisibility ability status alone.

This is a **static call-chain identity**, established from original assembly,
vtable entries and native registration. The completed friendly follow runs
exercise visible targets only. A controlled invisibility capture below instead cancels the Smart order
before this callback can block. The fog experiments below separately establish retained-group loss and
reacquisition. The global visibility option's gameplay name remains open. Raw decompilation and assembly are saved as `*-policy-*.json`
in the analysis report directory.

### Smart-follow cancellation on invisibility

The `follow_invisible` copied-map scenario exposes a lifecycle boundary before
that group callback. It creates a neutral-passive Footman target with mutual
passive alliance to player 0, issues Smart at tick 10, adds permanent
invisibility (`Apiv`) at tick 60, changes target Y from -144 to 112 at tick 80,
and removes `Apiv` at tick 160. Fog/mask remain disabled, so each tick records
`IsUnitVisible(target, Player(0))` explicitly; adding an ability is not itself
proof that the target became hidden.

Completed captures `runtime/follow-invisible.jsonl` and
`runtime/follow-invisible-stop.jsonl` both show:

- Visibility remains true through tick 79, becomes false at tick 80, and is
  true again from tick 160. This fixture's fade takes about two simulation
  seconds after adding the ability.
- The follower reaches `(-1936,-503.527)` and retains Smart order `851971`
  through tick 79. It has **order 0 already in `before_hidden_shift`**, before
  the JASS position change. It stays at that position with order 0 through
  tick 300, including after visibility returns.
- Only two group visibility transitions are observed, both unblocked (`0`).
  There are 233 callback calls, 15 refresh reloads, three accelerated searches
  and one fine search. The group never supplies a blocked callback witness;
  this is **cancellation, not persistent following of a cached target**.

Read-only stop backtraces in the second capture identify the cancellation
chain. Ghidra then resolves the corresponding call sites:

```
6f68b780 (invisibility state transition)
  → 6f651010 (CEventTargetLost, event code 0xd01a4)
  → event dispatch → 6f5fda10 (CAbilityMove vtable +0x0c)
  → 6f5ff490 (target-lost handler)
  → 6f5fb190 → 6f5fa7a0 → 6f69a840 → 6f05ca50
  → 6f171340 (mover stop / route invalidation)
```

`CAbilityMove::vftable` is at `6fb62794`; its `+0c` entry points to
`6f5fda10`, whose `0xd01a4` case calls `6f5ff490`. Event constructor
`6f651010` installs `CEventTargetLost::vftable` and puts the affected target
at event `+c`. Handler `6f5ff490` first calls `6f5fb940` for that target.
The latter returns zero for a valid visible target, or `0xdd` for a failed
visibility query (also used by other invalid-target branches). Its assembly
calls the same `6f66fdd0(ownerUnit, targetUnit, 0, 4)` query at `6f5fb9ec`.
Other target flags can return `0xa9/0xaa`; those meanings remain separate.

A third completed capture, `runtime/follow-invisible-event.jsonl`, observes
the notification directly: one dispatch from return RVA `0x68b7d8`, one
Move handler with event `852388` (`0xd01a4`) and the same target unit, then
validation result **221 (`0xdd`)** for that exact ability/target pair, followed
by a stop call whose stack includes `0x5ff645` and `0x65108f`. All occur
before the tick-80 position edit. The target validator also reports `0xdd`
for a subsequent null target; that separate cleanup call is not used as
proof of rejection of the actual target. `--require target-loss` requires
the linked notification/handler/actual-target validation/stop chain.

The scenario validator now asserts the observed cancellation contract:
controlled visibility phases, cancellation before the position edit, no
subsequent movement/order resumption or group destination changes, and visible
control callbacks linked to the target assignment. It deliberately does not
report this fixture as proof of the group's hidden-target hold/reacquire path.
Corruption tests reject absent hiding, cancellation after the jump, resumed
orders, unrelated callback targets and missing samples.

Reproduce using `make_wc3_pathfinding_map.py --scenario follow_invisible`,
a new output under the retail `Maps` directory, then the bounded controller
with `--seconds 100 --continue-at 50 --x11-display :94 --samples 2000` in the
owned display/server environment. Analyze with `--scenario follow_invisible`.
No retail data or production movement implementation is changed.

### Fog loss, cached-position pursuit and reacquisition

Fog loss reaches the group callback without the invisibility transition's
`CEventTargetLost` notification in these fixtures. `follow_fog` uses the same
neutral-passive target and Smart order, but enables fog and installs a
`FOG_OF_WAR_FOGGED` radius-600 modifier centered on the target's original
position at tick 60 (`useSharedVision=false`, `afterUnits=true`). It moves the
target at tick 80 and destroys the modifier at tick 160. The shorter
`follow_fog_reacquire` moves the target at tick 70 and removes fog at tick 75.
Both record all 300 target visibility/position samples and follower orders.

The static path now explains what the unseen counter does:

1. `6f16cd30` increments group `+6c` for each blocked visibility callback and
   returns the current path destination. A successful callback clears `+6c`
   **even if the refresh countdown is nonzero**. No resolved target clears
   both counters and returns fixed group XY.
2. Member stepping `6f16a790` temporarily replaces mover arrival range `+b0`
   with `0.49000000953674316` fine cells when `group+6c != 0`. It calls the
   normal movement/arrival machinery, then restores the saved range. A
   hidden target is therefore approached at its cached position using a
   tighter range than ordinary following. This override does not permanently
   rewrite the unit's authored follow range.
3. Completion processing `6f16c390` returns immediately when group flag
   `0x1` is set and unsigned `+6c < 33`. Otherwise it increments group `+60`
   and considers member completion. Member flag `0x10000` is required to
   stop that member through `6f16d4e0`; additional multi-member/`0x20000`
   branches may reset routes instead. **33 missed samples opens a completion
   gate; it does not force completion on the 33rd sample.** This routine is
   reached when the request is ready and accelerated waypoint index is zero,
   as reported by `6f16ce10` to group tick `6f16c150`.
4. Flag `0x1` is set/cleared by request-payload setter `6f16dc30` and copied
   to group `+80` through the established request handoff. Target-move setup
   `6f05a5c0` enables it together with flag `0x800` when its seventh stack
   argument is nonzero. `CMoveReq` wrapper `6f89cb30` also exposes it;
   the previously recovered captain AI helper `6f9d1040` explicitly passes
   zero to that wrapper. The complete gameplay policy of all callers remains
   to be mapped.

`verify_wc3_pathing_refresh.py` adds **1,035** complete-routine gate cases
with an empty member list, including unsigned counter boundaries and `+60`
overflow; **72** original sampler-tail cases with supplied callback result
and nonzero countdown; and **20** original arrival-range override/restore
slice cases. These execute without function stubs. They verify these
primitives, not target resolution, full visibility or multi-member movement.

Completed `runtime/follow-fog.jsonl` shows the callback blocking at owner
counter **1225**. The follower leaves its ordinary stopping point
`(-1936,-503.527)` and approaches cached target fine XY `(163.5,91.5)` with
range `0.49`. It eventually stops at `(-1936,-220.058)` before the tick-80
position edit, and does not resume after tick 160. Its last arrival succeeds
via mover flag `0x10000` at fine Y `89.1231918`, outside the 0.49 range; the
producer of that forced-arrival flag is traced below. Do
not describe this run as reaching the exact cached point. No target-lost
dispatch is observed.

Completed `runtime/follow-fog-reacquire.jsonl` provides the retained-group
witness:

| Owner counter | Observed state |
| --- | --- |
| 1164 | Follow group flags `0x21801`, unseen count 0; completion gate closed despite an arrived member |
| 1225 | Visibility blocks, countdown 7; unseen count becomes 1; arrival range narrows to 0.49 |
| 1257 | Same group, unseen count **33**, completion gate opens; member is not arrived and keeps moving |
| 1278 | Same group/target visible again, callback sees prior unseen count **53**, countdown 0; sampler resets unseen count to 0 |
| 1278 | Group destination becomes `(163.5,99.5)`, arrival range returns to **11.3125**, completion gate closes again |
| 1283 | Member arrives at the new follow range, but persistent-follow completion remains suppressed |

The modifier was removed at timer tick 75; callback reacquisition is observed
after the tick-76 sample, so the test allows fog-update latency instead of
assuming synchronous visibility restoration. The group destination never
changes during the hidden interval. All follower samples from tick 10 onward
retain order **851971**. Final position is `(-1937.180,-247.212)`, about 359.2
world units from target `(-1936,112)`. There are six accelerated and three
fine searches, and no target-lost notification. Thus loss through fog and
loss through this invisibility transition have different observed lifecycles.

Analyze with `--scenario follow_fog` or `--scenario follow_fog_reacquire`.
The shorter fixture requires same-group callback identity, no destination
update while hidden, the exact new destination on reacquisition, continuous
Smart order, arrival-range restoration, and the 33/reset completion-gate
observations. Negative tests reject unrelated groups, stale destinations,
TargetLost dispatch and a missing threshold witness. Both maps preserve the
source terrain and modify only the copied `war3map.j`.

### Forced arrival from route advancement

Setter `6f171060` ORs mover `+d8` with `0x10000`; clearer `6f16f560`
removes only that bit. The currently identified direct callers of the setter
are group stepping `6f16a790` (group flag `0x200`) and mover stepping
`6f16fbd0`. In the latter, the branch immediately after `Path_Advance` has
this contract:

| Advance result | Destination-change readiness | Effect |
| --- | --- | --- |
| 0 | Either | Clears the local movement-enable value |
| 3 | Nonzero | Sets forced arrival |
| 4 | Nonzero | Sets the extra status output and forced arrival |
| 3 or 4 | Zero | Does not set forced arrival |
| Other | Either | Neither of these forced-arrival effects |

Readiness here is the result of `6f167e40`, including its same-cell ready
case; it is not synonymous with destination coordinates changing.
`verify_wc3_pathing_arrival.py` executes the original result-handling slice
`6f16fd0a..6f16fd3b`, its real flag setter, and full flag clearer across
**96** combinations of return code, readiness and pre-existing flags. No
functions are stubbed. This verifies the consumer, not the complete route
advance computation.

Completed `runtime/follow-fog-force.jsonl` repeats long fog loss with
read-only observers on `Path_Advance` and the setter. Exactly one setter call
is observed, from return RVA **`0x16fd32`** at owner counter **1288**. Its
same-path, same-counter advance result is **3**, both waypoint indices are
zero, and its destination remains `(163.5,91.5)`. Mover flags change from
0 to 65536. The subsequent arrival observation succeeds with that flag at
fine Y `89.1231918`, beyond the temporary 0.49-cell arrival range. This
identifies the mover branch, rather than the group flag-`0x200` branch, as
the producer in this capture.

Static producer chain: fine progress `6f165c60` can fall through to
`6f167290`. That routine returns 3 when `6f166310(position, NULL)` succeeds;
otherwise it initializes/consumes path retry count `+98`, returning 4 when
that count equals one, or resetting fine route buffers and returning 1.
`6f166310` uses an explicit object argument or path target pointer `+a4`,
requires a live object (`+38 != -1`), and tests its occupancy on a square
perimeter via `6f148790 → 6f14a710 → 6f148850`. The four footprint classes
select `(origin offset, width)` `(1,3)/(2,4)/(2,5)/(3,6)`. This is a
**target-object perimeter condition**, not a claim that the requested point
was reached. The matched-cell/object witness is recovered below; retry-policy branches
still require independent coverage.

The analyzer rejects setter records that change other flags, or whose mover
call site lacks a matching path/current-counter advance result 3 or 4.
Corruption tests cover stale observations, unrelated result codes and lost
pre-existing flags.

### Target identity on the footprint perimeter

`6f148850` walks the fine cell's low-24-bit link head through map `+78`
eight-byte records. It skips type-2 search metadata even when that record's
payload equals the requested pointer. For any other type, pointer equality
terminates the search; the result is true only if that matching record is
**type 1**. A same-pointer non-type-1/non-type-2 record therefore rejects
without searching later links. This is an identity query, independent of
terrain high bits and the ordinary occupancy query's blocking masks.
`6f14a710` adds unsigned map bounds, non-null target and target `+38 != -1`
checks before the link walk.

`6f148790` enumerates the square perimeter in the same clockwise order used
by the footprint scan: top row left-to-right, right edge downward, bottom
row backward, left edge upward. It returns on the first matching cell.
Interior cells are not queried. Out-of-map samples simply fail the bounded
cell predicate.

New no-stub original-x86 oracle `tools/ghidra/verify_wc3_pathing_target.py`
passes **11,526** mixed-link/empty-chain cases, **75** bounds/null/dead-object
cases, and **10,240** full perimeter scans. The latter place the target at
every cell of a 16×16 map for all four footprint classes, with live/dead
states and centers at the map middle and four corners. Mixed-chain cases
cover all 256 leading type bytes, alternate pointers, metadata aliases,
later matching records, and ignored terrain high bits. This verifies the
predicates with well-formed synthetic lists, not arbitrary corrupt chains
or the complete unit movement lifecycle.

Completed `runtime/follow-fog-perimeter.jsonl` supplies the live bridge.
At owner counter **1288**, `6f166310` calls the perimeter routine with
center **(163,89)**, offset **2**, width **4**, and object **`0x11734a40`**.
The first successful cell is **(163,90)**. The subsequent `Path_Advance`
record has the same target pointer at path `+a4`, result **3**, and counter
1288; the forced-arrival setter follows in that update. This is the object
identity used by the path query, not a JASS handle or gameplay-unit address.
The target's requested center remains `(163.5,91.5)`, explaining why a
perimeter-based terminal condition can stop short of the center.

Analyze with `--scenario follow_fog --require target-perimeter`. In addition
to the scenario checks, the analyzer verifies footprint dimensions, that
the reported match lies on the perimeter, and the same-target/same-counter
association when a result-3 forced arrival is captured. Reproduce the oracle:

Oracle: `verify_wc3_pathing_target.py` → `target-oracle.json`.

### Retry exhaustion and route result 4

When target-perimeter acceptance fails, `6f167290` consumes path `+98` as
an attempt counter. Zero initializes it through `6f1689d0`; one returns
**4** immediately without changing the counter or buffers. Every other
nonzero value calls `Path_ResetBuffers(0,0,0,0)`, decrements `+98`, and
returns **1**. The reset clears only fine-route count/index (and its usual
unconditional disabled flag), preserving accelerated route count/index,
queue membership, result flags and retry state. Repeated calls at count one
keep returning 4 until another lifecycle operation resets the path. This is
an attempt count, not a timer or number of owner updates.

Initialization measures squared distance from current fine-grid position to
path's **adjusted destination `+24/+28`**, using `6f070850`. Startup
`6f0040e0` initializes threshold `6fd54190` from integer `0x90` through
`6f070d80`, i.e. 144. The short branch uses computed distance squared
**<=144** and resolved mover group member count **<=1**:

- Short branch initializes `+98=2` without consuming random state.
- Otherwise `6f1d62b0(2, pathingOwner)` returns 0 or 1 and the initializer
  adds 7, producing **7 or 8**. It advances the owner's PRNG state through
  `6f1b7130`; the local `(6,8)` pair is not a seed or an inclusive 6–8
  range. Assembly confirms the PRNG receives the second stack argument,
  despite Ghidra's incomplete displayed prototype.

144 squared fine cells corresponds to a nominal 12-cell / 384-world-unit
radius. The engine's computed squared distance controls the boundary;
complete numerical parity for arbitrary fractional vectors remains separate.

`tools/ghidra/verify_wc3_pathing_retry.py` passes **112** selection cases
at/below/above the threshold, for group counts 0/1/2/10 and four PRNG states.
It executes the original comparison and selection slices with supplied
computed distance and resolved group; the original PRNG runs without a
stub. Short cases preserve its state, long cases advance it, and observed
initial counts are exactly 2/7/8. **35** full `6f167290` cases use a null
target and pre-existing nonzero counts, checking decrements, result codes,
fine reset, preserved accelerated route and result flags. Three complete
countdown sequences add **20** full calls: starting at 2 yields `1,4,4`;
starting at 7 or 8 yields six or seven retry results followed by persistent
4s. These fixtures do not execute the group-handle resolver or the complete
zero-count initialization through full movement. The blocked-goal fixture below supplies a live short-count result-4 witness;
live long-count and multi-member policies remain outstanding.

Oracle: `verify_wc3_pathing_retry.py` → `retry-oracle.json`.

### Live blocked-goal retry exhaustion

`blocked_goal` marks a 5×5 patch unwalkable at initialization: X -2000 through
-1872 and Y -208 through -80, in 32-world-unit steps. The ordinary point
order still requests `(-1936,-144)` at tick 10. Only copied-map JASS changes;
this fixture performs its edits before the map's final hierarchy rebuild,
unlike the previously documented mid-simulation stale-hierarchy experiments.

Completed `runtime/blocked-goal.jsonl` shows two movement phases. The group
accelerated search returns -1 after 130 pops with a 5000 cap; the mover first
approaches fine goal `(167.5,89.5)` with a normal 0.49-cell arrival range.
After reaching that point, it attempts the original `(163.5,91.5)` goal.
The second mover accelerated search returns -1 after 133 pops with a 400
cap, and two fine searches each stop at **701/700** pops. Those fine results
are budget exhaustion; they are not independently proof of disconnection.

At owner counter **1261**, retry initialization sees current position
`(163.8741913,88.5391846)`, adjusted path destination **(167,91)**, threshold
squared **144**, and selects count **2**. The first `6f167290` call returns
1 and decrements the count to **1**. At counter **1262**, the next call
returns **4** and retains count 1. `Path_Advance` returns 4 on that same path
and counter, with a null target object, and caller `0x16fd32` sets forced
arrival. This is separate from the target-perimeter/result-3 witness.

The next arrival check has forced-distance acceptance but heading error
`-0.3496363163`, so it still returns false. At the same position, the later
check has zero heading error and succeeds. Final world position is
**(-1924.026,-238.746)** with order 0 through tick 300. Retail therefore
performs an adjusted-point approach and a subsequent retry attempt before
stopping; the rejected destination is not simply replaced once at order time.

The observer records retry initializer inputs/counts and every target-or-retry
result. The analyzer verifies transitions (including unchanged result-3 and
terminal count-1 cases), the exact 25 edited coordinates, same-path and
same-counter result-4/force association, accepted forced arrival for that
mover, and final order completion. Run with `--scenario blocked_goal
--require retry-exhaustion`. The generated map/sidecar are
`Maps/PathingRE-BlockedGoal.w3m`, `.j`, and `.json` under the retail test data
root. The capture and analysis report reside in the external runtime report
directory.

## Target arrival range and heading

Smart-order setup `6f5fd270` looks up `Misc.FollowRange` or
`Misc.StructureFollowRange` through `6f047840`, depending on the target's
structure test. It submits the target/range through `6f6926b0`. Target-move
setup `6f05a5c0` converts both movers' `+90` footprints from fine cells to world
units, adds them to the supplied range, divides by 32, and clamps the result
to `DAT_6fcd53f0 = 0.49000000953674316` fine cells. Setter `6f1710a0` stores this
arrival range at mover `+b0`:

```
arrival_range_fine = max(0.49, (authored_range_world + mover_radius_world
                               + target_radius_world) / 32)
```

This is the target-move setup path; do not assume every movement producer uses
the same radius additions. Point movement and other callers of `6f1710a0`
remain separate paths to audit. The no-stub instruction-slice oracle executes
`6f05a702..6f05a764` with supplied range and already-converted radius locals:
150 combinations match the formula, including its minimum. It does not execute
the entire order setup or virtual calls.

Mover update `6f16fbd0` calls arrival predicate `6f16e910` **before** path
advancement. The predicate obtains distance using the engine math helper
`6f071480`, bearing via `6f1d4c80`, and signed heading difference via `6f173720`.
It snaps angular differences below `2.0000000233721948e-7` to zero. It writes
`inRange = (computedDistance <= threshold) || (mover.flagsD8 & 0x10000)`.
Its return value is `inRange && abs(angularDifference) <= 0.20000000298023224`
(radians). The flag forces the distance condition, not the heading condition.
On true, `6f16fbd0` zeros the movement output, clears that force flag through
`6f16f560`, removes queued path membership, and returns before route advancement.
With `inRange` true but heading not yet accepted, it calls `6f170880` and also
removes queued membership; the shifted-target run below supplies a stationary heading-correction witness.

`tools/ghidra/verify_wc3_pathing_arrival.py` executes the complete arrival
predicate without stubs: 522 distance/heading/force-flag cases include thresholds
one float32 step below, equal to, and above the **computed** distance. Distance
and angular helpers execute unchanged but are observed, not independently
reimplemented. This distinction matters: at input `(11.3125,0)` the original
computed distance is `11.312666893005371`, so a threshold of exactly 11.3125
rejects. The approximate math also produces a small nonzero angular result for
some exactly horizontal inputs. Exact engine numeric parity remains a separate
math recovery task.

The completed `runtime/follow-arrival-retry.jsonl` supplies a live witness:
threshold `11.3125 = (300+31+31)/32`, Footman footprint `0.96875 = 31/32`,
destination `(163.5,91.5)` fine-grid coordinates. The last rejected sample is
at Y `80.1241683959961`; the first accepted one at Y `80.26477813720703` has
zero heading error and zero force flag. Its remaining distance is
`11.235221862792969 * 32 = 359.527099609375` world units, matching the JASS
position `(-1936,-503.527)`. All 300 timer samples complete; the analyzer's
`--scenario follow --require arrival` passes. The earlier
`runtime/follow-arrival.jsonl` ended at tick 186 when its controller terminated
(exit 143), with no trace-end record. Its orphaned owned game was identified by
the capture PID and terminated before retrying; it remains incomplete evidence.
The retry uses 100 seconds total with Space at 50 seconds.

The completed shifted-target capture adds a heading-boundary witness. At fine
position `(162.5575714111328,88.35855865478516)` toward `(163.5,99.5)`,
`inRange=1` but angular error `-0.21355879306793213` makes arrival return zero.
On the next observed update, 32 ms later, **the position is identical** and the
error is zero; arrival returns one. This verifies separation of the distance
and heading conditions in live movement. It does not yet establish the full
turn-rate law or how facing evolves for larger angles.

## Waypoint skipping and obstruction handling

`6f165e60` updates the fine-route index from `6f167bf0`. The latter starts one
index closer to the destination, tests progressively farther route entries,
and stops when `6f168d30` rejects a segment. It temporarily excludes the mover's
own occupancy object through its `+0x40` counter. This is a consumer-side
waypoint-skipping mechanism, separate from parent-chain reconstruction.

`6f168d30` is a discrete straight-segment sampler. Its supplied inputs are
start, direction and length. Starting at integer parameter **k=1**, it computes
`start + k*direction`, floors both components, and tests only when that cell
changes. It advances k by one while **k < length**. Thus lengths at most one
return success without any cell query; neither endpoint is tested directly.
The initial previous cell is **(0,0)**, not floor(start), even though the
function calls the floor helpers on start before resetting those locals.
This is confirmed in assembly and original execution.

A changed cell gets a direction code from `6f167940`: X increase 2, X decrease
8, Y increase 4, Y decrease 1. The selected class (`6fd53a80`) dispatches to
`6f149440/149630/149970/149cc0` using fine system `6fd53a84`. For class zero,
cardinal transitions test the current cell; diagonal transitions also test
the two predecessor-side cells. Because the initial previous cell is zero,
the first sample at positive X/Y uses code 6 even for a cardinal segment.
The sampler returns false on the first rejected footprint test. This is not
a generic Bresenham ray or continuous swept-circle test.

With valid current fine index n≥1, `6f167bf0` starts with candidate index
n−1, then tests straight segments to entries n−2, n−3, …, 0. A failed test
stops that progression and returns the last accepted index. The starting
candidate n−1 is not itself tested by this loop. The function increments and
restores the self-occupancy `+40` counter, installs path mask `+9c` in the fine
system, and leaves the path index unchanged. `6f165e60` commits the returned
index to path `+74` and copies that route point to its output.

Normalizer `6f168280` computes a software-float length and, only if that is
greater than one, multiplies the direction by its reciprocal. Numerical
approximation matters to sampling. Original input `(3,0)` produces length
**3.0000576973** and direction **(0.9999808669,0)**; integer k=3 is therefore
included. From start `(8.25,8.75)`, this samples cell `(11,8)`, also the
mathematical endpoint cell. This is consistent with excluding the supplied
length endpoint; it disproves any blanket claim that the destination *cell*
is never tested. Inputs of length 1, 2 and 4 are exact in the measured corpus.

`verify_wc3_pathing_segment.py` runs **21,588** complete original sampler
cases with class-zero footprints, cardinal unit directions, three starts,
seven supplied lengths and every possible single blocked cell on a 16×16
map plus a clear-map case. It checks every visited cell in order and every
return value, including **9,252 unchecked short-segment cases**. A further
**1,028 complete waypoint-selector calls** and **1,028 commit-wrapper calls**
check selected indices, copied points, self-counter restoration, mask setup
and exception-chain restoration. No called routine is stubbed. The composed
selection model deliberately uses measured original normalizer outputs;
it is not an independent bit-parity replacement for the numerical helpers.
All footprint classes, oblique directions, dynamic blockers, complete movement-consumer integration remain to be verified. Global setup
ownership is recovered in the yielding/countdown section below.

Oracle: `verify_wc3_pathing_segment.py` → `segment-oracle.json`.

Next-step obstruction routine `6f166140` gathers occupied cells through
`6f149560/1497c0/149b00/149e60`, then calls `6f168360` to inspect mover
candidates. The collector and its yielding decision are now also verified
in composition through the complete `6f166140` entry point.

Cell collector `6f148ad0` appends a **null blocker token** for out-of-bounds
coordinates or intersecting terrain bits. Otherwise it traverses the lazy
object chain in stored order, ignoring type-2 metadata, dead objects and
objects without `+34` bit `01000000`. It stamps each first-seen object before
checking record type; only type-1 records with matching low-24 mask and
`object+40 & 8fffffff == 0` contribute. Unlike the ordinary occupancy
predicate, it ignores fine-system `+d4` and does not exempt flags `20000000`
or `40000000`. Helper `6f1480d0` accepts a mover payload only when payload
`+10 == 60706375`; null or other payloads become null tokens.

Append admission stops at **32 entries**, including nulls and duplicates.
Deduplication is per cell, not across the queried footprint: an object that
occupies several queried cells can consume several slots. Terrain tokens
also consume capacity. An already oversized vector is not truncated by the
cell helper. The actual wrapper clears its previous vector before collection.

`6f166140` normalizes waypoint minus current position, floors current and
one-step-ahead positions, derives the crossing direction and dispatches the
footprint-class collector. It temporarily increments its own occupancy
object's `+40`, copies the path mask to the fine system, resolves moving
blockers and restores the self counter. Its Boolean result means **the
collected vector is empty**, not “no moving blocker was assigned.” A static
blocker or stationary mover can therefore return false without setting a wait.

`verify_wc3_pathing_blockers.py` checks **3,072 original cell-collector calls**
across 1,024 mixed-chain sequences, including exact entries/order, stamps,
flags, null tokens and the cap. It additionally runs **756 complete next-step
calls** through the original normalization, class-zero collector and handle
resolver/yielding code. Cases vary clear/terrain/self/peer/static/masked/
suppressed occupancy, both velocities, group relationship, group bit 8 and
prior countdown. They check vector clearing, result, both blocker identities
and countdowns, mask setup, self-counter and exception-chain restoration.
All pass without stubs; vector storage is preallocated. This composition
covers cardinal class-zero steps; larger and diagonal footprint composition,
allocator growth and a single dynamic-blocker + refill + hierarchy composition
remain open. Separate refill, enabled hierarchy advance and existing-waypoint
retry compositions are verified below.

Oracle: `verify_wc3_pathing_blockers.py` → `blocker-collector-oracle.json`.

### Engine segment and waypoint integration

The [engine port](retail-pathfinding-engine.md#retail-segment-sampling-and-waypoint-selection)
now covers all four static directional footprint consumers, software normalization
and the original selection loop. Its43,244 complete original sampler comparisons
and123 supplied-chain selection/commit comparisons match C at O0/O2. The earlier
class0/cardinal baseline remains valid; dynamic object composition, reconstruction
coordinates and public endpoint admission remain explicitly separate.

## Fine-route refill and admission

`6f167ce0` compares the unsigned fine index `+74` with fine count `+50`.
A valid index returns that stored XY immediately, bypassing scheduling and
search. An exhausted index (including `ffffffff`) selects a destination via
`6f167d70(output,0)` and calls the path-owned request `6f166e90`. A denied
request returns zero without writing the waypoint output. An admitted request
returns the indexed point from the resulting route, including partial routes.

Destination selector `6f167d70` has two demonstrated modes:

- Mode zero uses the accelerated point at `+78` when that index is nonzero
  and below accelerated count `+70`, multiplying by the accelerator map's
  `+64` scale (two in the normal base accelerator). Otherwise it copies path
  `+1c/+20`, the current destination, **not the mover's current position**.
- Mode one copies the adjusted destination at path `+24/+28` directly.

The path request checks its fine interval, selects the fine scheduler bucket,
and asks for admission before running `6f148100`. It accumulates actual search
pops into bucket work `+8`; when the resulting cumulative work is below 64,
it clears the path's fine timestamp `+7c`. The request's success return means
admitted processing, not a complete route to the goal. Endpoint inequality
against the selected destination sets path `+88` bit `10000000`.

When fine-system `+d0` is nonzero, helper `6f168870(mode0)` initializes the fine
index to `count-2` for counts greater than one, or zero for one point. Otherwise
the request sets index zero. This skips the source entry at the tail of a
normal reconstructed reverse route. The index setter's accelerated mode uses
`count-1`; that branch remains outside this refill corpus.

`verify_wc3_pathing_refill.py` executes **72 full `6f167ce0` refills** through
original destination selection, scheduling admission, search, reconstruction
and indexed retrieval. Fixtures are open terrain, a solid wall and a wide-gap
wall, each with four footprint classes, search budgets 0/5/700 and either
path destination or a scaled accelerated point. Each route is compared byte
for byte against the complete underlying request from identical initial map
state. Assertions also cover selected index/output, actual work charge,
timestamp, endpoint-mismatch flag and exception-chain restoration. Storage is
preallocated and no called routine is stubbed.

A further **144 denied refills** cover interval rejection and over-budget
scheduler rejection; search state/output remain untouched, and only the
budget case appends the path to the admission queue. **72 cached-index cases**
confirm retrieval bypasses those gates. Thus a zero getter result can mean
scheduling denial, while a successful getter can expose a partial or even
source-only route. Neither Boolean alone establishes reachability.

This composes already recovered primitives; it is not an independent numeric
or path-optimality oracle. Mixed-object topology, queue contention, allocator growth and physical movement
after refill remain to be composed. The initial dynamic-occupancy extension
below now covers self/target suppression in the whole chain. Static search graph correctness has its separate
[Dijkstra corpus](retail-pathfinding-search.md#complete-static-fine-grid-searches-and-stamp-reuse).

Oracle: `verify_wc3_pathing_refill.py` → `refill-oracle.json`.

### Object occupancy and target exit through the full refill

The refill oracle also runs **288 complete object-occupancy refills**: a
single object occupies a full-height wall at X=12, with four footprint
classes, budgets 0/5/700, roles none/self/target/both, and six initial `+40`
flag/counter values. The live-cell lazy records coexist with search-node
metadata; no query or search is replaced. A read-only observer at original
`6f148100` entry verifies the exact counter during the search. When the same
object is both self and target, it is incremented twice and decremented twice.
All original values are restored, including synthetic unsigned wrap cases;
those boundary cases are not evidence that retail normally reaches overflow.

Each wrapper result matches a complete underlying request with explicitly
prepared expected counter state, including byte-exact route, selected point,
index and work charge. **144 non-target cases additionally match the prior
static open/wall controls** byte for byte with identical pop counts. This
checks that suppression changes actual search topology, not just a diagnostic
counter. Initial `20000000`/`40000000` exemptions and high/low suppression flags
are included under the search's normal `+d4=0` policy.

Collision suppression does **not** suppress target identity. With initial
counter zero and budget 700, the class-zero unsuppressed ordinary wall exhausts
at endpoint `(11.5,19.5)` after **420 pops**. Designating the same object as self
makes it traversable and reaches requested `(19.25,19.75)` after **28 pops**.
Designating it as target instead returns underlying search success after
**8 pops**, with endpoint `(11.5,11.5)` beside the object's wall. Classes 0/1
produce that endpoint; classes 2/3 stop at `(10.5,10.5)` after seven pops,
consistent with their larger target-query footprint. These are synthetic
fine-grid coordinates, not a live map replay.

The target-success endpoint differs from the requested point, so path flag
`10000000` is set even though the underlying search returned success. Treat
it as **endpoint mismatch**, not proof of failed search. Also, fine-system
`+d0` is an observed-obstruction flag from occupancy queries, not the search
success Boolean: unobstructed/suppressed cases initialize path index zero,
whereas encountered obstruction can initialize near the route's source end.
The zero/nonzero `+d0` distinction and all selected indices are checked in the
composed corpus.

The observer initially missed an already translated Unicorn entry block;
flushing the emulator's translation cache after installing the observer
restored the hook. No retail behavior was changed, and missing observations
remain assertion failures rather than being ignored.

The separate2,304-case target matrix now freezes the core and complete fine
requests for both overlapping link orders, terrain-first rejection, inactive,
unlinked, moving and off-lane target records. C matches every cost/work/node
and parent-chain result; the engine consumes separate active ground targets.
See [target identity exits](retail-pathfinding-engine.md#target-identity-exits-reach-the-engine).
Public runtime link chronology is retained as FINE-01.6.

### Public path advance composed with refill

The oracle executes **108 complete `6f165ae0` (`Path_Advance`) calls**
from an empty fine route, through context setup, accelerated-route processing,
fine refill/search, waypoint handling and next-step/retry decisions. The
matrix combines the three static maps, four footprints, budgets 0/5/700 and
three accelerated states: empty with accelerated search disabled, an existing
final coarse point, or an existing nonfinal coarse point. All calls use the
original code and real synthetic mover/group handles; none are stubbed.

`6f165b60` converts current and adjusted destination coordinates with the
accelerator map's inverse scale `+68`. If its accelerated index is exhausted
and path flag `200000` is clear, it clears that buffer, appends the converted
adjusted destination and sets index zero. This is a one-point coarse route,
not a coarse search. A valid cached accelerated index bypasses refill even
when the flag is set. Exhausted enabled accelerated search calls `6f166c30`;
that search path remains outside this particular composition.

For budgets 5/700 in this corpus, full advance returns **0**, supplies the
expected fine waypoint and clears retry state. It checks all four footprint
contexts and charges exactly the search work from the separate request
control. With budget zero, fine search returns only the source position:

- With final coarse index zero, full advance returns **1**, clears the fine
  buffer and initializes/consumes the distant retry budget, leaving six or
  seven retries. The caller destination is restored.
- With coarse index one, the reached source-only fine waypoint instead
  triggers the accelerated transition: return **2**, coarse index becomes
  zero, fine index becomes `ffffffff` but count remains one, retry count
  remains zero, and the output is the fetched source point.

Thus the same zero-budget fine result has different public outcomes depending
on accelerated-route state. These are single-call state-machine tests, not
simulated physical movement or proof that the unit reaches its destination.
The remaining composition includes dynamic
blocker yielding after refill, movement/turn integration, gate placement and
multi-tick progression under contention.

### Enabled hierarchy search through public advance

The refill oracle additionally runs **288 enabled-search public advances**:
two map sizes (24×24 and 64×64 fine cells), three static maps, four fine
footprints, accelerated budgets 0/5/400 and four traversal lanes. Base accelerator classifications are supplied from the
synthetic 2×2 fine-cell occupancy groups; all parent levels are built with the
original reducer. This tests supplied map state rather than the runtime map
construction/invalidation lifecycle. Both search engines, their path-owned
admission wrappers, accelerated consumption and fine stepping execute without
stubs or replaced branches.

Each public call is compared with a standalone original path-owned accelerated
request from identical initial state: coarse route bytes/count, adjusted
destination, actual work charge and endpoint-mismatch flag agree. In this
24×24 fine-map corpus, distance-limit consumption reaches coarse index zero.
The complete subsequent fine route, selected output/index and pop count also
match the separate fine-only controls for every case; all public calls return
zero. The larger-map intermediate-index branch is verified below.

A concrete consequence is that coarse failure/partial routing is **not a veto
on the next fine search** in these states. On open terrain with accelerated
budget zero, coarse search pops once and emits only its source; the adjusted
destination becomes fine `(4.25,4.75)`. Current destination `+1c/+20` remains
`(19.25,19.75)`, so fine search reaches it in 28 pops and outputs that requested
point. On the solid wall, accelerated budget 400 yields a partial adjusted
destination `(9,17)` after 12 pops; class-zero fine search still searches the
current destination, exhausts after 420 pops and supplies the next point
`(5.5,5.5)` of its partial route. The tests assert both destinations separately.

The 64×64 extension adds **144 enabled-search calls**, with **96 retaining
intermediate coarse indices**. After every public advance, a separate original
fine request is given the independently calculated destination—selected coarse
point times two for nonzero index, current destination for zero. Fine route
bytes, selected index/output and work charge agree exactly. Buffer storage is
expanded to cover the larger synthetic maps; no allocator is stubbed.

On open 64×64 terrain, class zero with accelerated budget 400 produces four
coarse points in four pops, retains index one, and sends fine search to
`(33.5,33.5)` rather than final `(59.25,59.75)`. Fine search takes 88 pops. Fine
class two (coarse size two) instead uses `(34.5,34.5)` and 95 pops, exposing the
size-dependent reconstructed coarse coordinates through the full caller.
For the gapped wall, class zero/budget400 retains index five of eight points,
fine goal `(33.5,31.5)`, and 150 fine pops; its immediate step output remains
`(5.5,5.5)`. The immediate movement waypoint therefore differs from both the
coarse-selected fine goal and the final requested destination.

These observations follow the verified mode-zero request-destination selector:
nonzero valid accelerated index selects an intermediate coarse point; index
zero uses current destination. They do not imply that adjusted destination is
unused—accelerated refill and retry-distance selection consume it elsewhere.
Way Gates, multi-tick contention and physical movement remain outside this
enabled-search corpus. Initial hierarchy exclusion/restoration coverage follows.

### Coarse self/target exclusion clears a region, then rebuilds

The refill oracle now includes **18 full path-owned accelerated requests**
with self, target, or both pointing to an occupancy object: three rectangles
(aligned, unaligned and touching the map origin), each with and without
coincident blocked terrain. Unlike earlier supplied-base tests, these initial
hierarchies are rebuilt from the actual synthetic fine-cell records through
`6f15d360`, including all four traversal lanes and all four levels.

A read-only observer at actual `6f162cb0` entry captures all **1,360
classification bytes** in the 32×32/16×16/8×8/4×4 hierarchy. Before search,
`6f166c30` calls `6f15d360(rect,1)` separately for self and target. That clears
the base classification byte and propagates parents. It does **not** increment
object `+40`; the entry observer and post-call assertions both see zero.
After search, calls with mode zero reconstruct the covered base cells from
fine occupancy and propagate parents again. Every classification byte matches
the pre-search baseline afterward, including the shared self/target-pointer
case. Search metadata is allowed to change and is not included in this
classification-only equality check.

Base coverage in these nonnegative fixtures is inclusive
`floor(min/2)..floor(max/2)` on each axis. Thus half-open fine object rectangle
`[16,20) × [16,20)` clears a **3×3** coarse area, not just the 2×2 cells entirely
inside the object. The test places an unrelated all-lane terrain blocker at
fine `(20,16)`, outside that object rectangle. Its coarse classification is
nonzero before search, zero at search entry, and restored afterward. The
same holds for the unaligned and origin-adjacent fixtures. Coincident terrain
inside the object is temporarily cleared at the coarse level as well.

This is a region-level exception in the coarse graph, not identity-filtered
fine collision suppression. It does not establish that fine movement may
cross that terrain: the fine map itself is unchanged, and subsequent fine
queries still have their own occupancy rules. Negative/out-of-map rectangle
clipping, nonzero origins, differing self/target rectangles, overlapping other
objects, map edits during exclusion and failure/exception restoration remain
outside this corpus.

## Fine advance: obstruction is not the same as a yield

The full `6f165c60` caller first saves the caller's destination, obtains the
fine waypoint through `6f167ce0`, and calls `6f167070` to consume or approach it.
The latter compares squared waypoint distance with the software-float square
of constant `0.49`. If farther away, it calls `6f1687e0` and returns zero.
That reset clears path `+88` bit `100000`, retry count `+98` and delay `+94`.
At a reached waypoint with fine index `+74 > 0`, it performs the same reset,
selects/commits an earlier visible waypoint, and also returns zero.

For that zero result, the caller runs next-step collection/yielding. The
composed original-code corpus establishes these distinct outcomes:

| Next-step outcome | Full fine-advance result and state |
| --- | --- |
| No collected blocker | Return **0**, retain the selected waypoint output |
| Requester assigned a moving blocker | Return **1**, retain waypoint and fine buffer; requester delay **4** in the tested reset-before-check path |
| Static/stationary/unresolvable-group blocker, no requester wait | Restore caller destination; initialize/consume retry budget, clear fine route, return **1** in the tested near-goal single-member case |
| Faster requester assigns slower peer delay **20** | Peer keeps its new blocker/delay; requester still restores destination and enters the retry path |

Thus return **1** alone cannot distinguish yielding from a fine-route retry.
In the tested obstruction cases, a previous requester delay of seven is
cleared before collection; it does not survive as seven or force waiting.
The isolated collector wrapper intentionally preserves that delay, making
its enclosing caller essential to the actual behavior.

When the final fine index and final accelerated index are both zero and the
fine waypoint is reached, `6f167070` instead returns **4** without the progress
reset or obstruction collection. `6f165c60` restores the caller destination
and delegates to `6f167290`: target-perimeter handling can intervene; with
no target, a retry count of one returns **4** unchanged, while larger counts
clear the fine buffer/decrement/return **1**. A zero count initializes first.
Unlike the approach branch, this route-end branch preserves an existing
delay. This distinction explains how retry exhaustion can persist at a
terminal partial waypoint despite the progress branch resetting retries.

The same blocker oracle now executes **756 complete `6f165c60` calls** in
addition to its next-step corpus, using actual retry initialization, group
handle resolution, distance helpers and fine-buffer reset. All begin with
an existing cardinal class-zero waypoint and a nearby adjusted destination,
so retry initialization chooses two for a single-member group and consumption
leaves one. A further **72 final-waypoint calls** cover four reached distances,
six initial retry counts (including zero and unsigned `ffffffff`) and three
flag patterns. Assertions distinguish destination preservation/restoration,
both movers' delays, retry count, fine count/index and flags. All pass without
stubs. This does not yet cover route refill, target-perimeter success in the
composed caller, or Way Gate execution inside the accelerated-index-nonzero branch. The ordinary
accelerated transition is covered by the transition oracle below.

The downstream decision routine is `6f168360`.
Its current mover is global `6fd53a8c`; the receiver is that mover's path.
It clears the path's stored blocker identity `+a8/+ac` first, but does not
clear its existing countdown `+94`.

For each candidate in vector order, it skips null entries, movers without a
resolvable movement-group handle, zero stored velocity and candidates whose
path already has a **resolvable** blocker identity. Group handles come from
mover `+9c/+a0` through `6f1702a0`; blocker handles come from path `+a8/+ac`
through `6f1680f0`. Both use the actual handle resolver `6f054530`, including
generation matching. It compares squared lengths of current velocity
`mover+80/+84`, not authored maximum move speed.

For an eligible candidate, the exact choice is:

| Condition (any suffices for first row) | Effect |
| --- | --- |
| Same movement group and group `+80` bit 8 clear; OR candidate speed ≥ current speed; OR different path player-class nibble | Current path records candidate identity, raises countdown to at least **4**, returns immediately |
| Otherwise: current mover faster, same player-class nibble, and no same-group prohibition | Candidate's path records current mover identity, raises countdown to at least **20**, continues scanning |

The compared nibble is path `+88` bits 16–19, whose player ownership provenance
is recovered in the scheduler section. It is **not** `repulsePrio`, and the
routine does not require repulsion to be enabled. The gameplay meaning of
movement-group bit 8 remains open. This is an asymmetric yielding rule;
countdown values are not yet a demonstrated duration in seconds or frames.

Writer `6f168070` copies a mover's identity words `+14/+18` into the target
path, or writes `(-1,-1)` for a null mover. It only increases the unsigned
countdown: `max(previous, requested)`. The 20-count branch changes the
**other mover's path**, not the requesting path. Earlier candidates told to
wait retain those changes even if a later candidate makes the requester
wait for 4. A duplicate candidate can subsequently be skipped because its
path now resolves a blocker.

`verify_wc3_pathing_yield.py` executes the complete original decision routine,
soft-float arithmetic, handle resolver and identity/countdown writer without
stubs. **8,640 single-candidate cases** cover relative velocities, same/
different/missing groups, group bit 8, three player-class values, an already
blocked candidate and five prior countdowns. **128 sequence cases** cover
both candidate orders, duplicates, nulls, an empty vector and side effects
before early return. These establish ordered decisions and exact mutated
identities/countdowns on synthetic registered objects. Full collector
integration, stale/dead-object lifecycle variants and live yielding traces
remain open.

Oracle: `verify_wc3_pathing_yield.py` → `yield-oracle.json`.

### Yield countdown consumption and path context

`6f165ae0` checks disabled flag `path+88 & 0x100000` before the countdown.
That branch returns the raw nonzero value `0x100000` and leaves countdown
`+94` unchanged. Otherwise, any nonzero countdown is decremented by one and
returns **1 immediately**, even on the transition from 1 to 0. It does not
change the destination output, re-resolve the stored blocker or refresh the
global path context during that call. A delay N therefore consumes N eligible
`Path_Advance` calls; the following call reaches ordinary path work.

`6f1684d0`, reached after the countdown gate, owns the globals used by segment
refinement and next-step obstruction:

| Global | Installed value |
| --- | --- |
| `6fd53a80` ushort | Radius class 0/1/2/3 from path `+b4` thresholds 0.5, 1, 1.5 |
| `6fd53a84` | Owner `+24c` fine-search system |
| `6fd53a88` | Fine system `+1c` map |
| `6fd53a8c` | Mover supplied to `Path_Advance` |

The yielding oracle adds **25 complete countdown-gate calls**, including
large unsigned countdowns and disabled paths, two complete countdown
sequences (4 and 20) stopped at the next context-setup entry, and **eight
complete context-setup calls** around every radius boundary. Assertions
check return values, unchanged output/identity/context during delay,
countdown freezing when disabled, and all installed globals. No routines
are stubbed. This also resolves the setup ownership left open by the segment
sampler oracle.

The completed 100-second `runtime/ground-crowd-yield.jsonl` capture preserves
the nine-Footman fixture's 300 primary ticks, 2,700 crowd samples and final
stopping outcomes. It records **66 blocker assignments**: **56 requesting 4**
from return RVA `0x168498`, and **10 requesting 20** from `0x168477`.
Every assignment starts from zero countdown and stores exactly the supplied
mover identity. There are **424 delayed calls**, equal to `56*4 + 10*20`,
and 66 transitions to zero. All delayed calls return 1. The **358 adjacent
steps within delay sequences** have owner-counter difference exactly 1.
Thus each delay advances once per owner simulation tick in this controlled
fixture; the generic contract remains per-call, not wall-clock time.

The observer emits bounded `yield-set` and `path-delay` records. The analyzer
checks identity copying, `max(previous,requested)`, enabled decrement and
disabled freeze/return semantics, and reports the observed counter-gap
histogram. `runtime/ground-crowd-yield-analysis.json` passes all capture,
crowd, arrival-chain and countdown checks. Interruption, reordering, death
and group-specific timing are still separate live cases.

`6f167290` also has a finite retry path: it checks proximity to a target occupancy
object through `6f166310`, initializes a retry counter through `6f1689d0`, or
resets the fine buffer and decrements retries. Return codes, initialization and original PRNG evidence are recovered in
the retry sections above, including live forced-arrival chains. These gates
must be distinguished from heap exhaustion and disconnected terrain.

## Accelerated waypoint acceptance oracle

Assembly at `6f165f10` compares the squared distance from the mover's accelerated
coordinates to `coarseBuffer[currentIndex]` against the square of the float at
`6fcd53f0` (`0x3efae148`, approximately 0.49). `6f070850` computes squared
distance with retail's software float operations. The threshold is about 31.36
world units for this 64-unit accelerator grid. A nonzero second argument
bypasses the distance check. Accepted progress calls `6f165d10(1,&teleported)`;
success invalidates the fine index to `-1`, while failure sets the retry
countdown to at least 20 and returns 2.

`tools/ghidra/verify_wc3_pathing_waypoint.py` executes the original `6f165f10`
and arithmetic helpers in Unicorn, stubbing only the downstream `6f165d10`
consumer as successful with no teleport. **192 cases pass**, covering eight
distances, twelve directions, and the force flag; accepted cases also reset
the fine index and rejected cases preserve it. This isolates acceptance, not
warp placement or the failure branch, and excludes exact-boundary rounding.

Oracle: `verify_wc3_pathing_waypoint.py` → `waypoint-acceptance-oracle.json`.

This accounts for the approach capture: the waypoint is at world Y=-848;
the mover at tick 16 is still 41.005 units away, then enters the acceptance
radius before tick 17. The route's approach point and this radius together
explain traversal before the mover reaches the authored rectangle edge.

## Accelerated route distance limit and full ordinary transitions

Selector `6f167ae0` starts at accelerated index `+78 - 1`, walks toward zero,
and accumulates **edge lengths along the stored route**, using the original
software-float subtraction, square, square root and addition. It stops at
accumulated length **10 accelerator units**, or at index zero. The actual
initializer `6f0040d0` converts integer 10 into `6fd54194`; the oracle executes
that initializer rather than substituting a guessed constant. For the
64-world-unit accelerator coordinates this is 640 world units, but the final
edge can overshoot the threshold. This is neither a fixed node count nor a
straight-line distance from the current route point.

Before each edge calculation, the selector tests that candidate point's X
against the gate sentinel. Encountering sentinel index `i` sets its output
flag to one and returns `i+1` in mode zero (stop before the gate), or `i-1` in
nonzero mode (past the sentinel). Index zero is not inspected or included in
length accumulation. The selector itself does not mutate the path index.

Consumer `6f165d10` initializes its teleport output to zero. With no pending
gate it stores the mode-zero selector result into the accelerated index and
returns one. With traversal disabled, it likewise performs this selection
without trying a gate. Enabled pending-gate execution includes active-state
and [placement handling](retail-pathfinding-experiments.md#special-edges-are-way-gate-records); this oracle does not execute
that gate-placement branch.

`verify_wc3_pathing_transition.py` runs **1,456 complete selector cases** and
**832 complete consumer cases**, varying indices 0–12, cardinal edge lengths
1/2/4/8, straight versus alternating zigzag routes, gate-marker position and
selector mode. Zigzags distinguish accumulated route length from displacement.
All comparisons check index/output state; no called function is stubbed.
A further **1,248 complete accelerated-waypoint checks** vary force and six
distances, including values on either side of 0.49, through the actual consumer.
These extend the older stubbed acceptance oracle for ordinary routes. A
successful ordinary index advance returns **zero** from `6f165f10`, just as
no advance does: its successful return is the teleport flag, not a general
progress Boolean. The index mutations are required to distinguish them.

Finally, **288 complete `6f167070` calls** and **288 complete `6f165c60` calls**
cover reached final fine waypoints with remaining ordinary accelerated points
and delays 0/7/25. They advance the accelerated index, invalidate fine index
`+74` to `ffffffff`, preserve fine count, retry count, flags and countdown,
and return **2**. The full caller leaves the fetched fine point in its output;
it does not restore the previous caller destination on this transition.
This verifies the ordinary handoff with an existing route. Refill/search,
oblique numerical parity, exact threshold rounding and gate-placement failure
remain outside this corpus.

Oracle: `verify_wc3_pathing_transition.py` → `transition-oracle.json`.


## Full reconstruction producers and owned consumption

Payoff140 closes ROUTE-01.1 with2664 original fine and10656 original coarse
requests, all four classes and lanes, fractional oblique endpoints, partial
routes and same-cell/promoted-region bypasses. Coarse partial point0 is the raw
nearest-node centre +0.5, without the size2 edge correction used for interior
reconstructed points. Real166c30 size inputs are class>>1, producing1/2 only;
synthetic size4 chains establish the helper rule, not a public route producer.
The script uses36 regular vectors plus9 enclosed-goal vectors (45 distinct);
the earlier handoff's52-vector description is corrected.

Move consumes its retained native-coordinate arrays directly. Removing the
whole-chain copies leaves selector/segment work proportional to the inspected
portion, while preserving exact word order, save layout and gate sentinels.
The200-row adapter regression and fresh original/model/C O0/O2 comparison are
recorded in [the engine ledger](retail-pathfinding-engine.md#oblique-reconstruction-and-direct-owned-consumption-payoff140).
Growth, invalid starts and next public advance state remain ROUTE-01.2.
