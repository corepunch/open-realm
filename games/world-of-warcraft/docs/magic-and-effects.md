# WoW Magic Schools, Damage, and Status Effects

## Magic Schools

Every ability in WoW belongs to one of seven **magic schools**. Even non-damaging utility and crowd-control abilities have a school. Schools determine:

1. **Damage amplification/reduction** — buffs, debuffs, and boss mechanics interact with specific schools.
2. **School lockout on interrupt** — when a cast is interrupted, the caster is locked out of that school for a duration (e.g. Kick locks Fire for 3s; the mage can still cast Frostbolt).
3. **Resistance interactions** — racial passives grant small resistance bonuses to a specific school (e.g. Dwarves: Frost, Night Elves: Nature, Gnomes/Blood Elves: Arcane).

### Base Schools

| School | Common Users | Typical Spells |
|--------|-------------|----------------|
| **Physical** | All melee/ranged weapon users | Mortal Strike, Mutilate, auto-attacks |
| **Arcane** | Mage, Druid, Priest | Arcane Blast, Polymorph, Moonfire |
| **Fire** | Mage, Warlock, Shaman | Fireball, Immolate, Flame Shock |
| **Frost** | Mage, Shaman, Death Knight | Frostbolt, Frost Shock, Howling Blast |
| **Holy** | Paladin, Priest, Shaman | Holy Fire, Flash Heal, Healing Wave |
| **Nature** | Shaman, Druid, Hunter, Rogue (poisons) | Lightning Bolt, Chain Lightning, Serpent Sting |
| **Shadow** | Warlock, Priest, Death Knight | Shadow Bolt, Mind Flay, Void Bolt |

### Multi-School System

Multi-school spells combine two or more base schools. They benefit from bonuses to *any* of their component schools and use the lowest resistance among them. Interrupting a multi-school spell locks out *all* component schools simultaneously.

| Multi-School | Components | Primary Users |
|-------------|-----------|---------------|
| **Chaos** | Arcane + Fire + Frost + Nature + Shadow (+ optionally Holy + Physical) | Demon Hunter, Warlock |
| **Astral** | Arcane + Nature | Druid |
| **Elemental** | Fire + Frost + Nature | Shaman |
| **Frostfire** | Fire + Frost | Mage |
| **Shadowflame** | Fire + Shadow | Warlock |
| **Shadowfrost** | Frost + Shadow | Death Knight, Priest |
| **Holystrike** | Holy + Physical | Paladin |
| **Radiant** | Fire + Holy | Paladin, Priest |
| **Twilight** | Holy + Shadow | Priest |
| **Plague** | Nature + Shadow | Death Knight |
| **Volcanic** | Fire + Nature | Evoker, Shaman |
| **Spellfrost** | Arcane + Frost | Evoker |
| **Cosmic** | Arcane + Holy + Nature + Shadow | Demon Hunter, Rogue |
| **Flamestrike** | Fire + Physical | Shaman |
| **Stormstrike** | Nature + Physical | Warrior |

Chaos is the most comprehensive multi-school, making Demon Hunters/Warlocks nearly impossible to fully lock out via interrupts.

## Damage Types and Mitigation

| Mechanic | Applies To | Notes |
|----------|-----------|-------|
| **Block** | Physical attacks | Shield block chance |
| **Parry** | Physical attacks (from front) | Weapon parry chance |
| **Dodge** | Physical attacks | Agility-based dodge chance |
| **Armor** | Physical damage only | Reduces physical damage; formula: `dmg /= (1 + armor * constant)` |
| **Resistance** | Magical schools | Largely removed from modern WoW; only racial passives remain |
| **Chaos Brand** | All magic schools (DH debuff) | +5% magic damage taken; applied by Demon Hunter |
| **Mystic Touch** | Physical (Monk debuff) | +5% physical damage taken; applied by Monk |

## Buffs and Debuffs

### Buffs (Positive Effects)

Buffs enhance the target's capabilities. They have no effective stack limit on a single target. Key categories:

