/*
 * hud_command.c — SC2 command panel (ability button grid).
 *
 * The CommandPanel frame houses the per-unit ability buttons.
 * Dynamic button data (icon, onclick command, tooltip) is stamped
 * on CommandButton child frames before writing.
 */

#include "hud.h"

static sc2BaseFrame_t *command_root;
static BOOL command_root_found;

static void command_find_root(void) {
    if (command_root_found) return;
    command_root_found = true;
    command_root = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_COMMAND_PANEL);
}

void SC2_HUD_WriteCommandPanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;
    command_find_root();
    if (!command_root) return;
    SC2_HUD_WriteLayout(ent, frames, count, command_root, LAYER_COMMANDBAR);
}
