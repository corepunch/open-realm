#ifndef UI_CONTROL_POPUP_MENU_H
#define UI_CONTROL_POPUP_MENU_H

#define UI_POPUP_MAX_VISIBLE_ROWS 8
#define UI_POPUP_BOTTOM_PADDING_PIXELS 4.0f

static LPCFRAMEDEF active_popup_scroll_menu = NULL;
static DWORD active_popup_scroll = 0;

static BOOL UI_IsPopupFrameType(FRAMETYPE type) {
    return type == FT_POPUPMENU || type == FT_GLUEPOPUPMENU;
}

static void UI_ResetPopupScroll(void) {
    active_popup_scroll_menu = NULL;
    active_popup_scroll = 0;
}

static COLOR32 UI_PopupHoverBackgroundColor(COLOR32 color) {
    color.a = (BYTE)((DWORD)color.a / 10u);
    return color;
}

static LPFRAMEDEF UI_PopupMenuFrame(LPCFRAMEDEF popup) {
    LPFRAMEDEF menu;

    if (!popup || !popup->Popup.MenuFrame[0]) {
        return NULL;
    }
    menu = UI_FindChildFrame((LPFRAMEDEF)popup, popup->Popup.MenuFrame);
    if (!menu) {
        menu = UI_FindFrameNear(popup, popup->Popup.MenuFrame);
    }
    return menu;
}

static BOOL UI_IsActivePopupMenu(LPCFRAMEDEF frame) {
    return frame && active_popup && frame == UI_PopupMenuFrame(active_popup);
}

static BOOL UI_PointerBlockedByPopup(LPCFRAMEDEF frame) {
    LPFRAMEDEF menu;
    LPCRECT rect;

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
    rect = UI_LayoutRect(menu);
    return rect && UI_MouseContains(rect);
}

