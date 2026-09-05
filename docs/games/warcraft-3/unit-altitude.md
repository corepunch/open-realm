# WC3 Unit Altitude And Support Surfaces

## Contract

Warcraft unit altitude is data-driven. `UnitData.moveHeight` (`umvh`) is the unit type's authored default fly height;
`edict_t.unitinfo.FlyHeight` is the mutable per-unit current height used by `SetUnitFlyHeight`.

The server-side world position contract is:

```text
unit s.origin.z = support surface Z + unitinfo.FlyHeight
```

Do not derive vertical placement from MDX bounds, model scale, collision size, or the `movetp` name. Ground units and
`horse` units normally have a zero/near-zero `moveHeight`; the model's own origin/geometry places hooves and feet.
Flying units use the same model-origin rule with a positive authored `moveHeight`.

## Data Flow

```text
Units/UnitData.slk moveHeight / umvh
        -> UnitData_t.moveHeight
        -> SP_SpawnUnit
        -> edict_t.unitinfo.FlyHeight
        -> M_CheckGround
        -> entityState_t.s.origin.z
        -> client MDX transform
```

`GetUnitDefaultFlyHeight` reads the bound `UnitData_t.moveHeight`; it must not return the mutable current
`unitinfo.FlyHeight`.

`SetUnitFlyHeight(unit, height, rate)` updates `unitinfo.FlyHeight` and immediately recomputes Z through
`M_CheckGround`. Current Warsmash also leaves `rate` unimplemented, so OpenRealm intentionally applies `height`
immediately until rate interpolation is implemented as a separate feature.

## Supporting Surface

`M_CheckGround` owns WC3 unit support-surface selection:

| `movetp` | Base surface | Walkable destructable may replace base |
|---|---|---|
| `foot` | terrain | yes |
| `horse` | terrain | yes |
| `fly` | `max(terrain, water)` | yes |
| `hover` | `max(terrain, water)` | yes |
| `float` | `max(terrain, water)` | **no** |
| `amph` on walkable terrain | terrain | yes |
| `amph` swimming (`SWIMMABLE && !WALKABLE`) | `max(terrain, water)` | yes |

Water height comes from the W3E `waterlevel` field using the same bilinear interpolation and
`WATER_HEIGHT_COR` decode as the renderer. Amphibious swimming classification uses the immutable terrain WPM, not
baked/static building obstacles; `CM_TerrainPointIsWalkable` and `CM_TerrainPointIsSwimmable` expose that distinction.

Walkable destructables currently use their authored entity Z over the rectangular pathing-texture footprint. This is an
OpenRealm approximation of Warsmash's rendered-model collision/raycast. Keep that distinction explicit if bridge
collision is upgraded later.

## Projectile Height

Basic and spell missile entities already launch from their server-side source Z. Once unit altitude is part of
`s.origin.z`, a flying attacker's launch point naturally inherits its fly height.

Missile steering uses:

```text
target Z = target s.origin.z + target impactZ
```

`G_UnitImpactZ` resolves the ROC/TFT storage split:

- TFT: `UnitWeapons.impactZ`
- ROC: `UnitData.impactZ`

The existing `G_UnitAttack1LaunchX/Y/Z` helpers provide the equivalent source-side ROC/TFT split.

## Deliberately Not Implemented Here

These related Warsmash features need broader renderer/network/pathing work and should not be faked inside
`M_CheckGround`:

- movement-type-aware horizontal routing (`FLY` using flyability, `FLOAT` swimability, full `AMPH` routing);
- terrain slope pitch/roll from `elevRad`, `maxPitch`, and `maxRoll`;
- `selZ`/selection-circle height and bridge/water-aware splat placement;
- shadow bridge/water semantics;
- gradual `SetUnitFlyHeight` rate interpolation and transform takeoff/landing timers.

In particular, `AI_FLYING` currently separates air/ground dynamic collision but does not change the static WPM router.
Do not treat the altitude work as proof that flying pathfinding is complete.

## Verification

Focused tests are in `games/warcraft-3/game/tests/t_movement.c` and `t_api.c`:

- `wc3_movement.fly_height_is_added_to_support_surface`
- `wc3_movement.flyer_uses_water_surface_before_fly_height`
- `wc3_movement.float_unit_uses_water_surface_and_ignores_bridge`
- `wc3_api.fly_height_native_keeps_authored_default_separate`

A future runtime check should compare a known `movetp="fly"` unit and a `movetp="horse"` unit on flat terrain, water,
and a walkable bridge. The horse model origin should remain on the support surface; the flying model origin should remain
`moveHeight` above that support surface.

## See Also

- [WC3 Data Model](../../wc3-data-model.md)
- [WC3 Pathfinding](pathfinding.md)
- [WC3 Attack Damage](attack-damage.md)
