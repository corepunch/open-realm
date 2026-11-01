# Night Elf Sentinel (`Resc` / `Aesn`)

Sentinel research (`Resc`) at the Ancient of War unlocks Huntress `Aesn` when
listed in her `Upgrades Used` data. The research gate is shared with the normal
tech system and responds to completion and `SetPlayerTechResearched`.

- A Huntress targets a **living destructable tree** (not an arbitrary point).
- On success she places an owl and expends her **one Sentinel charge**. Her
  `Aesn` ability is removed, including from the command card. Re-granting the
  ability explicitly by JASS is supported, but expiry/dispel does not refund it.
- The owl retains the caster's player ownership, even after caster death, and
  uses `Aesn` **DataB** (Hovering Sight Radius) for day/night vision and
  true-sight radius and **DataC** (Hovering Height) for its perch height.
  Stock `Dur` is zero, so it remains until its tree is destroyed or damaged.
  Shared-vision rules apply. The owl reveals its authored circular radius
  without terrain occlusion from the tree it is perched on.
- The owl is unselectable; presentation uses the standard Owl model. Its
  thinker cleans up after expiry, destruction/replacement of the attachment
  tree, **damage to that tree**, or a spell-area dispel. The owl does not
  become an attackable combat unit.
- The thinker callback, tree reference and incarnation, placement health,
  ownership and expiry are part of the existing saved-edict contract. No
  new fields or external timers are required.

## Focused validation

Research then cast on a tree; confirm existing and newly trained Huntresses can
use exactly one owl each. Confirm a second cast is rejected, research removal
blocks new charges, and granting research again does not restore used charges.
Verify fog reveal, invisible enemy detection and allied shared vision, night
sight, tree hit/death, duration expiry, dispel, save/load mid-owl and caster
death. Also reject terrain clicks, non-tree objects and dead trees.

## Remaining parity gaps

The initial owl uses a stationary model rather than the retail travelling
Sentinel missile (`SentinelMissile.mdx`). Direct single-target Purge dispelling
is not yet wired; the shared area Dispel/Disenchant/Devour Magic path is. The
one-use rule is implemented with an authored ability removal and can be
explicitly overridden by `UnitAddAbility` on custom maps.
