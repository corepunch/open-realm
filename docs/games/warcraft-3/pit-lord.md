# Pit Lord (`Nplh`) — Howl, Cleave, Rain of Fire and Doom (Patches 1–4)

## Source evidence and scope

The stock MPQ/MDX inspection supplied for this task identifies `Nplh` with
`ANrf` (Rain of Fire), `ANht` (Howl of Terror), `ANca` (Cleaving Attack) and
`ANdo` (Doom). Patch 1 implements Howl target presentation and secondary-hit Cleave
presentation/eligibility. Patch 2 implements Rain of Fire wave and burning
gameplay. Patch 3 supplies Rain of Fire's data-selected wave art and sounds.
Patch 4 implements Doom gameplay and target/death art.

| Ability | Stock presentation data | Runtime ownership |
| --- | --- | --- |
| `ANht` | `CasterArt=Abilities\Spells\Other\HowlOfTerror\HowlCaster.mdl` | One-shot on caster via `G_SpawnAbilityEffectTarget` |
| `BNht` | `TargetArt=Abilities\Spells\Other\HowlOfTerror\HowlTarget.mdl` | Persistent per-unit `BNht` status effect, using default attachment (no authored `Targetattach`) |
| `ANca` / `ACce` | `SpecialArt=Abilities\Spells\Other\Cleave\CleaveDamageTarget.mdl`, `Specialattach=chest` | Temporary effect on each eligible **secondary** melee hit |

The reported Howl caster model has a single nonlooping `Birth` (~0.87 s).
The Howl target model has `Birth`, looping `Stand` and short `Death` sequences.
The Cleave effect has a nonlooping `Birth` (~0.5 s). These sequence descriptions
come from the MPQ/MDX report; the test fixture model is a synthetic animation
model, not the retail artwork. Animation selection and physical orientation still
need visual in-game verification.

## Gameplay and lifecycle

- `AbilityHowlOfTerror_Execute` reads area, duration, level and `BNht` from
  `AbilityData` and applies its authored *ground/enemy* target mask through
  `S_SpellAllowsAreaTarget`. Status application uses
  `S_SpellApplyTimedTargetStatus` so `BNht.TargetArt` belongs to the status,
  not to the cast. It also respects `HeroDur` when a target is a Hero.
- The incoming master series already installed `BNht` attack damage reduction
  through `WC3_STATUS_MOD_ATTACK_DAMAGE_PERCENT` and spawned `ANht.CasterArt`.
  Those remain untouched. Refresh/recast reuses a single attached visual per
  target and status ID; removal on dispel, expiry or death uses
  `unit_removestatus`/`unit_statusdeath` and `G_DestroyStatusEffectTarget`.
  When no `TargetArt` is configured, gameplay still applies the debuff.
- `S_ResolveAttackHit` applies Cleave only for melee weapon profiles. The
  secondary loop applies the authored `targs` mask (stock
  `ground,enemy,neutral`), excludes the primary target and attacker, and uses
  `DataA` as the physical splash fraction and `Area` as the impact radius.
  Cleave damage is a physical attack effect: spell immunity is **not** a reason
  to discard an otherwise eligible secondary target. Other targeting rules,
  including ethereal immunity to physical attacks, remain in effect.
- Each secondary hit requests `WC3_EFFECT_SPECIAL` from the applying ability
  rawcode, with the alias's `Specialattach`, then its base ability's field, and
  finally the stock `chest` socket when neither supplies it. One-shot art does
  not change damage, spawn gameplay units or introduce persistent state.

## Verification and known boundaries

`games/warcraft-3/game/tests/t_spell.c` includes:

- `howl_of_terror_reduces_enemy_attack_damage_by_authored_25_percent` —
  incoming-series damage modifier, now with grounded test targets.
- `pit_lord_howl_target_art_tracks_status_refresh_and_expiry` — one status
  visual, authored area filtering, fresh/recast duration, dispel and expiry.
