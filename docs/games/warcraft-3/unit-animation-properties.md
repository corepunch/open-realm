# Warcraft III Unit Animation Properties And Transformation Forms

## Required Animation Names

Warcraft unit profile data may provide `animProps` (`uani`, Required Animation Names). OpenRealm copies that authored value into the per-unit `animation_props` set when `SP_SpawnUnit()` initializes a unit. `SetUnitAnimation` retains the logical animation request separately in `animation_request`; animation lookup then combines the request tags with the unit's active Required Animation Names.

`AddUnitAnimationProperties` mutates the active per-unit tag set and immediately reselects the retained logical animation family. This state is inline edict data, so ordinary WC3 save/load persists it with the unit record.

Numbered model sequences such as `Walk`, `Walk 2`, and tagged walk variants are one logical family. Movement keeps the selected variant while that sequence is active, then selects a same-syncpoint variant when the cycle completes. Changing the logical animation or required properties still performs a fresh selection; movement ticks must not restart a numbered walk every frame.

The stock Medivh model/data combination is unusual: the raven-form unit requests `alternateex`, while the shared model exposes `Alternate` sequences. Animation selection therefore falls back from a required `alternateex` tag to `alternate` only when no matching `AlternateEx` sequence exists. A genuine `AlternateEx` model remains distinct.

## Raven Form Orders

WC3 exposes two stock abilities that use the `ravenform` / `unravenform` order pair: Medivh Crow Form (`Amrf`) and Druid of the Talon Storm Crow Form (`Arav`). Each ability's object data owns its own transformation endpoints:

- AbilityData Data A (`Data11` in ROC, `DataA1` in TFT) is the base unit type;
- AbilityData `UnitID1` is the raven/alternate unit type.

The immediate orders `ravenform` and `unravenform` first choose the stock transform ability whose authored Data A / UnitID endpoints contain the current unit, then transform the existing edict between those two authored types. The edict/JASS handle is retained rather than replacing the unit with a new entity. Rebinding the type reruns the normal unit-data initialization, including model, movement layer, collision, attacks, authored Required Animation Names, and other presentation state, while preserving current health/mana ratios and temporary combat bonuses.

Preplaced alternate-form campaign units are valid transformation endpoints even if they did not pass through OpenRealm's runtime ability-add path before the map script issues `unravenform`. This is required by the Prologue campaign Medivh raven: the map starts with the alternate unit and later orders that same unit to return to the base form.

The type rebind still happens on the script-issued order so the existing edict immediately owns the destination unit data, but presentation then runs the shared model's authored morph sequence as an active `umove_t`. Human-to-raven temporarily removes alternate-form animation properties to select the untagged `Morph` clip, restores the raven `alternateex` requirement while that clip is active, and enters the raven stand family when it completes. Raven-to-human plays `Morph Alternate` and clears the alternate-form properties at completion before entering the ordinary stand family. The morph move explicitly starts at the selected sequence's first frame, including for cinematic units that may otherwise be paused.

`M_MoveFrame()` restarts the animation that is active *after* an end callback returns. This matters for morph completion: an end callback can replace `Morph Alternate` with `Stand`, and resetting the frame to the completed morph's start would make the newly rebound human model sample an unrelated frame for one tick, producing a visible blank between forms.

Full `Amrf`/`Arav` cast time, transformation effects/sounds, duration/buff-driven automatic reversion remain separate compatibility work. The post-morph takeoff now interpolates the destination flying form's authored fly height over the ability's Data C duration; landing remains an immediate form change.

## Ability Ownership

`skills/s_raven.c` registers `a_raven_form` for both `Amrf` and `Arav`. The ability owns endpoint resolution,
command-card toggling, `ravenform`/`unravenform` orders, both morph `umove_t` records, completion callbacks, and takeoff.
`G_TransformUnitType` remains the generic in-place type-rebind operation shared with other transformation abilities.

