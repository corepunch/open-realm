/*
 * hud_console.c — SC2 console panel + ConsoleUIContainer (minimap, etc.).
 *
 * The layout tree under UIContainer is:
 *   ├── ConsolePanel          — bottom chrome model (backdrop)
 *   └── ConsoleUIContainer    — minimap, command buttons, info panel, controls
 *
 * ConsolePanel and ConsoleUIContainer are siblings.  Writing only the
 * ConsolePanel subtree leaves the minimap and all interactive elements
 * invisible.  Both subtrees are written here on LAYER_BACKGROUND, preceded
 * by their shared ancestors so parent references resolve correctly.
 */

#include "hud.h"

void SC2_HUD_WriteConsolePanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;

    sc2BaseFrame_t *console = SC2_LayoutFindFrameByName("ConsolePanel");
    sc2BaseFrame_t *ui_con  = SC2_LayoutFindFrameByName("ConsoleUIContainer");

    if (!console && !ui_con) {
        /* Fallback path — no SC2 layout data available */
        console = SC2_HUD_FindFallbackFrameByType(SC2_FRAMETYPE_CONSOLE_PANEL);
        if (!console) return;
        SC2_HUD_WriteLayout(ent, frames, count, console, LAYER_BACKGROUND);
        return;
    }

    SC2_HUD_WriteStart(LAYER_BACKGROUND);

    /* Shared ancestors (GameUI → UIContainer) */
    sc2BaseFrame_t *ref = console ? console : ui_con;
    SC2_HUD_WriteAncestors(frames, count, ref);

    /* ConsolePanel subtree (backdrop chrome model) */
    if (console) SC2_HUD_WriteFrameWithChildren(frames, count, console);

    /* ConsoleUIContainer subtree — minimap, command buttons, info panel, controls */
    if (ui_con) SC2_HUD_WriteFrameWithChildren(frames, count, ui_con);

    SC2_HUD_WriteEnd(ent);
}