- `pit_lord_cleave_hits_only_eligible_ground_secondaries_with_chest_art` —
  ground-only physical splash, no duplicate primary art, no allied/air/structure
  or distant victim, socket metadata, and absence of splash from ranged hits.

`TestUI\Models\anim_pulse.mdx` is loaded from the test archive. No retail MPQ
is required for automated regression tests. The actual retail caster animation
(`spell,slam`), model rendering, per-effect scaling/orientation and target
attachment geometry need short visual inspection after user compilation.

No saved persistent fields, callback function registrations or network state
are added by Patch 1. Existing status and effect serialization applies.

## Rain of Fire (`ANrf`), Patch 2 — gameplay

The stock MPQ report identifies `ANrf` levels 1–3 with `DataA` waves
(6/8/10), `DataB` wave damage (25/30/35), `DataC` visual shard counts
(6/7/10), `DataD` building multiplier (0.5), `DataE` burn DPS (5/10/15),
and `DataF` aggregate damage per wave (125/150/175). `Dur` is the 3-second
**total channel duration**, not the delay between waves. The Warcraft III
Pit Lord guide explicitly states that Rain of Fire damages friendly units and
that landed waves burn for three seconds. Creep aliases `ACrf` and `ACrg`
use their own authored cells; with missing `Dur`, the implementation uses
positive `Cast` as the wave interval, or a one-second conservative fallback.

Execution starts with one wave at cast time and schedules further waves at
`Dur / DataA` intervals using an absolute `freetime` deadline. `resources`
holds remaining waves, `variation` the cast rank, `damage` the wave damage,
`velocity` the cap, and `wait` the interval. The last wave retains its
channel until the authored duration elapses. Cancellation, stun, death or
a different cast retires the wave thinker; a retired thinker cannot stop a
new channel because the shared serial guard remains authoritative.

Each wave finds live, in-world, non-invulnerable, non-cycloned, non-spell-
immune targets inside its radius. Rain of Fire intentionally ignores the
friend/enemy relationship part of the ordinary target mask, but continues
to honor its authored target *types*. This can damage the caster, allied
units and enemies alike. After counting eligible targets, `DataF` scales the
per-target damage down if needed; `DataD` is applied to structures **after**
this cap. Integer hit amounts are truncated rather than rounded up above
the cap. Spell immunity and the central spell-damage boundary still apply.

A successful hit on a surviving non-structure with a positive `DataE`
creates one independent, save-safe burn thinker. Each landed wave can
therefore contribute its own three one-second damage ticks, including when
an earlier cast is interrupted. The burn thinker stores fixed DPS, target
and caster incarnations via `S_SpellIdentityThinker`; each tick revalidates
the target, and a recycled caster slot cannot claim the original burn.
These thinkers do not follow the caster's current channel or hero level.

**Patch 3 — presentation:** Each completed wave resolves `AbilityData.EfctID`
(stock `XErf`) and spawns `DataC` temporary, non-channel-owned
`EffectArt` instances at bounded points in the authored area. The stock
`XErf` effect object names `RainOfFireTarget.mdl` (one nonlooping `Birth`,
about 3.3 seconds). A local unsigned integer sequence derived from the wave's
saved identity and remaining wave count spreads the visuals across the area,
without consuming global combat RNG or changing area damage. It caps malformed
custom-data visual counts at 64 per wave to protect the entity pool. A missing
model does not affect any damage or burning.

The effect object's `EffectSound` (stock `RainOfFireWave`) is played **once
per wave** at the selected ground point; an explicit ability sound takes
precedence for custom maps. The separate `EffectSoundLooped` (stock
`RainOfFireLoop`) is attached to the existing channel thinker. On Stop,
interruption, death, replacement cast or natural completion, removal of that
thinker stops the loop. Already-landed temporary Birth effects and independent
burn thinkers finish after the channel ends. Both sound aliases are resolved
through the existing `AbilitySounds` table and server-side sound registration;
no ability-specific path is hardcoded. The MPQ report confirms the aliases,
but one-sound-per-wave versus one-per-shard cadence and exact scatter pattern
remain retail-observation questions.

