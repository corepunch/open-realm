/*
 * hud_command.c — SC2 command panel (ability button grid).
 *
 * The CommandPanel frame houses the per-unit ability buttons.
 * Dynamic button data (icon, onclick command, tooltip) is stamped
 * on CommandButton child frames before writing.
 */

#include "hud.h"

static sc2BaseFrame_t *command_find(void) {
    sc2BaseFrame_t *r = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_COMMAND_PANEL);
    if (r) return r;
    return SC2_HUD_FindFallbackFrameByType(SC2_FRAMETYPE_COMMAND_PANEL);
}

void SC2_HUD_WriteCommandPanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;
    sc2BaseFrame_t *root = command_find();
    if (!root) return;
    SC2_HUD_WriteLayout(ent, frames, count, root, LAYER_COMMANDBAR);
}
