# Warcraft III Polymorph

## Scope

OpenRealm implements the stock `Aply` Polymorph contract through the shared spell and timed-status systems. The implementation is intentionally limited to behavior supported by Warcraft data and the existing runtime model: it keeps the original edict/JASS handle and unit statistics, applies a reversible presentation/movement override, and lets the configured timed buff own restoration.

## Data contract

`AbilityData.slk` is authoritative. For the active level, Polymorph consumes:

- `DataA` / `Ply1`: maximum neutral-hostile creep level;
- `DataB` / `Ply2`: ground morph unit;
- `DataC` / `Ply3`: air morph unit;
- `DataD` / `Ply4`: amphibious morph unit;
- `DataE` / `Ply5`: water morph unit;
- normal mana, range, duration, targets, and `BuffID` fields.

Creep `ACpy` is a `code=Aply` alias. ROC omits BuffID; `human_buff` falls back to
the TFT token `Bply`. That fallback is not in `melee_buff_fallback` — Polymorph
never reads the melee table.

The current typed AbilityData loader exposes the first rawcode from each `Ply2`-`Ply5` unit-list string. Stock `Aply` uses that representation directly. Selecting among multiple authored morph-unit rawcodes remains future data-loader work rather than a hard-coded fallback.

Movement-class selection uses `UnitData.moveTypeName`: `fly` selects `Ply3`, `amph` selects `Ply4`, `float` selects `Ply5`, and ordinary ground/hover movement selects `Ply2`.

## Runtime behavior

A successful cast keeps the target's `class_id`, owner, health, attacks, references, and other simulation identity intact. `edict_t.polymorph` snapshots the model handle, model scale, and base movement speed needed for restoration. The configured morph unit contributes its model/scale and authored movement speed while the Polymorph buff is live.

The morph interrupts the current incompatible action and queued orders. While `polymorph.active` is set, attack orders and new spell casts are rejected. Fresh movement orders remain legal, using the temporary movement speed. `S_HumanCanAttack()` also rejects autonomous attacks so acquisition cannot bypass order validation.

Re-casting the same configured buff uses `unit_addtimedstatus()`'s existing refresh semantics. The original presentation snapshot is recorded only on the first transition, so a refresh cannot turn the temporary form into the restore target.

## Target validation

The stock handler requires an enemy living target through the normal spell pipeline and additionally rejects Heroes, mechanical units, summoned units, and illusions. `Ply1` is applied to Neutral Hostile creeps; a creep above the authored maximum is rejected. The selected `Ply2`-`Ply5` form must resolve to a unit with model data.

This patch does not synthesize the separate Dark Wizard summoned-unit destruction behavior. That variant needs a data/ability distinction before it can be implemented without changing stock Sorceress targeting semantics.

## Buff removal and death

The configured timed buff owns the reversible transformation. Normal expiry calls `S_HumanStatusExpired()`, which restores the saved presentation/movement state. Dispel Magic and Spell Steal now run the same status-expiry hook before deleting a timed status, preventing reversible status effects such as Polymorph from becoming visually stuck.

Death retires the active restore contract so an expired timer cannot revive or remorph a dead unit. The death presentation already selected at death is left intact.

## JASS and orders

`polymorph` is registered as Warcraft order ID `852074`, so the canonical string/numeric order router introduced by the spell-order work can dispatch owned `Aply` abilities through the same spell pipeline.

`IsUnitType(unit, UNIT_TYPE_POLYMORPHED)` (`ConvertUnitType(22)`) reflects `edict_t.polymorph.active`; it does not infer Polymorph from the temporary model or a particular sheep rawcode.

## Save/load

Save format 18 serializes the Polymorph restoration record (`ability`, `buff`, selected form type, original model/scale/movement speed, and active state). The timed buff itself already round-trips in `abilstatus[]`, so a loaded active Polymorph retains both its remaining duration and the state required to restore the original presentation.

## Known boundaries

- The implementation consumes the first rawcode from each `Ply2`-`Ply5` unit-list field; multi-entry morph-list selection is not yet modeled.
- Variant-specific summoned-unit destruction is not implemented.
- Exact retail queue preservation after the transformation is not claimed; incompatible current/queued actions are cleared conservatively.
- Polymorph does not replace the target's full UnitData/UnitBalance row. This is deliberate for the modern Warcraft behavior where the existing unit identity/statistics survive and the temporary form supplies presentation and movement speed.

## Regression coverage

Focused tests cover the authored creep-level ceiling, summon/illusion rejection, restoration of model/scale/movement speed without changing `class_id`, canonical `polymorph` order ID, and every saved field in the Polymorph restoration record.
