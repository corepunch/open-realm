#include "client.h"
#include "menu_text_input.h"
#include <ctype.h>

#define MAX_WINDOW_SCROLL_VALUES 32 // entries; bounds retained scroll state; used per transient window
#define MAX_WINDOW_EDIT_VALUES 8 // entries; bounds retained edit state; used per transient window
#define MAX_WINDOW_LIST_VALUES 16 // entries; bounds retained list state; used per transient window

typedef struct {
    uint32_t frame;
    float value;
} clientWindowValue_t;

typedef struct {
    uint32_t frame;
    uint32_t text_frame;
    uint32_t max_chars;
    uint32_t cursor;
    char text[256];
} clientWindowEdit_t;

typedef struct {
    uint32_t frame;
    int16_t selected;
} clientWindowList_t;

typedef struct {
    string_t data;
    uint32_t size;
} windowTextOut_t;

typedef struct clientWindow_s {
    uint32_t id, class_id, flags;
    handle_t layout;
    VECTOR2 offset;
    bool debug_draw_logged;
    clientWindowValue_t scroll_values[MAX_WINDOW_SCROLL_VALUES];
    uint32_t num_scroll_values;
    clientWindowEdit_t edit_values[MAX_WINDOW_EDIT_VALUES];
    uint32_t num_edit_values;
    clientWindowList_t list_values[MAX_WINDOW_LIST_VALUES];
    uint32_t num_list_values;
    struct clientWindow_s *prev, *next;
} clientWindow_t;

static struct {
    clientWindow_t *first, *last, *focus, *drag, *scroll_drag, *edit_window;
    uint32_t scroll_drag_frame;
    uint32_t edit_frame;
    VECTOR2 drag_point, drag_offset;
    bool modal_paused;
} cl_windows;

static rect_t CL_WindowRoot(clientWindow_t const *window);
static bool CL_WindowIsEditBox(LPCUIFRAME frame);
static LPUIFRAME CL_WindowEditTextFrame(LPCUIFRAME edit);

static bool CL_WindowDebugEnabled(void) {
    return Cvar_Integer("ui_window_debug", 0) != 0;
}

static cstring_t CL_WindowImageName(RESOURCE image) {
    if (!image) return "<none>";
    if (image >= MAX_IMAGES) return "<out-of-range>";
    return cl.configstrings[CS_IMAGES + image];
}

