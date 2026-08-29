# WoW Enemies and Creatures

## Creature Types (Taxonomy)

Every creature in WoW is classified into one of 11 **creature types**. This classification determines which abilities, tracking, crowd-control effects, and profession interactions affect the creature.

| Creature Type | Description | Trackable By | CC Vulnerabilities | Profession Interactions |
|--------------|-------------|-------------|-------------------|----------------------|
| **Aberration** | Bizarre anatomy, mutated, non-solid bodies (oozes, tentacles). Old Gods and their minions. | None (untrackable) | Banish (Warlock), Fear (Paladin) | None |
| **Beast** | Standard animals: bears, wolves, raptors, cats, etc. | Track Beasts (Hunter) | Polymorph, Hibernate, Freezing Trap, Wyvern Sting | Skinning (most) |
| **Critter** | Harmless ambient creatures (squirrels, rabbits, deer). Predatory animals attack them for realism. | None | None | None |
| **Demon** | Evil creatures of the Twisting Nether. Burning Legion forces. | Track Demons (Hunter), Sense Demons (Warlock) | Banish, Enslave Demon (Warlock), Seduction (Succubus) | None |
| **Dragonkin** | Dragon-based creatures from whelps to dragonflight leaders. | Track Dragonkin (Hunter) | Hibernate | Skinning (dragon scales) |
| **Elemental** | Manifestations of elements (fire, air, water, earth, mana, shadow, life). Includes treants and lashers. | Track Elementals (Hunter) | Banish | Mining (rock types), Herbalism (plant types) |
| **Giant** | Huge creatures, usually elite. Mountain giants, sea giants. | Track Giants (Hunter) | None standard | Mining (rock-based) |
| **Humanoid** | All playable races plus sapient creatures (ogres, etc.). | Track Humanoids (Hunter/Druid) | Sap, Polymorph, Seduction, Soothe, Incapacitate, Pick Pocket | Skinning (some), Cloth drops for Tailoring |
| **Mechanical** | Moving devices from engineering + magic. Gnome/goblin creations. | Track Mechanicals (Hunter, Engineering) | Polymorph (some) | Engineering (dismantle for parts) |
| **Uncategorized** | Naaru, enchanted objects, inanimate vendors. Not affected by type-specific abilities. | None | None | None |
| **Undead** | Creatures with terminated life functions bound by servitude. Scourge-controlled. | Track Undead (Hunter), Sense Undead (Paladin) | Shackle Undead, Soothe | None |

### Social Behavior

- **Humanoids** are "social" — pulling one draws nearby humanoids. They tend to flee when low on health and attempt to pull other mobs.
- **Demons** and some **undead** also exhibit social behavior.
- **Beasts** tend to have larger aggro radius than other types.
- **Critters** are ignored by players but are hunted by predatory animals (felines, canines) for realism.

## Creature Classifications (Difficulty)

Each creature has a **classification** that indicates its relative power and intended group size.

| Classification | Icon | HP/Damage | Group Size | Loot Quality | Notes |
|---------------|------|-----------|-----------|-------------|-------|
| **Normal** | (none) | Baseline | Solo | Standard | Most common mob type |
| **Elite** | Gold dragon border | Significantly increased | Group (2-5) | Higher quality, more gold | Found in dungeons, outdoor elites, some quest areas |
| **Rare** | Silver dragon border | Similar to normal of same level | Solo/small group | Unique drops (mounts, pets, transmog) | Low spawn rate, long respawn timers (30min-several hours) |
| **Rare Elite** | Gold + silver dragon | Increased + rare drops | Group | High quality + unique drops | Combines elite toughness with rare loot tables |
| **Boss** | Skull level (??) | Massive HP, unique abilities | Raid group (10-30) | Best quality (epic items, tier gear) | Immune to most CC; unique loot tables |
| **World Boss** | Skull level (??) | Extreme HP, outdoor raid | Large raid (20-40) | Best quality | Outdoor; often contested between factions |
| **Mini-boss** | Elite | Above normal elite | Group (5) | Good quality | Found in instances; no complex boss mechanics |

### Boss Difficulty Tiers (Instance Content)

