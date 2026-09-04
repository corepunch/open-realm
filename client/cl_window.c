#include "client.h"
#include <ctype.h>

#define MAX_WINDOW_SCROLL_VALUES 32

typedef struct {
    DWORD frame;
    FLOAT value;
} clientWindowValue_t;

typedef struct clientWindow_s {
    DWORD id, class_id, flags;
    HANDLE layout;
    VECTOR2 offset;
    BOOL debug_draw_logged;
    clientWindowValue_t scroll_values[MAX_WINDOW_SCROLL_VALUES];
    DWORD num_scroll_values;
    struct clientWindow_s *prev, *next;
} clientWindow_t;

static struct {
    clientWindow_t *first, *last, *focus, *drag, *scroll_drag;
    DWORD scroll_drag_frame;
    VECTOR2 drag_point, drag_offset;
    BOOL modal_paused;
} cl_windows;

static RECT CL_WindowRoot(clientWindow_t const *window);

static BOOL CL_WindowDebugEnabled(void) {
    return Cvar_Integer("ui_window_debug", 0) != 0;
}

static LPCSTR CL_WindowImageName(RESOURCE image) {
    if (!image) return "<none>";
    if (image >= MAX_IMAGES) return "<out-of-range>";
    return cl.configstrings[CS_IMAGES + image];
}

