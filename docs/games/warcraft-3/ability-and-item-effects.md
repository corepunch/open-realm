# WC3 Ability, Buff, And Item Presentation Effects

## Contract

Warcraft III gameplay owns effect timing and semantics. The renderer never decides that an art model deals damage, heals a unit, applies a buff, or consumes an item. Game code first performs or validates the simulation action and then requests presentation by Warcraft rawcode and effect slot.

The game-side effect selector is `wc3EffectType_t` in `games/warcraft-3/game/g_local.h`:

| Effect type | Warcraft presentation field |
|---|---|
| `WC3_EFFECT_EFFECT` | `EffectArt` / `Effectart` |
| `WC3_EFFECT_TARGET` | `TargetArt` / `Targetart` |
| `WC3_EFFECT_CASTER` | `CasterArt` / `Casterart` |
| `WC3_EFFECT_SPECIAL` | `SpecialArt` / `Specialart` |
| `WC3_EFFECT_AREA_EFFECT` | `AreaEffectArt` / `Areaeffectart` |
| `WC3_EFFECT_MISSILE` | `MissileArt` / `Missileart` |
| `WC3_EFFECT_LIGHTNING` | reserved; dedicated lightning rendering is not implemented |

`games/warcraft-3/game/g_effects.c` is the common WC3 presentation entry point. It keeps content selection in the game module: it resolves an art path, registers that model, and exposes the result as an ordinary game edict. No spell rawcode or WC3 effect category is added to shared engine state.

This is adjacent to, but intentionally different from, `entityState_t.effect/effect_flags`: that compact server-selected effect channel is useful when presentation is an attribute of another entity, such as building damage fire. Spell/JASS effects need independent handles and lifetimes, so they are represented by independent edicts instead.

See also [Server-Selected Presentation Effects](../../architecture/server-selected-effects.md) and [Inventory And Items](inventory-and-items.md).

## Data Flow

Ability-authored presentation follows:

```text
ability/rawcode + wc3EffectType_t + index
    -> G_AbilityEffectArt
    -> Ability Func configuration lookup
    -> alias field, then base AbilityData.code field as fallback
    -> comma-separated art selection
    -> G_RegisterModel
    -> independent effect edict
```

`G_AbilityEffectArt` first checks the requested ability alias. Custom ability aliases can therefore override their base ability's art. If the alias has no value, `AbilityData.code` is used as a fallback. If an art list contains multiple comma-separated paths, `index` selects the requested element; an out-of-range index falls back to the final configured element. Empty/`-`/`_` entries do not create effects.

Typed `AbilityBuffData` now retains the buff `TargetArt`, `SpecialArt`, `EffectArt`, and `Missileart` columns in addition to the existing icon/tooltip fields. If no ability Func value is found, the resolver checks the requested buff rawcode (and its buff `code` base row where present). This enables generic JASS/buff rawcode art lookup without making a buff model authoritative for gameplay lifetime. Attachment metadata and effect sounds are not normalized by this slice.

## Effect Edict Lifecycle

`G_SpawnModelEffect` creates an independent edict for a model path. `G_SpawnAbilityEffectAtPoint` and `G_SpawnAbilityEffectTarget` add the Warcraft ability-data lookup above it.

A target effect:

- copies the target's position/facing;
- uses `MOVETYPE_LINK` to follow the target;
- keeps the target's `spawn_time` as a generation guard so a recycled edict slot cannot become the new attachment target;
- frees itself if that target ceases to be the same live edict.

A point effect uses the supplied world X/Y and `CM_GetHeightAtPoint` for Z.

Temporary effects try `birth`, then `stand`, and free when that sequence completes. If neither sequence exists, the temporary edict is freed rather than leaked indefinitely.

Persistent effects try `birth`, transition to looping `stand`, and remain until `G_DestroyEffect`. Destruction detaches the effect and attempts `death`; if no death sequence exists, the edict is freed immediately.

The generic game-side attachment implementation currently recognizes only `"overhead"`, represented as a vertical offset while following the target. Other attachment strings are accepted by JASS but currently fall back to the target origin. Full MDX attachment-token matching (`origin`, `chest`, `hand,left`, and similar) is intentionally deferred until the renderer/model attachment contract is implemented generically.

## Production Callers

The following existing abilities now use the common resolver rather than owning separate art-path lookup code:

- Holy Light: `WC3_EFFECT_TARGET` on the affected unit.
- Blink: `WC3_EFFECT_SPECIAL` before relocation and `WC3_EFFECT_AREA_EFFECT` after relocation.
- Devotion Aura: persistent `WC3_EFFECT_TARGET` on the caster.
- Thunder Bolt / Fire Bolt: `WC3_EFFECT_MISSILE` supplies the existing projectile edict's model; projectile speed, tracking, damage, stun, and impact lifecycle remain in `s_thunderbolt.c`.
- supported immediate item abilities in `s_item.c`: `WC3_EFFECT_TARGET` after a successful gameplay effect.
- Scroll of Protection (`spro` / `AIda`): the item ability applies its authored
  area/duration `Bdef` status to allowed friendly targets and uses the same
  `TARGET` art resolver. Combat and the HUD derive the temporary armor bonus
  from the live status, so expiry needs no extra callback/save field.

