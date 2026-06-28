#include "client.h"
#include <stdlib.h>  /* getenv (BZ_FPS_LOG diagnostic) */
#include <SDL2/SDL.h>

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

void SCR_BeginLoadingPlaque(void) {
    if (cls.disable_screen)
        return;
    if (cls.state == ca_disconnected)
        return;
    if (cls.key_dest == key_console)
        return;
    SCR_UpdateScreen(0);
    cls.disable_screen = SDL_GetTicks();
    cls.disable_servercount = -1;
}

void SCR_EndLoadingPlaque(void) {
    cls.disable_screen = 0;
}

void SCR_DrawScreenField(DWORD msec) {
    re.BeginFrame();

    switch (cls.state) {
    default:
        Com_Error(ERR_FATAL, "SCR_DrawScreenField: bad cls.state");
        break;
    case ca_disconnected:
        ui.Refresh(cl.time);
        break;
    case ca_connecting:
    case ca_connected:
        ui.Refresh(cl.time);
        break;
    case ca_active:
        V_RenderView();
        /* TODO: research whether to replace key_dest enum with a keyCatchers bitmask
        * like Q3 — multiple input consumers can be active simultaneously. */
        if (cls.key_dest == key_menu) {
            ui.Refresh(cl.time);
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

    if (cls.disable_screen) {
        if (SDL_GetTicks() - cls.disable_screen > 120000) {
            cls.disable_screen = 0;
            fprintf(stderr, "Loading plaque timed out.\n");
        }
        return;
    }

    if (++recursive > 2) {
        Com_Error(ERR_FATAL, "SCR_UpdateScreen: recursively called");
    }
    recursive = 1;

    SCR_DrawScreenField(msec);

    recursive = 0;
}