static void CL_WindowDebugLayout(clientWindow_t const *window) {
    RECT root;
    LPCUIFRAME root_frame;

    if (!window || !CL_WindowDebugEnabled()) return;
    root = CL_WindowRoot(window);
    SCR_WindowPrepare(window->layout, &root);
    root_frame = SCR_Frame(1);
    fprintf(stderr,
            "UI_WINDOW_DEBUG window=%08x class=%08x flags=%08x frames=%u "
            "offset=(%.4f,%.4f) root=(%.4f,%.4f %.4fx%.4f)\n",
            (unsigned)window->id, (unsigned)window->class_id, (unsigned)window->flags,
            (unsigned)SCR_NumFrames(), window->offset.x, window->offset.y,
            root.x, root.y, root.w, root.h);
    if (root_frame) {
        RECT const *r = SCR_LayoutRect(root_frame);
        fprintf(stderr,
                "UI_WINDOW_DEBUG window=%08x root_frame number=%u parent=%u type=%u "
                "rect=(%.4f,%.4f %.4fx%.4f) color=(%u,%u,%u,%u) buffer=%u\n",
                (unsigned)window->id, (unsigned)root_frame->number, (unsigned)root_frame->parent,
                (unsigned)root_frame->flags.type, r->x, r->y, r->w, r->h,
                (unsigned)root_frame->color.r, (unsigned)root_frame->color.g,
                (unsigned)root_frame->color.b, (unsigned)root_frame->color.a,
                (unsigned)root_frame->buffer.size);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME frame = SCR_Frame(i);
        uiBackdrop_t const *bd;
        RECT const *r;
        LPCSTR bg_name, edge_name;
        if (!frame || frame->flags.type != FT_BACKDROP) continue;
        r = SCR_LayoutRect(frame);
        if (frame->buffer.size < sizeof(uiBackdrop_t) || !frame->buffer.data) {
            fprintf(stderr,
                    "UI_WINDOW_DEBUG window=%08x backdrop frame=%u parent=%u "
                    "rect=(%.4f,%.4f %.4fx%.4f) INVALID_BUFFER size=%u\n",
                    (unsigned)window->id, (unsigned)frame->number, (unsigned)frame->parent,
                    r->x, r->y, r->w, r->h, (unsigned)frame->buffer.size);
            continue;
        }
        bd = frame->buffer.data;
        bg_name = CL_WindowImageName(bd->Background);
        edge_name = CL_WindowImageName(bd->EdgeFile);
        fprintf(stderr,
                "UI_WINDOW_DEBUG window=%08x backdrop frame=%u parent=%u "
                "rect=(%.4f,%.4f %.4fx%.4f) color=(%u,%u,%u,%u) "
                "bg=%u path=\"%s\" loaded=%p edge=%u path=\"%s\" loaded=%p "
                "cornerFlags=%d corner=%.4f bgSize=%.4f insets=(%.4f,%.4f,%.4f,%.4f) "
                "tile=%u blend=%u mirrored=%u\n",
                (unsigned)window->id, (unsigned)frame->number, (unsigned)frame->parent,
                r->x, r->y, r->w, r->h,
                (unsigned)frame->color.r, (unsigned)frame->color.g,
                (unsigned)frame->color.b, (unsigned)frame->color.a,
                (unsigned)bd->Background, bg_name ? bg_name : "",
                bd->Background < MAX_IMAGES ? (void *)cl.pics[bd->Background] : NULL,
                (unsigned)bd->EdgeFile, edge_name ? edge_name : "",
                bd->EdgeFile < MAX_IMAGES ? (void *)cl.pics[bd->EdgeFile] : NULL,
                (int)bd->CornerFlags, bd->CornerSize, bd->BackgroundSize,
                bd->BackgroundInsets[0], bd->BackgroundInsets[1],
                bd->BackgroundInsets[2], bd->BackgroundInsets[3],
                (unsigned)bd->TileBackground, (unsigned)bd->BlendAll, (unsigned)bd->Mirrored);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME frame = SCR_Frame(i);
        RECT const *r;
        if (!frame) continue;
        r = SCR_LayoutRect(frame);
        if (frame->flags.type == FT_SCROLLBAR) {
            fprintf(stderr,
                    "UI_WINDOW_DEBUG window=%08x control=scrollbar frame=%u parent=%u "
                    "rect=(%.4f,%.4f %.4fx%.4f) value=%.3f payload=%u",
                    (unsigned)window->id, (unsigned)frame->number, (unsigned)frame->parent,
                    r->x, r->y, r->w, r->h, frame->value, (unsigned)frame->buffer.size);
            if (frame->buffer.data && frame->buffer.size >= sizeof(uiScrollBar_t)) {
                uiScrollBar_t const *sb = frame->buffer.data;
                fprintf(stderr,
                        " track=%u path=\"%s\" inc=%u path=\"%s\" "
                        "dec=%u path=\"%s\" thumb=%u path=\"%s\"",
                        (unsigned)sb->background.Background, CL_WindowImageName(sb->background.Background),
                        (unsigned)sb->incButton.Background, CL_WindowImageName(sb->incButton.Background),
                        (unsigned)sb->decButton.Background, CL_WindowImageName(sb->decButton.Background),
                        (unsigned)sb->thumbButton.Background, CL_WindowImageName(sb->thumbButton.Background));
            }
            fputc('\n', stderr);
        } else if ((frame->flags.type == FT_CHECKBOX || frame->flags.type == FT_GLUECHECKBOX ||
             frame->flags.type == FT_SIMPLECHECKBOX) &&
            frame->buffer.data && frame->buffer.size >= sizeof(uiCheckBox_t)) {
            uiCheckBox_t const *cb = frame->buffer.data;
            fprintf(stderr,
                    "UI_WINDOW_DEBUG window=%08x control=checkbox frame=%u parent=%u "
                    "rect=(%.4f,%.4f %.4fx%.4f) checked=%.0f "
                    "normal=%u path=\"%s\" loaded=%p checkedArt=%u path=\"%s\" loaded=%p "
                    "hover=%u path=\"%s\" loaded=%p\n",
                    (unsigned)window->id, (unsigned)frame->number, (unsigned)frame->parent,
                    r->x, r->y, r->w, r->h, frame->value,
                    (unsigned)cb->normal.Background, CL_WindowImageName(cb->normal.Background),
                    cb->normal.Background < MAX_IMAGES ? (void *)cl.pics[cb->normal.Background] : NULL,
                    (unsigned)cb->checked.alphaFile, CL_WindowImageName(cb->checked.alphaFile),
                    cb->checked.alphaFile < MAX_IMAGES ? (void *)cl.pics[cb->checked.alphaFile] : NULL,
                    (unsigned)cb->mouseOver.alphaFile, CL_WindowImageName(cb->mouseOver.alphaFile),
                    cb->mouseOver.alphaFile < MAX_IMAGES ? (void *)cl.pics[cb->mouseOver.alphaFile] : NULL);
        } else if ((frame->flags.type == FT_BUTTON || frame->flags.type == FT_TEXTBUTTON ||
                    frame->flags.type == FT_POPUPMENU || frame->flags.type == FT_GLUEPOPUPMENU ||
                    frame->flags.type == FT_GLUETEXTBUTTON || frame->flags.type == FT_GLUEBUTTON) &&
                   frame->buffer.data && frame->buffer.size >= sizeof(uiGlueTextButton_t)) {
            uiGlueTextButton_t const *button = frame->buffer.data;
            fprintf(stderr,
                    "UI_WINDOW_DEBUG window=%08x control=button frame=%u parent=%u "
                    "rect=(%.4f,%.4f %.4fx%.4f) normal=%u path=\"%s\" loaded=%p "
                    "pushed=%u path=\"%s\" loaded=%p highlight=%u path=\"%s\" loaded=%p\n",
                    (unsigned)window->id, (unsigned)frame->number, (unsigned)frame->parent,
                    r->x, r->y, r->w, r->h,
                    (unsigned)button->normal.Background, CL_WindowImageName(button->normal.Background),
                    button->normal.Background < MAX_IMAGES ? (void *)cl.pics[button->normal.Background] : NULL,
                    (unsigned)button->pushed.Background, CL_WindowImageName(button->pushed.Background),
                    button->pushed.Background < MAX_IMAGES ? (void *)cl.pics[button->pushed.Background] : NULL,
                    (unsigned)button->highlight.alphaFile, CL_WindowImageName(button->highlight.alphaFile),
                    button->highlight.alphaFile < MAX_IMAGES ? (void *)cl.pics[button->highlight.alphaFile] : NULL);
        }
    }
}

