# Retail pathfinding evidence: Movement, formations and order lifecycle

[Contract, current ledger and reproduction](retail-pathfinding.md).
Retail **1.27.1.7085** only; addresses and report paths use the conventions there.

## Speed and heading update

`6f170880` updates a scalar speed and heading from a signed heading error.
The scalar's meaning is established by caller `6f16a790`: it computes the
magnitude of mover velocity `+80/+84` into group-member `+20`, supplies that
field to `6f16fbd0`/`6f170880`, then clamps it against mover maximum `+88`.
The companion member `+24` holds heading. This resolves the earlier ambiguity
about whether the first scalar was speed or a wait/time quantity.

For mover speed increment `a = +b4`, turn cap `t = +b8`, movement-angle
threshold `q = +bc`, heading error `d` and stop-translation argument `stop`,
the original helper implements:

```text
speed = 0                       if stop != 0 or abs(d) >= q
speed = speed + a                otherwise
heading = heading + clamp(d, -t, t)   for nonnegative t
```

Turning still occurs when translation is stopped. Equality at the movement
angle threshold stops translation; equality at the turn cap permits the full
cap. This helper neither clamps speed against mover `+88` nor wraps the
resulting heading. Those policies belong to other stages. In `6f16fbd0`, an
ordinary `Path_Advance` result zero permits translation; other route results
pass a stop-translation argument. The in-range-but-not-facing arrival branch
also invokes the helper with translation stopped.

`verify_wc3_pathing_motion.py` executes **13,824 complete original helper
calls** varying initial speed, speed increment, turn cap, threshold, signed
error, stop flag and initial heading. Binary-fraction inputs allow exact
output assertions without approximating Blizzard's numerical routines. It
checks both outputs and preservation of input/field values; maximum speed is
set below several outputs to confirm the helper does not clamp it. No called
routine is stubbed. Negative turn caps and nonfinite inputs are outside the
physically meaningful tested domain.

The same script executes **32 complete `6f05c5c0` producer calls**, including
the real mover-handle resolver and setters `6f15ff40`/`6f170f60`. For tested
nonnegative normal values, it caps supplied speed against
`[6fd3c82c]+80`, divides by **32**, and writes the converted value to both
maximum speed `+88` and per-update increment `+b4`. The input remains intact.
This producer can therefore configure a full-speed increment, rather than
implying a gradual acceleration ramp. Other group/request producers also
write `+b4` and remain to be traced before generalizing that behavior.

Turn-cap and movement-angle setters are `6f170f80` and `6f171200`, called
through `6f05c8c0` and `6f05c890`. Static inspection shows both use `6f062930`
(angle normalization), with the turn-cap wrapper additionally enforcing a
minimum near `0.001`. Their principal-range and wrapped conversion are covered below;
authored turn-data sources and full update cadence remain open. Speed-producer
clamping is covered above; velocity commit and position integration follow.

Oracle: `verify_wc3_pathing_motion.py` → `motion-oracle.json`.

### Heading error and angle normalization

`6f16f630(output, currentHeading, direction)` computes a software-float vector
length, then calls `6f1d4c80` to derive a direction heading. The latter uses
**acos(X / length)** and selects `2π - angle` when Y is negative; it is not an
`atan2` implementation. Its very-short-vector and very-small-angle guards
return zero. `6f173720` then computes the signed shortest heading change,
using at most one ±2π correction, and `6f16f630` zeros errors strictly smaller
than constant `6fcd5470` (**2.0000000233721948e-7 radians**).

The motion oracle runs **441 complete heading-error calls** across a 7×7
signed XY grid (including the zero vector) and nine current headings. Against
an independent mathematical `atan2`/shortest-angle reference, maximum error is
**0.00024685265 radians**, below the asserted 0.0003 corpus bound. Exact
opposite-direction cases compare magnitude because the represented π selects
the sign at that boundary. This is a numerical comparison, not an independent
bit-exact reimplementation of the software square root/division/acos chain.
Six additional cases place current heading immediately below, exactly at and
immediately above the deadzone float, in both signs, with an eastward vector.
They confirm that equality is preserved and only strictly smaller error zeros.

Normalizer `6f062930` uses the stored reciprocal of 2π, truncates the product
with `6f0715c0`, multiplies by stored 2π and applies the negative-input
correction. Nine original-call samples show why a generic `% (2*pi)` is not
a bit-parity substitute: stored positive 2π (**6.2831854820251465**) remains
2π, positive twice that becomes 2π, while the corresponding negative inputs
become zero. Input zero yields negative zero. Circular errors against the
mathematical modulo reference remain below 1e-5 in this corpus.

**40 complete turn-parameter producer calls** exercise actual handle resolution,
normalization and setters. The turn-cap wrapper writes
`max(normalizedAngle, 0.0010000000474974513)` to mover `+b8`; the movement-angle
wrapper writes normalized angle to `+bc` without that minimum. Twenty-two
principal-range calls use independent exact expectations; the remaining
18 wrapped producer calls compare against the separately executed original
normalizer and do not claim independent normalization bit parity. Parameter
provenance and valid authored ranges still need recovery before interpreting
negative or multi-revolution inputs as normal gameplay configuration.

## Stored-velocity integration and movement clocks

`6f1603d0` commits mover position by computing elapsed time with `6f161040`,
then applying, componentwise:

```text
position = storedPosition + storedVelocity * elapsed + suppliedDisplacement
```

It writes mover `+78/+7c`, keeps velocity `+80/+84`, resets the time origin
through `6f161090`, and executes both proximity (`6f1604d0`) and fine occupancy
(`6f160590`) rectangle updates. All stages, including lazy spatial record
insertion/removal, now execute together in the motion oracle.

Clock selection follows mover identity `+14` bit `80000000`: clear selects
owner `+14`, set selects owner `+68`. This is a selection rule; the gameplay
meaning of the two clock domains is not yet established. Within the selected
clock, float time is `+40`, epoch counter `+44`, and epoch span `+48`. Mover
`+70/+74` stores its previous float time/epoch. `6f15cea0` subtracts the float
times, zeros a difference strictly smaller in magnitude than
**9.999999747378752e-5**, then adds epoch difference times epoch span. It does
not clamp negative elapsed time. `6f161090` copies both current clock fields
into the mover's origin.

The oracle executes **2,304 complete position integrations in 1,152 two-call
sequences**. It varies clock domain, current/old time, epoch, signed velocity,
explicit displacement and all four collision-size classes. Binary-fraction
inputs permit exact expected positions. Two real 16×16 spatial maps receive
proximity and fine occupancy updates. The first call inserts fresh records;
the second uses the retained records, reverses explicit displacement and
advances time by 0.25. Assertions cover position, velocity preservation,
origin reset, both rectangles, exact insertion/removal record counts, and
effective occupancy of every cell after the second call. No called routine
is stubbed. The synthetic epoch span is eight; this is not a claim about the
retail clock's configured span or update frequency.

A separate **108 elapsed/read–origin-reset–read sequences** cover positive,
zero and negative epoch/time differences in both domains. The read leaves the
origin unchanged; reset makes the subsequent elapsed value zero. **12 exact
float-neighbor boundary cases** verify the strict time deadzone in both signs
and domains. These strengthen the time-state contract independently of spatial
updates.

Position commit and cached-route group ticks are verified separately. Remaining: actual clock cadence,
notification/region callbacks, general software-float parity, mixed-object
occupancy, clipping and multi-unit physical trajectories remain to be composed.

## Committing speed/heading to velocity

`6f16fe20` records requested speed at mover `+c0` and signed heading change at
`+c4`. For positive speed it computes desired XY velocity with the original
sine/cosine helpers, subtracts current velocity and passes that delta to
`6f15f7e0` with notification enabled. For nonpositive speed it similarly
requests zero velocity and then writes normalized facing directly.

The ordering is important: `6f15f7e0` first calls `6f1603d0` with zero extra
displacement, committing **old velocity** through the current time, including
spatial updates and time-origin reset. It then calls `6f1606e0` to add the
velocity delta. That routine zeros negligible motion, clamps a nonzero vector
to maximum speed `+88` through `6f15fc70` when necessary, and updates the fine
occupancy object's `+40` bit **20000000**. The actual retail vtable
`6fa9129c +50` dispatches `6f1711b0`: update facing from velocity (`6f160060`)
and ensure the mover is on its update list. This is distinct from the position
notification slot `+54` and its region-transition callback.

The motion oracle now executes **80 complete speed/heading commits** through
that real vtable callback, varying four requested speeds, five headings and
four maxima (including zero and clamping cases). Movers begin already linked
to an update list, so list insertion itself is not exercised. Both actual
spatial maps are initialized, and no called routine is stubbed. Every case
first advances position exactly from `(8,8)` to `(8.0625,7.9375)` using prior
velocity `(0.125,-0.125)` over elapsed 0.5, independent of the newly requested
heading/speed. The new velocity matches a mathematical clamped sine/cosine
reference with maximum component error **6.599843e-6**, below the asserted
1e-5 corpus bound; this is a numerical comparison, not bit parity.

A further **80 position commits** advance time another 0.25 and verify that
only the new velocity contributes to subsequent displacement (component error
below 2e-6). Requested-speed field `+c0`, time origins and motion flag are also
checked. These calls connect speed/heading selection to actual position and
occupancy changes, while the group scheduler, velocity-dependent notifications
to gameplay state, unlinked-list insertion, very small velocity thresholds,
mixed-object collisions and full live trajectories remain open.

This recovers a concrete meaning/producer for the previously anonymous
`20000000` occupancy exemption: it is maintained by velocity updates. Static
inspection of group routine `6f16c250` also finds a separate producer of
`40000000`, temporarily set on eligible members around the group's per-member
routing loop and cleared afterward. The composed decision cases below now
verify this exemption and preservation of the velocity flag; live trajectory
validation remains open.

## Group decision and speed commit

Static inspection of `6f16c150` establishes the group tick ordering: sample
its destination and request a route, run **all member decisions** through
`6f16c250`, then commit **all member speeds/headings** through `6f16c570`.
On a failed group route request, `6f16c5d0` instead zeros each member speed,
uses current mover facing, commits the stop and unlinks path scheduling.
The cached-route full-tick cases below now verify this ordering. Fresh-search
ticks and live formation setup remain open. The stop loop itself is composed below.

The motion oracle executes **480 complete two-member `6f16c570` calls** with
actual `6f16fe20` commits, retail vtable callbacks and spatial updates, without
stubs. Four requested speeds, two ordered pairs of maximum speeds, group bit
`8`, member flags `0/10000/200000`, and mover flag `01000000` are varied, with five auxiliary states: absent,
published cap 0.25/1/4, or the sentinel.
`6f169b50` enables shared speed only when group `+80` bit `8` is clear, every
member `+28 & 210000` is zero and every mover `+d8 & 01000000` is zero.
The gameplay meaning of that mover flag is unresolved.

When eligible and group `+7c` is null, `6f16b060` selects the minimum mover
`+88` maximum. `6f16b5c0` submits `min(member+20, sharedCap)` and member `+24`
heading. Otherwise `6f16c570` uses the large sentinel at `6fcd5480` as cap.
Every mover still applies its own velocity maximum during the commit. The
corpus checks submitted speed, final velocity, unchanged member request,
position and the auxiliary minimum accumulator. Group bit `8` thus participates in both shared-speed eligibility and
the independently recovered yielding rule; its higher-level meaning should
not be inferred from either consumer alone.

A further **216 complete `6f16b060` calls** cover zero/one/two members, all
combinations of excluded member bit `200000`, both maximum-speed orderings,
and three prior/published auxiliary values. With a non-null group `+7c`,
that auxiliary object's `+24` accumulates the minimum of its prior value and
the current eligible member maximum. The returned cap is auxiliary `+20`
unless it equals the sentinel, in which case it is the current member minimum.
No eligible members produces the sentinel. Published `+20` is not changed by
this selector and can exceed the current member minimum: it is an override,
not another minimum operation. The full commit corpus also verifies these overrides and that disabled
sharing leaves the auxiliary accumulator untouched. The ownership and
publication routines below explain the prior-pass value; complete owner-tick
and live Captain AI composition remain open.

`6f16b5c0` also has a separate group-bit `800` branch for a visible moving
target whose maximum is below the submitted speed, with additional distance
and elapsed-time calculations. That branch is excluded from this corpus.
Temporary occupancy bit `40000000` in `6f16c250` is covered by the separate
composed decision cases below. These results establish group-level speed coordination,
not full formation or crowd trajectory parity.

## Shared group parameters: ownership and publication

The group `+7c` object is shared through move requests. Static call-chain
inspection connects `CaptainAI_PrepareMoveRequestMember` (`6f9d1040`) through
`6f89c810` and `6f16d8b0` to move-request `+f8`. `6f16bdb0` transfers that
reference to group `+7c` via `6f16d890`; `6f169550` can copy the group's
reference back into a request. The AI producer batches members in twelves and
conditionally attaches this shared object. This establishes an AI producer,
not that every user formation uses it or that it has no other producer.

Both setters decrement the old object's `+1c` reference count, increment the
new object's count, and store the pointer. **48 complete original setter calls**
verify null/non-null assignment, replacement and assignment to the same object
at both owner offsets. Neither setter destroys an object whose count reaches
zero. Group/request cleanup calls these setters with null.

For an object with nonzero `+1c`, `6f16c220` performs this publication step:

| Field | Operation | Meaning recovered from consumers |
|---|---|---|
| `+20` | Copy prior `+24` | Published speed cap |
| `+24` | Reset to `6fcd5480` sentinel | Minimum accumulated by group speed selection |
| `+28` | Reset to zero | Maximum radius rebuilt before movement ticks |

For zero references it dispatches vtable `+10` with argument zero instead;
both registered and unregistered release/pool-reuse branches are executed below. Static
inspection of owner update `6f15aa80` places publication over owner list
`+38c` after scheduler maintenance, then radius collection over groups in
`+3b8`, then `PathGroup_TickMovement` over those groups, then mover updates
and staggered separation. The publication helper also has a serialization-path
caller `6f15b080`; it should not be described as exclusively runtime-called.

**32 three-pass sequences** compose original publication and speed selection
for two groups sharing one object, varying maxima and group traversal order.
The first pass starts with the sentinel and each selector returns its own
minimum while accumulating a shared minimum. Subsequent passes return the
previous accumulated minimum even when the current maxima have increased.
Each pass resets the accumulator and rebuilds it independent of traversal
order. This explains the earlier apparent override: it is a prior-pass
aggregate, rather than an arbitrary cap inferred from a single selector call.
The sequence follows the statically recovered owner ordering; it does not
execute the complete owner tick or prove a wall-clock tick frequency.

`6f16e1f0` traverses group members in reverse, resolves each member identity
with the original handle registry, resolves the mover's group identity at
`+9c/+a0`, and contributes mover radius `+90` only when that group is the
current group. It accumulates the maximum at shared-object `+28`.
**128 two-group sequences** compose publication, both radius-collection calls,
and both radius queries, using real handle resolution. They cover four radii
per group, both traversal orders, valid identities, stale member generation,
stale group generation, and a mover belonging to the other group.

`6f16c940` returns shared `+28` when bound; otherwise it computes the maximum
radius of all local members, starting at zero. Another **160 complete calls**
cover zero through nine local members (including the four-at-a-time loop),
four radii and absent/zero/small/large shared values. This local query uses
member pointers directly, unlike the shared-radius collector's identity
validation. The only direct call xref to this query is `PathGroup_RequestRoute`
(`6f16ce10`, call at `6f16ce84`), which passes the resulting radius into
`Path_SetFootprint` before the accelerated route request. Thus the shared
maximum reaches group route sizing, as well as the shared minimum reaching
movement speed. This consumer chain is static evidence; the full request with
shared parameters, fresh pool-block allocation, new-handle registration and
full AI/live lifecycle remain to be composed.

## Cluster-group pool lifetime and failed-route stop

RTTI/vtable evidence identifies the shared-parameter object as
**`NIpse::CPrClusterGroup`**, with object vtable `6fa90c98`; the owner list
sentinel at `owner+384` instead has linked-list vtable `6fa9191c`. Constructor
`6f14fd70` initializes links and references to zero, identity `+14/+18` to
`-1/-1`, speed fields `+20/+24` to the sentinel and radius `+28` to zero.
The owner constructor `6f157610` also initializes its pool at `owner+658`.

