# Call to Arms and Militia

## Contract

Human Call to Arms is a paired `Amic`/`Amil` interaction. `Amic` belongs to an
eligible Hall and orders nearby Peasants/Militia; `Amil` belongs to the worker
and owns the Peasant <-> Militia transform. The transform keeps the same edict
so selection, groups, and JASS unit handles continue to identify the same unit.

The worker-side target orders are:

| Order | Meaning |
| --- | --- |
| `militia` | approach an eligible same-owner `Amic` Hall, then arm |
| `militiaoff` | approach an eligible same-owner `Amic` Hall, then return to Peasant form |

`G_IssueUnitTargetOrder()` must explicitly admit both strings and
`unit_issuetargetorder_now()` must route them to `S_MilitiaTargetOrder()`. These
are not generic `move`/`smart` orders.

## Data Flow

```text
Amic/Amil command button
        -> G_IssueUnitTargetOrder()
        -> unit_issuetargetorder_now()
        -> S_MilitiaTargetOrder()
        -> Hall footprint approach
        -> Data A/Data B in-place transform
        -> Bmil timed expiry or militiaoff return
```

`Amil` Data A is the normal worker type and Data B is the alternate Militia
unit type. `S_SpellDataId()` preserves the rawcode interpretation of those
fields. The authored ability duration controls the per-unit Militia lifetime.

A zero pairing `Area` is a sentinel for unbounded search, matching Warsmash's
generic pairing behavior. Non-zero `Area` remains a literal search radius.

## Hall Eligibility

A worker pairs only with a living, unpaused, same-owner unit exposing `Amic`.
TFT melee initialization can add `Amic` dynamically to the starting Human Town
Hall. OpenRealm therefore honors runtime-added `Amic`; for the standard Human
Hall chain it can recover the missing runtime add for the first `htow` and for
`hkee`/`hcas`, while an explicit `UnitRemoveAbility('Amic')` remains
respectable state.

## Transform State

Arming deposits carried Gold/Lumber, preserves proportional HP/mana and the
same edict, then binds the alternate unit's authored data. A `Bmil` timed status
owns natural expiry. `Bmil` also opts into the generic selected-unit countdown
bar; see [Timed Status Presentation](timed-status-presentation.md). Explicit
`militiaoff` may resume the remembered Gold or Lumber job; natural expiry only
restores Peasant form and does not force an economic order.

Because the transform deliberately preserves the edict/selection handle, a type
change does not itself trigger the normal selection-change portrait rebuild.
`militia_transform_type()` must invalidate both the info panel and the selected
portrait. `G_InvalidateUnitPortrait()` marks every connected client currently
selecting the unit as `presentation_dirty`; `G_RunClients()` then rewrites the
portrait layer on the next server frame using the rebound `ent->s.model`. Do not
write `svc_layout` directly from the transform. Without this explicit portrait
invalidation, natural `Bmil` expiry can leave a selected Peasant displaying the
old Militia portrait even though the world entity has already reverted.

## Target-Order Dispatch Contract

`repair`, `militia`, and `militiaoff` are specialized target orders admitted by
`G_IssueUnitTargetOrder()`. Keep all three when resolving changes to the generic
target-order whitelist; dropping the Militia orders prevents a successful Hall
search from ever reaching `S_MilitiaTargetOrder()`. The regression test
`wc3_unit.militia_target_order_reaches_militia_behavior` protects this contract.

## Remaining Parity Work

- resolve Call-to-Arms failures through Warcraft `CommandStrings.txt` keys;
- ability-specific sound/effect presentation;
- morph animation polish;
- broader handling of dynamically-added worker abilities during Militia form;
- workers currently hidden inside Gold Mines.
