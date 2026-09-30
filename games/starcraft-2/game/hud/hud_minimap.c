#include "hud.h"

void SC2_HUD_PrepareMinimap(edict_t const *client, sc2BaseFrame_t *root) {
    (void)client;
    static struct { cstring_t name, command, tip; } const controls[]={
        {"PingButton","minimapping","Ping minimap"},
        {"TerrainButton","minimapterrain","Toggle minimap terrain"},
        {"ColorButton","minimapcolors","Toggle alliance colors"},
        {"ClearSelectionButton","select 0","Clear selection"},
    };
    FOR_LOOP(i,(sizeof(controls)/sizeof(*controls))) {
        sc2BaseFrame_t *button=SC2_HUD_Find(root,controls[i].name);
        if (!button) continue;
        button->onclick=controls[i].command; button->tooltip=controls[i].tip;
        sc2BaseFrame_t *hover=SC2_HUD_Find(button,"HoverImage");
        if (hover) hover->ui_flags|=SC2_UIFLAG_HIDDEN;
    }
    sc2BaseFrame_t *observer=SC2_HUD_Find(root,"ClearSelectionButton"),*background=SC2_HUD_Find(root,"ClearSelectionBackground");
    /* Native clear-selection is observer-only; normal play clears by selecting the ground. */
    if (observer) observer->ui_flags|=SC2_UIFLAG_HIDDEN;
    if (background) background->ui_flags|=SC2_UIFLAG_HIDDEN;
}
