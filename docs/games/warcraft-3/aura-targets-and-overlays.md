# Aura Targets And Overlays

## Data Contract

`AbilityData.slk` stores aura range, strength, and target rules. Reign of Chaos has
one `targs` column shared by every ability rank. The Frozen Throne uses `targs1`,
`targs2`, and later rank columns. The `ability_schema` in `g_metadata.c` assigns
the shared column to all four stored levels, then lets any per-rank column
override it. A null rank mask means unrestricted targeting to
`aura_allows_target`; losing RoC's shared mask therefore changes gameplay.

For Human02, the generated `war3map.j` learns `AHad` three times for Arthas.
The RoC `AHad` row has `targs=air,ground,friend,self,vuln,invu` and `Area3=900`.
Before the shared-column correction, `G_AbilityLevel(AHad, 3)->targs` was null.
`ability_audit` now uses the same schema mapping; its RoC rank-three line should
report the shared target mask. The bounded authoritative-data check is:

```sh
build/bin/ability_audit -data 'data/Warcraft III' -roc -raw AHad
```

The local regression fixture in `t_slk.c` carries the RoC shared column and
separate TFT rank columns. `t_spell.c` checks rank-three Devotion Aura against a
ground unit and a disallowed target type.

## Recipient And Effect Lifecycle

`monster_think` calls `S_RunAbilityUpdates` for units and destructables because
both use the animation clock. `S_UpdateUnitPassiveEffects` is the aura
presentation entry point; `S_AuraUnitActive` is shared by numeric aura effects
and presentation target filtering. Both exclude `SVF_STATIC_SCENERY`, which
map doodads and destructables receive during spawn. A non-null
`data.UnitBalance` pointer is not a unit test: `G_UnitBalance` returns a static
zero row for unknown rawcodes, and `G_BindEntityData` binds that pointer to
scenery too.

`S_UpdateHeroAuraEffects` selects the strongest eligible Devotion or Unholy Aura
source, resolves `BuffID` target art when present, and maintains one effect
edict attached to each eligible recipient. RoC `AHad` has no `BuffID` column;
the existing art fallback resolves `AHad.TargetArt`, the gold Devotion Aura
rune. The scenery bug made those runes appear under crates and trees. The
recipient regression runs `monster_think` on a friendly unit and two static
destructables, then checks the buff state and attached effect edicts.

Neutral Passive is passively allied with map players by default, so living
neutral units such as sheep can still qualify for a friendly aura if their
target type matches the authored mask. They are distinct from static scenery.

See [Regeneration Auras And Fountains](regeneration-auras.md) for the other aura
families and [Adding Warcraft III Abilities](ability-implementation-plan.md)
for the RoC/TFT data workflow.