| Difficulty | HP | Damage | Mechanics | Loot ilvl | Access |
|-----------|-----|--------|----------|----------|--------|
| **Looking-for-Raid (LFR)** | Lowest | Lowest | Simplified | Lowest | Queue-based, split into wings |
| **Normal** | Moderate | Moderate | Standard | Moderate | Group formed manually |
| **Heroic** | High | High | Additional mechanics | High | Group formed manually |
| **Mythic** | Highest | Highest | Full mechanics + extras | Highest | Fixed 20-player raid; hardest content |

### Dungeon Difficulties

| Difficulty | Notes |
|-----------|-------|
| **Normal** | Queue-based; suitable for leveling and fresh max-level |
| **Heroic** | Higher ilvl requirement; better loot |
| **Mythic** | No queue; weekly lockout; harder than Heroic |
| **Mythic+** | Infinite scaling with timers and affixes; seasonal rotation |
| **Timewalking** | Legacy content scaled to current level; available during events |

## NPC Roles and AI Behavior

### Threat and Aggro

Every NPC maintains a **threat table** — a list of all characters that have generated threat against it. The NPC attacks the character with the highest threat (the "tank").

**Threat generation sources:**
- Dealing damage (primary source for DPS)
- Healing allies (healer threat)
- Threat-modifying abilities (Taunt, Misdirection, Fade, etc.)

**Aggro rules:**
- Exceeding current target's threat by **10% in melee range** or **30% at range** forces a target switch.
- Taunt instantly sets your threat to match the current target's threat.
- Leaving combat range, Feign Death, or dying drops threat.

### Tank Role

Tanks maintain aggro and position enemies. Key mechanics:
- **Active mitigation** — abilities that reduce incoming damage (Shield Block, Ironfur, Death Strike)
- **Threat generation** — high-threat abilities to hold aggro against DPS burst
- **Positioning** — facing bosses away from the group, positioning for cleave
- **Taunt** — forces enemy to attack the tank for a duration

### Healer Role

Healers keep the group alive. Key mechanics:
- **Direct healing** — immediate HP restoration (Flash Heal, Holy Light)
- **HoTs** — healing over time (Rejuvenation, Renew)
- **Absorbs** — prevent damage before it happens (Power Word: Shield)
- **Cooldowns** — powerful short-duration effects (Spirit Link Totem, Divine Hymn)
- **Mana management** — resource that regenerates during combat

### DPS Role

DPS deal damage to kill enemies. Key mechanics:
- **Single-target** — maximum damage on one target
- **AoE/Cleave** — damage multiple enemies simultaneously
- **Burst vs. sustained** — front-loaded vs. consistent damage
- **Interrupts** — stopping enemy spellcasts
- **Utility** — crowd control, debuffs, raid buffs

## Aggro Radius and Leash Behavior

- **Aggro radius**: the distance at which a creature will notice and attack a player. Varies by creature type (beasts have larger radius).
- **Leash range**: the maximum distance a creature will pursue a target before resetting. If the target moves beyond this range, the creature returns to its spawn point and rapidly regenerates health.
- **Social aggro**: pulling one humanoid draws nearby humanoids. Range varies.
- **Patrol paths**: some creatures follow predetermined routes. Timing pulls around patrols is essential in dungeons.

## Creature Spawning

| Spawn Type | Behavior |
|-----------|----------|
| **Static** | Fixed position; respawns after death on a timer |
| **Timed** | Appears at specific real-world times or intervals |
| **Conditional** | Spawns based on quest progress, world events, or player actions |
| **Rare** | Low spawn probability; long respawn timer (30min - several hours) |
| **Elite** | Always elite classification; often guards quest areas or dungeon entrances |
| **Event** | Spawned during world events ( holidays, invasions, seasonal content) |

## Creature Abilities

NPCs use abilities similar to player classes but without the same constraints:

- **Casters** — ranged magic damage (fireballs, shadow bolts, heals)
- **Melee** — physical attacks (auto-attack, special strikes)
- **Hybrid** — mix of melee and spells
- **Support** — healing, buffing, resurrecting allies
- **CC** — stuns, fears, silences, roots on players
- **Summons** — calling additional enemies

### Common Boss Mechanics

