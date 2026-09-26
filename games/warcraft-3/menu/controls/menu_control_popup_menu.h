#ifndef UI_CONTROL_POPUP_MENU_H
#define UI_CONTROL_POPUP_MENU_H

#define UI_POPUP_MAX_VISIBLE_ROWS 8
#define UI_POPUP_BOTTOM_PADDING_PIXELS 4.0f

static frameDef_t const * active_popup_scroll_menu = NULL;
static uint32_t active_popup_scroll = 0;

static bool UI_IsPopupFrameType(FRAMETYPE type) {
    return type == FT_POPUPMENU || type == FT_GLUEPOPUPMENU;
}

static void UI_ResetPopupScroll(void) {
    active_popup_scroll_menu = NULL;
    active_popup_scroll = 0;
    active_popup_hover_item = -1;
}

static color32_t UI_PopupHoverBackgroundColor(color32_t color) {
    color.a = (uint8_t)((uint32_t)color.a / 10u);
    return color;
}

static frameDef_t * UI_PopupMenuFrame(frameDef_t const * popup) {
    frameDef_t * menu;

    if (!popup || !popup->Popup.MenuFrame[0]) {
        return NULL;
    }
    menu = UI_FindChildFrame((frameDef_t *)popup, popup->Popup.MenuFrame);
    if (!menu) {
        menu = UI_FindFrameNear(popup, popup->Popup.MenuFrame);
    }
    return menu;
}

static bool UI_IsActivePopupMenu(frameDef_t const * frame) {
    return frame && active_popup && frame == UI_PopupMenuFrame(active_popup);
}

static bool UI_PointerBlockedByPopup(frameDef_t const * frame) {
    frameDef_t * menu;

    if (UI_PointerBlockedByModal(frame)) {
        return true;
    }
    if (!active_popup) {
        return false;
    }
    menu = UI_PopupMenuFrame(active_popup);
    if (!menu || frame == active_popup || frame == menu) {
        return false;
    }
    return !UI_FrameWithinRoot(menu, frame);
}

static frameDef_t * UI_PopupTitleTextFrame(frameDef_t const * popup) {
    frameDef_t * title;
    frameDef_t * text;

    if (!popup) {
        return NULL;
    }
    title = UI_FindChildFrame((frameDef_t *)popup, popup->Popup.TitleFrame);
    text = title && title->Text ? UI_FindChildFrame(title, title->Text) : NULL;
    if (!text) {
        text = title ? UI_FindChildFrame(title, "StandardPopupMenuTitleTextTemplate") : NULL;
    }
    if (!text) {
        text = title ? UI_FindChildFrame(title, "CampaignPopupMenuTitleTextTemplate") : NULL;
    }
    if (!text) {
        text = title ? UI_FindChildFrame(title, "BattleNetPopupMenuTitleTextTemplate") : NULL;
    }
    return text ? text : title;
}

static float UI_PopupBottomPadding(void) {
    refExport_t * renderer = mi.GetRenderer();
    rect_t scene = UI_GetSceneRect();
    size2_t window;

    if (!renderer || !renderer->GetWindowSize) {
        return 0.003f;
    }
    window = renderer->GetWindowSize();
    if (window.height <= 0) {
        return 0.003f;
    }
    return scene.h * UI_POPUP_BOTTOM_PADDING_PIXELS / (float)window.height;
}

static float UI_PopupMenuMaxHeight(frameDef_t const * popup, frameDef_t const * menu, float row_height, float border) {
    rect_t const * popup_rect;
    rect_t scene;
    float menu_top;
    float screen_bottom;
    float full_height;
    float max_height;
    float available_height;

    if (!popup || !menu) {
        return 0.0f;
    }
    full_height = border * 2.0f + row_height * (float)menu->Menu.ItemCount;
    max_height = border * 2.0f + row_height * (float)MIN(menu->Menu.ItemCount, UI_POPUP_MAX_VISIBLE_ROWS);
    popup_rect = UI_LayoutRect(popup);
    scene = UI_GetSceneRect();
    menu_top = popup_rect ? popup_rect->y + popup_rect->h : scene.y;
    screen_bottom = scene.y + scene.h - UI_PopupBottomPadding();
    available_height = screen_bottom - menu_top;
    if (available_height < 0.0f) {
        available_height = 0.0f;
    }
    return MIN(full_height, MIN(max_height, available_height));
}

