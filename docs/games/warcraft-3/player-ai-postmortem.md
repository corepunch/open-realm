# Warcraft III Player AI Postmortem

## Outcome

Warcraft III player AI is implemented as a script-compatible controller over the normal server simulation. Blizzard's archived
`common.ai` and race or campaign `.ai` files retain ownership of build policy, economy targets, hero selection, army composition,
attack timing, and captain sequencing. The C game module provides the player-bound state queries, native actions, cooperative
scheduling, and lifecycle services those scripts expect.

The result is not a second strategy engine written in C. It is an executor for Blizzard's strategy programs:

```text
Blizzard.j / war3map.j
        |
        | StartMeleeAI / StartCampaignAI / CommandAI
        v
player-owned AI JASS VM
common.j -> common.ai -> requested .ai
        |
        | facts, requested actions, sleeps, commands
        v
authoritative C game services
        |
        v
construction, training, harvesting, movement, combat, resources, pathing
```

This division is the principal architectural result of the work.

## What The Scripts Establish

The `.ai` files expose an engine ABI, not a self-contained simulation:

| Contract | Representative natives | Owner |
|---|---|---|
| Scheduling | `StartThread`, `Sleep` | AI VM host |
| Queries | `GetUnitCount`, `GetTownUnitCount`, `GetGoldOwned`, readiness queries | Server state |
| Requested actions | `SetProduce`, `HarvestGold`, `AddAssault`, `SuicidePlayer` | Authoritative gameplay systems |
| External interruption | `CommandAI`, `CommandsWaiting`, `GetLastCommand`, `GetLastData`, `PopLastCommand` | Map VM to player AI |

`Blizzard.j` starts one race-specific script for every playing computer slot. `StandardAI` starts independent worker and attack
threads. `CampaignAI` starts recurring economy and build threads. The scripts therefore require player-private globals and
cooperative coroutines; a shared AI root or a separate fixed C think scheduler would change authored behavior.

The ordinary AI is polling-driven:

- `BuildLoop` reconciles the desired production list every two seconds.
- `CampaignBasics` periodically reapplies harvesting, defender, farm, and guard-post policy.
- Race attack scripts wait for completed heroes and army thresholds with `Sleep` loops.
- `SuicideOnPlayer` blocks while forming a captain, launching it, and waiting for combat or captain-empty conditions.

This polling is intentional. Engine events may reduce response latency, but they must not replace or reorder script timing.

## Events, Commands, And Orders

Three mechanisms have distinct roles.

### Coroutine Events

`jass_runevents()` resumes runnable and expired sleeping coroutines. `G_RunFrame()` pumps the map VM and then each player AI VM
before client and entity simulation. This is the scheduler behind `StartThread` and `Sleep`; calling it an event queue is accurate
at the VM level, even though AI policy mostly consists of timed polling loops.

Each bot needs its own JASS root because `common.ai` stores mutable policy in globals. The roots currently execute serially because
host context remains process-global. Pause, replacement, removal, restart, and map shutdown must affect only the selected root and
must release all its programs, globals, coroutine stacks, and native-owned state.

### `CommandAI`

`CommandAI(player, command, data)` is an authentic map-to-AI interrupt channel. `common.ai` polls it in `WaitForSignal`,
`SuicideSleep`, and assault loops so a map can interrupt long waits or redirect scripted progression.

The paired command and data values belong to a player-owned ordered collection. `GetLastCommand` and `GetLastData` observe the same
newest entry until `PopLastCommand` removes it. It is not a general unit-order bus and must not be represented as one overwriteable
flag.

### Gameplay Orders

Routine production, harvesting, movement, and combat do not pass through `CommandAI`. AI natives directly request authoritative
game actions. They also should not synthesize UI or network client commands. Instead, player UI and AI adapters converge on shared
operations such as construction initiation, training, point orders, and target orders.

The shared operation performs ownership, visibility, capability, prerequisite, resource, food, placement, pathing, queue, and
lifecycle checks. Once accepted, ordinary entity state machines execute the action while the script sleeps. This matches the
observable native contract even though Blizzard's internal C structures are not available for comparison.

## Decisions That Held Up

### Keep Strategy In JASS

Following the archived scripts avoided a parallel and divergent C planner. Build ordering, virtual resource reservation, count
equivalence, difficulty quantities, wave timing, and readiness semantics remain authored data and code.

`SetProduce` is a desired-count request, not permission to spawn units. `common.ai` computes the unmet count and a pass-local
affordable quantity. The native still performs real payment and legality checks, and later script passes retry deficits.

### Use Private Player VMs

Private roots preserve per-player build arrays, hero functions, timers, commands, captain state, and sleepers. Explicitly binding
the current player while a coroutine runs lets the same native table serve every bot without leaking policy state between players.

