# Undead Sacrifice (Acolyte -> Shade)

## Contract

The stock Undead Sacrifice mechanic is exposed from both ends:

| Rawcode | Owner | Target | Result |
| --- | --- | --- | --- |
| `Asac` | Sacrificial Pit | Acolyte | Shade (`ushd`) |
| `Alam` | Acolyte | Sacrificial Pit | Shade (`ushd`) |

Neither ability contains a `UnitID`/Data field for the result. Warcraft's
published unit contract and Warsmash's `StartSacrificingUnit` flow both model
this as the fixed Acolyte -> Shade conversion at a Sacrificial Pit.

The two endpoint abilities are used as capability markers rather than hardcoded
unit rawcodes: the structure must own `Asac`, the consumed non-Hero unit must
own `Alam`, both must be alive and owned by the same player, and the Pit must
not already have production/construction/upgrade state occupying the queue.

## Warsmash production-queue behavior

Warsmash uses `QueueItemType.SACRIFICE`, not an independent spell timer:

```text
accepted Sacrifice
  -> queue Shade on Sacrificial Pit
  -> hide the Acolyte
  -> do NOT reserve extra food for Shade
  -> advance using Shade UnitBalance.buildTime
  -> when complete:
       create/reveal Shade
       remove sacrificed Acolyte
       transfer normal result food accounting
       fire train-finish / trained-unit behavior
       nudge/rally result normally
```

OpenRealm mirrors this in the existing production queue. The hidden Shade edict
is the queue item and owns an `edictSacrifice_s` record pointing at the consumed
Acolyte plus its spawn generation. Train keeps queue allocation, progress,
ordering, placement and resource mechanisms; the worker lifecycle (validation,
hide/pause inverse, completion consume, food-slot inheritance) lives in
`s_sacrifice.c` behind typed `A_QUEUE_VALIDATE` / `A_QUEUE_COMPLETE` /
`A_QUEUE_CANCEL` messages dispatched from Train through the flat ability
procedure. `G_QueueSacrifice` is defined in the owner on Train-owned
mechanisms (`unit_add_build_queue`, `TrainSetBuildMove`,
`G_RefreshTrainingQueue`).

The queue deliberately bypasses normal `ReserveTrainingFood`: the Acolyte still
owns its food slot while hidden. At completion the worker is removed first,
then the Shade's authored food use is applied, avoiding a transient extra food
requirement. Stock Acolyte and Shade each use 1 food.

## Cancellation and removal

Cancelling the active Sacrifice queue item:

- removes the queued Shade;
- refunds the result unit's authored cost (stock Shade has no ordinary resource
  cost);
- restores the Acolyte's hidden/paused state;
- leaves the Acolyte alive;
- leaves player food usage unchanged.

Destroying/removing the Sacrificial Pit routes through the same production queue
cleanup and therefore restores the Acolyte rather than orphaning a hidden unit.
If the Acolyte is removed by another mechanism while queued, the next production
update cancels the invalid Sacrifice result safely.

## Save/load

Save format 37 adds `edict_s.sacrifice` and an `F_EDICT` fixup for
`sacrifice.worker`. The worker spawn generation and scalar queue state live in
the raw edict record. This is required because a mid-Sacrifice save must retain
the exact hidden Acolyte relationship instead of reconstructing it by
proximity.

## Remaining fidelity gaps

- Exact Sacrificial Pit/Acolyte sacrifice animation and effect presentation.
- Warcraft-specific command-error keys such as the dedicated "pit already
  sacrificing" / "target a Sacrificial Pit" feedback are not yet selected by
  the boolean spell-validation interface.
- Custom maps cannot choose a different result unit through `Asac`/`Alam`
  object data because retail exposes no result-unit field; the stock result is
  intentionally `ushd`.

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.sacrifice*'
make test-wc3-engine WC3_PATTERN='wc3_save.field_sacrifice*'
```

Focused tests cover both registered endpoints, queue creation, hidden worker,
no extra food reservation, a non-stock 2-second result build time, completion,
cancellation, counterpart validation, and save relocation of the worker pointer.