`ability_t.orders`/`.order` declare immediate orders; `FindAbilityByOrder` resolves them without spell-name branches
in `m_unit.c`. Persistent `.update` callbacks run through `S_RunAbilityUpdates` in `monster_think`. `InitAbilities`
builds a unique callback list once, so two rawcodes using one handler do not tick it twice and each unit tick avoids
scanning the full ability registry. If an order replaces forward Morph before its end callback runs, the Raven update
starts the pending ascent without replacing that order. Callbacks own eligibility and pause policy: Raven's ascent continues after Move
replaces the morph order, including while paused, preserving #398's independent timer behavior. Animation itself
still pauses. Morph moves have no acquisition think callback, so idle AI cannot replace a morph with Attack.

The takeoff values are inline `edict.raven` state, explicitly described by `raven_fields` in `g_save.c` and covered
by ROC/TFT round-trip tests. The generic `currentmove` relocation retains ability-owned morph moves.

## Investigation Notes

- #398 (`dd551065`) introduced morph/takeoff in `m_unit.c` and a direct Raven update in `monster_think`.
  These now belong to the ability; generic animation completion remains in `M_MoveFrame`.
- The animation regression trace showed `old=morph alternate old_start=0 new=stand alternate new_start=1000`;
  restarting the completed clip selected the wrong frame after the callback.
- Live ROC Prologue01 originally rejected `nmdm`: `Amrf` loaded `UnitID1=nmdm`, but its base ID was zero.
  Archive inspection showed `Data11=nmed`; the DDX schema loaded ROC Data11..Data34 only as numbers.
  `BZ_AB_ROW` now populates both numeric and FOURCC views, as the TFT DataA..DataI schema already did.
  Do not patch Raven with hardcoded unit IDs or per-format column lookup.

- Prologue01 orders Move 0.5 seconds after `ravenform`. The interruption trace showed `move=walk`,
  `anim=Stand Alternate`, `height=0` with takeoff still pending: replacing `currentmove` bypasses the Morph
  end callback. The ability update now starts ascent when the pending morph has been replaced.

## Verification

Focused unit tests install both synthetic `Amrf` and unrelated `Arav` object data and verify that the Medivh-style endpoint resolves through `Amrf`, then verify that:

- `ravenform` changes the same unit from Data A to UnitID and adopts the alternate type's `animProps`;
- `unravenform` restores the Data A unit type and clears the alternate type's authored properties;
- a preplaced alternate-form endpoint can execute `unravenform` without first being transformed by OpenRealm.
- Raven Form holds the transformed unit on its support surface through `Morph`, then interpolates to its authored fly height.

The `mdxgen morph` fixture supplies distinct base/alternate Stand, Walk, and Morph clips at the original
`Units/Creeps/Medivh/Medivh.mdx` archive path. It is generated into the fixture MPQ only. Dispatch tests cover
registered orders, toggle state, nonmatching units, ordinary-unit update no-ops, morph completion, ascent during
Move/pause, reversal, and both data-column schemas.

Use the automated reproducers for functionality checks; they replace repeated launches of Prologue01:

```sh
make test-wc3-engine WC3_PATTERN='wc3_unit.*'
make test-wc3-engine WC3_PATTERN='wc3_combat.mmoveframe*'
make test-wc3-engine WC3_PATTERN='wc3_slk.roc_ability_data_preserves_object_ids_and_numbers'
make test-wc3-engine WC3_PATTERN='wc3_save.*'
make test
```

These headless fixture tests exercise the same order, callback, interruption, and persistence situations as the campaign.
If another gameplay failure is reported, extend the reproducer rather than relying on another manual replay. See
[test-first verification](../../../CONTRIBUTING.md#test-first-behavior-verification).

The original live-archive reproducer is retained for an uncovered visual/platform question only; identify that gap before
using it (repeat with `-tft` if the gap depends on archive variants):

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc -com_fast_forward +set sv_cheats 1 +map Maps/Campaign/Prologue01.w3m +jass Trig_End_Cinematic_Actions +com_frame_limit 1600
```
