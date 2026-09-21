# Corpse Lifecycle, Cannibalize, and Raise Dead

## Contract

OpenRealm keeps corpse existence in authoritative unit state. `UnitData.deathType`
controls the stock authored policy: bit 0 means the dead unit may be raised and bit 1
means its remains use a decay lifetime. Runtime lifecycle flags may further remove a
corpse from eligibility (`AI_CORPSE_UNRAISABLE`) or temporarily reserve it for an
active consumer (`AI_CORPSE_RESERVED`).

A corpse is usable only while the edict is still live, is a dead monster, has the
authored raise bit, and is neither unraisable nor reserved. Ability-specific target
masks are then applied by `S_SpellAllowsCorpseTarget`; corpse checks deliberately do
not reuse the living-target spell-immunity/invisibility gate.

## Decay timing

The death animation remains separate from corpse timing. When that animation finishes:

```text
ordinary unit with decay bit
  -> Misc.DecayTime flesh phase
  -> Misc.BoneDecayTime bone/remains phase
  -> remove edict

structure with decay bit
  -> Misc.StructureDecayTime
  -> remove edict

Hero
  -> Misc.DissipateTime
  -> persistent Hero revival state

AI_CORPSE_NO_DECAY or no authored decay bit
  -> remove after the death presentation
```

`DecayTime`, `BoneDecayTime`, `StructureDecayTime`, and `DissipateTime` are already
loaded from the active Warcraft misc data / map overrides. The simulation timers own
corpse existence; a model does not need a separate flesh/bone animation sequence.
`AI_CORPSE_RESERVED` suspends either ordinary decay phase while a consumer owns the
corpse. The flag rides in persisted `aiflags`, so no new save pointer is required.

## Cannibalize (`Acan`)

Cannibalize is a no-target channel. It searches `DataB` for the nearest non-Hero corpse
allowed by the ability's authored Targets Allowed field. Stored Meat Wagon corpses are measured
at the holder's current position; if the caster must approach one, movement targets the Wagon
while the consuming thinker continues to own the actual hidden corpse entity. A successful cast reserves the
corpse for the entire channel by setting `AI_CORPSE_RESERVED` and installing the
ability-alias targeting status used by the Warsmash behavior definition. Other corpse
consumers therefore cannot claim the same remains.

`DataA` is HP restored per second. OpenRealm applies that amount continuously on the
simulation cadence while the shared channel remains valid. The channel ends when:

- the caster reaches maximum life;
- the authored ability duration expires; or
- normal channel rules interrupt the caster (movement, stun, death, replacement order).

Every started Cannibalize channel consumes its reserved corpse when the channel ends,
including interruption. No valid corpse rejects before spell commitment and reports the
Warcraft `Cantfindcorpse` CommandStrings key. Presentation remains data-driven through
the common spell/effect paths.

## Raise Dead (`Arai`, `ACrd`, `AIrd`)

Raise Dead is an automatic corpse-target spell. Retail documentation states that it
preserves stronger corpses by choosing a lower-ranked eligible corpse first (for example,
a critter before a Ghoul); OpenRealm uses the same `UnitBalance.level` rank already used
for the inverse "most powerful corpse" ordering of Resurrection/Animate Dead, then distance
as the equal-rank tie-breaker. This intentionally differs from current Warsmash, which
selects the nearest corpse. `Arai` retains autocast; `AIrd` uses the same implementation
for the item alias.

On resolution, the ability reads the two stock Object Editor summon groups directly:

```text
DataA times DataC unit type
DataB times DataD unit type
```

This supports the stock two-Skeleton layout and data/upgrade variants without unit-ID
special cases. Each result:

- spawns at the consumed corpse location and facing;
- belongs to the caster's player;
- records `summon_ability` so `UNIT_TYPE_SUMMONED` and shared summon consumers identify it;
- uses the common `BTLF` timed-life clock for engine lifetime semantics;
- also receives the ability-authored first `BuffID` for Warcraft buff identity/presentation;
- emits the authored ability `Effect` presentation at the created unit.

The installed TFT upgrade table identifies `Rusl` (Skeletal Longevity) as effect
`rrai`, base `15`, with the description “undead skeleton life span.” Raise Dead adds
that authored researched effect to its summon duration through the generic player-tech
lookup. `Rusm` (Skeletal Mastery) remains on the existing generic `rlev` path, which
raises `Arai` to its authored second level.

