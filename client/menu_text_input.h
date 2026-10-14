/*
 * Shared UI text-input engine.
 *
 * Provides a single implementation of text editing logic (insert, backspace,
 * delete, cursor movement, key dispatch) and cursor rendering. Game UI modules
 * own the text buffer, backdrop rendering, and font selection — they override
 * only visuals.
 *
 * Usage:
 *   1. Game allocates or points menuTextInput_t.text at a buffer.
 *   2. On focus change, game sets text/size/max_chars/cursor.
 *   3. Game calls M_TextInput_Key() for key events, M_TextInput_Insert()
 *      for text input.
 *   4. Game renders backdrop and font; calls M_DrawTextInputCursor() for
 *      the caret.
 */
#ifndef menu_text_input_h
#define menu_text_input_h

#include "client/menu.h"
#include "client/keys.h"
#include <stdio.h>
#include <string.h>

#ifndef SDLK_BACKSPACE
#include <SDL2/SDL_keycode.h>
#endif

/* ---- Data ---- */

typedef struct {
    char *text;       /* game-owned buffer pointer (may be NULL) */
    uint32_t size;       /* allocated size of buffer in bytes */
    uint32_t max_chars;  /* max characters (0 = no limit beyond buffer) */
    uint32_t cursor;     /* current cursor position (bytes into text) */
    uint32_t anchor;     /* selection origin; equal to cursor when unselected */
} menuTextInput_t;

/* ---- Key event result ---- */

enum {
    MENU_TEXTINPUT_NONE,
    MENU_TEXTINPUT_CONSUMED,
    MENU_TEXTINPUT_ENTER,
    MENU_TEXTINPUT_ESCAPE,
    MENU_TEXTINPUT_TAB,
};

/* ---- Editing primitives ---- */

/* Selection is the half-open byte range between anchor and cursor. */
static inline bool M_TextInput_HasSelection(menuTextInput_t const *ti) {
    return ti && ti->anchor != ti->cursor;
}

static inline bool M_TextInput_RemoveSelection(menuTextInput_t *ti) {
    size_t start, end, len;
    if (!ti || !ti->text || !M_TextInput_HasSelection(ti)) return false;
    len = strlen(ti->text);
    start = MIN(MIN(ti->anchor, ti->cursor), len);
    end = MIN(MAX(ti->anchor, ti->cursor), len);
    memmove(ti->text + start, ti->text + end, len - end + 1);
    ti->cursor = ti->anchor = (uint32_t)start;
    return true;
}

/* Insert replaces the selection, even when the original buffer is full. */
static inline bool M_TextInput_Insert(menuTextInput_t *ti, cstring_t text) {
    size_t len, add, cursor, max;
    if (!ti || !ti->text || !ti->size || !text || !*text) return false;
    M_TextInput_RemoveSelection(ti);
    len = strlen(ti->text);
    cursor = MIN(ti->cursor, len);
    max = ti->max_chars ? MIN(ti->max_chars, ti->size - 1) : ti->size - 1;
    if (len >= max) return false;
    add = MIN(strlen(text), max - len);
    if (!add) return false;
    memmove(ti->text + cursor + add, ti->text + cursor, len - cursor + 1);
    memcpy(ti->text + cursor, text, add);
    ti->cursor = ti->anchor = (uint32_t)(cursor + add);
    return true;
}

static inline bool M_TextInput_Backspace(menuTextInput_t *ti) {
    size_t len, cursor;
    if (!ti || !ti->text) return false;
    if (M_TextInput_RemoveSelection(ti)) return true;
    len = strlen(ti->text);
    cursor = MIN(ti->cursor, len);
    if (!cursor) return false;
    memmove(ti->text + cursor - 1, ti->text + cursor, len - cursor + 1);
    ti->cursor = ti->anchor = (uint32_t)(cursor - 1);
    return true;
}

static inline bool M_TextInput_Delete(menuTextInput_t *ti) {
    size_t len, cursor;
    if (!ti || !ti->text) return false;
    if (M_TextInput_RemoveSelection(ti)) return true;
    len = strlen(ti->text);
    cursor = MIN(ti->cursor, len);
    if (cursor >= len) return false;
    memmove(ti->text + cursor, ti->text + cursor + 1, len - cursor);
    return true;
}

