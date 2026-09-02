#include "client.h"
#include <ctype.h>

typedef struct clientWindow_s {
    DWORD id, class_id, flags;
    HANDLE layout;
    VECTOR2 offset;
    struct clientWindow_s *prev, *next;
} clientWindow_t;

static struct {
    clientWindow_t *first, *last, *focus, *drag;
    VECTOR2 drag_point, drag_offset;
} cl_windows;

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

static RECT CL_WindowRoot(clientWindow_t const *window) {
    return MAKE(RECT, window->offset.x, window->offset.y, SCR_UICanvasWidth(), UI_BASE_HEIGHT);
}

static BOOL CL_WindowContains(clientWindow_t *window, LPCVECTOR2 point) {
    RECT root = CL_WindowRoot(window);
    SCR_WindowPrepare(window->layout, &root);
    LPCUIFRAME frame = SCR_Frame(1);
    return frame && Rect_contains(SCR_LayoutRect(frame), point);
}

static LPCUIFRAME CL_WindowClickableAt(clientWindow_t *window, LPCVECTOR2 point) {
    RECT root = CL_WindowRoot(window);
    SCR_WindowPrepare(window->layout, &root);
    for (DWORD i = SCR_NumFrames(); i > 0; i--) {
        LPCUIFRAME frame = SCR_Frame(i - 1);
        if (SCR_LayoutFrameHasClickCommand(frame) && Rect_contains(SCR_LayoutRect(frame), point)) return frame;
    }
    return NULL;
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
    CL_WindowFocus(window);
}

void CL_WindowClose(DWORD id) {
    clientWindow_t *window = CL_WindowById(id);
    if (!window) return;
    if (cl_windows.focus == window) cl_windows.focus = NULL;
    if (cl_windows.drag == window) cl_windows.drag = NULL;
    CL_WindowUnlink(window);
    SAFE_DELETE(window->layout, MemFree);
    MemFree(window);
    if (!cl_windows.focus) cl_windows.focus = cl_windows.last;
}

void CL_WindowClear(void) {
    while (cl_windows.first) CL_WindowClose(cl_windows.first->id);
    memset(&cl_windows, 0, sizeof(cl_windows));
}

BOOL CL_WindowModalActive(void) { return CL_WindowModal() != NULL; }

void CL_WindowDraw(void) {
    FOR_EACH_LIST(clientWindow_t, window, cl_windows.first) {
        RECT root = CL_WindowRoot(window);
        SCR_WindowPrepare(window->layout, &root);
        SCR_LayoutDrawOverlay(window->layout);
    }
}

BOOL CL_WindowMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) {
    VECTOR2 point = SCR_ScreenToUI(x, y);
    clientWindow_t *modal = CL_WindowModal(), *window;
    LPCUIFRAME frame;

    if (cl_windows.drag) {
        if (event == UI_MOUSE_MOVE) {
            cl_windows.drag->offset.x = cl_windows.drag_offset.x + point.x - cl_windows.drag_point.x;
            cl_windows.drag->offset.y = cl_windows.drag_offset.y + point.y - cl_windows.drag_point.y;
        } else if (event == UI_MOUSE_UP && param == 1) cl_windows.drag = NULL;
        return true;
    }
    for (window = cl_windows.last; window; window = window->prev) {
        if (modal && window != modal) continue;
        if (!CL_WindowContains(window, &point)) continue;
        if (event == UI_MOUSE_DOWN && param == 1) CL_WindowFocus(window);
        frame = CL_WindowClickableAt(window, &point);
        SCR_LayoutSetPointer(window->layout, frame ? frame->number : 0, event == UI_MOUSE_DOWN && param == 1);
        if (event == UI_MOUSE_UP && param == 1 && frame) SCR_LayoutSendFrameCommand(frame);
        else if (event == UI_MOUSE_DOWN && param == 1 && !frame && (window->flags & UI_WINDOW_MOVABLE)) {
            cl_windows.drag = window;
            cl_windows.drag_point = point;
            cl_windows.drag_offset = window->offset;
        }
        return true;
    }
    if (event == UI_MOUSE_DOWN && param == 1 && !modal) cl_windows.focus = NULL;
    return modal != NULL;
}

BOOL CL_WindowKeyEvent(int key) {
    clientWindow_t *window = CL_WindowModal();
    int upper = toupper(key);
    RECT root;

    if (!window) window = cl_windows.focus;
    if (!window) return false;
    root = CL_WindowRoot(window);
    SCR_WindowPrepare(window->layout, &root);
    for (DWORD i = SCR_NumFrames(); i > 0; i--) {
        LPCUIFRAME frame = SCR_Frame(i - 1);
        BOOL cancel;
        if (!SCR_LayoutFrameHasClickCommand(frame)) continue;
        cancel = key == K_ESCAPE && !strcmp(frame->onclick, "button CmdCancel");
        if (cancel || (frame->hotkey && toupper(frame->hotkey) == upper)) {
            SCR_LayoutSendFrameCommand(frame);
            return true;
        }
    }
    return window->flags & UI_WINDOW_MODAL;
}