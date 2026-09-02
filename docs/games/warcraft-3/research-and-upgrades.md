# Warcraft III Research And Upgrades

OpenRealm models Warcraft III research as player-owned techtree state started by
a producer building. A Blacksmith only supplies the research command and queue;
a completed upgrade belongs to the player and survives the researching building.

## Reference behavior

The implementation here was checked against a WarsmashModEngine `main` snapshot
dated 2026-08-31. The relevant Warsmash paths are
`CAbilityQueue` / `CUnit` / `CPlayer` for research ownership and queueing,
`CUpgradeData` / `CUpgradeType` / `CUpgradeEffectAttackDice` /
`CUpgradeEffectDefenseUpgradeBonus` for `UpgradeData.slk` and the implemented
effects, and `MeleeUI` for attack/armor status-icon family selection.

The effect and status-icon behavior documented below is Warsmash-specific where
stated; remaining retail-parity gaps stay listed under **Intentionally unresolved**
rather than being filled with guessed behavior.

## Data sources

The runtime reads upgrade metadata from `Units\UpgradeData.slk`:

- `maxlevel`
- `goldbase` / `goldmod`
- `lumberbase` / `lumbermod`
- `timebase` / `timemod`
- `class`
- `inherit`
- the four `effect`, `base`, `mod`, and `code` slots (`effect`/`code` are
  stored as FOURCCs)

Buildings expose upgrades through the `Researches` (`ures`) field already parsed
into `UnitProfile_t.researches`. Unit types opt into completed upgrade effects
through `UnitBalance.upgrades` (`upgrades` / Upgrades Used).

The race `*UpgradeFunc.txt` and `*UpgradeStrings.txt` tables remain authoritative
for research art, button positions, names, tooltips, hotkeys, and per-level
requirements.

## Research availability

For upgrade rawcode `U`, the next level is:

```text
player.researched[U] + 1
```

The research command is hidden when:

- the next level exceeds `UpgradeData.maxlevel`;
- `SetPlayerTechMaxAllowed` restricts the player below that level; or
- the same upgrade is already in progress for that player.

Per-level `Requires` and `Requiresamount` fields disable the command until the
requirements are satisfied. Level 1 uses the unsuffixed fields; level 2 uses
`Requires1` / `Requiresamount1`, level 3 uses `Requires2` /
`Requiresamount2`, matching Warcraft object-data convention.

Costs are:

```text
gold   = goldbase   + goldmod   * (level - 1)
lumber = lumberbase + lumbermod * (level - 1)
time   = timebase   + timemod   * (level - 1)
```

Gold and lumber are charged when the research is accepted. The command-card
tooltip uses the same next-level calculation and the ordinary Warcraft tooltip
resource row, so research buttons show gold/lumber icons and costs exactly like
training buttons. Because the next level is player-owned state, command-bar
serialization must set the current UI client before formatting those tooltips.

## Production queue

Research shares the existing seven-entry producer queue with training and Hero
revival. A queued research item is a lightweight hidden server entity carrying
only the upgrade rawcode, level, exact charged resources, duration, and elapsed
progress.

Only the queue head advances. Research does not reserve Food Used. The building
uses the same production think path as unit training, so a building that can
both train and research preserves the authored queue order instead of running a
second independent timer.

The selected-building info panel shows:

```text
RESEARCHING
```

for an active research head, uses the level-specific upgrade `Art`, and drives
the existing progress bar from simulation progress.

Cancelling a queued research item:

- removes it from the shared queue;
- refunds the exact gold/lumber charged for that item;
- clears the player's in-progress marker; and
- activates the next queue head.

Producer destruction uses the same queue cancellation path, so unfinished
research is cancelled/refunded while completed research remains on the player.

## Completion and player state

Completion clears the in-progress marker and sets the player's researched level
to the level carried by the queue item. The command card is invalidated so the
next level (or no button at maximum level) becomes visible immediately.

The local player also receives the upgrade-complete text and the active race
skin's `ResearchComplete` UI sound when those presentation resources are
available.

`SetPlayerTechResearched` and `AddPlayerTechResearched` use the same stat-effect
path as normal completion. Scripted tech changes therefore update live unit
stats rather than only changing the integer queried by JASS/AI.

## Blacksmith stat effects

This implementation intentionally enables only the upgrade effect codes whose
current Warsmash behavior is sufficiently unambiguous for the stock Blacksmith:

### `ratd` — attack dice

For an affected unit, each level's effect value is:

```text
base + mod * (level - 1)
```

Changing level applies only the difference between the old and new value to
runtime attack dice. This avoids stacking the full new level on top of the old
one and preserves unrelated Hero/item damage modifiers.

### `rarm` — defense upgrade bonus

Armor follows the Warcraft unit type's `defUp` / `armorPerUpgrade` value:

```text
upgrade armor = armorPerUpgrade * researched level
```

Changing level applies the delta between old and new levels to
`edict_t.armor_value`, preserving unrelated armor modifiers.

Existing owned units whose `Upgrades Used` list contains the upgrade are updated
when player tech changes. Newly spawned/trained units run the same application
path after their base/Hero stats are initialized, so they inherit research that
was completed earlier.

The existing info panel resolves the attack/armor upgrade holder from the unit's
`Upgrades Used` list and the upgrade's `class`; it therefore displays the same
player researched level that drives these effects. The class lookup is shared
with game-side upgrade data rather than re-parsed independently by the HUD.

Upgrade families remain distinct even when they share the same `armor` class.
For the stock Human data, for example:

- `Rhar` is the heavy-unit armor family used by units such as Footmen;
- `Rhla` is the light-unit armor family used by Riflemen; and
- `Rhac` is the building armor family used by Human structures.

Researching `Rhar` therefore must not make a Rifleman's `Rhla` holder or a
building's `Rhac` holder display level 1. Each holder displays the researched
level of the rawcode actually present in that selected unit type's `Upgrades
Used` list. Units whose listed upgrades have no attack/armor class, such as the
stock Peasant list, keep those level holders hidden.

## Intentionally unresolved

This patch does **not** guess at the remaining Warcraft upgrade effect codes.
HP, mana, movement, regeneration, attack range/speed, spell-level and other
upgrade effect families still need clean data-driven implementations before
they should mutate runtime units.

The following compatibility work also remains:

- `war3map.w3q` custom upgrade-object overrides are not yet merged into the
  normalized runtime upgrade rows;
- W3I upgrade-availability records are parsed but are not yet applied to the
  research state;
- `EVENT_*_RESEARCH_START/CANCEL/FINISH` are declared but the JASS event context
  still lacks the researched rawcode needed for a correct `GetResearched()`
  implementation, so this patch does not publish incomplete research events;
- second-attack runtime initialization is still incomplete elsewhere in the
  unit combat path, so `ratd` updates attack 2 only when that runtime attack is
  already present;
- ownership-transfer semantics for upgrades whose `inherit` flag controls
  transfer are not changed here;
- UpgradeData `global` semantics and any timing/game-speed adjustment beyond the
  authoritative SLK base/increment values are not introduced speculatively here.

These gaps should be completed from authoritative Warcraft/Warsmash behavior
rather than by adding Blacksmith-specific rawcode tables.
