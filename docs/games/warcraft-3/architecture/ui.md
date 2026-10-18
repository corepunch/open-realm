# Warcraft III UI Architecture

Main-menu/glue presentation and game-authored HUD are separate modules with exclusive execution.
See the [engine UI contract](../../../architecture/ui-system.md) for the lifecycle and historical root cause.

## Menu and Glue

`M_GetAPI` provides lifecycle, drawing, keyboard/text/mouse input, and lobby updates. It receives no player or
selected-unit state. `M_Init` parses menu FDF assets and loads the menu theme; `uiScreen_t` controllers manage
main-menu, campaign, mission, and lobby navigation. `cl_start_menu` selects the initial screen.

`CL_MenuActive()` gates every menu entry from the engine. Loading invokes menu Shutdown to release its caches;
return-to-menu invokes Init. The loaded library remains resident so command callback pointers stay valid.

## Game-Authored HUD

Client input sends the selection/order command and updates its local selection hint. The game's authoritative
selection path validates the entity set and authors command, inventory, queue, and information frames from
`games/warcraft-3/game/hud/`. Those frames travel through `svc_layout` to the generic client renderer and input
handlers. No menu callback, separate HUD query, or menu-owned unit cache participates.

Recipient-specific skin keys are resolved in the game before registering concrete texture paths. HUD templates
and media identities are rebuilt across map/save-load transitions; see [HUD Media Lifetime](../hud-media.md).

Health/resources and other changing values use existing replicated state bindings or refreshed frames. In-game
Quest, Log, Menu, Allies, and other dialogs are game-authored windows using `svc_window`. The generic client owns
focus, z-order, dragging, local text input, and modal exclusion. These dialogs never call the main-menu library.

## Frame Definition Files

Both modules use FDF schema/binding support, with separate registries and resource ownership. `FRAMEDEF` represents
an authoring frame tree: templates specify anchors, dimensions, textures, text, backdrop/control data, and children.
Menu frames are drawn inside the menu library. Game HUD frames are serialized into the shared `uiFrame_t` protocol.
The client then resolves layout geometry and draws/interacts with the transmitted frames.

## Adding a New UI Element

1. Determine whether it belongs to glue navigation or gameplay.
2. For glue, add the FDF binding and owning screen controller under `menu/`.
3. For gameplay, add game-owned FDF/layout authoring under `game/hud/`; use `svc_layout` or `svc_window`.
4. Bind dynamic values through existing replicated state or game-authored updates. Resolve skin/media paths in the game.
5. Test serialization and real draw/input/lifecycle entry points with repository fixtures in both ROC and TFT.

Do not extend the menu ABI to carry HUD state. Extend the generic gameplay presentation contract if necessary.

## Verification and References

`make test` includes menu and gameplay suites plus the mechanical menu-boundary audit. Assets belong in
`games/warcraft-3/tests/resources-src/` and are packed into the generated test MPQ; tests must not depend on local
retail installations. See [CONTRIBUTING](https://github.com/corepunch/open-realm/blob/main/CONTRIBUTING.md).

- [UI quick reference](ui-quick-reference.md)
- [Campaign/menu flow](ui-flow.md)
- [FDF format](../file-formats/fdf.md)
- [HUD media lifetime](../hud-media.md)
- [Client windows](../../../architecture/client-windows.md)