These migrations are deliberately presentation-only. They do not change damage/healing calculations, target validation, projectile movement, or spell cooldown/mana behavior.

## JASS Effect Handles

`games/warcraft-3/game/api/api_effect.h` now routes the following natives through independent effect edicts:

- `AddSpecialEffect`
- `AddSpecialEffectLoc`
- `AddSpecialEffectTarget`
- `DestroyEffect`
- `AddSpellEffect`
- `AddSpellEffectLoc`
- `AddSpellEffectById`
- `AddSpellEffectByIdLoc`
- `AddSpellEffectTarget`
- `AddSpellEffectTargetById`

`AddSpecialEffect*` takes an explicit model path and does not consult ability data. `AddSpellEffect*` takes an ability rawcode/string plus a converted effect type and resolves the corresponding ability presentation field. A returned JASS `effect` handle points at the independent effect edict, not at the target unit. Destroying the effect therefore cannot overwrite or clear the target unit's `model2` state.

Weather effects now use a separate map-lifetime handle/renderer path rather than effect edicts: W3I/W3R/JASS weather is keyed by `TerrainArt\Weather.slk`, synchronized through a renderer game command, and emitted through the shared particle pool. See [Weather](weather.md) for the implemented fields and deliberate compatibility gaps. `LIGHTNING` remains unsupported by the model-effect resolver because Warcraft lightning requires a dedicated endpoint/colour/movement lifecycle rather than an MDX model path.

## Item Use And Charges

Item object data remains authoritative for item properties. `abilList` is an
`ItemData.slk` column, so active command dispatch resolves it through the typed
`ItemData_t.abilList` row first. `FindConfigValue(itemRawcode, "abilList")` is
retained only as a compatibility fallback for custom/legacy data; that helper
searches TXT/INI configuration and is not an authoritative ItemData SLK lookup.
Immediate item abilities use the `ability_t.item_use` callback when they can
synchronously report whether the gameplay effect actually happened. The
callback is append-only at the end of `ability_t`; existing dispatch-field
offsets must not be changed.

The inventory command walks `abilList` in authored order and uses the first registered ability it can handle. For a synchronous `item_use` callback:

```text
inventory click
    -> item ability validates carrier/state
    -> gameplay effect succeeds
    -> optional ability TARGET art
    -> EVENT_PLAYER_UNIT_USE_ITEM / EVENT_UNIT_USE_ITEM
    -> G_ConsumeItemCharge
```

Failed uses do not publish use-item events and do not consume a charge. For example, a healing item at full health returns failure.

`G_ConsumeItemCharge` decrements a positive runtime charge count after successful use. When the final charge belongs to a `perishable` item, the item is removed through `G_RemoveItem`, which also reverses passive item-stat hooks and clears the inventory slot. A non-perishable item also decrements to zero but remains present.

Legacy/asynchronous item abilities that enter a targeting command through `ability_t.cmd` are still dispatched, but their eventual success cannot be known by the inventory click handler. This slice intentionally does not consume their charges or publish success events at click time. The eventual targeted-item completion path needs to own those operations.

Spell command dispatch has a similar rawcode boundary: a WC3 FourCC held in a
`DWORD` is not a C string. Runtime lookup must convert it through
`GetClassName(code)` and `FindAbilityForCommand`, rather than casting `&code` to
`LPCSTR`. The latter reads beyond the four rawcode bytes and made a command such
as Holy Light (`AHhb`) fail or succeed depending on unrelated stack contents.

## Known Gaps

The following are deliberately outside this implementation slice:

- arbitrary MDX attachment-token resolution and team-coloured spell-effect attachment;
- generic binding of buff lifetime to persistent world-art ownership and non-stacking FX;
- ability/buff `EffectSound` and `EffectSoundLooped`;
- Warcraft lightning effects;
- item `cooldownID` / `ignoreCD` shared cooldown behavior;
- automatic `powerup` acquisition/use;
- asynchronous targeted-item success/charge completion;
- spell cast-point/backswing timing changes;
- a fully generalized missile-art/arc object separate from existing projectile simulation.
- save/load rebinding for independent effect-edict animation callbacks and persistent effect ownership.

Do not work around these by adding spell-specific asset paths to the renderer or new WC3-specific flags to shared engine structs. Extend the game-side effect resolver/lifecycle instead.

## Verification

Relevant in-engine tests are:

- `wc3_slk.ability_buff_ui_columns_decode`
- `wc3_effects.ability_effect_art_selects_requested_entry_and_last_fallback`
- `wc3_api.effect_natives_return_independent_handles`
- `wc3_items.perishable_success_consumes_charge_and_removes_at_zero`
- `wc3_items.nonperishable_use_decrements_charges_but_keeps_item_at_zero`
- `wc3_items.inventory_click_uses_itemdata_ability_list_and_applies_scroll`
- `wc3_spell.holy_light_rawcode_lookup_is_nul_safe`

The requested workflow for this patch deliberately did not compile or execute tests. When validating manually, cover at least Holy Light, Blink, Thunder Bolt, a healing consumable with more than one charge, its final-charge removal, and a JASS map that creates/destroys point and target special effects.