Zero-reference publication dispatches `6f169a50` through the real vtable.
That routine unlinks `+4/+8`, resets speed/radius fields, and calls `6f1c5490`.
The latter removes a registered identity when `+14 != -1`, clears both
identity words, and dispatches vtable `+4` to `6f154460`. That routine returns
the object to the pool selected by `6f169af0` (`[6fd53a48]+658`): store the old
free-list head in the four-byte header preceding the object, decrement live
count `pool+18`, then set free head `pool+14` to that header.

**16 complete constructor → zero-reference publication → release → reuse
sequences** execute these actual virtual calls without stubs. They vary both
list neighbors, the existing free-list head and live count. Assertions cover
neighbor repair, empty object links, reset scalar fields and identities,
free-list topology, and unchanged allocation counter `pool+1c`. Reuse through
`6f155490` pops the header, increments live count and returns the same object;
it relies on release having reset the fields. These objects start with
unregistered `-1/-1` identities, so registry removal `6f1cbfb0` is not exercised.
Fresh pool-block allocation through `6f06a320` is also outside this corpus.

A further **48 complete registered-release sequences** initialize a live
identity and execute the same zero-reference publication, actual vtable cleanup
and pool-reuse chain. They cover both index-sign domains, three slot indices,
two generations, two existing free-list heads and two live counts. The original
`6f054530` resolver must return the object before release, zero after release,
and still zero after the object's storage has been reused.

`6f1cbfb0` selects the registry domain by identity bit `80000000`; `6f1cbff0`
writes the prior domain free index into slot word zero, clears slot word one
(the object pointer), makes the released index the new free head and decrements
the domain live count. Positive-domain slots are at registry `+c`, count bound
`+1c`, free head `+40`, live count `+48`; negative-domain equivalents are
`+2c/+3c/+44/+4c`. Its helper uses the masked low 31-bit index. The resolver's
32-bit scaled address arithmetic also discards the high bit. Slot marker
`-2` identifies a live slot; generation equality against object `+18` is a
second validation gate. These tests verify the selected domain changes and
the other domain remains intact, then both object identity words become `-1`.
They do not execute allocation/registration of a new identity, generation
advancement or rollover, and do not justify claims about generation exhaustion.

The same motion oracle now executes **480 complete two-member
`PathGroup_StopMembers` (`6f16c5d0`) calls**, each following a tested movement
commit. Before stopping, both paths are placed in a valid pending queue; the
cases rotate through all four scheduler bucket policies. Time advances by
0.25, so the original speed commit must first integrate the **previous**
velocity into position and update spatial occupancy/time origin, then zero
velocity. The loop sets member requested speed to zero, copies current facing
to member heading, calls the full mover commit and calls `6f168800` using
mover `+a8` path. The latter selects the correct scheduler bucket, unlinks
that path and clears path flag `02000000`.

Checks cover both final positions, time origins, zero velocities, cleared
occupancy motion flags, member facing preservation, requested speed, path
flags/links and empty bucket count/head/tail. This verifies the stop action
used by the statically observed group-route-failure branch. It does not yet
execute the complete group tick that chooses that branch, prove gameplay
order completion, or cover stopping a mover absent from the update list.

## Formation interval classification before member stepping

`6f16c250` clears each member's `200000` flag before its multi-member setup.
When group cooldown `+68` is zero and group flag `2` is clear, `6f169b00`
rejects setup if any member has `+28 & e0000`, any mover has `+d8 & 01000000`,
or any member path has `+88 & 30000000`. Rejection sets cooldown to `0x42`;
acceptance calls `6f16b2f0`. These caller conditions are static evidence.

The latter projects predicted mover positions (`position + velocity * elapsed`)
along the group heading `+70`. It forms lower/upper intervals expanded by
`mover.radius + 1`. The padding comes from initializer `6f004140`, which parses
the binary-owned decimal at `6fa91bfc` into `6fd541d4`. The oracle initializes
that scalar from the same text because the initializer calls an unloaded CRT
`isdigit` import; no formation routine is stubbed.

`6f16dfd0` sorts the interval/index pairs by descending lower edge using
selection sort: move the strictly smallest lower edge to the tail, then
repeat. Equal-key swaps are not forced; the overall sort is not generally
stable. The caller scans from the tail (smallest lower edge) toward the head.
It skips member bit `10000` and uses `(mover+ d8 >> 12) & 15` as a rank:

- A rank greater than the minimum already seen is marked `200000`.
- Otherwise update the minimum and inspect later projected intervals while
  their lower edge is strictly below the current upper edge. A non-skipped
  peer with a smaller rank also marks the current member `200000`.

The movement oracle executes **1,728 complete three-member classifications**
against a separate interval/rank model: four positional arrangements (including
coincident positions and reversed order), all 27 rank triples from 0/1/2,
two radii, two elapsed times, two first-member velocities and the middle-member
skip flag. These cases use heading zero and avoid ideal contact boundaries.
Their coverage establishes this rank/interval policy, not gameplay names for
the rank nibble or a full formation algorithm.

A separate retained contact regression demonstrates why ideal arithmetic is
insufficient for exact parity. At X `0/2/4`, heading zero, radius one and zero
velocity, the original pre-sort intervals are:

| Member | Lower | Upper |
|---|---:|---:|
| 0 | -2 | 2 |
| 1 | -0.00000011920928955078125 | 3.999999761581421 |
| 2 | 1.999999761581421 | 5.999999523162842 |

A read-only function-entry observer captures these original values. With ranks
`1/0/0` and member 1 skipped, member 0 is marked because member 2's lower edge
is strictly below 2. Ideal projection would put that edge exactly at 2 and
not mark it. This regression preserves the observed decision; the general
software-trigonometry/float bit-parity model remains open.

After classification, `6f16c630` sets member `100000` when group
`20000` is clear or unsigned group age `+5c` exceeds `0x41`. Members with
neither `100000` nor `200000` then receive temporary occupancy bit `40000000`
around the group's member-step pass. Thus rank classification contributes to
collision-exemption eligibility.

The motion oracle now executes **576 complete `6f16c250` calls**, with original
classification, member steps and cleanup. The corpus varies one/two members,
group flags `0/20000/20002/20200`, age `0/65/66`, cooldown `0/1`, ranks
`0,0`/`1,0`, prior member flags `0/100000/200000`, and occupancy movement bit
`20000000` clear/set. Forced-arrival inputs keep both the ordinary and held
member paths bounded without invoking a search. A read-only observer at every
`6f16a790` entry verifies that all eligible members already carry `40000000`
throughout the pass. Final assertions check exemption removal, preservation of
`20000000`, resulting member flags, zero requested speed and consumed forced
arrival. Singleton and group-bit `200` paths bypass exemption setup; group bit
`2` skips rank classification. An unused second member remains untouched in
the singleton cases.

Another **12 complete decision passes** remove forced arrival from the
rank-one member, leaving a rank-zero peer at the same position. The classifier
marks the first member `200000`; its full held-step path requests zero speed
and turns toward the destination by at most `0.125`, without marking arrival.
The peer carries the exemption through both steps and clears it afterward.
Cases vary prior velocity `0/0.25`, facing `0/0.5/1`, and occupancy movement
bit clear/set. Actual position and velocity remain unchanged during this
decision pass; the separate commit pass owns their update. No original routine
is stubbed. Ordinary non-arrival movement and the positive-request-speed
cleanup branch are covered by the following composition. Blocked-classification
cooldown reset, oblique group headings, all ranks and live formation
trajectories remain open.

## Cached routes through decisions, velocity commit and position integration

**144 additional two-member sequences** compose `6f16c250` with ordinary
`6f16fbd0` (`Mover_StepRoute`), full `Path_Advance`, cached fine/coarse waypoint
handling, original footprint blocker collection, `6f16c570` velocity commits
and subsequent `6f1603d0` position integration. Inputs vary prior speed
`0/0.25/0.5`, all four footprint classes, maximum-speed pairs `0.5,1`/`2,1`,
group flags `0/20000/20002`, and ranks `0,0`/`1,0`. The clear 16-by-16 maps
start without linked object occupancy. Members begin at `(8,6)`/`(8,10)`
and each has a cached endpoint four cells east. Their update-list links are
already present; no original routine is stubbed.

These sequences establish the composed order and observable transitions:

1. Classification can hold rank-one member 0 while member 1 follows its route.
   Eligible temporary `40000000` flags are present at both step entries.
2. Ordinary members request `min(oldSpeed + 0.25, ownMaximum)`; the held member
   requests zero. The pass removes exemptions and sets member `100000` on
   eligible members with positive requested speed. Actual velocity is still
   the old value, and both cached route indices remain zero.
3. The commit applies the shared minimum maximum-speed cap when eligible.
   Member `100000` does **not** disable that cap: the eligibility mask is
   `210000`, i.e. `200000 | 10000`. A held member therefore disables sharing,
   while the positive-request marker does not. Assertions distinguish member
   requested speeds from the resulting velocities.
4. At zero elapsed time, commits preserve position and update occupancy
   movement bit `20000000`. Advancing the owner clock by `0.25` and calling
   original position integration moves each member by its committed velocity
   times `0.25`, with actual spatial-bound updates.

The harness maps a zero-based SEH slot because the original blocker collector
reads/writes `FS:[0]`; every sequence verifies restoration. This is process
environment setup, not a substitute for blocker code. These initial sequences
call the decision and commit routines separately. The following corpus executes
the complete tick around them. Fine/coarse search composition is tested
separately by the refill oracle; live group trajectories and initially linked
dynamic blockers remain open.

## Full cached-route group tick and membership prepass

**144 complete `6f16c150` calls** repeat the preceding movement matrix through
the actual group tick. The fixture registers both movers and their group in
the real handle table, supplies valid member and owning-group identities,
and uses a fixed point destination with a cached group route at final index
zero. Original `6f16bc10` invokes the actual mover vtable `+54` callback
(`6f16fa00`) in reverse member order, then `6f16d1c0` resolves and retains both
members. A read-only callback observer verifies that order. Remembered fine
cells match the current positions, so this covers the region callback's
same-cell branch, not region entry/exit notifications.

The tick then samples the fixed destination, accepts the cached group route,
executes all member decisions and commits, updates target-refresh state, and
scans completion readiness. No member has arrived. Assertions check a single
commit-pass entry, old velocities and removed exemptions at that entry,
requested/final speeds, both retained members, unchanged route indices,
group age `+5c` and completion counter `+60` advancing from zero to one, and
the subsequent position integration. No routine is replaced or bypassed by a
hook. Fresh group/member searches, moving targets, actual completion
callbacks, nonzero-elapsed region transitions and full owner-tick scheduling
remain outside these full cached-route ticks.

The same oracle executes **156 complete `6f16bc10` membership prepasses** for
zero through three rows, exhaustively combining five states per row: valid,
stale member generation, deleted member slot, mover owned by another group,
and stale owning-group generation. It executes original handle resolution,
valid-member region callbacks and pruning. `6f16d1c0` overwrites member `+14`
with the resolved mover and retains the row only when the mover's `+9c/+a0`
group identity resolves to this group. It walks backward and removes an
invalid row by copying the last row into its slot, then shrinking the member
array via `6f16d550`; surviving order is not stable.

Every complete 44-byte surviving row, final count/return value, and reverse
callback order is checked against an independent swap-removal model. Callback
dispatch happens before pruning and only for members whose mover and group
identities both resolve correctly. These cases cover empty/all-invalid
prepasses without invoking the enclosing tick's group-destruction branch.

## Callback-timed membership mutation

**GROUP-04.1, S/O:** the original motion oracle now pauses at an actual mover
slot54 callback entry (`6f16fa00`), executes the original `6f16dd70` member
unbind or `6f170fa0` mover-group detach on a separate stack, restores the CPU
context, and resumes the untouched callback and prepass. Only the requested
producer's memory effects survive the context restoration. No original code,
virtual callback or resolver is replaced. All nonvolatile registers, stack
cleanup, instruction budgets and the exception chain are checked.

The **68 cases** cover one through three members, every callback position,
every removal subset, and both producers, including zero-mutation controls.
Already-dispatched higher rows remain in the observed callback order; lower
rows resolve the updated identity/ownership and skip removed members. Pruning
re-resolves every row, swaps the last row into a hole, and preserves every word
of each surviving 44-byte record. A second original prepass verifies the new
reverse iteration order and unchanged survivor words. Removing row0 of three
members yields `[2,1]`, whose next callback order is `[1,2]`.

Frozen expectations are
[`retail-callback-mutations-1.27.json`](../../../tools/ghidra/fixtures/retail-callback-mutations-1.27.json),
normalized digest `7f56b7d4603f449c8139791d508944cf5ae91d86c116abc6a3b3079016ac34ac`.
The asset-free regression checks the exhaustive case inventory, complete row
contents, and both immediate and later callback sequences. Two fresh strict
corpus executions reproduce that digest under the documented report root:
`group-04.1-corpus-first/motion-oracle.json` and
`group-04.1-corpus-repeat/motion-oracle.json`.
The final `group-04.1-corpus-provenance/corpus-results.json` also fingerprints
both Ghidra annotation scripts. `make test` passes72 pathfinding tool tests and
36,633 assertions in2,123 engine tests for each ROC/TFT fixture mode.

```sh
"$pathing_python" tools/ghidra/run_wc3_pathfinding_corpus.py \
  --binary "$wc3_dir/game.dll" --archive "$pathing_reports" \
  --only oracle-motion --output "$pathing_reports/group-04.1-reproduction"
```

This is a controlled external request at a real callback boundary, with the
same-cell callback branch. It does not prove a complete JASS/gameplay region
callback graph, handle destruction/reuse, changed-cell notifications, or a
refreshed survivor trajectory. Those contracts remain BASE-03.1/MOVE-03,
GROUP-04.3 and GROUP-04.4. OpenRealm's current Move owns individual caps and
routes, without a persistent retail group/member array. This contract therefore
records a required group implementation behavior; no individual speed kernel
change follows from it. GROUP-04.4 owns the group integration and its scheduler
and lifecycle regressions.

## Callback-timed handle reclamation and reuse

**GROUP-04.3, S/O:** `verify_wc3_pathing_order_tasks.py --shared-pair
--callback-reuse` starts with originally admitted units, the original shared
request and a fresh owner update. It pauses an actual slot54 callback, executes
the complete original mover destructor `6f16eb20`, then the original pooled
factory `6f14ee90` and activation `6f16ea70`, restores CPU context and resumes
the unmodified callback/prepass. Each pair has four trigger/victim combinations,
each repeated twice. Both the open pair and the three-cell stock-mask wall pair
have frozen exact outcomes.

The destructor releases the mover's owned path, retires both spatial objects,
detaches relationships/payload, removes canonical identity and returns mover
storage to its pool. The next factory reuses the **same address and slot with
a new generation**. For the first mover, the identity changes from `[8,108]`
to `[8,153]`. The original resolver rejects all four old identities and accepts
the new mover. Pruning rejects the stale member despite pointer reuse, preserves
the survivor's entire44-byte row, and the subsequent prepass dispatches only
that survivor. A victim whose callback already ran keeps its old identity in
the callback record; a victim reused at its own paused callback resumes with
the new identity. A lower victim is skipped by later reverse iteration.

The positive-domain registry's live count changes **50→46→49**. Four objects
are removed and three are activated; the old owned path is not recreated.
Mover pool live/allocation counters change **2/2→1/2→2/3**. Each retired spatial
object still has two lazy references; both original objects remain in storage
pending map cleanup. Two supplied, originally constructed spare regions support
new activation, increasing spatial pool live/allocation counters **4/4→6/6**.
No reference count is cleared to force early reuse. Further cleanup/growth is
still MAP-05/BASE-03 work.

This composition exposed a real fixture initialization gap. Original
`6f050a70` calls `6f04a6b0`, whose result is published at both
`6fd6860c` (**owner registry**) and `6fd68610` (**current registry**). The previous
baseline supplied only the latter. Spatial retirement `6f14dae0` consumes the
former; canonical resolution and general unregister consume the latter.
`wc3_pathing_baseline.construct` now supplies both aliases, as the original
creation chain requires. A frozen explicit counterfactual clears only the owner
alias before destruction: old handle resolution still returns zero after object
generations are cleared, but both old spatial slots remain live and registry
accounting changes **50→48→51**. Thus handle rejection alone could hide the gap.
This control certifies no native fidelity and stays separate in the corpus.

Frozen fixtures and normalized case digests:

| Fixture | Digest |
| --- | --- |
| `retail-callback-reuse-1.27.json` | `7ba73aa142594f52f1ac9eca5ed85f1a7c21fe0527065aacc9c8e2944da3b6db` |
| `retail-callback-reuse-wall-1.27.json` | `4086ce129f2a26df8484c75e3e99d0f554cf9876a65924eb683e3fd10d8ca982` |
| `retail-callback-reuse-missing-alias-1.27.json` (counterfactual) | `08d9573880edba72e68aa1abadebb0804b31036fc7a83590cda088d918b1e65f` |

