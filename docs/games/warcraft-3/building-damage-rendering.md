# Warcraft III Building Damage Rendering

## Contract

Building combat damage is presentation-only. The server remains authoritative for unit health and selects the visible damage phase for entities carrying `EF_BUILDING`; the client only renders the resulting effect data.

The game module keeps the building's normal MDX model and selects the authored building-fire model and attachment slots. The renderer overlays that model at the building model's `Sprite ... Ref` attachment pivots. The fire does not create a game entity, change HP, affect pathing, or participate in combat.

Construction is deliberately separate. Human construction begins at low HP, so `R_RenderModel()` suppresses combat-damage fire while the building model is in a `Birth...` sequence. Once construction leaves Birth, ordinary health thresholds apply.

## Data Flow

```text
UnitData / UnitBalance
    -> G_UnitIsBuilding(class_id)
       -> EF_BUILDING

edict health
    -> compress_stat()
    -> entityState_t.stats[ENT_HEALTH] (0..255)
    -> G_UpdateOnFire()
    -> entityState_t.effect/effect_flags
    -> client V_AddClientEntity()
    -> renderEntity_t.effect_model/effect_flags

building MDX pose
    -> MDLX_CollectAttachmentPositions(..., "Sprite ", ...)
    -> authored Sprite First..Fifth world positions
    -> R_RenderModel()
    -> server-selected building-fire MDX overlay
```

No additional health network field is needed. `entityState_t.stats[ENT_HEALTH]` already carries the health ratio every snapshot; `renderEntity_t.health` is only a renderer-facing copy of that existing byte.

## Damage Staging

The renderer uses the compressed equivalents of the observed WC3 damage thresholds:

| Remaining health | Fire slots | Effect tier |
| --- | --- | --- |
| `> 75%` | none | none |
| `<= 75%` | First, Second | small |
| `<= 50%` | First, Second, Fourth, Fifth | medium |
| `<= 25%` | First, Second, Third, Fourth, Fifth | severe |

With 0..255 compressed health this is tested as `<=191`, `<=127`, and `<=63`. Health zero does not draw the damage overlay; normal death rendering owns that state.

The slot staging is based on retail observation rather than recovered Blizzard engine source. A 2025 Hive Workshop observation records First/Second at 75%, Fourth/Fifth joining at 50%, and Third joining at 25%:

- <https://www.hiveworkshop.com/threads/sprite-ref-percentage.364442/>

`Sprite Sixth` and `Sprite Large` are intentionally not implemented because their retail activation rules were not established with enough confidence during this work.

## Fire Families

Human and Orc buildings use the standard fire family. Undead and Night Elf structures use their authored race-specific families. `G_UpdateOnFire()` resolves the unit's authored `UnitData.race` and registers the resulting model in the game module; the renderer receives only the selected model index and slot mask.

| Tier | Human / Orc / default | Undead | Night Elf |
| --- | --- | --- | --- |
| Small | `Environment\\SmallBuildingFire\\SmallBuildingFire2.mdx` | `Environment\\UndeadBuildingFire\\UndeadSmallBuildingFire2.mdx` | `Environment\\NightElfBuildingFire\\ElfSmallBuildingFire2.mdx` |
| Medium | `Environment\\LargeBuildingFire\\LargeBuildingFire2.mdx` | `Environment\\UndeadBuildingFire\\UndeadLargeBuildingFire2.mdx` | `Environment\\NightElfBuildingFire\\ElfLargeBuildingFire2.mdx` |
| Severe | `Environment\\LargeBuildingFire\\LargeBuildingFire1.mdx` | `Environment\\UndeadBuildingFire\\UndeadLargeBuildingFire1.mdx` | `Environment\\NightElfBuildingFire\\ElfLargeBuildingFire1.mdx` |

These Warcraft asset names are corroborated by the shipped MPQ and by long-lived community asset lists; they are external compatibility evidence, not OpenRealm-owned assets:

- <https://www.hiveworkshop.com/threads/attachment-points-for-buildings.94223/>
- <https://www.hiveworkshop.com/threads/building-burn.359430/>

The renderer registers these required models through the normal model registry. Missing required models therefore use the registry's existing error reporting rather than introducing a silent custom fallback.

## MDX Attachment Contract

`MDLX_CollectAttachmentPositions()` lives in the MDX animation layer because attachment pivots must use the same interpolated node hierarchy as the visible model. It:

1. evaluates the current/old model frames;
2. resolves attachment-node visibility;
3. transforms the attachment pivot through the node hierarchy;
4. transforms the resulting local point through the building model matrix;
5. returns matching authored names and world positions.

The building renderer filters the returned `Sprite ` attachments by semantic names (`Sprite First`, `Sprite Second`, and so on). It must not assume linked-list order and must not invent random offsets when an authored slot is absent.

## Network Contract

`EF_BUILDING` remains in the existing `USHORT entityState_t.flags` field. The selected effect model is a separate `BYTE entityState_t.effect`, serialized as `NFT_BYTE`; `USHORT entityState_t.effect_flags` is serialized as `NFT_SHORT` and carries the effect kind plus authored attachment-slot mask. `net.entity_delta_preserves_effect_model` guards that contract.

`model2` remains available for ordinary attached/duplicate models. This separation is important because an effect model must not hijack an unrelated `model2` consumer.

## Known Gaps

- `Sprite Sixth` / `Sprite Large` behavior is not implemented because its retail health-stage semantics remain unresolved.
- The implementation follows the documented 75/50/25 presentation stages but does not claim recovered Blizzard source-level rules for threshold rounding beyond the existing 8-bit health compression.
- Fire effects are rendered through the ordinary MDX model/particle path. PRE2 `FilterMode` is preserved per emitter so blended smoke is not accidentally rendered as additive white smoke; remaining particle-instance limitations are renderer-wide and are not special-cased here.
- Race-family selection is based on the structure's authored `UnitData.race`; Human, Orc, and unclassified structures use the standard family.

## Verification

After building, focused automated checks are:

```sh
make test-renderer-model
make test-wc3-engine WC3_PATTERN='wc3_unit.spawned_*building*'
make test
```

Runtime checks should cover:

1. Damage a completed Human/Orc building just below 75%; fire appears at authored First/Second points.
2. Cross 50%; Fourth/Fifth join and the medium fire model is used.
3. Cross 25%; Third joins and the severe fire model is used.
4. Repair back across 25%, 50%, and 75%; the overlay immediately steps down/removes without changing simulation health.
5. Start Human construction; low construction HP does not produce combat-damage fire while the model is in Birth.
6. Damage completed Undead and Night Elf buildings; their race-specific fire families are used.
7. Destroy a building; zero-health damage overlays disappear and the existing death animation/effects own the visual state.
8. Test models with missing individual Sprite slots; only authored slots render, with no fabricated offsets.

## See Also

- [Building Construction](building-construction.md)
- [Economy And Unit Presentation](economy-and-unit-presentation.md)
- [Warcraft III MDX Model Format](file-formats/mdx.md)
