#ifndef UI_CONTROL_MAP_LIST_H
#define UI_CONTROL_MAP_LIST_H

#define MAP_LIST_SCROLLBAR_WIDTH 0.0125f

typedef struct {
    rect_t up, down, track, thumb;
} mapListScrollbar_t;

static uint32_t UI_MapListVisibleRows(uiMapListControl_t const *control, rect_t const *rect) {
    float const row_height = control->RowHeight > 0 ? control->RowHeight : 0.019f;

    return control->VisibleRows ? control->VisibleRows
                                : (uint32_t)((rect->h - control->InsetY * 2.0f) / row_height);
}

/* The scrollbar exists only when the list overflows, as in the original client. */
static bool UI_MapListScrollbar(uiMapListControl_t const *control, rect_t const *rect, mapListScrollbar_t *bar) {
    uiMapListState_t const *state = control->State;
    float const row_height = control->RowHeight > 0 ? control->RowHeight : 0.019f;
    uint32_t const visible_rows = UI_MapListVisibleRows(control, rect);
    float const width = MAP_LIST_SCROLLBAR_WIDTH;
    float const height = row_height * (float)visible_rows;
    float x, y, travel, fraction;

    if (!state || visible_rows == 0 || state->count <= visible_rows) {
        return false;
    }
    x = rect->x + rect->w - control->InsetX - width;
    y = rect->y + control->InsetY;
    bar->track = MAKE(rect_t, x, y, width, height);
    bar->up = MAKE(rect_t, x, y, width, width);
    bar->down = MAKE(rect_t, x, y + height - width, width, width);
    travel = height - width * 3.0f;
    fraction = (float)MIN(state->scroll, state->count - visible_rows) / (float)(state->count - visible_rows);
    bar->thumb = MAKE(rect_t, x, y + width + travel * fraction, width, width);
    return true;
}

