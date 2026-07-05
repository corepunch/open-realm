/*
 * hud_command.c — SC2 command panel (ability button grid).
 *
 * The CommandPanel frame houses the per-unit ability buttons.
 * Dynamic button data (icon, onclick command, tooltip) is stamped
 * on CommandButton child frames before writing.
 */

#include "hud.h"
#include <ctype.h>

/* SC2 command buttons need runtime icon assignment; template defaults only
 * define the button shell/background and leave the command art blank. */
static void command_apply_icons(sc2BaseFrame_t *frames, DWORD count, sc2BaseFrame_t *root) {
    static LPCSTR icon_paths[] = {
        "Assets/Textures/icon-mineral.dds",
        "Assets/Textures/icon-gas.dds",
        "Assets/Textures/icon-supply.dds",
        "Assets/Textures/icon-highyieldmineral.dds",
        "Assets/Textures/ui_idlepeon_normalpressed_terran.dds",
        "Assets/Textures/ai_avatar.dds",
        "Assets/Textures/ui_warpin_normalpressed.dds",
        "Assets/Textures/ui_controlgroup_normalpressed_terran.dds",
    };
    if (!frames || !root) return;
    static BOOL logged_once;
    int stamped = 0;

    for (DWORD i = 0; i < count; i++) {
        sc2BaseFrame_t *btn = &frames[i];
        int slot = 0;
        if (btn->sc2_type != SC2_FRAMETYPE_COMMAND_BUTTON) continue;
        if (btn->parent_index == (DWORD)-1 || btn->parent_index != root->number) continue;
        if (btn->name && strlen(btn->name) >= 2 &&
            isdigit((unsigned char)btn->name[strlen(btn->name) - 2]) &&
            isdigit((unsigned char)btn->name[strlen(btn->name) - 1]))
            slot = (btn->name[strlen(btn->name) - 2] - '0') * 10 + (btn->name[strlen(btn->name) - 1] - '0');

        RESOURCE icon = gi.ImageIndex(icon_paths[slot % (sizeof(icon_paths) / sizeof(*icon_paths))]);
        for (DWORD j = 0; j < count; j++) {
            sc2BaseFrame_t *child = &frames[j];
            if (child->parent_index != btn->number || child->sc2_type != SC2_FRAMETYPE_IMAGE) continue;
            if (!child->name) continue;
            if (!strcasecmp(child->name, "NormalImage") || !strcasecmp(child->name, "HoverImage"))
                child->image = icon, stamped++;
        }
    }
    if (!logged_once) {
        fprintf(stderr, "SC2_HUD: stamped command icon textures on %d image frames\n", stamped);
        logged_once = true;
    }
}

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
    command_apply_icons(frames, count, root);
    SC2_HUD_WriteLayout(ent, frames, count, root, LAYER_COMMANDBAR);
}
