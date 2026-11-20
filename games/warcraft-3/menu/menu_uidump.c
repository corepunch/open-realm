/*
 * menu_uidump.c — console diagnostics for the glue screens: list the live frame tree, click a frame by name.
 *
 *   uidump [filter]               one UIFRAME line per frame whose name contains filter
 *   click <name>[#n] [button]     move, press and release at the centre of a named frame
 *
 * Output is key=value text so scripts can grep it.  Rects are in scene units (0..0.8 x 0..0.6 at 4:3, wider
 * in x under an expanding canvas) and in window pixels.  `click` sends the same MOVE/DOWN/UP sequence through
 * M_MouseEvent that the client delivers for real pointer input, so the pixel comes from the live canvas and
 * window and nothing here depends on a resolution.  The command buffer can be fed from other sources, so the
 * `click` registration is decided once at startup from the environment, outside the console.
 */
#include <stdlib.h>
#include "menu_local.h"
#include "menu_screen.h"

#define UI_DUMP_TEXT_MAX 120

extern cstring_t FrameType[];

typedef struct { float x, y; int px, py; } uiClickPoint_t;

static bool ui_diagnostics_registered;

static bool UI_WindowSize(size2_t *window) {
    refExport_t *renderer = mi.GetRenderer ? mi.GetRenderer() : NULL;
    *window = renderer && renderer->GetWindowSize ? renderer->GetWindowSize() : MAKE(size2_t, 0, 0);
    return window->width > 0 && window->height > 0;
}

static bool UI_AncestorHidden(frameDef_t const *frame) {
    for (uint32_t depth = 0; frame && depth < 64; frame = frame->Parent, depth++)
        if (frame->hidden) return true;
    return false;
}

static void UI_DumpEscape(string_t out, size_t size, cstring_t text) {
    size_t n = 0;
    for (cstring_t p = text ? text : ""; *p && n + 3 < size && n < UI_DUMP_TEXT_MAX; p++) {
        if (*p == '"' || *p == '\\') out[n++] = '\\', out[n++] = *p;
        else if (*p == '\n') out[n++] = '\\', out[n++] = 'n';
        else if (*p == '\r' || *p == '\t') out[n++] = ' ';
        else out[n++] = *p;
    }
    out[n] = '\0';
}

static cstring_t UI_DumpTypeName(frameDef_t const *frame) {
    uint32_t i = 0;
    /* FrameType is NULL-terminated and shorter than the enum (runtime-only types have no FDF name). */
    while (FrameType[i] && i < (uint32_t)frame->Type) i++;
    return i == (uint32_t)frame->Type && FrameType[i] && FrameType[i][0] ? FrameType[i] : "NONE";
}

static cstring_t UI_DumpScreenName(void) {
    uiScreen_t *screen = UI_GetCurrentScreen();
    return screen && screen->name ? screen->name : "-";
}

static bool UI_RectToPixels(rect_t const *rect, rect_t const *scene, size2_t window, float *out) {
    if (scene->w <= 0 || scene->h <= 0) return false;
    out[0] = (rect->x - scene->x) / scene->w * (float)window.width;
    out[1] = (rect->y - scene->y) / scene->h * (float)window.height;
    out[2] = rect->w / scene->w * (float)window.width;
    out[3] = rect->h / scene->h * (float)window.height;
    return true;
}

/* Pixel to scene units, the inverse of UI_RectToPixels and of the mapping M_MouseEvent applies. */
static vec2_t UI_PixelToScene(int px, int py, rect_t const *scene, size2_t window) {
    return MAKE(vec2_t, scene->x + (float)px / (float)window.width * scene->w,
                        scene->y + (float)py / (float)window.height * scene->h);
}

