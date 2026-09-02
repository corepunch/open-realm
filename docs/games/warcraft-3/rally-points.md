# Rally points

## Contract

Rally is producer-owned metadata. A unit whose `UnitProfile.trains` is non-empty or whose normalized `Revive` field is non-zero exposes `CmdRally`. The producer stores one current target independently of its production queue; queue entries do not snapshot the rally destination.

Zero-initialized `edict.rally.type == RALLY_TARGET_SELF` is the default state. It means the producer itself is the rally widget, matching Warcraft/Warsmash's effective default without allocating a separate target object during spawn.

The runtime target forms are:

| State | Runtime payload | Meaning |
| --- | --- | --- |
| `RALLY_TARGET_SELF` | none | producer itself |
| `RALLY_TARGET_POINT` | `VECTOR2 point` | fixed world point |
| `RALLY_TARGET_ENTITY` | `LPEDICT entity` + `entity_spawn_time` | live widget target |

`entity_spawn_time` is part of the handle identity. A freed edict may later be reused, so a raw `LPEDICT` is not sufficient for persistent rally state.

## Orders and command UI

`CmdRally` is registered as an engine command ability in `skills/s_skills.c`. Activating it enters the normal server-owned point/entity target mode through `gameClient_t::menu`.

Both explicit `setrally` and Smart/right-click are accepted by rally-capable producers:

```text
unit_issueorder(producer, "setrally", point)
unit_issueorder(producer, "smart", point)
unit_issuetargetorder(producer, "setrally", widget)
unit_issuetargetorder(producer, "smart", widget)
```

This handling occurs before immobile-unit and gold-mine-worker movement guards because setting Rally is metadata, not producer movement.

Right-click point handling preserves the existing formation-aware SmartPoint path for normal units. A selection containing only rally-capable production structures stores the clicked point instead of trying to move the structures.

Rally is a control operation, not a consequence of selection. `SmartPoint` and the Rally point/entity target callbacks iterate `FOR_CONTROLLABLE_SELECTED_UNITS`, and `rally_command` validates the focused producer with `G_UnitCanControl`. Selecting an enemy or neutral producer for inspection must therefore never change its Rally target. See [selection-and-control.md](selection-and-control.md).

Setting Rally back onto the producer normalizes to `RALLY_TARGET_SELF`.

## Production handoff

`G_ApplyRallyOrder(producer, produced)` is the single post-production handoff. It resolves the producer's *current* rally state when completion occurs and issues ordinary Smart semantics to the resulting unit:

```text
point target  -> unit_issueorder(produced, "smart", point)
widget target -> unit_issuetargetorder(produced, "smart", widget)
```

Normal training calls this only after `SP_FindUnitExitPosition` has found a legal location, the queued unit has been revealed, and the existing train-finish event has been published. Hero revival calls the same helper after `G_ReviveHero` and the Hero revive-finish events.

The existing Smart resolver remains authoritative for what the produced unit does. Current supported downstream cases include point movement, worker Gold Mine harvesting, worker tree harvesting, resource return, worker repair, item pickup, destructable attack, and enemy attack. Rally does not contain special cases for those actions.

Changing Rally while a unit is queued therefore affects that unit if the change occurs before completion. The queue contains no rally copy.

## Target lifetime

`G_ResolveRallyTarget` validates stored widget identity before dereferencing it. A freed/reused edict resets to self. `G_FreeEdict` proactively invalidates matching rally references before clearing an entity.

Current Warsmash explicitly resets dead *unit* rally targets. OpenRealm mirrors that rule: a `SVF_MONSTER` target is invalid when either `SVF_DEADMONSTER` is set or its authoritative life is zero, resolves to `RALLY_TARGET_SELF`, and `unit_die` invalidates matching rally references immediately. Checking both representations is intentional because lifecycle code and focused tests may observe the death flag before or independently of a life-value transition.

Destroyed destructable and consumed/removed item semantics are intentionally not guessed beyond safe edict-lifetime validation. A destructable that remains an in-use dead object remains a stored widget until a later compatibility decision is backed by retail/Warsmash evidence.