- **Stat buffs** — increase STR/AGI/INT/STA/SPI (e.g. Power Word: Fortitude, Mark of the Wild)
- **Damage buffs** — increase damage dealt (e.g. Battle Shout, Arcane Intellect)
- **Healing buffs** — increase healing received or output
- **Defensive buffs** — damage absorption, damage reduction, immunities (e.g. Power Word: Shield, Ice Block, Divine Shield)
- **Utility buffs** — movement speed, stealth, water breathing, etc.

### Debuffs (Negative Effects)

Debuffs hinder the target. There is no effective limit on debuff count per target (the 40-debuff limit was removed in WotLK 3.0.2). Debuffs are **color-coded by type**:

| Debuff Type | Border Color | Removed By |
|------------|-------------|-----------|
| **Magic** | Blue (`#3399FF`) | Druid (Nature's Cure), Mage (Remove Curse), Paladin (Cleanse), Priest (Purify), Shaman (Purge/Dispel) |
| **Curse** | Purple (`#9900FF`) | Druid (Remove Curse), Mage (Remove Curse) |
| **Disease** | Brown (`#996600`) | Paladin (Cleanse), Priest (Purify Disease), Monk (Detox) |
| **Poison** | Green (`#009900`) | Druid (Cure Poison), Paladin (Cleanse), Monk (Detox), Shaman (Cure Poison) |
| **Physical/Other** | Red (`#CC0000`) | Cannot be mass-dispelled; some bleeds removable by Luffa or Stoneform |

### Stacking Rules

- Similar non-DoT debuffs generally do **not** stack from the same source.
- DoTs from different players **do** stack (e.g. multiple Corruption instances).
- DoTs from the same player on different targets stack independently.
- Boss monsters can stack multiple copies of the same DoT on a single player.

## Damage Over Time (DoT) and Healing Over Time (HoT)

### DoT Mechanics

DoTs apply periodic damage at regular intervals called **ticks**. Each tick deals a fixed amount (some spells escalate per tick). Key rules:

- **Snapshot**: most DoTs snapshot the caster's stats at application time and maintain those stats for the full duration.
- **Refresh**: reapplying a DoT typically extends the duration from the current time (pandemic mechanic — refresh at ≤30% remaining duration to avoid losing ticks).
- **Haste**: reduces time between ticks, increasing total damage within the same duration.
- **Crit**: each tick can independently crit.
- **Mastery**: some specs amplify DoT damage (e.g. Affliction Warlock's Dread Touch).

Examples: Corruption (Warlock), Immolate (Warlock), Shadow Word: Pain (Priest), Rip (Feral Druid), Serpent Sting (Hunter).

### HoT Mechanics

HoTs are the healing counterpart. Restoration Druids are the primary HoT class. Key rules:

- HoTs can stack on the same target (e.g. multiple Rejuvenation from different druids).
- Mastery can amplify healing per active HoT on a target (Druid's Mastery: Harmony).
- HoTs benefit from the same haste/crit/versatility scaling as DoT.

Examples: Rejuvenation (Druid), Lifebloom (Druid), Healing Stream Totem (Shaman), Renew (Priest).

## Crowd Control (CC)

CC effects limit an opponent's ability to act. All CC has a **type** and is subject to **diminishing returns (DR)** in PvP.

### CC Types

| CC Type | Breaks on Damage? | Target Behavior | DR Category |
|---------|-------------------|----------------|-------------|
| **Stun** | No | Cannot move or act | Stuns |
| **Fear/Horrify** | No (runs away) | Wanders randomly away from caster | Fears |
| **Incapacitate** | Yes | Stationary, cannot act | Incapacitates |
| **Disorient** | Yes | Wanders randomly | Disorients |
| **Root** | No | Cannot move, can still cast/attack | Roots |
| **Freeze** | No | Cannot move (ice-based) | Roots |
| **Snare/Slow** | No | Reduced movement speed | Snares |
| **Silence** | No | Cannot cast spells | Silences |
| **Disarm** | No | Cannot use weapons | Disarms |
| **Sleep** | Yes | Stationary, wakes on damage | Sleeps |
| **Charm** | No | Controlled by caster | Charms |
| **Banish** | No | Immune to all damage/healing, cannot act | Banishes |
| **Polymorph** | Yes | Transformed (sheep/pig/turtle), regenerates HP | Incapacitates |
| **Cyclone** | No | Immune to all damage/healing, cannot act | Disorients |
| **Knockback** | N/A | Pushed back | Knockbacks |

### Diminishing Returns (PvP)

Each successive CC of the same DR category on the same target within 15s is reduced:
- 2nd application: 50% duration
- 3rd application: 25% duration
- 4th+ application: immune

DR categories: **Stuns, Silences, Disarms, Knockbacks, Roots, Disorients, Incapacitates**.

### Class CC Examples

| Class | Primary CC | Type |
|-------|-----------|------|
| Mage | Polymorph | Incapacitate (breaks on damage) |
| Mage | Frost Nova | Root |
| Warlock | Fear | Fear |
| Warlock | Banish | Banish (demons/elementals) |
| Warlock | Seduction | Charm (humanoids, Succubus pet) |
| Hunter | Freezing Trap | Incapacitate |
| Hunter | Wyvern Sting | Sleep |
| Rogue | Sap | Incapacitate |
| Rogue | Kidney Shot | Stun |
| Druid | Entangling Roots | Root |
| Druid | Hibernate | Sleep (beasts/dragonkin) |
| Paladin | Repentance | Incapacitate (humanoids/undead/dragonkin/giants/demons) |
| Shaman | Hex | Incapacitate (beasts/humanoids) |
| Priest | Psychic Scream | Fear (AoE) |
| Priest | Mind Control | Charm (humanoids) |
| Priest | Shackle Undead | Sleep (undead) |
| Death Knight | Death Grip | Grip (forced movement) |
| Monk | Paralysis | Incapacitate |
| Demon Hunter | Imprison | Incapacitate |

## Other Status Effects

| Effect | Description |
|--------|-------------|
| **Bleed** | Physical DoT; cannot be dispelled (removed by Luffa/Stoneform) |
| **Enrage** | Damage increase buff; removable by Soothe (Druid) |
| **Daze** | Forced slow when hit from behind while mounted/fleeing |
| **Taunt/Provoke** | Forces enemy to attack the caster for a duration |
| **Banish** | Immune to damage/healing, cannot act (demons/elementals) |
| **Seduction** | Charm effect on humanoids (Succubus) |
| **Mind Control** | Priest takes control of humanoid target |
| **Slow** | Reduced cast speed or movement speed |
| **Knockback** | Pushed away from caster |
| **Grip** | Pulled toward caster (Death Knight) |
| **Disarm** | Cannot use weapons |
| **Silence** | Cannot cast spells |
| **Interrupt** | Stops current cast and locks the school |

## Spell Properties

| Property | Description |
|----------|-------------|
| **Cast Time** | Time to cast before effect fires (0 = instant) |
| **Channeled** | Effect persists over the cast duration; interrupted by movement/damage |
| **Cooldown** | Time before the spell can be cast again |
| **GCD** | Global Cooldown triggered after most spells (1.5s base, reduced by haste) |
| **Mana Cost** | Resource consumed on cast |
| **Range** | Maximum distance to target |
| **Area of Effect** | Radius/cone/line of effect |
| **Projectile** | Travels to target over time (can be dodged/intercepted) |
| **Instant** | No cast time, fires immediately |

## References

- Warcraft Wiki — Magic schools: https://warcraft.wiki.gg/wiki/Magic_schools
- Warcraft Wiki — Debuff: https://warcraft.wiki.gg/wiki/Debuff
- Warcraft Wiki — Crowd control: https://warcraft.wiki.gg/wiki/Crowd_control
- Warcraft Wiki — Stun: https://warcraft.wiki.gg/wiki/Stun
- Warcraft Wiki — Incapacitate: https://warcraft.wiki.gg/wiki/Incapacitate
- Maxroll — Magic Schools: https://maxroll.gg/wow/resources/magic-schools
- Liquipedia — Diminishing Returns: https://liquipedia.net/worldofwarcraft/Diminishing_Returns
