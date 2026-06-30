/*
 * hud_command.c — SC2 command panel (ability button grid).
 *
 * The CommandPanel frame houses the per-unit ability buttons.
 * Dynamic button data (icon, onclick command, tooltip) is stamped
 * on CommandButton child frames before writing.
 */

#include "hud.h"

static BOOL command_loaded;
static sc2BaseFrame_t *command_root;

static void command_ensure_loaded(void) {
    if (command_loaded) return;
    command_loaded = true;

    SC2_LayoutInit();
    if (!SC2_LayoutBuildGameUI()) return;
    command_root = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_COMMAND_PANEL);
}

void SC2_HUD_WriteCommandPanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames;

    command_ensure_loaded();
    frames = SC2_LayoutGetFrames(&count);
    if (!frames || !command_root) return;
    SC2_HUD_WriteLayout(ent, frames, count, command_root, LAYER_COMMANDBAR);
}
