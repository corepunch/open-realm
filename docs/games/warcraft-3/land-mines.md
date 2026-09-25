# Warcraft III Land Mines

This document records the OpenWarcraft3 contracts for the stock Goblin Land
Mines chain used by campaign maps such as Undead08.  The implementation is
ability-data driven; campaign code does not special-case the `gobm` item.

## Object chain

The stock behavior is split across three gameplay abilities:

- `AIpm` is the point-target item ability.  Its authored `UnitID` is spawned at
  the selected point through the ordinary summon path, preserving player
  ownership and summon events.
- `Amin` is an intrinsic ability on the mine unit.  Data A is the activation
  delay, Data B is the invisibility transition time, and the ability's authored
  cast range is the proximity trigger radius.
- `Amnx` is an `Adda` (AOE Damage Upon Death) alias.  Data A/B are the full
  radius/damage pair; Data C/D are the partial radius/damage pair; `Duration`
  delays damage after death.

`AIdm` is intentionally not part of the stock mine damage path.  Warcraft
ability research identifies it as a deprecated tree/wall helper; modern stock
mine data uses `Amnx`/AOE Damage Upon Death `Targets Allowed` for unit and
destructible classes instead.  OpenWarcraft3 therefore must not synthesize a
second `AIdm` blast that could double-damage victims.  Compatibility for custom
maps that explicitly invoke the legacy ability remains separate work.

Research references used for the non-obvious compatibility rules in this file:

- Warcraft III patch 1.03 records the rooted-Ancient land-mine trigger fix:
  <https://liquipedia.net/warcraft/Patch_1.03>.
- The Warcraft III Editor Ability Insight notes identify `AIdm` as deprecated
  and say destructible damage moved to AOE Damage Upon Death `Targets Allowed`:
  <https://github.com/Cokemonkey11/wc3-ability-doc/blob/main/sources/screwthetrees-wc3-editor-ability-insight-document/2023-11-14.md>.
- Model-author discussion confirms the stock mine explosion is authored in a
  distinct `Death Spell` sequence rather than a separate explosion ability
  effect:
  <https://www.hiveworkshop.com/threads/cant-find-the-location-to-change-the-model-file-for-goblin-mine-explosion.246079/>.

## Placement and charged items

Point-target item commands retain the exact carried item while target mode is
active.  Clicking the inventory button does not consume a charge.  Invalid
point clicks and Cancel leave the charge unchanged.  After the shared point
spell path successfully executes the ability, it publishes the normal
`EVENT_PLAYER_UNIT_USE_ITEM` / `EVENT_UNIT_USE_ITEM` events and consumes one
charge.  A final perishable charge can therefore remove the item only after a
successful placement.

This completion contract currently covers point-target item abilities.  A
unit-target item that walks into range before executing still needs an
analogous carried-item completion token attached to the deferred approach
order.

## `Amin` runtime state

`Amin` is registered as an innate passive rather than a point spell.  On unit
initialization it creates a saveable thinker only for units that actually own
`Amin`.

The thinker stores:

- the mine edict and its spawn generation;
- the concrete ability rawcode and level;
- the activation deadline;
- when applicable, the invisibility-transition deadline.

Mines set simulation collision radius to zero immediately so they remain
walk-over traps before and after invisibility.  Negative Data B leaves the mine
visible; zero hides immediately; a positive value hides when the authored
transition expires.  `RF_HIDDEN` is treated as gameplay invisibility for units
that own `Amin`, so owner/shared vision and True Sight reuse the normal
per-viewer invisibility path rather than globally unhiding the entity.

After Data A has elapsed, alive enemy ground units inside `Rng` trigger the
mine.  Air units and ordinary structures do not trigger it.  Warcraft III 1.03
specifically fixed rooted Ancients incorrectly triggering Goblin Land Mines, so
Root-capable structures are treated as triggerable only while their current
movement state is mobile/uprooted.  This consumes the existing Root movement
state rather than hard-coding Night Elf unit rawcodes.

The thinker retires first, then the mine kills itself through `unit_die()`.
This is important: normal unit/player death events and authored death abilities
must run.  `Amin` clears its live invisibility render state on death so the
death presentation is no longer suppressed by the trap's hidden state.