`UnitID`/`Raiu` is not a summon-output field for this ability family: Warcraft metadata
labels it **Unit Type for Limit Check**. Stock Raise Dead limits that authored type to 25
living summoned units per player; after a cast crosses the cap, OpenRealm retires the
oldest matching summon first (stable `spawn_time`, then entity-number tie-break). Clearing
`Raiu` in a custom ability disables this check. Skeletal Mastery's second summon type does
not implicitly join the limit unless authored as the `Raiu` type.

The corpse is removed after the summon groups are created. If no valid corpse exists,
the cast rejects before cost/cooldown commitment with `Cantfindcorpse`.


## Graveyard Create Corpse (`Agyd`)

`Agyd` is a passive update ability. Stock Blizzard documentation says a Graveyard produces
one Ghoul corpse every 15 seconds and can maintain up to five corpses within 25 displayed
game units. The object-data fields expose that contract directly: `Cool` is the production
interval, `DataA`/`Gyd1` the corpse cap, `DataB`/`Gyd2` **Radius of Gravestones** (spawn
radius), `DataC`/`Gyd3` **Radius of Corpses** (the area used for the cap), and `UnitID`/`Gydu`
the generated corpse type.

OpenRealm arms an ability-owned saveable thinker. On each `Cool` pulse it counts matching
dead `UnitID` corpses inside `DataC`; when below `DataA`, it creates one dead, non-selectable
corpse on the deterministic placement ring defined by `DataB` and enters the normal
corpse-decay lifecycle. Stored corpses are counted at their current holder's position rather
than their hidden pre-load origin. Generated corpses therefore use the same authored
raisability and map decay constants as combat corpses and can be consumed by Raise Dead or
Cannibalize. Exact retail random/grave-marker placement within `Gyd2` remains presentation/
placement fidelity work.

## Shared / deferred work

The high-confidence corpse-cargo follow-up resolves several earlier ambiguities:

- Meat Wagon cargo keeps the actual corpse edict hidden/paused, but its **effective gameplay
  position is the current holder position**. Cannibalize range, Raise Dead range/spawn
  position, and Graveyard nearby-corpse counting therefore follow a moving Wagon rather
  than the corpse's stale pre-load coordinates.
- Dropping a corpse that was already in the bone/remains phase restarts the map-authored
  `BoneDecayTime`, matching Warcraft's cargo/decay behavior. The generic cargo death path
  already spills stored corpses when the Wagon is destroyed.
- `Agyd` now treats `Gyd2` and `Gyd3` as distinct authored radii, and Raise Dead honors its
  authored `Raiu` limit-check unit type with the stock 25-per-player oldest-first cap.

Remaining work is deliberately limited to behavior that still lacks an exact contract:

- Blizzard's classic documentation confirms that a group Cannibalize order assigns corpses
  to the most injured units first and that ordinary group orders do not interrupt units
  already Cannibalizing. The exact injury comparator/tie order and the generic distinction
  between group orders and an explicit single-unit cancellation still need a shared
  multi-selection/group-order policy; do not add an `Acan` special case to generic orders.
- Cargo evidence establishes a bone-phase timer restart on unload. Exact behavior for a
  corpse picked up during the brief flesh phase, plus exact multi-corpse unload facing and
  placement, remains unresolved; OpenRealm keeps the generic unstuck placement path.
- Corpse state now requests the standard model sequences `Decay Flesh` and `Decay Bone` at
  the matching simulation phase. Missing sequences leave the prior/final death pose in place,
  so lifetime correctness remains independent of model authoring. Exact retail playback-rate,
  sequence-duration scaling, and malformed/custom-model fallback details remain presentation work.

## Verification

Focused tests are provided for the new contracts. They are intended to be run by the
caller after applying this patch:

```sh
make test-wc3-engine WC3_PATTERN='wc3_unit.*corpse*'
make test-wc3-engine WC3_PATTERN='wc3_ability_lifecycle.cannibalize*'
make test-wc3-engine WC3_PATTERN='wc3_spell.raise_dead*'
make test-wc3-engine WC3_PATTERN='wc3_spell.graveyard*'
make test-wc3-engine WC3_PATTERN='wc3_spell.*corpse*'
make test-wc3-engine WC3_PATTERN='wc3_spell.raise_dead*limit*'
```

The patch was prepared without running compilation or tests locally, per the handoff
request.
