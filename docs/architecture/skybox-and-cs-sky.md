# Skybox and `CS_SKY`

## Quake II contract

Quake II publishes a logical environment name in `CS_SKY`, then the client
passes that name to the renderer. The renderer loads six images named
`env/<name><suffix>` and draws them as an untranslated skybox before world
geometry. The name is not a model path and is not a single cubemap texture.

## Warsmash precedent

Warsmash exposes a JASS `SetSkyModel(string)` native. Its viewer loads the
specified MDX, clears the model's lights, marks every material layer unshaded,
creates one model instance, selects sequence 0, and renders the instance at
the camera location before the normal scene. The model is therefore an
infinite-looking animated scene object, not a Quake II cubemap.

The native is explicit and script-driven: Warsmash does not derive the model
path from `lightEnvironmentTileset`. The historical implementation is in
`War3MapViewer.setSkyModel()` and its render loop in commit `60db46c8` of the
vendored `data/WarsmashModEngine` checkout.

## OpenWarcraft status

`CS_SKY` is reserved in `common/shared.h`, but it is currently unused. A
configstring-only change would not render anything because the generic client
has no sky metadata in `viewDef_t` and the renderer has no skybox pass.

The intended adaptation is to let `CS_SKY` carry a model path, have the client
register it through the normal model pool, and pass the resulting model handle
in `viewDef_t`. The shared renderer can then apply the Warsmash lifecycle to a
model supplied by each game. The WC3 `SetSkyModel` native now registers the
model and publishes its index; map scripts remain responsible for choosing the
authoritative model path.

## Map data audit

- Warcraft III `war3map.w3i` stores `mainGroundType` and, for newer formats,
  `lightEnvironmentTileset`. Warsmash reads the same latter field through
  `War3MapW3i.getLightEnvironmentTileset()`. Neither field is a Quake-style
  six-face environment name, and no authoritative tileset-to-sky asset table
  was found in the checked archives.
- StarCraft II `MapInfo` fixtures store dimensions and a display name. The
  parsed lighting record contains light parameters, but no sky environment
  name or six-face asset contract.
- WoW WDT/ADT map data stores terrain, WMO, doodad, and lighting-related map
  data. The current Classic data path has no per-map skybox name; its outdoor
  sun is synthesized from the day phase.

Do not publish guessed paths or derive `CS_SKY` from a map name. The renderer
should be implemented only after an authoritative game-specific sky asset
mapping is identified, with each game's map loader resolving that value before
the server publishes the configstring.

## Validation commands

Use the Quake II reference when implementing the renderer:

```sh
grep -RInE 'CS_SKY|R_DrawSkyBox|sky_images' data/Quake-2-master
```

Then validate all game variants with the normal bounded runs and test suite:

```sh
make test
make run-wc3 ARGS="+com_frame_limit 100"
make run-sc2 ARGS="+com_frame_limit 100"
make run-wow ARGS="+com_frame_limit 100"
```