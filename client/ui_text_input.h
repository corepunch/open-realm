/*
 * Shared UI text-input engine.
 *
 * Provides a single implementation of text editing logic (insert, backspace,
 * delete, cursor movement, key dispatch) and cursor rendering. Game UI modules
 * own the text buffer, backdrop rendering, and font selection — they override
 * only visuals.
 *
 * Usage:
 *   1. Game allocates or points uiTextInput_t.text at a buffer.
 *   2. On focus change, game sets text/size/max_chars/cursor.
 *   3. Game calls UI_TextInput_Key() for key events, UI_TextInput_Insert()
 *      for text input.
 *   4. Game renders backdrop and font; calls UI_DrawTextInputCursor() for
 *      the caret.
 */
#ifndef ui_text_input_h
#define ui_text_input_h

#include "client/ui.h"
#include <stdio.h>
#include <string.h>

#ifndef SDLK_BACKSPACE
#include <SDL2/SDL_keycode.h>
#endif

/* ---- Data ---- */

typedef struct {
    char *text;       /* game-owned buffer pointer (may be NULL) */
    DWORD size;       /* allocated size of buffer in bytes */
    DWORD max_chars;  /* max characters (0 = no limit beyond buffer) */
    DWORD cursor;     /* current cursor position (bytes into text) */
} uiTextInput_t;

/* ---- Key event result ---- */

enum {
    UI_TEXTINPUT_NONE,
    UI_TEXTINPUT_CONSUMED,
    UI_TEXTINPUT_ENTER,
    UI_TEXTINPUT_ESCAPE,
    UI_TEXTINPUT_TAB,
};

/* ---- Editing primitives ---- */

/* Insert text at cursor position. Returns true if text was modified. */
static BOOL UI_TextInput_Insert(uiTextInput_t *ti, LPCSTR text) {
    char buf[512];
    LPCSTR old;
    size_t old_len, add_len, cursor;
    DWORD max;

    if (!ti || !ti->text || !text || !*text)
        return false;
    old = ti->text;
    old_len = strlen(old);
    add_len = strlen(text);
    cursor = MIN((size_t)ti->cursor, old_len);
    max = ti->max_chars ? ti->max_chars : (ti->size ? ti->size - 1 : 255);
    if (old_len >= max)
        return false;
    if (old_len + add_len > max)
        add_len = max - old_len;
    if (add_len == 0)
        return false;
    if (old_len + add_len + 1 > sizeof(buf))
        add_len = sizeof(buf) - old_len - 1;
    if (add_len == 0)
        return false;
    memcpy(buf, old, cursor);
    memcpy(buf + cursor, text, add_len);
    memcpy(buf + cursor + add_len, old + cursor, old_len - cursor + 1);
    memcpy(ti->text, buf, old_len + add_len + 1);
    ti->cursor = (DWORD)(cursor + add_len);
    return true;
}

/* Backspace: remove character before cursor. Returns true if modified. */
static BOOL UI_TextInput_Backspace(uiTextInput_t *ti) {
    char *t;
    size_t len, cursor;

    if (!ti || !ti->text)
        return false;
    t = ti->text;
    len = strlen(t);
    cursor = MIN((size_t)ti->cursor, len);
    if (cursor == 0)
        return false;
    memmove(t + cursor - 1, t + cursor, len - cursor + 1);
    ti->cursor = (DWORD)(cursor - 1);
    return true;
}

/* Delete: remove character at cursor. Returns true if modified. */
static BOOL UI_TextInput_Delete(uiTextInput_t *ti) {
    char *t;
    size_t len, cursor;

    if (!ti || !ti->text)
        return false;
    t = ti->text;
    len = strlen(t);
    cursor = MIN((size_t)ti->cursor, len);
    if (cursor >= len)
        return false;
    memmove(t + cursor, t + cursor + 1, len - cursor);
    return true;
}

