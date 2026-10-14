#ifndef UI_CONTROL_EDITBOX_H
#define UI_CONTROL_EDITBOX_H

static frameDef_t *UI_CreateEditTextFrame(frameDef_t *frame) {
    frameDef_t *text_frame;
    frameDef_t const *template;

    if (!frame) {
        return NULL;
    }

    text_frame = UI_Spawn(FT_TEXT, frame);
    if (!text_frame) {
        return NULL;
    }

    snprintf(text_frame->Name,
             sizeof(text_frame->Name),
             "%.75sText",
             frame->Name[0] ? frame->Name : "EditBox");
    template = UI_FindFrame("StandardEditBoxTextTemplate");
    if (template) {
        text_frame->DecorateFileNames = template->DecorateFileNames;
        text_frame->Width = template->Width;
        text_frame->Height = template->Height;
        text_frame->Font = template->Font;
    }
    if (!text_frame->Font.Name[0]) {
        snprintf(text_frame->Font.Name, sizeof(text_frame->Font.Name), "MasterFont");
    }
    if (text_frame->Font.Size <= 0.0f) {
        text_frame->Font.Size = 0.015f;
    }
    if (!text_frame->Font.Color.a) {
        text_frame->Font.Color = COLOR32_WHITE;
    }
    text_frame->Font.Justification.Horizontal = FONT_JUSTIFYLEFT;
    text_frame->Font.Justification.Vertical = FONT_JUSTIFYMIDDLE;
    UI_SetAllPoints(text_frame);
    snprintf(frame->Edit.TextFrame, sizeof(frame->Edit.TextFrame), "%s", text_frame->Name);
    return text_frame;
}

static frameDef_t *UI_EditTextFrame(frameDef_t const *frame) {
    frameDef_t *text_frame;

    if (!frame) {
        return NULL;
    }
    text_frame = UI_FindChildFrame((frameDef_t *)frame, frame->Edit.TextFrame);
    if (!text_frame && frame->Edit.TextFrame[0]) {
        text_frame = UI_FindFrameNear(frame, frame->Edit.TextFrame);
    }
    if (!text_frame) {
        text_frame = UI_CreateEditTextFrame((frameDef_t *)frame);
    }
    return text_frame;
}

static cstring_t UI_EditText(frameDef_t const *frame) {
    frameDef_t *text_frame = UI_EditTextFrame(frame);
    return text_frame && text_frame->Text ? text_frame->Text : "";
}

static void UI_SetEditText(frameDef_t const *frame, cstring_t text) {
    frameDef_t *text_frame = UI_EditTextFrame(frame);

    if (!text_frame) {
        return;
    }
    UI_SetText(text_frame, "%s", text ? text : "");
}

static uint32_t UI_EditMaxChars(frameDef_t const *frame) {
    uint32_t max_chars = frame && frame->Edit.MaxChars ? frame->Edit.MaxChars : 255;
    return MIN(max_chars, sizeof(((frameDef_t *)frame)->TextStorage) - 1);
}

static void UI_FocusEdit(frameDef_t *frame) {
    if (!frame) {
        active_edit = NULL;
        drag_edit = NULL;
        active_ti.text = NULL;
        active_ti.size = 0;
        active_ti.max_chars = 0;
        active_ti.cursor = 0;
        active_ti.anchor = 0;
        return;
    }
    if (active_edit != frame) {
        frameDef_t *text_frame = UI_EditTextFrame(frame);
        active_edit = frame;
        active_ti.text = text_frame ? text_frame->TextStorage : NULL;
        active_ti.size = text_frame ? sizeof(text_frame->TextStorage) : 0;
        active_ti.max_chars = UI_EditMaxChars(frame);
        active_ti.cursor = (uint32_t)strlen(UI_EditText(frame));
        active_ti.anchor = active_ti.cursor;
    }
}

bool M_EditKey(int key, uint32_t mods) {
    int result;

    if (!active_edit) {
        return false;
    }

    result = M_TextInput_KeyModified(&active_ti, key,
                                    (mods & KEY_MOD_SHIFT) != 0,
                                    (mods & KEY_MOD_CTRL) != 0);
    switch (result) {
        case MENU_TEXTINPUT_CONSUMED:
            return true;
        case MENU_TEXTINPUT_ENTER:
            return false;
        case MENU_TEXTINPUT_ESCAPE:
            UI_FocusEdit(NULL);
            return false;
        default:
            return false;
    }
}

void UI_EditTextInput(cstring_t text) {
    char filtered[256];

    if (!active_edit || !text) {
        return;
    }
    M_TextInput_Filter(text, filtered, sizeof(filtered));
    if (filtered[0]) {
        M_TextInput_Insert(&active_ti, filtered);
    }
}

