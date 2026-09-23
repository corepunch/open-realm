# Gargoyle Stone Form

## Contract

`CAbilityStoneForm` in `game/skills/s_stone_form.c` owns the reversible unit-type change. The `Astn` ability row in `AbilityData.slk` supplies the two endpoints: `DataA1` (`level[0].data[0].id`) is the ordinary unit rawcode and `UnitID1` (`level[0].unitID`) is the stone-form rawcode. Do not encode Gargoyle IDs in the procedure; custom object data can author different endpoints.

The procedure validates that the current unit is one endpoint, transforms the same edict with `G_TransformUnitType`, then clears movement goals/progress and returns it to Stand. This preserves script and selection identity while the shared transform path rebinds authored unit data. Immediate orders reach the shared spell path, which checks ability ownership before execution.

## Verification

`wc3_unit.stoneform_order_requires_authored_ability_ownership` checks an unowned request is rejected. `wc3_unit.stoneform_uses_authored_transform_endpoints_in_both_directions` supplies non-stock rawcodes in the Astn AbilityData fixture and checks both rebindings. Run them with:

```sh
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_unit.stoneform_*'
```

The round-trip fixture calls the registered ability execute message directly because its minimal test unit data does not model all live-cast prerequisites. The separate ownership test covers immediate-order rejection; a complete live cast fixture should be used if cast timing/cooldown behavior changes.
