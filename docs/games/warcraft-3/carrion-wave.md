# Travelling Carrion Swarm and Crushing Wave

`games/warcraft-3/game/skills/s_area_spell.c` owns the travelling wave
shared by Carrion Swarm (`AUcs`/`ACca`) and Crushing Wave (`ACcv`/`ACc2`/`ACc3`).
This is distinct from Shockwave (`AOsh`), whose corridor damage is immediate.

## Authoritative gameplay

- The clicked point determines direction only. Ability `DataC` determines the
  travelled distance, `Area` the initial half-width, `DataD` the final
  half-width, `DataA` damage per eligible target, and `DataB` the total damage
  cap (zero means uncapped).
- The wave keeps `carrionWave_t` state on an ordinary fixed-target projectile
  edict with a registered `carrion_wave_think` save callback. Each game tick,
  projectile physics moves it; `carrion_wave_think` then resolves the finite
  swept interval from `previous_position` to its current position. The initial
  and final widths are interpolated by distance travelled, not frame count.
- Eligible target **centres** must project within the current finite swept
  interval; the lateral test includes the target collision radius. No unit
  behind the current segment, ahead of it, or beyond `DataC` is eligible on
  that tick simply because it is near the infinite travel line.
- When several targets occupy a single swept interval, the wave resolves them
  in increasing projected travel distance, then by edict index for exact
  ties. This controls which targets consume `DataB` first. Targets already
  hit are remembered by saveable `CWht` identity markers and cannot be hit
  again by later segments of the same wave.
- For performance, the collision pass snapshots the edict count, reconstructs
  the wave's prior hit ledger in one pass, gathers candidates with stable entity
  identity and projected travel distance, then sorts once before resolving
  impacts. Candidates are revalidated before damage because earlier impacts can
  move, remove, or otherwise invalidate later candidates. Allocation failure
  falls back to the deterministic nearest-first scan so a spell still delivers
  damage. This keeps the normal path linearithmic in the number of candidates
  instead of repeatedly rescanning every edict for each impact.
- The server never requires missile art to deal damage. `MissileArt` may be
  missing, empty, or fail model registration; the wave then travels
  invisibly. A missing, zero, negative or nonfinite `Missilespeed` uses a
  1100-units/second fallback to keep the simulation moving. The art and
  presentation state do not determine hit eligibility. Do not substitute
  a synchronous blast when data is missing.
- The caster's spawn identity and wave identity protect recycled entity
  slots; the parent wave and its hit markers retain the existing save/load
  components and `F_CFUNCTION` callback registration. No network or save
  schema change is required for this correction.

## Regression coverage

`games/warcraft-3/game/tests/t_spell.c` drives point-cast and `G_RunEntities`
from the production path:

- `carrion_swarm_travels_authored_distance_and_hits_once_with_expanding_width`:
  first-tick future targets, behind-caster targets, beyond-distance targets,
  widening area and once-only hits.
- `crushing_wave_without_missile_art_or_speed_still_damages`: empty art and
  zero speed still advance an invisible wave and deal the authored damage.
- `carrion_swarm_spends_authored_maximum_damage_in_travel_order`: three targets
  allocated in reverse spatial order are encountered in one tick, receiving
  the capped 50/50/20 damage amounts in spatial order.

The optimized collision ordering retains the deterministic travel-distance
then edict-index tie break. Gameplay damage remains server-authoritative;
client effect-model index bounds only protect presentation from invalid
snapshot indices.