`BNrd`/`BNrf` are the authored `BuffID` values but the supplied asset report
identifies **no target model mapping for either**. The presentation patch does
not invent persistent target art or create a buff merely for its icon. Burning
damage remains independent of the generic 16-slot status inventory: a full
inventory cannot silently cancel paid spell damage, but dispelling the visual
buff is **not yet wired** to the independent burn thinker. Exact original
burn/dispel and overlapping-cast stacking restrictions warrant retail
verification. `DataC` changes presentation count, not gameplay hit count.

Regression tests use nonstock `DataA/B/D/E/F` values to verify initial and
subsequent wave deadlines, friendly fire, building reduction, area
exclusion, per-wave cap, independent burning after channel interruption
and burn-thinker save/load. Patch 3 adds model/sound resolver tests that
provide synthetic `XErf` and `AbilitySounds` SLK rows, plus a no-model damage
regression. No retail MPQ is required by those fixtures.

## Doom (`ANdo`) — Patch 4

Doom is a dedicated `CAbilityDoom` procedure, no longer the generic timed
status application. The stock `ANdo` row has `DataA=40` damage per second,
`DataB=1` (interval metadata), `DataC=120` seconds of summoned Doom Guard
lifetime, `DataD=5` maximum neutral-aggressive creep level, `UnitID=nba2`,
and `BuffID=BNdo,BNdi`. The reported classic guide's 180-second summon lifetime
conflicts with the supplied stock SLK `DataC=120`. This implementation follows
the active **authored row**, not a fixed 180-second duration. Custom map rows can
change the summon type and duration.

- Validate a live enemy, the authored target mask, resistant/hero exclusion,
  nonmechanical, nonstructure and nonillusion eligibility and the neutral creep
  level ceiling. Check recipient status capacity **before** spell costs commit.
- Apply permanent `BNdo` with `WC3_STATUS_STATE_SILENCED`, the negative/magical
  and undispellable flags, and status-owned `BNdo.TargetArt` (default `origin`
  attachment in the absence of a `Targetattach`). A victim cannot cast, and
  magical dispels cannot remove the curse. Cancellation of an already active
  channel is delegated to `S_SpellCancelChannel`.
- `next_tick` schedules one second pulses of `DataA` damage. Persistent statuses
  (`timestamp == 0`) with an explicit `next_tick` participate in the shared
  event-driven status scheduler; no new entity thinker or serialized callback
  is needed. Ordinary timed statuses continue to use their original path.
- `unit_statusdeath` dispatches Doom's `A_STATUS_DEATH` once, whether death
  came from the curse or another source. It retires the status **before**
  generating effects and summoning, so recursive death events cannot spawn
  duplicate minions. `BNdi.EffectArt` supplies the one-shot death visual.
  `S_SummonAbilityAt` creates the authored unit at the victim position, records
  the creating ability and applies `DataC` timed life. When the original caster
  entity is gone, its player ID retained in the status supplies fallback
  ownership; the invalid source pointer is never followed.
- The original `source_ability`, `source_spawn_time`, rank, player and `next_tick`
  are held in the existing saved status layout. No save-format version bump is
  required because no persistent field was added.

Tests cover persistence without a timer expiry, the silence and undispellability
contract, exact one-second damage ticks, one summon following ordinary death,
source-player ownership when the caster is removed, and save/load with a live
permanent tick. A synthetic `hfoo` summons in the test fixture in place of the
retail `nba2` asset. Rendering of `DoomTarget.mdl` and `DoomDeath.mdl` still
requires user-side visual verification, especially `BNdi` animation placement
and its short 4.5-second `Birth` lifecycle. No runtime tests were run here.