/* Resolve the nearest character boundary in the current edit text. */
static uint32_t UI_EditCursorAt(frameDef_t const *frame, float x) {
    refExport_t *renderer = mi.GetRenderer();
    frameDef_t *text_frame = UI_EditTextFrame(frame);
    rect_t const *rect = UI_LayoutRect(frame);
    cstring_t text = UI_EditText(frame);
    font_t const *font;
    char prefix[512];
    drawText_t measure;
    size_t n, len;
    float const origin = rect ? rect->x + MAX(0.0f, frame->Edit.BorderSize) +
                                frame->Edit.TextOffset.x : 0.0f;
    float const target = MAX(0.0f, x - origin);
    float previous = 0.0f;
    if (!renderer || !text_frame)
        return (uint32_t)strlen(text);
    font = renderer->LoadFont(UI_FontFile(text_frame->Font.Name),
                              UI_FontPixelSize(text_frame->Font.Size));
    if (!font) return (uint32_t)strlen(text);
    len = MIN(strlen(text), sizeof(prefix) - 1);
    measure = MAKE(drawText_t, .font = font, .lineHeight = 1.33f,
                        .halign = FONT_JUSTIFYLEFT);
    for (n = 1; n <= len; ++n) {
        float width;
        memcpy(prefix, text, n);
        prefix[n] = '\0';
        measure.text = prefix;
        width = renderer->GetTextSize(&measure).x;
        if (target < (previous + width) * 0.5f) return (uint32_t)(n - 1);
        previous = width;
    }
    return (uint32_t)len;
}

static void UI_EditPointer(frameDef_t *frame, float x, bool extend) {
    uint32_t cursor;
    if (!frame) return;
    UI_FocusEdit(frame);
    cursor = UI_EditCursorAt(frame, x);
    active_ti.cursor = cursor;
    if (!extend) active_ti.anchor = cursor;
}

static void UI_DrawEditBox(frameDef_t const *frame, rect_t const *rect) {
    refExport_t *renderer = mi.GetRenderer();
    frameDef_t *text_frame = UI_EditTextFrame(frame);
    frameDef_t const *backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
    rect_t text_rect = *rect;

    UI_DrawBackdropWithColor(backdrop, rect, frame->Color);
    if (!text_frame) {
        return;
    }

    if (frame->Edit.BorderSize > 0.0f) {
        text_rect.x += frame->Edit.BorderSize;
        text_rect.y += frame->Edit.BorderSize;
        text_rect.w = MAX(0.0f, text_rect.w - frame->Edit.BorderSize * 2.0f);
        text_rect.h = MAX(0.0f, text_rect.h - frame->Edit.BorderSize * 2.0f);
    }
    text_rect.x += frame->Edit.TextOffset.x;
    text_rect.y += frame->Edit.TextOffset.y;

    /* Paint selected text before glyphs and the caret; the default is grey. */
    if (active_edit == frame && M_TextInput_HasSelection(&active_ti)) {
        cstring_t text = UI_EditText(frame);
        size_t len = strlen(text);
        size_t start = MIN(MIN(active_ti.anchor, active_ti.cursor), len);
        size_t end = MIN(MAX(active_ti.anchor, active_ti.cursor), len);
        char prefix[512], selected[512];
        font_t const *font = renderer->LoadFont(UI_FontFile(text_frame->Font.Name),
                                                UI_FontPixelSize(text_frame->Font.Size));
        if (font && end < sizeof(prefix)) {
            drawText_t measure = MAKE(drawText_t, .font = font, .lineHeight = 1.33f,
                                      .rect = text_rect, .textWidth = text_rect.w,
                                      .halign = FONT_JUSTIFYLEFT,
                                      .valign = text_frame->Font.Justification.Vertical);
            vec2_t before_size, selection_size;
            rect_t highlight = text_rect;
            color32_t color = frame->Edit.HighlightColor.a
                ? frame->Edit.HighlightColor : MAKE(color32_t, 120, 120, 120, 255);
            memcpy(prefix, text, start); prefix[start] = 0;
            memcpy(selected, text + start, end - start); selected[end - start] = 0;
            measure.text = prefix;
            before_size = renderer->GetTextSize(&measure);
            measure.text = selected;
            selection_size = renderer->GetTextSize(&measure);
            highlight.x += before_size.x;
            highlight.w = MIN(selection_size.x, MAX(0.0f, text_rect.w - before_size.x));
            if (highlight.w > 0) renderer->DrawImageEx(&MAKE(drawImage_t,
                .texture = NULL, .shader = SHADER_UI, .alphamode = BLEND_MODE_BLEND,
                .screen = highlight, .uv = MAKE(rect_t, 0, 0, 1, 1),
                .color = color, .flags = DRAW_CLIP, .clip = text_rect));
        }
    }
    UI_DrawText(text_frame, &text_rect);

    if (active_edit == frame && renderer && renderer->DrawText && renderer->GetTextSize) {
        cstring_t text = UI_EditText(frame);
        font_t const *font = renderer->LoadFont(UI_FontFile(text_frame->Font.Name),
                                          UI_FontPixelSize(text_frame->Font.Size));
        color32_t cursor_color = frame->Edit.CursorColor.a ? frame->Edit.CursorColor : COLOR32_WHITE;

        if (!font) {
            return;
        }
        M_DrawTextInputCursor(renderer,
                               &MAKE(drawText_t,
                                     .font = font,
                                     .text = text,
                                     .rect = text_rect,
                                     .textWidth = text_rect.w,
                                      .lineHeight = 1.33f,
                                      .halign = text_frame->Font.Justification.Horizontal,
                                     .valign = text_frame->Font.Justification.Vertical),
                               text,
                               active_ti.cursor,
                               cursor_color);
    }
}

#endif
