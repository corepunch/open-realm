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
- Wisp Harvest (`Awha`): persistent `WC3_EFFECT_TARGET` follows the reserved
  tree and uses `DataC` as its vertical attachment offset. The same effect
  carries authored `EffectSoundLooped`, so retask/removal destroys the model
  and stops the looping harvest sound through one lifecycle.
- Moon Well (`Ambt`): persistent race-indexed `WC3_EFFECT_EFFECT` follows the
  well and uses `DataD * mana fraction` as its vertical offset; successful
  replenish also emits temporary caster/special art.
- Entangle Gold Mine (`Aent`): persistent `WC3_EFFECT_CASTER` follows the
  casting tree and is owned by the entangled overlay so mine teardown removes
  it deterministically. The overlay separately owns the caster/ability
  relationship used for hidden/permanent command state, so gameplay semantics
  do not depend on an art field being present.
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

The Undead01 intro trace showed two `MassTeleportCaster` effects being added, but cleanup called `DestroyEffect` only for the later handle. The earlier JASS handle was overwritten and lost. Its flagged non-looping `Stand` now stops rendering at the sequence boundary, while its edict remains live because no `DestroyEffect` can reach it.

TODO: Decide whether the engine should diagnose or reclaim effects whose last JASS handle reference is overwritten. A variable reassignment alone does not prove the handle is lost; another JASS variable or array entry may still alias it. The JASS VM currently exposes global assignment names at its value-copy point, so an opt-in overwrite trace is feasible for globals; local and array assignments need additional instrumentation.

Weather effects use a separate map-lifetime handle/renderer path rather than effect edicts: W3I/W3R/JASS weather is keyed by `TerrainArt\Weather.slk`, carried in the per-frame game datagram, and emitted through the shared particle pool. See [Weather](weather.md) for the implemented fields and deliberate compatibility gaps. Warcraft lightning now uses the same ownership principle with a distinct endpoint registry: game code resolves `LightningEffect` rawcodes and endpoints, the datagram carries presentation-only `LIGHTNINGEFFECT` records, and `r_lightning.c` resolves `Splats\LightningData.slk` into a generic camera-facing textured ribbon. Ability bolts retain entity pointers plus spawn-generation identities so their endpoints follow moving units safely after save/load; explicitly positioned JASS bolts remain coordinate-driven. Lightning remains separate from the MDX model-effect resolver because it has two moving endpoints rather than one model transform.

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

The inventory command walks `abilList` in authored order and uses the first registered ability it can handle. Successful immediate and command-backed item uses converge on `G_CompleteItemUse`:

```text
inventory click
    -> item ability validates carrier/state
    -> immediate effect OR targeted spell command
    -> gameplay A_EXECUTE succeeds
    -> optional ability TARGET art
    -> EVENT_PLAYER_UNIT_USE_ITEM / EVENT_UNIT_USE_ITEM
         source = originating item
    -> charge/perishable completion
```

Speed-family powerups are a deliberate exception to inventory-slot insertion: `AIsp`/`AIsa`/`APsa` resolve directly against the acquiring unit, use authored `Area`/`Dur`/`HeroDur`/`BuffID`/target-mask data, and retain the consumed item handle until queued pickup/use events release it. The active status drives movement to `Misc.MaxUnitSpeed` (including `war3mapMisc.txt` overrides) for its lifetime. Other unsupported powerup families are not auto-consumed.

Failed uses do not publish use-item events and do not consume a charge. For example, a healing item at full health returns failure. Targeted item commands preserve the source item and its spawn generation while the shared spell path walks into range, so cancellation, a rejected target, or a stale/moved item cannot consume a charge accidentally.

`G_ConsumeItemCharge` remains the ordinary charge helper. `G_CompleteItemUse` adds event-context lifetime semantics: when a successful use consumes the final charge of a perishable item, the item leaves the carrier immediately but its handle remains valid until queued use-item events and any sleeping JASS response coroutine are finished. This is required for retail-style `GetManipulatedItem()` conditions such as Orc08's Soul Gem trigger. Non-perishable items decrement to zero and remain present.

`AIso` and `Asou` share the Soul Trap lifecycle in `skills/s_item.c`: successful targeted use keeps the target edict alive but out of world interaction, binds the filled `soul` item, reveals the carrier to the trapped unit's owner, and releases the target at carrier death. The stock target rules and Orc08 item-event identity are covered in [Inventory And Items](inventory-and-items.md#soul-gem-gsou-soul-aiso-asou); custom target-mask edge cases remain a separate parity area.

