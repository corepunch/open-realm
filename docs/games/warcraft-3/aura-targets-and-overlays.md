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

`regen_aura_cache_update` builds one shared provider snapshot per simulation
frame (and again if the ability-data generation changes). For each in-use edict,
`aura_resolve_owned_abilities` walks the native `abilList` once, then the
runtime-added list, then ranked hero slots; it fills the 14 distinct aura-family
slots as it goes. This is equivalent to calling `S_ResolveAbilityAlias` once
per family but avoids parsing the same list for every family. The first matching
alias within each source category wins, and native abilities outrank added and
hero entries. Removed native/added skills are ignored; hero slots retain their
actual levels. Separate combat DataA/DataB consumers share the resolved ability.
The snapshot does not persist across frames: changes to runtime skills, hero
ranks and ability data are still observed on the next provider refresh.

`aura_owned_family_mask` additionally caches the rawcode-to-family bitmask in a
256-entry, generation-tagged direct-mapped lookup. It caches *classification*
(including non-aura rawcodes), not whether a particular unit owns the ability.
Native and runtime-added entries are checked with `G_ActorHasSkill` only if
the classification can provide an aura, and hero ranks are still taken live.
One rawcode can match both an explicit aura ID and a different inherited
`AbilityData.code`; both family bits must be retained to match
`S_ResolveAbilityAlias` independently for each family. A hash collision is a
miss, not a false match. Ability-data replacement changes
`G_AbilityDataGeneration()`, invalidating both positive and negative entries.
No unit, save, network or effect-lifecycle state is added.

`aura_combat_owned_slots` maps the 14 authored combat cache keys directly to
the resolved family slots (including duplicate DataA/DataB uses). Keep it in
sync with `aura_cache_keys`; the discovery regression oracle verifies that
mapping before comparing every family's alias and rank with the standalone
resolver. Tests also cover filtered ownership checks, metadata replacement,
rawcode collisions and cross-family alias matches.

`wc3_spell.aura_provider_discovery_preserves_alias_precedence_and_ranks`
compares the bulk discovery results against the standalone resolver across
native aliases, added abilities, hero ranks, removed aliases and direct
fixture edits. `combat_aura_alias_resolution_scales_with_edicts` also checks
that discovery visits each live edict at most once per cache rebuild.

`S_UpdateHeroAuraEffects` selects the strongest eligible Devotion or Unholy Aura
for each recipient and keeps that buff's `TargetArt` on the recipient. The
ability's own `TargetArt` is a second effect, spawned only on the unit that owns
the aura.

| | Brilliance Aura | Devotion Aura | Unholy Aura |
|---|---|---|---|
| Ability rawcode | `AHab` | `AHad` | `AUau` |
| Ability `TargetArt` (caster ground pattern) | `Abilities\Spells\Human\Brilliance\Brilliance.mdl` | `Abilities\Spells\Human\DevotionAura\DevotionAura.mdl` | `Abilities\Spells\Undead\UnholyAura\UnholyAura.mdl` |
| Buff rawcode | `BHab` | `BHad` | `BUau` |
| Buff `TargetArt` (recipient glow) | `Abilities\Spells\Other\GeneralAuraTarget\GeneralAuraTarget.mdl` | `Abilities\Spells\Other\GeneralAuraTarget\GeneralAuraTarget.mdl` | `Abilities\Spells\Other\GeneralAuraTarget\GeneralAuraTarget.mdl` |

ROC `War3.mpq` and TFT `War3x.mpq` both author those strings in
`Units/HumanAbilityFunc.txt` and `Units/UndeadAbilityFunc.txt`. `Targetattach`
is `origin` for each. TFT ranks set `AHad` `BuffID` to `BHad` and `AUau` to
`BUau` (`ability_audit -tft -raw AHad`). ROC `AbilityData.slk` has no `BuffID`
column, but the RoC executable has a hardcoded Devotion Aura mapping from
`AHad` to `BHad`. The missing field therefore does not mean the recipient uses
the caster's model. For RoC Devotion, render ability art once on the owner and
buff art on each eligible recipient, as TFT data does explicitly. The owner is
also a recipient when `targs` includes `friend` or `self`. The executable path
and registration evidence are documented in [RoC Aura Registrations](roc-aura-registrations.md).

This engine mapping has only been traced for `AHad`→`BHad` in this investigation.
The matching OpenRealm fallback is a stock `AHad` compatibility entry gated by
`BZ_COMPAT_WORKAROUNDS`; it applies when the resolved aura has no authored buff
ID. Explicit `BuffID` values continue to take precedence. Do not generalize the
mapping to every aura from the class registration table alone.

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

Brilliance Aura uses the same source-pattern and recipient-glow pair as
Devotion and Unholy Aura, and exposes its active recipient buff virtually in
the selected-unit status strip. Only friendly recipients with a positive max
mana value receive its mana bonus or presentation. TFT reads the authored `AHab` `BuffID`
(`BHab`); RoC has no `BuffID` column, so the `BZ_COMPAT_WORKAROUNDS` mapping
supplies `BHab` for stock `AHab`. Endurance, Vampiric, Thorns, and Command Aura
do not use these world-space overlay slots. Vampiric's extra buff `SpecialArt`
is a separate contract.

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
`wc3_spell.devotion_aura_roc_compat_uses_bhad_recipient_art` omits `BuffID` and,
with `BZ_COMPAT_WORKAROUNDS` enabled, checks the caster pattern and recipient
glow from `BHad.TargetArt`. With the flag disabled,
`wc3_spell.devotion_aura_without_compat_uses_ability_art_fallback` checks the
generic ability-art fallback.
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