static void CL_WindowDebugLayout(clientWindow_t const *window) {
    rect_t root;
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
        rect_t const *r = SCR_LayoutRect(root_frame);
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
        rect_t const *r;
        cstring_t bg_name, edge_name;
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
        rect_t const *r;
        if (!frame) continue;
        r = SCR_LayoutRect(frame);
        if (CL_WindowIsEditBox(frame) && frame->buffer.data &&
            frame->buffer.size >= sizeof(uiEditBox_t)) {
            uiEditBox_t const *edit = frame->buffer.data;
            LPUIFRAME text = CL_WindowEditTextFrame(frame);
            fprintf(stderr,
                    "UI_WINDOW_DEBUG window=%08x control=edit frame=%u parent=%u id=\"%s\" "
                    "rect=(%.4f,%.4f %.4fx%.4f) font=%u maxChars=%u focused=%d "
                    "textFrame=%u textRect=(%.4f,%.4f %.4fx%.4f) text=\"%s\"\n",
                    (unsigned)window->id, (unsigned)frame->number, (unsigned)frame->parent,
                    edit->id, r->x, r->y, r->w, r->h, (unsigned)edit->font,
                    (unsigned)edit->maxChars,
                    cl_windows.edit_window == window && cl_windows.edit_frame == frame->number,
                    text ? (unsigned)text->number : 0u,
                    text ? SCR_LayoutRect(text)->x : 0.0f,
                    text ? SCR_LayoutRect(text)->y : 0.0f,
                    text ? SCR_LayoutRect(text)->w : 0.0f,
                    text ? SCR_LayoutRect(text)->h : 0.0f,
                    text && text->text ? text->text : "");
        } else if (frame->flags.type == FT_LISTBOX && frame->buffer.data &&
                   frame->buffer.size >= sizeof(uiListBox_t)) {
            uiListBox_t const *list = frame->buffer.data;
            float item_height = list->itemHeight > 0.0f ? list->itemHeight : 0.018f;
            float inner_height = MAX(0.0f, r->h - list->border * 2.0f);
            int visible_rows = item_height > 0.0f ? (int)floorf(inner_height / item_height) : 0;
            fprintf(stderr,
                    "UI_WINDOW_DEBUG window=%08x control=list frame=%u parent=%u id=\"%s\" "
                    "rect=(%.4f,%.4f %.4fx%.4f) font=%u itemHeight=%.4f border=%.4f "
                    "visibleRows=%d selected=%d scroll=%.3f text=\"%s\"\n",
                    (unsigned)window->id, (unsigned)frame->number, (unsigned)frame->parent,
                    list->id, r->x, r->y, r->w, r->h, (unsigned)list->text.font,
                    list->itemHeight, list->border, visible_rows,
                    (int)list->selectedIndex, frame->value, frame->text ? frame->text : "");
        } else if ((frame->flags.type == FT_STRING || frame->flags.type == FT_TEXT) &&
                   frame->text && *frame->text) {
            fprintf(stderr,
                    "UI_WINDOW_DEBUG window=%08x control=text frame=%u parent=%u type=%u "
                    "rect=(%.4f,%.4f %.4fx%.4f) text=\"%s\"\n",
                    (unsigned)window->id, (unsigned)frame->number, (unsigned)frame->parent,
                    (unsigned)frame->flags.type, r->x, r->y, r->w, r->h, frame->text);
        } else if (frame->flags.type == FT_SCROLLBAR) {
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

static clientWindow_t *CL_WindowById(uint32_t id) {
    FOR_EACH_LIST(clientWindow_t, window, cl_windows.first)
        if (window->id == id) return window;
    return NULL;
}

static clientWindow_t *CL_WindowByClass(uint32_t class_id) {
    FOR_EACH_LIST(clientWindow_t, window, cl_windows.first)
        if (window->class_id == class_id) return window;
    return NULL;
}

static clientWindow_t *CL_WindowModal(void) {
    for (clientWindow_t *window = cl_windows.last; window; window = window->prev)
        if (window->flags & UI_WINDOW_MODAL) return window;
    return NULL;
}

static bool CL_WindowPauseOwnerPresent(void) {
    for (clientWindow_t *window = cl_windows.last; window; window = window->prev) {
        if ((window->flags & UI_WINDOW_MODAL) && !(window->flags & UI_WINDOW_NO_PAUSE)) return true;
    }
    return false;
}

/* Quake II's menu stack owns the single-player pause cvar. Keep the server
 * request synchronized with pause-owning modal-list presence so a modal such
 * as WC3's Allies dialog can capture gameplay input without freezing time. */
static void CL_WindowSyncPause(void) {
    bool paused = CL_WindowPauseOwnerPresent();
    char command[16];
    if (paused == cl_windows.modal_paused) return;
    cl_windows.modal_paused = paused;
    snprintf(command, sizeof(command), "pause %u", (unsigned)paused);
    Cmd_ForwardToServer(command);
}

static rect_t CL_WindowRoot(clientWindow_t const *window) {
    rect_t root = SCR_LayoutSceneRect();
    root.x += window->offset.x;
    root.y += window->offset.y;
    return root;
}

static void CL_WindowRememberScroll(clientWindow_t *window, uint32_t frame_number, float value) {
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

static bool CL_WindowIsEditBox(LPCUIFRAME frame) {
    return frame && (frame->flags.type == FT_EDITBOX ||
                     frame->flags.type == FT_GLUEEDITBOX ||
                     frame->flags.type == FT_SLASHCHATBOX);
}

static LPUIFRAME CL_WindowEditTextFrame(LPCUIFRAME edit) {
    if (!edit) return NULL;
    FOR_LOOP(i, SCR_NumFrames()) {
        LPUIFRAME child = SCR_Frame(i);
        if (child && child->parent == edit->number &&
            (child->flags.type == FT_TEXT || child->flags.type == FT_STRING))
            return child;
    }
    return NULL;
}

static clientWindowEdit_t *CL_WindowEditValue(clientWindow_t *window, LPCUIFRAME edit, bool create) {
    LPUIFRAME text_frame;
    uiEditBox_t const *wire;
    clientWindowEdit_t *value;

    if (!window || !CL_WindowIsEditBox(edit)) return NULL;
    FOR_LOOP(i, window->num_edit_values)
        if (window->edit_values[i].frame == edit->number) return &window->edit_values[i];
    if (!create || window->num_edit_values >= MAX_WINDOW_EDIT_VALUES) return NULL;
    text_frame = CL_WindowEditTextFrame(edit);
    if (!text_frame) return NULL;
    value = &window->edit_values[window->num_edit_values++];
    memset(value, 0, sizeof(*value));
    value->frame = edit->number;
    value->text_frame = text_frame->number;
    wire = edit->buffer.data && edit->buffer.size >= sizeof(uiEditBox_t)
        ? (uiEditBox_t const *)edit->buffer.data : NULL;
    value->max_chars = wire && wire->maxChars ? wire->maxChars : sizeof(value->text) - 1;
    value->max_chars = MIN(value->max_chars, sizeof(value->text) - 1);
    snprintf(value->text, sizeof(value->text), "%.*s",
             (int)value->max_chars, text_frame->text ? text_frame->text : "");
    value->cursor = (uint32_t)strlen(value->text);
    return value;
}

static clientWindowList_t *CL_WindowListValue(clientWindow_t *window, LPCUIFRAME frame, bool create) {
    uiListBox_t const *wire;
    clientWindowList_t *value;

    if (!window || !frame || frame->flags.type != FT_LISTBOX) return NULL;
    FOR_LOOP(i, window->num_list_values)
        if (window->list_values[i].frame == frame->number) return &window->list_values[i];
    if (!create || window->num_list_values >= MAX_WINDOW_LIST_VALUES) return NULL;
    wire = frame->buffer.data && frame->buffer.size >= sizeof(uiListBox_t)
        ? (uiListBox_t const *)frame->buffer.data : NULL;
    value = &window->list_values[window->num_list_values++];
    value->frame = frame->number;
    value->selected = wire ? wire->selectedIndex : -1;
    return value;
}

static void CL_WindowBlurEdit(void) {
    cl_windows.edit_window = NULL;
    cl_windows.edit_frame = 0;
    CL_SetTransientTextInput(false);
}

static void CL_WindowFocusEdit(clientWindow_t *window, LPCUIFRAME edit) {
    clientWindowEdit_t *value = CL_WindowEditValue(window, edit, true);
    if (!value) return;
    cl_windows.edit_window = window;
    cl_windows.edit_frame = edit->number;
    CL_SetTransientTextInput(true);
}

static cstring_t CL_WindowListSelectedText(LPCUIFRAME frame, int16_t selected, windowTextOut_t *out) {
    char items[2048];
    char *save = NULL, *line;
    int index = 0;

    if (!out || !out->data || out->size == 0) return "";
    out->data[0] = '\0';
    if (!frame || !frame->text || selected < 0) return out->data;
    snprintf(items, sizeof(items), "%s", frame->text);
    for (line = strtok_r(items, "\n", &save); line; line = strtok_r(NULL, "\n", &save), index++) {
        char *hidden;
        if (index != selected) continue;
        hidden = strchr(line, '\t');
        snprintf(out->data, out->size, "%s", hidden && hidden[1] ? hidden + 1 : line);
        return out->data;
    }
    return out->data;
}

/* A chooser may bind its selected hidden value to an edit control so an existing name can be amended or overwritten. */
static void CL_WindowListApplyEdit(clientWindow_t *window, LPCUIFRAME frame, int16_t selected) {
    uiListBox_t const *wire;
    char selected_text[CMDARG_LEN];

    if (!window || !frame || !frame->buffer.data || frame->buffer.size < sizeof(uiListBox_t)) return;
    wire = frame->buffer.data;
    if (!wire->editTarget || !CL_WindowListSelectedText(frame, selected, &MAKE(windowTextOut_t, .data = selected_text, .size = sizeof(selected_text)))) return;
    FOR_LOOP(i, SCR_NumFrames()) {
        LPUIFRAME edit = SCR_Frame(i);
        clientWindowEdit_t *value;
        if (!CL_WindowIsEditBox(edit) || !edit->buffer.data || edit->buffer.size < sizeof(uiEditBox_t)) continue;
        if (edit->number != wire->editTarget) continue;
        value = CL_WindowEditValue(window, edit, true);
        if (!value) return;
        snprintf(value->text, sizeof(value->text), "%.*s", (int)value->max_chars, selected_text);
        value->cursor = (uint32_t)strlen(value->text);
        return;
    }
    fprintf(stderr, "CL_WindowListApplyEdit: unresolved edit frame %u\n", (unsigned)wire->editTarget);
}

static bool CL_WindowControlValue(clientWindow_t *window, cstring_t id, windowTextOut_t *out) {
    if (!window || !id || !*id || !out || !out->data || out->size == 0) return false;
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME frame = SCR_Frame(i);
        if (!frame || !frame->buffer.data) continue;
        if (CL_WindowIsEditBox(frame) && frame->buffer.size >= sizeof(uiEditBox_t)) {
            uiEditBox_t const *edit = frame->buffer.data;
            clientWindowEdit_t *value;
            if (strcmp(edit->id, id)) continue;
            value = CL_WindowEditValue(window, frame, true);
            if (!value) return false;
            snprintf(out->data, out->size, "%s", value->text);
            return true;
        }
        if (frame->flags.type == FT_LISTBOX && frame->buffer.size >= sizeof(uiListBox_t)) {
            uiListBox_t const *list = frame->buffer.data;
            clientWindowList_t *value;
            if (strcmp(list->id, id)) continue;
            value = CL_WindowListValue(window, frame, true);
            if (!value) return false;
            CL_WindowListSelectedText(frame, value->selected, out);
            return out->data[0] != '\0';
        }
    }
    return false;
}

static bool CL_WindowFormatCommand(clientWindow_t *window, cstring_t src, windowTextOut_t *dst) {
    uint32_t out = 0;

    if (!dst || !dst->data || dst->size == 0) return false;
    dst->data[0] = '\0';
    if (!src) return false;
    for (uint32_t i = 0; src[i] && out + 1 < dst->size; i++) {
        if (src[i] == '{') {
            char id[80], value[256];
            uint32_t n = 0, j = i + 1;
            while (src[j] && src[j] != '}' && n + 1 < sizeof(id)) id[n++] = src[j++];
            if (src[j] == '}') {
                id[n] = '\0';
                /* An unresolved control means the authored command is invalid; the old "0" substitution could target the wrong save. */
                if (!CL_WindowControlValue(window, id, &MAKE(windowTextOut_t, .data = value, .size = sizeof(value)))) {
                    fprintf(stderr, "CL_WindowFormatCommand: unresolved control {%s}\n", id);
                    dst->data[0] = '\0';
                    return false;
                }
                for (uint32_t k = 0; value[k] && out + 1 < dst->size; k++) {
                    if ((value[k] == '"' || value[k] == '\\') && out + 2 < dst->size)
                        dst->data[out++] = '\\';
                    dst->data[out++] = value[k];
                }
                i = j;
                continue;
            }
        }
        dst->data[out++] = src[i];
    }
    dst->data[out] = '\0';
    return true;
}

static void CL_WindowPrepareState(clientWindow_t *window, rect_t const * root) {
    if (!window) return;
    SCR_WindowPrepare(window->layout, root);
    FOR_LOOP(i, window->num_scroll_values) {
        LPUIFRAME frame = SCR_Frame(window->scroll_values[i].frame);
        if (frame) frame->value = window->scroll_values[i].value;
    }
    FOR_LOOP(i, window->num_edit_values) {
        clientWindowEdit_t *value = &window->edit_values[i];
        LPUIFRAME text = SCR_Frame(value->text_frame);
        if (text) {
            text->text = value->text;
            text->textLength = (uint32_t)strlen(value->text);
        }
    }
    FOR_LOOP(i, window->num_list_values) {
        clientWindowList_t *value = &window->list_values[i];
        LPUIFRAME frame = SCR_Frame(value->frame);
        if (frame && frame->buffer.data && frame->buffer.size >= sizeof(uiListBox_t))
            ((uiListBox_t *)frame->buffer.data)->selectedIndex = value->selected;
    }
}

static LPUIFRAME CL_WindowScrollOwner(LPUIFRAME frame) {
    LPUIFRAME parent;
    if (!frame) return NULL;
    if (frame->flags.type == FT_TEXTAREA || frame->flags.type == FT_LISTBOX) return frame;
    if (frame->flags.type != FT_SCROLLBAR || frame->parent >= SCR_NumFrames()) return NULL;
    parent = SCR_Frame(frame->parent);
    return parent && (parent->flags.type == FT_TEXTAREA || parent->flags.type == FT_LISTBOX)
        ? parent : frame;
}

static int CL_WindowListMaxScroll(LPCUIFRAME frame) {
    uiListBox_t const *lb;
    rect_t view;
    float item_height;
    int count = 0, visible;

    if (!frame || frame->flags.type != FT_LISTBOX || !frame->buffer.data ||
        frame->buffer.size < sizeof(uiListBox_t)) return 0;
    lb = frame->buffer.data;
    /* Share the renderer's inset math so list hit-testing uses the exact same viewport. */
    view = Rect_inset(SCR_LayoutRect(frame), lb->border);
    item_height = lb->itemHeight > 0.0f ? lb->itemHeight : 0.018f;
    if (frame->text && *frame->text) {
        count = 1;
        for (cstring_t p = frame->text; *p; p++) if (*p == '\n') count++;
    }
    visible = MAX((int)floorf(view.h / item_height), 1);
    return MAX(count - visible, 0);
}

static float CL_WindowScrollStep(LPCUIFRAME owner) {
    if (owner && owner->flags.type == FT_LISTBOX) {
        int max_scroll = CL_WindowListMaxScroll(owner);
        return max_scroll > 0 ? 1.0f / max_scroll : 0.0f;
    }
    return 0.1f;
}

static bool CL_WindowSetScroll(clientWindow_t *window, LPUIFRAME owner, float value) {
    if (!window || !owner) return false;
    if ((owner->flags.type == FT_TEXTAREA && SCR_LayoutTextAreaMaxScroll(owner) <= 0.0f) ||
        (owner->flags.type == FT_LISTBOX && CL_WindowListMaxScroll(owner) <= 0)) {
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
    for (uint32_t i = SCR_NumFrames(); i > 0; i--) {
        LPUIFRAME frame = SCR_Frame(i - 1);
        if (frame && frame->flags.type == type && Rect_contains(SCR_LayoutRect(frame), point))
            return frame;
    }
    return NULL;
}

static bool CL_WindowScrollWheel(clientWindow_t *window, LPCVECTOR2 point, int wheel_y) {
    LPUIFRAME hit, owner;
    if (!window || !point || !wheel_y) return false;
    hit = CL_WindowFrameAtType(point, FT_SCROLLBAR);
    if (!hit) hit = CL_WindowFrameAtType(point, FT_TEXTAREA);
    if (!hit) hit = CL_WindowFrameAtType(point, FT_LISTBOX);
    owner = CL_WindowScrollOwner(hit);
    if (!owner) return false;
    if ((owner->flags.type == FT_TEXTAREA && SCR_LayoutTextAreaMaxScroll(owner) <= 0.0f) ||
        (owner->flags.type == FT_LISTBOX && CL_WindowListMaxScroll(owner) <= 0))
        return true;
    return CL_WindowSetScroll(window, owner,
                              owner->value - wheel_y * CL_WindowScrollStep(owner));
}

static bool CL_WindowScrollBarSetFromPoint(clientWindow_t *window, LPUIFRAME scrollbar,
                                           LPCVECTOR2 point, bool drag_track) {
    LPUIFRAME owner = CL_WindowScrollOwner(scrollbar);
    rect_t const *screen;
    rect_t track;
    float bh, th, value;

    if (!window || !scrollbar || !owner || !point) return false;
    if ((owner->flags.type == FT_TEXTAREA && SCR_LayoutTextAreaMaxScroll(owner) <= 0.0f) ||
        (owner->flags.type == FT_LISTBOX && CL_WindowListMaxScroll(owner) <= 0))
        return true;
    screen = SCR_LayoutRect(scrollbar);
    if (!screen || screen->w <= 0.0f || screen->h <= 0.0f) return true;
    bh = MIN(screen->w * UI_PIXEL_ASPECT, screen->h * 0.5f);
    track = MAKE(rect_t, screen->x, screen->y + bh, screen->w, screen->h - bh * 2.0f);
    value = owner->value;

    if (!drag_track && point->y < track.y) {
        value -= CL_WindowScrollStep(owner);
    } else if (!drag_track && point->y >= track.y + track.h) {
        value += CL_WindowScrollStep(owner);
    } else if (track.h > 0.0f) {
        bool compact = scrollbar->buffer.size == sizeof(uiScrollBarImage_t);
        th = MIN(compact ? bh : MIN(bh, 0.010f), track.h);
        value = track.h > th
            ? (point->y - track.y - th * 0.5f) / (track.h - th)
            : 0.0f;
    }
    return CL_WindowSetScroll(window, owner, value);
}

static bool CL_WindowContains(clientWindow_t *window, LPCVECTOR2 point) {
    rect_t root = CL_WindowRoot(window);
    CL_WindowPrepareState(window, &root);
    LPCUIFRAME frame = SCR_Frame(1);
    return frame && Rect_contains(SCR_LayoutRect(frame), point);
}

bool CL_WindowMouseOver(int x, int y) {
    VECTOR2 point = SCR_ScreenToUI(x, y);
    clientWindow_t *modal = CL_WindowModal();
    for (clientWindow_t *window = cl_windows.last; window; window = window->prev) {
        if (modal && window != modal) continue;
        if (CL_WindowContains(window, &point)) return true;
    }
    return modal != NULL;
}

static LPCUIFRAME CL_WindowClickableAt(clientWindow_t *window, LPCVECTOR2 point) {
    rect_t root = CL_WindowRoot(window);
    CL_WindowPrepareState(window, &root);
    for (uint32_t i = SCR_NumFrames(); i > 0; i--) {
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
        cstring_t source = frame->onclick + close_command_len;
        char command[CMDARG_LEN * 4];
        if (CL_WindowFormatCommand(window, source, &MAKE(windowTextOut_t, .data = command, .size = sizeof(command))))
            Cmd_ForwardToServer(command);
        CL_WindowClose(window->id);
    } else if (!strcmp(frame->onclick, UI_WINDOW_DISCONNECT_ACTION)) {
        /* A gameplay leave is a full world-to-front-end boundary.  Defer it
         * until input dispatch returns so the client can disconnect, stop the
         * local server/game module, clear map-scoped renderer state, and then
         * rebuild the menu/FDF state in that order. */
        MenuAction("menu", "menu_main");
    } else if (!strcmp(frame->onclick, UI_WINDOW_QUIT_ACTION)) {
        /* Defer normal application shutdown until input dispatch returns. */
        Cbuf_AddText("quit\n");
    } else {
        char command[CMDARG_LEN * 4];
        if (CL_WindowFormatCommand(window, frame->onclick, &MAKE(windowTextOut_t, .data = command, .size = sizeof(command))))
            Cmd_ForwardToServer(command);
    }
}

void CL_WindowOpen(uiWindowDef_t const *def, handle_t layout) {
    clientWindow_t *window = CL_WindowById(def->id);
    if (!window && (def->flags & UI_WINDOW_UNIQUE)) window = CL_WindowByClass(def->class_id);
    if (!window) {
        window = MemAlloc(sizeof(*window));
        memset(window, 0, sizeof(*window));
        window->prev = cl_windows.last;
        if (cl_windows.last) cl_windows.last->next = window;
        else cl_windows.first = window;
        cl_windows.last = window;
    } else {
        if (cl_windows.edit_window == window) CL_WindowBlurEdit();
        SAFE_DELETE(window->layout, MemFree);
    }
    window->id = def->id; window->class_id = def->class_id; window->flags = def->flags; window->layout = layout;
    window->debug_draw_logged = false;
    window->num_edit_values = 0;
    window->num_list_values = 0;
    CL_WindowFocus(window);
    CL_WindowDebugLayout(window);
    {
        rect_t root = CL_WindowRoot(window);
        CL_WindowPrepareState(window, &root);
        FOR_LOOP(i, SCR_NumFrames()) {
            LPCUIFRAME frame = SCR_Frame(i);
            if (CL_WindowIsEditBox(frame)) { CL_WindowFocusEdit(window, frame); break; }
        }
    }
    CL_WindowSyncPause();
}

void CL_WindowClose(uint32_t id) {
    clientWindow_t *window = CL_WindowById(id);
    if (!window) return;
    if (cl_windows.edit_window == window) CL_WindowBlurEdit();
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

bool CL_WindowModalActive(void) { return CL_WindowModal() != NULL; }

/* SDL text input is owned by a focused transient edit box while gameplay
 * remains key_game. Player snapshots repeatedly reaffirm gameplay input, so
 * expose that ownership rather than letting CL_SetGameplayInput disable text
 * delivery underneath the still-focused edit control. */
bool CL_WindowTextInputActive(void) {
    return cl_windows.edit_window != NULL && cl_windows.edit_frame != 0;
}

void CL_WindowDraw(void) {
    FOR_EACH_LIST(clientWindow_t, window, cl_windows.first) {
        rect_t root = CL_WindowRoot(window);
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

bool CL_WindowMouseEvent(menuMouseEvent_t event, int x, int y, int32_t param) {
    VECTOR2 point = SCR_ScreenToUI(x, y);
    clientWindow_t *modal = CL_WindowModal(), *window;
    LPCUIFRAME frame;

    if (cl_windows.scroll_drag) {
        rect_t root = CL_WindowRoot(cl_windows.scroll_drag);
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
        rect_t root;
        LPUIFRAME scrollbar;
        if (modal && window != modal) continue;
        if (!CL_WindowContains(window, &point)) continue;
        if (event == MENU_MOUSE_DOWN && param == 1) CL_WindowFocus(window);

        root = CL_WindowRoot(window);
        CL_WindowPrepareState(window, &root);
        if (event == MENU_MOUSE_SCROLL && CL_WindowScrollWheel(window, &point, MENU_MOUSE_PARAM_Y(param)))
            return true;

        if (event == MENU_MOUSE_DOWN && param == 1) {
            LPUIFRAME edit = CL_WindowFrameAtType(&point, FT_EDITBOX);
            LPUIFRAME list = CL_WindowFrameAtType(&point, FT_LISTBOX);
            LPUIFRAME list_scrollbar = CL_WindowFrameAtType(&point, FT_SCROLLBAR);
            if (!edit) edit = CL_WindowFrameAtType(&point, FT_GLUEEDITBOX);
            if (!edit) edit = CL_WindowFrameAtType(&point, FT_SLASHCHATBOX);
            if (edit) {
                CL_WindowFocusEdit(window, edit);
                SCR_LayoutSetPointer(window->layout, 0, true);
                return true;
            }
            if (list && !list_scrollbar &&
                list->buffer.data && list->buffer.size >= sizeof(uiListBox_t)) {
                uiListBox_t const *lb = list->buffer.data;
                rect_t list_rect = *SCR_LayoutRect(list);
                float item_height = lb->itemHeight > 0.0f ? lb->itemHeight : 0.018f;
                clientWindowList_t *value = CL_WindowListValue(window, list, true);
                list_rect.x += lb->border; list_rect.y += lb->border;
                list_rect.w = MAX(0.0f, list_rect.w - lb->border * 2.0f);
                list_rect.h = MAX(0.0f, list_rect.h - lb->border * 2.0f);
                if (value && item_height > 0.0f && Rect_contains(&list_rect, &point)) {
                    /* Match the front-end cinematic/mission MapListBox: rows
                     * run top-to-bottom inside one clipped content rectangle.
                     * Drawing and hit-testing must use the same origin or a
                     * visible row can select a different save. */
                    int row = (int)floorf((point.y - list_rect.y) / item_height);
                    int count = 0;
                    int visible = MAX((int)floorf(list_rect.h / item_height), 1);
                    int max_scroll, scroll_offset;
                    if (list->text) for (cstring_t p = list->text; *p; p++) if (*p == '\n') count++;
                    if (list->text && *list->text) count++;
                    max_scroll = MAX(count - visible, 0);
                    scroll_offset = max_scroll
                        ? (int)lroundf(MIN(MAX(list->value, 0.0f), 1.0f) * max_scroll)
                        : 0;
                    row += scroll_offset;
                    if (row >= 0 && row < count) {
                        value->selected = (int16_t)row;
                        CL_WindowListApplyEdit(window, list, value->selected);
                    }
                }
                SCR_LayoutSetPointer(window->layout, 0, true);
                return true;
            }
            if (cl_windows.edit_window == window) CL_WindowBlurEdit();
        }

        scrollbar = CL_WindowFrameAtType(&point, FT_SCROLLBAR);
        if (scrollbar && event == MENU_MOUSE_DOWN && param == 1) {
            rect_t const *sr = SCR_LayoutRect(scrollbar);
            float bh = MIN(sr->w * UI_PIXEL_ASPECT, sr->h * 0.5f);
            bool track = point.y >= sr->y + bh && point.y < sr->y + sr->h - bh;
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

bool CL_WindowKeyEvent(int key) {
    clientWindow_t *window = CL_WindowModal();
    int upper = toupper(key);
    rect_t root;
    bool modal;

    if (!window) window = cl_windows.focus;
    if (!window) return false;
    modal = window->flags & UI_WINDOW_MODAL;
    if (cl_windows.edit_window == window && cl_windows.edit_frame) {
        clientWindowEdit_t *value = NULL;
        FOR_LOOP(i, window->num_edit_values)
            if (window->edit_values[i].frame == cl_windows.edit_frame) { value = &window->edit_values[i]; break; }
        if (value) {
            menuTextInput_t ti = {
                .text = value->text, .size = sizeof(value->text),
                .max_chars = value->max_chars, .cursor = value->cursor,
            };
            if (key == 8) { M_TextInput_Backspace(&ti); value->cursor = ti.cursor; return true; }
            /* Non-modal gameplay windows must not turn WC3's horizontal
             * camera arrows into local edit-cursor movement. Up/Down already
             * fall through this edit path; keep Left/Right symmetric with
             * them while modal edit dialogs retain normal cursor control. */
            if (key == K_LEFTARROW) {
                if (!modal) return false;
                M_TextInput_MoveCursor(&ti, -1); value->cursor = ti.cursor; return true;
            }
            if (key == K_RIGHTARROW) {
                if (!modal) return false;
                M_TextInput_MoveCursor(&ti, 1); value->cursor = ti.cursor; return true;
            }
            if (key == K_ENTER || key == K_TAB) { CL_WindowBlurEdit(); return true; }
            if (key == K_ESCAPE) { CL_WindowBlurEdit(); return true; }
        }
    }
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
    for (uint32_t i = SCR_NumFrames(); i > 0; i--) {
        LPCUIFRAME frame = SCR_Frame(i - 1);
        bool cancel;
        if (!SCR_LayoutFrameHasClickCommand(frame)) continue;
        cancel = key == K_ESCAPE && !strcmp(frame->onclick, "button CmdCancel");
        if (cancel || (frame->hotkey && toupper(frame->hotkey) == upper)) {
            CL_WindowActivateFrame(window, frame);
            return true;
        }
    }
    return modal;
}


bool CL_WindowTextInput(cstring_t text) {
    clientWindow_t *window = cl_windows.edit_window;
    clientWindowEdit_t *value = NULL;
    char filtered[256];
    menuTextInput_t ti;

    if (!window || !text || !*text) return false;
    FOR_LOOP(i, window->num_edit_values)
        if (window->edit_values[i].frame == cl_windows.edit_frame) { value = &window->edit_values[i]; break; }
    if (!value) return false;
    M_TextInput_Filter(text, filtered, sizeof(filtered));
    if (!filtered[0]) return true;
    ti = (menuTextInput_t){
        .text = value->text, .size = sizeof(value->text),
        .max_chars = value->max_chars, .cursor = value->cursor,
    };
    M_TextInput_Insert(&ti, filtered);
    value->cursor = ti.cursor;
    if (Cvar_Integer("ui_window_debug", 0) >= 2)
        fprintf(stderr,
                "UI_WINDOW_DEBUG text-input window=%08x editFrame=%u input=\"%s\" "
                "value=\"%s\" cursor=%u maxChars=%u\n",
                (unsigned)window->id, (unsigned)value->frame, filtered, value->text,
                (unsigned)value->cursor, (unsigned)value->max_chars);
    return true;
}

static clientWindow_t *CL_WindowPrepared(void) {
    FOR_EACH_LIST(clientWindow_t, window, cl_windows.first)
        if (SCR_WindowLayoutIsCurrent(window->layout)) return window;
    return NULL;
}

cstring_t CL_WindowEditTextValue(uint32_t text_frame) {
    clientWindow_t *window = CL_WindowPrepared();

    if (!window || !text_frame) return NULL;

    /* Frame numbers are local to each serialized layout. A gameplay HUD frame
     * can therefore have the same number as a transient edit-box text child.
     * Only consult retained edit state for the window whose layout is currently
     * prepared; otherwise the edit value can leak into an unrelated HUD label. */
    FOR_LOOP(i, window->num_edit_values) {
        clientWindowEdit_t const *value = &window->edit_values[i];
        if (value->text_frame == text_frame) return value->text;
    }
    return NULL;
}

bool CL_WindowEditCursor(uint32_t text_frame, uint32_t * cursor) {
    clientWindow_t *window = CL_WindowPrepared();
    if (!window || window != cl_windows.edit_window || !text_frame) return false;
    FOR_LOOP(i, window->num_edit_values) {
        clientWindowEdit_t const *value = &window->edit_values[i];
        if (value->frame == cl_windows.edit_frame && value->text_frame == text_frame) {
            if (cursor) *cursor = value->cursor;
            return true;
        }
    }
    return false;
}
