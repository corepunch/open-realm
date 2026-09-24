# Warcraft III Trigger Event Dispatch And Response Context

OpenRealm routes Warcraft III gameplay events through `GAMEEVENT` in
`games/warcraft-3/game/g_events.c`. Map JASS registers `LPEVENT` handlers;
authoritative gameplay publishes an event only when the corresponding state
transition actually happens. Tutorial/campaign behavior must use this generic
bridge rather than map-specific checks.

## Event response payload

`GAMEEVENT` carries the response state that must survive until the JASS action
coroutine runs:

| Field | Meaning |
| --- | --- |
| `edict` | primary/triggering unit or widget |
| `source` | secondary unit/widget: attacker, order target, manipulated item, etc. |
| `value` | event-specific scalar such as spell/research rawcode |
| `point` / `has_point` | spell point target when the event carries one |
| `responseTo` | exact targeted registration for region/range/game-state events |

`jass_calltriggerevent()` copies those fields into `JASSCONTEXT`; sleeping JASS
coroutines retain the same response context. The save/JASS snapshot serializers
persist the queue/context fields introduced by the event bridge.

## Explicit trigger execution

Event dispatch evaluates conditions, then queues action coroutines for the JASS
scheduler. A direct `TriggerExecute()` call instead starts each action and runs
it immediately until it finishes or reaches its first wait. A waiting action
remains scheduled to resume after the caller continues.
`ConditionalTriggerExecute()` therefore exposes action writes before the
action's first wait to its caller; if the action has no wait, all of its writes
are visible when the native returns.

Nested immediate actions must preserve the active parent coroutine. In
particular, if the parent calls `TriggerSleepAction()` after a nested
`TriggerExecute()`, it must still yield and resume at that point. `jass_resume()`
restores the enclosing coroutine, player/unit response context, and loop index
when the nested action returns.

This distinction fixed Orc03's zeppelin victory gate. Its `ForGroup` callback
executes a counter trigger for each zeppelin, then immediately checks the
counter. Queuing those `TriggerExecute()` actions made the check run too early,
so the victory trigger was skipped even when both zeppelins were home. The
regression is covered by
`wc3_jass_map.orc03_home_counter_actions_complete_before_victory_gate_check` and
`wc3_jass_map.nested_trigger_execute_preserves_parent_coroutine`.

## Selection

Interactive selection uses `G_SelectEntity()` / `G_DeselectEntity()` and emits
`EVENT_PLAYER_UNIT_SELECTED` / `DESELECTED` plus the unit-specific equivalent
only on an actual membership transition. `GAMEEVENT` does not currently carry a
separate selecting-player field; response code that needs that distinction must
not infer support from the event payload.

`SelectUnit` / `ClearSelection` are local-presentation natives. A synchronized
call applies to every **connected local client view**, not to unused map-player
slots. When JASS is executing under a local-player context, only that connected
client view is changed. Disconnected placeholder clients must never acquire
selection bits or generate selection events.

## Attacked events

An attack event is emitted when an actual unit attack swing starts, not when an
attack order is merely accepted:

```text
attacker begins melee/ranged swing
    -> EVENT_PLAYER_UNIT_ATTACKED
    -> EVENT_UNIT_ATTACKED
```

Response context is:

```text
GetTriggerUnit() = attacked unit
GetAttacker()    = attacker
```

The attacked unit is `GAMEEVENT.edict`; the attacker is `GAMEEVENT.source`.
Destructable attacks do not synthesize unit-attacked events.

## Spell effect events

The current unified spell pipeline supports the high-confidence **starts the
effect** transition:

```text
validation succeeds
    -> mana/cooldown commit where applicable
    -> EVENT_PLAYER_UNIT_SPELL_EFFECT
    -> EVENT_UNIT_SPELL_EFFECT
    -> spell gameplay callback
```

Response context is:

```text
GetSpellAbilityUnit() = caster
GetSpellAbilityId()   = ability rawcode
GetSpellTargetUnit()  = entity target when the current spell target is a unit
GetSpellTargetX/Y()   = point target coordinates when the current target is a point
GetSpellTargetLoc()   = location for a point target
```

`GetSpellTargetItem()` / `GetSpellTargetDestructable()` safely classify the
secondary target handle if future spell target modes carry those widget types.
OpenRealm does not yet expose a confident ability-instance handle for
`GetSpellAbility()`.

