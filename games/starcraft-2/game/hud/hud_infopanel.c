/*
 * hud_infopanel.c — SC2 info panel (selected unit name, wireframe, stats).
 *
 * Sends the InfoPanel subtree. Dynamic unit data (name, HP, shields) is
 * written by stamping .text / .stat on the relevant label frames before
 * calling SC2_HUD_WriteLayout.
 */

#include "hud.h"
#include "common/ui_constants.h"
#include <string.h>

static sc2BaseFrame_t *infopanel_find(void) {
    sc2BaseFrame_t *r = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_INFO_PANEL);
    if (r) return r;
    return SC2_HUD_FindFallbackFrameByType(SC2_FRAMETYPE_INFO_PANEL);
}

void SC2_HUD_WriteInfoPanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;
    sc2BaseFrame_t *root = infopanel_find();
    if (!root) return;
    SC2_HUD_WriteLayout(ent, frames, count, root, LAYER_INFOPANEL);
}

/*
 * PLACEHOLDER: SC2_HUD_WriteSelectedUnitUI
 *
 * This is a minimal programmatic unit card shown bottom-center when a unit
 * is selected.  It is NOT derived from the SC2 layout data — it exists only
 * to prove the selection→HUD pipeline works end-to-end.  Replace with a
 * proper InfoPanel binding once the full SC2 unit data structs are wired up.
 */
void SC2_HUD_WriteSelectedUnitUI(LPEDICT ent, int model_index, LPCSTR unit_name) {
    if (model_index <= 0) return;

    SC2_HUD_WriteStart(LAYER_INFOPANEL);

    /* Frame 1: bottom-center container, 320×80 px in SC2 coords (1600×1200). */
    static sc2BaseFrame_t container;
    memset(&container, 0, sizeof(container));
    container.number       = 1;
    container.parent_index = (DWORD)-1;
    container.type         = FT_FRAME;
    container.color        = (COLOR32){ 255, 255, 255, 200 };
    container.alpha        = 1.0f;
    container.size.width   = 320;
    container.size.height  = 80;
    /* Anchor: horizontal center, flush to bottom edge. */
    container.points.x[FPP_MIN].used      = 1;
    container.points.x[FPP_MIN].targetPos = FPP_MID;
    container.points.x[FPP_MIN].relative_index = (DWORD)-1;
    container.points.x[FPP_MIN].offset    = (int16_t)(-160 * UI_FRAMEPOINT_SCALE);
    container.points.y[FPP_MAX].used      = 1;
    container.points.y[FPP_MAX].targetPos = FPP_MAX;
    container.points.y[FPP_MAX].relative_index = (DWORD)-1;
    container.points.y[FPP_MAX].offset    = 0;
    SC2_HUD_WriteFrame(&container);

    /* Frame 2: portrait sub-frame (left side of the unit card). */
    static sc2BaseFrame_t portrait;
    memset(&portrait, 0, sizeof(portrait));
    portrait.number       = 2;
    portrait.parent_index = 1;
    portrait.type         = FT_PORTRAIT;
    portrait.image        = (DWORD)model_index;
    portrait.color        = (COLOR32){ 255, 255, 255, 255 };
    portrait.alpha        = 1.0f;
    portrait.size.width   = 80;
    portrait.size.height  = 80;
    portrait.points.x[FPP_MIN].used      = 1;
    portrait.points.x[FPP_MIN].targetPos = FPP_MIN;
    portrait.points.x[FPP_MIN].relative_index = (DWORD)-1;
    portrait.points.x[FPP_MIN].offset    = 0;
    portrait.points.y[FPP_MIN].used      = 1;
    portrait.points.y[FPP_MIN].targetPos = FPP_MIN;
    portrait.points.y[FPP_MIN].relative_index = (DWORD)-1;
    portrait.points.y[FPP_MIN].offset    = 0;
    SC2_HUD_WriteFrame(&portrait);

    /* Frame 3: unit name label (right of portrait). */
    if (unit_name && *unit_name) {
        static sc2BaseFrame_t name_label;
        memset(&name_label, 0, sizeof(name_label));
        name_label.number       = 3;
        name_label.parent_index = 1;
        name_label.type         = FT_TEXT;
        name_label.color        = (COLOR32){ 255, 255, 255, 255 };
        name_label.alpha        = 1.0f;
        name_label.size.width   = 230;
        name_label.size.height  = 24;
        name_label.stat         = 0;
        name_label.text         = unit_name;
        name_label.label.textalignx = FONT_JUSTIFYLEFT;
        name_label.label.textaligny = FONT_JUSTIFYMIDDLE;
        name_label.points.x[FPP_MIN].used      = 1;
        name_label.points.x[FPP_MIN].targetPos = FPP_MIN;
        name_label.points.x[FPP_MIN].relative_index = (DWORD)-1;
        name_label.points.x[FPP_MIN].offset    = (int16_t)(85 * UI_FRAMEPOINT_SCALE);
        name_label.points.y[FPP_MIN].used      = 1;
        name_label.points.y[FPP_MIN].targetPos = FPP_MIN;
        name_label.points.y[FPP_MIN].relative_index = (DWORD)-1;
        name_label.points.y[FPP_MIN].offset    = 0;
        SC2_HUD_WriteFrame(&name_label);
    }

    SC2_HUD_WriteEnd(ent);
}
