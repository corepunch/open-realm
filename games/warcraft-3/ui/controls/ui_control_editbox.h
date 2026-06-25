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
        active_edit_cursor = 0;
        return;
    }
    if (active_edit != frame) {
        active_edit = frame;
        active_edit_cursor = (DWORD)strlen(UI_EditText(frame));
    }
}

static void UI_InsertEditText(LPCSTR text) {
    char buffer[256];
    LPCSTR old_text;
    size_t len;
    size_t add_len;
    DWORD max_chars;
    DWORD cursor;

    if (!active_edit || !text || !*text) {
        return;
    }
    old_text = UI_EditText(active_edit);
    len = strlen(old_text);
    add_len = strlen(text);
    max_chars = UI_EditMaxChars(active_edit);
    cursor = MIN(active_edit_cursor, (DWORD)len);
    if (len >= max_chars) {
        return;
    }
    if (len + add_len > max_chars) {
        add_len = max_chars - len;
    }
    if (len + add_len >= sizeof(buffer)) {
        add_len = sizeof(buffer) - len - 1;
    }
    if (add_len == 0) {
        return;
    }

    memcpy(buffer, old_text, cursor);
    memcpy(buffer + cursor, text, add_len);
    memcpy(buffer + cursor + add_len, old_text + cursor, len - cursor + 1);
    UI_SetEditText(active_edit, buffer);
    active_edit_cursor = cursor + (DWORD)add_len;
}

BOOL UI_EditKey(int key) {
    char buffer[256];
    LPCSTR old_text;
    size_t len;
    DWORD cursor;

    if (!active_edit) {
        return false;
    }

    old_text = UI_EditText(active_edit);
    len = strlen(old_text);
    cursor = MIN(active_edit_cursor, (DWORD)len);

    switch (key) {
        case SDLK_BACKSPACE:
            if (cursor > 0) {
                memcpy(buffer, old_text, cursor - 1);
                memcpy(buffer + cursor - 1, old_text + cursor, len - cursor + 1);
                UI_SetEditText(active_edit, buffer);
                active_edit_cursor = cursor - 1;
            }
            return true;
        case SDLK_DELETE:
            if (cursor < len) {
                memcpy(buffer, old_text, cursor);
                memcpy(buffer + cursor, old_text + cursor + 1, len - cursor);
                UI_SetEditText(active_edit, buffer);
            }
            return true;
        case SDLK_LEFT:
            if (cursor > 0) {
                active_edit_cursor = cursor - 1;
            }
            return true;
        case SDLK_RIGHT:
            if (cursor < len) {
                active_edit_cursor = cursor + 1;
            }
            return true;
        case SDLK_HOME:
            active_edit_cursor = 0;
            return true;
        case SDLK_END:
            active_edit_cursor = (DWORD)len;
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return false;
        case SDLK_ESCAPE:
            UI_FocusEdit(NULL);
            return false;
        default:
            break;
    }
    return false;
}

void UI_TextInputLocal(LPCSTR text) {
    char filtered[256];
    DWORD out = 0;

    UI_LayoutTextInput(text);

    if (!active_edit || !text) {
        return;
    }
    for (LPCSTR in = text; *in && out < sizeof(filtered) - 1; in++) {
        unsigned char ch = (unsigned char)*in;
        if (ch >= 32) {
            filtered[out++] = (char)ch;
        }
    }
    filtered[out] = '\0';
    UI_InsertEditText(filtered);
}

static void UI_DrawEditBox(LPCFRAMEDEF frame, LPCRECT rect) {
    LPRENDERER renderer = UI_GetRenderer();
    LPFRAMEDEF text_frame = UI_EditTextFrame(frame);
    LPCFRAMEDEF backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
    RECT text_rect = *rect;

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
        LPCSTR text = UI_EditText(frame);
        DWORD const cursor = MIN(active_edit_cursor, (DWORD)strlen(text));
        char prefix[256];
        LPCFONT font = renderer->LoadFont(UI_FontFile(text_frame->Font.Name),
                                          UI_FontPixelSize(text_frame->Font.Size));
        VECTOR2 prefix_size;
        RECT cursor_rect = text_rect;
        COLOR32 cursor_color = frame->Edit.CursorColor.a ? frame->Edit.CursorColor : COLOR32_WHITE;

        if (!font) {
            return;
        }
        snprintf(prefix, sizeof(prefix), "%.*s", (int)cursor, text);
        prefix_size = renderer->GetTextSize(&MAKE(drawText_t,
                                                  .font = font,
                                                  .text = prefix,
                                                  .rect = text_rect,
                                                  .textWidth = text_rect.w,
                                                  .lineHeight = 1.33f,
                                                  .wordWrap = FALSE,
                                                  .halign = text_frame->Font.Justification.Horizontal,
                                                  .valign = text_frame->Font.Justification.Vertical));
        cursor_rect.x += prefix_size.x;
        cursor_rect.w = MAX(0.0f, cursor_rect.w - prefix_size.x);
        renderer->DrawText(&MAKE(drawText_t,
                                 .font = font,
                                 .text = "|",
                                 .rect = cursor_rect,
                                 .color = cursor_color,
                                 .textWidth = cursor_rect.w,
                                 .lineHeight = 1.33f,
                                 .wordWrap = FALSE,
                                 .halign = FONT_JUSTIFYLEFT,
                                 .valign = text_frame->Font.Justification.Vertical));
    }
}

#endif