Spell command dispatch has a similar rawcode boundary: a WC3 FourCC held in a
`DWORD` is not a C string. Runtime lookup must convert it through
`GetClassName(code)` and `FindAbilityForCommand`, rather than casting `&code` to
`LPCSTR`. The latter reads beyond the four rawcode bytes and made a command such
as Holy Light (`AHhb`) fail or succeed depending on unrelated stack contents.

## Farseer Ability Notes

The uploaded Warsmash reference implements `AOcl` Chain Lightning and `AOsf` Feral Spirit, but does not register implementations for `AOfs` Far Sight or `AOeq` Earthquake. OpenRealm therefore uses Warsmash as the mechanical reference where it exists and keeps the latter two data-driven rather than claiming Warsmash parity.

Chain Lightning reads damage from Data A, target count from Data B, per-jump damage reduction from Data C, and jump radius from Area. The first target is struck at spell effect time; subsequent targets are selected and damaged on a save-safe 250 ms server cadence matching Warsmash's `SECONDS_BETWEEN_JUMPS = 0.25`. Each delayed jump chooses the nearest valid unvisited enemy in the authored jump radius, with edict order breaking exact distance ties. Internal no-client marker edicts retain pointer plus spawn-generation identity for every previously struck target, so delayed selection cannot jump back to an earlier unit or confuse a reused edict slot with that earlier unit. The chain remains capped at 32 targets. Every delayed jump rechecks the authored target mask as well as enemy/alive/range/visited state. OpenRealm applies target art for each struck unit, plays the authored one-shot `Effectsound`, and emits the ability's authored `LightningEffect` entries: index 0 for caster-to-primary-target and index 1 for later jumps. The renderer resolves `Splats\LightningData.slk` and the stock `CLPB`/`CLSB` rows (`AvgSegLen=100`, widths 50/30, `NoiseScale=0.05`, `TexCoordScale=0.5`, `Duration=2`) into an additive `Lightning.blp` ribbon. Geometry changes in deterministic ~55 ms crackle steps rather than continuously waving; stock noise uses width-scaled lateral kinks with tapered endpoints and small along-bolt displacement, while the authored texture scrolls along the ribbon and opacity fades near the end of the authored lifetime. The same registry backs JASS `AddLightning`/`AddLightningEx`, movement, color, destruction, and hashtable/save identity. Unit-target spell clicks are accepted before the caster is in range: the caster follows the selected unit with the normal movement path and commits the spell once the authored cast range is reached. Replacing that movement order cancels the pending cast.

Far Sight snapshots the casting player, target point, authored Area, and normal duration into an independent thinker. The thinker owns expiration, while `G_FowUpdate` reapplies the active reveal after rebuilding ordinary unit sight and before script fog modifiers, so frame ordering cannot reduce Far Sight to a one-frame reveal. The reveal uses the existing shared-vision propagation in `G_FowSetStateRadius` and no longer depends on the caster remaining alive after the cast. The same live Far Sight thinker is a player-aware true-sight source: the casting player and players receiving that player's shared vision can detect invisible units inside its authored Area for the same saved lifetime. Known gameplay invisibility (`Apiv`, `Binv`, `BOwk`, Sentry Ward, or Stasis Trap) is evaluated per viewer for networking, selection, automatic acquisition, attack continuation, and spell target checks; owners/shared-vision allies retain access and hostile viewers require true sight. `RF_HIDDEN` is cleared only in an eligible viewer's outgoing snapshot, so detection cannot accidentally expose cargo, mine workers, training/revival placeholders, or other non-invisibility users of that broad render flag. The thinker now also owns the authored persistent `Areaeffectart` (falling back to `EffectArt`) plus one-shot/looped ability sound presentation and destroys that presentation on expiry.

Feral Spirit reads its summoned unit from UnitID, count from Data B, lifetime from the normal duration field, and spawn distance from Area. Recasting kills surviving summons created by the caster's previous `AOsf` cast, then creates the new summons at the same authored point in front of the caster, matching the Warsmash reference, and requests SpecialArt on each summon. Summons retain their source ability rawcode so replacement does not accidentally kill unrelated summons of the same unit type. The generic attack path consumes stock creep Critical Strike (`ACct`), which makes the authored Dire/Shadow Wolf critical-strike ability functional. Stock Permanent Invisibility (`Apiv`) is registered as a passive and represented by a dedicated per-unit reveal deadline instead of `RF_HIDDEN`: after the authored transition time on spawn the unit is invisible to hostile viewers unless detected, while starting an attack or committing a spell restarts that authored reveal/transition window. Undetected units are excluded from hostile automatic acquisition, ongoing hostile attack validity, unit-target spell validation, networking, and selection; owners/shared-vision allies retain access, and Far Sight/passive detectors expose the unit only to the corresponding viewer. A zero authored transition keeps the unit continuously invisible across attacks/casts, while a negative transition disables entry into permanent invisibility. The reveal deadline is authoritative saved state, so save/load cannot make a revealed Shadow Wolf vanish early or reset its transition.

