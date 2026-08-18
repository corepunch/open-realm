# Ability Coverage

This note tracks the current OpenWarcraft3 game-side ability registry against
the reference ability base-code list used for parity work.

Primary local files:

- `games/warcraft-3/game/skills/s_skills.c`
- `games/warcraft-3/game/skills/*.c`

The practical parity list maps Warcraft ability base codes to concrete ability
type definitions. Unknown or unsupported codes should resolve to explicit local
stubs only when their command cards need to appear.

## Current Model

OpenWarcraft3 currently uses a small Quake-style `ability_t`:

```c
typedef struct ability_s {
    void (*init)(LPCSTR, struct ability_s *);
    void (*cmd)(LPEDICT);
} ability_t;
```

Abilities are discovered through the static `abilitylist[]` in
`games/warcraft-3/game/skills/s_skills.c`. Normal unit command buttons are shown only when the
unit ability alias resolves through `Units\AbilityData.slk` to a registered base
code. The command keeps the original alias when dispatching so per-alias SLK
data such as item heal amounts still resolves correctly. Hero abilities are
stored as `heroability_t` entries on the unit, but the skill selection menu is
still incomplete.

Directly copying another engine's ability classes is not mechanical. The local
implementation should port behavior into flat C handlers, `umove_t` state
machines, existing edict fields, and data loaded from SLK/config tables.

## Local Registry

| Code | Local file | Status |
|---|---|---|
| `CmdStop` | `s_stop.c` | Registered, no `cmd`; order helper exists. |
| `CmdMove` | `s_move.c` | Implemented ground move command. |
| `CmdAttack` | `s_attack.c` | Implemented basic melee/ranged attack and projectiles. |
| `CmdBuild` | `s_build.c` | Implemented build menu and placement flow. |
| `CmdHoldPos` | `s_holdpos.c` | Registered stub. |
| `CmdPatrol` | `s_patrol.c` | Registered stub. |
| `CmdCancel` | `s_cancel.c` | Implemented UI cancel. |
| `CmdSelectSkill` | `s_selectskill.c` | Partial; menu draw exists, selection callback is commented out. |
| `Ahar` | `s_harvest_lumber.c` | Partial worker harvest implementation. |
| `Amil` | `s_militia.c` | Registered stub. |
| `Arep` | `s_repair.c` | Partial; repair state helper exists, command is not wired. |
| `Agld` | `s_goldmine.c` | Basic gold mine harvest loop. |
| `AHad` | `s_devotionaura.c` | Partial local-only aura effect; status application is stubbed. |
| `AHhb` | `s_holylight.c` | Partial target spell; validates target/range/masks, spends mana, starts cooldown, heals allies, damages undead enemies, and plays target art. |
| `AHtb` | `s_thunderbolt.c` | Partial target spell; projectile, damage, mana, cooldown, range, target masks, and stun status. |
| `ANfb` | `s_thunderbolt.c` | Partial target spell sharing Thunder Bolt behavior with Fire Bolt data/art. |
| `AHwe`, `AOsf` | `s_summon.c` | Partial no-target summon; reads unit id/count/duration, spawns owned timed-life units. |
| `AHbz`, `AUcs`, `ANcl` | `s_area_spell.c` | Partial point/channel spell family; Blizzard ticks area damage, Carrion Swarm applies a simple point blast, Channel opens cancel mode. |
| `ANch`, `AIco`, `Aeat`, `Ambt`, `Aroo` | `s_utility_abilities.c` | Partial utility behaviors: Charm ownership transfer, Eat Tree heal/remove, Moon Well transfer, Root toggle. |
| `AIhe`, `AIma`, `AImi` | `s_item.c` | Partial item use for heal, mana restore, and permanent life gain; item consumption is not yet wired. |
| Heavy/system abilities | `s_ability_stubs.c` | Registered explicit stubs for passive autocast, cargo, mine, shop, harvest variants, item passives, and stat/XP item families. |

`a_train` exists in `s_train.c`, but training is currently handled by the
generic `Button` command path rather than by a registered ability code.

## Reference Parity List

