#ifndef WC3_CURSOR_H
#define WC3_CURSOR_H

/* Server-owned interaction state. These are NOT retail cursor mode IDs. */
typedef enum {
    WC3_POINTER_IDLE,
    WC3_POINTER_TARGETING,
    WC3_POINTER_HOLDING,
    WC3_POINTER_SIGNALING,
} wc3PointerInteraction_t;

/* Resolved retail cursor-owner modes (game.dll 1.27b), NOT sequence indexes.
 * WC3 presentation combines interaction, hover relationship and edge scrolling. */
typedef enum {
    WC3_CURSOR_NORMAL = 0,
    WC3_CURSOR_SELECT_YELLOW = 1,
    WC3_CURSOR_SELECT_RED = 2,
    WC3_CURSOR_SELECT_GREEN = 3,
    WC3_CURSOR_TARGET = 4,
    WC3_CURSOR_TARGET_SELECT_YELLOW = 5,
    WC3_CURSOR_TARGET_SELECT_RED = 6,
    WC3_CURSOR_TARGET_SELECT_GREEN = 7,
    WC3_CURSOR_SIGNAL = 8,
    WC3_CURSOR_HOLD_ITEM = 9,
    WC3_CURSOR_SCROLL_LEFT = 10,
    WC3_CURSOR_SCROLL_RIGHT = 11,
    WC3_CURSOR_SCROLL_UP = 12,
    WC3_CURSOR_SCROLL_DOWN = 13,
    WC3_CURSOR_SCROLL_UP_LEFT = 14,
    WC3_CURSOR_SCROLL_UP_RIGHT = 15,
    WC3_CURSOR_SCROLL_DOWN_LEFT = 16,
    WC3_CURSOR_SCROLL_DOWN_RIGHT = 17,
    WC3_CURSOR_COUNT,
} wc3CursorMode_t;

#endif
