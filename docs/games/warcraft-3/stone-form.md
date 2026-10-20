# Gargoyle Stone Form

## Contract

`CAbilityStoneForm` in `game/skills/s_stone_form.c` owns the reversible unit-type change. The `Astn` ability row in `AbilityData.slk` supplies the two endpoints: `DataA1` (`level[0].data[0].id`) is the ordinary unit rawcode and `UnitID1` (`level[0].unitID`) is the stone-form rawcode. Do not encode Gargoyle IDs in the procedure; custom object data can author different endpoints.

The visible statue form also requires the model's `alternate` animation property. Warcraft's stock Stone Form Gargoyle reuses the Gargoyle model and presents its stone geometry through the Alternate animation family, so the ability adds `alternate` after the stone-form rebind and the reverse rebind restores the ordinary form's authored animation properties. The alternate unit type remains authoritative for gameplay stats; the animation property is presentation state.

The procedure validates the endpoint and rejects an active morph, clears the previous order, and starts the shared primary morph request. Landing publishes a ground mover immediately but defers `G_TransformUnitType` until `Dur + DataC`; takeoff rebinds immediately and completes the request after `Dur`. Stock Gargoyle durations are 1.0 seconds forward and 0.5 seconds reverse. Ordinary `Cast` is not added. Completion applies the animation property and returns to Stand. This preserves script and selection identity while the shared transform path rebinds authored unit data. Immediate orders are owned by `CAbilityStoneForm.A_ORDER`: resolve the actual owned Astn alias, require the requested direction's source endpoint, then invoke shared no-target cast validation. An already transformed unit must reject the same direction before committing mana, cooldown or order cleanup. Returning unhandled is insufficient because registered immediate owners also own rejection; falling through after a rejected toggle could execute its inverse.

A command-card Stone Form click is a focused-subgroup command. If several Gargoyles of the active unit type are selected, the command is cast independently by every controllable member of that focused type subgroup. Other selected unit types are not transformed. The subgroup rawcode is captured before the first transform because `G_TransformUnitType` changes each Gargoyle's `class_id` in place.

## Verification

`wc3_unit.stoneform_order_requires_authored_ability_ownership` checks an unowned request is rejected. `wc3_unit.stoneform_uses_authored_transform_endpoints_in_both_directions` supplies non-stock rawcodes in the Astn AbilityData fixture, checks both rebindings, verifies the stone form gains the runtime `alternate` presentation tag, and verifies the ordinary form restores its authored animation properties. `wc3_unit.stoneform_command_applies_to_focused_unit_type_subgroup` checks command-card propagation reaches every selected member of the focused form without crossing into another selected type subgroup. Run them with:

```sh
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_unit.stoneform_*'
```

The round-trip fixture issues `stoneform` and `unstoneform` through `unit_issueimmediateorder`, exercising ownership lookup, shared no-target spell validation, and both authored transforms. The separate ownership test confirms an unowned request is rejected.

`wc3_unit.stoneform_immediate_direction_rejection_preserves_move_and_queue` adds Astn through the production actor API, rejects reverse on the base form without losing active Move or its queued successor, executes both valid directions and rejects repeated forward on the stone form. The pre-fix four-test Stone Form run failed eight assertions because the registered owner returned unhandled instead of invoking shared casting. No network or save layout changes are required by this fix.

Focused validation passes the complete `wc3_unit.*` suite (127 tests /12,708 assertions) and eight Defend regressions (104 assertions) in both Classic and TFT, using rebuilt production and test modules.


## Timed flight lifecycle (audit332)

Stone Form and Destroyer Form share `S_PrepareMorphFlight` with Raven/Bear Form.
`CAbilityMorphTransition` owns the sparse ascent update, so a destination form
need not retain the source ability in its authored list. The primary request
owns the separation contribution; animation completion cannot release it early.
Stop waits for the inverse, and scripted pause acquires its own physical hold
after that inverse. Both the pending request and ascent state survive saves.
Destroyer Form keeps the `CAbilityMetamorphosis` registration used by AI and
delegates public `avengerform` and spell execution to `CAbilityAvengerForm`.
Its takeoff changes `uobs` to `ubsp` before acquiring suppression; stock `Dur`
is 1.1 seconds. It is a one-way public conversion.

`wc3_unit.morph332_flight_forms_preserve_type_timing_and_saved_transition`
uses non-stock endpoints, both ROC/TFT Data columns, distinct Dur/DataC/Cast,
Stop, pause, and cold saves during both Stone directions and Avenger takeoff.
The old engine failed 134 of 394 assertions. After implementation, the complete
unit suite passes 15,379 assertions in 141 tests in each edition. Existing Stone
subgroup/direction and Raven ascent tests remain in that suite. Native evidence
and broader separation scope are tracked in [separation ownership](retail-pathfinding-separation.md).


Permanent morphs use authored DataB bit8, independently of an ability's
`UnitMakeAbilityPermanent` retention. Normal completion retires the ability;
early ability removal cancels the morph request without restoring the base or
releasing the pending separation contribution. Stock Destroyer Form sets this
flag. `morph334_permanent_flag_controls_removal_and_completion` covers both
schemas, custom flags/endpoints, early/late removal, death, cold saves, inactive
Move preservation and independent ascent after ability retirement. Its initial
red was28/416 failures; expanded regression468/468 and full unit142/15847 now
pass in each edition. The exact retail removal evidence is linked from the
[audit334 section](retail-pathfinding-separation.md#audit334-permanent-morph-removal).

Configured `Misc/MorphAlternateDisable` passive availability, its takeoff-rebind
inverse and detach are documented in [Stone Form passive availability](stone-form-passive-availability.md).