static LPFRAMEDEF UI_PopupTitleTextFrame(LPCFRAMEDEF popup) {
    LPFRAMEDEF title;
    LPFRAMEDEF text;

    if (!popup) {
        return NULL;
    }
    title = UI_FindChildFrame((LPFRAMEDEF)popup, popup->Popup.TitleFrame);
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

static FLOAT UI_PopupBottomPadding(void) {
    LPRENDERER renderer = UI_GetRenderer();
    RECT scene = UI_GetSceneRect();
    size2_t window;

    if (!renderer || !renderer->GetWindowSize) {
        return 0.003f;
    }
    window = renderer->GetWindowSize();
    if (window.height <= 0) {
        return 0.003f;
    }
    return scene.h * UI_POPUP_BOTTOM_PADDING_PIXELS / (FLOAT)window.height;
}

static FLOAT UI_PopupMenuMaxHeight(LPCFRAMEDEF popup, LPCFRAMEDEF menu, FLOAT row_height, FLOAT border) {
    LPCRECT popup_rect;
    RECT scene;
    FLOAT menu_top;
    FLOAT screen_bottom;
    FLOAT full_height;
    FLOAT max_height;
    FLOAT available_height;

    if (!popup || !menu) {
        return 0.0f;
    }
    full_height = border * 2.0f + row_height * (FLOAT)menu->Menu.ItemCount;
    max_height = border * 2.0f + row_height * (FLOAT)MIN(menu->Menu.ItemCount, UI_POPUP_MAX_VISIBLE_ROWS);
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

static void UI_PositionPopupParts(LPFRAMEDEF popup) {
    LPFRAMEDEF title;
    LPFRAMEDEF arrow;
    LPFRAMEDEF menu;
    FLOAT inset;
    FLOAT arrow_width;
    FLOAT title_width;

    if (!popup) {
        return;
    }

    inset = popup->Popup.ButtonInset;
    title = UI_FindChildFrame(popup, popup->Popup.TitleFrame);
    arrow = UI_FindChildFrame(popup, popup->Popup.ArrowFrame);
    menu = UI_PopupMenuFrame(popup);
    arrow_width = arrow && arrow->base.size.width > 0.0f ? arrow->base.size.width : 0.011f;
    title_width = popup->base.size.width - arrow_width - inset * 2.0f;

    if (title && !title->AnyPointsSet) {
        UI_SetSize(title, MAX(0.0f, title_width), popup->base.size.height);
        UI_SetPoint(title, FRAMEPOINT_LEFT, popup, FRAMEPOINT_LEFT, inset, 0.0f);
    }
    if (title) {
        LPFRAMEDEF title_text = UI_PopupTitleTextFrame(popup);
        if (title_text) {
            title_text->Font.Justification.Horizontal = FONT_JUSTIFYLEFT;
            title_text->Font.Justification.Offset.x = 0.0f;
        }
    }
    if (arrow && !arrow->AnyPointsSet) {
        UI_SetPoint(arrow, FRAMEPOINT_RIGHT, popup, FRAMEPOINT_RIGHT, -inset, 0.0f);
    }
    if (menu) {
        FLOAT row_height = menu->Menu.Item.Height > 0.0f ? menu->Menu.Item.Height : 0.014f;
        FLOAT border = menu->Menu.Border > 0.0f ? menu->Menu.Border : 0.006f;
        if (menu->Menu.ItemCount > 0) {
            UI_SetSize(menu, popup->base.size.width, UI_PopupMenuMaxHeight(popup, menu, row_height, border));
        }
        if (!menu->AnyPointsSet) {
            UI_SetPoint(menu, FRAMEPOINT_TOPLEFT, popup, FRAMEPOINT_BOTTOMLEFT, 0.0f, 0.0f);
            UI_SetPoint(menu, FRAMEPOINT_TOPRIGHT, popup, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
        }
    }
}

static void UI_UpdatePopupVisibility(LPCFRAMEDEF const *draw_order, DWORD count) {
    FOR_LOOP(i, count) {
        LPFRAMEDEF frame = (LPFRAMEDEF)draw_order[i];

        if (!UI_IsPopupFrameType(frame->base.type)) {
            continue;
        }

        LPFRAMEDEF menu = UI_PopupMenuFrame(frame);
        UI_PositionPopupParts(frame);
        if (menu) {
            UI_SetHidden(menu, active_popup != frame);
        }
    }
}

static void UI_DrawMenu(LPCFRAMEDEF frame, LPCRECT rect) {
    LPRENDERER renderer = UI_GetRenderer();
    LPCFRAMEDEF backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
    LPCFONT font;
    FLOAT const border = frame->Menu.Border > 0.0f ? frame->Menu.Border : 0.006f;
    FLOAT const row_height = frame->Menu.Item.Height > 0.0f ? frame->Menu.Item.Height : 0.014f;
    FLOAT const content_height = MAX(0.0f, rect->h - border * 2.0f);
    COLOR32 const highlight_color = frame->Menu.TextHighlightColor.a
        ? frame->Menu.TextHighlightColor
        : Theme_ListBoxSelectedTextColor();
    COLOR32 const text_color = frame->Font.Color.a ? frame->Font.Color : COLOR32_WHITE;
    DWORD visible_rows;
    DWORD max_scroll;
    RECT clip;

    UI_DrawBackdropWithColor(backdrop, rect, frame->base.color);
    if (!renderer || !renderer->LoadFont || !renderer->DrawText) {
        return;
    }
    font = renderer->LoadFont(UI_FontFile(frame->Font.Name), UI_FontPixelSize(frame->Font.Size));
    if (!font) {
        return;
    }
    visible_rows = content_height > 0.0f ? (DWORD)floorf(content_height / row_height) : 0;
    if (content_height > (FLOAT)visible_rows * row_height + 0.0001f) {
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
    clip = MAKE(RECT,
                rect->x + border,
                rect->y + border,
                MAX(0.0f, rect->w - border * 2.0f),
                content_height);

    FOR_LOOP(row_index, visible_rows) {
        DWORD const i = active_popup_scroll + row_index;
        RECT row = MAKE(RECT,
                        rect->x + border,
                        rect->y + border + row_height * (FLOAT)row_index,
                        MAX(0.0f, rect->w - border * 2.0f),
                        row_height);
        RECT hover_rect = row;
        BOOL hover;

        if (i >= frame->Menu.ItemCount || row.y >= clip.y + clip.h) {
            break;
        }
        if (hover_rect.y + hover_rect.h > clip.y + clip.h) {
            hover_rect.h = MAX(0.0f, clip.y + clip.h - hover_rect.y);
        }
        hover = hover_rect.h > 0.0f && UI_MouseContains(&hover_rect);
        if (hover && renderer->DrawImageEx) {
            renderer->DrawImageEx(&MAKE(drawImage_t,
                                        .texture = NULL,
                                        .shader = SHADER_UI,
                                        .alphamode = BLEND_MODE_BLEND,
                                        .screen = hover_rect,
                                        .uv = MAKE(RECT, 0, 0, 1, 1),
                                        .color = UI_PopupHoverBackgroundColor(text_color),
                                        .hasClip = TRUE,
                                        .clip = clip));
        }

        renderer->DrawText(&MAKE(drawText_t,
                                 .font = font,
                                 .text = frame->Menu.Items[i].text,
                                 .rect = row,
                                 .color = hover ? highlight_color : text_color,
                                 .textWidth = row.w,
                                 .lineHeight = 1.0f,
                                 .wordWrap = FALSE,
                                 .halign = FONT_JUSTIFYLEFT,
                                 .valign = FONT_JUSTIFYMIDDLE,
                                 .hasClip = TRUE,
                                 .clip = clip));
    }
    if (max_scroll > 0 && renderer->DrawImageEx) {
        FLOAT const scroll_w = MIN(0.004f, MAX(0.0f, clip.w * 0.2f));
        RECT track = MAKE(RECT,
                          clip.x + clip.w - scroll_w,
                          clip.y,
                          scroll_w,
                          clip.h);
        FLOAT thumb_h = MIN(track.h, MAX(row_height, track.h * (FLOAT)visible_rows / (FLOAT)frame->Menu.ItemCount));
        FLOAT travel = MAX(0.0f, track.h - thumb_h);
        RECT thumb = MAKE(RECT,
                          track.x,
                          track.y + (max_scroll ? travel * (FLOAT)active_popup_scroll / (FLOAT)max_scroll : 0.0f),
                          track.w,
                          thumb_h);

        renderer->DrawImageEx(&MAKE(drawImage_t,
                                    .texture = NULL,
                                    .shader = SHADER_UI,
                                    .alphamode = BLEND_MODE_BLEND,
                                    .screen = track,
                                    .uv = MAKE(RECT, 0, 0, 1, 1),
                                    .color = MAKE(COLOR32, 0, 0, 0, 96),
                                    .hasClip = TRUE,
                                    .clip = clip));
        renderer->DrawImageEx(&MAKE(drawImage_t,
                                    .texture = NULL,
                                    .shader = SHADER_UI,
                                    .alphamode = BLEND_MODE_BLEND,
                                    .screen = thumb,
                                    .uv = MAKE(RECT, 0, 0, 1, 1),
                                    .color = Theme_ListBoxSelectionColor(),
                                    .hasClip = TRUE,
                                    .clip = clip));
    }
}

#endif
