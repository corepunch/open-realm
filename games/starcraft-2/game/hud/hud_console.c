/*
 * hud_console.c — SC2 console panel + minimap (LAYER_BACKGROUND).
 *
 * LAYER_BACKGROUND carries the always-visible chrome and minimap only:
 *   ConsolePanel          — bottom chrome model (backdrop)
 *   ConsoleUIContainer    — written as a bare container frame (for parent refs)
 *   └── MinimapPanel      — minimap + ping/terrain/color buttons
 *
 * CommandPanel and InfoPanel live in ConsoleUIContainer in the layout data
 * but are sent on their own dedicated layers (LAYER_COMMANDBAR / LAYER_INFOPANEL)
 * by hud_command.c and hud_infopanel.c.  Writing them here would double-render
 * them and pollute the background layer with 100+ command-card frames.
 */

#include "hud.h"

void SC2_HUD_WriteConsolePanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;

    sc2BaseFrame_t *console = SC2_LayoutFindFrameByName("ConsolePanel");
    sc2BaseFrame_t *ui_con  = SC2_LayoutFindFrameByName("ConsoleUIContainer");
    sc2BaseFrame_t *minimap = SC2_LayoutFindFrameByName("MinimapPanel");

    if (!console && !minimap) {
        /* Fallback path — no SC2 layout data available */
        console = SC2_HUD_FindFallbackFrameByType(SC2_FRAMETYPE_CONSOLE_PANEL);
        if (!console) return;
        SC2_HUD_WriteLayout(ent, frames, count, console, LAYER_BACKGROUND);
        return;
    }

    SC2_HUD_WriteStart(LAYER_BACKGROUND);

    sc2BaseFrame_t *ref = console ? console : minimap;
    SC2_HUD_WriteAncestors(frames, count, ref);

    /* ConsolePanel subtree (chrome models) */
    if (console) SC2_HUD_WriteFrameWithChildren(frames, count, console);

    /* ConsoleUIContainer as a bare container, then MinimapPanel only.
     * CommandPanel/InfoPanel are intentionally omitted — they belong on
     * LAYER_COMMANDBAR and LAYER_INFOPANEL respectively. */
    if (ui_con)  SC2_HUD_WriteFrame(ui_con);
    if (minimap) SC2_HUD_WriteFrameWithChildren(frames, count, minimap);

    SC2_HUD_WriteEnd(ent);
}
