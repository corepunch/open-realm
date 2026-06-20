#include "client.h"
#include <stdlib.h>  /* getenv (BZ_FPS_LOG diagnostic) */

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

void SCR_UpdateScreen(DWORD msec) {
    re.BeginFrame();
    
    V_RenderView();

#ifndef SC2
    /* Client-owned UI frame rendering — iterate DLL's frame array by stride */
    if (ui.frames && ui.frame_size > 0) {
        for (DWORD i = 0; i < ui.num_frames; i++) {
            LPUIBASEFRAME f = (LPUIBASEFRAME)((char *)ui.frames + i * ui.frame_size);
            if (!f || (f->ui_flags & UIFLAG_HIDDEN) || f->hidden) {
                continue;
            }
            if (f->on_draw) {
                f->on_draw(f, &f->screen_rect);
            }
        }
    }
#endif

    /* Client-owned layout frame rendering — server-authored HUD */
    CL_LayoutDrawOverlays();

    CON_DrawConsole();
    if (Cvar_Integer("scr_showfps", 0)) {
        SCR_DrawFPS(msec);
    }
    
//    if (cl.pics[42]) re.DrawPic(cl.pics[42], 0, 0);
    
    re.EndFrame();
}