static void CL_WindowUnlink(clientWindow_t *window) {
    if (window->prev) window->prev->next = window->next;
    else cl_windows.first = window->next;
    if (window->next) window->next->prev = window->prev;
    else cl_windows.last = window->prev;
    window->prev = window->next = NULL;
}

/* The tail is frontmost; moving focus there makes draw and input order agree. */
static void CL_WindowFocus(clientWindow_t *window) {
    if (!window) { cl_windows.focus = NULL; return; }
    if (window != cl_windows.last) {
        CL_WindowUnlink(window);
        window->prev = cl_windows.last;
        if (cl_windows.last) cl_windows.last->next = window;
        else cl_windows.first = window;
        cl_windows.last = window;
    }
    cl_windows.focus = window;
}

static clientWindow_t *CL_WindowById(DWORD id) {
    FOR_EACH_LIST(clientWindow_t, window, cl_windows.first)
        if (window->id == id) return window;
    return NULL;
}

static clientWindow_t *CL_WindowByClass(DWORD class_id) {
    FOR_EACH_LIST(clientWindow_t, window, cl_windows.first)
        if (window->class_id == class_id) return window;
    return NULL;
}

static clientWindow_t *CL_WindowModal(void) {
    for (clientWindow_t *window = cl_windows.last; window; window = window->prev)
        if (window->flags & UI_WINDOW_MODAL) return window;
    return NULL;
}

static BOOL CL_WindowPauseOwnerPresent(void) {
    for (clientWindow_t *window = cl_windows.last; window; window = window->prev) {
        if ((window->flags & UI_WINDOW_MODAL) && !(window->flags & UI_WINDOW_NO_PAUSE)) return true;
    }
    return false;
}

/* Quake II's menu stack owns the single-player pause cvar. Keep the server
 * request synchronized with pause-owning modal-list presence so a modal such
 * as WC3's Allies dialog can capture gameplay input without freezing time. */
static void CL_WindowSyncPause(void) {
    BOOL paused = CL_WindowPauseOwnerPresent();
    char command[16];
    if (paused == cl_windows.modal_paused) return;
    cl_windows.modal_paused = paused;
    snprintf(command, sizeof(command), "pause %u", (unsigned)paused);
    Cmd_ForwardToServer(command);
}

static RECT CL_WindowRoot(clientWindow_t const *window) {
    return MAKE(RECT, window->offset.x, window->offset.y, SCR_UICanvasWidth(), UI_BASE_HEIGHT);
}

static void CL_WindowRememberScroll(clientWindow_t *window, DWORD frame_number, FLOAT value) {
    if (!window || !frame_number) return;
    value = MIN(1.0f, MAX(0.0f, value));
    FOR_LOOP(i, window->num_scroll_values) {
        if (window->scroll_values[i].frame == frame_number) {
            window->scroll_values[i].value = value;
            return;
        }
    }
    if (window->num_scroll_values >= MAX_WINDOW_SCROLL_VALUES) return;
    window->scroll_values[window->num_scroll_values++] = (clientWindowValue_t){
        .frame = frame_number, .value = value,
    };
}

static void CL_WindowPrepareState(clientWindow_t *window, LPCRECT root) {
    if (!window) return;
    SCR_WindowPrepare(window->layout, root);
    FOR_LOOP(i, window->num_scroll_values) {
        LPUIFRAME frame = SCR_Frame(window->scroll_values[i].frame);
        if (frame) frame->value = window->scroll_values[i].value;
    }
}

