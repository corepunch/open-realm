# Entangling Roots

`AEer` (Keeper of the Grove), `Aenr` (creep Entangling Roots), and `Aenw`
(Entangling Seaweed) share `CAbilityEntanglingRoots`. The runtime reads the
requested rawcode's own AbilityData row, so aliases retain their authored
duration, BuffID, targets, and `DataA` (`Eer1`) damage-per-second values.

## Runtime contract

A successful unit-target cast applies the authored timed buff for the normal or
Hero duration. `BEer` is the stock Entangling Roots movement/attack token. While
it remains active:

- `S_UnitCanTranslate` rejects translation;
- ordinary attacks and Attack Ground are suppressed;
- the target is not stunned or silenced and may cast otherwise legal spells;
- application cancels an already-active channel, which also covers an in-progress
  Mass Teleport channel;
- the status remembers the applying rawcode, rank, source edict/incarnation, and
  next damage pulse;
- `DataA`/`Eer1` is applied through `S_SpellDamage`, preserving spell immunity,
  Anti-Magic Shell handling, damage attribution, and normal death events.

The status scheduler uses one-second pulses because `Eer1` is authored as
damage per second and the existing Warcraft status-DPS path uses a deterministic
1000 ms interval. This is an OpenRealm scheduling convention; the exact retail
internal pulse granularity is not claimed as recovered behavior.

## Removal and persistence

Entangling Roots uses the shared timed-status lifecycle. Expiration or generic
dispel clears `BEer`; movement and attacks then become legal again without a
Roots-specific inverse flag. The existing status fields (`data`, `source`,
`source_spawn_time`, `rank`, `next_tick`) are already part of save/load, so no
new serialized or network state is required. Damage stops once the status slot
is gone.

The persistent target visual is owned by the active buff/status presentation,
not by a one-shot cast effect. OpenRealm resolves the target model from the
authored buff rawcode (`BEer` for the stock spell), attaches it at `origin`, and
keeps one persistent effect entity per status/target generation. For stock data
this resolves to the Entangling Roots target model.

`A_STATUS_REMOVE` destroys that status-owned effect, so normal expiration,
generic dispel, and death cleanup all retire the visual through the same status
lifecycle as the gameplay root. `A_STATUS_REFRESH` can recreate a missing
presentation effect without duplicating an existing one. The authoritative
state remains the timed status; the effect tag is presentation-only. Its save
mapping preserves cleanup ownership across load without making the visual an
authoritative gameplay status.

## Deliberately not generalized

Retail evidence confirms that Entangling Roots interrupts channels and is used
to stop Mass Teleport/Town Portal gameplay, but that does not establish a
universal ban on scripted relocation. Do not make `BEer` block arbitrary
`SetUnitPosition`-style movement or unrelated scripted teleports without further
evidence.

## Regression coverage

Tests cover registration as a unit-target spell, stock `BEer` target-art
resolution, authored DPS/source tracking, Hero duration and alias-specific data,
channel interruption, death cleanup, status-allocation failure, generic Dispel
Magic removal, movement restoration, and disarm behavior for both ordinary
attacks and Attack Ground while preserving normal attacks for inherently
immobile artillery.

## Verification

The regression coverage exercises the production order/status paths for normal attacks, Attack Ground, authored DPS, source attribution, Hero duration, alias-specific data, channel interruption, status removal, death cleanup, and allocation failure. The damage scheduler uses OpenRealm's deterministic 1000 ms DPS pulse convention; that interval is an engine convention, not a claim about retail Warcraft III's internal pulse granularity.

Recommended verification after applying this change:

```sh
make test
```

A focused in-engine test run may use the `wc3_spell.entangling_roots*` and
`wc3_combat.*roots*` patterns supported by the shared test runner. The unit test
verifies that stock `BEer` resolves target art containing
`EntanglingRootsTarget`; actual MDX animation/rendering still requires an
in-engine visual check.
