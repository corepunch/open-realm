# Warcraft III Area-Targeting Preview

## Contract and ownership

While a point-target spell with an authored positive `Area` is being aimed, the
local cursor displays a terrain decal and temporarily colours world models
whose selection footprints overlap its area green. The preview is visual
feedback, **not** the authoritative spell-target filter: invulnerable or
otherwise untargetable actors may still be tinted. Clicking/cancelling does
not modify any unit's scripted vertex colour or the spell's gameplay effects.

The spell game module resolves the caster's `UnitData.race` and the current
ability level's `Area`; the selected player does not always share the caster's
race (mixed-race armies, Charm, custom maps). `Theme_UnitString` looks up
`PlacementCursor` in `war3mapSkin.txt`'s `[CustomSkin]` first, then that
caster's `UI\\war3skins.txt` section (`Human`, `Orc`, `Undead`, `NightElf`),
then `Default`, with the same edition-specific `_V0`/`_V1` fallback used by
other UI art. The stock data supplies separate BLPs for each race, under
`ReplaceableTextures\\Selection`:

| Race | Stock art |
| --- | --- |
| Human | `SpellAreaOfEffect.blp` |
| Orc | `SpellAreaOfEffect_Orc.blp` |
| Undead | `SpellAreaOfEffect_Undead.blp` |
| Night Elf | `SpellAreaOfEffect_NE.blp` |

Unknown races use `Default`/map overrides, not an invented Naga texture.

## Data flow

1. `spell_cmd` or `AbilityAttackGround` selects an authoritative positive
   area from spell `Area` or weapon splash bands; absent or invalid area
   disables the decal. `AbilityChannel` retains its explicit test preview.
2. `S_SpellCursorSplat` resolves the image with the selected caster's race,
   registers it and unicasts the existing `svc_cursor_splat` (`image`, `radius`)
   to the commanding client. No entity/player snapshot fields are added.
3. `CL_ParseCursorSplat` caches the generic decal metadata. `CL_AddCursorSplat`
   reserves a drawable slot; `CL_RefreshCursorSplat` traces mouse position and
   updates the decal **every render**, including while gameplay is paused.
   The shared client exposes only `viewDef.cursor_area_origin` and
   `cursor_area_radius` to the selected renderer, without Warcraft semantics.
4. `games/warcraft-3/renderer/r_game.c` applies `WC3_AreaPreviewGreen` only
   to a **draw-local copy** of an MDX `renderEntity_t` whose X/Y footprint
   intersects the area. Its normal RGBA and team-colour rendering survive.
   The authored snapshot tint and JASS `SetUnitVertexColor` are unmodified.
5. Moving the cursor away, moving over gameplay UI, failed world trace,
   cancellation, and leaving targeting mode make the area radius zero or
   stop intersection. No prior highlighted-entity list needs cleanup.

World models including ordinary units, structures and destructibles are
considered if they are visible render entities with nonzero snapshot numbers and a positive selection radius. This excludes
most purely visual projectiles/temporary effects that have zero radius.
The footprint is currently the snapshot **selection radius** times model
scale; that radius is retained for trees with zero gameplay collision due to
baked pathing textures (`SP_SpawnDestructable`). Do **not** use the destructible's
`collision` field as the visual radius. This is conservative circle overlap,
not exact rectangular building pathing or pixel-art boundaries.

## Known limitations and retail verification

The green blend (25% authored RGB + 75% green) is a provisional visual match, not a
measured retail RGBA constant. The overlap formula (center distance <=
AoE radius + selection radius) is an approximation supported by enlarged-unit
observations; exact rectangular footprints need retail comparison. Whether
regular noninteractive doodads should tint, whether Attack Ground universally
highlights, and the exact fog/invisibility boundary remain unverified. The
preview never overrides `RF_HIDDEN` and adds no visibility/reveal state.

## Regression coverage and manual comparison

- `wc3_area_preview.*` tests the intersection edge, disabled radius, transient
  tint alpha preservation, caster race lookup, Default and map skin override.
- Compare Human Blizzard, Orc spell, Undead Death and Decay, Night Elf Starfall
  targeting where applicable; mixed-race control must use the caster's art.
- Compare tree (zero gameplay collision), structure, gold mine, ordinary and
  enlarged unit, building edge, hidden unit, and neutral destructible.
- Move the targeting cursor across objects, over the HUD, cancel with Escape,
  then cast a new spell and verify no green tint remains on previous targets.
- Repeat while paused and with both RoC and TFT game data mounted.

The tests validate pure math and skin precedence, not GPU framebuffer colour.
The developer performs compile, `make test`, and actual retail visual checks.

See also [Ability implementation](ability-implementation.md),
[MDX Materials](mdx-materials.md), and
[Game/client boundary](../../architecture/game-client-boundary.md).
