/*
 * hud_portrait.c — SC2 portrait panel (selected unit 3D model view).
 *
 * Called by SC2_Select after the selection bitmask changes.  Unhides the
 * PortraitPanel and stamps the model index of the first selected unit into
 * the portrait frame so SCR_LayoutDrawPortrait can render it.
 *
 * If no unit is selected the PortraitPanel is hidden so the blank portrait
 * background rectangle does not show.
 */

#include "hud.h"
#include <string.h>

static sc2BaseFrame_t *portrait_panel_find(void) {
    sc2BaseFrame_t *r = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_PORTRAIT_PANEL);
    if (r) return r;
    return SC2_HUD_FindFallbackFrameByType(SC2_FRAMETYPE_PORTRAIT_PANEL);
}

void SC2_HUD_WritePortraitPanel(LPEDICT ent, int model_index) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;
    sc2BaseFrame_t *root = portrait_panel_find();
    if (!root) return;

    if (model_index <= 0) {
        /* No selection — hide portrait. */
        root->ui_flags |= SC2_UIFLAG_HIDDEN;
        SC2_HUD_WriteLayout(ent, frames, count, root, LAYER_PORTRAIT);
        return;
    }

    root->ui_flags &= ~SC2_UIFLAG_HIDDEN;

    /* Stamp the model slot into every FT_PORTRAIT child frame so the
     * client knows which model to render. */
    for (DWORD i = 0; i < count; i++) {
        if (frames[i].type == FT_PORTRAIT) {
            DWORD p = i;
            while (p != (DWORD)-1 && p < count) {
                if (frames[p].number == root->number) {
                    frames[i].image = (DWORD)model_index;
                    break;
                }
                p = frames[p].parent_index;
            }
        }
    }

    SC2_HUD_WriteLayout(ent, frames, count, root, LAYER_PORTRAIT);
}