static LPUIFRAME CL_WindowScrollOwner(LPUIFRAME frame) {
    LPUIFRAME parent;
    if (!frame) return NULL;
    if (frame->flags.type == FT_TEXTAREA) return frame;
    if (frame->flags.type != FT_SCROLLBAR || frame->parent >= SCR_NumFrames()) return NULL;
    parent = SCR_Frame(frame->parent);
    return parent && parent->flags.type == FT_TEXTAREA ? parent : frame;
}

static BOOL CL_WindowSetScroll(clientWindow_t *window, LPUIFRAME owner, FLOAT value) {
    if (!window || !owner) return false;
    if (owner->flags.type == FT_TEXTAREA && SCR_LayoutTextAreaMaxScroll(owner) <= 0.0f) {
        value = 0.0f;
    }
    value = MIN(1.0f, MAX(0.0f, value));
    owner->value = value;
    CL_WindowRememberScroll(window, owner->number, value);
    /* TextArea scrollbars mirror their parent's local viewport state. */
    FOR_LOOP(i, SCR_NumFrames()) {
        LPUIFRAME child = SCR_Frame(i);
        if (child && child->parent == owner->number && child->flags.type == FT_SCROLLBAR)
            child->value = value;
    }
    return true;
}

static LPUIFRAME CL_WindowFrameAtType(LPCVECTOR2 point, FRAMETYPE type) {
    for (DWORD i = SCR_NumFrames(); i > 0; i--) {
        LPUIFRAME frame = SCR_Frame(i - 1);
        if (frame && frame->flags.type == type && Rect_contains(SCR_LayoutRect(frame), point))
            return frame;
    }
    return NULL;
}

static BOOL CL_WindowScrollWheel(clientWindow_t *window, LPCVECTOR2 point, int wheel_y) {
    LPUIFRAME hit, owner;
    if (!window || !point || !wheel_y) return false;
    hit = CL_WindowFrameAtType(point, FT_SCROLLBAR);
    if (!hit) hit = CL_WindowFrameAtType(point, FT_TEXTAREA);
    owner = CL_WindowScrollOwner(hit);
    if (!owner) return false;
    if (owner->flags.type == FT_TEXTAREA && SCR_LayoutTextAreaMaxScroll(owner) <= 0.0f)
        return true;
    return CL_WindowSetScroll(window, owner, owner->value - wheel_y * 0.1f);
}

static BOOL CL_WindowScrollBarSetFromPoint(clientWindow_t *window, LPUIFRAME scrollbar,
                                           LPCVECTOR2 point, BOOL drag_track) {
    LPUIFRAME owner = CL_WindowScrollOwner(scrollbar);
    RECT const *screen;
    RECT track;
    FLOAT bh, th, value;

    if (!window || !scrollbar || !owner || !point) return false;
    if (owner->flags.type == FT_TEXTAREA && SCR_LayoutTextAreaMaxScroll(owner) <= 0.0f)
        return true;
    screen = SCR_LayoutRect(scrollbar);
    if (!screen || screen->w <= 0.0f || screen->h <= 0.0f) return true;
    bh = MIN(screen->w * UI_PIXEL_ASPECT, screen->h * 0.5f);
    track = MAKE(RECT, screen->x, screen->y + bh, screen->w, screen->h - bh * 2.0f);
    value = owner->value;

    if (!drag_track && point->y < track.y) {
        value -= 0.1f;
    } else if (!drag_track && point->y >= track.y + track.h) {
        value += 0.1f;
    } else if (track.h > 0.0f) {
        BOOL compact = scrollbar->buffer.size == sizeof(uiScrollBarImage_t);
        th = MIN(compact ? bh : MIN(bh, 0.010f), track.h);
        value = track.h > th
            ? (point->y - track.y - th * 0.5f) / (track.h - th)
            : 0.0f;
    }
    return CL_WindowSetScroll(window, owner, value);
}

static BOOL CL_WindowContains(clientWindow_t *window, LPCVECTOR2 point) {
    RECT root = CL_WindowRoot(window);
    CL_WindowPrepareState(window, &root);
    LPCUIFRAME frame = SCR_Frame(1);
    return frame && Rect_contains(SCR_LayoutRect(frame), point);
}

