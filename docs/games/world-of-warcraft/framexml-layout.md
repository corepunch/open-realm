# WoW FrameXML Layout

`games/world-of-warcraft/menu/stb_wowxml.h` parses FrameXML using DDX-style schema tables and owns the element registry, inheritance,
parent/relative-frame links, anchors, and rectangle calculation. `ui_xml.c` supplies Lua bindings, drawing, and input. Production
and UI tests compile the single-header implementation with `STB_WOW_XML_IMPLEMENTATION` in `ui_xml.c`.

## DDX Schema Tables (stb_wowxml.h)

`stb_wowxml.h` parses FrameXML grammar through declarative descriptor tables:

- `uiwow_node_types[]`: Maps XML element tags (`Frame`, `ScrollFrame`, `Slider`, `Model`, `Texture`, `FontString`, `Button`, `CheckButton`, `EditBox`, `ThumbTexture`, etc.) to `uiWowXmlType_t` and intrinsic flags (`EF_IS_SCROLLFRAME`, `EF_CHECKBUTTON`, `EF_FOCUSABLE`).
- `uiwow_shared_attrs[]`: Maps XML attributes (`file`, `text`, `hidden`, `virtual`, `setAllPoints`, `password`, `id`, `wordWrap`, `checked`) to element fields or flags through `uiWowAttrType_t`.
- `uiwow_script_tags[]`: Dispatches `<Scripts>` child elements (`OnClick`, `OnLoad`, `OnShow`, `OnEnter`, `OnLeave`, `OnEnterPressed`, `OnEscapePressed`, `OnTabPressed`, `OnMouseWheel`, `OnUpdateModel`, `OnUpdate`) directly to `uiWowXmlStr_t` fields.
- `uiwow_button_part_tags[]`: Table-drives button texture layers (`NormalTexture`, `PushedTexture`, `HighlightTexture`, `CheckedTexture`) and their associated texcoord flags.
- `uiwow_button_text_tags[]`: Table-drives button font strings (`NormalText`, `HighlightText`, `DisabledText`) to `uiWowXmlButtonTextState_t`.
- `uiwow_point_factors[]`: Maps anchor points (`TOPLEFT`, `TOP`, `CENTER`, `BOTTOMRIGHT`, etc.) to X and Y bounding-box interpolation factors, eliminating branching ladders in anchor math.
- `uiwow_layer_levels[]`, `uiwow_justify_h[]`, `uiwow_justify_v[]`: Table-driven string-to-enum lookups.

## Geometry contract

- `<Size><AbsDimension .../></Size>` supplies explicit axes after conversion from the native 1024x768 grid.
- Two authored anchors can derive width and/or height from their pinned edges.
- `setAllPoints` inherits the complete parent rectangle.
- An unconstrained FontString axis uses the renderer's natural text measurement. Text changes invalidate that measurement.
- Any other unresolved axis remains zero. Never invent a default rectangle: it hides missing inheritance, relative-frame, or anchor
  support and makes invalid XML appear usable.
- Drawing logs `UIWow: unresolved FrameXML geometry` once per affected element with its frame and source-file names. The zero extent
  also prevents an unresolved Button/EditBox from receiving a fabricated hit target.

Focused verification:

```sh
make test-wow-hud-xml
make run-wow ARGS='+com_frame_limit 100'
```

The standalone test covers authored size, zero unresolved size, one-shot diagnostics, and renderer-measured FontString dimensions.

## Native-only production UI

Production does not ship project-owned files under `share/Interface/FrameXML/`. Load Blizzard FrameXML from the client archives when
it exists. If classic WoW creates presentation outside FrameXML, reproduce that runtime path in C instead of authoring a parallel XML
layout. Test MPQs may carry reduced fixtures under the original Blizzard paths, but production must resolve those paths from WoW data.

Classic `interface.MPQ` has no loading-screen FrameXML, so `menu_loading.c` draws the server-selected map texture and title at runtime.
The custom message inbox and its combined tutorial/message alert strip also remain runtime presentation because no Blizzard FrameXML
owns that project-specific data model. Their tutorial alert art and dimensions follow the native `TutorialFrame` contract.

## Native tutorial layout and height

`menu_windows.c` loads `Fonts.xml`, `BasicControls.xml`, `UIPanelTemplates.xml`, and `TutorialFrame.xml` from `interface.MPQ`. These own
the 230px frame, backdrop, zero-height natural FontStrings, `UICheckButtonTemplate`, button textures, and input rectangles. The parser
supports native `CheckedTexture` state; C only binds localized strings and client state.

`TutorialFrame_Update` in Blizzard's `TutorialFrame.lua` runs `TutorialFrame:SetHeight(TutorialFrameText:GetHeight() + 62)`. The C
binding applies the same formula before drawing the backdrop: it measures the 210px-wide wrapped body, caches the FontString's natural
height, then changes only the native frame's runtime height. Never replace the zero-height fields with guessed fixed XML heights.

The welcome tutorial (`id == 42`) has another native Lua lifecycle rule: `TutorialFrame_CheckIntro` moves the frame's `BOTTOM` anchor
to `UIParent.CENTER` with a `-90` Y offset. That position assumes Blizzard's original body height; retaining the bottom anchor after a
long localized body expands shifts the popup upward. The runtime therefore uses `CENTER`/`CENTER` for tutorial 42 so the resized bounds
remain vertically centered. Ordinary help retains the XML-authored `BOTTOM`/`BOTTOM` anchor at `+100`. Both go through FrameXML's
runtime `SetPoint` representation rather than changing or replacing native XML.

Inspect the original client source with:

```sh
build/bin/mpqtool -mpq data/world-of-warcraft/interface.MPQ cat 'Interface/FrameXML/TutorialFrame.xml'
```

The native Lua source can be inspected alongside the XML:

```sh
build/bin/mpqtool -mpq data/world-of-warcraft/interface.MPQ cat 'Interface/FrameXML/TutorialFrame.lua'
```