### Reuse Authoritative Gameplay Paths

Routing AI requests through normal gameplay exposed real lifecycle defects rather than hiding them behind bot-only spawning:

- accepted pending buildings must count toward `GetUnitCount`, but not `GetUnitCountDone`;
- pending buildings need serialized placement until their footprints exist;
- hidden queued trainees must not animate, harvest, or lose their queue links;
- active and pending builders must not be reassigned by recurring harvest reconciliation;
- replacing a miner's order must release mine membership first;
- routing clearance and interaction distance are separate values;
- captain membership must coexist with normal entity combat and death state.

These were gameplay lifecycle bugs. Fixing them improved the common simulation rather than creating AI exceptions.

### Treat Archive Selection As Runtime Correctness

ROC `common.ai` cannot be mixed with incompatible localized or TFT race scripts. ROC hides only `Scripts/*.ai` from
`War3Local.mpq`, while retaining localized non-AI data. The policy applies to streaming and whole-file reads. TFT keeps normal
expansion precedence.

## What Cost Time

### Script Survival Was Mistaken For Gameplay Progress

A run without an unresolved native or JASS error proves parser and ABI coverage only. It does not prove that resources recur,
workers remain assigned, buildings complete, heroes train, captains fill, or attacks reach an opponent. Runtime milestones must
therefore be behavioral: repeated deposits, accepted and completed construction, queue completion, hero completion, captain
formation, attack orders, contact, and post-combat progression.

### Integration Testing Started Too Late

Small API tests established local contracts, but only a real melee map exposed interactions among pathing footprints, harvest
reassignment, production counts, construction workers, and long-running script loops. Rivercross by Bernard became the preferred
two-player ROC integration map because it reaches these systems with little unrelated content.

### The Initial Bound Measured The Wrong Clock

`com_frame_limit` counts main-loop iterations, not 10 Hz server ticks. Uncapped hidden runs could terminate after thousands of loop
iterations while advancing little simulated time. `-com_fast_forward` or `+set com_fast_forward 1` contributes one fixed 100 ms
`FRAMETIME` per loop, making bounded AI diagnostics deterministic. Do not use it for rendering, networking, input, or frame pacing.

### Reconciliation Was Misread As Replacement

`ClearHarvestAI` clears the current assignment pass, not every worker's active harvest order. Reissuing movement to workers already
inside mines reset their lifecycle and prevented deposits. The correct implementation reserves matching active workers first, then
assigns only the remaining demand.

### Generic Routing Budgets Leaked Into Gameplay Semantics

Builders and miners initially used generic point routing whose shared work budget could be exhausted after building footprints
changed. Collision-sized resumable routing fixed the underlying contract. Using a structure's interaction range as the mover radius
was also incorrect: arrival distance and physical route clearance are independent.

## What Is Proven And What Is Inferred

The archived scripts prove the host ABI, player ownership, cooperative scheduling, command interruption, policy/action split, and
observable behavior expected from native services. They strongly support an original implementation with analogous native AI
state and engine action adapters.

They do not reveal Blizzard's private C data structures, exact queue allocation, pathfinding implementation, or internal frame
ordering. Our `bot_t`, arrays, edict references, and shared order functions are OpenWarcraft implementation choices. Fidelity should
be judged by script-visible semantics and gameplay outcomes, not presumed source-level similarity.

## Rules For Future Work

1. Treat `common.ai`, the active race or campaign script, `Blizzard.j`, and map `war3map.j` as the behavioral source of truth.
2. Keep strategy, timing, desired counts, and attack sequencing in JASS.
3. Implement narrow player-bound query and action natives; do not add a competing C planner.
4. Use `CommandAI` only for its authored map-to-AI interrupt role.
5. Route accepted actions through shared authoritative gameplay operations, never direct spawning or UI command synthesis.
6. Preserve polling semantics. Event observation may accelerate wakeups only when dropping an event self-recovers on the next poll.
7. Test accepted/pending and completed production states, plus active and idle worker assignment states.
8. Validate structural changes in ROC and TFT, then run a real bounded map with `-com_fast_forward`.
9. Require economy, base, hero, army, and assault milestones before declaring an AI path playable.
10. Diagnose the first failed lifecycle transition; do not hide it with a fallback or bot-specific exception.

## Related Documentation

- [Player AI implementation and verification](player-ai.md)
- [Economy and unit presentation](economy-and-unit-presentation.md)
- [Building construction](building-construction.md)
- [Build and repair routing](build-repair-routing.md)
- [Pathfinding](pathfinding.md)
- [JASS native coverage](jass-native-coverage.md)
- [MPQ loading policy](file-docs/mpq.md)
