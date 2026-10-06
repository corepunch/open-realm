# Neutral Creep Sleep

`skills/s_creep_sleep.c` owns Warcraft III's **natural neutral-creep sleep** through
`CAbilityCreepSleep`, registered as `ACsp` with `AB_PASSIVE | AB_INNATE`.
The TFT registry (`games/warcraft-3/tft-ability-classes.txt`) identifies its parent
as `Asla` / `CAbilitySleepAlways`. The concrete procedure delegates shared wake,
interruption, removal, and acquisition messages directly to that parent procedure.
Dreadlord Sleep (`AUsl` / `BUsL`) continues to use the independent timed-status system.

## Ability ownership

`AB_INNATE` marks unit-data behaviors which receive lifecycle messages without
requiring an explicit ability-list or command-card slot. `InitAbilities` builds
concrete `abilityitem_t` entries once; `S_UnitAbilityEvent` sends each procedure
its rawcode and registry row through `abilityCall_t`.

| Entry point | Message | Ability-owned decision |
|---|---|---|
| `SP_SpawnUnit` (including type rebind) | `A_UNIT_INIT` | Seed mutable eligibility from `UnitData.canSleep` |
| `ai_stand`, before neutral-owner early return | `A_IDLE` | Enter sleep if eligible at night |
| `unit_setmove`, before replacing a distinct move | `A_MOVE_LEAVE` | Clear sleep state and remove overhead effects |
| Positive post-mitigation `T_Damage` | `A_DAMAGED` | Wake before combat response |
| `G_FreeEdict`, before clearing the edict | `A_UNIT_REMOVE` | Destroy owned effects before slot reuse |
| AI target filter | `A_NO_ACQUIRE` | Suppress automatic targeting while asleep |

Notifications visit every registered innate owner. Idle/acquisition queries stop
when an owner returns true. The private sleep move carries `CAbilityCreepSleep`
as its procedure and owns the dawn-check think callback. Natural-sleep rules now
live in the ability rather than AI, spawn, or damage dispatch. `UnitAddSleep` and `UnitWakeUp` resolve
`ACsp` through the registry and send `A_ENABLE`/`A_DISABLE` and `A_CANCEL` respectively.
The read-only JASS queries stay in the same ability module.

The original `g_creep_sleep.c` implementation (commit `98d73350`) bypassed the
registry. Its removal path left a live overhead effect after `G_FreeEdict`; the
refactor reproduces that failure in the effect lifecycle test and fixes it through
`A_UNIT_REMOVE`.

## Authoritative data

`UnitData.canSleep` (`usle`) is already parsed from Warcraft unit data and is the
authored default for whether a unit may use natural night sleep.  Runtime spawn
initialization copies that value into `edict_s.sleep.can_sleep`; `UnitAddSleep`
may then change the runtime value for neutral units without mutating the shared
SLK row.

Asset inspection of the retail data supplied for this work confirms that the
Ogre family (`noga`, `nogl`, `nogm`, `nogn`, `nomg`) has `canSleep = 1`, and the
corresponding Ogre MDX models contain a looping `Sleep` sequence.  The runtime
therefore requests the model's authored `Sleep` animation instead of treating
natural sleep as stun or freezing a `Stand` frame.

Warcraft also authors the yellow sleeping `Zzz` through the hidden `Creep Sleep`
ability (`ACsp`) TARGET presentation. Retail/modding references identify its
model as `Abilities\Spells\Other\CreepSleep\CreepSleepTarget.mdl`/`.mdx` and
attach it to the sleeping unit's `overhead` point. OpenRealm first resolves
`ACsp` through the existing ability-effect data path so map/test overrides remain
authoritative. Some retail data sets do not expose a `TargetArt` entry for this
hidden ability through the loaded `*AbilityFunc.txt` metadata; when that lookup
is empty, natural creep sleep falls back specifically to the canonical retail
`Abilities\Spells\Other\CreepSleep\CreepSleepTarget.mdx` model.

TODO: The current overlay animation renders blue, but the retail `Zzz` sleep
presentation should render yellow. Trace the authoritative material/team-colour
data before changing the effect asset or renderer path.

## Runtime contract

Natural automatic sleep currently applies only to `PLAYER_NEUTRAL_AGGRESSIVE`.
An eligible creep enters sleep when it is idle at night and all of these are
true:

- the unit is alive;
- its mutable `can_sleep` flag is true;
- it is not currently affecting combat;
- `PLAYER_STATE_NO_CREEP_SLEEP` (playerstate 25) is zero for Neutral Hostile.

The existing authoritative time-of-day clock owns the night decision through
`G_IsNight()`.  There is no second creep-only clock.

While naturally sleeping:

- the unit's active move requests the `Sleep` model sequence;
- one persistent `ACsp` TARGET effect is attached at `overhead`; the existing
  target-effect edict follows the unit, remains non-selectable, and owns its
  own model animation/lifetime;
