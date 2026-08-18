# UI Frame Definition File (FDF)

FDF (Frame Definition File) is a plain-text declarative format used by Warcraft III to describe the UI frame hierarchy — positions, sizes, textures, fonts, and child frames. OpenWarcraft3 parses FDF files client-side in the Warcraft III UI runtime library (`games/warcraft-3/ui/`).

## File Format

FDF files are hierarchical blocks enclosed in braces. The general form is:

```
FrameType "FrameName" {
    InheritsParts "BaseName"
    Width  0.025
    Height 0.025
    SetPoint TOPLEFT, TOPLEFT, 0.005, -0.005
    BackdropTileBackground false
    BackdropBackground "path\\to\\background.blp"
    ...
    ChildFrame {
        ...
    }
}
```

### Frame Types

The following frame types are recognised by the Warcraft III UI parser (`games/warcraft-3/ui/ui_fdf.c`):

| Type | Purpose |
|------|---------|
| `BACKDROP` | Textured rectangle background |
| `BUTTON` | Clickable button with up/down/hover states |
| `CHATDISPLAY` | Scrolling chat log area |
| `CHECKBOX` | Toggleable checkbox |
| `DIALOG` | Modal dialog container |
| `EDITBOX` | Single-line text input |
| `FRAME` | Generic container |
| `GLUEBUTTON` | Menu/glue screen button |
| `GLUECHECKBOX` | Menu/glue screen checkbox |
| `GLUEEDITBOX` | Menu/glue screen text input |
| `GLUETEXTBUTTON` | Menu/glue screen text button |
| `HIGHLIGHT` | Highlight overlay |
| `LISTBOX` | Scrollable list |
| `MODEL` | Inline 3D model viewer |
| `POPUPMENU` | Drop-down menu |
| `SCROLLBAR` | Scroll bar control |
| `SIMPLEBUTTON` | Lightweight button |
| `SIMPLEFRAME` | Lightweight container |
| `SLIDER` | Numeric slider |
| `SPRITE` | Animated texture |
| `TEXT` | Static or dynamic text label |
| `TEXTAREA` | Multi-line text area |
| `TIMEDTEXTFRAME` | Timed-display text |

### Common Properties

| Property | Description |
|----------|-------------|
| `Width <float>` | Frame width as a fraction of the screen width |
| `Height <float>` | Frame height as a fraction of the screen height |
| `SetPoint <anchor>, <target>, <x>, <y>` | Attach a frame anchor to a parent anchor with an offset |
| `SetAllPoints` | Stretch to fill the parent |
| `InheritsParts "Name"` | Copy all properties from the named base template |
| `BackdropBackground "path"` | Texture path for the backdrop |
| `BackdropTileBackground <bool>` | Tile the texture instead of stretching |
| `BackdropEdgeFile "path"` | Texture for the nine-slice border edges |
| `BackdropEdgeSize <float>` | Width of the border region |
| `FontString <font>, <height>, <flags>` | Text font and size |
| `Text "<string>"` | Default text content |
| `Color <r> <g> <b> <a>` | RGBA vertex colour (0–255 each) |

### Anchor Points

`SetPoint` accepts one of nine named anchor points:

`TOPLEFT`, `TOP`, `TOPRIGHT`, `LEFT`, `CENTER`, `RIGHT`, `BOTTOMLEFT`, `BOTTOM`, `BOTTOMRIGHT`

The offset is specified in screen-width fractions:

```
SetPoint TOPLEFT, TOPLEFT, 0.02, -0.02
```

This aligns the frame's top-left corner 2% from the screen left edge and 2% from the top.

## Frame Registry

`UI_ParseFDF` (`games/warcraft-3/ui/ui_fdf.c`) tokenises the FDF text, constructs `frameDef_t` structs, and stores them in the global frame registry keyed by name. Screen controllers query the registry, parent frames into the active tree, and draw through `games/warcraft-3/ui/ui_render.c`.

## Programmatic API

Frames can also be created directly in C without any FDF text using the API described in `ui_fdf.c`:

```c
FRAMEDEF f;
UI_InitFrame(&f, FT_TEXT);
UI_SetPoint(&f, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.05, -0.30);
UI_SetText(&f, "Hello, world!");
```

Helper functions:

| Function | Purpose |
|----------|---------|
| `UI_InitFrame(frame, type)` | Zero-initialise and set the frame type |
| `UI_SetPoint(frame, point, rel, targetPoint, x, y)` | Anchor a point to a relative frame |
| `UI_SetSize(frame, width, height)` | Set explicit dimensions |
| `UI_SetText(frame, fmt, ...)` | Printf-style text assignment |
| `UI_SetTexture(frame, name, decorate)` | Assign a texture by skin-entry name |
| `UI_SetParent(frame, parent)` | Attach to a parent frame |
| `UI_DrawFrame(frame)` | Draw a frame and its descendants |

For draw-call inspection, use the stdout renderer:

```bash
make run-ui-text
```

## Related Source Files

| Source | Purpose |
|--------|---------|
| `games/warcraft-3/ui/ui_fdf.c` | FDF text parser and programmatic frame API |
| `games/warcraft-3/ui/ui_main.c` | Loads FDF/theme assets and executes `ui_start_command` |
| `games/warcraft-3/ui/ui_render.c` | Layout solving and frame rendering |
| `games/warcraft-3/ui/ui_theme.c` | Warcraft theme file loading |
| `games/warcraft-3/ui/screens/*.c` | Screen controllers |
| `renderer/r_stdout.c` | Text renderer for draw-call diagnostics |
