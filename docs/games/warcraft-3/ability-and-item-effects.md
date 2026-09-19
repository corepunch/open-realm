# WC3 Ability, Buff, And Item Presentation Effects

## Contract

For the gameplay distinction between an ability and its applied buff, including
the demo's `Ablo`/`Bblo` Bloodlust registrations, see
[Abilities versus buffs](demo-ability-classes.md#abilities-versus-buffs).

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
| `WC3_EFFECT_LIGHTNING` | dedicated `LightningData.slk` ribbon path; ability code selects authored lightning rawcodes and shared rendering consumes resolved endpoints |

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
- regeneration auras (`Aoar`, `Aabr`, `Aarm`, including Fountain aliases): the
  selected recipient `buffID` supplies persistent `WC3_EFFECT_TARGET` art while the
  recipient remains inside the live aura.
- Natural Neutral Hostile creep sleep: persistent hidden `ACsp` `WC3_EFFECT_TARGET`
  attached at `overhead`; wake/behavior replacement destroys the effect through
  the same independent-effect lifecycle.  See [Neutral Creep Sleep](creep-sleep.md).
- Haunted Gold Mine (`Abgm`): one persistent `WC3_EFFECT_EFFECT` is placed at
  each authored Acolyte mining-ring slot using the slot's radial facing. The
  effect edicts are owned by the mine and destroyed through the normal effect
  death lifecycle when the Haunted Mine dies or is removed.
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

Weather effects use a separate map-lifetime handle/renderer path rather than effect edicts: W3I/W3R/JASS weather is keyed by `TerrainArt\Weather.slk`, carried in the per-frame game datagram, and emitted through the shared particle pool. See [Weather](weather.md) for the implemented fields and deliberate compatibility gaps. Warcraft lightning now uses the same ownership principle with a distinct endpoint registry: game code resolves `LightningEffect` rawcodes and endpoints, the datagram carries presentation-only `wc3LightningEffect_t` records, and `r_lightning.c` resolves `Splats\LightningData.slk` into a generic camera-facing textured ribbon. Lightning remains separate from the MDX model-effect resolver because it has two moving endpoints rather than one model transform.

## Item Use And Charges

Item object data remains authoritative for item properties. `abilList` is an
`ItemData.slk` column, so active command dispatch resolves it through the typed
`ItemData_t.abilList` row first. `FindConfigValue(itemRawcode, "abilList")` is
retained only as a compatibility fallback for custom/legacy data; that helper
searches TXT/INI configuration and is not an authoritative ItemData SLK lookup.
Immediate item abilities declare `AB_ITEM` and handle `A_ITEM_USE` when they can
synchronously report whether the gameplay effect actually happened. The
callback is append-only at the end of `ability_t`; existing dispatch-field
offsets must not be changed.

The inventory command walks `abilList` in authored order and uses the first registered ability it can handle. For a synchronous `A_ITEM_USE` procedure case:

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

Asynchronous item abilities that enter a targeting command through `AB_COMMAND`/`A_COMMAND` are still dispatched, but their eventual success cannot be known by the inventory click handler. This slice intentionally does not consume their charges or publish success events at click time. The eventual targeted-item completion path needs to own those operations.

Spell command dispatch has a similar rawcode boundary: a WC3 FourCC held in a
`DWORD` is not a C string. Runtime lookup must convert it through
`GetClassName(code)` and `FindAbilityForCommand`, rather than casting `&code` to
`LPCSTR`. The latter reads beyond the four rawcode bytes and made a command such
as Holy Light (`AHhb`) fail or succeed depending on unrelated stack contents.

## Farseer Ability Notes

The uploaded Warsmash reference implements `AOcl` Chain Lightning and `AOsf` Feral Spirit, but does not register implementations for `AOfs` Far Sight or `AOeq` Earthquake. OpenRealm therefore uses Warsmash as the mechanical reference where it exists and keeps the latter two data-driven rather than claiming Warsmash parity.

Chain Lightning reads damage from Data A, target count from Data B, per-jump damage reduction from Data C, and jump radius from Area. The first target is struck at spell effect time; subsequent targets are selected and damaged on a save-safe 250 ms server cadence matching Warsmash's `SECONDS_BETWEEN_JUMPS = 0.25`. Internal no-client marker edicts retain pointer plus spawn-generation identity for every previously struck target, so delayed selection cannot jump back to an earlier unit or confuse a reused edict slot with that earlier unit. The chain remains capped at 32 targets. Every delayed jump rechecks the authored target mask as well as enemy/alive/range/visited state. OpenRealm applies target art for each struck unit, plays the authored one-shot `Effectsound`, and now emits the ability's authored `LightningEffect` entries: index 0 for caster-to-primary-target and index 1 for later jumps, each with the Warsmash Chain Lightning two-second visual lifetime. The game-owned lightning registry is save-safe and published through the presentation datagram; the renderer resolves `Splats\LightningData.slk` texture, width and RGBA into an additive camera-facing ribbon. The renderer synthesizes a bounded procedural polyline using authored `AvgSegLen`, `Width`, `NoiseScale`, `TexCoordScale`, RGBA, and `Duration`, so the stock `CLPB` and `CLSB` rows produce the intended thicker primary and thinner secondary blue-white bolts. The same registry backs JASS `AddLightning`/`AddLightningEx`, movement, color, destruction, and hashtable/save identity. Unit-target spell clicks are accepted before the caster is in range: the caster follows the selected unit with the normal movement path and commits the spell once the authored cast range is reached. Replacing that movement order cancels the pending cast.

Far Sight snapshots the casting player, target point, authored Area, and normal duration into an independent thinker. The thinker owns expiration, while `G_FowUpdate` reapplies the active reveal after rebuilding ordinary unit sight and before script fog modifiers, so frame ordering cannot reduce Far Sight to a one-frame reveal. The reveal uses the existing shared-vision propagation in `G_FowSetStateRadius` and no longer depends on the caster remaining alive after the cast. The same live Far Sight thinker is a player-aware true-sight source: the casting player and players receiving that player's shared vision can detect invisible units inside its authored Area for the same saved lifetime. Known gameplay invisibility (`Apiv`, `Binv`, `BOwk`, Sentry Ward, or Stasis Trap) is evaluated per viewer for networking, selection, automatic acquisition, attack continuation, and spell target checks; owners/shared-vision allies retain access and hostile viewers require true sight. `RF_HIDDEN` is cleared only in an eligible viewer's outgoing snapshot, so detection cannot accidentally expose cargo, mine workers, training/revival placeholders, or other non-invisibility users of that broad render flag. The thinker now also owns the authored persistent `Areaeffectart` (falling back to `EffectArt`) plus one-shot/looped ability sound presentation and destroys that presentation on expiry.

Feral Spirit reads its summoned unit from UnitID, count from Data B, lifetime from the normal duration field, and spawn distance from Area. Recasting kills surviving summons created by the caster's previous `AOsf` cast, then creates the new summons at the same authored point in front of the caster, matching the Warsmash reference, and requests SpecialArt on each summon. Summons retain their source ability rawcode so replacement does not accidentally kill unrelated summons of the same unit type. The generic attack path consumes stock creep Critical Strike (`ACct`), which makes the authored Dire/Shadow Wolf critical-strike ability functional. Stock Permanent Invisibility (`Apiv`) is registered as a passive and represented by a dedicated per-unit reveal deadline instead of `RF_HIDDEN`: after the authored transition time on spawn the unit is invisible to hostile viewers unless detected, while starting an attack or committing a spell restarts that authored reveal/transition window. Undetected units are excluded from hostile automatic acquisition, ongoing hostile attack validity, unit-target spell validation, networking, and selection; owners/shared-vision allies retain access, and Far Sight/passive detectors expose the unit only to the corresponding viewer. A zero authored transition keeps the unit continuously invisible across attacks/casts, while a negative transition disables entry into permanent invisibility. The reveal deadline is authoritative saved state, so save/load cannot make a revealed Shadow Wolf vanish early or reset its transition.

Earthquake remains a fixed-point channel. Data A supplies the initial effect delay, Data B supplies the per-second structure/destructable damage, Data C supplies the movement-speed reduction, Area supplies the affected radius, and the normal duration controls channel lifetime. After the authored delay, each one-second tick damages enemy structures and tree/debris destructables in the area and refreshes the Earthquake buff only on enemy ground units; air units are not slowed. The movement path consumes the active `BOeq` reduction for both ordinary and group-speed calculations and preserves Warcraft's 140 minimum speed for units that were authored faster than that threshold. The channel thinker now owns persistent authored `Areaeffectart` (falling back to `EffectArt`), plays `Effectsound`, attaches `Effectsoundlooped` through generic snapshot-synchronised looping audio, and tears those resources down on interruption or natural completion. Data D's specialized final-area terrain deformation/presentation and broader destructable target-mask nuances remain gaps.

Chain Lightning's `CLPB`/`CLSB` records now feed a bounded procedural lightning polyline. `AvgSegLen` controls point density, `NoiseScale` supplies deterministic animated jitter, `Width` controls the beam thickness, `TexCoordScale` tiles and advances `Lightning.blp`, and `Duration` drives the authored fade lifetime. The shared registry also owns JASS lightning handles, so spell and script bolts use the same renderer and save/hashtable identity.

## Known Gaps

The following are deliberately outside this implementation slice:

- arbitrary MDX attachment-token resolution and team-coloured spell-effect attachment;
- generic binding of buff lifetime to persistent world-art ownership and non-stacking FX
  beyond explicitly owned lifecycles such as natural creep sleep;
- ability/buff `EffectSound` and `EffectSoundLooped`;
- broader invisibility families such as Ghost/Ghost Visible and Shadow Meld remain separate from the `Apiv`/`Binv`/`BOwk`/ward visibility paths covered here;
- Earthquake Data D/final-area specialized terrain presentation remains separate from the now-implemented persistent area-art/looped-sound ownership;
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
- `wc3_movement.haunted_mine_uses_acolyte_ring_slots_and_parent_gold`
- `wc3_api.effect_natives_return_independent_handles`
- `wc3_items.perishable_success_consumes_charge_and_removes_at_zero`
- `wc3_items.nonperishable_use_decrements_charges_but_keeps_item_at_zero`
- `wc3_items.inventory_click_uses_itemdata_ability_list_and_applies_scroll`
- `wc3_spell.holy_light_rawcode_lookup_is_nul_safe`
- `wc3_spell.chain_lightning_bounces_respect_authored_target_mask`
- `wc3_spell.chain_lightning_delays_each_jump_and_never_rehits_previous_targets`
- `wc3_spell.chain_lightning_stops_if_caster_slot_is_reused`
- `wc3_spell.far_sight_reapplies_visibility_until_authored_duration_expires`
- `wc3_spell.far_sight_detects_permanent_invisibility_only_for_its_viewers`
- `wc3_spell.permanent_invisibility_uses_authored_transition_after_spawn_and_reveal`
- `wc3_spell.active_spell_commit_restarts_permanent_invisibility_transition`
- `wc3_spell.sentry_true_sight_makes_known_rf_hidden_invisibility_selectable_for_viewer`
- `wc3_spell.true_sight_snapshot_and_selection_are_viewer_local`
- `wc3_spell.permanent_invisibility_blocks_hostile_acquisition_and_spell_targets_until_detected`
- `wc3_spell.true_sight_only_reveals_rf_hidden_states_known_to_be_invisibility`
- `wc3_spell.earthquake_waits_for_effect_delay_slows_ground_and_damages_structures_and_trees`
- `wc3_spell.hero_passives_use_authored_data_and_runtime_consumers` (includes stock `ACct`)
- `wc3_save.round_trip_entity_c_callbacks` (includes live Far Sight/delayed Chain Lightning thinker state, visited-target marker identity, and the Permanent Invisibility reveal deadline)
- `wc3_api.game_datagram_carries_and_expires_lightning_snapshot`
- `wc3_save.lightning_registry_round_trip`

The requested workflow for this patch deliberately did not compile or execute tests. When validating manually, cover Chain Lightning against mixed air/ground candidates, verify its quarter-second jump cadence, primary/secondary authored lightning textures, and two-second bolt lifetime; Far Sight for its full authored duration, persistent area art, sound, true sight of an invisible enemy, and shared-vision locality; Earthquake against ground/air units plus structures and trees, persistent area art/looped audio, and interruption cleanup; level-2/3 Feral Spirit critical strikes; Shadow Wolf visibility before/during/after attacks and spell casts, including detection; and save/load while Far Sight, delayed Chain Lightning/lightning presentation, or a revealed `Apiv` unit is active.
