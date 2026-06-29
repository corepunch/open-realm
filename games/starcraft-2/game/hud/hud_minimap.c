/*
 * hud_minimap.c — Minimap frame.
 *
 * The minimap has no .SC2Layout equivalent in the generic frame types,
 * so we build it programmatically — same as WC3's hud_console.c does
 * with UI_WriteMinimapFrame().
 */

#include "hud_local.h"

/* Write the minimap frame into the current svc_layout message.
   Caller must have already called SC2_WriteStart(). */
void SC2_WriteMinimapFrame(void) {
    uiFrame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_MINIMAP;
    frame.color = COLOR32_WHITE;

    /* Bottom-left corner, 240x240 on 1600x1200 canvas */
    frame.points.x[FPP_MIN].used = 1;
    frame.points.x[FPP_MIN].targetPos = FPP_MIN;
    frame.points.x[FPP_MIN].relativeTo = UI_PARENT;
    frame.points.x[FPP_MIN].offset = 0.0f;
    frame.points.y[FPP_MAX].used = 1;
    frame.points.y[FPP_MAX].targetPos = FPP_MAX;
    frame.points.y[FPP_MAX].relativeTo = UI_PARENT;
    frame.points.y[FPP_MAX].offset = 0.0f;
    frame.size.width  = SC2_NormX(240);
    frame.size.height = SC2_NormY(240);

    ui_next_frame_number++;
    frame.number = ui_next_frame_number;
    gi.Write(PF_UIFRAME, &frame);
}