```sh
"$pathing_python" tools/ghidra/run_wc3_pathfinding_corpus.py \
  --binary "$wc3_dir/game.dll" --archive "$pathing_reports" \
  --only callback-reuse-open --only callback-reuse-wall \
  --only callback-reuse-missing-alias \
  --output "$pathing_reports/group-04.3-reproduction"
```

Original expectation exports are
`group-04.3-callback-reuse-frozen-export.json`,
`group-04.3-callback-reuse-wall-export.json`, and
`group-04.3-missing-registry-alias-control.json` under the report root.
The final strict114-entry replay is
`group-04.3-final-frozen-corpus/corpus-results.json`; it also rechecks the
previous owner/pair frozen states after the alias fix. Asset-free regressions
check the generations, exact survivor rows, pool/reference accounting, and the
counterexample's stale slots. The shared callback-call helper is tested for
register, stack, budget and exception-chain failures, including CPU restoration
with retained memory effects.
The final114-entry replay passes with no changed source fingerprints; `make
test` passes75 pathfinding tool tests and36,633 assertions in2,123 engine tests
for each ROC/TFT fixture mode. Earlier mutable-source/failed-alias attempts are
diagnostics, not this checkpoint's reproduction evidence.

Ghidra now persists11 partial layouts,80 verified fields,14 explicit prototypes,
the two registry labels and their typed pointers. Applied/read back metadata is
`group-04.3-ghidra-registry-types.json`; `game.dll` is saved. Complete gameplay
RemoveUnit/region callback graphs, replacement owned-path creation, natural
survivor completion and OpenRealm group integration remain excluded here and
owned by BASE-03.1/MOVE-03/GROUP-04.4. This original mover lifecycle is not a
claim that a CUnit's active order was cancelled by a gameplay RemoveUnit native.

## Member completion: retry boundary, notification and deferred removal

The motion oracle executes **1,536 complete `6f16c390` completion scans**, each
followed by the original membership prepass. Inputs vary one/two members,
completion counter `0/18/19/20`, member-to-destination distance `0/8/16/32`,
arrived member flags `10000/30000`, all combinations of path result bits
`10000000/20000000`, and completion gating (group bit `1` clear, or set with
unseen counter `32/33`). Only member 0 is arrived; an optional second member
is retained and remains moving. The matrix also varies null/attached gameplay
payload, using the actual retail bridge and CUnit vtables described below.
Paths begin unqueued with nonempty fine and
coarse buffers. No original routine is stubbed.

When the persistent-follow gate permits processing, group `+60` increments
**before** testing the retry boundary. An arrived member with `20000` gets a
route reset instead of completion only when the group has multiple members,
the incremented counter is below `20`, and squared distance from predicted
position to member destination `+18/+1c` is strictly above **256**. Thus prior
counter `18` can retry, but `19` cannot; exactly 16 cells does not retry.
The threshold comes from `6f004150` parsing literal `6fa91e84` into
`6fd541ac`; the oracle seeds that binary-owned literal. Retry resets path
buffers/results/retry state while retaining membership and velocity. A scan
that completes any member resets group `+60` to zero. The closed follow gate
preserves the counter and all tested state.

On completion, `6f16d4e0` (`PathGroup_FinishMember`) clears the member handle
pair and cached mover pointer through `6f16dd70(0)`, then calls
`6f171340(1,1,blocked,partial,0,0,0)` on the saved mover. Here `blocked` is
member `20000`; `partial` is true only for path `10000000` set and
`20000000` clear. The stop routine clears forced arrival, commits zero
velocity, clears mover owning-group identity `+9c/+a0`, and selects a
notification before invalidating the path:

| Condition | Original notification builder | Message tag |
|---|---|---|
| Member `20000` clear | `6f170d50` | `63702661` |
| Member `20000` set, partial false | `6f170dc0(0)` | `63702670` |
| Member `20000` set, partial true | `6f170dc0(1)` | `6370266f` |

Read-only observers verify both builder selection and all eleven message
words: `[5e70726f, 60706375, tag, mover, 0,0,0,0,0, ffffffff, ffffffff]`.
At that point group ownership is already cleared, while the old path
destination and enabled state remain available. The null-payload half executes
the builders without dispatch; the attached half follows virtual `+20` through
the bridge below. Order-subscriber side effects and reentrancy remain open;
the tag names alone do not establish those gameplay semantics.

After notification construction, current and adjusted path destinations
`+1c/+20` and `+24/+28` become **-128000**, both route buffers empty, indices
become `-1`, retries/delay clear and disabled bit `100000` is set. Original
requested destination `+2c/+30` remains unchanged. The oracle executes the
actual integer initializer `6f004eb0` to obtain `6fd541dc`; this sentinel is
exactly -128000, distinct from the Way Gate point sentinel.

The completion scan leaves the member-array count unchanged. The following
full `6f16bc10` prepass removes the cleared member identity and, for a
two-member group, swaps the still-moving member into row 0. Every sequence
checks the final count, surviving identity and cached pointer. This proves
deferred row removal, not destruction of an empty group by the enclosing
tick or execution of a movement-order subscriber.

## Completion bridge into CUnit events

The attached-payload half of the completion corpus supplies the original
`CAgentBaseAbs` vtable `6fa8099c` and CUnit vtable `6fb77eb0`. No virtual slot
is patched. The complete chain is:

`mover+30` → payload virtual `+20` = `6f057590` → payload `+54` CUnit virtual
`+18` = `6f056090` → `6f055800`/`6f055820` → CUnit virtual `+14` = `6f071dc0`.

`6f057590` filters message tags and forwards accepted packets unchanged.
`6f056090` requires global bridge `6fd3c82c` and its `+8` field nonzero,
resolves the CUnit's `+c/+10` identity through the real handle table, requires
the resolved object type at `+c` to equal `2b61676c`, and requires its `+20`
field to be zero. The fixture satisfies these guards with a registered
payload. Guard-failure permutations and live payload construction remain
outside this corpus.

| Low-level completion tag | Higher-level CUnit event | Builder/static event record |
|---|---|---|
| `63702661` | `40190065` | `6f055800`, record `6fcd57a8` |
| `6370266f` or `63702670` | `40190066` | `6f055820`, record `6fcd57bc` |

Both blocked variants therefore converge to one event at this bridge.
The builders set record `+8` to the event code and `+10` to the CUnit,
then invoke that CUnit's event dispatcher. A read-only observer at the actual
`6f071dc0` entry verifies receiver, event argument, record event code and
record sender in every attached completion case; suppressed/retry scans
produce no event. Assembly confirms the receiver is the CUnit held in ESI
by `6f056090`, not the low-level packet's mover pointer returned by
`6f15e0c0` for other message cases.

These CUnits have no event subscriber table (`+8 == 0`), so the original
dispatcher returns without invoking an order subscriber. The next unresolved
boundary is the movement-order handlers' effects, replacement/queued orders
and callback reentrancy. The following bounded prefixes now verify subscriber
event-ID remapping and arrival at those handlers. Delivery to the dispatcher
does not prove that an order has been completed or failed at the gameplay layer.

## Movement subscriptions and internal event remapping

Retail event constructors identify `40190065` as **CAgentArrivalEvent**
(`6f054f90`) and `40190066` as **CAgentCantPathEvent** (`6f054fc0`). Registration
wrappers `6f0561f0` and `6f056210` pass those respective IDs through the
agent's virtual `+8`, along with a subscriber-specific event ID and callback.
Static move setup `6f5ffb60` registers arrival as **`d0196`** and can't-path as
**`d0198`**, or passes those same IDs through move bridge `6f05b970`.
Target-move setup `6f5ff8b0` conditionally registers/removes the arrival
subscription. Move cleanup `6f600340` and `6f5fc120` call unregister wrappers
`6f0566c0`/`6f0566e0`; these remove the corresponding agent event for this
ability via `6f0728c0`. These setup/cleanup call chains are static evidence,
not complete executed order-lifecycle tests.

**12 bounded original-code prefixes** start at `6f071dc0` with a real CUnit
vtable and one manually constructed subscriber, execute `6f071e00` and the
actual CAbilityMove vtable `6fb62794` slot `+0c`, and stop at the real handler
entry:

| CUnit event | Subscriber event | CAbilityMove handler |
|---|---|---|
| `40190065` | `d0196` | `6f5fa7a0` |
| `40190066` | `d0198` | `6f603110` |

The corpus varies hash bucket counts `1/4/16` and initial CUnit reference
counts `1/3`. The subscriber table starts with dispatch depth byte `+0`,
bucket count byte `+1`, entry count ushort `+2`, and bucket-array pointer
`+4`. A circular subscriber node contains next pointer `+0`, source event
`+4`, callback object `+8`, and remapped event `+c`. The dispatcher matches
the source event, **overwrites packet `+8` with node `+c`**, and invokes
callback virtual `+c`. At the stop boundary, assertions verify exact handler
PC, ability receiver, remapped packet ID, unchanged CUnit sender, incremented
CUnit reference count and dispatch depth one.

These are deliberately suspended prefixes, not completed dispatcher calls:
These prefixes do not verify handler effects or unwind. The complete arrival
corpus below covers empty-order cleanup/unwind; general reentrancy remains open. Each fixture is rebuilt before the next prefix; no instruction
or virtual target is patched. The source packet's ID at dispatcher entry must
not be assumed to remain unchanged for subsequent subscribers or consumers.

Static inspection of `6f5fa7a0` shows target revalidation when ability flag
`400` is set, removal of both completion subscriptions, and an active-order
branch guarded by ability flag `4` and the unit's current order code. The
can't-path handler `6f603110` takes a distinct branch when ability flag `2` is
clear; when set, it inspects additional unit/order state and can construct a
replacement order. Those branches must be resolved and executed before
equating these events with unconditional success/failure of the user order.

## Arrival cleanup and internal order queue

Further static tracing of `6f5fa7a0` identifies the active-order branch's
sequence: clear ability flag `4`, call `6f674180`, pop through `6f691260`,
then process remaining internal orders through `6f67df00`. This branch
requires a non-null queue-head order from `6f69c6a0` and a recognized code
among `d016b..d0174`, `d0193`, `d0176`, or `d0144`. Earlier target validation
and cleanup branches can prevent reaching it. These are internal movement
events/order nodes; equivalence to the user-visible Shift queue is not yet
established.

Cleanup is broader than clearing a path pointer. `6f5fbfc0` handles the
ability's `+d8/+dc` target association; `6f5fbea0` handles `+cc/+d0`;
`6f0606c0` is called on the embedded object at ability `+90`, not on the
ability base. `6f69a840` calls movement bridge `6f05ca50`, which invokes
`Mover_StopAndInvalidatePath` with notification disabled, and then performs
unit spatial/support refresh through `6f684480`. These calls introduce
additional state and terrain dependencies into a complete arrival-handler
fixture. The repeated stop is statically inside the gameplay notification
chain, before the outer completion stop's final path invalidation; its
interaction with subscriber reentrancy needs full execution.

The queue head is unit **`+174/+178`**, resolved by `6f69c6a0`. It is distinct
from the order identity at **`+19c/+1a0`**. `6f691260` resolves the head,
follows the node's `+24/+28` successor identity, validates the successor's
canonical agent wrapper (`+c == 2b61676c`), and installs that wrapper's
`+14/+18` identity as the new unit head. Failure clears both head words to
`ffffffff`. For old-head codes `d016b..d0174`, `6f674180` separately resolves
unit `+19c/+1a0` and clears bit `4` in that order's `+20`; it does not replace
that identity. The old head's virtual `+5c` release and `6f67e130` cleanup
follow the head mutation.

**432 complete original `6f691260` calls** first pause at `6f691331`, just
before reading the old node's vtable for release, then resume to return through
the actual release and cleanup below. They vary six successor
states (none, valid, stale generation, deleted slot, wrong wrapper type,
inactive wrapper), all ten movement codes plus `d0144/d0196`, order flags
`0/4/84`, and a valid/stale `+19c/+1a0` identity. Assertions check the exact
new head, unchanged separate order identity, conditional bit clearing, and
unchanged old-node bytes. Both `6f061320` and `6f054530` handle resolvers and
`6f674180` run unmodified. The no-head error branch and subsequent order
dispatch remain outside these cases.

**Type correction:** unit `+174/+178` contains **CTask**, not COrder.
The 432 cases below use COrderPoint payloads to validate shared handle,
queue-link and deferred-release mechanics; they do not establish authentic
movement-task dispatch. User COrderPoint has code `+24`, coordinates `+48/+50`;
CTaskPoint has event `+30`, X/Y/range FloatMini pairs `+34/+38`, `+3c/+40`,
`+44/+48`, optional object `+4c`. Verified producer chain:
`5fd270 → 692120 → 691e60` translates an ordinary-point **COrderTarget**
(`ordt`, derived from COrderPoint) and **prepends** its tasks; bare COrderPoint
is not a valid input to this handler. See the full producer cases below.
CTaskPoint: vtable `6fb78ec0`, rawcode `74736b2e`, factory `6fd70f1c` (registration `693fc0`),
factory entry `680db0 → 682ac0`, base constructor `3eb0e0`, destructor `667a80`.

Internal dispatch `67df00 → 071da0 → 071dc0 → 071e00 → 5fda10` delivers
`d016b/d016c` to `5ffb60`. Acceptance clears unit `+5c` bit 1 and sets ability
`+20` bit 4, retaining the accepted task as head. The empty-task path
`67de20 → 694a10` also processes the separate user-order queue at `+19c/+1a0`,
whose successor is `+2c/+30`; it returns immediately when that head is invalid.
Actual next-move composition still requires group creation/activation, speed
production and (for `d016b`) presentation/timer setup. These are static findings,
not evidence that the 432 generic queue cases execute that composition.

RTTI identifies COrder vtable `6fb787dc`, COrderPoint `6fb7886c` and COrderTarget
`6fb78994`; all use **`6f0557b0` at virtual `+5c`**. The corpus uses the actual
COrderPoint table. That slot resolves the order's `+c/+10` agent identity,
checks canonical wrapper type `2b61676c`, and tail-calls `6f15e0e0`.
It requests deferred release rather than destroying the order immediately.
If wrapper `+20` is already nonzero, it returns without enqueueing again.

For an active wrapper, the scheduler chooses owner clock `+14`/`+68` by
wrapper identity high bit, increments clock serial `+50`, and enqueues a
request with minimum delay **`6fcd539c` (approximately 0.0001)** through
`6f15e310`. The request pointer is stored in wrapper `+20`. The complete-pop
corpus covers the positive identity domain at clock time 0.5, serial 17,
with a preallocated free request and an empty heap. It verifies serial 18,
deadline approximately `0.5 + minimumDelay`, heap admission, and this record:

| Request offset | Observed field |
|---|---|
| `+0` | zero |
| `+4` | scheduled deadline |
| `+8` | minimum delay |
| `+c` | owning clock |
| `+10` | flags `20000` |
| `+14` | serial 18 |
| `+18` | old order's agent wrapper |
| `+1c` | zero |

Clock `+38` is the request free-list head, `+3c` its active allocation count,
and the heap container begins at `+4`. The allocated block includes a
four-byte free-list header before the returned request. Wrapper `+54` still
points to the old order, and the handle registry still points to its wrapper,
but original active-agent resolver `6f061320` now returns null because wrapper
`+20` is nonzero. A second complete `6f0557b0` call preserves the same request,
serial, heap count and allocation count in all 432 cases.

The post-pop `6f67e130` cleanup executes `6f0606c0` on unit `+180`. Each case
also verifies that it cancels the fixture's pending embedded request by
setting request `+10` bit `10000`, clears unit `+18c`, and clears bits `2/4`
in unit `+190` while preserving bit `80`. Unit `+280` bit `40` is clear in
this corpus, so its additional cleanup branch is untested. Fresh request
allocation and negative-domain release remain open.

The oracle continues all **432 cases through the original due-clock
release path**. `6f052380` examines the heap root while count `+20 > 1` and
request deadline `+4 <= clock +40`. It pops through `6f04f3c0`, temporarily
sets clock time to the request deadline, clears queued bit `20000`, and calls
`6f054370`. After draining, it restores the clock's original time. Every case
first verifies retention before the deadline, then drains at equality or at
0.75 (216 cases each), and repeats the empty drain. A read-only callback hook
verifies that even overdue callbacks observe their scheduled deadline.

