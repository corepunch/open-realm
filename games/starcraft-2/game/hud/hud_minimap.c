/*
 * hud_minimap.c — Minimap frame.
 *
 * The minimap has no .SC2Layout equivalent in the generic frame types,
 * so we build it programmatically — same as WC3's hud_console.c does
 * with UI_WriteMinimapFrame().
 */

#include "hud_local.h"

void SC2_WriteMinimapFrame(LPEDICT ent) {
    uiFrame_t frame;

    SC2_WriteStart(LAYER_CONSOLE);

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_MINIMAP;
    frame.color = COLOR32_WHITE;

    /* Bottom-left corner, 240x240 on 1600x1200 canvas */
    frame.points.x[FPP_MIN].used = 1;
    frame.points.x[FPP_MIN].targetPos = FPP_MIN;
    frame.points.x[FPP_MIN].relativeTo = UI_PARENT;
    frame.points.x[FPP_MIN].offset = 0.0f;
    frame.points.y[FPP_MIN].used = 1;
    frame.points.y[FPP_MIN].targetPos = FPP_MAX;
    frame.points.y[FPP_MIN].relativeTo = UI_PARENT;
    frame.points.y[FPP_MIN].offset = -240.0f / SC2_VIRT_H;
    frame.size.width  = 240.0f / SC2_VIRT_W;
    frame.size.height = 240.0f / SC2_VIRT_H;

    ui_next_frame_number++;
    frame.number = ui_next_frame_number;
    gi.Write(PF_UIFRAME, &frame);

    SC2_WriteEnd(ent);
}
