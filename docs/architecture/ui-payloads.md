# Server-Authored UI Payloads

## Contract

`uiFrame_t.buffer` is a wire payload, not a place to serialize an entire
renderer or parser runtime struct for convenience. The game module authors the
smallest frame-type-specific schema the client drawer needs. Shared frame data
already carried on the wire by `uiFrame_t`—rectangle, color, texture, text, and
commands—must not be repeated in the payload.

The payload length is one unsigned byte, so the hard wire limit is 255 bytes:

```text
svc_layout, layer
  MSG_WriteDeltaUIFrame(...)
  byte payload_size
  payload[payload_size]
...
long 0, short 0                  termination marker
```

The flow is:

```text
game UI authoring
  -> UI_WriteProxyFrame
  -> gi.Write(PF_UIFRAME)
  -> server/sv_game.c:PF_Write
  -> client/cl_parse.c:CL_ParseLayout
  -> client/cl_layout.c:SCR_Clear
  -> client/cl_scrn.c frame-type drawer
```

`MSG_ReadByte` intentionally retains signed-`char` behavior for legacy callers,
including `-1` sentinels. Code reading an unsigned wire field must cast its
result to `BYTE`; do not globally change `MSG_ReadByte` semantics.

## What Went Wrong

In the first textured WoW quest scrollbar implementation, I made the mistake of
extending `uiScrollBar_t`, the full legacy FDF runtime shape, with three
complete `uiSimpleButtonState_t` values and transmitted button/thumb
dimensions. That made every scrollbar payload 192 bytes.

This was the wrong payload design for two reasons:

1. It serialized data the quest drawer did not use: four FDF backdrops, three
   fonts, three font colors, three independent UV rectangles, and explicit
   dimensions already implied by the frame geometry.
2. It crossed 127 bytes and exposed an existing decode bug. The one-byte value
   `192` was read through signed `char` as `-64`, then assigned to `DWORD` as
   `4294967232`. `CL_ParseLayout` consequently reported
   `malformed layer 8` because the apparent payload exceeded the message.

The diagnostic regression serialized one frame with a 192-byte payload. Before
the fix it produced this decisive boundary evidence:

```text
CL_ParseLayout: malformed layer 8 frame=1 payload=4294967232 read=10 total=208
```

The mistake was not that 192 exceeds the protocol limit—it does not. The
mistake was treating a broad runtime struct as an appropriate wire schema. The
signed-byte bug was a separate defect revealed by that waste.

## Fix

Both layout decoders now read `uiFrame_t.buffer.size` as
`(BYTE)MSG_ReadByte(...)`, so all legal sizes from 0 through 255 retain their
wire value. The server-side layout diagnostic uses the same interpretation.

The WoW scrollbar now sends `uiScrollBarImage_t`:

| Field | Bytes | Meaning |
|-------|------:|---------|
| `RESOURCE image[3]` | 6 | Down arrow, up arrow, and thumb texture IDs |
| `BYTE texcoord[4]` | 4 | One UV crop shared by all three textures |
| **Total** | **10** | Asserted by the WoW quest serialization test |

The client selects compact scrollbar art only when the payload size exactly
matches `sizeof(uiScrollBarImage_t)`. Otherwise it accepts the full legacy
`uiScrollBar_t` FDF backdrop payload. Thus the optimization does not remove the
generic scrollbar path.

Warcraft III server-authored FDF windows use that legacy path. A serialized
`FT_SCROLLBAR` owns its track, increment arrow, decrement arrow, and thumb art
in one `uiScrollBar_t`; the authored button children are embedded control art
and must not also be emitted as independent frames. This is important for the
stock TextArea controls: serializing those children independently leaves their
template-relative geometry unresolved and can place unrelated placeholder art
at the window origin. The WC3 bridge also applies TextArea's declared scrollbar
relationship before flattening anchors, so a template that provides only a
scrollbar width still resolves to the text viewport's right edge.

Compact mode deliberately supports only what the authoritative WoW template
requires:

- one visual state;
- one UV rectangle shared by all parts;
- no track backdrop;
- square arrow and thumb parts whose height is the frame width multiplied by
  the per-game `UI_PIXEL_ASPECT`;
- white texture tint.

Those limits reduced the type payload from 192 to 10 bytes (94.8%) without
changing the visible 16x16 quest controls. If a future control genuinely needs
more states or independent geometry, define another explicit compact wire
shape or use the existing full schema; do not inflate the common compact shape.

## What We Strive For

For every new `uiFrame_t.buffer` schema:

1. Start from the draw call's irreducible inputs, not an existing runtime
   struct. Write down which values the client cannot infer.
2. Reuse `uiFrame_t` fields before adding payload fields. Geometry belongs in
   `frame.size`/anchors and common art in `frame.tex` when one texture is
   sufficient. Confirm the field is present in `uiFrameFields`; a member merely
   existing in the runtime struct does not make it part of the wire contract.
3. Transmit resource IDs, compact enums, flags, and quantized bytes rather than
   paths, pointers, duplicated colors, or unused state objects.
4. Share values that are identical across parts. A single UV rectangle is
   preferable to three copies.
5. Prefer a documented limitation over speculative generality. Add capability
   only when authoritative data or a real caller requires it.
6. Keep the schema fixed, bounded, and recognizable. Exact payload size may be
   used as a variant discriminator when the drawer supports legacy and compact
   forms.
7. Test the exact serialized size, every field the drawer consumes, the compact
   path, and the legacy/inverse path.
8. Test wire boundaries independently of the current payload. Values above 127
   remain legal and must decode unsigned; values above 255 require a different
   protocol rather than truncation.