Do **not** fake the rest of the lifecycle. `SPELL_CHANNEL`, `SPELL_CAST`,
`SPELL_FINISH`, and `SPELL_ENDCAST` remain unsupported until the spell state
machine has distinct authoritative transitions for them; event diagnostics
continue to report registrations for those gaps.

## Orders, items, Hero skills, regions and range

The bridge also provides:

- accepted immediate, point, and target order events with `GetIssuedOrderId()`,
  point and target response natives;
- successful item pickup/use context via `GetManipulatingUnit()` and
  `GetManipulatedItem()`;
- successful Hero skill learning via `GetLearningUnit()`, `GetLearnedSkill()`
  and `GetLearnedSkillLevel()`;
- region enter/leave with `GetTriggeringRegion()` and entering/leaving unit;
- `TriggerRegisterUnitInRange` with the watched unit as trigger unit and the
  entering/other unit as filter source.

Authored boolexpr filters run with the event-specific filter unit before trigger
conditions/actions are scheduled.

## Diagnostics

`wc3_event_debug` is opt-in and must not affect gameplay:

```text
wc3_event_debug 1   registrations, known gaps, per-map summary
wc3_event_debug 2   plus event queue, filter rejection and dispatch detail
```

Summary counters have distinct meanings:

- `registered`: map registrations of that event type;
- `queued`: `GAMEEVENT`s published by gameplay;
- `matched`: a registration was reached and its event filter passed;
- `dispatched`: `jass_calltriggerevent` / direct timer trigger queued work after
  trigger conditions;
- `filtered`: authored registration filter rejected the event.

`EVENT_GAME_TIMER_EXPIRED` is special: `G_RunTimers()` invokes its registrations
directly instead of creating a `GAMEEVENT`, so `queued` can remain zero while
`matched`/`dispatched` increase. This is working behavior, not a missing timer
producer.

A summary reports `REGISTERED_NEVER_OBSERVED` only when an event was registered
but neither a queued gameplay event nor a direct matched registration was seen.
That status is evidence to investigate, not proof of a bug: the player may
simply not have performed the relevant action.

## Human01 diagnostic findings (2026-09-04)

A `wc3_event_debug 2` Human01 run established these reusable facts:

- region enter, unit-in-range, game-state, death, accepted order, Hero-skill and
  selection producers were active;
- script-driven selection incorrectly generated transitions for disconnected
  players; fixed by limiting local selection presentation to connected views;
- Human01 registers both player-unit and unit-specific attacked events while no
  attacked producer existed; fixed at attack-swing start;
- Human01 registers `EVENT_PLAYER_UNIT_SPELL_EFFECT`; the unified spell pipeline
  had no lifecycle producer, so `SPELL_EFFECT` is now emitted immediately before
  the gameplay effect callback;
- zero observed pickup/Hero-level/leave-region events in that run were **not**
  treated as implementation failures because those actions were not proven to
  have occurred;
- timer summaries were previously false positives because direct timer trigger
  execution bypassed the queued-event counter; `matched` now records them.

## Verification

Focused coverage lives in:

- `games/warcraft-3/game/tests/t_jass_map.c` — Orc03's synchronous `ForGroup` /
  `ConditionalTriggerExecute` counter gate, direct trigger waits, and nested
  parent-coroutine restoration;
- `games/warcraft-3/game/tests/t_api.c` — selection locality, attacked response,
  order/item/Hero/region/range response context, spell response context;
- `games/warcraft-3/game/tests/t_avatar.c` — a runtime-added spell can cast through
  its stock order and publishes the two spell-effect event families;
- `games/warcraft-3/game/tests/t_game.c` — direct timer expiry is counted as
  matched/dispatched diagnostics and save/load timer behavior remains intact.

After building, useful targeted runs are:

```bash
make test-wc3-engine WC3_PATTERN=wc3_jass_map.orc03_home_counter_actions_complete_before_victory_gate_check
make test-wc3-engine WC3_PATTERN='wc3_api.*'
make test-wc3-engine WC3_PATTERN='wc3_spell.*'
make test-wc3-engine WC3_PATTERN='wc3_save.round_trip_jass_timers'
```