- ordinary automatic enemy acquisition ignores it, so player units do not
  auto-attack a camp merely because it is in acquisition range;
- a direct attack order remains valid;
- positive damage wakes it before normal combat/counterattack handling;
- dawn wakes it;
- `UnitWakeUp` wakes it;
- `UnitAddSleep(unit, false)` disables future natural sleep and wakes it now.

Any other runtime behavior that replaces the special creep-sleep move also
leaves natural sleep through the same cleanup path. This clears the sleeping
flag and destroys the `ACsp` overlay, preventing an ordered/moving unit from
remaining logically asleep or retaining an orphaned `Zzz` effect.

## JASS natives

The following natives now consume the natural-sleep state:

- `UnitAddSleep(unit, boolean)` changes neutral-unit night-sleep eligibility;
- `UnitCanSleep(unit)` reports that mutable neutral-unit eligibility;
- `UnitIsSleeping(unit)` reports **natural creep sleep only**;
- `UnitWakeUp(unit)` wakes **natural creep sleep only**;
- `UnitCanSleepPerm(unit)` reports whether the unit currently owns the authored
  `Sleep Always` ability (`Asla`).

`UnitIsSleeping` and `UnitWakeUp` intentionally do not inspect or remove the
Dreadlord Sleep buff (`BUsL`).  Spell sleep still wakes from damage through its
existing timed-status cleanup.

`UnitAddSleepPerm` remains conservative.  Warcraft's editor metadata ties this
native to `Sleep Always` (`Asla`), whose `Sleep Once` and `Allow On Any Player
Slot` activation fields still need an authoritative behavioral contract. Aliasing
it to ordinary night sleep would make those semantics wrong, so the native stays
a placeholder. `CAbilitySleepAlways` currently supplies shared exit behavior to
`ACsp`; `Asla` remains unregistered until its activation rules are implemented.

## Player-wide creep sleep switch

Warcraft's `EnableCreepSleepBJ` writes
`PLAYER_STATE_NO_CREEP_SLEEP` on `PLAYER_NEUTRAL_AGGRESSIVE`.  OpenRealm already
allows that playerstate index through the generic 32-slot player-state storage;
natural sleep now consumes it when deciding whether an idle hostile creep may
enter sleep.

The Blizzard.j disable path also explicitly wakes Neutral Hostile units after
setting the state.  That wake-all behavior is supplied by the script's
`WakePlayerUnits` loop and `UnitWakeUp`; changing the playerstate alone only
prevents new natural sleep.

## Save/load

The mutable natural-sleep fields live on `edict_s`, so they cross the ordinary
raw-edict save boundary together with the unit.  Save format version 15 marks the
`edict_t` layout change.  `currentmove` already uses the existing `F_MMOVE`
relocation contract, so a unit saved while using the static creep-sleep move can
restore that move without serializing an address. This ownership refactor changes
neither the edict layout nor its field schema; saves use the existing same-build
move relocation contract. The round-trip test verifies that Stop after loading
clears sleep and destroys its restored overhead effect.

## Deliberately unresolved

The patch does **not** guess the following retail details:

- camp-level wake propagation when one member is provoked;
- the exact guard/camp association and radius used for propagation;
- wake behavior caused by nearby building construction;
- wake behavior caused by using a guarded neutral building;
- `ACsp`-owned audio details such as the exact snore/loop sound lifecycle;
- `Sleep Always` (`Asla`) state transitions and `UnitAddSleepPerm`.

Those should be implemented only after the corresponding retail data or
behavioral contract is traced.  In particular, a guessed proximity radius would
make campaign/melee camp behavior less reliable than leaving propagation as a
known gap.

## Verification

Focused in-engine coverage is under `wc3_unit`, `wc3_combat`, `wc3_effects`,
and the `wc3_save` round trip.
After building the test binary, useful filters are:

```bash
make openwarcraft3-tests test-assets
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_unit.*'
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_effects.*'
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_combat.*'
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_save.*'
python3 tools/wc3_ability_class_audit.py --format=coverage
```

For a retail-data manual check, use a map with one of the Ogre rows above,
advance the authoritative clock across dusk/dawn, and verify that the model
enters/leaves its `Sleep` sequence, the `ACsp` `Zzz` TARGET effect appears over
the sleeper and disappears on wake, and direct damage wakes it.

## See also

- [Ability ownership](ability-implementation.md#ability-owned-orders-and-persistent-behavior)
- [Time of Day](time-of-day.md)
- [JASS Native Coverage](../../../games/warcraft-3/jass-native-coverage.md)
- [Attack and Damage](attack-damage.md)
- [Ability, Buff, And Item Presentation Effects](ability-and-item-effects.md)
- [Save / Load](save-load.md)