Earthquake remains a fixed-point channel. Data A supplies the initial effect delay, Data B supplies per-second structure/destructable damage, Data C supplies movement-speed reduction, `Area` supplies the affected radius, and normal duration controls channel lifetime. The retail masks for both `AOeq` and `SNeq` are exactly `ground,structure,debris,tree`; neither row authors `enemy`, `friend`, `neutral`, `air`, `organic`, `mechanical`, `ancient`, `ward`, `vulnerable`, or `invulnerable`. Ground units and structures use those unit categories, while destructables are admitted through `tree` and `debris`. The absence of a relationship token means the data does not author a friend/enemy/neutral restriction, and the generic mask rule (ours and Warsmash's `canBeTargetedBy`) reads that as "any player". That matches the original release, where Earthquake damaged the caster's own buildings; a later Blizzard patch stopped that without changing the mask (reported by a contributor; the patch number is not recorded here). The default build follows the patched rule: with no authored relationship token, `earthquake_allows_unit` admits enemies only. `make WC3_EARTHQUAKE_FRIENDLY_FIRE=1` builds the pre-patch behaviour, the authored mask as written. `wc3_spell.earthquake_retail_mask_is_enemy_only_and_reaches_invisible_units` asserts whichever rule the build selects. The movement path consumes the active `BOeq` reduction for ordinary and group-speed calculations and preserves Warcraft's 140 minimum speed for units authored faster than that threshold.

### Earthquake retail presentation chain

The retail object-data chain is `AOeq.EfctID1=XOeq`; the ability audit exposes that field as `EfctID=XOeq`. `SNeq` also has `EfctID1=XOeq`. No separate `Effects` field appears in the inspected ability row; the retail reference is `EfctID`. The presentation fields from `Units\\OrcAbilityFunc.txt` and object data are:

The data check used the repository's `War3.mpq` and `War3x.mpq`; the relevant ability rows agree between those archives.

| Rawcode | Art | TargetArt | CasterArt | EffectArt | AreaEffectArt | SpecialArt | MissileArt | Buffs | EfctID | Effectsound | Effectsoundlooped |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `AOeq` | button `ReplaceableTextures\\CommandButtons\\BTNEarthquake.blp` | — | — | — | — | — | — | `BOeq,BOea` | `XOeq` | — | — |
| `BOeq` | — | `Abilities\\Spells\\Orc\\StasisTrap\\StasisTotemTarget.mdl` | — | — | — | — | — | — | — | — | — |
| `BOea` | — | — | — | — | — | — | — | — | — | — | — |
| `XOeq` | — | — | — | `Abilities\\Spells\\Orc\\EarthQuake\\EarthQuakeTarget.mdl` | — | — | — | — | — | — | `EarthquakeLoop` |

`AOeq` also has `ResearchArt=ReplaceableTextures\\CommandButtons\\BTNEarthquake.blp`, which is a research icon rather than world presentation. The archive contains the effect model as `Abilities\\Spells\\Orc\\EarthQuake\\EarthquakeTarget.mdx`; the authored `.mdl` path resolves to that asset through Warcraft's model-path lookup. `SNeq` has the same `EfctID` and is an alternate ability row, not an assumption based on rawcode spelling. No `Effectsound` is authored on these rows. The table's `—` means no populated presentation value was found in the inspected retail rows/config.

The model has `SEQS` Birth (167–1200), Stand (1233–10274), Death (62300–63267), four `TEXS` paths (`Textures\\EQ_Rock2.blp`, `Textures\\Dust5A.blp`, `Textures\\LavaLump.blp`, `Textures\\Red_Glow3.blp`), two `PRE2` emitters, one `EVTS`, and no `PREM`, `RIBB`, `ATCH`, or `LITE`. Its eight event objects are all root nodes with no parent and no global sequence:

| Event name | Family / key | Sequence | Node pivot (model units) | Data lookup |
|---|---|---|---|---|
| `SNDXAEQK` | `SND`, frame 167 | Birth | `(-5.516, 11.992, 91.675)` | `AnimLookups` `AEQK` -> label `Earthquake` -> `AnimSounds` `Earthquake` -> `Abilities\\Spells\\Orc\\EarthQuake\\EarthquakeRock.wav` |
| `UBRATHND` | `UBR`, frame 1033 | Birth | `(7.568, 7.219, 12.750)` | `UberSplatData` `THND` |
| `UBRbTHND` | `UBR`, frame 1300 | Stand | `(173.838, -24.387, 12.750)` | `UberSplatData` `THND` |
| `UBRdTHND` | `UBR`, frame 2801 | Stand | `(-130.571, -182.247, 12.750)` | `UberSplatData` `THND` |
| `UBReTHND` | `UBR`, frame 4559 | Stand | `(-130.571, 180.168, 12.750)` | `UberSplatData` `THND` |
| `UBRfTHND` | `UBR`, frame 5922 | Stand | `(95.024, 91.402, 12.750)` | `UberSplatData` `THND` |
| `UBRgTHND` | `UBR`, frame 7979 | Stand | `(-109.460, -57.197, 12.750)` | `UberSplatData` `THND` |
| `UBRcTHND` | `UBR`, frame 9787 | Stand | `(125.551, -125.525, 12.750)` | `UberSplatData` `THND` |

There are no `SPN`, `SPL`, or `FPT` events, so the stock chain has no nested child model and does not use `Splats\\SplatData.slk`. `UberSplatData.slk` row `THND` resolves to `ReplaceableTextures\\Splats\\ThunderClapUbersplat.blp`: `Scale=280`, `BirthTime=0.2`, `PauseTime=2`, `Decay=2`; Start/Middle/End RGB are all 255 and alpha is 0/255/0. `BlendMode=1`; the renderer currently uses its alpha-blended splat primitive, retains this value, and issues one bounded unsupported-field warning. The exact retail meaning of mode 1 has not been established, so full blend parity is not claimed. `Sound="NULL"` is the table's empty sentinel and does not name a sound. The generic warning now treats `NULL`, `-`, and `_` as empty optional sound values.

The renderer already handles the stock model's `PRE2`, `SND`, and `UBR` shapes generically. It resolves event rows in renderer asset scope, so map archive overrides keep their normal priority. The game-side area presentation follows the selected `AbilityData.EfctID` when direct ability fields do not provide art or sound: direct ability values keep precedence, then the effect object's `AreaEffectArt`/`EffectArt` and one-shot/looped sound fields are considered. This makes the `XOeq` model and loop sound data-driven; the model's `SNDXAEQK` supplies the one-shot rock sound. The model presentation uses the existing effect-entity path; the separate provisional terrain pulses are described below.

For in-game visual checking, active Earthquake ticks also emit provisional `TerrainDeformRandom`-shaped pulses centered at the cast point. They use authored `Area` for radius and guessed values of ±48 height units, 200 ms update interval, and 1000 ms lifetime. These are explicitly unverified presentation guesses, not claims about retail Earthquake or `Oeq4`; see [Terrain Deformation](terrain-deformation.md).

For the generic event parser and renderer contracts, see [MDX Event Objects](mdx-event-objects.md); this Earthquake chain uses its `PRE2`, `SND`, and `UBR` paths and does not exercise `SPN`, `SPL`, or `FPT`.

`BOeq` is a buff class (`CBuffEarthquake`) and authors `TargetArt=Abilities\\Spells\\Orc\\StasisTrap\\StasisTotemTarget.mdl` with `Targetattach=overhead`; `BOea` is a buff class (`CBuffEarthquakeAoe`) listed alongside `BOeq` on `AOeq`, but has no corresponding art fields in the inspected Func data. Those authored buff-target fields are distinct from the `XOeq` area model. Generic status-to-`TargetArt` lifetime binding is not implemented, so the current Earthquake effect chain does not claim that this overhead art is displayed. `XOeq` is an effect class (`CEffectEarthquake` in extracted retail class metadata), not a buff rawcode; its `EffectArt` selects the model whose `UBR` events resolve terrain decals. These facts establish an effect-object-to-model-to-UberSplat data chain, but do not establish whether the executable also treats `XOeq` as gameplay state, how it owns the model's lifetime, or whether it reads `Oeq4`.

### Earthquake data questions still open

`AbilityMetaData.slk` defines `Oeq4` as `AbilityData` Data index 4, displays it as `Final Area`, permits `0..99999`, and lists `AOeq,SNeq` as supported IDs. Both stock rows have `Area=250` and `Oeq4=250`. The equality does not establish that `Oeq4` is a radius or an effect scale; no reliable executable/runtime read site was recovered. Its semantics remain unresolved, and the provisional deformation pulse does not read it.

| Retail row | Area | Oeq1 / Oeq2 / Oeq3 / Oeq4 | Duration | EffectArt / AreaEffectArt | BuffID | EfctID |
|---|---:|---|---:|---|---|---|
| `AOeq` | 250 | `0.5 / 50 / 0.75 / 250` | 25 | no direct value / no direct value | `BOeq,BOea` | `XOeq` |
| `SNeq` | 250 | `0.5 / 50 / 0.75 / 250` | 25 | no direct value / no direct value | `BOeq` | `XOeq` |

These values are identical for the timing, damage, slow, area, and effect reference; the buff list differs. The model and loop sound are inherited through `EfctID=XOeq`, as described above.

The retail data establishes that `AOeq` lists both `BOeq` and `BOea`, while `SNeq` lists only `BOeq`. The class metadata identifies them as buff classes, but does not establish `BOea`'s gameplay contribution. Overlapping casts and modified `Oeq3` values were not tested in retail. OpenRealm's shared status storage replaces/refreshes an existing status with the same buff rawcode unless a `BuffStackType` override says otherwise; this is a code fact, not evidence of retail stacking behavior. No Earthquake-specific stacking rule was added.

Chain Lightning's `CLPB`/`CLSB` records feed a bounded procedural lightning polyline. `AvgSegLen` controls point density, `Width` controls the ribbon thickness, `NoiseScale` scales deterministic stepped crackle relative to the stock 0.05 value, `TexCoordScale` tiles and advances `Lightning.blp`, and `Duration` bounds the authored lifetime. The shared registry also owns JASS lightning handles, so spell and script bolts use the same renderer and save/hashtable identity.

Persistent ability state stays in its owning procedure: `CAbilityPermanentInvisibility` receives spawn, add, remove, and level-change messages, while projectile physics publishes an impact message that lets `CAbilityDefend` own missile reflection. The renderer caches every missing `LightningData.slk` row for the current map, so alternating unresolved effect IDs do not produce per-frame warnings.

## Known Gaps

The following are deliberately outside this implementation slice:

- arbitrary MDX attachment-token resolution and team-coloured spell-effect attachment;
- generic binding of buff lifetime to persistent world-art ownership and non-stacking FX
  beyond explicitly owned lifecycles such as natural creep sleep;
- ability/buff `EffectSound` and `EffectSoundLooped`;
- Ghost/Ghost Visible remain separate from the player-aware invisibility paths covered here; Shadow Meld now uses dedicated saved state and the same owner/shared-vision/detector visibility query as the implemented `Apiv`/`Binv`/`BOwk`/ward mechanics;
- Earthquake `Oeq4`/`Final Area` semantics, the retail meaning of `UberSplatData.BlendMode=1`, `BOea`'s contribution, and overlapping-Earthquake stacking remain unverified;
- generic buff `TargetArt`/`Targetattach` creation and lifetime binding remain unimplemented, including the authored `BOeq` overhead model;
- item `cooldownID` / `ignoreCD` shared cooldown behavior;
- automatic `powerup` acquisition/use outside the implemented Speed (`AIsp`/`AIsa`/`APsa`) and Gold (`AIgo`) families;
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
- `wc3_spell.earthquake_respects_authored_relationship_and_destructable_tokens`
- `wc3_spell.hero_passives_use_authored_data_and_runtime_consumers` (includes stock `ACct`)
- `wc3_save.round_trip_entity_c_callbacks` (includes live Far Sight/delayed Chain Lightning thinker state, visited-target marker identity, and the Permanent Invisibility reveal deadline)
- `wc3_api.game_datagram_carries_and_expires_lightning_snapshot`
- `wc3_save.lightning_registry_round_trip`

The focused WC3 engine tests cover the data-driven Chain Lightning cadence and target lifecycle, attached lightning datagrams, JASS lightning identity, Far Sight persistence and viewer-local detection, Earthquake cleanup, Feral Spirit critical strikes, Permanent Invisibility transitions, and save/load restoration. Run `make test-wc3-engine` for both ROC and TFT schema modes, then `make test` for the repository-wide regression suite. Visual validation should additionally cover the authored primary/secondary lightning textures, moving-unit endpoints, persistent area art/looped audio, and save/load while a live presentation effect is active.
