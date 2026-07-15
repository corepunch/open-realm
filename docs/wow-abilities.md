# WoW Ability System

Read this when adding new action bar abilities, modifying the projectile system,
or working with spell targeting.

## Overview

Abilities are dispatched through the `wow_action N` client command, where N is
the action bar slot index (0-based). The dispatch lives in
`Wow_ClientCommand()` in `games/world-of-warcraft/game/g_wow.c:1183`.

## Action Slots

| Slot | Command | Ability | Description |
|------|---------|---------|-------------|
| 0 | `wow_action 0` | Attack | Melee attack nearest enemy |
| 1 | `wow_action 1` | Charge | (placeholder) |
| 2 | `wow_action 2` | Battle Shout | (placeholder) |
| 3 | `wow_action 3` | Healing Touch | Instant self-heal +2 HP (cap 100) |
| 4 | `wow_action 4` | Firebolt | Launches homing projectile at target |

Slots are defined in `wow_start_actions[]` at `g_wow.c:41`.

## Projectile System (WC3-style homing missiles)

### Architecture

Projectiles are server-side entities with kind `WOW_ENTITY_PROJECTILE`.
They move each frame via `Wow_RunProjectile()` toward their tracked target.
When they reach the target, they apply damage and self-remove.

### Projectile Fields (`wowEntityLocal_t`)

| Field | Purpose |
|-------|---------|
| `projectile_target` | Entity number of the tracked target |
| `projectile_caster` | Entity number of the firing entity |
| `projectile_speed` | Units/sec movement speed |
| `projectile_damage` | Damage applied on hit |
| `projectile_yaw` | Initial heading (radians, set at spawn) |
| `projectile_pitch` | (reserved) |

### Hit Detection

`Wow_RunProjectile()` computes `step = speed * (FRAMETIME / 1000.0)` each frame.
If the remaining distance to the target is ≤ step, the projectile hits:
- Deals `projectile_damage` to `target_local->health`
- Calls `target->pain(target)` if damage > 0
- Calls `Wow_AIDie(target, projectile)` if lethal
- Sets `ent->inuse = false`

If the target dies or is removed mid-flight, the projectile self-destructs.

### Spawning a Projectile

Use `Wow_FireFirebolt(caster, target)` as a reference pattern:

```c
proj = Wow_Spawn();
pl = Wow_EntityLocal(proj);
pl->kind = WOW_ENTITY_PROJECTILE;
pl->projectile_target = target->s.number;
pl->projectile_speed = MY_SPEED;
pl->projectile_damage = MY_DAMAGE;
proj->s.origin = caster->s.origin;
proj->s.model = G_RegisterModel("Spells\\MySpell\\MyMissile.m2");
```

All projectile entities run during `Wow_RunFrame()` at `g_wow.c:1125-1135`.

## Targeting

`Wow_FindSpellTarget(ent, range)` provides the targeting logic:
1. If `ent->client->ps.selected_entity` is set and within range, use that
2. Otherwise fall back to `Wow_FindNearestAttackTarget(ent)` (6-unit radius)

## Adding a New Ability

1. Add the icon entry in `wow_start_actions[]` in `g_wow.c`
2. Add a `case` in the `wow_action` switch in `Wow_ClientCommand()` (`g_wow.c:1186`)
3. Write the ability function (static in `g_wow.c`)
4. Add unit tests in `tests/test_wow_abilities.c`
5. Run `make test-wow-abilities` to verify

## Healing Touch

`Wow_HealingTouch(caster)` — instant self-heal:
- Adds `WOW_HEALING_TOUCH_HEAL` (2 HP) to `local->health`, capped at 100
- Attempts to play `SpellCastOmni` animation (or `Cast`/`Attack1H` fallback)
- Animated via `Wow_SetEntityMoveFirstAnimation()`

## Firebolt

`Wow_FireFirebolt(caster, target)` — ranged damage:
- Fires a homing projectile at `WOW_FIREBOLT_SPEED` (25 units/sec)
- Deals `WOW_FIREBOLT_DAMAGE` (2) on impact
- Range for targeting: `WOW_FIREBOLT_RANGE` (30 units)
- Model resolved via `Wow_FireboltModel()` (tries `FireballMissile.m2`,
  `Fireball.m2`, `FireBolt.m2`)
- Caster's `enemy` set to target for combat tracking

See `g_wow.c:482` and `g_wow.c:543`.

## Key Constants

| Constant | Value | File |
|----------|-------|------|
| `WOW_FIREBOLT_SPEED` | 25.0 | `g_wow.c:477` |
| `WOW_FIREBOLT_DAMAGE` | 2 | `g_wow.c:478` |
| `WOW_FIREBOLT_RANGE` | 30.0 | `g_wow.c:479` |
| `WOW_HEALING_TOUCH_HEAL` | 2 | `g_wow.c:480` |
| `WOW_MAX_CLIENTS` | 1 | `g_wow_local.h:8` |
| `WOW_MAX_EDICTS` | 128 | `g_wow_local.h:9` |
