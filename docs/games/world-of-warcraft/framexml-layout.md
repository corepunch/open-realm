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
