# Shared uiBaseFrame — Engine/Game UI Pattern

## Pattern
Same as `edict_s` in `server.h` / `g_local.h`: engine defines base struct, game extends it with `// keep above in sync` comment.

## Steps

### Step 1: Define uiBaseFrame_t in common/shared.h
Common fields: number, type, parent (void*), anchor, size, color, alpha, hidden, disabled, image, texcoord, text, text_color, bg/edge images, tile_bg, bg_insets, ui_flags, on_event, on_draw.

### Step 2: Embed uiBaseFrame_t in WC3 uiFrameDef_s
Add `uiBaseFrame_t base` as first member. Update existing code to access via `frame->base.field` where needed.

### Step 3: Embed uiBaseFrame_t in WoW uiWowXmlElem_t
Add `uiBaseFrame_t base` as first member. Update XML code to access via `e->base.field`.

### Step 4: Add shared helper functions in client/ui.h
Generic functions operating on uiBaseFrame_t*: UI_BaseIsVisible, UI_BaseSetHidden, UI_BaseSetSize.

### Step 5: Update WC3 ui_render.c to use base fields
Hit testing, event dispatch, draw dispatch go through base struct.

### Step 6: Update WoW ui_xml.c to use base fields
Hit testing, event dispatch, draw go through base struct.

### Step 7: Verify both builds clean