| Mechanic | Description |
|----------|-------------|
| **Tank buster** | High single-target damage requiring cooldown usage |
| **Cleave** | Cone attack in front of boss; group must avoid |
| **AoE** | Area-of-effect damage hitting multiple players |
| **DoT** | Periodic damage applied to players |
| **Add spawns** | Additional enemies that must be killed or controlled |
| **Phase transitions** | Boss changes behavior at HP thresholds |
| **Intermission** | Special phase between normal combat phases |
| **Soft enrage** | Progressive damage increase over time; must kill before overwhelming |
| **Hard enrage** | Timer after which boss instantly kills the raid |
| **Interruptible cast** | Spell that must be stopped or causes raid-wide damage |
| **Dispellable debuff** | Negative effect that healers must remove |
| **Positional requirement** | Must be behind/inside/outside certain areas |

## Entity Architecture (Implementation)

### Entity Types

The engine represents every in-world object as an `edict_t` with a `wowEntityLocal_t` side-car. Behaviour is driven entirely by the side-car's `think`, `idle`, `move`, `attack`, and `pain` function pointers (Quake 2 style); these game callbacks do not belong to the shared server `edict_t`. There is no type/kind tag dispatched on at runtime. Unlike Q2's `spawns[]` table and WC3's `SP_CallSpawn`, WoW has no central entity-type dispatcher; if the taxonomy grows, the hub belongs in `g_spawn.c` (see [spawn-and-teleport.md](spawn-and-teleport.md)).

| Think function | Role |
|---|---|
| `Wow_RunCreatureFrame` | Ambient NPC (currently spawned procedurally) |
| `Wow_RunGameObjectFrame` | Doodad/interactive object (spawned from ADT MDDF, cross-referenced with `GameObjectDisplayInfo.dbc`) |
| `Wow_RunCorpseFrame` | Dying creature transitioned in place; timer-based decay then free |
| `Wow_RunDynamicObjectFrame` | Timed area effect (spell AoE); spawned via `Wow_SpawnDynamicObject` |
| `Wow_RunProjectile` | In-flight spell projectile (firebolt, frostbolt) |

### Spell System

Spells are data-driven via the `wowSpellDef_t` table (`wow_spells[]`) following the Q2 `g_items.c` pattern. Each row holds `cast`, `cast_time`, `mana_cost`, `range`, `cast_anim`, and `ready_anim`. Dispatch in `BeginSpellCast`/`CompleteSpellCast` indexes directly into this table.

- `SPELL_NONE` sentinel is `(DWORD)-1` — not `0` — to avoid collision with `WOW_SPELL_ATTACK=0`.
- Projectile impact visuals go through `svc_temp_entity` + `TE_FIREBOLT_IMPACT`/`TE_FROSTBOLT_IMPACT` (Q2 pattern); do not spawn `DynamicObject` entities for that path.

### Per-Frame Spawn Budget

`WOW_MAX_SPAWNS_PER_FRAME = 64` — the `wow_spawns_this_frame` counter is reset each frame. Spawning functions check this budget before allocating edicts to prevent burst stalls on large initial entity loads.

### Creature Info Cache

`Wow_CachedCreatureName/Type/Family/Rank(display_id)` provide lazily-loaded lookups into `CreatureDisplayInfo.dbc` / `CreatureModelData.dbc`. Results are memoized per `display_id`.

### Key Files

| File | Purpose |
|---|---|
| `games/world-of-warcraft/game/g_wow.c` | `Wow_RunFrame`, entity dispatch, player movement |
| `games/world-of-warcraft/game/g_gameobject.c` | Game-object spawn/frame callbacks; corpse and dynamic-object decay |
| `games/world-of-warcraft/game/g_spawn.c` | Player spawn teleport; designated home for a future `SP_CallSpawn`-style dispatcher |
| `games/world-of-warcraft/game/g_ai.c` | Creature AI (idle, move, attack, pain, die) |
| `games/world-of-warcraft/game/m_creature.c` | Move tables and animation selectors |
| `games/world-of-warcraft/game/g_wow_local.h` | All WoW-game types (`wowEntityLocal_t`, `wowSpellDef_t`, etc.) |

## References

- Warcraft Wiki — Creature: https://warcraft.wiki.gg/wiki/Creature
- Warcraft Wiki — Rare mob: https://warcraft.wiki.gg/wiki/Rare_mob
- Warcraft Wiki — Boss: https://warcraft.wiki.gg/wiki/Boss
- Warcraft Wiki — Aggro: https://warcraft.wiki.gg/wiki/Aggro
- Warcraft Wiki — Class role: https://warcraft.wiki.gg/wiki/Class_role
- Warcraft Wiki — Threat: https://warcraft.wiki.gg/wiki/Threat
