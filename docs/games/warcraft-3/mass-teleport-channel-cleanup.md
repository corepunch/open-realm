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
