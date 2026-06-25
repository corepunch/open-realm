#include "client.h"
#include "ui_layout.h"
#include <stdlib.h>  /* getenv (BZ_FPS_LOG diagnostic) */

BOOL scr_initialized;

#define SCR_FPS_HEIGHT 8
#define SCR_FPS_BOTTOM_MARGIN 4

static void SCR_DrawString(int x, int y, LPCSTR string) {
    if (!string) {
        return;
    }
    for (DWORD i = 0; string[i]; i++) {
        re.DrawChar(x + i * 8, y, (BYTE)string[i]);
    }
}

static void SCR_DrawFPS(DWORD msec) {
    static DWORD elapsed = 0;
    static DWORD frames_drawn = 0;
    static DWORD fps = 0;
    char text[32];
    size2_t window = re.GetWindowSize();
    DWORD inset = SCR_FPS_HEIGHT + SCR_FPS_BOTTOM_MARGIN;
    DWORD y = window.height > inset ? window.height - inset : 0;

    elapsed += msec;
    frames_drawn++;
    if (elapsed >= 500) {
        fps = frames_drawn * 1000 / elapsed;
        elapsed = 0;
        frames_drawn = 0;
        if (getenv("BZ_FPS_LOG")) fprintf(stderr, "BZ_FPS %u\n", (unsigned)fps);
    } else if (!fps && msec > 0) {
        fps = 1000 / msec;
    }

    if (fps) {
        snprintf(text, sizeof(text), "FPS: %u", (unsigned)fps);
    } else {
        snprintf(text, sizeof(text), "FPS: --");
    }
    SCR_DrawString(10, y, text);
}

void SCR_DrawScreenField(DWORD msec) {
    re.BeginFrame();

    switch (cls.state) {
    default:
        Com_Error(ERR_FATAL, "SCR_DrawScreenField: bad cls.state");
        break;
    case ca_disconnected:
        if (ui.Refresh) ui.Refresh(cl.time);
        break;
    case ca_connecting:
    case ca_connected:
        if (ui.Refresh) ui.Refresh(cl.time);
        break;
    case ca_loading:
        if (ui.DrawLoadingScreen) {
            ui.DrawLoadingScreen(cl.loading_map, cl.loading_status, cl.loading_progress);
        } else {
            size2_t w = re.GetWindowSize();
            char buf[256];
            int y = (int)w.height / 2 - 16;
            if (cl.loading_map[0]) {
                snprintf(buf, sizeof(buf), "Loading %s...", cl.loading_map);
                SCR_DrawString((int)(w.width - strlen(buf) * 8) / 2, y, buf);
            }
            if (cl.loading_status[0]) {
                snprintf(buf, sizeof(buf), "%s", cl.loading_status);
                SCR_DrawString((int)(w.width - strlen(buf) * 8) / 2, y + 16, buf);
            }
            snprintf(buf, sizeof(buf), "%.0f%%", cl.loading_progress * 100.0f);
            SCR_DrawString((int)(w.width - strlen(buf) * 8) / 2, y + 32, buf);
        }
        break;
    case ca_active:
        V_RenderView();
        SCR_DrawLayout();
        /* TODO: research whether to replace key_dest enum with a keyCatchers bitmask
        * like Q3 — multiple input consumers can be active simultaneously. */
        if (cls.key_dest == key_menu) {
            if (ui.Refresh) ui.Refresh(cl.time);
        }
        break;
    }

    CON_DrawConsole();
    if (Cvar_Integer("scr_showfps", 0)) {
        SCR_DrawFPS(msec);
    }

    re.EndFrame();
}

void SCR_UpdateScreen(DWORD msec) {
    static int recursive;

    if (!scr_initialized) {
        return;
    }

    if (++recursive > 2) {
        Com_Error(ERR_FATAL, "SCR_UpdateScreen: recursively called");
    }
    recursive = 1;

    SCR_DrawScreenField(msec);

    recursive = 0;
}