/* An unmodified arrow collapses an existing selection towards that side. */
static inline void M_TextInput_MoveCursorSelected(menuTextInput_t *ti, int dir, bool extend) {
    size_t len;
    if (!ti || !ti->text) return;
    len = strlen(ti->text);
    ti->cursor = (uint32_t)MIN(ti->cursor, len);
    ti->anchor = (uint32_t)MIN(ti->anchor, len);
    if (!extend && M_TextInput_HasSelection(ti) && (dir == -1 || dir == 1)) {
        ti->cursor = dir < 0 ? MIN(ti->anchor, ti->cursor) : MAX(ti->anchor, ti->cursor);
    } else {
        switch (dir) {
            case -1: if (ti->cursor) --ti->cursor; break;
            case 1: if (ti->cursor < len) ++ti->cursor; break;
            case 0: ti->cursor = 0; break;
            case 2: ti->cursor = (uint32_t)len; break;
        }
    }
    if (!extend) ti->anchor = ti->cursor;
}

static inline void M_TextInput_MoveCursor(menuTextInput_t *ti, int dir) {
    M_TextInput_MoveCursorSelected(ti, dir, false);
}

/* Filter text input: copy only printable characters (>= 0x20) to out.
 * Returns number of bytes written (not counting NUL). */
static inline uint32_t M_TextInput_Filter(cstring_t in, string_t out, uint32_t out_size) {
    uint32_t n = 0;

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

/* Handle a key event. Returns MENU_TEXTINPUT_* result.
 * Games call this from their KeyEvent handler for the focused editbox. */
static inline int M_TextInput_KeyModified(menuTextInput_t *ti, int key, bool shift, bool ctrl) {
    if (!ti || !ti->text)
        return MENU_TEXTINPUT_NONE;

    if (ctrl && (key == 'a' || key == 'A')) {
        ti->anchor = 0;
        ti->cursor = (uint32_t)strlen(ti->text);
        return MENU_TEXTINPUT_CONSUMED;
    }
    switch (key) {
        case SDLK_BACKSPACE:
            return M_TextInput_Backspace(ti) ? MENU_TEXTINPUT_CONSUMED : MENU_TEXTINPUT_NONE;
        case SDLK_DELETE:
            return M_TextInput_Delete(ti) ? MENU_TEXTINPUT_CONSUMED : MENU_TEXTINPUT_NONE;
        case K_LEFTARROW:
            M_TextInput_MoveCursorSelected(ti, -1, shift);
            return MENU_TEXTINPUT_CONSUMED;
        case K_RIGHTARROW:
            M_TextInput_MoveCursorSelected(ti, 1, shift);
            return MENU_TEXTINPUT_CONSUMED;
        case K_HOME:
            M_TextInput_MoveCursorSelected(ti, 0, shift);
            return MENU_TEXTINPUT_CONSUMED;
        case K_END:
            M_TextInput_MoveCursorSelected(ti, 2, shift);
            return MENU_TEXTINPUT_CONSUMED;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return MENU_TEXTINPUT_ENTER;
        case SDLK_ESCAPE:
            return MENU_TEXTINPUT_ESCAPE;
        case SDLK_TAB:
            return MENU_TEXTINPUT_TAB;
        default:
            return MENU_TEXTINPUT_NONE;
    }
}

static inline int M_TextInput_Key(menuTextInput_t *ti, int key) {
    return M_TextInput_KeyModified(ti, key, false, false);
}

/* ---- Cursor rendering ---- */

static inline void M_DrawTextInputCursor(refExport_t *renderer,
                                   drawText_t const *style,
                                   cstring_t text,
                                   uint32_t cursor,
                                   color32_t color) {
    char prefix[1024];
    drawText_t measure, draw;
    vec2_t prefix_size;
    rect_t cursor_rect;
    uint32_t len;

    if (!renderer || !renderer->DrawText || !renderer->GetTextSize || !style || !style->font)
        return;
    text = text ? text : "";
    len = (uint32_t)strlen(text);
    cursor = MIN(cursor, len);
    if (cursor >= sizeof(prefix)) cursor = sizeof(prefix) - 1;
    snprintf(prefix, sizeof(prefix), "%.*s", (int)cursor, text);
    measure = *style;
    measure.text = prefix;
    measure.flags &= ~DRAW_WORD_WRAP;
    prefix_size = renderer->GetTextSize(&measure);
    cursor_rect = style->rect;
    cursor_rect.x += prefix_size.x;
    cursor_rect.w = MAX(0.0f, cursor_rect.w - prefix_size.x);
    draw = *style;
    draw.text = "|";
    draw.rect = cursor_rect;
    draw.color = color.a ? color : COLOR32_WHITE;
    draw.textWidth = cursor_rect.w;
    draw.flags &= ~DRAW_WORD_WRAP;
    draw.halign = FONT_JUSTIFYLEFT;
    renderer->DrawText(&draw);
}

#endif /* menu_text_input_h */
