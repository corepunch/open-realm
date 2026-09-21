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
allowed by the ability's authored Targets Allowed field. A successful cast reserves the
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

The corpse is removed after the summon groups are created. If no valid corpse exists,
the cast rejects before cost/cooldown commitment with `Cantfindcorpse`.


## Graveyard Create Corpse (`Agyd`)

`Agyd` is a passive update ability. Stock Blizzard documentation says a Graveyard produces
one Ghoul corpse every 15 seconds and can maintain up to five corpses within 25 displayed
game units. The object-data fields expose that contract directly: `Cool` is the production
interval, `DataA`/`Gyd1` the corpse cap, `DataC`/`Gyd3` the corpse radius, and `UnitID`/`Gydu`
the generated corpse type.

OpenRealm arms an ability-owned saveable thinker. On each `Cool` pulse it counts matching
dead `UnitID` corpses inside `DataC`; when below `DataA`, it creates one dead, non-selectable
corpse inside that radius and enters the normal corpse-decay lifecycle. Generated corpses
therefore use the same authored raisability and map decay constants as combat corpses and
can be consumed by Raise Dead or Cannibalize. Exact retail random placement among
gravestones remains presentation/placement fidelity work.

## Shared / deferred work

This patch intentionally does not guess behavior where the current evidence or engine
infrastructure is incomplete:

- Meat Wagon cargo now uses the actual corpse edicts in the shared cargo slots. Installed
  TFT data identifies hidden `Sch2`/`Amtc` as the Cargo Hold with `DataA=8`; `Amel` is the
  authored corpse-load command (`ground,dead,nonhero`, range 100) and `Amed` drops all
  stored corpses. Loading hides and pauses the corpse while retaining its decay move/timer;
  unloading uses the existing generic unstuck placement. Raise Dead and Cannibalize can
  consume stored corpses directly, and Exhume creates directly into these slots.
- The installed data does not establish whether retail restarts or resumes a partially
  elapsed decay phase after unloading, the exact unload facing/placement rule, or the
  outcome when a Meat Wagon is removed while carrying corpses. OpenRealm currently
  preserves the saved decay state, uses shared unstuck placement, and follows existing
  cargo-removal ejection behavior for those unresolved cases.
- Blizzard's classic documentation confirms that a group Cannibalize order assigns
  corpses to the most injured units first and that ordinary group orders do not interrupt
  units already Cannibalizing. OpenRealm still needs a shared multi-selection/group-order
  policy to represent those semantics without an ability-local command hack.
- Blizzard documents a 25 Skeleton Warrior per-player cap; modern documentation also says
  excess Raise Dead skeletons remove the oldest first. Exact Skeletal Mage/combined-cap
  treatment is not first-party-clear, and OpenRealm has no shared summon-cap owner yet, so
  this remains deferred rather than guessing.
- Warcraft unit models commonly expose distinct `Decay Flesh` and `Decay Bone` sequences,
  but the exact retail sequence-transition/fallback contract is not established by the
  gameplay documentation. Exact model presentation remains separate from the authoritative
  lifetime state machine.

## Verification

Focused tests are provided for the new contracts. They are intended to be run by the
caller after applying this patch:

```sh
make test-wc3-engine WC3_PATTERN='wc3_unit.*corpse*'
make test-wc3-engine WC3_PATTERN='wc3_ability_lifecycle.cannibalize*'
make test-wc3-engine WC3_PATTERN='wc3_spell.raise_dead*'
make test-wc3-engine WC3_PATTERN='wc3_spell.graveyard*'
```

The patch was prepared without running compilation or tests locally, per the handoff
request.
