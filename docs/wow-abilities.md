# WoW Ability and Casting System

Read this before changing action-bar input, casts, projectiles, spell animation, or spell UI.

## Player Flow

Action commands use zero-based slots; keyboard labels are one-based. To cast the current Fireball prototype:

1. Select a living hostile target with left mouse button.
2. Press `5`, which is bound in `games/world-of-warcraft/share/config.cfg` as `cmd wow_action 4`.
3. Remain stationary until the cast bar completes. Starting while moving is rejected; moving afterward cancels without
   spending mana. Escape sends `stopattack`, which cancels an active cast and clears combat targeting.
4. At completion the server spends mana, launches the missile, and starts the release animation.

`6` is bound as `cmd wow_action 5` for Frostbolt. Tab is `cmd wow_cycle_target`. Camera look is `bind MOUSE2 "+look"` (MOUSE3 too). Wheel zoom is the generic client `zoom` command (`bind MWHEELUP "zoom 1"`). The Mage action-bar payload exposes these icons; the current command handler does
not yet validate the character's spellbook/class, so class authorization remains required work.

## Implemented Prototype Spells

| Key / slot | Current name | Cast | Mana | Projectile | Effect | Status |
|---|---|---:|---:|---:|---|---|
| `4` / 3 | Healing Touch | instant | 8 | none | self-heal 2 | Prototype; not DBC-driven |
| `5` / 4 | Fireball | 1.5 s | 10 | 25 units/s | 2 fire damage | Cast, GCD, homing missile, impact event |
| `6` / 5 | Frostbolt | 2.5 s | 15 | 20 units/s | 3 frost damage, 50% slow for 2 s | Cast, GCD, homing missile, impact event |

These are engine prototypes, not exact WoW ranks. In the installed `Spell.dbc`, spell 133 is `Fireball (Rank 1)` and spell
116 is `Frostbolt (Rank 1)`, but `g_wow.c` does not consume either record yet. The Fireball code retains legacy internal
`firebolt` function/field names; do not describe its hardcoded numbers as an exact Classic rank. The next data-model step
is a spell definition table populated from `Spell.dbc`,
`SpellCastTimes.dbc`, `SpellRange.dbc`, and spell visual data.

Useful local checks:

```sh
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\Spell.dbc' 16332
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ str 'DBFilesClient\Spell.dbc' 74 112  # Fireball
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ str 'DBFilesClient\Spell.dbc' 64 112  # Frostbolt
```

## Server Cast Contract

`wowEntityLocal_t` owns the authoritative state:

- `cast_spell`, `cast_duration`, `cast_remaining`, `cast_target`, and `cast_origin` describe preparation.
- `cast_release_time` locks the short post-launch release sequence.
- `gcd_time` begins when a valid cast begins. A cast longer than 1.5 seconds therefore finishes after its GCD expires.
- Mana is validated at begin and spent only at completion.
- Movement or target death cancels preparation. An accepted spell interrupts an active melee swing.
- `WOW_STAT_CAST_PROGRESS` and `WOW_STAT_CAST_MAX` replicate display state; zero max hides the bar.

The transition is:

```text
idle -> ReadySpellDirected -> launch + SpellCastDirected -> idle/combat
              | movement, invalid/dead target
              +-----------------------------------------> idle
```

The release sequence is deliberately distinct from preparation. Do not use `Attack1H` as a spell fallback and do not let
auto-chase overwrite the release animation on the launch frame.

## Animation IDs and Release Timing

WoW character M2 sequence IDs used here are:

| ID | Loader name | Use |
|---:|---|---|
| 53 | `ReadySpellDirected` | looping preparation for a unit-targeted spell |
| 54 | `ReadySpellOmni` | looping preparation without a direction |
| 55 | `SpellCastDirected` | unit-targeted launch/release |
| 56 | `SpellCastOmni` | non-directed launch/release |

The OrcMale archive proves sequences 53 and 55 exist. Inspect another character with:

```sh
build/bin/m2tool -mpq data/world-of-warcraft/model.MPQ \
  -model 'Character\Orc\Male\OrcMale.m2' --dump-all
```

The authoritative projectile launches when `cast_remaining` reaches zero—the start of `SpellCastDirected`. M2 animation
events may later refine client-only hand glow or sound timing, but must not delay authoritative damage/spawn semantics.

