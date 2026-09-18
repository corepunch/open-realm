# Unit Vertex Color

## Contract

`SetUnitVertexColor(unit, red, green, blue, alpha)` is a persistent per-unit presentation override. The Warcraft III game module owns the requested RGBA value and the client caches that presentation state by entity number; the MDX renderer multiplies it with the model's evaluated geoset colour.

The native clamps every component to `0..255`. Alpha `0` is a valid explicit value and must not be confused with an unset tint. The override changes rendering only; it does not change ownership, selection, collision, pathing, targeting, fog visibility, or simulation lifetime.

Current scope is intentionally narrow:

- `SetUnitVertexColor` is implemented end-to-end.
- `SetWaterBaseColor` remains a placeholder; the W3M water renderer has no live server-authored base-colour contract yet.
- JASS `texttag` / `SetTextTagColor` store registry state; client draw is still deferred ([multiboard-and-texttag.md](multiboard-and-texttag.md)).
- Unit invisibility continues to use its existing gameplay/render-state path; this work does not redefine invisibility as vertex alpha.
- Parsed `UnitUI.slk` `red`/`green`/`blue` values are not yet applied as the default runtime tint. `SetUnitVertexColor` supplies an explicit runtime override when scripts call it.

## Data Flow

```text
JASS SetUnitVertexColor
    -> games/warcraft-3/game/api/api_unit.h
    -> edict->vertex_color + vertex_color_set
    -> WC3 per-frame game datagram (entity number + COLOR32)
    -> client centity_t tint cache
    -> client/cl_view.c V_AddClientEntity
    -> renderEntity_t.tint + tint_valid
    -> games/warcraft-3/renderer/mdx/r_mdx_geoset.c
    -> evaluated geoset RGBA multiplied by instance RGBA
```

The transport deliberately does **not** widen `entityState_t`. `AGENTS.md` treats that structure as a bandwidth-sensitive network contract; the game datagram is the existing extension path for WC3 presentation state that must converge for reconnects and dropped packets.

The datagram's existing weather count reserves its high bit (`BZ_GAME_DATAGRAM_ENTITY_TINTS`) to indicate a following tint section. That section is a `USHORT` count followed by repeated `USHORT entity number + COLOR32` records. If the complete visible tint set would not fit `MAX_GAME_DATAGRAM_SIZE`, the extension is omitted for that frame rather than producing a truncated authoritative set.

## Alpha-Zero Sentinel

Before script-controlled tinting, `renderEntity_t.tint.a == 0` meant "no tint supplied" and the MDX renderer substituted opaque white. That sentinel cannot represent an explicit `SetUnitVertexColor(..., 0)` alpha.

The client cache and `renderEntity_t.tint_valid` disambiguate the two cases:

- no cached override: use the renderer's default white tint;
- cached override: copy all four supplied bytes exactly, including alpha `0`, and mark the render tint valid.

When a full tint section is received, active client entities are first returned to the unset state and then the current visible overrides are applied. Entity removal also clears the cached tint. The renderer still accepts older transient callers that set a non-zero `renderEntity_t.tint.a` without `tint_valid`, so move/attack confirmation and other existing tint producers retain their current behavior.

## Opaque MDX Layers

The Warcraft III MDX renderer already supports fading models whose authored layers are normally `None` or `AlphaKey`. When the instance alpha is below `1.0`, those layers use standard source-alpha blending and disable depth writes for the translucent pass. `SetUnitVertexColor` reuses that existing path; it does not introduce a second fade renderer.

## Persistence

`vertex_color` and `vertex_color_set` live in the Warcraft III `edict_t`, so save/load preserves both the RGBA value and the distinction between unset and explicit alpha zero. Save format version 17 accompanies the changed WC3 edict layout and rejects older incompatible records rather than interpreting shifted bytes.

## Verification

Automated coverage is provided without retail assets:

- `wc3_api.set_unit_vertex_color_publishes_clamped_rgba` checks JASS argument consumption, clamping, alpha zero, and WC3 datagram publication;
- `wc3_save.field_vertex_tint_round_trip` checks persistence.

Manual map check:

```jass
call SetUnitVertexColor(u, 255, 255, 255, 128)
call TriggerSleepAction(2.00)
call SetUnitVertexColor(u, 255, 255, 255, 0)
call TriggerSleepAction(2.00)
call SetUnitVertexColor(u, 255, 255, 255, 255)
```

Expected result: the unit becomes approximately half-transparent, then visually transparent, then opaque again while remaining the same simulation entity throughout.
