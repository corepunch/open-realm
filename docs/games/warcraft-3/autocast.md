# Warcraft III Autocast and Auto Repair

## Scope

OpenRealm has a small generic autocast contract in `ability_t` and uses it for the Repair family (`Arep`, `Aren`, `Arst`) and Human support spells. The scheduler owns *when* an idle unit gets an automatic acquisition opportunity; the ability owns toggle state, target selection, and issuance of its ordinary order.

The implementation intentionally does not turn the existing `AB_AUTOCAST` metadata flag into behavior for every spell. Cold Arrows and other autocast abilities still need their own targeting/execution rules.

## Ability contract

Autocast procedures receive three typed messages:

- `A_AUTOCAST_ON` checks the selected policy;
- `A_AUTOCAST_SET` supplies `call->enabled` and returns whether the procedure accepts the change;
- `A_AUTOCAST_ACQUIRE` acquires a target and issues its ordinary order using `call->item->code`.

`G_SetUnitAutocast(unit, rawcode, enabled)` enforces one selected autocast ability.
`edict.autocast_code` retains the actual authored alias; `AI_AUTOCAST_ACTIVE` is
its fast scheduler marker. Human Heal, Inner Fire, Slow and Spell Steal use that
selection directly. Repair additionally stores its policy bit in
`AI_AUTOCAST_REPAIR`. Both fields survive ordinary completion and interruption.

Switching disables the previous policy before enabling the new one because
related procedures can share a policy bit. If the new procedure rejects the
change, the old policy is restored. Disabling a nonselected alias leaves the
selection intact. Removal disables a selected ability; acquisition accepts
runtime-added abilities and does not require a static UnitAbilities entry.

`abilityCall_t` is a message-tagged union: boolean messages must not decode
`call->target`. See [ability verification](ability-verification-review.md) for
alias, switching, removal and crash regression coverage.

## Command-card toggle

A command button can expose an optional secondary command through `gameCommandButton_t.alternate`. The WC3 HUD serializes it in the command frame's `text` field; `alternate_active` maps to `UIFLAG_ALTERNATE_ACTIVE` for the active glow.

For Repair the secondary command is:

```text
autocast <repair rawcode>
```

Left click remains the normal Repair targeting action. Right-button down/up over a command button with a secondary command is consumed by the client layout layer so it cannot also become a world Smart order. Right-button up sends the secondary command.

The server toggles all controllable selected units that carry the requested authored rawcode and plays `AutoCastButtonClick`. Enabling Auto Repair on an already idle worker immediately tries one acquisition pass; toggling during active movement/work does not interrupt that behavior.

JASS/string immediate orders `repairon` and `repairoff` use the same toggle path. The canonical order table also exposes `repair=852024`, `repairon=852025`, and `repairoff=852026`, so numeric `IssueTargetOrderById` / `IssueImmediateOrderById` resolve through the same ordinary order paths.

## Acquisition ordering

`ai_stand()` retains the existing staggered acquisition cadence. At an acquisition slot it now performs:

```text
try enabled autocast abilities
    -> if one starts an order, stop
otherwise
try normal automatic attack acquisition
```

This lets a worker with an attack prefer a valid Auto Repair target over an enemy when both are available. Units with no attack can still autocast because the autocast pass occurs before the attack-capability early return.

The current implementation only integrates this with ordinary idle/default stand. Hold Position, Attack-Move, Patrol, Follow, and other behaviors need explicit resume/movement semantics before they should call generic autocast.

## Auto Repair target policy

Repair implements Warsmash's `NEARESTVALID` policy for units:

1. Read the worker's normal acquisition range with `G_AcquisitionRange()`.
2. Enumerate nearby edicts in a box expanded by the worker collision radius.
3. Reject candidates that the existing Repair rules do not accept.
4. Rank valid candidates by collision-aware edge distance:

```text
max(0, center_distance - worker_collision - target_collision)
```

5. Issue the ordinary `repair` entity-target order for the nearest candidate.

The acquisition range is a discovery radius, not Repair cast range. Once the normal Repair order is issued, `s_repair.c` remains authoritative for building-footprint or live mechanical-unit approach routing, `Rng` plus floating-target `DataE`, work animation, completed-target costs, Human paused-construction/power-building rules, failure, completion, and Shift-order continuation.

Auto Repair uses the same high-confidence Repair validator as explicit orders. Authored stock-style `targs` can therefore admit completed Friend structures and damaged mechanical non-buildings, including `ground`/`air` and Ancient/Non-Ancient classification restrictions; sparse rows still fall back to the legacy building-only policy. Human power building remains owner-only. Destructible Repair and complete generic target-mask semantics remain separate gaps.

## Repair cancellation and replacement orders

Repair cleanup must not destroy state already installed by the replacement order. Move/Attack/Follow paths can install their new goal before `unit_setmove()` switches away from the Repair move, and that switch calls `S_CancelRepair()`.

`S_CancelRepair()` therefore clears `goalentity` only when it still names the Repair-owned target. If a replacement order has already installed a different goal, that goal is preserved. Conversely, an ordinary Stop/stand transition still retires the old Repair target.

This ownership rule prevents the previous crash where Move installed a waypoint, Repair cancellation nulled it, and the new movement behavior subsequently dereferenced the missing goal.

## Diagnostics

Autocast investigative diagnostics are compile-time disabled in normal builds. Build the WC3 game module with `WC3_DEBUG_AUTOCAST` defined, then opt into runtime verbosity with:

```text
+set wc3_autocast_debug 1
```

Level 1 reports toggle/acquisition/Repair lifecycle summaries. Level 2 also reports every Auto Repair candidate and its rejection reason. Without `WC3_DEBUG_AUTOCAST`, neither the diagnostic CVar lookup nor the per-order/per-candidate tracing is compiled into the game module.

Useful prefixes:

```text
WC3_AUTOCAST
WC3_AUTOREPAIR
```

Typical level-2 candidate output distinguishes reasons such as full health, wrong owner, construction state, missing Repair code, and ordinary Repair target-rule rejection. Keep this logging behind both the `WC3_DEBUG_AUTOCAST` compile-time guard and the runtime CVar; do not add unconditional per-frame acquisition logs.

## Tests

Focused coverage lives primarily in `games/warcraft-3/game/tests/t_building.c` and `tests/test_net.c` and covers:

- Repair toggle state and `repairon` / `repairoff`;
- command-card secondary command serialization and active state;
- client right-click dispatch/consumption;
- nearest valid damaged building selection;
- nearest valid damaged mechanical-unit selection;
- Friend/allied completed-target validation without stealing active Human construction ownership;
- `DataE` naval range only for `float` mechanical targets;
- canonical numeric `repair` / `repairon` / `repairoff` order IDs;
- collision-aware acquisition of a large nearby building whose centre lies outside `uacq`;
- ignoring a nearer full-health building;
- autocast before idle auto-attack, with attack fallback when autocast is off;
- routing automatic/manual Repair through the normal target-order path;
- Shift-queued Repair;
- moving away while repairing without losing the replacement goal;
- normal Repair cleanup still clearing a Repair-owned goal.
