# Gul'dan cinematic fixes: merge review

## Decision summary

This branch fixes the NightElfX03 intro transition to Gul'dan's scene: the
filter is drawn instead of becoming a black overlay, its texture opacity more
closely matches retail, it covers only the 3D viewport below the HUD, and the
cinematic portrait resolves the correct model. The HUD also retains the
player's selected race skin when cinematic layers are serialized.

The review found two compatibility changes that should be considered before
merging:

1. **Client/server wire protocol:** `cinefilter_image` and `cinefilter_color`
   are added to `playerStateFields` as `NFT_LONG`. Protocol version 21 rejects
   older peers during connection setup. The new fields are appended after the
   existing player-state fields, preserving existing delta bit assignments;
   changed filters still add values to the snapshot payload. The portrait
   model reuses stats slot 29, so it does not add a delta field, but its
   rendering meaning changes. Client and server must both use protocol 21.
2. **WC3 save format:** the portrait stats-slot contract changes, and the save
   version is now 77. Version 76 saves are rejected. The repository policy in
   `AGENTS.md` and `CONTRIBUTING.md` requires version bumps and rejection when
   serialized meaning changes, and explicitly disallows migrations. This
   intentionally discards compatibility with old saves.

The portrait network value itself occupies the pre-existing one-byte player
stats slot 29, so it does not add a delta field. It now uses the entire byte as
the model index, while `cinematic_portrait` indicates presence. This allows
indices 64–255 and preserves a valid index 0. Maps with an index above 63 were
previously affected by truncation; that is the intended behavior correction.

## Commit sequence

The commits are split by behavior and review pass. They should remain separate
for bisectability:

- `418cf778` — avoid black overlays for color-only cinematic filters.
- `8f172847` — transmit/render cinematic filter textures and color.
- `2658c1fe` — reduce Gul'dan filter opacity to match retail.
- `b933f571` — fit the filter to the world viewport below HUD layers.
- `415a1660` — preserve full cinematic portrait model indices and update the
  serialized portrait meaning.
- `93b4b9dd` — serialize cinematic HUD using the active player's race skin.
- `3959fc37` — reject legacy version 76 saves under save format 77.
- `e9595dd6` — cover portrait indices above 63 through the player delta and
  client drawing paths.
- `083843ba` — restore the prior UI client after cinematic-layer serialization;
  keep skin context scoped.
- `7a60c75c` — cover filter delta round-trip and viewport rendering.
- `2308d2c7` — exercise the cinematic HUD race-skin context through actual
  layout serialization.
- Pending protocol fix — bump to protocol 21 and append filter fields after
  existing player-state fields.

The first four commits form the filter path; the portrait/save commits form the
portrait contract correction; the HUD race-skin commit is an independent fix
needed by the same cinematic. If merging selectively, preserve this ordering
within each path and take the regression tests with their fixes.

## Behavior and implementation details

- Filter image and packed RGBA state are sent in two `NFT_LONG` player-state
  fields. The server interpolates transitions and halves authored alpha to
  match the observed retail opacity. Image-only filters with zero alpha are
  normalized to white to avoid a black overlay.
- The client draws the filter within the camera's normalized 3D viewport and
  before HUD layers. It is not a full-screen overlay.
- Portrait model identity uses the full byte in existing stats slot 29. The
  presence flag is separate, including for model index 0.
- `UI_WriteCinematicLayer` serializes the HUD with the selected client's
  player/race context so the Human default skin is not used during the
  Night Elf cinematic.
- No persistent filter state was added. Save version 77 is needed for the
  serialized portrait meaning change; v76 is rejected.

See [cinematics.md](docs/games/warcraft-3/cinematics.md) and
[save-load.md](docs/games/warcraft-3/save-load.md) for durable subsystem and
save-policy details.

## Validation

- Baseline before fixes: `make test` passed (recorded during the review).
- Save regression: targeted save version test passed after version 77 change.
- Portrait regression: `make test-core` and targeted portrait tests passed.
- `make test-core` passed with **2503 assertions in 240 tests**.
- `make test-wc3-engine` passed on classic and TFT; each variant completed all
  four shards. The latest run after strengthening the race-skin test reported
  16,468 / 5,910 / 6,237 / 20,815 assertions per variant.
- `make test` passed, including the core, server, client/UI, renderer, WC3, WoW,
  and SC2 test targets. This run preceded the protocol 21 update; rerun it
  after that commit.
- `python3 tools/engine_boundary_audit.py` reported clean against `main`.
- `git diff --check` passed before the protocol update; rerun after it.

The test suite verifies the serialized values and geometry contract. It does
not establish pixel-for-pixel retail visual parity; the alpha adjustment is
based on the observed Gul'dan scene appearance.

## Merge recommendation

Deploy protocol 21 client and server builds together; older peers are rejected
during connection setup. Confirm the save-version 77 rejection is acceptable
for the target branch and release policy. The commits are small and covered by
focused regressions. Retail framebuffer parity for the filter opacity remains
a visual observation rather than an automated pixel comparison.
