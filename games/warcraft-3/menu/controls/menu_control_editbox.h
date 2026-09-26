#ifndef UI_CONTROL_EDITBOX_H
#define UI_CONTROL_EDITBOX_H

static frameDef_t * UI_CreateEditTextFrame(frameDef_t * frame) {
    frameDef_t * text_frame;
    frameDef_t const * template;

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

static frameDef_t * UI_EditTextFrame(frameDef_t const * frame) {
    frameDef_t * text_frame;

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

static cstring_t UI_EditText(frameDef_t const * frame) {
    frameDef_t * text_frame = UI_EditTextFrame(frame);
    return text_frame && text_frame->Text ? text_frame->Text : "";
}

static void UI_SetEditText(frameDef_t const * frame, cstring_t text) {
    frameDef_t * text_frame = UI_EditTextFrame(frame);

    if (!text_frame) {
        return;
    }
    UI_SetText(text_frame, "%s", text ? text : "");
}

static uint32_t UI_EditMaxChars(frameDef_t const * frame) {
    uint32_t max_chars = frame && frame->Edit.MaxChars ? frame->Edit.MaxChars : 255;
    return MIN(max_chars, sizeof(((frameDef_t *)frame)->TextStorage) - 1);
}

static void UI_FocusEdit(frameDef_t * frame) {
    if (!frame) {
        active_edit = NULL;
        active_ti.text = NULL;
        active_ti.size = 0;
        active_ti.max_chars = 0;
        active_ti.cursor = 0;
        return;
    }
    if (active_edit != frame) {
        frameDef_t * text_frame = UI_EditTextFrame(frame);
        active_edit = frame;
        active_ti.text = text_frame ? text_frame->TextStorage : NULL;
        active_ti.size = text_frame ? sizeof(text_frame->TextStorage) : 0;
        active_ti.max_chars = UI_EditMaxChars(frame);
        active_ti.cursor = (uint32_t)strlen(UI_EditText(frame));
    }
}

bool M_EditKey(int key) {
    int result;

    if (!active_edit) {
        return false;
    }

    result = M_TextInput_Key(&active_ti, key);
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

static void UI_DrawEditBox(frameDef_t const * frame, rect_t const * rect) {
    refExport_t * renderer = mi.GetRenderer();
    frameDef_t * text_frame = UI_EditTextFrame(frame);
    frameDef_t const * backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
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

    UI_DrawText(text_frame, &text_rect);

    if (active_edit == frame && renderer && renderer->DrawText && renderer->GetTextSize) {
        cstring_t text = UI_EditText(frame);
        font_t const * font = renderer->LoadFont(UI_FontFile(text_frame->Font.Name),
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
