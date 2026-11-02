# Call to Arms and Militia

## Contract

Human Call to Arms is a paired `Amic`/`Amil` interaction. `Amic` belongs to an
eligible Hall and orders nearby Peasants/Militia; `Amil` belongs to the worker
and owns the Peasant <-> Militia transform. The transform keeps the same edict
so selection, groups, and JASS unit handles continue to identify the same unit.

The native worker-side target orders are:

| Order | Meaning |
| --- | --- |
| `militiaconvert` (852071) | approach an eligible same-owner `Amic` Hall, then arm |
| `militiaunconvert` (852651) | approach an eligible same-owner `Amic` Hall, then return to Peasant form |

The Save223 implementation routes these target orders through the Amil-owned
admission and queued-order hooks. The distinct `militia`/`militiaoff` names are
not aliases for the target orders. [Native producer evidence](retail-pathfinding-engine.md#militia-target-command-producer)
records the accepted names, range/facing branches and interruption captures;
implementation validation and remaining limits are below.

## Current Engine Data Flow

```text
Amic/Amil command button
        -> G_IssueUnitTargetOrder()
        -> Amil admission and queued packet activation
        -> captured Hall range / ordinary target chase / facing continuation
        -> S_MilitiaApproachComplete() revalidates Hall and live range
        -> Data A/Data B in-place transform
        -> Bmil timed expiry or militiaunconvert return
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

## Target Continuation and Save223

Admission validates the same-owner live Hall, the worker's current form and
available Amil, and the Hall's Amic. Activation retains the Hall incarnation,
conversion direction and its current-rank cast range in `militia_t`. The
configured range or target-perimeter predicate admits immediate completion only
when the worker also faces the Hall within 0.2 radians. Outside range, ordinary
Move owns a target chase with the captured range. Inside range but facing away,
the same owner receives `FLT_MAX` and turns without translating. The fine-grid
target-perimeter alternative is independent of radial cast range: a public
engine test converts from the perimeter while the configured radial predicate
is false. This does not reinstate the old distance-to-footprint walk.

`S_MilitiaApproachComplete` rechecks both abilities, partner identity and the
Hall's live rank/range at the terminal. Removing Amil or Amic leaves the admitted
physical approach intact but prevents conversion. Hall removal ends physical
translation; the scheduled Militia owner subsequently retires its public head.
Stop and replacement orders release the old pairing, while pause/resume retains
the continuation. Native near-away Stop permits residual visual facing
interpolation, so unchanged XY does not imply a frozen angle.

Issued-order callbacks can invalidate the Hall before activation. Rejected
activation retires its own admitted head through `unit_stand`, allowing queued
successors to run. Callback-owned Move survives, and a replacement pairing to
a different Hall owns its new target. These callback/FIFO cases have engine
regressions; the native lifecycle captures do not independently cover them.

Save223 writes `partner` as `F_EDICT`, its spawn generation, `returning`, and
`pair_range`; the common physical-group state retains its concrete chase.
`S_MilitiaApproachComplete` is in the C-function callback roster. Save222 and
earlier layouts are rejected. Public scheduler tests cold-load both approach
and facing continuations; the removed Hall is reclaimed before the snapshot.

## Verification and Scope

The integrated Save223 implementation passes ten public-JASS tests / 403 assertions
in each edition. They cover correct names/numeric IDs, immediate aligned
completion, facing in both conversion directions, seven fresh/saved lifecycle
cases, saved facing, live Hall-rank changes, issued-callback rejection and
replacement, queued rejection, a replacement Hall, and perimeter-only admission.
The initial regression had 18 failed assertions per edition; Hall removal then
exposed four position failures before physical chase release was corrected.
A later callback regression exposed four assertions from a stranded admitted
head; normal owner retirement now preserves successor dispatch.

The final Militia/Unit/Order Lifecycle/Ability Dispatch checks pass 314 tests
and 87542 assertions per edition. Save and Pool checks separately pass 240 tests
and 82694 assertions per edition against unchanged serialization. The focused
total is 554 tests / 170236 assertions per edition. The source-pinned record is
`militia-review171-checkpoint.json` under the TARGET-01.2 analysis runtime. These
results are separate from the preceding Entangle composite checkpoint and do
not establish a new successful full `make test` invocation.

The native portable producer and lifecycle corpora preserve 2994 accepted public
markers and a separate 1524-marker wrong-name rejection witness; 37 Python checks
pass. They remain `engine_parity:false`: no exact far-route trajectory or full
Militia timing comparison is claimed. Native lifecycle captures cover forward
conversion only, one pause interval, ability removal and target Hall removal.
Worker death/removal, Hall death or motion, reentrant callbacks, reverse
interruption cases and adjacent range boundaries remain uncaptured. The rank
mutation regression exercises the implementation's static recovered live-range
contract, not a new native rank-mutation witness. TARGET-01.2 remains open.

## Remaining Parity Work

- resolve Call-to-Arms failures through Warcraft `CommandStrings.txt` keys;
- ability-specific sound/effect presentation;
- morph animation polish;
- broader handling of dynamically-added worker abilities during Militia form;
- workers currently hidden inside Gold Mines.
