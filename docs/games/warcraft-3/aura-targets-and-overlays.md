# Aura Targets And Overlays

## Data Contract

`AbilityData.slk` stores aura range, strength, and target rules. Reign of Chaos has
one `targs` column shared by every ability rank. The Frozen Throne uses `targs1`,
`targs2`, and later rank columns. The `ability_schema` in `g_metadata.c` assigns
the shared column to all four stored levels, then lets any per-rank column
override it. A null rank mask means unrestricted targeting to
`aura_allows_target`; losing RoC's shared mask therefore changes gameplay.

For Human02, the generated `war3map.j` learns `AHad` three times for Uther
(`Huth`, Player 9).
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
for each recipient and keeps that buff's `TargetArt` on the recipient. The
ability's own `TargetArt` is a second effect, spawned only on the unit that owns
the aura.

| | Devotion Aura | Unholy Aura |
|---|---|---|
| Ability rawcode | `AHad` | `AUau` |
| Ability `TargetArt` (caster ground pattern) | `Abilities\Spells\Human\DevotionAura\DevotionAura.mdl` | `Abilities\Spells\Undead\UnholyAura\UnholyAura.mdl` |
| Buff rawcode | `BHad` | `BUau` |
| Buff `TargetArt` (recipient glow) | `Abilities\Spells\Other\GeneralAuraTarget\GeneralAuraTarget.mdl` | `Abilities\Spells\Other\GeneralAuraTarget\GeneralAuraTarget.mdl` |

ROC `War3.mpq` and TFT `War3x.mpq` both author those strings in
`Units/HumanAbilityFunc.txt` and `Units/UndeadAbilityFunc.txt`. `Targetattach`
is `origin` for each. TFT ranks set `AHad` `BuffID` to `BHad` and `AUau` to
`BUau` (`ability_audit -tft -raw AHad`). ROC `AbilityData.slk` has no `BuffID`
column, so
`hero_aura_sync_overlay` falls back to the ability `TargetArt` on every
recipient, including the caster. `hero_aura_sync_source` must not spawn a second
copy of that same model. TFT keeps both models: ability art once on the owner,
buff art on each recipient. The owner is also a recipient when `targs` includes
`friend` or `self`.

Each effect is a persistent `WC3_EFFECT_TARGET` owned by the unit it follows.
Both effects store the base rawcode (`AHad` or `AUau`) in `summon_ability`,
and `aura_effect_role` records `AURA_EFFECT_SOURCE` or `AURA_EFFECT_RECIPIENT`
when the edict is spawned. `regen_aura_cache_update` clears the slot arrays
every simulation frame and rebinds each live edict by that stable role. It does
not resolve current artwork to decide ownership. A custom alias can author a
different rune from its base ability; after removal the alias is absent, and
the base model cannot identify the old rune. Model-based classification put
that rune and the recipient glow into one slot, leaving the rune untracked
indefinitely when another provider kept the glow alive.

The saved role keeps both effects distinguishable across skill removal, art
changes, and save/load. Save format 72 serializes the role and rejects earlier
saves, following the normal no-migration policy. There is no network change.
See [Save/Load](save-load.md).

Brilliance, Endurance, Vampiric, Thorns, and Command Aura do not use this pair
of slots. Vampiric's extra buff `SpecialArt` is a separate contract.

The scenery bug made recipient art appear under crates and trees. The recipient
regression runs `monster_think` on a friendly unit and two static destructables,
then checks the buff state and attached effect edicts.

## Known Pitfalls

Issue 594 (the Devotion ground pattern missing while the glow still renders) is
this art split, not a renderer failure. `DevotionAura.mdx` has no `TXAN` chunk.
Its layers are filter mode 4 (`LAYER_BLEND_ADDALPHA` in `MDLX_SetLayerBlend`:
`GL_SRC_ALPHA`, `GL_ONE`). Geosets 0–2 sample `AuraRune10.blp`; geoset 3 samples
`AuraRuneArrow1.blp` and fades that layer with material `KMTA` across Stand
(333–1400). Both textures are BLP1 JPEG. The arrow's last mip header is 1×0 and
is skipped; levels 0–6 still upload, so the texture stays complete. An orbit
render of the real model draws the rune and the arrow ring together.

