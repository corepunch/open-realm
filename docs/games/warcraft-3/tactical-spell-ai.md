# Warcraft III autonomous tactical spellcasting

The game module now shares a bounded tactical spell-policy dispatcher between
Neutral Hostile units and computer-controlled player slots. `g_monster.c`
invokes `G_TryUnitTacticalCast()` after the active move callback, allowing
computer-controlled units to consider their spells during attack cooldowns.
The dispatch remains independent of both the map JASS VM and `common.ai` /
melee/campaign AI-script VMs. Script-issued orders continue to work normally.

## Admission and scheduling

- The unit must belong to Neutral Hostile or a player whose current JASS
  controller is `MAP_CONTROL_COMPUTER` (including `SetPlayerController` changes).
  Human and Neutral Passive units are excluded. Ownership changes take effect
  on the next evaluation without a cached owner flag.
- The unit must be alive, not a building, paused, stunned, asleep, channeling,
  executing a queued order or returning to its creep guard point.
- The current move must be an ordinary idle/stand or attack. Travel, building,
  harvest and other explicitly ordered movements are not interrupted.
- The unit's abilities are examined from current authored `abilList`, dynamic
  additions, and learned hero abilities. AbilityData aliases retain the
  procedure of their base ability while retaining the original rawcode for
  mana, cooldown, effects and targets.
- Spell eligibility uses existing availability/research checks and the normal
  `S_CastNoTargetSpell`, `S_CastUnitTargetSpell` and
  `S_CastPointTargetSpell` validation/commit pipeline. Channelled area spells
  begin only while idle.
- The scheduler uses the staggered 300 ms acquisition cadence and skips area
  enumeration for units with no supported tactical spells. Each evaluation
  scans nearby world entities once with bounded candidate arrays.
- Enabled autocasts retain their existing, separate acquisition path. Abilities
  flagged `AB_AUTOCAST` are not re-issued by the tactical dispatcher.

## Initial policy coverage

| Policy | Ability procedures | Decision rule |
| --- | --- | --- |
| Self-area attack | War Stomp, Thunder Clap | At least two nearby enemies within authored AoE |
| Friendly area buff | Roar, Battle Roar | At least two friendly units and a nearby enemy, no current Roar buff |
| Summon | Feral Spirit, Spirit Beast, Water Elemental | Enemy in effective range |
| Friendly wounded-unit heal | Rejuvenation, Healing Wave | Nearby ally at or below 65% health (avoid refreshing Rejuvenation) |
| Group unit-target | Chain Lightning, Frost Nova, Forked Lightning | At least two enemies within authored AoE around selected target |
| Group point-target | Shockwave, Impale, Blizzard, Rain of Fire, Flame Strike | At least two enemies in selected AoE |
| Single enemy target | Sleep, Storm Bolt, Fire Bolt, Hex, Cripple, Ensnare, Banish, Shadow Strike, Purge | Nearest legal target, usual spell-specific validation |

The AI does **not** attempt to cast arbitrary `AB_SPELL` abilities. Several
spells are passive/toggle/script-only or need specialised decisions. In
particular the generic autocast facility and existing AI-script orders remain
responsible for their respective abilities.

## Retail fidelity and remaining work

**BZ_COMPAT_GUESS:** group size (2), heal threshold (65%), fallback radii,
initial deterministic selection order and the 300 ms cadence reuse are
provisional. They have not been established as retail constants. Some
Warcraft III patches and abilities have different targeting rules. The
first implementation deliberately favours safe, useful behaviour over
unverified claims of exact retail parity.

Open questions: retail target weights, ability priority for units with
multiple spells, tactical use of defensive single-target buffs, summon
population limits, corpse spells, polymorph, dispel logic, and whether
individual abilities should consider more sophisticated battle state.
Comparative in-game tests should be done against a named ROC/TFT/Reforged
version before hard-coding further rules.