## Projectile Source and Destination

`data/whoa-master/src/component/Types.hpp` defines M2 attachment 1 as right hand and 2 as left hand. Attachment 11 is the
head; using it produced the visibly high Fireball launch. The current server seeds Z from right-hand attachment 1's local
position and falls back to the caster gameplay radius. It homes toward target attachment 20 (chest) when available, otherwise
using twice the target gameplay radius as a gameplay-space chest approximation.

This is only the server/gameplay approximation. Exact visuals require the renderer's animated attachment matrix:

1. At launch, evaluate `M2_AttachmentMatrix(..., 1, ...)` for the caster's current release frame.
2. Seed the client projectile visual from that world-space hand transform.
3. Aim at a renderer-owned impact/chest tag on the target and update that destination while homing.
4. Keep server hit testing in gameplay space; renderer bone matrices must not cross into the game module.

Warcraft-Arena-Unity uses the same separation: `Projectile.DeterminePositioning()` resolves a configured launch tag from
the caster renderer, while `SpellVisualProjectile.UpdateDestination()` tracks the target's `Impact` tag. Its default launch
tag is a hand, not a hardcoded world-height offset.

## Cast Bar UI

The current bar in `game/g_ui.c` fills from `1 - remaining / duration` and hides when max is zero. A complete WoW-style bar
still needs replicated spell identity so it can show the spell name and icon, plus explicit completed/interrupted outcomes.
Warcraft-Arena-Unity's `CastFramePresenter` is the reference pattern: visibility follows casting state, progress is derived
from remaining/total time, and spell identity selects localized label and icon.

Do not infer casting from the local key press. The server's replicated cast state is authoritative, so rejected casts never
show a false bar and remote-unit cast bars can use the same contract.

## Movement Facing and Directional Animation

The player's facing is derived server-side from the movement direction vector (`atan2(dir.y, dir.x)` in `Wow_RunFrame`,
`game/g_wow.c`), so strafing turns the model sideways toward its direction of travel instead of pinning it to the camera yaw.
Backpedal (S without W, including back-strafe diagonals) keeps facing forward, matching `Wow_SetDirectionalMove`'s backpedal
rule; auto-chase leaves the input vector zero-length so facing stays on the camera. A/D moves laterally and uses the normal
locomotion animation; `ShuffleLeft`/`ShuffleRight` are turn animations and must not be used for strafe input. Pure S movement
selects `WalkBackwards`; forward or diagonal movement selects `Run`.

## References to Copy Deliberately

- [TrinityCore spell lifecycle](https://github.com/TrinityCore/TrinityCore/tree/master/src/server/game/Spells): validation,
  preparation, launch, effects, power costs, interrupts, and cooldown ownership.
- [AzerothCore WotLK DBC schema](https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/shared/DataStores/DBCStructure.h):
  `SpellEntry` and `SpellCastTimesEntry`; verify the installed client build before copying field offsets.
- [Warcraft-Arena-Unity Spell.cs](https://github.com/Reinisch/Warcraft-Arena-Unity/blob/master/Assets/Scripts/Core/Spells/Spell.cs):
  cast validation, movement interrupt threshold, launch transition, delayed processing, and costs.
- [Warcraft-Arena-Unity SpellCast.cs](https://github.com/Reinisch/Warcraft-Arena-Unity/blob/master/Assets/Scripts/Core/Spells/Spell%20Processing/Cast/SpellCast.cs):
  authoritative versus replicated display cast state.
- [Warcraft-Arena-Unity Projectile.cs](https://github.com/Reinisch/Warcraft-Arena-Unity/blob/master/Assets/Scripts/Client/Projectiles/Projectile.cs):
  renderer launch tags, projectile movement, hit scan, and impact handoff.
- [Warcraft-Arena-Unity cast UI](https://github.com/Reinisch/Warcraft-Arena-Unity/blob/master/Assets/Scripts/Client/UI/Panels/Battle/Unit%20Frames/CastFramePresenter.cs):
  bar visibility/progress and spell name/icon binding.

## Required Tests

Every cast change must cover successful completion and cancellation. Changes to targeting or resources must additionally
cover dead/invalid targets, insufficient mana, GCD rejection, and melee interruption. Projectile changes must cover spawn,
homing, hit, target removal, source height, and both Fire/Frost effect paths.