static LPCUIFRAME CL_WindowClickableAt(clientWindow_t *window, LPCVECTOR2 point) {
    RECT root = CL_WindowRoot(window);
    CL_WindowPrepareState(window, &root);
    for (DWORD i = SCR_NumFrames(); i > 0; i--) {
        LPCUIFRAME frame = SCR_Frame(i - 1);
        if (SCR_LayoutFrameHasClickCommand(frame) && Rect_contains(SCR_LayoutRect(frame), point)) return frame;
    }
    return NULL;
}

/* Consume client-owned button actions locally; ordinary layout actions remain server commands. */
static void CL_WindowActivateFrame(clientWindow_t *window, LPCUIFRAME frame) {
    size_t const close_command_len = sizeof(UI_WINDOW_CLOSE_COMMAND_PREFIX) - 1;
    if (!frame) return;
    if (!strcmp(frame->onclick, UI_WINDOW_CLOSE_ACTION) ||
        !strcmp(frame->onclick, UI_WINDOW_CLOSE_NOTIFY_ACTION)) {
        CL_WindowClose(window->id);
    } else if (!strncmp(frame->onclick, UI_WINDOW_CLOSE_COMMAND_PREFIX, close_command_len)) {
        LPCSTR command = frame->onclick + close_command_len;
        if (*command) Cmd_ForwardToServer(command);
        CL_WindowClose(window->id);
    } else if (!strcmp(frame->onclick, UI_WINDOW_DISCONNECT_ACTION)) {
        /* Server-authored menus may offer a leave action, but the local client
         * owns session teardown and only performs it after explicit input. */
        CL_Disconnect("Left game.", false);
    } else if (!strcmp(frame->onclick, UI_WINDOW_QUIT_ACTION)) {
        /* Defer normal application shutdown until input dispatch returns. */
        Cbuf_AddText("quit\n");
    } else {
        SCR_LayoutSendFrameCommand(frame);
    }
}

void CL_WindowOpen(uiWindowDef_t const *def, HANDLE layout) {
    clientWindow_t *window = CL_WindowById(def->id);
    if (!window && (def->flags & UI_WINDOW_UNIQUE)) window = CL_WindowByClass(def->class_id);
    if (!window) {
        window = MemAlloc(sizeof(*window));
        memset(window, 0, sizeof(*window));
        window->prev = cl_windows.last;
        if (cl_windows.last) cl_windows.last->next = window;
        else cl_windows.first = window;
        cl_windows.last = window;
    } else SAFE_DELETE(window->layout, MemFree);
    window->id = def->id; window->class_id = def->class_id; window->flags = def->flags; window->layout = layout;
    window->debug_draw_logged = false;
    CL_WindowFocus(window);
    CL_WindowDebugLayout(window);
    CL_WindowSyncPause();
}

void CL_WindowClose(DWORD id) {
    clientWindow_t *window = CL_WindowById(id);
    if (!window) return;
    if (cl_windows.focus == window) cl_windows.focus = NULL;
    if (cl_windows.drag == window) cl_windows.drag = NULL;
    if (cl_windows.scroll_drag == window) {
        cl_windows.scroll_drag = NULL;
        cl_windows.scroll_drag_frame = 0;
    }
    CL_WindowUnlink(window);
    SAFE_DELETE(window->layout, MemFree);
    MemFree(window);
    if (!cl_windows.focus) cl_windows.focus = cl_windows.last;
    CL_WindowSyncPause();
}

void CL_WindowClear(void) {
    while (cl_windows.first) CL_WindowClose(cl_windows.first->id);
    memset(&cl_windows, 0, sizeof(cl_windows));
}

BOOL CL_WindowModalActive(void) { return CL_WindowModal() != NULL; }

void CL_WindowDraw(void) {
    FOR_EACH_LIST(clientWindow_t, window, cl_windows.first) {
        RECT root = CL_WindowRoot(window);
        CL_WindowPrepareState(window, &root);
        if (!window->debug_draw_logged && CL_WindowDebugEnabled()) {
            fprintf(stderr,
                    "UI_WINDOW_DEBUG draw window=%08x frames=%u root=(%.4f,%.4f %.4fx%.4f)\n",
                    (unsigned)window->id, (unsigned)SCR_NumFrames(),
                    root.x, root.y, root.w, root.h);
            window->debug_draw_logged = true;
        }
        SCR_LayoutDrawOverlay(window->layout);
    }
}

