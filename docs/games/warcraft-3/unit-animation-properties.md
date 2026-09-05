# Warcraft III Unit Animation Properties And Transformation Forms

## Required Animation Names

Warcraft unit profile data may provide `animProps` (`uani`, Required Animation Names). OpenRealm copies that authored value into the per-unit `animation_props` set when `SP_SpawnUnit()` initializes a unit. `SetUnitAnimation` retains the logical animation request separately in `animation_request`; animation lookup then combines the request tags with the unit's active Required Animation Names.

`AddUnitAnimationProperties` mutates the active per-unit tag set and immediately reselects the retained logical animation family. This state is inline edict data, so ordinary WC3 save/load persists it with the unit record.

The stock Medivh model/data combination is unusual: the raven-form unit requests `alternateex`, while the shared model exposes `Alternate` sequences. Animation selection therefore falls back from a required `alternateex` tag to `alternate` only when no matching `AlternateEx` sequence exists. A genuine `AlternateEx` model remains distinct.

## Raven Form Orders

WC3 exposes two stock abilities that use the `ravenform` / `unravenform` order pair: Medivh Crow Form (`Amrf`) and Druid of the Talon Storm Crow Form (`Arav`). Each ability's object data owns its own transformation endpoints:

- AbilityData Data A (`DataA1`) is the base unit type;
- AbilityData `UnitID1` is the raven/alternate unit type.

The immediate orders `ravenform` and `unravenform` first choose the stock transform ability whose authored Data A / UnitID endpoints contain the current unit, then transform the existing edict between those two authored types. The edict/JASS handle is retained rather than replacing the unit with a new entity. Rebinding the type reruns the normal unit-data initialization, including model, movement layer, collision, attacks, authored Required Animation Names, and other presentation state, while preserving current health/mana ratios and temporary combat bonuses.

Preplaced alternate-form campaign units are valid transformation endpoints even if they did not pass through OpenRealm's runtime ability-add path before the map script issues `unravenform`. This is required by the Prologue campaign Medivh raven: the map starts with the alternate unit and later orders that same unit to return to the base form.

The current implementation performs these script-issued form changes immediately. After rebinding the type it also snaps the entity frame to the first frame of the newly selected animation sequence. This is required for cinematics because paused units do not execute the normal `M_MoveFrame()` clamp; without the explicit snap the portrait/type can already be human while the world model remains frozen on an old raven `Alternate` frame. Full `Amrf`/`Arav` cast time, morph-sequence timing, takeoff/landing interpolation, transformation effects/sounds, duration/buff-driven automatic reversion, and command-card ability behavior remain separate compatibility work.

## Verification

Focused unit tests install both synthetic `Amrf` and unrelated `Arav` object data and verify that the Medivh-style endpoint resolves through `Amrf`, then verify that:

- `ravenform` changes the same unit from Data A to UnitID and adopts the alternate type's `animProps`;
- `unravenform` restores the Data A unit type and clears the alternate type's authored properties;
- a preplaced alternate-form endpoint can execute `unravenform` without first being transformed by OpenRealm.
