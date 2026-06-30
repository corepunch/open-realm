/*
 * hud.c — SC2 sc2BaseFrame_t → uiFrame_t serialization bridge.
 *
 * SC2 anchor model → uiFramePoint_t:
 *   SC2_SIDE_LEFT/RIGHT → x axis, SC2_SIDE_TOP/BOTTOM → y axis.
 *   SC2_POS_MIN/MID/MAX → FPP_MIN/FPP_MID/FPP_MAX targetPos.
 *   offset is stored in int16 scaled by UI_FRAMEPOINT_SCALE.
 *
 * Frame numbers are assigned sequentially as frames are written
 * (matching the WC3 pattern in hud.c / UI_ResetFrameWriteList).
 * parent_index == (DWORD)-1 means the frame is a root (parent = 0).
 */

#include "hud.h"
#include "client/ui.h"
#include <string.h>

/* Pull in the SC2 layout parser directly (it lives in the UI module but the
 * game module needs it for server-side parsing).  Unity build would only pick
 * up files under $(SC2_DIR)/game; including the .c here is the correct way to
 * bring in a single out-of-tree translation unit without polluting the ui/
 * directory listing. */
#include "games/starcraft-2/ui/sc2_layout.c"

/* uiimport — host services for sc2_layout.c when compiled into the game module.
 * gi.ReadFile signature (HANDLE, LPDWORD) differs from uiimport.FS_ReadFile
 * (int, void**), so we wrap it. */
uiImport_t uiimport;

static int sc2_hud_read_file(LPCSTR filename, void **buf) {
    DWORD size = 0;
    *buf = gi.ReadFile(filename, &size);
    return *buf ? (int)size : -1;
}

static void sc2_hud_free_file(void *buf) { gi.MemFree(buf); }

void SC2_HUD_InitLayoutHost(void) {
    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.FS_ReadFile = sc2_hud_read_file;
    uiimport.FS_FreeFile = sc2_hud_free_file;
}

/* ------------------------------------------------------------------ */
/* Frame numbering — mirrors WC3 UI_ResetFrameWriteList pattern */

#define SC2_MAX_FRAMES_WRITE 512
static DWORD frame_numbers[SC2_MAX_FRAMES_WRITE]; /* frame_numbers[i] = wire# for sc2BaseFrame[i] */
static DWORD num_frames_written;

static void reset_frame_write(void) {
    memset(frame_numbers, 0, sizeof(frame_numbers));
    num_frames_written = 0;
}

static DWORD assign_number(DWORD index) {
    if (index < SC2_MAX_FRAMES_WRITE && !frame_numbers[index])
        frame_numbers[index] = ++num_frames_written;
    return index < SC2_MAX_FRAMES_WRITE ? frame_numbers[index] : 0;
}

static DWORD lookup_number(DWORD index) {
    return (index != (DWORD)-1 && index < SC2_MAX_FRAMES_WRITE) ? frame_numbers[index] : 0;
}

/* ------------------------------------------------------------------ */
/* Anchor → uiFramePoint_t conversion */

static void copy_points(uiFrame_t *out, LPCSC2BASEFRAME frame) {
    for (int i = 0; i < FPP_COUNT; i++) {
        /* X axis */
        sc2BaseFramePoint_t const *px = &frame->points.x[i];
        if (px->used) {
            out->points.x[i].used      = 1;
            out->points.x[i].targetPos = px->targetPos;
            out->points.x[i].relativeTo = (px->relative_index != (DWORD)-1)
                                          ? (BYTE)lookup_number(px->relative_index)
                                          : UI_PARENT;
            out->points.x[i].offset = (int16_t)(px->offset * UI_FRAMEPOINT_SCALE);
        }
        /* Y axis */
        sc2BaseFramePoint_t const *py = &frame->points.y[i];
        if (py->used) {
            out->points.y[i].used      = 1;
            out->points.y[i].targetPos = py->targetPos;
            out->points.y[i].relativeTo = (py->relative_index != (DWORD)-1)
                                          ? (BYTE)lookup_number(py->relative_index)
                                          : UI_PARENT;
            out->points.y[i].offset = (int16_t)(py->offset * UI_FRAMEPOINT_SCALE);
        }
    }
}

/* ------------------------------------------------------------------ */

BOOL SC2_HUD_BuildFrameForWrite(LPCSC2BASEFRAME frame, uiFrame_t *out) {
    if (!frame || !out) return false;

    memset(out, 0, sizeof(*out));
    out->number = assign_number(frame->number);
    out->parent = (frame->parent_index != (DWORD)-1)
                  ? lookup_number(frame->parent_index)
                  : 0;
    out->color       = frame->color;
    out->size.width  = frame->size.width;
    out->size.height = frame->size.height;
    out->tex.index   = (USHORT)frame->image;
    out->flags.type  = frame->type;
    out->stat        = 0; /* dynamic panels override this */
    out->text        = frame->text;
    copy_points(out, frame);
    return true;
}

void SC2_HUD_WriteFrame(LPCSC2BASEFRAME frame) {
    uiFrame_t tmp;
    if (SC2_HUD_BuildFrameForWrite(frame, &tmp))
        gi.Write(PF_UIFRAME, &tmp);
}

void SC2_HUD_WriteFrameWithChildren(LPCSC2BASEFRAME frames, DWORD count,
                                    LPCSC2BASEFRAME frame) {
    if (!frame || (frame->ui_flags & SC2_UIFLAG_HIDDEN)) return;
    SC2_HUD_WriteFrame(frame);
    for (DWORD i = 0; i < count; i++) {
        if (frames[i].parent_index == frame->number &&
            !(frames[i].ui_flags & SC2_UIFLAG_HIDDEN))
            SC2_HUD_WriteFrameWithChildren(frames, count, &frames[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Shared layout load — one SC2_LayoutBuildGameUI() for all panels */

static BOOL layout_loaded;
static BOOL layout_ok;

sc2BaseFrame_t *SC2_HUD_EnsureLayout(DWORD *count) {
    if (!layout_loaded) {
        layout_loaded = true;
        layout_ok = SC2_LayoutBuildGameUI();
    }
    if (!layout_ok) {
        if (count) *count = 0;
        return NULL;
    }
    return SC2_LayoutGetFrames(count);
}

/* ------------------------------------------------------------------ */

void SC2_HUD_WriteStart(DWORD layer) {
    reset_frame_write();
    gi.Write(PF_BYTE, &(LONG){ svc_layout });
    gi.Write(PF_BYTE, &(LONG){ layer });
}

void SC2_HUD_WriteEnd(LPEDICT ent) {
    gi.Write(PF_LONG,  &(LONG){ 0 });  /* bits=0  — frame terminator */
    gi.Write(PF_SHORT, &(LONG){ 0 });  /* number=0 */
    gi.unicast(ent);
}

void SC2_HUD_WriteLayout(LPEDICT ent, LPCSC2BASEFRAME frames, DWORD count,
                         LPCSC2BASEFRAME root, DWORD layer) {
    SC2_HUD_WriteStart(layer);
    SC2_HUD_WriteFrameWithChildren(frames, count, root);
    SC2_HUD_WriteEnd(ent);
}
