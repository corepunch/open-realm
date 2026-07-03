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

/* Pull in the SC2 layout parser (single-header library). */
#include "games/starcraft-2/common/stb_sc2layout.h"
#include "common/ui_constants.h"

#include "hud.h"
#include "client/ui.h"
#include <string.h>

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

/* SC2 layout resources are CTexture catalog keys.  The file-backed subset
 * uses the default CTexture convention: Assets/Textures/##id##.dds.
 * Resources prefixed UI/ are engine-internal (dynamic render targets, atlas
 * regions, skin textures) — return 0 for unresolved entries. */
static int sc2_hud_image_index(LPCSTR resource) {
    static struct { LPCSTR logical, physical; } const paths[] = {
        { "UI/ResourceIcon0", "Assets/Textures/icon-mineral.dds" },
        { "UI/ResourceIcon1", "Assets/Textures/icon-gas.dds" },
        { "UI/ResourceIcon2", "Assets/Textures/icon-highyieldmineral.dds" },
        { "UI/ResourceIcon3", "Assets/Textures/icon-mineral.dds" },
        { "UI/ResourceIconSupply", "Assets/Textures/icon-supply.dds" },
        { "UI/ResourceIconPlayer", "Assets/Textures/ui_ingame_resourcesharing_playericon.dds" },
    };
    while (*resource == '@') resource++;
    FOR_LOOP(i, sizeof(paths) / sizeof(*paths))
        if (!strcasecmp(resource, paths[i].logical)) return gi.ImageIndex(paths[i].physical);
    if (!strncasecmp(resource, "UI/", 3)) return 0;
    return gi.ImageIndex(resource);
}

void SC2_HUD_InitLayoutHost(void) {
    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.FS_ReadFile = sc2_hud_read_file;
    uiimport.FS_FreeFile = sc2_hud_free_file;
    uiimport.ImageIndex = sc2_hud_image_index;
    uiimport.FontIndex = gi.FontIndex;
}

/* ------------------------------------------------------------------ */
/* Frame numbering — flat wire[] map, (DWORD)-1 = unassigned */

#define SC2_MAX_FRAMES_WRITE 512
static DWORD frame_to_wire[SC2_MAX_FRAMES_WRITE];
static DWORD num_frames_written;

static void reset_frame_write(void) {
    for (int i = 0; i < SC2_MAX_FRAMES_WRITE; i++)
        frame_to_wire[i] = (DWORD)-1;
    num_frames_written = 0;
}

static DWORD get_wire(DWORD index) {
    return (index < SC2_MAX_FRAMES_WRITE && frame_to_wire[index] != (DWORD)-1)
           ? frame_to_wire[index] : 0;
}

