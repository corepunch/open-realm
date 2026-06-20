#ifndef UI_CONTROL_MAP_LIST_H
#define UI_CONTROL_MAP_LIST_H

static void UI_DrawMapListControl(LPCFRAMEDEF frame, LPCRECT rect) {
    LPRENDERER renderer = UI_GetRenderer();
    uiMapListControl_t const *control;
    uiMapListState_t *state;
    LPCFONT font;
    DWORD visible_rows;
    FLOAT row_height;
    DWORD first_row;
    FLOAT visual_scroll;
    FLOAT row_offset;
    RECT content;
    RECT clip;
    VECTOR2 mouse;

    if (!frame || !rect) {
        return;
    }

    control = &frame->MapListControl;
    state = control->State;
    if (!state || !renderer || !renderer->LoadFont || !renderer->DrawText) {
        return;
    }

    row_height = control->RowHeight > 0 ? control->RowHeight : 0.019f;
    visible_rows = control->VisibleRows ? control->VisibleRows : (DWORD)((rect->h - control->InsetY * 2.0f) / row_height);
    content = MAKE(RECT,
                   rect->x + control->InsetX,
                   rect->y + control->InsetY,
                   rect->w - control->InsetX * 2.0f,
                   row_height);
    clip = MAKE(RECT,
                content.x,
                content.y,
                content.w,
                row_height * (FLOAT)visible_rows);

    if (!UI_PointerBlockedByPopup(frame) &&
        UI_MouseContains(rect) &&
        uiimport.GetMouseEvent && uiimport.GetMouseEvent() == UI_CLIENT_MOUSE_LEFT_UP && visible_rows > 0) {
        FLOAT row;
        DWORD index;
        mouse = UI_MouseToFdf();
        row = (mouse.y - content.y) / row_height;
        index = (DWORD)floorf(state->visualScroll + row);
        if (row >= 0.0f && row < (FLOAT)visible_rows && index < state->count) {
            char command[128];
            snprintf(command,
                     sizeof(command),
                     control->SelectCommand[0] ? control->SelectCommand : "menu_lan_select %u",
                     (unsigned)index);
            UI_MenuCommandLocal(command);
        }
    }
    if (!UI_PointerBlockedByPopup(frame) &&
        UI_MouseContains(rect) &&
        uiimport.GetMouseEvent &&
        (uiimport.GetMouseEvent() == UI_CLIENT_MOUSE_WHEEL_UP || uiimport.GetMouseEvent() == UI_CLIENT_MOUSE_WHEEL_DOWN) &&
        visible_rows > 0 && state->count > visible_rows) {
        DWORD const max_scroll = state->count - visible_rows;

        if (uiimport.GetMouseEvent() == UI_CLIENT_MOUSE_WHEEL_UP) {
            state->scroll = state->scroll > 0 ? state->scroll - 1 : 0;
        } else if (state->scroll < max_scroll) {
            state->scroll++;
        }
    }

    font = renderer->LoadFont(UI_FontFile(control->FontName), UI_FontPixelSize(control->FontSize));
    if (!font) {
        return;
    }

    visual_scroll = state->visualScroll;
    if (visual_scroll < 0.0f) {
        visual_scroll = 0.0f;
    }
    first_row = (DWORD)floorf(visual_scroll);
    row_offset = (visual_scroll - (FLOAT)first_row) * row_height;

    for (DWORD row = 0; row <= visible_rows; row++) {
        DWORD const index = first_row + row;
        uiMapListItem_t const *item;
        char text[256];
        BOOL selected;
        RECT row_rect = content;
        RECT icon_rect;
        RECT text_rect;

        if (index >= state->count) {
            break;
        }

        item = &state->items[index];
        selected = index == state->selected;
        row_rect.y += row_height * (FLOAT)row - row_offset;
        if (row_rect.y + row_rect.h <= clip.y || row_rect.y >= clip.y + clip.h) {
            continue;
        }
        if (selected && renderer->DrawImageEx) {
            RECT selection = row_rect;
            selection.x += 0.0025f;
            selection.y += 0.002f;
            selection.w -= 0.005f;
            selection.h -= 0.004f;
            renderer->DrawImageEx(&MAKE(drawImage_t,
                                        .texture = NULL,
                                        .shader = SHADER_UI,
                                        .alphamode = BLEND_MODE_BLEND,
                                        .screen = selection,
                                        .uv = MAKE(RECT, 0, 0, 1, 1),
                                        .color = Theme_ListBoxSelectionColor(),
                                        .hasClip = TRUE,
                                        .clip = clip));
        }
        snprintf(text,
                 sizeof(text),
                 "%s",
                 item->name[0] ? item->name : item->path);
        icon_rect = row_rect;
        icon_rect.x += 0.004f;
        icon_rect.y += 0.001f;
        icon_rect.w = row_height - 0.002f;
        icon_rect.h = row_height - 0.002f;
        if (renderer->DrawImageEx) {
            DWORD const icon = UI_LoadTexture("ui\\widgets\\glues\\icon-file-melee.blp", false);
            LPCTEXTURE icon_texture = UI_GetTexture(icon);

            if (icon_texture) {
                renderer->DrawImageEx(&MAKE(drawImage_t,
                                            .texture = icon_texture,
                                            .shader = SHADER_UI,
                                            .alphamode = BLEND_MODE_BLEND,
                                            .screen = icon_rect,
                                            .uv = MAKE(RECT, 0, 0, 1, 1),
                                            .color = COLOR32_WHITE,
                                            .hasClip = TRUE,
                                            .clip = clip));
            }
        }
        if (item->players > 0) {
            char players[8];
            LPCFONT small_font = renderer->LoadFont(UI_FontFile(control->FontName), 9);

            snprintf(players, sizeof(players), "%u", (unsigned)item->players);
            if (small_font) {
                renderer->DrawText(&MAKE(drawText_t,
                                         .font = small_font,
                                         .text = players,
                                         .rect = icon_rect,
                                         .color = Theme_ListBoxIconTextColor(),
                                         .textWidth = icon_rect.w,
                                         .lineHeight = 1.0f,
                                         .wordWrap = FALSE,
                                         .halign = FONT_JUSTIFYCENTER,
                                         .valign = FONT_JUSTIFYMIDDLE,
                                         .hasClip = TRUE,
                                         .clip = clip));
            }
        }
        text_rect = row_rect;
        text_rect.x += 0.026f;
        text_rect.w -= 0.028f;
        renderer->DrawText(&MAKE(drawText_t,
                                 .font = font,
                                 .text = text,
                                 .rect = text_rect,
                                 .color = selected ? control->SelectedTextColor : control->TextColor,
                                 .textWidth = text_rect.w,
                                 .lineHeight = 1.0f,
                                 .wordWrap = FALSE,
                                 .halign = FONT_JUSTIFYLEFT,
                                 .valign = FONT_JUSTIFYMIDDLE,
                                 .hasClip = TRUE,
                                 .clip = clip));
    }
}

#endif
