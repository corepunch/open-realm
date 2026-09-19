# Warcraft III Blight Rendering Plan

## Goal

Render Warcraft III Blight as terrain texture selection rather than as a square decal. The renderer should use the map's tileset-specific Blight atlas and preserve Warcraft III's irregular terrain transitions.

## Current state

- The map tileset is resolved through `UI\\WorldEditData.txt`.
- The corresponding `TerrainArt\\Blight\\*_Blight.blp` texture is loaded from the Warcraft III virtual filesystem.
- Gameplay Blight is authoritative on the 32-unit pathing grid.
- The server synchronizes that grid to the client.
- The existing renderer derives 128-unit tile masks from the synchronized grid.
- The old presentation path renders Blight as a separate splat/decal, which does not match Warcraft III terrain composition.

## Target rendering model

```text
map tileset
    -> tileset-specific Blight atlas

terrain corner
    -> cliff-associated ground texture when applicable
    -> Blight texture when blighted
    -> authored normal ground texture otherwise

four effective corner textures
    -> normal Warcraft III atlas transition selection
    -> terrain layer rendering
```

The Blight atlas contains opaque fill variations and partial-corner alpha masks. Solid Blight tiles use the opaque variation path; mixed tiles use the four-corner mask path.

## Implementation steps

1. Keep the existing authoritative tileset-to-Blight texture lookup.
2. Add a renderer-side 128-unit visual Blight layer driven by the client mask.
3. Convert synchronized 32-unit cells into a four-corner mask for each terrain tile.
4. Use `SetTileUV()` and the Blight atlas for solid and partial masks.
5. Build the layer with terrain-conforming vertices and the normal terrain shader.
6. Draw the Blight layer after normal ground layers and before cliff layers.
7. Rebuild the visual layer only when the terrain-mask generation changes.
8. Remove the standalone Blight splat/decal draw from the world pass.
9. Ensure authored W3E Blight remains visible; it must not fall back to normal ground merely because it is not a runtime mutation.
10. Update the Blight architecture documentation to describe the final ownership and data flow.

## Validation

- Build the Warcraft III renderer with and without `WC3_DEBUG_BLIGHT`.
- Verify solid Blight uses the opaque atlas variation.
- Verify one-, two-, and three-corner transitions use the correct atlas masks.
- Verify non-Blighted terrain remains the normal ground texture.
- Verify authored map Blight and runtime-added Blight both render.
- Verify runtime removal exposes the normal ground texture again.
- Verify cliff and ramp tiles retain their existing rendering behavior.
- Run the focused WC3 Blight tests and the full test suite.

## Constraints

- Gameplay Blight remains a 32-unit authoritative state.
- Visual terrain composition remains renderer-owned.
- The renderer must not invent a second gameplay/pathing representation.
- Missing textures or invalid atlas data must be reported rather than silently replaced.