## JASS

The following declared natives are registered and read the same producer state:

- `GetUnitRallyPoint` returns the resolved target's current X/Y. Widget rallies therefore follow the widget's current position.
- `GetUnitRallyUnit` returns the widget only when it is a unit. The default self-rally of a producer therefore returns the producer.
- `GetUnitRallyDestructable` returns the widget only when it is a destructable.

There is no JASS rally-item getter in `common.txt`, although the runtime rally state may store an item widget.

String-based `IssuePointOrder`/`IssueTargetOrder` work with `"setrally"` through the normal order functions. The generic Warcraft numeric order-ID table is still not implemented, so `Issue*OrderById`/`OrderId("setrally") == 851980` are not claimed by this subsystem yet.

## Presentation

The persistent rally marker is derived presentation, not simulation state. `G_UpdateRallyIndicator` reads the focused selected producer's current rally target and updates one ordinary edict owned by that game client. The game resolves the active player's `RallyIndicatorDst` skin field server-side and marks the edict `SVF_OWNER_ONLY`, so other clients never receive it in their snapshots.

The marker is selection-scoped like Warsmash's single `rallyPointInstance`:

- a focused selected rally-capable producer shows its current marker;
- changing selection frees or updates that same client-owned edict;
- selecting the producer again reconstructs the marker from authoritative `edict.rally` state;
- setting or invalidating the rally target refreshes the selected marker without changing production/pathfinding state.

Point rallies are terrain-grounded. Widget rallies follow their target through `MOVETYPE_LINK`; items use their ground position, and destructables retain the current Warsmash `+192` vertical offset. The indicator uses the producer owner/team colour, rotates by `Misc.BuildingAngle`, ignores fog and selection picking, and starts in the model's `Stand` sequence while visible.

Successful explicit Rally and Smart/right-click Rally also play the authored `RallyPointPlace` UI sound. Point Rally displays the same transient green `UI\Feedback\Confirmation\Confirmation.mdx` feedback used by movement; this remains separate from the persistent rally marker.

Queued-order `WaypointIndicator` flags are still a presentation gap. OpenRealm does not yet expose a Warsmash-equivalent player order queue suitable for reconstructing those flags without broader order-queue work, so this change intentionally does not synthesize them from internal movement route waypoints.

## Verification

Focused in-engine tests live in `games/warcraft-3/game/tests/t_rally.c` and cover:

- train/revive capability vs research-only structures;
- `CmdRally` handler registration;
- default self-rally;
- point and widget storage through `setrally`/Smart;
- moving widget coordinates;
- reset by clicking the producer;
- dead-unit and freed-edict invalidation;
- point Smart handoff;
- reading the latest Rally target at training completion.

Manual presentation validation should additionally check:

- default self-rally marker appears when a rally-capable producer is selected;
- terrain Rally shows both the transient green click confirmation and persistent marker;
- a widget Rally follows a visible moving target and prefers `sprite rally` / `sprite` / `overhead ref` attachments;
- owner/team colour and race-skin `RallyIndicatorDst` are used;
- deselection clears the marker and reselection rebuilds it from stored Rally state;
- Rally target invalidation returns the marker to the producer;
- `RallyPointPlace` plays for explicit and Smart Rally without replacing the normal unit acknowledgement.

Run when validating locally:

```sh
make test-wc3-engine WC3_PATTERN='wc3_rally.*'
```

The implementation task that introduced this document intentionally did not compile or run tests; that was a caller constraint.

## See also

- [command-feedback.md](command-feedback.md) — transient point confirmations and persistent command-state marker boundaries.
- [economy-and-unit-presentation.md](economy-and-unit-presentation.md) — Smart worker/resource behavior reused by Rally.
- [hero-revival.md](hero-revival.md) — persistent Hero object and Altar production lifecycle.
- [building-construction.md](building-construction.md) — production queue and command-card ownership.
- [jass-native-coverage.md](jass-native-coverage.md) — native registration/conformance rules.
