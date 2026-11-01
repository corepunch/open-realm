# Firelord: Lava Spawn, Incinerate and Volcano

## Data source and implementation scope

Active TFT `AbilityData.slk` and the 2026-11-01 read-only Firelord research report
are the numerical contract. The checked-in 2026-10-29 OpenRealm source, with the
pending combined changes and subsequent Tinker/Alchemist patches, is the baseline.
All behavior is owned in the Warcraft III game module; no universal client changes.

## Lava Spawn (`ANlm`)

`DataA=1` summons one of `nlv1`/`nlv2`/`nlv3` for `Dur=70` seconds.
`DataB=5` delays a split after `DataC=15` successful attacks. `DataD=.5`
restores up to half maximum HP on split; `DataE=12` extends timed life;
`DataF=3` limits descendant generations. All values are read from the
ability's learned rank, not hardcoded. Stock data differs from modern patches
that lower the attack threshold. A dedicated **idle/event-driven** controller
entity tracks the successful hit counter and generation and only ticks during a
pending split. It validates unit and caster spawn identities, including across
save/load, and uses normal `S_SummonAbilityAt` creation/ownership/events and
collision-safe placement. BTLF is the single expiry owner. Child inherits the
parent's remaining BTLF time plus the authored extension. A split in progress
is cancelled if the parent dies before completion; no phantom child is made.
Attack orders are suspended while splitting; the authoritative hit counter
continues only after the morph timer completes.

The Object Editor establishes the fields, but whether the parent receives an
additive .5 max-HP heal, whether the new unit starts at .5 max HP, attack/stand
locking during the five seconds, exact appearance darkening, and the maximum
interpretation of generation count require retail verification. The current
implementation is an explicit approximation in those details.

## Incinerate (`ANic` passive; `ANia` autocast)

`ANia` has a six-mana attack modifier. It is a toggle/autocast command; its
selected state must be on and the mana payment must succeed for the post-hit
mark and stacking damage to apply. No fallback to free `ANic` is permitted
when an owned `ANia` is disabled. The passive `ANic` path remains available
for maps that author that rawcode instead. The damage mark `BNic` lasts four
seconds, incrementing per landed hit. Delayed death damage uses `DataB/C`
within the inner ring and `DataD/E` within the outer ring, after `DataF`, with
one death explosion even if the victim dies from another source. The status
owns target art. Exact retail mana charge at launch versus impact remains
unverified; the game module currently commits it on confirmed hit.

## Volcano (`ANvc`)

The existing eight damage-wave logic is authoritative: `DataB` count and
`DataC` interval, full damage `DataE` within half `Area`, reduced damage by
`DataF` in the outer ring, structure factor `DataD`, and stun from `Dur` and
`HeroDur`. `DataA` is used **only for visual rock missiles**. New projectiles
use authored MissileArt/Missilespeed and independent impact visuals; they
never apply damage a second time. The channel's area effect and loop sound
remain owned by its thinker and are cleaned up on interruption or completion.
Already-launched visual rocks can finish their flight independently.
Precise random rock trajectories, whether individual rocks cause retail
impact damage, and exact audio cadence still require retail verification.

## Testing expectations

Regression scenarios include: attack launch vs actual hit, threshold-1 vs
threshold, kill during split delay, generation limit, timed-life inheritance,
no doubled life expiry, save during a pending split; `ANia` on/off and mana
starvation, `ANic` passive, death chain prevention; Volcano visual count,
no duplicate damage, interrupted channel cleanup and in-flight rock save.
These tests must use synthetic archive fixtures and be run by the user; no
build or local test execution was performed while preparing this change.
