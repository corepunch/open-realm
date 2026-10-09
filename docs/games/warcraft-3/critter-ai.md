# Critter Wandering (`Awan`)

`Awan` makes idle neutral critters take short autonomous walks and run from a
hit. The ability owns only scheduling, the flee decision and the identity of
its private destination; the standard Move ability owns routing, collision and
arrival. Implementation: `games/warcraft-3/game/skills/s_wander.c`, registered
as `{ "Awan", CAbilityWander, AB_PASSIVE | AB_INNATE }` in `skills/s_skills.c`.
Custom rawcodes whose `code` column is `Awan` are found through
`S_ResolveAbilityAlias(unit, 'Awan')`, not by matching the stock rawcode only.

## Contract

- An eligible idle unit with `Awan` starts a Move to a random nearby point
  every 8-10 s. There is no spawn leash; destinations are relative to the
  current position.
- While `Awan` is present the unit never auto-acquires enemies and never
  retaliates (`A_IDLE` is consumed, `A_NO_RETALIATE` returns true). Enemies
  may still target the critter.
- A hit that the unit survives makes it walk away from the attacker unless an
  explicit order (move, attack, cast, build, harvest, patrol) is active. A
  flee may interrupt an earlier autonomous Move.
- `Awan` only ever cancels movement it started. External orders, including a
  Move whose procedure happens to be the same walk behaviour, are untouched.
- No JASS order events are synthesized for autonomous Moves.

## Eligibility

`wander_eligible()` requires the ability present, the unit alive, not paused,
stunned, training, under construction, `AI_IMMOBILE`, holding position,
cycloned, entangled, ensnared or Purge-immobilized, no queued orders, and
either no active order or an active order that `Awan` itself owns.

## Scheduling

| Constant (`s_wander.c`) | Value | Meaning |
|---|---|---|
| `WANDER_MIN_DELAY_MS` + `% WANDER_DELAY_SPREAD_MS` | 8000 + [0, 2001) ms | pause before the next autonomous decision |
| `WANDER_MIN_DISTANCE` + `WANDER_DISTANCE_SPREAD` | 64 + [0, 192) units | destination radius |
| `WANDER_ATTEMPTS` | 6 | destination rolls per decision; all rejected means wait for the next deadline |
| `WANDER_BUILDING_CLEARANCE` | 32 units | extra gap kept from live building footprints |
| `WANDER_FLEE_DISTANCE` | 192 units | first retreat length, halved per failed attempt |
| `WANDER_FLEE_ATTEMPTS` | 4 | retreats tried: 192 / 96 / 48 / 24 units |

`wander_next_time` is re-armed (`wander_schedule()`) whenever a decision is
taken, at `A_MOVE_ARRIVE`, at `A_MOVE_BLOCKED`, and after a flee starts, so a
finished or failed journey always begins a fresh pause. Randomness comes from a
per-unit LCG (`wander_random_state`) seeded from the edict number and spawn
time, never from the process RNG or wall clock, so replays and saves are
deterministic. `A_IDLE` is dispatched by `ai_stand()` in `g_ai.c`; the first
idle tick only arms the deadline.

Destination rules: `M_MoveIsValid()` (terrain, baked footprints, swept unit
collision) plus `wander_clear_of_buildings()`, which rejects any point closer
than `unit->collision + building->collision + WANDER_BUILDING_CLEARANCE` to a
live `EF_BUILDING` edict. Every building type counts, not just lumber mills;
destroyed structures (`M_IsDead()`) still occupy their edict but do not.

## Move ownership and ability events

Each wandering unit keeps one private destination edict, `wander_waypoint`
(`SVF_NOCLIENT`, allocated by the first autonomous Move, freed on disable,
death or removal). The shared `Waypoint_add()` ring is never used because a
ring slot can be overwritten while a Move still references it. `wander_goal`
plus `wander_goal_generation` form the ownership token: `wander_owns_move()`
is true only when `goalentity == wander_goal`, the generation matches the
waypoint's current `waypoint_generation`, and the unit is in the ordinary walk
move. A private waypoint is never rewritten while a Move still targets it.