| Code | Reference behavior | OpenWarcraft3 status | Notes |
|---|---|---|---|
| `AHhb` | Holy Light | Partial | Target selection, range, mana, cooldown, target masks, undead-only enemy damage, healing, and target art exist. Needs richer dispel/damage typing. |
| `AHwe` | Summon Water Elemental | Partial | No-target summon, level unit id, mana/cooldown, ownership, and timed life exist. Needs better placement/art/order polish. |
| `AHbz` | Blizzard | Partial | Point targeting and periodic enemy area damage exist. Needs shard visuals, channel cancellation details, building reduction, and max-damage rules. |
| `AHtb` | Thunder Bolt | Partial | Target selection, projectile, damage, stun, range, mana, cooldown, and target masks exist. Needs better cast animation timing. |
| `ANfb` | Firebolt via Thunder Bolt type | Partial | Shares Thunder Bolt implementation with Fire Bolt data/art. Needs better cast animation timing. |
| `Apxf` | Phoenix Fire | Stub | Registered passive stub. Needs autocast/projectile aura behavior. |
| `AOsf` | Feral Spirit | Partial | No-target summon, level unit id/count, mana/cooldown, ownership, and timed life exist. |
| `Abun` | Burrow cargo hold | Stub | Needs cargo hold state, load/unload slots, and burrow-specific command behavior. |
| `Astd` | Stand Down | Stub | Coupled to cargo/burrow state. |
| `AEim` | Immolation | Stub | Toggle status exists; needs mana drain, periodic area damage, and caster buff art/rules. |
| `Aenc` | Entangled mine cargo hold | TODO | Requires entangled mine cargo behavior. |
| `Aent` | Entangle Gold Mine | TODO | Needs gold mine transform/ownership behavior and target checks. |
| `Aegm` | Entangled Mine | TODO | Needs entangled mine simulation behavior. |
| `Aeat` | Eat Tree | Partial | Tree target, self-heal, mana/cooldown, and tree removal exist. Needs rip/eat timing and buff art. |
| `Ambt` | Moon Well | Partial | Friendly target life transfer from well mana exists. Needs mana transfer, autocast, and night-only regeneration. |
| `ANch` | Charm | Partial | Target ownership transfer, range, mana/cooldown, and max-level gate exist. Needs full target restrictions/order cleanup. |
| `AIco` | Item command using Charm behavior | Partial | Shares Charm handler; inventory alias-to-base dispatch is wired. |
| `AHca` | Cold Arrows | TODO | Needs autocast/toggle projectile modifier and slow buff. |
| `Agld` | Gold Mine | Partial | Local gold mining works for the basic worker loop, but richer mine ability typing remains. |
| `Agl2` | Overlayed Gold Mine | TODO | Needs overlay/minable mine variant. |
| `Abgm` | Blighted Gold Mine | TODO | Needs undead mine variant. |
| `Abli` | Blight | TODO | Needs blight placement/spread and terrain interaction. |
| `Aaha` | Acolyte Harvest | TODO | Needs undead gold harvesting behavior. |
| `Artn` | Return Resources | TODO | Local harvest returns are hardwired inside harvest states, not a standalone ability. |
| `Ahar` | Harvest | Partial | Local worker harvest exists, but not as the same split data model as the ability data. |
| `Awha` | Wisp Harvest | TODO | Needs wisp-specific gather behavior. |
| `Ahrl` | Harvest Lumber | Partial | Local lumber harvest is under `Ahar`; `Ahrl` is not registered. |
| `ANcl` | Channel test | Stub | Opens cancel mode as a generic channel scaffold. |
| `AUcs` | Carrion Swarm dummy | Partial | Simple point-area enemy damage exists. Needs missile/line travel and art. |
| `AInv` | Inventory | Partial | Inventory storage and item use exist, but no `AInv` ability type/capacity/drop rules. |
| `Arep` | Human Repair | Partial | Registered locally, but the command is not wired to target selection. |
| `Aren` | Repair | TODO | Can share most implementation with `Arep` once repair targeting is generalized. |
| `Arst` | Repair | TODO | Can share generalized repair implementation. |
| `Avul` | Invulnerable | Partial | Units with `Avul` spawn with damage immunity; damage guard is tested. Needs targetability/UI status polish. |
| `Apit` | Shop Purchase Item | TODO | Needs shop inventory and purchase flow. |
| `Aneu` | Neutral Building | TODO | Needs neutral interaction and command card behavior. |
| `Aall` | Shop Sharing | TODO | Depends on shop/neutral building systems. |
| `Acoi` | Couple Instant | TODO | Test/special ability; low priority unless map data requires it. |
| `AIhe` | Item Heal | Partial | Inventory alias dispatch and selected-unit heal exist. Needs item charge/consume rules. |
| `AIma` | Item Mana Regain | Partial | Inventory alias dispatch and selected-unit mana restore exist. Needs item charge/consume rules. |
| `AIat` | Item Attack Bonus | TODO | Needs item stat modifier system. |
| `AIab` | Item Stat Bonus | TODO | Needs hero stat modifier system. |
| `AIim` | Permanent Intelligence Gain | TODO | Needs permanent hero stat updates. |
| `AIsm` | Permanent Strength Gain | TODO | Needs permanent hero stat updates. |
| `AIam` | Permanent Agility Gain | TODO | Needs permanent hero stat updates. |
| `AIxm` | Permanent Multi-stat Gain | TODO | Needs permanent hero stat updates. |
| `AIde` | Item Defense Bonus | TODO | Needs armor/stat modifier system. |
| `AIml` | Item Life Bonus | TODO | Needs max-health modifier system. |
| `AImm` | Item Mana Bonus | TODO | Needs max-mana modifier system. |
| `AIfs` | Figurine Summon | TODO | Needs item summon behavior. |
| `AImi` | Permanent Life Gain | Partial | Selected-unit max-health and current-health gain exist. Needs item charge/consume rules. |
| `AIem` | Experience Gain | TODO | Needs hero XP/level flow. |
| `AIlm` | Level Gain | TODO | Needs hero level-up flow. |
| `Acar` | Cargo Hold | TODO | Needs cargo slots and load/unload state. |
| `Aloa` | Load | TODO | Depends on cargo targeting and transport state. |
| `Adro` | Drop | TODO | Depends on cargo targeting and transport state. |
| `Adri` | Drop Instant | TODO | Depends on cargo targeting and transport state. |
| `Aroo` | Root | Partial | Basic rooted movement toggle exists. Needs alternate unit transform and build/move interaction. |

## Suggested Port Order

1. Finish the common spell plumbing: ability level data, mana/cooldown checks,
   target filters, point/unit/no-target command flows, and buff/status storage.
2. Complete one targeted spell end to end, preferably `AHtb` Thunder Bolt, then
   reuse that path for `ANfb`.
3. Bring `AHhb` to real behavior, replacing the current visual-only local
   implementation with target validation and healing.
4. Add no-target summon spells (`AHwe`, `AOsf`) once timed-life units are
   available.
5. Generalize repair/harvest variants (`Arep`, `Aren`, `Arst`, `Ahrl`, `Awha`,
   `Aaha`) after the base worker loops are stable.
6. Defer cargo, shops, inventory item modifiers, root/entangle mine, and passive
   autocast abilities until the underlying status, item, and transform systems
   exist.
