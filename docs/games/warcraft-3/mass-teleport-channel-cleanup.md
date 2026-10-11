# Mass Teleport cancellation and recast cleanup

Mass Teleport uses a target-channel thinker, holding a snapshot of the caster
and destination incarnations plus the cast serial. Cancelling the channel
cleans up owned area effects and releases destination pause, but the retired
thinker may receive one more scheduled tick before `S_SpellEndChannel` frees it.

The cleanup path must not clear `target->paused` when a different live Mass
Teleport thinker already owns the same destination. This includes a recast on
the same building, and a reused caster with a different serial. Previous
pre-existing pause is preserved through the thinker's `wait` snapshot.

Regression coverage is in `mass_teleport_old_thinker_preserves_recast_destination_pause`.
The test deliberately simulates a second channel identity after cancellation
so cooldown policy does not obscure stale-cleanup behavior.

## Siphon Mana and Life Drain channel pulses

The shared drain thinker resolves caster and target incarnations and checks the
channel serial before each scheduled pulse. Life Drain must revalidate the
channel and living target after applying its health-damage component: the damage
can kill the target or invoke cancellation through gameplay events. A cancelled
or fatal health pulse must not proceed to its mana-transfer component. The
regression tests cover pre-pulse stun and lethal Life Drain.
