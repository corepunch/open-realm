/*
 * hud_console.c — SC2 console panel + minimap (LAYER_BACKGROUND).
 *
 * LAYER_BACKGROUND owns the complete native console tree. Keeping chrome,
 * portrait, minimap, InfoPanel and commands in one message preserves the
 * layout's parent numbering and draw order, matching the WC3 HUD writer.
 */

#include "hud.h"

void SC2_HUD_WriteConsolePanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;

    sc2BaseFrame_t *console = SC2_LayoutFindFrameByName("ConsolePanel");
    sc2BaseFrame_t *ui_con  = SC2_LayoutFindFrameByName("ConsoleUIContainer");
    sc2BaseFrame_t *minimap = SC2_LayoutFindFrameByName("MinimapPanel");
    sc2BaseFrame_t *info = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_INFO_PANEL);
    sc2BaseFrame_t *command = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_COMMAND_PANEL);

    if (!console && !ui_con) return;
    SC2_HUD_PrepareCommandPanel(frames, count, command);

    SC2_HUD_WriteStart(LAYER_BACKGROUND);

    sc2BaseFrame_t *ref = console ? console : minimap;
    SC2_HUD_WriteAncestors(frames, count, ref);

    /* ConsolePanel owns the three chrome models and the portrait panel. */
    if (console) SC2_HUD_WriteFrameWithChildren(frames, count, console);

    /* These are siblings under ConsoleUIContainer and must retain that shared parent. */
    if (ui_con)  SC2_HUD_WriteFrame(ui_con);
    if (minimap) SC2_HUD_WriteFrameWithChildren(frames, count, minimap);
    if (info) SC2_HUD_WriteFrameWithChildren(frames, count, info);
    if (command) SC2_HUD_WriteFrameWithChildren(frames, count, command);

    SC2_HUD_WriteEnd(ent);
}
