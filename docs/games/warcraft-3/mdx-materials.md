# MDX material composition

`games/warcraft-3/renderer/mdx/r_mdx_geoset.c` owns MDX material submission.
Opaque/alpha-key layers draw in the solid pass; blended layers draw after world water
in the alpha pass. Translucent instance tint retains the existing whole-instance
alpha-pass behavior.

Within an instance, translucent **layers**, rather than whole geosets, sort first
by material priority and then ascending MDX filter mode. Equal priority/filter keys
keep authored geoset/layer order. Mixed materials must contribute separate sort
entries: choosing one representative filter for a multilayer material is insufficient.
The comparator uses relational comparisons rather than subtracting signed priorities.

## HumanX03Secret portal (#643)

The TFT archive contains `Doodads/Cinematic/ShimmeringPortal/ShimmeringPortal.mdx`.
HumanX03Secret's portal is the custom `h000` unit at `(1.0, -1971.8)`.
The stock model has eight geosets, nine materials, twelve textures and no TXAN chunk.
These relevant geosets all have material priority zero:

| Geoset | Material | Filter | Texture |
|---|---|---|---|
| 3 | 3 | 2 / Blend | `Textures/ShockwaveWater1Black.blp` |
| 4 | 4 | 4 / AddAlpha | `Textures/GenericGlow5.blp` |
| 6 | 7 | 3 / Additive | `Textures/firering4.blp` |
| 7 | 8 | 2 / Blend | `Doodads/Cinematic/ShimmeringPortal/white32.blp` |

Geoset 7 has static alpha 1 and static RGB `(0.482353, 0.317647, 0.560784)`.
Its texture is an opaque white JPEG BLP1. This flat base is intentional artwork;
drawing it last covers the earlier textured additive planes and cylinder, producing
the reported purple disk. Missing texture animation is not the explanation.
The old comparator sorted priority then file order only. Ordering filter 2 before
filters 3 and 4 restores the authored overlay composition without modifying assets,
colors, alpha or blend factors. Local Warsmash `SetupGroups.java` independently uses
priority then filter ordering (with an additional authored-height tie-break).

## Verification

`make test-mdx-geoset` runs the production material submission path with only GPU
calls captured. `mdx_geoset.portal_base_blends_before_additive_texture_at_same_priority`
failed before the fix and checks the portal filter sequence, priority precedence,
stable ties, mixed materials and solid/alpha ownership. The fixture is synthetic and
does not require installed archives. Run `make test` for the complete suite.

Asset inspection and bounded framebuffer verification require local retail data:

```sh
build/bin/mdxtool -mpq 'data/Warcraft III/Frozen Throne/War3x.mpq' \
  -model Doodads/Cinematic/ShimmeringPortal/ShimmeringPortal.mdx --info
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +vid_hidden 1 \
  +set sv_cheats 1 +map Maps/FrozenThrone/Campaign/HumanX03Secret.w3x \
  +screenshot 100 +com_frame_limit 120
```

The first map frames show the authored introduction. After initialization, use
`cinematic stop` (the map's skip event), then `camera edge 0` and
`camera move 0 -1972`, and schedule a
screenshot with a bounded frame limit. Camera diagnostics accept integer coordinates;
disable edge scrolling so an unseen pointer cannot move the hidden camera. Headless draw-order assertions establish the
submission contract; framebuffer inspection establishes visible texture composition.

Network and save formats are unchanged. See [main-menu rendering](main-menu-rendering.md)
for camera-scene alpha passes and [MDX events](mdx-event-objects.md) for event children.

A bounded local TFT capture (`screenshots/shot0004.jpg` during #643 investigation)
confirmed the restored ripples, cylinder strands and rim after the comparator fix.
Animation phases differ from the report's retail image. Baseline/final `make test`,
debug/release `openwarcraft3`, and renderer suites with/without `USE_SHADOWMAPS` passed.
