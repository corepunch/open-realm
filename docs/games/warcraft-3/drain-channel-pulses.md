# Siphon Mana and Life Drain channel pulses

The shared drain thinker validates the caster, target incarnation, range, and
channel serial before each scheduled pulse. Life Drain applies its health
component before its mana component, so it revalidates the channel and living
target after health damage. A fatal pulse or gameplay interruption ends the
thinker before it can transfer mana from a dead or stale target.

The lifecycle regressions cover a stun before a Siphon Mana pulse, a nonfatal
Life Drain pulse that proves authored mana transfer still occurs, and a lethal
Life Drain pulse with nonzero mana data that must leave the target's mana
unchanged.

## Siphon Mana excess mana

Siphon Mana's `CAbilityDrain` (including creep alias `ACsm`) transfers mana
into the caster's existing `mana.value` up to **2 × `mana.max_value`**, without
changing the maximum. `CAbilityDrainNeutral` (Life Drain) stays capped at the
normal maximum. The amount removed from an enemy is the amount the receiver
can accept; reaching the cap does not destroy extra target mana. Allied transfer
uses current mana, including excess, but is limited by the ally's normal cap.

`G_RunEntity` runs the general over-cap decay branch using `AHdr` `DataI`
(**3 mana/second** in installed 1.29 data) and stops at the ordinary maximum.
The Siphon-owned `S_ManaExcessDecayRate()` returns zero while a Siphon channel
is active; cancellation, expiry, movement or stun causes decay to resume.
`ACsm` has `DataI=0` and does not replace the Blood Mage row. A non-stock
`DataI=5` regression verifies that one 100 ms production tick removes 0.5
mana, proving this is not a hardcoded retail value. The 200% cap comes from
patch 1.17. Natural regeneration resumes only below normal max.

`compress_stat` saturates the 8-bit snapshot ratio at 255, while the selected
unit's existing current/max mana fields carry the real over-cap number. Hero
recalculation, temporary mana bonuses, nonhero mana research, and transforms
preserve existing over-cap mana. No extra edict field, save schema version,
network field or client feature is introduced: `mana.value` was already a float
in the persisted edict.

Regression fixtures use the real `AHdr` / `ANdr` cast and channel thinker paths;
they also cover the 200% ceiling, decay after cancellation, channel immunity,
cap changes and compressed HUD saturation. Do not use local retail MPQ files
in those tests.

Reference: [Blizzard patch 1.17](https://warcraft.wiki.gg/wiki/Warcraft_III/Patch_1.17),
[Blizzard's Blood Mage guide](https://classic.battle.net/war3/human/units/bloodmage.shtml).

## Life Drain visual ownership

Dark Ranger `ANdr` now adds chest-attached `Bdcl`/`Bdtl` effect models,
a linked ability lightning ribbon and the configured `DrainLoop` sound
when the shared drain thinker begins. The thinker's persisted `variation`
contains its unique lightning registry handle; cancellation and natural
completion remove that beam and both owned model effects without affecting
newer channel incarnations. Siphon Mana's mana-transfer/overcharge behaviour
is unchanged. See [Dark Ranger](dark-ranger.md) for asset mappings and
verification limitations.