9. Document ownership, limitations, and the authoritative source that justifies
   the fields.

The goal is not merely “under 255 bytes.” The goal is a wire schema whose every
byte has a current consumer and whose constraints are obvious to the next
author.

## Diagnostic Workflow

When a layout reports `malformed layer N`:

`ui_layout_debug 1` prints generic client-side `svc_layout` begin/clear/store breadcrumbs, including the layer number, payload size, UI flags, and whether the layer is hidden. Use it only while tracing transport or layer-state problems; game modules should keep their own semantic diagnostics on the game side.

1. Inspect `PF_Write(PF_UIFRAME)` and `CL_ParseLayout` together; verify frame
   number, raw payload byte, decoded payload size, read offset, and message size.
2. Reproduce with a serialization test that writes delta-frame metadata, the
   size byte, the raw payload, and the six-byte zero terminator.
3. Check every one-byte field for signed extension before investigating
   renderer geometry or assets.
4. Use `git blame` and `git log -p -S <symbol>` before changing a shared reader;
   signed behavior may be an established sentinel contract elsewhere.
5. Remove temporary diagnostic logs after the regression captures the root
   cause.

Verification commands:

```sh
make test
make run-wow-preview ARGS="+com_frame_limit 100"
git diff --check
```

Relevant regressions:

- `net.layout_parser_accepts_scrollbar_payload_above_127_bytes`
- `net.layout_scrollbar_draws_cropped_texture_parts_top_to_bottom`
- `net.layout_scrollbar_without_art_draws_nothing`
- `wow_game.deputy_willem_opens_classic_first_human_quest_frame`

## Entity-Context Bindings

`uiFrame_t.stat` remains an unsigned byte on the wire. Values below `MAX_STATS` bind player numeric stats, the existing
`MAX_STATS` range binds player text slots, and the reserved high values `UI_STAT_CONTEXT_NAME`, `UI_STAT_CONTEXT_HEALTH`, and
`UI_STAT_CONTEXT_MANA` bind a server-declared frame to the current layout context.

`LAYER_WORLD_HOVER` uses `cl.hover_entity` as that context only when the recipient's snapshot carries `EF_HOVER_HEALTH`, a live
model, and nonzero health. The generic client resolves the pooled name from `entityState_t.name`/`CS_GENERAL`, reads compressed health
and mana from `entityState_t.stats`, and projects `re.GetEntityOverheadPosition` through the current view matrix. The layer is skipped
when the point is behind the camera or outside the world scissor.

This is not a per-hover network protocol. The server sends the static frame tree, art/font indexes, geometry, and binding declarations
once; mouse picking, projection, and evaluation of already-replicated values happen locally each render frame. Do not revive
`clc_request_unit_ui`/`svc_unit_ui`, add an entity-name query, or resend `svc_layout` on mouse motion.

Spawnable and movable gameplay panels use `svc_window`, not new `UILAYOUTLAYER` entries. Window frame strings are DWORD offsets
into a trailing packet text arena, allowing long text independently of the one-byte frame-type payload size. See
[Client Windows](client-windows.md).

Game modules own the layer contents. WC3 and WoW send their native frame trees during `ClientBegin`; WoW authors compressed creature
vitals through `CustomizeEntity` and keeps `entityState_t.name` hidden until that recipient selects the creature. SC2 sends a header
and terminator with no frames until its gameplay state can author a real widget. The renderer has no parallel health-bar pass, and
there is no ALT-driven show-all mode because one `LAYER_WORLD_HOVER` instance has one `cl.hover_entity` context.

## Key Files

| File | Responsibility |
|------|----------------|
| `common/shared.h` | Shared payload schemas such as `uiScrollBarImage_t` |
| `server/sv_game.c` | Writes delta frame, unsigned size byte, and raw payload |
| `client/cl_parse.c` | Validates and retains complete `svc_layout` blobs |
| `client/cl_layout.c` | Decodes retained frames and attaches payload views |
| `client/cl_scrn.c` | Dispatches frame drawers and interprets typed payloads |
| `tests/test_net.c` | Wire-boundary and client-drawer regressions |

## See Also

- [UI System Architecture](ui-system.md)
- [Network Architecture](network.md)
- [UI Screen Authoring](../ui-authoring.md)
- [WoW Quest UI](../games/world-of-warcraft/quest-ui.md)
- [WC3 Triggered Dialogue](../games/warcraft-3/triggered-dialogue.md)

## Checkbox Payloads

### Race-skinned stock control artwork

Server-authored WC3 windows serialize stock FDF controls after the FDF tree has already been cached. Runtime logging showed that the embedded `ButtonBackdropTemplate` and `EscMenuCheckBox...` children can contain placeholder image indexes even while ordinary dialog backdrops are correct. The typed button/checkbox payload must therefore not trust those cached placeholder images for Blizzard's stock EscMenu control roles.

`games/warcraft-3/game/hud/hud.c` recognizes the stable stock child-role names used by `EscMenuButtonTemplate` and `EscMenuCheckBoxTemplate` and preserves their `war3skins.txt` keys in the image configstrings. The WC3 client UI resolves those keys for the local player when the configstring is loaded. Normal/pushed/disabled button backdrops, button hover highlight, checkbox normal/pushed/disabled backdrops, and checked/disabled-check highlights all use this path. Custom control child names keep their authored image indexes unchanged.

Do **not** globally defer every decorated FDF texture. That experiment fixed the Alliance root backdrop but also caused unrelated decorated backdrops to arrive with empty image paths, while the embedded control parts still carried the placeholder art. Keep per-player late resolution limited to the stock control roles whose authored skin keys are known.