static void UI_DrawMapListControl(frameDef_t const *frame, rect_t const *rect) {
    refExport_t *renderer = mi.GetRenderer();
    uiMapListControl_t const *control;
    uiMapListState_t *state;
    font_t const *font;
    uint32_t visible_rows;
    float row_height;
    uint32_t first_row;
    float visual_scroll;
    float row_offset;
    rect_t content;
    rect_t clip;
    mapListScrollbar_t bar;
    bool has_bar;

    if (!frame || !rect) {
        return;
    }

    control = &frame->MapListControl;
    state = control->State;
    if (!state || !renderer || !renderer->LoadFont || !renderer->DrawText) {
        return;
    }

    row_height = control->RowHeight > 0 ? control->RowHeight : 0.019f;
    visible_rows = control->VisibleRows ? control->VisibleRows : (uint32_t)((rect->h - control->InsetY * 2.0f) / row_height);
    content = MAKE(rect_t,
                   rect->x + control->InsetX,
                   rect->y + control->InsetY,
                   rect->w - control->InsetX * 2.0f,
                   row_height);
    has_bar = UI_MapListScrollbar(control, rect, &bar);
    if (has_bar) {
        content.w -= MAP_LIST_SCROLLBAR_WIDTH + 0.002f;
    }
    clip = MAKE(rect_t,
                content.x,
                content.y,
                content.w,
                row_height * (float)visible_rows);

    font = renderer->LoadFont(UI_FontFile(control->FontName), UI_FontPixelSize(control->FontSize));
    if (!font) {
        return;
    }

    visual_scroll = state->visualScroll;
    if (visual_scroll < 0.0f) {
        visual_scroll = 0.0f;
    }
    first_row = (uint32_t)floorf(visual_scroll);
    row_offset = (visual_scroll - (float)first_row) * row_height;

    for (uint32_t row = 0; row <= visible_rows; row++) {
        uint32_t const index = first_row + row;
        uiMapListItem_t const *item;
        char text[256];
        bool selected;
        rect_t row_rect = content;
        rect_t icon_rect;
        rect_t text_rect;

        if (index >= state->count) {
            break;
        }

        item = &state->items[index];
        selected = index == state->selected;
        row_rect.y += row_height * (float)row - row_offset;
        if (row_rect.y + row_rect.h <= clip.y || row_rect.y >= clip.y + clip.h) {
            continue;
        }
        if (selected && renderer->DrawImageEx) {
            rect_t selection = row_rect;
            selection.x += 0.0025f;
            selection.y += 0.002f;
            selection.w -= 0.005f;
            selection.h -= 0.004f;
            renderer->DrawImageEx(&MAKE(drawImage_t,
                                        .texture = NULL,
                                        .shader = SHADER_UI,
                                        .alphamode = BLEND_MODE_BLEND,
                                        .screen = selection,
                                        .uv = MAKE(rect_t, 0, 0, 1, 1),
                                         .color = Theme_ListBoxSelectionColor(),
                                         .flags = DRAW_CLIP,
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
            cstring_t const icon_path = item->kind == UI_MAP_ITEM_FOLDER ? "ui\\widgets\\glues\\icon-folder.blp"
                                      : item->kind == UI_MAP_ITEM_PARENT ? "ui\\widgets\\glues\\icon-folder-up.blp"
                                                                         : "ui\\widgets\\glues\\icon-file-melee.blp";
            uint32_t const icon = UI_LoadTexture(icon_path, false);
            texture_t const *icon_texture = UI_GetTexture(icon);

            if (icon_texture) {
                renderer->DrawImageEx(&MAKE(drawImage_t,
                                            .texture = icon_texture,
                                            .shader = SHADER_UI,
                                            .alphamode = BLEND_MODE_BLEND,
                                            .screen = icon_rect,
                                            .uv = MAKE(rect_t, 0, 0, 1, 1),
                                             .color = COLOR32_WHITE,
                                             .flags = DRAW_CLIP,
                                            .clip = clip));
            }
        }
        if (item->kind == UI_MAP_ITEM_MAP && item->players > 0) {
            char players[8];
            font_t const *small_font = renderer->LoadFont(UI_FontFile(control->FontName), 9);

            snprintf(players, sizeof(players), "%u", (unsigned)item->players);
            if (small_font) {
                renderer->DrawText(&MAKE(drawText_t,
                                         .font = small_font,
                                         .text = players,
                                         .rect = icon_rect,
                                         .color = Theme_ListBoxIconTextColor(),
                                         .textWidth = icon_rect.w,
                                          .lineHeight = 1.0f,
                                          .flags = DRAW_CLIP,
                                          .halign = FONT_JUSTIFYCENTER,
                                          .valign = FONT_JUSTIFYMIDDLE,
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
                                  .flags = DRAW_CLIP,
                                  .halign = FONT_JUSTIFYLEFT,
                                  .valign = FONT_JUSTIFYMIDDLE,
                                  .clip = clip));
    }

    if (has_bar && renderer->DrawImageEx) {
        struct { rect_t const *rect; cstring_t path; } const parts[] = {
            { &bar.up, "UI\\Widgets\\Glues\\GlueScreen-Scrollbar-UpArrow.blp" },
            { &bar.down, "UI\\Widgets\\Glues\\GlueScreen-Scrollbar-DownArrow.blp" },
            { &bar.thumb, "UI\\Widgets\\Glues\\SinglePlayerSkirmish-ScrollBarKnob.blp" },
        };

        for (uint32_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
            texture_t const *texture = UI_GetTexture(UI_LoadTexture(parts[i].path, false));

            if (!texture) {
                continue;
            }
            renderer->DrawImageEx(&MAKE(drawImage_t,
                                        .texture = texture,
                                        .shader = SHADER_UI,
                                        .alphamode = BLEND_MODE_BLEND,
                                        .screen = *parts[i].rect,
                                        .uv = MAKE(rect_t, 0, 0, 1, 1),
                                        .color = COLOR32_WHITE));
        }
    }
}

#endif