static DWORD assign_number(DWORD index) {
    if (index < SC2_MAX_FRAMES_WRITE && frame_to_wire[index] == (DWORD)-1)
        frame_to_wire[index] = ++num_frames_written;
    return get_wire(index);
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
            out->points.x[i].relativeTo = (BYTE)((px->relative_index != (DWORD)-1)
                                          ? get_wire(px->relative_index)
                                          : UI_PARENT);
            out->points.x[i].offset = (int16_t)(px->offset * UI_FRAMEPOINT_SCALE);
        }
        /* Y axis */
        sc2BaseFramePoint_t const *py = &frame->points.y[i];
        if (py->used) {
            out->points.y[i].used      = 1;
            out->points.y[i].targetPos = py->targetPos;
            out->points.y[i].relativeTo = (BYTE)((py->relative_index != (DWORD)-1)
                                          ? get_wire(py->relative_index)
                                          : UI_PARENT);
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
                  ? get_wire(frame->parent_index)
                  : 0;
    out->color       = frame->color;
    out->color.a     = (BYTE)(out->color.a * frame->alpha);
    out->size.width  = frame->size.width;
    out->size.height = frame->size.height;
    out->tex.index   = (USHORT)frame->image;
    out->flags.type  = frame->type;
    out->stat        = frame->stat;
    out->text        = frame->text;
    if (frame->type == FT_TEXT) {
        out->buffer.size = sizeof(frame->label);
        out->buffer.data = (HANDLE)&frame->label;
    }
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

/* Walk the parent chain of 'frame' to the root and write each ancestor
 * once (root first), so every parent has a smaller wire number than its
 * children.  Already-assigned frames are silently skipped by assign_number. */
void SC2_HUD_WriteAncestors(LPCSC2BASEFRAME frames, DWORD count,
                             LPCSC2BASEFRAME frame) {
    if (!frame || frame->parent_index == (DWORD)-1) return;
    LPCSC2BASEFRAME parent = &frames[frame->parent_index];
    SC2_HUD_WriteAncestors(frames, count, parent);
    if (!(parent->ui_flags & SC2_UIFLAG_HIDDEN))
        SC2_HUD_WriteFrame(parent);
}

/* ------------------------------------------------------------------ */
/* Shared layout load — one SC2_LayoutBuildGameUI() for all panels */

static BOOL layout_loaded;
static BOOL layout_ok;

/* ------------------------------------------------------------------ */
/* Fallback frame builder — used when no SC2 layout data is available */

#define SC2_FB_MAX 64
static sc2BaseFrame_t sc2_fb_frames[SC2_FB_MAX];
static DWORD sc2_fb_count;
static BOOL sc2_fb_built;

/* Fallback builder uses native SC2 pixel coords (1600×1200). */
static sc2BaseFrame_t *fb_add(DWORD parent_index, sc2FrameType sc2_type) {
    if (sc2_fb_count >= SC2_FB_MAX) return NULL;
    sc2BaseFrame_t *f = &sc2_fb_frames[sc2_fb_count];
    memset(f, 0, sizeof(*f));
    f->number = sc2_fb_count;
    f->type = SC2_MapFrameType(sc2_type);
    f->sc2_type = sc2_type;
    f->parent_index = parent_index;
    f->color = (COLOR32){ 255, 255, 255, 255 };
    f->alpha = 1.0f;
    f->label.textaligny = FONT_JUSTIFYMIDDLE;
    if (uiimport.FontIndex)
        f->label.font = (RESOURCE)uiimport.FontIndex("UI/Fonts/EurostileExt-Med.otf", 16);
    sc2_fb_count++;
    return f;
}

static void fb_anchor(sc2BaseFrame_t *f, BOOL is_x, int side, int target, int offset_px) {
    sc2BaseFramePoint_t *p = is_x ? &f->points.x[side] : &f->points.y[side];
    p->used = 1;
    p->targetPos = (uiFramePointPos_t)target;
    p->relative_index = (DWORD)-1;
    p->offset = is_x ? (FLOAT)offset_px : -(FLOAT)offset_px;
}

static BOOL SC2_HUD_BuildFallbackLayout(void) {
    if (sc2_fb_built) return true;
    sc2_fb_built = true;
    sc2_fb_count = 0;

    /* Frame 0: root (fills scene) */
    sc2BaseFrame_t *root = fb_add((DWORD)-1, SC2_FRAMETYPE_GAME_UI);
    if (!root) return false;
    fb_anchor(root, 1, FPP_MIN, FPP_MIN, 0);
    fb_anchor(root, 1, FPP_MAX, FPP_MAX, 0);
    fb_anchor(root, 0, FPP_MIN, FPP_MIN, 0);
    fb_anchor(root, 0, FPP_MAX, FPP_MAX, 0);

    /* Frame 1: console backdrop — full-width bar at bottom */
    sc2BaseFrame_t *console = fb_add(0, SC2_FRAMETYPE_CONSOLE_PANEL);
    if (!console) return false;
    console->size.width  = 1600;
    console->size.height = 260;
    fb_anchor(console, 1, FPP_MIN, FPP_MIN, 0);
    fb_anchor(console, 1, FPP_MAX, FPP_MAX, 0);
    fb_anchor(console, 0, FPP_MAX, FPP_MAX, 0);
    console->points.y[FPP_MIN].used = 0;

    /* Frame 2: console background texture */
    sc2BaseFrame_t *bg = fb_add(1, SC2_FRAMETYPE_IMAGE);
    if (!bg) return false;
    bg->size.width  = 1600;
    bg->size.height = 260;
    bg->alpha = 0.65f;
    fb_anchor(bg, 1, FPP_MIN, FPP_MIN, 0);
    fb_anchor(bg, 1, FPP_MAX, FPP_MAX, 0);
    fb_anchor(bg, 0, FPP_MIN, FPP_MIN, 0);
    fb_anchor(bg, 0, FPP_MAX, FPP_MAX, 0);

    /* Frame 3: minimap */
    sc2BaseFrame_t *mm = fb_add(1, SC2_FRAMETYPE_MINIMAP);
    if (!mm) return false;
    mm->type = FT_MINIMAP;
    mm->size.width  = 240;
    mm->size.height = 240;
    fb_anchor(mm, 1, FPP_MIN, FPP_MIN, 0);
    fb_anchor(mm, 0, FPP_MAX, FPP_MAX, 0);

    /* Frame 4: resource panel container (top-right) */
    sc2BaseFrame_t *res = fb_add(0, SC2_FRAMETYPE_RESOURCE_PANEL);
    if (!res) return false;
    res->size.width  = 400;
    res->size.height = 40;
    fb_anchor(res, 1, FPP_MAX, FPP_MAX, -16);
    fb_anchor(res, 0, FPP_MIN, FPP_MIN, 16);

    /* Frame 5: mineral label — PLAYERSTATE_RESOURCE_GOLD = 2 */
    sc2BaseFrame_t *gold = fb_add(4, SC2_FRAMETYPE_LABEL);
    if (!gold) return false;
    gold->type = FT_TEXT;
    gold->stat = PLAYERSTATE_RESOURCE_GOLD;
    gold->text = "0";
    gold->size.width  = 80;
    gold->size.height = 20;
    gold->label.textalignx = FONT_JUSTIFYRIGHT;
    fb_anchor(gold, 1, FPP_MAX, FPP_MAX, -4);
    fb_anchor(gold, 0, FPP_MIN, FPP_MIN, 4);

    /* Frame 6: vespene label — PLAYERSTATE_RESOURCE_LUMBER = 3 */
    sc2BaseFrame_t *gas = fb_add(4, SC2_FRAMETYPE_LABEL);
    if (!gas) return false;
    gas->type = FT_TEXT;
    gas->stat = PLAYERSTATE_RESOURCE_LUMBER;
    gas->text = "0";
    gas->size.width  = 80;
    gas->size.height = 20;
    gas->label.textalignx = FONT_JUSTIFYRIGHT;
    fb_anchor(gas, 1, FPP_MAX, FPP_MAX, -4);
    fb_anchor(gas, 0, FPP_MIN, FPP_MIN, 24);

    /* Frame 7: supply label — PLAYERSTATE_RESOURCE_FOOD_USED = 5 */
    sc2BaseFrame_t *supply = fb_add(4, SC2_FRAMETYPE_LABEL);
    if (!supply) return false;
    supply->type = FT_TEXT;
    supply->stat = PLAYERSTATE_RESOURCE_FOOD_USED;
    supply->text = "0/0";
    supply->size.width  = 80;
    supply->size.height = 20;
    supply->label.textalignx = FONT_JUSTIFYRIGHT;
    fb_anchor(supply, 1, FPP_MAX, FPP_MAX, -4);
    fb_anchor(supply, 0, FPP_MIN, FPP_MIN, 44);

    fprintf(stderr, "SC2_HUD: built %u fallback frames\n", (unsigned)sc2_fb_count);
    return true;
}

sc2BaseFrame_t *SC2_HUD_FindFallbackFrameByType(sc2FrameType type) {
    for (DWORD i = 0; i < sc2_fb_count; i++)
        if (sc2_fb_frames[i].sc2_type == type) return &sc2_fb_frames[i];
    return NULL;
}

sc2BaseFrame_t *SC2_HUD_EnsureLayout(DWORD *count) {
    if (!layout_loaded) {
        layout_loaded = true;
        layout_ok = SC2_LayoutBuildGameUI();
    }
    if (layout_ok) {
        return SC2_LayoutGetFrames(count);
    }
    /* Fallback when SC2 data is unavailable */
    if (SC2_HUD_BuildFallbackLayout()) {
        if (count) *count = sc2_fb_count;
        return sc2_fb_frames;
    }
    if (count) *count = 0;
    return NULL;
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
    SC2_HUD_WriteStart(layer);   /* resets num_frames_written to 0 */
    SC2_HUD_WriteFrameWithChildren(frames, count, root);
    SC2_HUD_WriteEnd(ent);
}