BOOL CL_WindowMouseEvent(menuMouseEvent_t event, int x, int y, int32_t param) {
    VECTOR2 point = SCR_ScreenToUI(x, y);
    clientWindow_t *modal = CL_WindowModal(), *window;
    LPCUIFRAME frame;

    if (cl_windows.scroll_drag) {
        RECT root = CL_WindowRoot(cl_windows.scroll_drag);
        CL_WindowPrepareState(cl_windows.scroll_drag, &root);
        frame = SCR_Frame(cl_windows.scroll_drag_frame);
        if (event == MENU_MOUSE_MOVE && frame && frame->flags.type == FT_SCROLLBAR) {
            CL_WindowScrollBarSetFromPoint(cl_windows.scroll_drag, (LPUIFRAME)frame, &point, true);
        } else if (event == MENU_MOUSE_UP && param == 1) {
            cl_windows.scroll_drag = NULL;
            cl_windows.scroll_drag_frame = 0;
        }
        return true;
    }
    if (cl_windows.drag) {
        if (event == MENU_MOUSE_MOVE) {
            cl_windows.drag->offset.x = cl_windows.drag_offset.x + point.x - cl_windows.drag_point.x;
            cl_windows.drag->offset.y = cl_windows.drag_offset.y + point.y - cl_windows.drag_point.y;
        } else if (event == MENU_MOUSE_UP && param == 1) cl_windows.drag = NULL;
        return true;
    }
    for (window = cl_windows.last; window; window = window->prev) {
        RECT root;
        LPUIFRAME scrollbar;
        if (modal && window != modal) continue;
        if (!CL_WindowContains(window, &point)) continue;
        if (event == MENU_MOUSE_DOWN && param == 1) CL_WindowFocus(window);

        root = CL_WindowRoot(window);
        CL_WindowPrepareState(window, &root);
        if (event == MENU_MOUSE_SCROLL && CL_WindowScrollWheel(window, &point, MENU_MOUSE_PARAM_Y(param)))
            return true;

        scrollbar = CL_WindowFrameAtType(&point, FT_SCROLLBAR);
        if (scrollbar && event == MENU_MOUSE_DOWN && param == 1) {
            RECT const *sr = SCR_LayoutRect(scrollbar);
            FLOAT bh = MIN(sr->w * UI_PIXEL_ASPECT, sr->h * 0.5f);
            BOOL track = point.y >= sr->y + bh && point.y < sr->y + sr->h - bh;
            CL_WindowScrollBarSetFromPoint(window, scrollbar, &point, track);
            if (track) {
                cl_windows.scroll_drag = window;
                cl_windows.scroll_drag_frame = scrollbar->number;
            }
            SCR_LayoutSetPointer(window->layout, 0, true);
            return true;
        }

        frame = CL_WindowClickableAt(window, &point);
        SCR_LayoutSetPointer(window->layout, frame ? frame->number : 0, event == MENU_MOUSE_DOWN && param == 1);
        if (event == MENU_MOUSE_UP && param == 1 && frame) CL_WindowActivateFrame(window, frame);
        else if (event == MENU_MOUSE_DOWN && param == 1 && !frame && (window->flags & UI_WINDOW_MOVABLE) &&
                 point.y <= window->offset.y + 24.0f) {
            cl_windows.drag = window;
            cl_windows.drag_point = point;
            cl_windows.drag_offset = window->offset;
        }
        return true;
    }
    if (event == MENU_MOUSE_DOWN && param == 1 && !modal) cl_windows.focus = NULL;
    return modal != NULL;
}

BOOL CL_WindowKeyEvent(int key) {
    clientWindow_t *window = CL_WindowModal();
    int upper = toupper(key);
    RECT root;

    if (!window) window = cl_windows.focus;
    if (!window) return false;
    /* Quake II pops the active menu on Escape. Dismiss locally first, then
     * release only the server-side pause owner associated with this window. */
    if (key == K_ESCAPE) {
        if (window->flags & UI_WINDOW_NO_ESCAPE)
            return (window->flags & UI_WINDOW_MODAL) != 0;
        CL_WindowClose(window->id);
        return true;
    }
    root = CL_WindowRoot(window);
    CL_WindowPrepareState(window, &root);
    for (DWORD i = SCR_NumFrames(); i > 0; i--) {
        LPCUIFRAME frame = SCR_Frame(i - 1);
        BOOL cancel;
        if (!SCR_LayoutFrameHasClickCommand(frame)) continue;
        cancel = key == K_ESCAPE && !strcmp(frame->onclick, "button CmdCancel");
        if (cancel || (frame->hotkey && toupper(frame->hotkey) == upper)) {
            CL_WindowActivateFrame(window, frame);
            return true;
        }
    }
    return window->flags & UI_WINDOW_MODAL;
}
