#ifndef UI_CONTROL_EDITBOX_H
#define UI_CONTROL_EDITBOX_H

static LPFRAMEDEF UI_CreateEditTextFrame(LPFRAMEDEF frame) {
    LPFRAMEDEF text_frame;
    LPCFRAMEDEF template;

    if (!frame) {
        return NULL;
    }

    text_frame = UI_Spawn(FT_TEXT, frame);
    if (!text_frame) {
        return NULL;
    }

    snprintf(text_frame->Name,
             sizeof(text_frame->Name),
             "%sText",
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

static LPFRAMEDEF UI_EditTextFrame(LPCFRAMEDEF frame) {
    LPFRAMEDEF text_frame;

    if (!frame) {
        return NULL;
    }
    text_frame = UI_FindChildFrame((LPFRAMEDEF)frame, frame->Edit.TextFrame);
    if (!text_frame && frame->Edit.TextFrame[0]) {
        text_frame = UI_FindFrameNear(frame, frame->Edit.TextFrame);
    }
    if (!text_frame) {
        text_frame = UI_CreateEditTextFrame((LPFRAMEDEF)frame);
    }
    return text_frame;
}

static LPCSTR UI_EditText(LPCFRAMEDEF frame) {
    LPFRAMEDEF text_frame = UI_EditTextFrame(frame);
    return text_frame && text_frame->Text ? text_frame->Text : "";
}

static void UI_SetEditText(LPCFRAMEDEF frame, LPCSTR text) {
    LPFRAMEDEF text_frame = UI_EditTextFrame(frame);

    if (!text_frame) {
        return;
    }
    UI_SetText(text_frame, "%s", text ? text : "");
}

static DWORD UI_EditMaxChars(LPCFRAMEDEF frame) {
    DWORD max_chars = frame && frame->Edit.MaxChars ? frame->Edit.MaxChars : 255;
    return MIN(max_chars, sizeof(((LPFRAMEDEF)frame)->TextStorage) - 1);
}

static void UI_FocusEdit(LPFRAMEDEF frame) {
    if (!frame) {
        active_edit = NULL;
        active_ti.text = NULL;
        active_ti.size = 0;
        active_ti.max_chars = 0;
        active_ti.cursor = 0;
        return;
    }
    if (active_edit != frame) {
        LPFRAMEDEF text_frame = UI_EditTextFrame(frame);
        active_edit = frame;
        active_ti.text = text_frame ? text_frame->TextStorage : NULL;
        active_ti.size = text_frame ? sizeof(text_frame->TextStorage) : 0;
        active_ti.max_chars = UI_EditMaxChars(frame);
        active_ti.cursor = (DWORD)strlen(UI_EditText(frame));
    }
}

BOOL UI_EditKey(int key) {
    int result;

    if (!active_edit) {
        return false;
    }

    result = UI_TextInput_Key(&active_ti, key);
    switch (result) {
        case UI_TEXTINPUT_CONSUMED:
            return true;
        case UI_TEXTINPUT_ENTER:
            return false;
        case UI_TEXTINPUT_ESCAPE:
            UI_FocusEdit(NULL);
            return false;
        default:
            return false;
    }
}

void UI_TextInputLocal(LPCSTR text) {
    char filtered[256];

    if (!active_edit || !text) {
        return;
    }
    UI_TextInput_Filter(text, filtered, sizeof(filtered));
    if (filtered[0]) {
        UI_TextInput_Insert(&active_ti, filtered);
    }
}

static void UI_DrawEditBox(LPCFRAMEDEF frame, LPCRECT rect) {
    LPRENDERER renderer = UI_GetRenderer();
    LPFRAMEDEF text_frame = UI_EditTextFrame(frame);
    LPCFRAMEDEF backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
    RECT text_rect = *rect;

    UI_DrawBackdropWithColor(backdrop, rect, frame->base.color);
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
        LPCSTR text = UI_EditText(frame);
        LPCFONT font = renderer->LoadFont(UI_FontFile(text_frame->Font.Name),
                                          UI_FontPixelSize(text_frame->Font.Size));
        COLOR32 cursor_color = frame->Edit.CursorColor.a ? frame->Edit.CursorColor : COLOR32_WHITE;

        if (!font) {
            return;
        }
        UI_DrawTextInputCursor(renderer,
                               &MAKE(drawText_t,
                                     .font = font,
                                     .text = text,
                                     .rect = text_rect,
                                     .textWidth = text_rect.w,
                                     .lineHeight = 1.33f,
                                     .wordWrap = FALSE,
                                     .halign = text_frame->Font.Justification.Horizontal,
                                     .valign = text_frame->Font.Justification.Vertical),
                               text,
                               active_ti.cursor,
                               cursor_color);
    }
}

#endif