uint32_t UI_DumpFrames(cstring_t filter, void (*emit)(cstring_t line, void *user), void *user) {
    char line[1024], text[2 * UI_DUMP_TEXT_MAX + 4];
    size2_t window;
    bool const have_window = UI_WindowSize(&window);
    rect_t const scene = UI_GetSceneRect();
    uint32_t count = 0;

    snprintf(line, sizeof(line), "UIDUMP screen=%s window=%ux%u scene=%.6f,%.6f,%.6f,%.6f",
             UI_DumpScreenName(), (unsigned)(have_window ? window.width : 0), (unsigned)(have_window ? window.height : 0),
             scene.x, scene.y, scene.w, scene.h);
    emit(line, user);
    FOR_LOOP(i, MAX_UI_CLASSES) {
        frameDef_t const *frame = &frames[i];
        rect_t rect;
        float px[4];
        char geometry[160];
        cstring_t texture;
        if (!frame->inuse || (filter && *filter && !strstr(frame->Name, filter))) continue;
        bool const laid = UI_FrameLaidOutRect(frame, &rect);
        if (laid && have_window && UI_RectToPixels(&rect, &scene, window, px))
            snprintf(geometry, sizeof(geometry), "rect=%.6f,%.6f,%.6f,%.6f px=%.1f,%.1f,%.1f,%.1f",
                     rect.x, rect.y, rect.w, rect.h, px[0], px[1], px[2], px[3]);
        else if (laid)
            snprintf(geometry, sizeof(geometry), "rect=%.6f,%.6f,%.6f,%.6f px=-", rect.x, rect.y, rect.w, rect.h);
        else
            snprintf(geometry, sizeof(geometry), "rect=- px=-");
        texture = UI_TextureName(frame->Texture.Image);
        if (!texture) texture = UI_TextureName(frame->Backdrop.Background);
        UI_DumpEscape(text, sizeof(text), frame->Text);
        bool hit = false;
        if (laid && UI_FrameCanReceiveClick(frame))
            hit = UI_HitTest(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f) == frame;
        snprintf(line, sizeof(line),
                 "UIFRAME id=%u name=%s type=%s fdf=%s parent=%s visible=%u enabled=%u interactive=%u hit=%u text=\"%s\" tex=%s %s",
                 (unsigned)i, frame->Name[0] ? frame->Name : "-", UI_DumpTypeName(frame), UI_FrameSourceFile(frame),
                 frame->Parent && frame->Parent->Name[0] ? frame->Parent->Name : "-",
                 (unsigned)(laid && !UI_AncestorHidden(frame)), (unsigned)!frame->disabled,
                 (unsigned)UI_FrameCanReceiveClick(frame), (unsigned)hit, text, texture ? texture : "-", geometry);
        emit(line, user);
        count++;
    }
    snprintf(line, sizeof(line), "UIDUMP end n=%u", (unsigned)count);
    emit(line, user);
    return count;
}

/* Resolve "Name" or "Name#n" to a frame a pointer could hit at its centre now.  On failure the reason says why. */
static frameDef_t const *UI_FindClickTarget(cstring_t spec, rect_t const *scene, size2_t window,
                                            uiClickPoint_t *point, string_t reason, size_t reason_size) {
    char name[sizeof(UINAME)];
    uint32_t ordinal = 0, seen = 0, clickable = 0;
    frameDef_t const *best = NULL;
    cstring_t failure = "not-found";
    char blocker[sizeof(UINAME) + 16] = "";
    char const *hash = strrchr(spec, '#');

    snprintf(name, sizeof(name), "%s", spec);
    if (hash && hash[1] >= '1' && hash[1] <= '9') {
        char *end;
        ordinal = (uint32_t)strtoul(hash + 1, &end, 10);
        if (!*end) name[hash - spec] = '\0';
        else ordinal = 0;
    }
    FOR_LOOP(i, MAX_UI_CLASSES) {
        frameDef_t const *frame = &frames[i];
        rect_t rect;
        if (!frame->inuse || strcmp(frame->Name, name)) continue;
        if (ordinal && ++seen != ordinal) continue;
        if (UI_AncestorHidden(frame)) { failure = "hidden"; continue; }
        if (!UI_FrameLaidOutRect(frame, &rect)) { failure = "not-laid-out"; continue; }
        if (frame->disabled) { failure = "disabled"; continue; }
        if (!UI_FrameCanReceiveClick(frame)) { failure = "not-interactive"; continue; }
        float const cx = rect.x + rect.w * 0.5f, cy = rect.y + rect.h * 0.5f;
        int const px = (int)lroundf((cx - scene->x) / scene->w * (float)window.width);
        int const py = (int)lroundf((cy - scene->y) / scene->h * (float)window.height);
        vec2_t const at = UI_PixelToScene(px, py, scene, window);
        frameDef_t const *hit = UI_HitTest(at.x, at.y);
        if (hit != frame) {
            failure = "obscured";
            snprintf(blocker, sizeof(blocker), "obscured-by:%s", hit && hit->Name[0] ? hit->Name : "-");
            continue;
        }
        clickable++;
        best = frame; /* ascending index: the last match is the topmost, the one a pointer would reach */
        *point = (uiClickPoint_t){ cx, cy, px, py };
    }
    if (!best) {
        snprintf(reason, reason_size, "%s", !strcmp(failure, "obscured") ? blocker : failure);
        return NULL;
    }
    if (clickable > 1) fprintf(stderr, "UICLICK warning: %u clickable frames named %s; using the topmost (use %s#n)\n",
                               (unsigned)clickable, name, name);
    reason[0] = '\0';
    return best;
}