The stock model contains the visible detonation in its `Death Spell` sequence,
while ordinary `Death` remains the generic destruction/deactivation sequence.
`Amin` therefore lets `unit_die()` complete first so normal death events,
`Amnx`, and decay state remain authoritative, then the proximity-trigger path
requests `death spell` and restarts that selected sequence from its first frame.
The renderer's normal Death-family fallback still handles custom models that do
not provide the secondary `Spell` tag.

The renderer now also consumes Warcraft MDX `SPN` events reached by death
animations.  `SPN` is presentation-only: it resolves a row through
`Splats\SpawnData.slk`, snapshots the animated event-node world transform, and
plays that row's model sequence 0 in an independent transient after the source
unit has died.  This addresses the observed case where `Death Spell` made the
mine's lingering fire visible but the immediate event-spawned blast was still
absent.  See [MDX Event Objects](mdx-event-objects.md).

Destroying a mine by ordinary damage follows the same gameplay death path but
does not pass through the proximity-trigger presentation override, so it keeps
the ordinary `death` request.  The `Amin` thinker is removed by its `A_DEATH`
lifecycle callback.

## `Adda` / `Amnx` death damage

`CAbilityDeathDamageAoe` now respects the authored two-ring fields and target
mask.  Physical explosion filtering does not use spell-immunity or
caster-visibility checks.  The supported unit target tokens include
air/ground/structure, organic/mechanical, and player/friend/enemy/neutral
alliance categories.

When `Duration <= 0`, damage resolves immediately at the death position.  When
`Duration > 0`, death creates a saveable thinker that snapshots only:

- death position;
- owner/player attribution;
- concrete ability rawcode and level;
- resolution deadline.

Victims are enumerated when the timer resolves, not at mine trigger time.  A
unit that leaves the partial-damage radius before the delay expires therefore
escapes the blast.  This also keeps full/partial ring selection data-driven.

The same delayed enumerator also applies the authored destructible target
classes (`tree`, `wall`, `debris`, `bridge`, and `decoration`) through
`G_DestructableApplyDamage()`.  Destructibles have no player alliance, so their
eligibility is determined by the authored class token; `AIdm` is not invoked as
a second blast.

## Save/load

`land_mine_think` and `death_damage_aoe_think` are persistent C callbacks and
are appended to the `save_cfunctions[]` roster.  Their state uses existing
serialized edict fields; no persistent `edict_t` layout fields were added.
The point-target inventory `ability_item` pointer is transient UI targeting
state and is explicitly ignored by save/load, matching the other menu callback
state.

## Tests

Fixture-only regression coverage should verify:

- `AIpm`, `Amin`, and `Amnx` resolve to their intended procedures;
- point placement creates an owned real unit at the requested location;
- invalid point item targets do not spend charges and successful placement
  spends exactly one;
- Data B invisibility transition and True Sight interaction;
- Data A arming and `Rng` proximity triggering;
- air/ordinary-structure trigger exclusion;
- rooted Ancients do not trigger while their uprooted/mobile form does;
- self-death through the normal death pipeline and `Death Spell` presentation
  for proximity detonation;
- ordinary/manual destruction keeps the generic `Death` presentation;
- `Amnx` delayed resolution and fast-unit escape;
- full and partial authored damage rings;
- authored tree/debris target classes use the same delayed rings;
- manual destruction still executes death damage;
- both new thinker callbacks round-trip through the save callback roster.

The tests use authored non-stock fixture values so the implementation cannot
pass merely by hard-coding retail constants.

## Remaining gaps

- nested `EVTS` emitted by an `SPN` child model are not recursively dispatched;
  no stock land-mine requirement for nested events is currently known;
- broader unit/alliance target-mask edge cases not exercised by stock `Amnx`;
- fuller Root/Unroot gameplay parity remains a separate subsystem concern; mine
  triggering already consumes the current mobile/rooted state when available;
- unit-target asynchronous item charge completion;
- `AIdm` compatibility for custom maps that explicitly author that deprecated
  legacy ability.