static void UI_PositionPopupParts(frameDef_t * popup) {
    frameDef_t * title;
    frameDef_t * arrow;
    frameDef_t * menu;
    float inset;
    float arrow_width;
    float title_width;

    if (!popup) {
        return;
    }

    inset = popup->Popup.ButtonInset;
    title = UI_FindChildFrame(popup, popup->Popup.TitleFrame);
    arrow = UI_FindChildFrame(popup, popup->Popup.ArrowFrame);
    menu = UI_PopupMenuFrame(popup);
    arrow_width = arrow && arrow->Width > 0.0f ? arrow->Width : 0.011f;
    title_width = popup->Width - arrow_width - inset * 2.0f;

    if (title && !title->AnyPointsSet) {
        UI_SetSize(title, MAX(0.0f, title_width), popup->Height);
        UI_SetPoint(title, FRAMEPOINT_LEFT, popup, FRAMEPOINT_LEFT, inset, 0.0f);
    }
    if (title) {
        frameDef_t * title_text = UI_PopupTitleTextFrame(popup);
        if (title_text) {
            title_text->Font.Justification.Horizontal = FONT_JUSTIFYLEFT;
            title_text->Font.Justification.Offset.x = 0.0f;
        }
    }
    if (arrow && !arrow->AnyPointsSet) {
        UI_SetPoint(arrow, FRAMEPOINT_RIGHT, popup, FRAMEPOINT_RIGHT, -inset, 0.0f);
    }
    if (menu) {
        float row_height = menu->Menu.Item.Height > 0.0f ? menu->Menu.Item.Height : 0.014f;
        float border = menu->Menu.Border > 0.0f ? menu->Menu.Border : 0.006f;
        if (menu->Menu.ItemCount > 0) {
            UI_SetSize(menu, popup->Width, UI_PopupMenuMaxHeight(popup, menu, row_height, border));
        }
        if (!menu->AnyPointsSet) {
            UI_SetPoint(menu, FRAMEPOINT_TOPLEFT, popup, FRAMEPOINT_BOTTOMLEFT, 0.0f, 0.0f);
            UI_SetPoint(menu, FRAMEPOINT_TOPRIGHT, popup, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
        }
    }
}

static void UI_UpdatePopupVisibility(frameDef_t const * const *draw_order, uint32_t count) {
    FOR_LOOP(i, count) {
        frameDef_t * frame = (frameDef_t *)draw_order[i];

        if (!UI_IsPopupFrameType(frame->Type)) {
            continue;
        }

        frameDef_t * menu = UI_PopupMenuFrame(frame);
        UI_PositionPopupParts(frame);
        if (menu) {
            UI_SetHidden(menu, active_popup != frame);
        }
    }
}

static void UI_DrawMenu(frameDef_t const * frame, rect_t const * rect) {
    refExport_t * renderer = mi.GetRenderer();
    frameDef_t const * backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
    font_t const * font;
    float const border = frame->Menu.Border > 0.0f ? frame->Menu.Border : 0.006f;
    float const row_height = frame->Menu.Item.Height > 0.0f ? frame->Menu.Item.Height : 0.014f;
    float const content_height = MAX(0.0f, rect->h - border * 2.0f);
    color32_t const highlight_color = frame->Menu.TextHighlightColor.a
        ? frame->Menu.TextHighlightColor
        : Theme_ListBoxSelectedTextColor();
    color32_t const text_color = frame->Font.Color.a ? frame->Font.Color : COLOR32_WHITE;
    uint32_t visible_rows;
    uint32_t max_scroll;
    rect_t clip;

    UI_DrawBackdropWithColor(backdrop, rect, frame->Color);
    if (!renderer || !renderer->LoadFont || !renderer->DrawText) {
        return;
    }
    font = renderer->LoadFont(UI_FontFile(frame->Font.Name), UI_FontPixelSize(frame->Font.Size));
    if (!font) {
        return;
    }
    visible_rows = content_height > 0.0f ? (uint32_t)floorf(content_height / row_height) : 0;
    if (content_height > (float)visible_rows * row_height + 0.0001f) {
        visible_rows++;
    }
    if (visible_rows > frame->Menu.ItemCount) {
        visible_rows = frame->Menu.ItemCount;
    }
    max_scroll = frame->Menu.ItemCount > visible_rows ? frame->Menu.ItemCount - visible_rows : 0;
    if (active_popup_scroll_menu != frame) {
        active_popup_scroll_menu = frame;
        active_popup_scroll = 0;
    }
    if (active_popup_scroll > max_scroll) {
        active_popup_scroll = max_scroll;
    }
    clip = MAKE(rect_t,
                rect->x + border,
                rect->y + border,
                MAX(0.0f, rect->w - border * 2.0f),
                content_height);

    FOR_LOOP(row_index, visible_rows) {
        uint32_t const i = active_popup_scroll + row_index;
        rect_t row = MAKE(rect_t,
                        rect->x + border,
                        rect->y + border + row_height * (float)row_index,
                        MAX(0.0f, rect->w - border * 2.0f),
                        row_height);
        rect_t hover_rect = row;
        bool const hover = (int)i == active_popup_hover_item;

        if (i >= frame->Menu.ItemCount || row.y >= clip.y + clip.h) {
            break;
        }
        if (hover_rect.y + hover_rect.h > clip.y + clip.h) {
            hover_rect.h = MAX(0.0f, clip.y + clip.h - hover_rect.y);
        }
        if (hover && hover_rect.h > 0.0f && renderer->DrawImageEx) {
            renderer->DrawImageEx(&MAKE(drawImage_t,
                                        .texture = NULL,
                                        .shader = SHADER_UI,
                                        .alphamode = BLEND_MODE_BLEND,
                                        .screen = hover_rect,
                                        .uv = MAKE(rect_t, 0, 0, 1, 1),
                                         .color = UI_PopupHoverBackgroundColor(text_color),
                                         .flags = DRAW_CLIP,
                                        .clip = clip));
        }

        renderer->DrawText(&MAKE(drawText_t,
                                 .font = font,
                                 .text = frame->Menu.Items[i].text,
                                 .rect = row,
                                 .color = hover ? highlight_color : text_color,
                                 .textWidth = row.w,
                                  .lineHeight = 1.0f,
                                  .flags = DRAW_CLIP,
                                  .halign = FONT_JUSTIFYLEFT,
                                  .valign = FONT_JUSTIFYMIDDLE,
                                  .clip = clip));
    }
    if (max_scroll > 0 && renderer->DrawImageEx) {
        float const scroll_w = MIN(0.004f, MAX(0.0f, clip.w * 0.2f));
        rect_t track = MAKE(rect_t,
                          clip.x + clip.w - scroll_w,
                          clip.y,
                          scroll_w,
                          clip.h);
        float thumb_h = MIN(track.h, MAX(row_height, track.h * (float)visible_rows / (float)frame->Menu.ItemCount));
        float travel = MAX(0.0f, track.h - thumb_h);
        rect_t thumb = MAKE(rect_t,
                          track.x,
                          track.y + (max_scroll ? travel * (float)active_popup_scroll / (float)max_scroll : 0.0f),
                          track.w,
                          thumb_h);

        renderer->DrawImageEx(&MAKE(drawImage_t,
                                    .texture = NULL,
                                    .shader = SHADER_UI,
                                    .alphamode = BLEND_MODE_BLEND,
                                    .screen = track,
                                    .uv = MAKE(rect_t, 0, 0, 1, 1),
                                     .color = MAKE(color32_t, 0, 0, 0, 96),
                                     .flags = DRAW_CLIP,
                                    .clip = clip));
        renderer->DrawImageEx(&MAKE(drawImage_t,
                                    .texture = NULL,
                                    .shader = SHADER_UI,
                                    .alphamode = BLEND_MODE_BLEND,
                                    .screen = thumb,
                                    .uv = MAKE(rect_t, 0, 0, 1, 1),
                                     .color = Theme_ListBoxSelectionColor(),
                                     .flags = DRAW_CLIP,
                                    .clip = clip));
    }
}

#endif