| Event | `CAbilityWander` behaviour |
|---|---|
| `A_IDLE` | consume while present; arm or evaluate the deadline; start a Move |
| `A_NO_RETALIATE` | true while present |
| `A_DAMAGED` | `wander_on_damage()`: flee (see below) |
| `A_ORDER_ACCEPTED` | clear the ownership token (an accepted order may keep the same move object) |
| `A_MOVE_START` | clear ownership unless `call->move_target` is the private waypoint |
| `A_MOVE_ARRIVE` | if the arriving Move targets the private waypoint: clear ownership, re-arm |
| `A_MOVE_BLOCKED` | `wander_recover_blocked_move()`: return to idle instead of terminal Hold, re-arm |
| `A_MOVE_LEAVE` | clear the ownership token |
| `A_DISABLE`, `A_DEATH`, `A_UNIT_REMOVE` | stop an owned Move (`unit_stand_no_queue`), detach `goalentity`, free the waypoint, reset the deadline |

`A_MOVE_BLOCKED` is raised by `move_hold()` in `skills/s_move.c` before Move
installs its terminal Hold pose. The handler checks the live `goalentity`
against `wander_waypoint`, not the token, because `A_MOVE_LEAVE` or an accepted
order may already have retired the token while the route was still in flight.
It returns false for external Moves so their Hold semantics are unchanged.

## Flee

`T_Damage()` in `skills/s_attack.c` dispatches `A_DAMAGED` only after the hit
has been applied to a surviving unit; a killing blow goes straight to
`die()`, so no flee waypoint is allocated for a corpse. `wander_on_damage()`
needs a live attacker, eligibility, and no active order other than an owned
Move. It retires an owned Move first, then walks away along
`Vector2_sub(unit, attacker)` (a random heading when the attacker is
coincident), trying `WANDER_FLEE_DISTANCE / 2^i` for `WANDER_FLEE_ATTEMPTS`
attempts; each candidate passes `M_MoveIsValid()` and the building clearance.

## Save and load

| `edict_t` field (`g_local.h`) | `g_save.c` | Role |
|---|---|---|
| `wander_next_time` | `F_INT` | next decision deadline (`level.time` ms) |
| `wander_random_state` | `F_INT` | per-unit LCG state |
| `wander_goal` | `F_EDICT` | ownership token target |
| `wander_waypoint` | `F_EDICT` | private destination edict |
| `wander_goal_generation` | `F_INT` | ownership token generation |

A save taken mid-Move restores the private waypoint as the live `goalentity`
and the Move resumes through the normal think callback.

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_wander.*'
make test-wc3-engine WC3_PATTERN='wc3_spell.*aura*'   # aura target masks incl. neutral slots
make test                                             # full matrix before committing
```

`games/warcraft-3/game/tests/t_wander.c` covers direct dispatch (schedules,
ownership tokens, disable/death cleanup, save round trips) and the real
scheduler: `frame_scheduler_*` tests advance `level.time` and call
`globals.RunFrame()` so `G_RunEntities`, path jobs and the full server frame
run. They prove the 8-10 s deadline, Move start and arrival re-arm, a player
Move that outlives every deadline and a hit, a flee started by the landed-attack
path (`S_ResolveAttackHit` -> `T_Damage` -> `A_DAMAGED`), and recovery from a
genuinely blocked route (a solid unit parks on the destination after the Move
starts).

## Known pitfalls

- A frame-driven test must load a JASS VM (`run_test_jass("function main
  takes nothing returns nothing\nendfunction\n")`): `G_RunFrame()` pumps
  `jass_runevents(level.vm)` unconditionally and crashes on `NULL`.
- Test colliders need `s.model = 1`; `IS_HOLLOW()` ignores model-less edicts.
- `M_MoveIsValid()` sweeps the whole origin-to-destination segment, so a
  unit already on the line rejects the destination up front. A blocked route
  can only be produced by an obstacle placed after the Move has started, and
  Move only settles as blocked inside its near-goal band
  (`move_distance + collision + MOVE_SLOT_MARGIN`); a distant blocker keeps
  the order alive by design.
- Compare waypoint identity by generation, never by pointer alone.
- Never free the private waypoint while `goalentity` still points at it; the
  disable path stands the unit down first.

## Retail fidelity

The intervals, radius and retreat lengths above are provisional
approximations, not measured classic-retail constants. Stop/Hold ordering,
proximity-only fleeing, special-case critter behaviour and JASS order-event
compatibility have not been compared against retail.
