# WoW FrameXML Layout

`games/world-of-warcraft/ui/stb_wowxml.h` parses FrameXML and owns the element registry, inheritance, parent/relative-frame links,
anchors, and rectangle calculation. `ui_xml.c` supplies Lua bindings, drawing, and input. Production and UI tests compile the
single-header implementation with `STB_WOW_XML_IMPLEMENTATION`; keep the guarded declarations in `ui_xml.c` synchronized.

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

## Project-owned loading layout

Classic `interface.MPQ` has no loading-screen FrameXML. `share/Interface/FrameXML/OpenWarcraftLoadingScreen.xml` supplies the
project-owned root and title FontString. XML owns its 1024x768-native size, anchor, font, color, and justification. `ui_loading.c`
continues to draw the server-selected map texture, sets only `OpenWarcraftLoadingTitle` text, draws that named XML tree, and hides it
again so it cannot leak into glue or in-game screens.

`UIWow_XMLDrawFrame(name)` draws one named tree without rendering every visible GlueXML root. `UIWow_XMLSetFrameText(name, text)` is
the C-side data-binding equivalent of FrameXML's Lua `SetText`; neither API changes geometry.

## Project-owned tutorial layout

`share/Interface/FrameXML/OpenWarcraftTutorialFrame.xml` preserves the classic `TutorialFrame.xml` composition without rebuilding it
in `ui_windows.c`: the 230x128 frame, bottom anchor, backdrop, title/body columns, checkbox, and Okay button are authored in XML. C
loads localized `GlobalStrings.lua` values, binds those strings plus checked/pressed state, and uses the named XML button rectangles for
input. `UIWow_XMLSetButtonPressed(name, pressed)` selects the XML `NormalTexture`/`PushedTexture`; it does not assign geometry.

Inspect the original client source with:

```sh
build/bin/mpqtool -mpq data/world-of-warcraft/interface.MPQ cat 'Interface/FrameXML/TutorialFrame.xml'
```

## Project-owned inbox panel

`share/Interface/FrameXML/OpenWarcraftInbox.xml` owns the open-message backdrop, 512x169 placement, and title/body typography. The
client inbox model remains populated by the server's `wow_inbox` payload; selecting an unread alert binds only its title and body,
shows `OpenWarcraftInbox`, and sends `message_read <id>` back to the server.
