# Warcraft III guard position and Stop return

## Implemented contract

OpenRealm distinguishes the player's Stop guard point from Hold Position.

- Stop captures the unit's current world position as its guard position.
- Ordinary idle acquisition after Stop is a temporary guard-combat detour. The unit may chase and fight normally.
- When that automatic combat ends, and no explicit Shift-queued order is waiting, the unit returns to the remembered guard position through the ordinary point-Move path.
- Arrival uses the existing Move completion/tolerance rules; guard return does not introduce another pathfinder or magic arrival radius.
- After the return Move completes, the unit resumes ordinary stopped/idle behavior and can acquire again.
- A newer explicit behavior order clears the old player Stop guard point, including Move, Attack, Smart, and Harvest. It therefore cannot reassert after the newer order later completes.
- A queued player order outranks guard return. Combat completion advances the FIFO instead of starting the internal return.
- A second Stop captures the unit's then-current position as the new guard point.
- Hold Position is separate: it suppresses automatic translation and does not use a return-to-anchor journey.

Guard return is internal default AI behavior rather than a player FIFO entry. This keeps the priority order:

```text
current explicit order
    > queued explicit orders
    > automatic combat detour
    > Stop guard return
    > ordinary idle acquisition
```

## State ownership

The authoritative state lives on the unit movement state:

```text
guard_position
guard_position
guard_state: NONE, IDLE, COMBAT, or RETURNING
```

`guard_state` is `NONE` without an anchor, `IDLE` while a stopped unit may acquire normally, `COMBAT` during an automatic combat detour, and `RETURNING` while the internal return Move is active. Explicit Attack and persistent parent behaviors such as Patrol, Attack-Move, and Follow keep their existing completion/resume rules and do not use Stop guard return.

The Move path changes `RETURNING` back to `IDLE` on arrival or its normal near-goal settle edge, then installs the ordinary stand state. While that Move is active, normal idle acquisition is not running.

## Explicit-order replacement

The player Stop guard point is deliberately conservative: an explicit point/target replacement clears it instead of assuming every Move destination automatically becomes a new retail guard point. Pressing Stop at the desired location establishes a new point.

This prevents an old Stop location from unexpectedly taking control after a later explicit Move or Attack completes.

## Retail guard-system follow-up

Warcraft also exposes a broader AI/creep guard-position system (`SetUnitCreepGuard`, `RemoveGuardPosition`, `RecycleGuardPosition`) and neutral-creep leash constants such as GuardDistance, MaxGuardDistance, and GuardReturnTime. Those behaviors are related but are not folded into this player Stop patch:

- `RemoveGuardPosition` and `RecycleGuardPosition` are still JASS placeholders in OpenRealm.
- `SetUnitCreepGuard` is declared by `common.txt` but does not yet have a native implementation here.
- neutral-creep 600/1000/5-style leash timing needs its own owner/AI policy and damage-timestamp state rather than being guessed onto player units.
- forced relocation does not rewrite a generic retail creep guard point, whereas Hold Position remains a non-anchor policy. Keep those systems separate.

Implement the broader creep/JASS guard layer as a follow-up on top of the shared guard-return movement primitive rather than changing Stop semantics again.

## Neutral Hostile camp assistance (Stage B)

After a surviving Neutral Hostile takes positive attack damage, the game broadcasts
a **single, non-recursive** help event to idle, eligible, attack-capable Neutral
Hostile neighbors. The radius is read from `[Misc] CreepCallForHelp` (600
world units fallback). To avoid pulling neighboring camps through moving units,
responders must be within radius both by their fixed Stage A guard anchors and
by current world position. Responders validate hostility and legal attack targets,
respect queued orders, and use the existing attack/order and Stage A leash paths.
Natural `ACsp` sleepers wake through `G_UnitWakeUp`; magical Sleep is not cleared.

**Compatibility limits:** This bounded proximity policy is not proof of retail
camp identity: the true `CreepCampPathingCellDistance` connectivity, map-authored
groups, multiple indirect alert triggers, attacking a sleeping creep without
positive damage, and nighttime re-sleep scheduling remain future verification
work. The explicit command/guard natives are reserved for Stage D. No per-tick
world scan is added: only a confirmed surviving positive-damage event broadcasts.

### Stage C: retail creep acquisition special cases (2026-10-10)

- Autonomous Neutral Hostile guard scans omit airborne flyovers and finished buildings; explicit attacks, retaliation and provoked construction attacks still use the normal legal-target checks. A ground-bound flyer is not filtered by the flyover exception.
- Level 7+ guard scans rank wounded Heroes first, then wounded units or Heroes, then other eligible targets, using distance as a tie-breaker. This is a documented approximation of retail preference, **not** a verified exact retail scoring algorithm.
- Real building construction start invokes a one-shot guard-anchor-bounded notification using the map-overridable `BuildingPlacementNotifyRadius` Misc key (600 fallback). The notification does not occur for previews, progress ticks or pre-existing buildings.
- Neutral building usage/item-sale notification remains deferred: retail sale-specific radii and eligibility are not yet verified. Do not convert proximity or purchase UI clicks into blanket creep aggression.
- Test manually with flying scout, Ensnare/Web grounded flyer, nearby completed building versus newly started construction, low-level and level-7 mixed targets, and campaign script-ordered creeps. No automatic compilation or runtime validation has been performed.
