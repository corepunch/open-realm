# Spirit Walker (TFT)

Resistant Skin was added to Spirit Walkers after the 1.07 TFT baseline; see
[Warcraft III data version history](data-version-history.md).

## Data contract

The standard TFT unit rows are `ospm` (ethereal) and `ospw` (corporeal), both
using the SpiritWalker model and both listing `Acpf,Aspl,Adcn,Aast,Aion,ACsk`.
The producer's authored `Trains` rawcode determines which form a newly trained
unit starts in; runtime preserves that rawcode. `Acpf` turns `ospm` into
`ospw`; the reciprocal `Aetf` turns `ospw` into `ospm`. Both are no-target
spells with no mana cost and authored 30-second cooldowns. On entering Ethereal,
the `Aetf` cooldown is shown on the newly visible `Acpf` button; returning to
Corporeal clears the `Acpf` cooldown so the newly visible `Aetf` button is ready.
`Dur=0.7` is
an authored gameplay field; the MDX `Spell Morph` clip is 1.267 seconds. The
exact retail frame at which combat state changes remains to be measured.

## Runtime implementation

`CAbilitySpiritWalkerForm` uses `G_TransformUnitType` to retain the same edict,
JASS handle, ownership, buffs, food, control group, and upgrades. The existing
`Acpf` unit ability becomes an `Aetf` button when the current class is `ospw`.
Entering Ethereal starts the authored `Aetf` cooldown. Returning to Corporeal
does not start a cooldown on `Acpf`. Animation playback uses the shared-model
`Spell Morph` sequence.

An `ospm` unit is intrinsically ethereal even without a timed status buff. The
shared status-state query reports ethereal for this form **in addition to** any
separate Banish status, so dispelling Banish cannot grant physical vulnerability.
Attack and spell damage, healing and targeting continue to use the shared
ethereal combat checks. `ACsk` uses the existing Resistant Skin behavior.

`Rowt` tier 1 unlocks Disenchant (`Adcn`; legacy `Adch` for custom maps); tier
2 unlocks Ancestral Spirit (`Aast`). Both form rows retain their abilities
through research, while server-side command admission rejects unresearched
casts. The rank HUD is provided by the assumed merged base patch.

Ancestral Spirit revives the original edict for owned, raisable non-Hero
`otau`, `ospm`, or `ospw` corpses. It restores `DataA` of max health and
`DataB` of max mana; no eligible corpse means no cost/cooldown. The active TFT
Disenchant is `Adcn` (100 mana, 650 range, 250 area, 250 summoned-unit damage),
which uses shared dispel handling instead of the older `Adch` row.

## Verification and outstanding parity

The test suite contains focused reciprocal-form/cooldown and Ancestral Spirit
revival coverage. Run engine tests and retail comparisons externally; no local
build or tests were performed while authoring this patch.

Open comparisons: precise in-animation morph switch frame, potential order
interruptions, full MDX emitter/attachment parameters, Disenchant `Adcn` target
filter/ward handling and graphical fidelity, and Spirit Link lightning endpoints.
Numerical research effects and production buildings always use the active
archive Unit/Upgrade data rather than values hardcoded in gameplay.