`6f054370` skips canceled requests (`10000`); otherwise it invokes wrapper
virtual `+48`, `6f15e500`. For the release request matching wrapper `+20`,
that callback invokes wrapper virtual `+10`, **`6f145c70`**. With empty
relationship/child lists, it marks wrapper flags `+4c` with `01000000`,
clears links and payload pointer `+54`, cancels its request, unregisters its
identity through `6f1c5490`, and returns the wrapper to its real pool through
virtual `+4`, **`6f0576b0`**. The pool is selected by global `6fd3c82c +34`;
its free head is `+14` and active count `+18`. The request block is separately
returned to clock `+38`, decrementing clock `+3c`.

Assertions cover the cleared registry slot, registry free head/live count,
invalidated wrapper identity, cleared release/payload pointers, wrapper pool
header and count, request cancellation/free list, empty heap and restored
clock. Both virtual dispatches use the original CAgentBaseAbs vtable
`6fa8099c`; no code or callbacks are replaced.

The owner notification is now recovered too. Static initializer `6f04eec0`
installs **`6f04d9c0`** through setter `6f15cb90` into owner `+254`.
Wrapper destruction calls `6f146e20` → `6f15b830` with operation 1 and the
wrapper identity; operation 1 enters **`6f04c2c0`**. It resolves the still
registered wrapper, takes payload `+54`, sends event **`40190064`** through
payload virtual `+10`, calls virtual `+34`, clears subscriptions, invalidates
payload agent identity `+c/+10`, and decrements payload reference count `+4`.
Only a resulting zero count calls payload virtual `+0`. Finally it clears
wrapper payload `+54` through `6f057c30(0)`.

**216 of the 432 release cases execute this real owner callback**, with the
original COrderPoint vtable and no subscribers. The payload starts with two
references; assertions verify event code/sender, identity `[-1,-1]`, reference
count 1, and all remaining payload bytes unchanged. The other 216 keep the
owner callback null and verify the payload remains byte-for-byte unchanged.
Both paths finish wrapper unregister/pool return and request recycling.

**Scope boundary:** no relationship/child lists or event subscribers are
populated. Zero-reference payload reclamation is covered by the separate
lifetime oracle below. Static
COrderPoint virtual `+0`, `6f03e9c0`, obtains rawcode `6f72642e` from virtual
`+1c`, looks up its class entry through `6f03eaf0`, and invokes the factory
at entry `+70`, virtual `+4`. Registration is `6f693e40` → `6f04daa0` with
factory object `6fd70e14`. The lifetime oracle validates its table/allocator
with preallocated storage. Populated
relationships, repeating requests and nonempty heap ordering also remain
separate work.

Harness detail: after a resumed case executes across the next case's
intermediate stop address, Unicorn's translated blocks must be flushed before
starting the next prefix. Otherwise the next run can pass that observation
boundary. The oracle flushes and asserts the PC at both boundaries; it does
not patch the retail code or replace the release slot.

Static `6f67df00` repeatedly resolves the queue head, constructs a CEvent
from node `+30`, sets unit `+5c` bit `1`, and invokes unit virtual `+10`.
If the handler clears bit `1`, processing returns. Otherwise bit `2` selects
between popping the head and clearing bit `2` before another iteration.
Acceptance clears bit 1; the composed in-range rejection below leaves it set.
Other bit producers and general callback-driven queue mutation remain open;
accepted next-task search, travel and arrival are executed below in an open-map fixture. Can't-path
recovery `6f5fb190` can enter this arrival cleanup after other recovery
attempts; it must not be modeled as an unconditional queue pop.

## Cannot-path recovery task sequence (static)

`603110` routes ability `+20` bit-2-clear failures to `5fb190(1)`; bit-2-set
has separate target/replacement-order recovery. `5fb190` inspects the separate
user order and target flags, attempting code-specific recovery for
`d0003/d004b/d004e/d0050`. Its fallback is **not merely an arrival/pop**.
Assembly at `5fb3d3..5fb3fe` executes:
`691260` pop → `6927a0(0)` → `691f20(d0144)` twice → `5fa7a0` arrival cleanup.
`6927a0` selects **CTaskAction**, rawcode `74736b41`, RTTI vtable
`6fb78d98` (virtual1c getter`688440`); initializer `689c40` writes event
`d0162` and argument zero at `+34`. Because `691e60` prepends, the newly
constructed task sequence is `d0144 → d0144 → d0162 → previous successor`
before the final arrival cleanup executes.