bool UI_ClickFrame(cstring_t spec, uint32_t button, string_t result, size_t result_size) {
    size2_t window;
    rect_t const scene = UI_GetSceneRect();
    uiClickPoint_t point = { 0 };
    char reason[sizeof(UINAME) + 32];
    frameDef_t const *target;

    if (!spec || !*spec || button < 1 || button > 3) {
        snprintf(result, result_size, "UICLICK fail name=%s reason=bad-arguments", spec ? spec : "-");
        return false;
    }
    if (!UI_WindowSize(&window) || scene.w <= 0 || scene.h <= 0) {
        snprintf(result, result_size, "UICLICK fail name=%s reason=no-window", spec);
        return false;
    }
    target = UI_FindClickTarget(spec, &scene, window, &point, reason, sizeof(reason));
    if (!target) {
        snprintf(result, result_size, "UICLICK fail name=%s reason=%s", spec, reason);
        return false;
    }
    M_MouseEvent(MENU_MOUSE_MOVE, point.px, point.py, 0, 0);
    /* M_MouseEvent reports false while a screen transition or a non-menu owner swallows input. */
    if (!M_MouseEvent(MENU_MOUSE_DOWN, point.px, point.py, (int32_t)button, 0)) {
        snprintf(result, result_size, "UICLICK fail name=%s reason=input-refused", spec);
        return false;
    }
    M_MouseEvent(MENU_MOUSE_UP, point.px, point.py, (int32_t)button, 0);
    snprintf(result, result_size, "UICLICK ok name=%s id=%u px=%d,%d button=%u", spec,
             (unsigned)(target - frames), point.px, point.py, (unsigned)button);
    return true;
}

static void UI_DumpPrint(cstring_t line, void *user) {
    (void)user;
    fprintf(stderr, "%s\n", line);
    if (mi.Printf) mi.Printf("%s\n", line);
}

static void UI_Dump_f(void) {
    if (mi.Cmd_Argc() > 2) { fprintf(stderr, "Usage: uidump [filter]\n"); return; }
    UI_DumpFrames(mi.Cmd_Argc() == 2 ? mi.Cmd_Argv(1) : NULL, UI_DumpPrint, NULL);
}

static bool UI_ParseButton(cstring_t text, uint32_t *button) {
    if (!strcmp(text, "1") || !strcmp(text, "left")) *button = 1;
    else if (!strcmp(text, "2") || !strcmp(text, "middle")) *button = 2;
    else if (!strcmp(text, "3") || !strcmp(text, "right")) *button = 3;
    else return false;
    return true;
}

static void UI_Click_f(void) {
    char result[256];
    uint32_t button = 1;
    if (mi.Cmd_Argc() < 2 || mi.Cmd_Argc() > 3 || (mi.Cmd_Argc() == 3 && !UI_ParseButton(mi.Cmd_Argv(2), &button))) {
        fprintf(stderr, "Usage: click <frame-name>[#n] [1|2|3|left|middle|right]\n");
        return;
    }
    UI_ClickFrame(mi.Cmd_Argv(1), button, result, sizeof(result));
    UI_DumpPrint(result, NULL);
}

/* Only the exact value "1" enables synthetic clicks. */
bool UI_ClickEnabledByEnvironment(cstring_t value) {
    return value && !strcmp(value, "1");
}

void UI_RegisterDiagnosticCommands(void) {
    if (ui_diagnostics_registered) return;
    ui_diagnostics_registered = true;
    mi.Cmd_AddCommand("uidump", UI_Dump_f);
    if (UI_ClickEnabledByEnvironment(getenv("OPENREALM_UI_TEST"))) mi.Cmd_AddCommand("click", UI_Click_f);
}