Do not "fix" this by changing MDX blend modes. Geoset `ADD` / `ADDALPHA` in
`games/warcraft-3/renderer/mdx/r_mdx_geoset.c` are intentionally the opposite of
the shared particle path in `renderer/r_particles.c`.

`mdxtool --info` counts variable-size records as exclusive. Production
`r_mdx_load.c` treats the size as inclusive, which is what loads all four
DevotionAura geosets. Do not change the loader to match the tool. `mdxtool
--frame` is an offset inside the current sequence, not `entity.frame`.
`SaveFramePNG` reads the logical window size. On a Retina display the drawable
is larger, so the PNG is the bottom-left quadrant flipped vertically. A model
cropped into a corner of that PNG is not a placement bug.

## Verification

`wc3_spell.devotion_aura_source_shows_pattern_and_recipient_keeps_glow` uses
fixture `AHad` art `panel_sprite.mdx` and `BHad` art `anim_pulse.mdx`. The caster
keeps one of each, the ally keeps only the glow, a second refresh does not
stack a copy, leaving range removes the ally glow, and clearing the ability
removes both of the caster's effects.
`wc3_spell.devotion_aura_roc_fallback_does_not_stack_on_source` omits `BuffID`
and expects one ability-art effect on the caster and one on the ally.
`wc3_spell.devotion_aura_custom_art_is_removed_while_another_source_keeps_glow`
and the matching `unholy_aura_custom_art_is_removed_while_another_source_keeps_glow`
use custom `XHfx`/`XUfx` aliases with `ui_panel.mdx` art, distinct from both the
base rune and buff glow. Removing one provider's skill retires its rune while
the other provider keeps its recipient glow alive over three scheduler
refreshes. Removing the remaining provider then clears all attached art.
Both tests repeat the lifecycle after saving two live source/recipient pairs,
clearing the runtime caches, and loading the save. The save suite separately
checks the role's field descriptor and rejects version 71.

```sh
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_spell.devotion_aura*'
build/bin/openwarcraft3-tests -data build/tests -tft +dedicated 1 +test 'wc3_spell.devotion_aura*'
```

The `+test` matcher is exact or a trailing-star prefix of `suite.name`. A
pattern that does not start with `wc3_spell.` matches nothing and still exits 0.
`make test-wc3-engine WC3_PATTERN='wc3_spell.devotion_aura*'` runs classic and
`-tft`.

```sh
build/bin/ability_audit -data 'data/Warcraft III' -roc -raw AHad
build/bin/ability_audit -data 'data/Warcraft III' -tft -raw AHad
```

Aura relations are evaluated from the provider's owner: `friend`/`allies` apply
to that player's own units and allied players, and `enemy`/`enemies` apply to
their enemies. Neutral player slots are classified exclusively as neutral even
when the default alliance table reports them as friendly. They receive an aura
only when its authored mask includes `neutral`. Human02's neutral sheep and
Human03's neutral passive villagers are not recipients of Devotion Aura in retail.
Neutral units remain distinct from static scenery; a mask can still explicitly
include them.

The map-script regressions mirror the authored cases: Human02's Uther (`Huth`) on
Player 9 with level-three `AHad` versus Neutral Passive `nshe`, and Human03's
`Hart` on Player 1 with level-three `AHad` versus Neutral Passive `nvil`. Each
also checks a same-owner ground unit as a positive control.

See [Regeneration Auras And Fountains](regeneration-auras.md) for the other aura
families and [Adding Warcraft III Abilities](ability-implementation.md)
for the RoC/TFT data workflow.