Dispatcher `5fda10` routes `d0144` to `600340`, which cancels targets/timer,
unsubscribes completion callbacks, performs `69a840` stop/support refresh and
clears ability-active bit 4. It does not itself call task pop: progression is
owned by the surrounding dispatch loop. The [blocked-goal live capture](retail-pathfinding-experiments.md#live-blocked-goal-recovery-task-sequence)
now observes this prepend sequence and empty queues after recovery. Meaning of
`d0162`, alternate recovery branches and isolated producer/factory composition
remain open; successful-arrival emulation does not cover those branches.

## Active arrival and authentic task removal

`motion-oracle.json`: **36 active-arrival cases**, retaining the previous 36
inactive-arrival cases. Original allocator setup and `680db0 → 682ac0` construct
CTaskPoint in preallocated storage; canonical wrapper registration is supplied.
Alternating `d016b/d016c` task heads run complete
`071dc0 → 5fa7a0 → 691260 → 67df00 → 67de20 → 694a10`.
Assertions cover cleared ability-active bit, invalidated task head, unchanged
payload, one deferred release with exact deadline/heap/serial, inactive handle
resolution and idempotent repeat scheduling. Both task and separate user-order
queues finish empty. Terrain/support and subscription assertions remain active.
**36 additional cases** retain a next `d016c` CTaskPoint at the current position.
The real chain `67df00 → 071da0 → 071dc0/071e00 → 5fda10 → 5ffb60 → 05b440`
rejects it as already in range, then pops it and returns. Observational hooks
assert event/payload/ability identity and absence of suppression flags.
Both release requests enter the heap with serials 1/2; the persistent movement
subscription remains, ability refcount is 2, and unit dispatch bit 1 remains set.
The range predicate legitimately prevents group creation. Together with the accepted cases below these are **144 full arrival calls**.
Populated user orders/targets and due-time destruction within this arrival
composition remain open; separate lifetime cases cover CTaskPoint reuse.

**36 accepted next `d016c` cases** execute the positive-speed producer
`5fc890/5fc900`, `05b970`, temporary generator activation, persistent group/path
initialization, member attachment, destination setup and callback registration.
Controlled base speed256 × multiplier1 yields mover speed8. The next task stays
at the unit head, ability bit4 sets and dispatch bit1 clears. Assert exact group/
path/member identities, goal `(startX+2,startY)`, unchanged group position,
empty route buffers/indices−1, budgets700/5000 and path flags400000.
**The path is prepared, not scheduler-queued or searched.** Generator recycles;
group/path stay live; occupancy increments restore. Two genuinely allocated
callback nodes replace in-dispatch tombstones, leaving three subscriptions and
ability refcount4.

Generator/path constructors execute. Group storage is an explicitly supplied
recycled CPrCluster retaining its real twelve-row member buffer; first-ever
group construction requires external Storm allocation and is excluded.
Profile/constants are controlled no-buff fixtures; no suppression flags,
zero-speed shortcuts or replacement callbacks are used. Next dependency:
authored user-order production and obstacles/adaptive/crowd modes; stock flat/slope presentation is covered below.

**36 complete first fresh group ticks** now compose each accepted case through
`16c150 → 16ce10 → 165ae0 → 167ce0 → 148100 → 14a4c0`, route consumption
and movement commit. Supplied empty route/search backing, open16×16 maps,
request mask02000000, radius.25 and scheduler tables enable original admission
and fine A*: three pops, work3, route `[exact goal, intermediate cell center,
exact start]`, fine index0, no queued request. Group coarse-mode flag is clear;
its coarse route is one converted goal, not adaptive A*. Group age resets0,
completion counter1, flags20000. Submitted speed8 produces exact velocity bits
`40ffffff/80000000`; elapsed0 leaves position unchanged. Task/subscriptions
remain active; no arrival is claimed. This executes a group tick, not a complete
owner simulation frame or the producers of all supplied map/search state.

## Composed elapsed travel, arrival and reclamation

The motion oracle covers **78 complete trajectories /681 integration ticks**
after the accepted-task fresh search. Each advances original clock`054190`
by1/32 and runs the original group tick:73 open/plane cases arrive after seven
steps; two fine-only, one adaptive and two full-owner wall-detour runs arrive after34.
One full-owner case includes the controlled repulsor described below.
Independent integer add/multiply models verify every position update; both
spatial maps' bounds and effective cell membership match every step, including
lazy insertion/removal histories. No teleport or injected arrival is used.

Arrival stops velocity, clears active bit4, empties task/user queues, updates
unit XY/support height and changed-position transform, removes both completion
subscriptions, and reduces ability refs4→2 (persistent movement subscription
remains). The next clock drain and empty-group tick verify **156 task
reclamations and78 group/path releases**. Fine-only fixtures return to unit+mover;
the adaptive/owner fixtures retain the mover-owned path, as detailed below. Each has an
empty owner group list and request heap. Previous144 arrival and36 fresh-search
cases remain passing.

Presentation covers36 controlled zero-limit cases,36 stock Footman flat cases
and one slope1/8 plane. Stock `hfoo` UnitUI values are maxPitch/maxRoll10°,
elevRad20 (all three installed MPQs). Cache setters `352db0/352dd0/352cf0`
receive radians/radius; conversion matches `689130` x87 π/180 then float32
store, but full SLK ingestion is excluded. Nonzero limits execute three mode0
terrain samples through `7437c0 → 744bb0 → 743810`, cached height descriptor
`terrain+79c`, plus original bridge lookup, sqrt and angle extraction. Sample
positions and the final plane-normal/right/forward matrix have independent
geometric checks; maximum matrix error **7.29e−8**. Support mode−1 separately
uses packed terrain corners. Height-layer construction is not exercised.

Original shipped `msvcr120.dll` sin/cos/sqrt/angle exports execute: PE sections
mapped, HIGHLOW relocations applied, five import slots resolved; report records
hash/path/exports. CRT startup, other imports and independent CRT bit parity
are excluded. Scope remains one ground member/open16×16 map, supplied caches/
pools and direct group ticks. Pitch/roll clamping, bridges/water, user-order
construction, broader obstacles, adaptive/crowd modes, full owner ticks and
first-ever heap growth remain open.


### Fine wall detour and clearance counterexample

Two exactly repeated original-code runs place blocked fine cells`(6,2)..(6,6)`
between start`(4,4)` and goal`(8,4)`. Original fine A* expands37 nodes and
returns9 points, cost132 (independently matched by Dijkstra). The group tick
consumes indices`7→5→3→2→1→0→−1`, turns/pauses and arrives after34 ticks;
exact position integration, both occupancy maps and task/group reclamation pass.
Report stores every step's start/end/velocity bits and closest wall point.

Controlled radius is **0.25 fine units /8 world**, mask`02000000`; original
request receives that radius and derives footprint class0. This is not a stock
Footman collision-profile measurement. Center segments stay outside blocked
cells, but a radius0.25 swept disc overlaps at ticks18/19. Minimum distance is
**0.22356081089800175** at tick19, cell`(6,2)`, segment fraction
`0.09706426636933764`, center`(5.959997306949725,1.780047230710551)` versus
corner`(6,2)`. Thus this fine-search/group-tick composition does not enforce
strict Euclidean disc clearance. Full owner-wide separation/visual mover pass
is excluded; this is not proof of the complete live collision outcome.


### Fresh adaptive-to-fine wall composition

One additional34-tick case constructs/activates a genuine mover-owned path
`1657c0 → 166060`; flag`200000` is produced by activation, and group`16de50`
retains adaptive routing. Original`15d360` builds all four hierarchy levels
(sides8/4/2/1), independently checked from the blocked fine cells. Group/member
`162cb0` requests use lane0,size0,budgets5000/400, each9 pops and the same coarse
route`[(4,2),(3.75,.75),(2.75,.75),(2.75,1.75),(2,2)]`. Native fine search then
produces the37-pop9-point detour. Scheduler work is`[9,0,9,37]`; no further
searches occur. All34 position/velocity/occupancy/witness rows match the fine-only
case exactly, including the clearance counterexample.

Arrival reclaims tasks and group-owned path/group pools while preserving the
mover-owned registered path`[7,104]`: three registry entries remain, not a leak
of the released group path. This near-goal case immediately advances adaptive
indices to0. Owner counter remains100; periodic scheduler updates, intermediate
coarse waypoint progression, denial/replanning and full owner passes are excluded.

## Exact point-task range predicate

`verify_wc3_pathing_range.py` → `range-oracle.json`: **4,957 full `05b440`
calls**: 4,200 ordinary, 140 adjacent-bit boundaries, 590 prediction and 27
guarded tiny conversions. Original mover lookup, clock/prediction and arithmetic;
independent integer-bit references check intermediate radius²/distance², result,
ABI and unchanged input/mover/registry/clock state.

Target = guarded `(world−origin)/32`; effective radius = guarded requested
range/32 + mover radius. Center uses stored position or elapsed-velocity
prediction according to the flag. With retail arithmetic, acceptance is
`radius² > distance² || abs(distance²−radius²) < epsilon`,
epsilon bits `3a83126f` (~0.001). This tolerance is in **squared-distance units**.
Zero-radius/range target X bits `3f8186e2` accepts; adjacent `3f8186e3` yields
distance² exactly epsilon and rejects. Another witness changes result 0→1
when prediction is enabled, without mutating stored position/time.
Stale handles, nonfinite producer inputs, clock domain 1/epoch wrap and sibling
object-range predicate `05b580` remain outside this corpus.

## Exact object-range predicate

The same range oracle adds **948 full `05b580` calls**: 600 ordinary, 144
adjacent acceptance boundaries, 144 dual-mover predictions, 36 same-identity
and 24 minimum-clamp boundaries. Exact effective radius is
`max(0.49, add(add(guarded_div32(range), target.radius), source.radius))`.
Both centers use internal coordinates directly; prediction resolves each
mover's clock independently, including both clock domains. Acceptance uses
the point predicate's strict squared-distance tolerance. Same identity accepts;
this helper does not exclude self. Intermediate bits, ABI and state preservation
are checked against independent arithmetic models.

Six expected-fault probes cover stale generation, free slot and out-of-bounds
identity on each side. Resolution returns null, then radius access faults at
`05b5e5` (source) or `05b5c8` (target). Thus valid identities are **caller
preconditions**, not a false-result branch. Caller validation, nonfinite producer
inputs, epoch wrap and full target-order composition remain unverified.

## Complete arrival dispatch and support refresh

`motion-oracle.json` adds **36 full `6f071dc0` dispatches**, through
`071e00 → 5fda10 → 5fa7a0`, returning through the real dispatcher. Matrix:
three mover positions, terrain heights −32/0/64, one/two completion subscriptions,
and unit `+5c` status 0/10000. Real CUnit/CAbilityMove/mover vtables and handle
resolution; absent targets, no active internal order, no code replacement.

Assertions: SEH and unit reference restored; subscriber depth/count zero;
arrival and optional can't-path node callbacks nulled; ability retains its
one external reference; embedded timer canceled and bits 2/4 cleared; target
identities invalid; event remapped to `d0196`; mover stopped/detached; path
disabled with sentinel destinations; support XYZ/angle updated exactly.
Read-only hooks require one arrival handler, one `69a840`, one notification-
disabled `171340`, one `684480`, and no second arrival/blocked notification.

`69a840` always refreshes support through `684480(output,1)`. Its preliminary
unit virtual `+d4` also queries height. The full cases observe **two `73c800`
queries with flags 7 and eight `745110` corner reads**. Decompiler output hides
the layer-to-flags conversion in `7437c0`: layer −1 does not reach `73c800` as
raw −1. Ordinary support here uses corner interpolation (`745060/745110/749a30`),
not the bit-8 mesh-intersection branch.

Fixture: terrain root `6fd726c0`, dimensions `+b4/+b8=4`, 25 packed corner
records at `+e4`; world origin zero. Corner height bits encode −32/0/64; empty
bridge container at `6fd726cc`. Unit virtual `+b8=6864d0` returns embedded
movement bridge `unit+164` (vtable `6fac2f10`); its angle getter resolves mover
`+c8`. Cached XY/angle match, so changed-position transform rebuilding is
excluded. All positions are in-map; elapsed movement is zero before stopping
old velocity 0.25. These fixtures do not establish fresh movement, queued-order
progression, can't-path handlers, populated targets or nested dispatch.

## Last-reference payload release and factory reuse

`verify_wc3_pathing_lifetime.py` → `lifetime-oracle.json`: **eight complete
construction/release cycles, six actual payload-block reuses** (four/three per
COrderPoint and CTaskPoint). COrderPoint chain:
`6807a0 → 0557b0 → 052380 → 04c2c0 → 03e9c0 → 678db0 → 667400 → 06a3c0`,
then wrapper/request recycling. Callback order is asserted; no stubs/patches.

Factory object `6fd70e14`, vtable `6fb7885c`, embedded allocator `6fd70e18`;
original `06a270(0x58,1)` initializes it. Class rawcode `6f72642e` hashes to
`fa0c2d84` through `198420`; class entry `+70` selects the factory. Reclaim
calls the real destructor with argument zero, then returns the payload to the
allocator free list. This path requires no OS free.
CTaskPoint uses factory `6fd70f1c`, factory vtable `6fb78eb0`, allocator
`6fd70f20`, allocation size `0x50`, payload vtable `6fb78ec0`, rawcode
`74736b2e`. Its corresponding chain is `680db0 → 682ac0` construction and
`03e9c0 → 678ff0 → 667a80 → 06a3c0` reclamation. Both classes undergo the
same assertions below. Four cycles populate owned pointer `+54`/`+4c` with
a retained object: its refcount falls 2→1, all other bytes remain unchanged,
and no second reclamation occurs. The other four cycles use null pointers.

Verified: actual factory construction, predeadline retention, duplicate release
suppression, deadline/overdue execution, event `40190064` and identity clearing
before last-reference reclamation, exactly-once payload/wrapper/request return,
registry count/removal, empty heap, restored clock/SEH, idempotent empty drain,
and reuse through the original constructor. Class/wrapper registration and
storage are supplied fixtures; heap growth, negative handles, subscribers,
relations/children and final-reference owned-object destruction remain open
(ORDER-04 is partial).

## Authored formation-rank producer

The rank consumed above comes from **`Units/UnitData.slk` field `formation`**.
It is distinct from the `repulsePrio` field and separation rank bits 28–31.
The recovered static chain is:

`6f6b0870` binds literal `formation` to descriptor `+98` → accessor `6f6aa310`
→ profile builder `6f66bf40` writes rawcode lookup `+1ec` → getter `6f680430`
→ unit creation/refresh `6f6417c0/6f64c090` calls mover interface vtable `+cc`
→ `6f698750` → bridge `6f05c3f0` → setter `6f171070` → mover `+d8`.
The interface slot is at `6fb77f7c` in vtable `6fb77eb0`. Other initializers
`6f652620/6f653800` pass an input record's `+4c` through the same virtual slot.

**1,024 complete bridge calls** execute the real handle resolver and setter
for every byte input and four prior packed words. The exact setter is
`newFlags = (oldFlags & 0xffff0fff) | ((input & 255) << 12)`.
The classifier reads only bits 12–15. Inputs above 15 can therefore set bits
16–19 too; these tests record the actual packing, not a claim that stock data
uses those inputs or that the setter clamps to a nibble.

Extraction from this installation's `War3Patch.mpq` gives 836 explicit
formation rows: value/count `0/516`, `1/10`, `2/224`, `3/25`, `4/57`, `5/2`,
`6/2`. Examples are Footman `hfoo=0`, Knight `hkni=0`, Paladin `Hpal=0`,
Rifleman `hrif=2`, Gryphon Rider `hgry=2`, Archmage `Hamg=2`, boat `hbot=3`,
and Peasant `hpea=4`. These are authored values from this installation, not
inferred role labels. The original table and extracted rows are retained as
`UnitData-retail.slk` and `formation-authored-rows.json` in the external report
directory. To reproduce table extraction:

```sh
build/bin/mpqtool -mpq /run/media/lofcz/ssd_external/Games/w3/War3Patch.mpq \
  cat Units/UnitData.slk > /GitHub/wc3-analysis/reports/pathfinding-1.27/UnitData-retail.slk
```

The complete SLK loader-to-live-mover chain and mixed-unit formation scenes
remain to be composed. The tested interval classifier and tested setter are
separate original-code executions connected by static producer/consumer
tracing; they do not yet prove complete in-game formation trajectories.

## Formation-held member movement

`PathGroup_StepMember` (`6f16a790`) consumes member flag `200000` by selecting
`6f16fd90` instead of the ordinary `6f16fbd0` route-advance path. It first
recovers speed and heading from current velocity (or uses mover facing when
stationary). The held path tests arrival; successful arrival requests speed
zero, clears forced-arrival state and unlinks scheduling. Otherwise it calls
`Mover_UpdateSpeedAndHeading` with stop argument one, which requests zero
speed while still applying the allowed turn, then unlinks scheduling.
There is no fine or accelerated search in this held branch.

**16 complete original `PathGroup_StepMember` calls** compose this branch,
arrival testing, direction math, speed/heading update and scheduler unlink.
They vary initial motion, stationary facing, arrival radius and forced-arrival
state for an eastward destination. Assertions cover zero requested speed,
allowed heading change, member arrived flag `10000`, retained formation mark
`200000`, forced-arrival clearing on success, and unchanged mover position and
velocity. Movers are already on their update list, paths are unqueued, and
group special/visibility flags are clear in this corpus.

Thus `200000` is a movement hold with turning/arrival handling, not merely a
collision-exemption exclusion. The decision pass writes member speed/heading;
actual velocity changes later in the separate group commit. Queued-path stop
and commit behavior are independently covered above, but the complete
classification → all-member decision → commit chain remains to be composed.

Static inspection also finds group flag `10000` calls `6f16e250` before each
member decision: it sets member destination from the group point plus member
XY offsets, clears member flags `70000`, and may adjust that point through an
accelerated-grid query when nonzero offsets and path flag `200000` are present.
The destination adjustment and query thresholds are composed in the corpus
below; their role in the full formation-spacing lifecycle remains open.

## Formation destination adjustment through adaptive search

`6f16e250` first computes member destination `+18/+1c` as group point
`+54/+58` plus member offset `+c/+10`, and clears member flags `70000`.
A zero XY offset bypasses adjustment. With a nonzero offset, adjustment runs
only when the member path has flag `200000` (accelerator enabled).

It obtains the owner's accelerator through `6f168d10` → `6f15afe0`, scales
both group point and offset destination by base-map `+68` (0.5 in this map),
and invokes `6f1627e0` with budget **30**, collision lane
`2 * ((path.flags >> 30) & 3)`, size `footprintClass >> 1`, and final argument
one. This is a bounded distance/partial-endpoint query using the original
adaptive search, rather than reconstructing the full member route.

The return value controls three outcomes:

| Query return | Member destination | Member flag `40000` |
|---|---|---|
| Unsigned value ≤20 | Keep offset destination | Clear |
| Finite value >20 | Use the group point | Set |
| `ffffffff` | Use query endpoint multiplied by map `+64` | Set |

For setup's direct-result branch, `6f1627e0` obtains integer endpoint
coordinates and applies the original integer-square-root helper to
`(2*dx)^2 + (2*dy)^2`. Otherwise it runs search `6f164a20`; a reached endpoint
uses `6f163440` to sum analogous parent-edge lengths. A warp-tagged edge
instead contributes two and increments accelerator `+a0`. On failure, it
returns `ffffffff` and outputs either the original source (no advance from
start) or the best node centre via `PathAcc_NodeCentre`. The warp-distance
branch is static evidence and is not included in this new corpus.

`verify_wc3_pathing_refill.py` composes destination adjustment with the original
query in **384 cases** on synthetic 64×64 fine maps and all four hierarchy
levels. Offsets 0/2/18/20/22/40/42/48 include both sides of the cutoff: on the
open map, 20 is kept and 22 falls back to the group point. Every case
first executes the query independently, rebuilds the identical map/search
state, then executes full `6f16e250` and checks the destination and member
flags. Open-map cardinal distances also have independent exact expectations.
Cases vary open/wall/gap geometry, all four footprint classes and all four
collision lanes; the separate query comparison establishes wrapper composition,
not an independent proof of adaptive-search optimality.

Representative class-zero/lane-zero results from group point `(8,8)`:
open-map offset `(40,0)` gives distance 40 and returns the group point;
a wall at fine X=32 produces `ffffffff` and adjusted point `(17,9)`;
a gap in that wall at Y=29..34 gives finite distance 59 and also returns the
group point. Consequently a bounded partial search can choose an intermediate
formation destination even when the original offset lies beyond the wall.
This is not proof that the offset is globally unreachable or that the member
has arrived. Negative/out-of-map offsets, map origins, warp query branches,
caller formation layout and live mixed-unit behavior remain open.

## Formation layout pipeline and rank buckets

Static inspection locates the offset layout entry at `6f16a5b0`. A one-member
group receives zero offsets. Otherwise it initializes sixteen temporary
records, calls `6f16cb80` to group/project members, calls `6f16d270` to derive
bucket layout dimensions, lays out nonempty buckets in rank order through
`6f16a6e0`, then recentres/rotates the offsets through `6f169e10`.
Group flag `20` selects alternate spacing constants during this process;
its UI/gameplay meaning is not yet established.

The rank-bucket prepass `6f16cb80` is executed in **324 complete cases**.
Each bucket is `0xa4` bytes: count at `+0`, member indices at `+4`, projected
XY pairs at `+34` (stride eight). It uses mover `(flagsD8 >> 12) & 15` to
choose the bucket and appends members in original group order. The return
value is the number of nonempty rank buckets. For group heading θ it computes
predicted position `p = position + velocity * elapsed` and stores approximately
`(p.x*cos(θ) + p.y*sin(θ), -p.x*sin(θ) + p.y*cos(θ))`.

The corpus covers all 27 three-member rank triples from 0/2/4, three headings
(0, 0.5, float π/2), two elapsed times and both mover clock domains. Bucket
counts, index order and return value are exact assertions. Coordinates are
compared against an independent trigonometric reference with a `1e-4` bound;
maximum observed component error is **2.485663242279834e-5**. An initial `1e-5`
bound failed near π/2 because of the original software-trigonometry result.
This verifies the coordinate convention and prediction inputs, not floating
point bit parity.

The subsequent layout chain is also executed by the row and full-layout corpora: `6f16ca80` computes the
maximum member radius within a bucket; `6f16d270` selects row capacities using
tables `6fa91d20/6fa91d88` and writes bucket fields `+94/+98/+9c/+a0`.
`6f16a6e0` orders projected members and calls `6f16cf40` to assign rows; that
helper writes member offsets `+c/+10` and advances a member cursor, using
`6f16e090` to order members within multi-member rows. `6f16d120` is a direct
indexed offset setter used elsewhere. Final routine `6f169e10` calls
`6f16bb40` for mean offsets and transforms each centred offset by the group
heading. Row dimensions, placement, spacing and complete layout are verified
below; mixed-radius/moving/oblique complete layouts and live formations remain open.

## Formation row dimensions and placement

`6f16d270` scans the maximum radius `r` within each rank bucket and sets
`d = 2*r`. It chooses a capacity table using the number of nonempty rank
buckets, and the table half using **r ≥1.5** (the comparison uses radius,
not doubled diameter). Index zero is unused for a nonempty bucket.
Capacities for bucket counts 1 through 12 are:

| Occupied ranks | Radius | Capacities for counts 1..12 |
|---|---|---|
| One | <1.5 | 1,2,3,2,3,3,3,3,3,4,4,4 |
| One | ≥1.5 | 1,2,3,2,3,3,3,3,3,3,3,3 |
| Multiple | <1.5 | 1,2,3,4,3,3,4,4,4,4,4,4 |
| Multiple | ≥1.5 | 1,2,3,3,3,3,3,3,3,3,3,3 |

The raw tables are `6fa91d20` and `6fa91d88`, each with two 13-entry halves.
Capacity `k` is clamped to member count. With spacing scalar `g`, bucket width
is **`W = d*k + (d+g)*(k-1)`**. This is the observed formula, including the
second diameter term. Group flag `20` selects `6fd541cc` instead of `6fd541c0`,
but both initializers (`6f004120/6f004110`) parse the same authored string at
`6fa91eac`: **2**. That does not establish equivalence of the flag elsewhere;
other layout stages select other constants.

Output fields are bucket `+94=d`, `+98=W`, `+9c=k` as an integer and `+a0=k`
as a float. The caller's width accumulator increases to the largest bucket
width and never decreases. **384 complete dimension calls** cover counts
1..12, radii 0.25/0.5/1.5/2 with mixed smaller members, one/two rank buckets,
both flag states and two prior accumulators. Exact arithmetic expectations
use binary-fraction inputs and the binary-owned capacity tables. The text
spacing constants are initialized directly because the CRT parser import is
not present in the emulator.

`6f16cf40` places `n = min(requestedCapacity, remainingMembers)` at the current
cursor. For a singleton it writes `(rowX, W/2)`. For a multi-member row it
sorts that member span by projected Y and writes:

`(rowX, d/2 + j*(W-d)/(n-1))`, for `j = 0..n-1`.

It advances the cursor by n and returns whether members remain. Span sort
`6f16e090` moves the strictly largest projected Y to each tail position,
producing ascending Y while moving both coordinates and member indices
together. It is selection sort, not generally stable for ties. Members
outside the selected span remain untouched.

**352 complete placement calls** cover counts 1..12, cursor 0 or 2, requested
capacities 1..4, two widths, two diameters, and repeated Y keys. Checks cover
exact index permutation, unchanged unselected offsets, cursor/return value,
and placement coordinates within `1e-5` for division rounding. These compose
the actual span sort and row writer. Complete layout composition through `6f16a5b0` is covered below for a
restricted corpus; zero-sized/invalid rows
and counts beyond the observed twelve-member table domain are also excluded.

## Complete formation layout composition

`6f169e10` subtracts the mean of all member offsets, then rotates each centred
pair into the group heading: `(x*cosθ-y*sinθ, x*sinθ+y*cosθ)`. The mean helper
is `6f16bb40`; both operations execute together in **192 complete cases** with
counts 1..12, four headings, two translations and two point patterns. Member
flags remain unchanged. The numerical reference bound is `1e-4`, with measured
maximum error **6.851267023322283e-6**; this does not assert bit-exact trigonometry
or reciprocal arithmetic.

The complete layout entry **`6f16a5b0` passes 144 composed cases** against
an independent layout model: counts 1..12, radii 0.25/1.5, one/two/three authored
rank patterns and both group flag `20` states. All original grouping, dimension
selection, sorting, row placement, mean and rotation routines execute without
stubs. Constants parsed by CRT during static initialization are seeded from
their binary-owned decimal strings, as in the preceding helper corpora.

Recovered composition rules:

- Traverse nonempty ranks in increasing rank order.
- Use the maximum computed bucket width as the common width for every row.
- Within a rank, order members by descending projected X, divide into rows by
  that bucket's capacity, then order each row by ascending projected Y.
- Begin row X at zero. Additional rows in the same rank decrement it by
  `bucketDiameter + 2.5`.
- After each nonempty rank, decrement row X by `5.5` before the next rank.
- Finally subtract the mean offset across all members and rotate by heading.
  A singleton takes the direct zero-offset branch.

The depth-gap initializers `6f0041a0/6f0041b0` seed
`6fd541c4/6fd541d0` from string `6fa91eb0` (2.5); the rank-gap initializers
`6f004180/6f004190` seed `6fd541bc/6fd541c8` from string `6fa91ea8` (5.5).
Both flag-selected variants therefore have equal values in this binary for
all three spacing constants recovered here. This is not a claim about other
consumers of the flag.

The full-layout corpus uses stationary members, heading zero, uniform radii
within each case, unique projected X and repeated Y keys. Final per-member
offsets match the model within `1e-4`, measured maximum
**1.3351440433240214e-6**. Mixed radii, moving/oblique complete layouts, layout
rebuild triggers and the full layout → destination query → decision → commit
lifecycle remain to be composed. The synthetic complete-layout result is not
a substitute for live formation trajectory validation.

## Formation refresh entry and route lifecycle

`6f16d990` is the only direct caller of the recovered layout entry. It first
calls `6f16d8e0` with the previous group point `+54/+58` and a new point. The
heading is derived from **new point minus previous group point**, using the
original length and vector-heading helpers, when displacement length is
positive. Zero displacement preserves existing heading `+70`. Either way it
sets group flag `10000`, stores the new group point, and calls layout unless
group flag `200` is set. This heading is not taken from a member's facing.

**48 complete refresh calls** cover one/three/six members, unchanged/east/
north/west point transitions, two prior headings and both flag states.
Assertions verify the new point, preserved unrelated flags, set `10000`,
heading against a numerical angular reference, and exact member bytes against
a separately executed original layout with the recovered heading. With flag
`200`, all member bytes remain unchanged. This comparison proves the wrapper's
layout/bypass behavior; the independent full-layout reference remains the
144-case corpus above.

Static caller `6f1697a0` optionally invokes `6f16d6a0`, optionally zeros group
age/counter `+5c/+60`, invokes the `6f165e30` thunk, selects a route destination
with `Path_SelectRequestDestination(mode=0)`, and passes it to `6f16d990`.
It then clears group flag `20000`. Its two direct callers are successful
`PathGroup_RequestRoute` and `6f16c4f0`'s regroup/advance condition. The ordinary-route regroup/outer-refresh chain is now composed below; warp
transitions and complete owner ticks remain open.

The flag connection is now explicit: refresh sets `10000`, which makes
`PathGroup_StepMember` run the verified offset-destination adjustment before
its movement decision. Layout, refresh, adjustment and held-member stepping
have composed original-code evidence. Cached-route group ticks are covered;
fresh-search/layout-refresh ticks and live rebuild cadence remain open.

## Regroup status and advancement gate

`6f16b120` returns a count used by the regroup caller and writes two outputs:
arrived members (member `10000`) and near-but-not-arrived members. For each
non-arrived member, it predicts position using the selected mover clock and
velocity, then compares squared distance to that member's destination
`+18/+1c`. The comparison is strict `<256` normally or `<16` with group flag
`100`, using initialized constants `6fd541a4/6fd541a8` (decimal strings at
`6fa91e84/6fa91e8c`). These are squared distances, not linear thresholds.

With no arrived member the return is the full member count, regardless of
how many are near. With at least one arrived member, an active group cooldown
`+68`, any mover `+d8 & 01000000`, or group flag `4` makes it return zero.
Otherwise it returns `memberCount - arrivedCount - nearCount`.

**1,792 complete status calls** cover all three-member arrival masks, seven
distances, normal/tight distance policies, group flag `4`, cooldown, special
mover flag and prediction at two elapsed times. The outputs and return are
checked against independent squared-distance/count expectations. Exact
threshold equality is included and excluded from the near count. These tests
identify the flag's effect, not the gameplay name of mover bit `01000000`.

Static caller `6f16c4f0` advances/rebuilds only if the status returns zero or
group counter `+60` is **strictly greater** than its limit. The limit is 99
normally, 198 with group flag `100`, or 396 when both `100` and `20000` are
set. Even then, the shared path must have a valid nonzero accelerated index
before it calls `6f1697a0(1,1)`. Otherwise, if status is nonzero and at least
one member has arrived, it increments the counter. It does not increment it
merely because members are near. Counter limits are update counts here;
wall-clock timing remains unverified; the caller is composed below.

The optional reset in `6f1697a0` calls `6f16d6a0`, whose static loop resets
each member path through `Path_ResetBuffers(-1,1,0,1)` and clears all member
flags. Its `6f165e30` step advances the accelerated index through the recovered
distance-limit selector in mode one, then mode zero if a warp marker was
reported. Ordinary-route reset/index/formation refresh composition is covered below;
warp markers remain excluded from this composed corpus.

## Composed regroup, reset and formation refresh

The motion oracle now executes **648 complete `6f16c4f0` calls**, including its
original status calculation and, when selected, `6f1697a0(1,1)`, member path
resets, accelerated index selection, destination selection and full formation
refresh. The two-member fixture varies all three timeout policies, counters
99/100/198/199/396/397, none/one/both arrived, near/far destinations, cooldown,
and shared indices zero/valid-nonzero/out-of-count. No routine is stubbed.

For the selected advance branch, the synthetic three-point ordinary route
advances index 2 to 0. The new group point is path destination `(12,8)`, not
the former group point `(8,8)`. Both member routes lose fine/coarse counts,
indices become `-1`, retry/delay counters clear, result/disabled flags clear,
and all member flags reset. Group counters `+5c/+60` reset, group `20000`
clears and `10000` is set. The complete rebuilt two-member offsets are checked
against `(0,+1.5)` and `(0,-1.5)` with a `1e-5` numerical bound.

Branches without advance preserve route state, group point and member flags.
Counter `+60` increments only when status remains nonzero, the strict timeout
has not been exceeded, and at least one member has arrived. Index zero and
out-of-count indices block advancement even when the status/timeout condition
is satisfied. This distinguishes eligibility from actually advancing.

These cases use non-warp routes and unqueued member paths. The reset call's
unlink argument is zero, so scheduling preservation is part of its static
contract but is not exercised by a queued fixture here. They do not execute
search admission, actual movement commits, live group ticks or the separate
successful-new-route caller of `6f1697a0`.

## Ordinary ability speed production

`verify_wc3_pathing_speed.py` → `speed-oracle.json`: **1,483 full calls** to
`5fc890/5fc900`: 1,081 ordinary, 24 adjacent-bit, 224 status/default and eight
missing-profile cases, plus20 registered Move-list and126 MoveSpeedBonus cases. Suppression counter`+7c` is zero. Actual empty ability-list
query`48f410` returns0; raw speed is retail `(ability+70 + 0) * ability+78`.
Varying field`+38` does not change this branch. `+70=256,+78=1` returns256;
subsequent movement bridge conversion yields8 in the acceptance fixture.

Original profile lookup supplies bounds`+1d8/+1dc`, **not base speed**.
Nonzero bounds clamp to status-selected global limits; zero/missing bounds
select defaults **without clamping**. Apply lower then upper bound. Original
startup routines`016290/2b0/2c0/2d0 → 016210/220/230/240` establish1/522;
additional distinct synthetic runtime values prove selection and default bypass.
Exact output/intermediate bits, ABI/call counts and unchanged state are checked.
Controlled `hfoo` profile and empty ability list exclude stock ingestion,
general buff traversal, polymorph substitution, special status caps
and upstream production of the speed fields.

Actual attached-list traversal adds **CAbilityMoveSpeedBonus (`AIms`)**, vtable
`6fb1f1a0`, virtual184`569830` reading bonus`+88`. `48f410` returns
**max(0, bonuses), not sum**; test negatives, ties, reversed ordering, fractions
and multiplier/clamp composition. Real Move callback184`435760` contributes0.
Bply inheritance checks use supplied negative Amov/AIms class-cache entries;
cache-miss construction and bonus population remain unexecuted.

Static speed producers: Move virtual308`5fcd80` initializes70/78; setter`5fee60`
(called by`698af0/670950`) assigns70; `5fb740` adds to70; `5fb7c0` adds to78
and handles slow-status crossing. They refresh through`6005d0 → 5fc890 → unit
virtual+b8 → movement bridge+18`. The initializer remains static; the setter bodies are executed below.

**151 full setter/publication cases** add42 direct assignments,42 unit
assignments,36 additive and31 multiplier changes. Original chain:
FloatMini`247520 → 6005d0 → CUnit6864d0 → bridge05c5c0` writes capped /32
speed to mover`+88/+b4`. Exact full ability/mover byte images, callback counts,
ABI/FS and unchanged clock/position/velocity are checked. Adjacent tolerance
values, zero-delta no-publication, eight slow-state entries and one exit pass;
entry runs`058bc0 → 16e7c0` on an ungrouped mover. Resting mover and absent
CGameUI exclude velocity clipping, group notification and active UI effects.

## Original user-order to task production

`verify_wc3_pathing_order_tasks.py` → `order-tasks-oracle.json` passes
**984 full `692120` point producers and288 full `5fd270` order handlers**.
Authentic COrderTarget factory`680970`, payload vtable`b78994`, raw`6f726474`,
size88; class factory`d70e44`, factory vtable`b78984`. Original wrapper chain
`055d00 → 154b60 → owner+10:04c220 → 057940` allocates/registers the wrapper;
owner+254:`04d9c0` binds the actual factory payload. Point/event/action/order-
parameter tasks use their own original factories and canonical registry IDs.

For ordinary point input, handler creation order (consumption reverses it):
`action0 → d0166(d0012) → d0148 → d014a → d0165 → d016b(point) → d014e →
d014c → d0165 → d0178 → d0144 → action7`. Flag0/player≤11 gives this sequence;
player>11 inserts `d014f` before`d014c`. Flag1 omits both`d0165`, omits the
player-specific branch and replaces`d014c` with`d0148`. Three incoming codes
`d0003/d0012/d0016`, flag0/1, player0/11/12/15, empty/existing head and six
coordinate pairs cover288 calls. Event-code presence does not establish each
consumer's gameplay semantics.

`order+48/+50` copies bitwise to`task+38/+40`; ordinary range`+48` becomes+0,
event`+30=d016b`. All produced tasks prepend ahead of the unchanged previous
chain, carry authentic classes/consecutive registered identities and ref1.
Input order bytes/refcount are unchanged; ability flags100→80 in this fixture.
Standalone producers additionally verify range bit copies, signed zero,
subnormals, seeded raw finite values and optional retained-object increments.
Raw numeric/object fixtures establish copying/ownership, not gameplay reachability.
Every call returns with original ABI/exception-chain state intact.

**24 complete produced-chain dispatches** additionally run `5fd270 → 67df00`
to accepted`d016b`. Original Move registration`5fe6a0 → 0725b0` installs20
events; registered Unit self-action/state handlers and the attached Move list
execute action7, cleanup`600340 → 69a840` (stock Footman terrain transform,
shipped CRT), speed refresh`d0178` and intervening events. Preceding tasks pop
and schedule deferred release; acceptance retains the remaining chain and
prepares canonical group/path/member identities, exact target and speed8.
Report records dispatched event/receiver order. Other ability subscribers are
absent; events with no fixture subscriber do not prove their general semantics.
Attached Move inheritance queries use cached negative Amov→Bply/BUsl/Asla
results; cache-miss class construction is excluded.

Boundary: preallocated factory/wrapper pools and seeded class registry; no
external heap growth/global registration. Command fields are fixture data,
coordinates use original FloatMini setters. Ordinary point branch uses invalid
target identity and`order+68=0`; populated targets/local-target handling remain
open. UI notification singleton is absent. User input/network admission remains
a separate composition requirement. Full initial unit admission and the joined
downstream lifecycle are exercised below.


## Full owner update ordering (static)

`15aa80` increments owner`+538` (wrap0 becomes`400`), updates all scheduler
buckets`167310`, publishes/resets shared objects on list`+38c`, accumulates
group radii on`+3b8` when shared objects exist, then runs every group's`16c150`.
Next it visits mover list`+440` through`1705c0`, toggles parity`+53c`, and runs
alternating separation-list`+51c` members with scratch`+540`.

`1705c0` updates visual heading`mover+c8`/angular speed`+cc` toward desired
heading`+8c`; signed shortest delta`173720`, table`d541e0` indexed by
`(mover+d8 >> 8)&15`, tiny-delta virtual`+58`, easing/clamp and2π wrap.
Disassembly distinguishes this pass from positional collision. Its numeric
contract, original table producer`004210`, tiny-delta callback and composition
with complete owner separation remain to be tested. Direct group-tick fixtures
must not be described as executing this full owner sequence.


### Generated tasks through queued-user-order arrival

The order-task oracle extends all24 produced-chain cases through fresh fine
search, clock-driven travel and natural arrival: **328 bit-exact integration
ticks;288 generated tasks completed**, including remaining`d0166` through
original`5ffeb0` (its subscription is installed separately by`5fcd80`). Actual
`691c70` publishes the COrderTarget into the user queue. Arrival drains internal
tasks and user head/tail/count; a clock drain reclaims every generated task and
all24 user orders. All five factory free lists restore32 blocks; group/path
pools release; only preexisting unit/mover/ability identities remain live.

All initial orders now enter through`680320(order,1,1)` with the full fixture
prepared before admission; see the verified chronology below. Player/network
input remains outside the fixture. Original arrival delivery needs host`+8=1`;
COrderTarget completion uses cached positive `ordt→+ord` inheritance. Other
abilities, cache misses and full owner passes remain open; FIFO and interruption
compositions follow below.


### Two-user-order FIFO composition

Four additional two-order cases use original`693490` append. First arrival
advances through`67de20 → 67abe0 → 5fd270` into the queued successor, with the
first task chain drained and queue count1. **78 integration ticks and96 tasks**
cover reversal and adjacent-float successor targets; both ordered target
identities, actual callback/admission timing and final reclamation/pool recovery
are checked. For a successor one float above the first X (`192+1 bit`,Y128),
two distinct arrival callbacks occur at the same simulation time (tick7),
including real second task generation/dispatch/group tick. This is explicit
group-tick orchestration, not a full world-scheduler cadence claim. Initial
first-order admission uses the complete chronology below.


### Complete singleton owner updates and visual settling

One additional wall trajectory runs actual`15aa80`, including44 owner updates,
44`167310` scheduler updates,36 group ticks and44`1705c0` visual-heading calls.
All64 scheduler buckets are checked per update (**2,816 checks**), plus owner
counter/parity/order and heading bits. Native`1711b0 → 170800` links the mover
into initially empty owner`+440`; original`004200/004210` initialize threshold/
turn tables, using guarded shipped CRT`isdigit` at`5000f1d5` via IAT`a7c4fc`.

Translation/arrival matches the34-tick detour exactly. One release-clock step
and eight additional settling steps give43 clock advances; original virtual
`+58=170cf0` snaps heading, clears angular velocity and unlinks the visual
mover. Unit presentation captures heading before the later same-frame visual
pass. Member path`[7,104]` remains owned by the mover (registry live3).
Shared list`+38c` and separation list`+51c` are empty: no`1702f0` executes, so
this closes singleton owner ordering, not populated separation or crowd behavior.


### Active replacement versus interrupt/prepend

`680320` ABI: thiscall ECX=unit, stack`[order, mode, dispatch]` at entry
ESP`+4/+8/+c`. Three full replacement journeys use`(order,1,1)` at tick3 while
an active order and queued successor exist:47 integration ticks total, only
replacement admitted/arrived, old successor never reaches`5fd270`, all three
orders/tasks/group/path allocations reclaimed and unit refs4→4→4.

Mode1 executes`673610 → 673e80` queued cancellation (`d02a6`), then`673fe0`
active/task cancellation and`693490` append. Mode0 instead uses`673fe0` plus
`691c70` prepend: a separate complete call proves queue
`[new, old-active, old-queued]`, count3. Mode0 continuation is covered by the three cases below.
Original caller`233de0` selects`(order,1,1)` statically; widget escape`654090`
selects`(order,0,1)` dynamically. Calling the mode argument “queued” would hide
this distinction; replacement and interrupt/resume are separate contracts.


Three full mode0 journeys add **149 ticks and135 task events**: interruption
at tick3 → temporary destination → regenerated original active destination →
queued successor. All successor admissions run through genuine completion
and order dispatch, with queue counts3→2→1, original user-order identities,
fresh resumed task identity and exact admission clocks asserted. Integration,
final pool reclamation and reference balance pass; group/path pools recycle
through the stages. No manual handler calls resume the old orders. Controlled
command fields and explicit group scheduling remain fixture boundaries.


### Complete initial unit admission

The order-task oracle now prepares all search buffers, route storage, scheduler
budgets and speed inputs before calling`680320(order,1,1)`. **34 complete initial
admissions** execute original cancellation,`693490` queue publication,
`67abe0` ability validation/dispatch,`5fd270` task production and`67df00`
acceptance without mid-call fixture provisioning. Each generated task sequence
is independently predicted and asserted before dispatch.

Initial`d0012` selects flag0. Initial`d0014` selects flag1 and omits the early
`d0165`; the fixture's zero return from`058040` normalizes the later policy to
flag0. The separate direct-handler corpus retains unrestricted flag coverage.
The composed corpus passes24 ordinary arrivals, four two-order FIFO journeys,
three replacements and three interrupt/resume journeys, including exact motion,
queue identities and final pool/reference recovery.

Verified with the documented binary/CRT hashes using
`verify_wc3_pathing_order_tasks.py --binary <game.dll> --report <report.json>`;
the report records`complete_initial_admissions=34` and`passed=true`.
Factory-created order command fields remain controlled input; UI/network
production, populated targets, runtime obstacles, multiunit conflicts and full
world scheduling remain excluded. This closes the manual first-admission
boundary, not the broader BASE-06 contract.


### Owner updates with a controlled separation pair

The motion oracle also passes one active singleton trajectory with a registered
eligible repulsor. Its report records44 owner updates,43 separation updates,
branch counts33 cooldown/2 positive-`c0`/8 accumulation, and four accepted
separation attempts. The independent separation model's maximum error is
`3.2824460810265688e-6`. A separate16-tick post-arrival pair has14 attempts:
ten accepted and four blocked.

These fixtures use a controlled repulse-enabled profile; stock Footman
`repulse=0` remains a negative control. They extend singleton owner scheduling
through populated separation, but do not establish crowded active groups,
mixed profiles or general non-binary-fraction numeric parity. Reproduce with
`verify_wc3_pathing_motion.py --binary <game.dll> --report <report.json>`;
The combined report now contains78 elapsed trajectories,681 integration ticks,
156 task reclamations and78 group/path releases.
Inspect`move_owner_active_separation_trajectories` and
`move_owner_separation_cases` alongside`passed=true`.


### Initial admission through owner updates

BASE-06.2/.3 join the original `680320(order,1,1)` admission with original
`15aa80` owner updates. The report is `base-06.3-owner-fifo.json` under the
report root, evidence **O** (unmodified original instructions; real class vtables
and shipped CRT). Run:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_order_tasks.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --report /tmp/base-06.3-owner-fifo.json
```

The single-order baseline moves from grid `(4,4)` toward `(6,4)`, arriving
at clock tick7. Nine owner updates include fresh route creation, seven elapsed
steps and release. Its route, every raw position/velocity pair, arrival clock,
dispatch sequence and restored five32-block factory pools equal the explicit
group control exactly. The owner calls original table initializers
`004200/004210`; resolving the shipped CRT `isdigit` import is required.
Scheduler countdowns, owner counter/parity and empty shared/separation lists
are checked on every owner call. Visual heading settles through original
`170cf0`, leaving group/visual lists empty and mover links cleared.

Four FIFO cases extend this fixture with queued destinations `(8,8)`, `(4,4)`,
`(4,8)` and X=`192+1 float32 bit`, Y128 in world coordinates. They arrive at
clock ticks27,20,27,8 with29,26,29,10 owner updates respectively. Actual
`693490` publication and completion-driven `67de20/67abe0/5fd270` admit the
second order. Callback identities and target bits remain FIFO, then user
head/tail/count, generated tasks, group/path pools and wrappers all return to
idle. The adjacent-float successor arrives on tick8: owner iteration does
not explicitly run a newly linked successor at the predecessor's same clock.
Six settling updates are needed in the reverse case.

These fixtures still seed map descriptors, existing mover/unit/ability state,
class caches and allocator backing storage. Original producers replace those
boundaries in BASE-06.4; this closure does not claim full public world creation,
player/network admission, populated shared groups or separation.

### Producer-built frozen baseline

BASE-06.4/.5 and BASE-04.1/.2 use the same two ordinary-point scenarios, now
with original map/mover producers and frozen intermediate state. Evidence **S/O**:
Ghidra caller/constructor inspection and unmodified original instructions,
including the shipped CRT. The manifest is
[`retail-owner-baseline-1.27.json`](../../../tools/ghidra/fixtures/retail-owner-baseline-1.27.json);
its hash-guarded numeric expectations are
[`retail-owner-baseline-states-1.27.json`](../../../tools/ghidra/fixtures/retail-owner-baseline-states-1.27.json).
Neither contains executable retail bytes or requires installed assets in CI.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_order_tasks.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --producer-baseline --report /tmp/base-06.5-frozen-baseline.json
make test-pathfinding-tools
```

Reports `base-06.4-producer-baseline.json` and `base-06.5-frozen-baseline.json`
are under the report root. Each frozen case executes twice after original-image
page restoration and clearing supplied storage. Arrival ticks are7 and27;
11 and29 owner snapshots plus two initial-idle snapshots compare exactly.
Normalized output digests are respectively
`d67748378dd61f2c4a351e731ac2b1ae2b1784d4b8645087de293ca6320dc992`
and `e20d71fa91ea72e7ca150fb7f23995d41081228046b936a24a7aa2b416a670bd`.
The first route, every position/velocity word, arrival tick, completion sequence
and restored payload counts also equal the earlier manual baseline. This is a
finite deterministic original-code fixture, not a complete engine parity claim.

The map producer chain is terrain bounds`78b0a0` → `04c860/15ab60` → original
map/search pool factories and hierarchy`04e0b0/15d360`. Six newly registered
map identities are `[0,100]` through `[5,105]`; dimensions/scales are
5×5/8,16×16/1,17×17/2,8×8/4,4×4/8,2×2/16. Search identities are `[6,106]`
and `[7,107]`. Original proximity constructors`14c280` initialize the free
record sentinel; zeroing that field incorrectly means record0 is already free.
The four adaptive maps and two search entries reuse supplied object storage;
their first-allocation constructors remain excluded.

The actual`78c090` caller passes pointers to initialized terrain step32 and
simulation interval text`0.03`. Original initializers`017cd0/001e50` produce
raw words`42000000/3cf5c290`; the latter is one ULP above host float`.03`.
Supplying two unit scales instead wrongly derives a`.49` world speed ceiling.
The original loader now derives the ceiling, with no manual override. This
fixture separately advances its main clock by raw`3d000000` (1/32); matching
real frame cadence remains SCHED-01.1.

Original mover constructor`14fa30` and spatial constructors`14c1d0` precede
factory`14ee90/1512c0`, virtual activation`16ea70`, registration and
spatial linking. The resulting mover identity is `[8,108]`, spatial identities
`[9,109]/[10,110]`. Owned path`1657c0/166060` gets `[11,111]`. Original radius,
speed, position, turn and propagation-window setters supply the controlled
parameters. Point-order/group/task admission and all later updates remain
original producers from BASE-06.1/.2/.3. The owned-path adaptive bit produces
group flags`600000`, compared with the historical manual flags`400000`.

The version1 manifest defines build/CRT and terrain/support-data hashes, map,
entity/handle identities, initial clock, controlled advance, storage reset,
commands and terminal invariant. It explicitly makes no RNG seed claim.
Snapshots retain unsigned raw words for cells, record tags/free indices, path
indices/flags, all64 work/countdown budgets, actual member rows, pose, motion,
visual state, queues, registry/reference counts, deferred requests and event
order. Only known owning pointers become stable role names; unknown live
pointers fail. Spatial record type1 is live, type0 retains a region pointer,
and retired type2 uses scalar metadata; preserve its second word as an integer.
Active-group snapshots follow the newly admitted successor's actual group/path,
then retain that last group's released state through idle. Addresses in event
sequences name original code entry points, not allocated object pointers.

`wc3_pathing_scenario.py` supplies exact first-difference reporting and rejects
missing/changed expectation files and build/data hashes. The historical motion
example [`retail-motion-snapshot-1.27.json`](../../../tools/ghidra/fixtures/retail-motion-snapshot-1.27.json)
encodes `motion-engine-exact.json:move_owner_trajectories[0].steps[0]` alongside
the complete order snapshots. Unrecorded historical clock/events/membership
are explicit null observations; they cannot establish empty or equivalent state.
CI checks idle invariants, FIFO completion and rejection of one-bit motion
changes, missing ticks, changed cells/budgets/routes/membership/events and hashes.

Remaining supplied boundaries are assigned rather than hidden:

| Boundary | Remaining task |
| --- | --- |
| Existing unit/Move/owner state, class/profile caches, unit attachment and owned-path binding | BASE-03.1 |
| Preallocated wrapper/task/group payloads; first group construction and external heap growth | ORDER-04.4 |
| Reused map/search objects, node/heap/vector capacity and spatial allocator growth | MAP-05.1 |
| Shared auxiliary capacity growth | GROUP-03.3 |
| Synthetic terrain vertices/height layer; no file-backed map deserialization | MAP-02.1 |
| Controlled main-clock advancement and stationary maintenance clock | SCHED-01.1 |

The map maintenance clock retains two original requests; the advanced main
clock drains its task/order requests. Fourteen baseline identities, unit
reference count4, empty user/internal queues, zero velocity and empty group/
visual lists are asserted initially/finally as applicable. Broader map loading,
owner first construction, multiunit groups, runtime obstacles, public player/
network admission and real clock/RNG producers remain outside this closure.

## Fresh shared pair through owner arrival

**GROUP-02.2 (O).** `order_tasks --shared-pair` adds a second existing
Unit/Move bridge to the producer-built baseline, constructs its mover/spatial
objects and owned path through original factories, and admits both point
orders through `680320`. Their independent groups are then replaced by one
original request: `058430` creates it, `16db30` selects the destination,
`169620` registers both mover identities, `16dc90(1)` selects policy, and
`16bcf0 → 16bdb0 → 16b7b0` recruits both into one persistent group. No member
row, formation destination, route or runtime callback is written by the harness.
This controlled core producer is sufficient for this owner experiment;
selected-player, JASS and AI callers remain **GROUP-01.1**.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_order_tasks.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --shared-pair \
  --report /GitHub/wc3-analysis/reports/pathfinding-1.27/group-02.2-frozen-shared-pair.json
python3 tests/test_pathfinding_scenarios.py
```

Inputs and exact expectations are
`tools/ghidra/fixtures/retail-shared-pair-1.27.json`: first mover world128,128,
second128,160, radius8, speed256, turn/window0.5, target192,128, request policy1.
Both owned routes start empty. The original shared formation produces distinct
member destinations, followed by fresh member fine routes and a shared adaptive
route. Main-clock/owner execution is `054190 → 15aa80` throughout; there are no
explicit group ticks during travel or completion.

Expected and observed: eight owner decision/commit passes (fresh plus seven
elapsed), each **decision(first), decision(second), commit(first),
commit(second)** at `16a790` and `16fe20`. Both units naturally arrive on tick7,
complete their internal tasks, empty their user queues, and release both orders,
all three persistent groups/paths and the temporary request. Twenty baseline
identities remain; unit references return to4; all five task/order classes
recover32 payloads; main deferred requests, group list and visual list drain.
The original unit callbacks are captured separately for both members; additional
object callbacks retain their receiver identities/vtables instead of being
silently discarded. Both positions are checked against exact old-velocity
integration on every elapsed tick.

Two independent executions match every frozen intermediate word: motion/facing,
both unit poses/queues/callbacks, member rows, owned/shared routes and indices,
spatial cells/tokens and both object rectangles/flags, all64 work budgets and
owner events. Canonical digest:
`fc29e310cdbe5b8b1f2e62bf7ac03f784fa277d060b3cd7aa34165039c5c0c73`.
Asset-free tests reject changed second-member velocity, callback/arrival removal,
missing second state and a commit moved before the second decision.

Binary/CRT hashes are unchanged from the frozen singleton baseline. Supplied
existing class/unit/Move state and recycled allocator capacity retain the
BASE-03.1, ORDER-04.4, MAP-05.1 and GROUP-03.3 boundaries. File-backed terrain
and production clock cadence remain MAP-02.1/SCHED-01.1. This closes the fresh
unobstructed two-member owner lifecycle; walls, failed member routes, mixed
speeds, runtime membership changes, public producer sharing and populated
repulsion remain separate tasks, starting with **GROUP-02.3**.

## Shared pair with terrain obstruction and reversal

**GROUP-02.3 (S/O/C/L).** The shared-pair harness accepts a versioned
`--pair-fixture`. Four frozen controls distinguish terrain edits from the query
policy that actually consumes them:

| Fixture suffix (`retail-shared-pair-…-1.27.json`) | Profile mask | Terrain | Arrival ticks | Exact repeated output digest |
| --- | --- | --- | --- | --- |
| `wall-maskless` |0| Three blocked cells |7 /7|`60637aaec72387184cbfcf34ccb60cb6d76efb5f596cd6fb4ea81bd98507d1f9`|
| `ground-open` |`02000002`| Open |7 /7|`62ee245012478fc03779b2b06c8fd6e3b1899bb43caa35ee47f06cf099aa2a76`|
| `wall` |`02000002`| Three blocked cells |19 /25|`08c3390272a97ca3fe362963dccd16fe0a8c5885ec6cb3d161675fbb5e3199d2`|
| `ground-reversal` |`02000002`| Set, then clear the same cells |7 /7|`6bd48162c2fb883f6f775aa096e342d9c19e70cfd0d123cfe561547162a1f5e4`|

The wall occupies fine cells `(5,3)..(5,5)`. Original world editor`04d870`
receives cell-center world coordinates, mask2 and blocked1; explicit
`04e0b0(0)` rebuilds the hierarchy. Low24 occupancy bits are preserved and
all edits are checked against the original fine map. The maskless control
retains the earlier pair's identical route/trajectory despite terrain changing;
it is a negative control, not evidence of obstacle routing.

### Stock Footman mask producer

A fresh bounded read-only Frida capture adds `--profile-events` to the existing
observer. Original getters`690c20`/`690c80` return Footman query2/category`ca`.
Original bridge`05c7e0` publishes fine-object category`010000ca` and owned-path
mask`02000002`: `05c7b0` changes the object's low24 category bits;
`05c770 → 168c40` stores `(query &ffffff) | (query <<24)` at path`+9c`.
CUnit virtuals`+164/+160`, thunks`678b50/678b60`, read existing profile fields
`+1b0/+1ac` through the hashed rawcode table. The stock capture has two getter/
publication sequences and a complete trace-end marker, with no observer error.

```sh
DISPLAY=:94 WAYLAND_DISPLAY= WINEDEBUG=-all \
WINEPREFIX=/home/lofcz/.local/share/open-realm/wine-pathfinding-re \
/home/lofcz/.local/share/uv/tools/frida-tools/bin/python tools/frida/trace_wc3_pathfinding.py \
  --data /run/media/lofcz/ssd_external/Games/w3 --map 'Maps\PathingRE-StockTurn.w3m' \
  --seconds 130 --samples 1000 --profile-events --x11-display :94 --continue-at 80 \
  --output /tmp/movement-profile-stock.jsonl
python3 tools/frida/verify_wc3_profile_trace.py /tmp/movement-profile-stock.jsonl \
  --report /tmp/movement-profile-stock-verified.json
```

The harness supplies these observed values in its existing hfoo profile cache,
then executes the original CUnit getters and complete`05c7e0` bridge for both
units. Custom fixture radius8 remains supplied. The full`6945a0` caller also
runs`674310`, which validates/traverses attached abilities using the complete
class-descriptor graph. That graph is absent from this controlled baseline;
a full-call attempt reaches an invalid class-descriptor read. **BASE-03.1**
owns that missing notification context. No callback is patched or replaced,
and the harness does not claim to execute that later notification traversal.
Air/water/amphibious profile producers remain **BASE-02.1**.

### Owner result and exact replay

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_order_tasks.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --shared-pair \
  --pair-fixture tools/ghidra/fixtures/retail-shared-pair-wall-1.27.json \
  --engine-library /tmp/wc3-world-velocity-engine.so \
  --report /tmp/group-02.3-wall-ground-frozen.json
```

Report`group-02.3-wall-ground-frozen.json` matches both complete runs against
all frozen words. Both owned fine routes are12 words; each member retains its
own detour and a two-word adaptive route, while the shared group owns its own
two-word adaptive route. The two units travel on opposite sides of the wall.
The first completes on clock`.59375` (tick19), membership shrinks from2 to1,
and the survivor completes on`.78125` (tick25). All decisions precede all
commits at every two-member and singleton pass. Group identity/ownership,
queues, callbacks, spatial state, budgets, arrival, deferred release and visual
settling remain frozen. All orders and all three groups/paths reclaim, leaving
20 baseline identities and unit references4.

All **46** actual owner-driven velocity/facing commits compare exactly against
the production C world adapter, including stops and the membership transition.
`retail-wall-pair-velocity-1.27.json` replays these original words at O0/O2.
The reversal's routes and trajectories exactly equal the ground-open control,
but rebuilding leaves a real retained fine-object word (`+38`,33 versus41).
The fixture preserves that difference instead of resetting metadata to force
whole-state equality. Asset-free tests assert wall-induced route changes,
maskless negative behavior, removal/arrival timing, decision-before-commit
ordering and reversal travel.

Reports and frozen-source hashes are `group-02.3-*.json` and
`movement-profile-stock-provenance.json` under the documented report root.
Numeric-only `retail-ground-profile-1.27.json` freezes the observed profile
publication contract; its parser rejects missing completion, truncated getters,
observer errors and changed category/query/path words. `--record-pair-fixture`
is an explicit original-expectation export after complete identical runs;
normal replay always requires the frozen input/output hashes and exact states.

This closes one wall case and its controls, not failed-route/alternate-order
policy, runtime wall edits during travel, widget escape, mixed speeds, class
notification ownership, populated repulsion or production clock cadence.
Next bounded owner extension: **GROUP-04.3**, reclaim/reuse a member identity from movement callbacks while
its shared group is travelling; callback-boundary cancellation/pruning is now [verified](#callback-timed-membership-mutation). public entry sharing remains **GROUP-01.1**.

## Callback completion, surviving cohort and empty teardown

`verify_wc3_pathing_order_tasks.py --shared-pair --callback-finish` composes
original `16d4e0` member completion at an actual `16fa00` slot54 entry, resumes
the callback/prepass, then uses only original `15aa80` owner updates through the
survivor's natural arrival, deferred order/task reclamation and visual idle.
The four trigger/victim combinations each run twice from fresh state. The
open and stock-ground wall fixtures retain every normalized owner snapshot,
trajectory word, member row, decision/commit ordering and lifecycle event.

The second existing Move uses the original `5fee60 → 247520 → 6005d0` speed
producer to publish128 world units/sec against the first member's256. Its
supplied existing ability includes the embedded FloatMini vtables written by
constructor `5f9f9d..be`; supplying only values70/78 is insufficient for this
setter. This fixes fixture backing, not the open BASE-03.1 class-construction
and complete gameplay callback graph requirements.

| Frozen fixture | Survivor after completing first / second member | Exact production C world commits |
| --- | --- | ---: |
| `retail-callback-finish-1.27.json` | tick13 / tick7 | 48 |
| `retail-callback-finish-wall-1.27.json` | tick42 / tick19 | 130 |

Original shared speed is4 grid units/sec while both members are active.
Completing the slower second member changes the subsequent first-member cap
to8. Completing the first member leaves the second cap4. Every later commit
belongs to the survivor and follows its decision; the independent production
world adapter matches all178 observed commits exactly. These comparisons
verify the velocity kernel, not the engine's whole group trajectory.

Membership removal alone **does not rerun formation layout** in these fixed
point journeys. The retained44-byte row is exact immediately after pruning;
its reserved offset and destination remain unchanged through travel. The
observed `16d990 → 16a5b0` layout occurs once with both initial members. A
subsequent actual refresh producer must be composed before claiming new
survivor offsets or destinations (GROUP-04.5).

After the remaining member arrives, the following real prepass starts with
one `-1/-1` member identity, prunes it and reaches group virtual10 `1699c0`
with count0. The destructor clears shared ownership, unlinks the group,
releases its group-owned route and unregisters/returns the group. The owner
head is null; both individual movers retain their original canonical identity
and owned-path identity. User queues/internal tasks drain, all five payload
factory free lists return, wrapper pools return and references reach their
pre-order values. This is GROUP-04.2 evidence; group teardown does not destroy
individual mover paths.

Open cases SHA256:
`280033d83bf1155013b0b12248017fdce69b6c8547f46d0938610e963e036733`.
Wall cases SHA256:
`da6688056c1895fe4f16025d3de233364592d56c95228300e93a2a623f0c0016`.
Reports are `group-04.4-finish-{open,wall}-final-export.json` and the strict
`group-04.4-validated-frozen-corpus/` replay. Exports require an explicit new
filename and complete identical repeats; ordinary replay verifies the frozen
case digest and entire result.

Ghidra now persists247 role names,13 partial layouts,87 verified fields and22
explicit x86 prototypes. `16b060` returns its output pointer in EAX, verified
from both return paths; `16d4e0` consumes the member pointer on stack4 and
returns with ret4. `MapPathfindingTypes.java` preserves these contracts and
metadata readback is `group-04.4-ghidra-survivor-types-final.json`. Move+7c
remains undefined because constructor zero does not establish its semantics.

The applicable engine correction is [active cohort speed](retail-pathfinding-engine.md#active-move-cohort-speed).
Full callback-timed mover reuse followed by actual formation refresh and new
survivor destinations remains GROUP-04.5. Retail member flags, shared overrides,
queued cohort handoff and all-decisions-before-commits remain GROUP-04.6; these
slices do not close full engine fidelity or certify a live retail witness.


## Owned-path factory accounting

GROUP-04.7 is an explicit prerequisite uncovered while composing04.5. The
previous producer fixture constructed each individual `CLrPath` with1657c0,
registered it with166060, and supplied `mover+a8`. That established canonical
identity but omitted the original owner958 allocation accounting. Destroying
that path through `16eb20 → 1660d0 → 1c5490 → 167900` decremented a live count
which its direct registration had never incremented. The complete survivor/reuse
diagnostic exposed the resulting underflow; the earlier callback prefix's
mover/spatial/canonical assertions did not cover this path-pool count.

`wc3_pathing_baseline.create_owned_path` now supplies constructed recycled
backing and invokes the unchanged original `14ec50 → 166130 → 150d50 →
virtualc166060` factory. It verifies the returned pointer, preserved previous
free head, and exactly one increment each to live18 and allocation1c. The pair
uses the same helper. Supplied existing Unit/Move binding is unchanged and
remains BASE-03.1; native heap growth/failure remains MAP-05.3.

Original owner958 therefore has one individual path plus one active group path
for a singleton, or two individual paths plus one active group path for the
joined pair. Callback mover destruction must push its owned path's header,
link it to the previous free head, and change pair live3 to2 without changing
allocation1c. Reallocating that mover creates its two spatial objects but no
replacement owned path, so the path counts/head remain unchanged. These
assertions execute for both victim roles and callback positions, including the
explicit missing-spatial-registry counterfactual; that counterfactual still
receives no native evidence. Natural group teardown in ordinary complete owner
journeys retains the individual paths instead of claiming path live0.

The original path factory preserves all frozen motion/identity expectations.
The focused O report `group-04.7-native-path-factory-reuse-open.json` repeats all
four callback cases with the existing cases digest
`7ba73aa142594f52f1ac9eca5ed85f1a7c21fe0527065aacc9c8e2944da3b6db`.
`group-04.5-native-path-factory-short-parent-replay.json` retains the pair's
arrival tick7 and repeat digest
`fc29e310cdbe5b8b1f2e62bf7ac03f784fa277d060b3cd7aa34165039c5c0c73`.
These are original-code accounting checks, not new engine trajectory parity.
OpenRealm's `routePath_t` uses inline storage and has no corresponding retail
path pool; this fixture correction does not warrant a production pool shim.

Ghidra `game.dll` is saved with254 role names,14 partial layouts,90 verified
fields and29 instruction-backed x86 prototypes. New factory/destructor roles
include14ec50,150d50,166130,167900,170aa0,169840 and1660d0. The factory has ECX
output, EDX context and stack4 activation;150d50 returns the object in EAX and
consumes two stack words. The prior decompiler's void return was disproved by
`LEA EAX,[ESI+4]` and the full original caller. Readback is
`group-04.7-ghidra-native-path-factory-types-final.json`; pool bytes below14
remain undefined. No native heap-allocation or full-world lifetime claim is
made by this prefix.

Fresh validation is `group-04.7-native-path-factory-validated-corpus/corpus-results.json`: all116 declared outcomes reproduced, including frozen singleton/FIFO/pair trajectories, callback reuse on open/wall maps, the missing-registry-alias control, completion/survivor journeys and retained adaptive differences. Source hashes still matched at completion. `LD_LIBRARY_PATH=/tmp/wc3-sdl2-build make -j8 test` passed76 Python tool tests and36678/36678 assertions in2126 tests for each RoC/TFT schema (`/tmp/wc3-group-04.7-native-path-factory-validated-full-suite.log`). The first validation attempt correctly rejected the old annotation-schema checksum; only the fresh validated run is the checkpoint. GROUP-04.7 is closed; actual refresh and complete survivor/reuse composition remains GROUP-04.5.


## Completed-member reuse through survivor arrival

GROUP-04.8 splits the bounded released/reused-member lifecycle from04.5's
actual formation refresh/new-destination requirement. Its O composition is
`verify_wc3_pathing_order_tasks.py --shared-pair --callback-finish
--completed-member-reuse --finish-fixture FIXTURE --pair-fixture PARENT
--engine-library LIBRARY --report REPORT`. The controlled original slot54
boundary invokes `16d4e0 FinishMember` before `16eb20 Mover_Destroy`, then
`14ee90` reallocates the same mover storage with a new canonical generation.
The original prepass resumes and prunes the old row. Only original clock/owner
updates drive the survivor's remaining decisions, commits, arrival, teardown
and visual settling; no member, route or callback implementation is replaced.

Each open/wall fixture covers both victim roles and both callback positions,
with two identical complete original runs per case. The exact survivor raw
trajectory also equals the frozen completion-without-reuse control. The
reclaimed actor's state changes independently; comparing the survivor alone
preserves that distinction instead of claiming an unchanged whole world.

| Frozen fixture | First/second victim survivor arrival | Exact production world velocity commits |
| --- | --- | ---: |
| `retail-completed-member-reuse-1.27.json` | tick13 / tick7 |48|
| `retail-completed-member-reuse-wall-1.27.json` | tick42 / tick19 |130|

The open cases digest is
`54067d8b76fdb2b5b2fdaf38337d8a8cebdec144e40e213e594d0e7a2fb99e7a`;
the wall digest is
`d13956929d895117ec8d62f69345ea49e9adcd64338f30337bd58221364a9753`.
Exports are `group-04.8-completed-reuse-open-export-v2.json` and
`group-04.8-completed-reuse-wall-export.json` under the report root. The export
flag is explicit; strict replay uses committed frozen files and forbids
export-as-verification. C comparisons cover all178 observed velocity commits,
not full engine routing/cadence equivalence.

At final cleanup owner958 has live1/allocation5: the survivor's owned path
remains, the retired actor's owned path is on the free list, and all group paths
are released. The group live count is0; both Unit references return to4, both
user queues/internal task chains are empty, and group/visual owner lists are
empty. Old mover/path/spatial handles remain rejected after storage reuse;
the survivor's complete44-byte row is unchanged at pruning. Absent replacement
paths are represented as null, never reads through address0. Ghidra persists
this composition in FinishMember/Mover_Destroy comments and is saved.

Scope remains controlled completion before reclamation. The actual gameplay
`RemoveUnit` callback graph and replacement Unit/Move binding are BASE-03.1;
the replacement actor is not admitted to movement and has no owned path. This
case neither creates a new destination nor reruns formation layout: the
survivor retains its old slot until arrival. Those actual refresh producers
remain04.5. OpenRealm's active cohort speed, stale member exclusion, independent
cohort IDs, edict reuse and save/load handling were integrated in04.4; this
composition confirms those lifecycle boundaries without requiring a retail
pool abstraction in the inline-route engine.

Fresh validation is `group-04.8-completed-reuse-frozen-corpus/corpus-results.json`:118/118 declared outcomes, including the two newly frozen exact engine comparisons. All source fingerprints matched at completion. `LD_LIBRARY_PATH=/tmp/wc3-sdl2-build make -j8 test` passed77 Python tool tests and36678/36678 assertions in2126 tests per RoC/TFT schema (`/tmp/wc3-group-04.8-completed-reuse-full-suite.log`). GROUP-04.8 is closed; actual formation refresh/new destinations remains04.5.


## Actual survivor point replacement and formation refresh

GROUP-04.5 uses the completed-member reuse composition in04.8, then at elapsed
tick3 creates an original `COrderTarget` and invokes the surviving Unit's
complete `680320(order,1,1)` admission. This is mode1 replacement, not a layout
helper call or a write to the group's destination/member row. The original
cancellation drains the old internal point task, new Move tasks assemble a new
singleton request, and the next owner tick executes
`16ce10 RequestRoute → 1697a0 AdvanceAndRefreshFormation → 16d990
RefreshFormationPoint → 16a5b0 LayoutFormation`. The old group remains separately
tracked through its empty destructor; the new group moves to the new goal and
also reaches empty teardown. All observer hooks are removed before each repeat.

Reproduce with the existing order oracle's `--shared-pair --callback-finish
--completed-member-reuse --retarget-survivor --finish-fixture FIXTURE
--pair-fixture PARENT --engine-library LIBRARY --report REPORT`. Each fixture
covers both victim roles and both callback positions, with two identical raw
complete original runs per case. Both initial point/group identities and the
actual replacement order/new group identities are retained. The supplied
replacement world point is `(384,448)`, published by the original request as
fine point `(12,14)`; the retired/reused actor remains outside the new request.

| Frozen fixture | First/second victim survivor arrival | Exact production velocity commits |
| --- | --- | ---: |
| `retail-survivor-retarget-1.27.json` | tick98 / tick55 |314|
| `retail-survivor-retarget-wall-1.27.json` | tick97 / tick61 |324|

The open cases digest is
`179d8f134d22c50c1893247073bb074da302559550ad55a7d26691a749b4ee6e`;
the wall digest is
`a2040d14baa4a4efcae0d64d9bce38497a61b96b89f8d5c222f7063b52dc3758`.
Original exports are `group-04.5-survivor-retarget-open-export.json` and
`group-04.5-survivor-retarget-wall-export.json` under the report root. Their
export flag is explicit; committed frozen replay excludes expectation export.
The C adapter matches638 observed commits exactly; full engine route/cadence
parity is still NUM-02.3/E2E, not inferred from those local comparisons.

The survivor's old reserved slot remains unchanged until the real replacement.
The new singleton row starts with zero destination words at admission. The
owner refresh/layout centers its offsets to zero and publishes the new point;
subsequent decisions/commits use that point. Both group destructors see count0.
Final owner958 has live1/allocation6, preserving the survivor's individual path
and releasing the retired actor's path plus all group paths. All canonical,
task/payload free-list, Unit/Move reference, user/internal queue and owner-list
assertions run through final reclamation and visual idle. Existing mode1 task
chronology is checked independently: old prefix through point, cancellation
`d0144,d0162`, then a complete new task chain. Only the new order receives the
survivor's natural arrival.

OpenRealm's server-frame regression
`group_survivor_reorder_after_member_reuse_reaches_new_goal` covers both retired
roles: move a selected pair, Stop/free/reuse one edict, preserve the survivor's
old slot/cohort, issue its replacement point order, clear cohort ownership and
reach idle at the new goal with an empty queued-order FIFO. It uses
`globals.RunFrame` with the normal monster think lifecycle and asserts both
units actually moved before mutation. The existing Move implementation already
honors these ownership/destination boundaries, so no speculative production
change is needed for this composition. Its inactive goal cache remains retained
on stand, consistent with retail Stop retaining the original goal. Public active
order queries are separate: ORDER-01.4 explicitly owns the newly identified
`GetUnitCurrentOrder`/historical issued-ID distinction.

Ghidra `game.dll` is saved with257 names,14 partial layouts,90 verified fields
and34 instruction-backed x86 prototypes. Five additional ABIs cover the
request, advance, route preparation, point/target assembly and accelerated-lane
selection; readback is `group-04.5-ghidra-survivor-retarget-types.json`.
The only direct point/target setter xref is request assembly16bdfe, and the
refresh helper's only direct caller is1697a0. Actual replacement evidence
therefore follows the complete admission/new-request producer instead of
pretending that invoking either helper proves ordinary runtime retargeting.

Scope is point replacement into a new group after controlled completion/reuse.
Same-group moving-target refresh remains TARGET-02.1/02.2 and target-speed policy remains GROUP-03.2; the actual
UI/network/RemoveUnit caller graph and replacement actor Unit binding remain
BASE-01/03.1. No new live witness is claimed. The original wide callback
acceptance still requires GROUP-04.6's engine member storage/flags/shared
parameters/phase integration; closing04.5 does not close that leaf.

Fresh validation is `group-04.5-survivor-retarget-frozen-corpus/corpus-results.json`:120/120 declared outcomes, with all recorded source hashes still matching at completion. `LD_LIBRARY_PATH=/tmp/wc3-sdl2-build make -j8 test` passed78 Python tool tests and36720/36720 assertions in2127 tests for each RoC/TFT schema (`/tmp/wc3-group-04.5-survivor-retarget-full-suite.log`). Targeted movement validation has1514/1514 assertions in161 tests per schema; only `WC3_PATTERN='wc3_movement*'` selects that suite. Earlier exact/leading-wildcard patterns selected no tests and are not validation. The first full-frame fixture omitted monster think; after supplying the ordinary lifecycle, both roles moved and arrived. GROUP-04.5 is closed within the point-replacement scope; ORDER-01.4 owns the public active-order query discrepancy.
