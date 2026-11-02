# Crypt Lord Locust Swarm — Patches 5–8 (controller, combat, lifecycle and presentation)

## Source and contract

`AUls` is a self-target ultimate. The active AbilityData row supplies `UnitID`,
`Dur`, `Area`, and `DataA`–`DataE`; the reference Warcraft data uses `uloc`,
30 seconds, 800 range, 20 locusts, 0.2-second release interval, seven attackers
per target, a 0.75 healing factor and a 20-health return threshold. **Gameplay
reads the concrete row**, so custom map values override those stock numbers.
`AOls` (Voodoo Spirits) now dispatches through `CAbilityLocustSwarm` too, but
reads its **own** `UnitID`, `Dur`, `Area`, `targs` and `DataA`–`DataE` values;
it must not inherit stock AUls counts, targeting or healing parameters.
The swarm applies the authored ability target-category mask alongside the
locust weapon mask; it intentionally does not exclude ordinary physical hits
merely because the victim is spell immune.

## Release lifecycle

`CAbilityLocustSwarm` is now an ordinary non-channelled no-target cast.
The ability validates presence of a summoned `UnitID`, positive `Dur`, positive
`DataA` and positive `DataB` before a committed cast. On execution it creates
one invisible `locust_swarm_think` controller, which retains the caster's
`spawn_time` identity and an authored release schedule. The first release occurs immediately on the next simulation think, followed
by interval-spaced releases. This first-release policy remains provisional
until verified against retail. Each release uses the current caster location and the existing summon helper
`S_SummonAbilityAt` (food, spawn and ownership events). Each summoned unit
gets a `locustMember_t` with the controller pointer, its spawn generation and
its release index. Membership is distinct from the caster's generic `owner`
field. Summons have **no individual `BTLF` timed-life status**: an independent
status expiry would trigger ordinary `unit_die` while `locust_member_think`
replaces the unit's corpse/death-animation thinker, leaving a stationary corpse.
The controller owns all member lifetimes. At expiry, members with no damage
credit are immediately hidden and queued for deferred removal; members with
credit fly back to heal before disappearing, bounded by the recall grace
period. Recasting, teleporting, caster death and stale owner identities remove
all members regardless of remaining credit. A locust naturally killed by
combat still generates its normal death events, then its custom thinker queues
removal so no frozen model is left. Deferred removal preserves same-frame
simulation safety and releases normal unit/game resources. The controller does **not** cancel when the Crypt Lord moves
or attacks. It catches up on elapsed simulation deadlines after a delayed tick,
with a per-tick release budget of 32 and a protective ceiling of 256 units
for malformed custom `DataA`. A release slot is spent even if a pool/spawn
allocation fails, preventing infinite retry loops.

## Saved state

The typed pools use component IDs 32 `LOCUST_SWARM` and 33 `LOCUST_MEMBER`;
ID 31 is Impale and ID 34 is the independent Impale lift. The save version
is 135. The component schemas serialize authored schedules, target identities,
attack deadlines, damage credit and recall phase/deadline. Controller and
member thinkers are registered for save/load. The controller is game-owned
and invisible (`SVF_NOCLIENT`), not a client-side simulation or a Warcraft
JASS channel.

## Remaining phases

Patches 5–8 implement release, target acquisition, authored attacker caps,
ordinary physical attack damage, damage-credit healing, expiry recall, caster
death/teleport cleanup, recast replacement, the `AOls` Voodoo Spirits alias,
and optional model-authored Walk animation and looping ability sound.

The member thinker replaces `monster_think`, so ordinary Move/Attack order
callbacks cannot move a locust. Members instead steer directly using the
summoned unit's authored MoveSpeed: when idle they roam continuously through a
**wide, changing-radius cloud** around the moving caster, rather than fixed
points on a tight 100-unit orbit. Independent, deterministic phases and
incommensurate heading/radial oscillations use the authored `Area` as a limit.
Exact retail paths remain provisional. They chase eligible enemies before
physical hits and fly back with healing credit. The authoritative steering updates both `s.origin2` and `s.origin`,
plus height and linkage, for networked client presentation. The authored
`Walk` sequence advances in the member thinker if present; custom units
without that animation still move normally. The attacker occupancy cap is
checked on *acquisition*, not against a member's own valid reservation.
It does not create a separate locust missile: attacks apply through the shared
physical attack resolver. The controller resolves `SoundLooping` through the
generic ability sound path. Whether retail uses a separate `LocustMissile.mdx`
visual, exact orbit path, hit timing, recall grace and heal-threshold ordering
still needs comparison against retail. Missing art must not suppress simulation.

## Regression fixtures

`game/tests/t_locust_swarm.c` uses a deliberately non-stock `AUls` row with
`DataA=3`, `DataB=0.25`, `Dur=5`, `UnitID=hfoo`, a reduced target cap and
custom return fields. It tests delayed release, caster movement, catch-up,
identity cleanup, and save/load midway through release. The fixture is
synthetic and does not require the user's Warcraft archives.

Patch 8 adds alias-data and optional Walk-animation tests. Patch 7 adds tests
for expiry recall and credited healing, caster death, spell teleport,
successful recast and mid-recall save/load. Patch 6 tests attacker reservations,
normal physical damage and return healing, invulnerable/allied target rejection
and a member mid-flight save/load round-trip. These fixtures are synthetic and
do not require Warcraft archives. Retail orbit and missile presentation remain
visual verification gaps.

## Movement fix after Patch 8

The source shipped with Patches 5–8 overrode `monster_think` but issued
normal attack/return orders. Those orders did not have their normal movement
callbacks stepped, and seeking members had no idle movement at all. The
follow-up fix gives `locust_member_think` authoritative flight steering and
explicit Walk animation advancement, without adding a save field or any
client-specific gameplay branch. Synthetic tests cover idle/caster-follow
movement, target pursuit at a full reservation cap, and return healing.
The exact retail orbit paths remain unverified.

## Retail screenshot follow-up: idle flight and expiry

Two comparison screenshots show locusts spread broadly over the nearby area,
with members still travelling even without visible targets. The idle steering
now changes each member's radius and heading continuously; it is a visual
approximation, not a recovered retail AI formula. A second defect had allowed
`BTLF` to kill locusts into the default Death/corpse lifecycle, despite their
custom thinker not advancing that lifecycle. Locust lifetime and final removal
are exclusively controller-owned, so the swarm leaves no stationary corpses.
Expiry preserves return healing until arrival or the existing eight-second
upper bound, then forcibly removes remaining members. This fixes cleanup for
`AUls` and `AOls` without new save fields or client-side gameplay code.