/* Move cursor. dir: -1=left, +1=right, 0=home, 1=end. */
static void UI_TextInput_MoveCursor(uiTextInput_t *ti, int dir) {
    if (!ti || !ti->text)
        return;
    switch (dir) {
        case -1: if (ti->cursor > 0) ti->cursor--; break;
        case  1: { size_t len = strlen(ti->text); if (ti->cursor < len) ti->cursor++; break; }
        case  0: ti->cursor = 0; break;
        case  2: ti->cursor = (DWORD)strlen(ti->text); break;
    }
}

/* Filter text input: copy only printable characters (>= 0x20) to out.
 * Returns number of bytes written (not counting NUL). */
static DWORD UI_TextInput_Filter(LPCSTR in, LPSTR out, DWORD out_size) {
    DWORD n = 0;

    if (!in || !out || out_size == 0)
        return 0;
    for (; *in && n < out_size - 1; in++) {
        unsigned char ch = (unsigned char)*in;
        if (ch >= 0x20)
            out[n++] = (char)ch;
    }
    out[n] = '\0';
    return n;
}

/* ---- Key event dispatch ---- */

/* Handle a key event. Returns UI_TEXTINPUT_* result.
 * Games call this from their KeyEvent handler for the focused editbox. */
static int UI_TextInput_Key(uiTextInput_t *ti, int key) {
    if (!ti || !ti->text)
        return UI_TEXTINPUT_NONE;

    switch (key) {
        case SDLK_BACKSPACE:
            return UI_TextInput_Backspace(ti) ? UI_TEXTINPUT_CONSUMED : UI_TEXTINPUT_NONE;
        case SDLK_DELETE:
            return UI_TextInput_Delete(ti) ? UI_TEXTINPUT_CONSUMED : UI_TEXTINPUT_NONE;
        case SDLK_LEFT:
            UI_TextInput_MoveCursor(ti, -1);
            return UI_TEXTINPUT_CONSUMED;
        case SDLK_RIGHT:
            UI_TextInput_MoveCursor(ti, 1);
            return UI_TEXTINPUT_CONSUMED;
        case SDLK_HOME:
            UI_TextInput_MoveCursor(ti, 0);
            return UI_TEXTINPUT_CONSUMED;
        case SDLK_END:
            UI_TextInput_MoveCursor(ti, 2);
            return UI_TEXTINPUT_CONSUMED;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return UI_TEXTINPUT_ENTER;
        case SDLK_ESCAPE:
            return UI_TEXTINPUT_ESCAPE;
        case SDLK_TAB:
            return UI_TEXTINPUT_TAB;
        default:
            return UI_TEXTINPUT_NONE;
    }
}

/* ---- Cursor rendering ---- */

static void UI_DrawTextInputCursor(LPRENDERER renderer,
                                   LPCDRAWTEXT style,
                                   LPCSTR text,
                                   DWORD cursor,
                                   COLOR32 color) {
    char prefix[1024];
    drawText_t measure, draw;
    VECTOR2 prefix_size;
    RECT cursor_rect;
    DWORD len;

    if (!renderer || !renderer->DrawText || !renderer->GetTextSize || !style || !style->font)
        return;
    text = text ? text : "";
    len = (DWORD)strlen(text);
    cursor = MIN(cursor, len);
    if (cursor >= sizeof(prefix)) cursor = sizeof(prefix) - 1;
    snprintf(prefix, sizeof(prefix), "%.*s", (int)cursor, text);
    measure = *style;
    measure.text = prefix;
    measure.wordWrap = false;
    prefix_size = renderer->GetTextSize(&measure);
    cursor_rect = style->rect;
    cursor_rect.x += prefix_size.x;
    cursor_rect.w = MAX(0.0f, cursor_rect.w - prefix_size.x);
    draw = *style;
    draw.text = "|";
    draw.rect = cursor_rect;
    draw.color = color.a ? color : COLOR32_WHITE;
    draw.textWidth = cursor_rect.w;
    draw.wordWrap = false;
    draw.halign = FONT_JUSTIFYLEFT;
    renderer->DrawText(&draw);
}

#endif /* ui_text_input_h */
