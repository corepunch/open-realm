/*
 * hud_resource.c — Resource panel (minerals, vespene gas, supply).
 *
 * Loads UI/Layout/UI/ResourcePanel.SC2Layout at runtime and wires the
 * dynamic labels to player-state stats.  No hand-written/generated binding
 * struct is required: frames are looked up by name after parsing.
 */

#include "hud_local.h"

static LPSC2FRAMEDEF root;
static BOOL res_loaded;
static BOOL res_from_layout;
static SC2FRAMEDEF fallback_resource_frame;
static SC2FRAMEDEF fallback_minerals_icon;
static SC2FRAMEDEF fallback_minerals_text;
static SC2FRAMEDEF fallback_gas_icon;
static SC2FRAMEDEF fallback_gas_text;
static SC2FRAMEDEF fallback_supply_text;

static DWORD hud_font_index;

static void SetupTextLabel(LPSC2FRAMEDEF frame) {
    if (!frame) return;
    if (!hud_font_index)
        hud_font_index = SC2_FontIndex("UI/Fonts/EurostileExt-Med.otf", 16);
    frame->font.index = hud_font_index;
    frame->font.color = COLOR32_WHITE;
    frame->font.justify.vertical = FONT_JUSTIFYMIDDLE;
}

static LPSC2FRAMEDEF GetChild(LPCSC2FRAMEDEF parent, LPCSTR name) {
    LPSC2FRAMEDEF child = SC2_FindChildFrame(parent, name);
    if (!child) fprintf(stderr, "SC2_HUD: missing frame '%s' under ResourcePanel\n", name);
    return child;
}

static void ResourceEnsureLoaded(void) {
    if (res_loaded) return;
    res_loaded = true;

    if (SC2_EnsureLayout("UI/Layout/UI/ResourcePanel.SC2Layout")) {
        res_from_layout = true;
        root = SC2_FindFrame("ResourcePanelTemplate");
        if (!root) {
            fprintf(stderr, "SC2_HUD: ResourcePanelTemplate not found\n");
            res_from_layout = false;
            goto fallback;
        }
        /* Root has no explicit anchors in the .SC2Layout template; fill the
           scene so children can anchor against it predictably. */
        SC2_SetAllPoints(root);

        LPSC2FRAMEDEF label0 = GetChild(root, "ResourceLabel0");
        LPSC2FRAMEDEF label1 = GetChild(root, "ResourceLabel1");
        LPSC2FRAMEDEF label2 = GetChild(root, "ResourceLabel2");
        LPSC2FRAMEDEF label3 = GetChild(root, "ResourceLabel3");
        LPSC2FRAMEDEF supply = GetChild(root, "SupplyLabel");

        if (label0) { label0->Stat = PLAYERSTATE_RESOURCE_GOLD;          SetupTextLabel(label0); }
        if (label1) { label1->Stat = PLAYERSTATE_RESOURCE_LUMBER;        SetupTextLabel(label1); }
        if (label2) { label2->Stat = PLAYERSTATE_RESOURCE_HERO_TOKENS;   SetupTextLabel(label2); }
        if (label3) { label3->Stat = 0; label3->Text = "";               SetupTextLabel(label3); }
        if (supply) { supply->Stat = PLAYERSTATE_RESOURCE_FOOD_USED;     SetupTextLabel(supply); }
        return;
    }

fallback:
    res_from_layout = false;
    fprintf(stderr, "SC2_HUD: ResourcePanel.SC2Layout not found, using fallback\n");

    root                      = &fallback_resource_frame;
    LPSC2FRAMEDEF icon0       = &fallback_minerals_icon;
    LPSC2FRAMEDEF text0       = &fallback_minerals_text;
    LPSC2FRAMEDEF icon1       = &fallback_gas_icon;
    LPSC2FRAMEDEF text1       = &fallback_gas_text;
    LPSC2FRAMEDEF supply_text = &fallback_supply_text;

    SC2_InitFrame(root, FT_FRAME);
    SC2_SetAllPoints(root);

    SC2_InitFrame(icon0, FT_TEXTURE);
    SC2_SetParent(icon0, root);
    SC2_SetPoint(icon0, FPP_MIN, FPP_MIN, root, FPP_MIN, 0.01f, 0.01f);
    SC2_SetSize(icon0, 0.02f, 0.02f);

    SC2_InitFrame(text0, FT_TEXT);
    SC2_SetParent(text0, root);
    SC2_SetPoint(text0, FPP_MIN, FPP_MIN, icon0, FPP_MAX, 0.002f, 0.0f);
    SC2_SetSize(text0, 0.04f, 0.02f);

    SC2_InitFrame(icon1, FT_TEXTURE);
    SC2_SetParent(icon1, root);
    SC2_SetPoint(icon1, FPP_MIN, FPP_MIN, text0, FPP_MAX, 0.01f, 0.0f);
    SC2_SetSize(icon1, 0.02f, 0.02f);

    SC2_InitFrame(text1, FT_TEXT);
    SC2_SetParent(text1, root);
    SC2_SetPoint(text1, FPP_MIN, FPP_MIN, icon1, FPP_MAX, 0.002f, 0.0f);
    SC2_SetSize(text1, 0.04f, 0.02f);

    SC2_InitFrame(supply_text, FT_TEXT);
    SC2_SetParent(supply_text, root);
    SC2_SetPoint(supply_text, FPP_MAX, FPP_MIN, root, FPP_MAX, -0.01f, 0.01f);
    SC2_SetSize(supply_text, 0.04f, 0.02f);

    text0->Stat      = PLAYERSTATE_RESOURCE_GOLD;
    text1->Stat      = PLAYERSTATE_RESOURCE_LUMBER;
    supply_text->Stat = PLAYERSTATE_RESOURCE_FOOD_USED;
    SetupTextLabel(text0);
    SetupTextLabel(text1);
    SetupTextLabel(supply_text);
}

/* Write resource panel frames into the current svc_layout message.
   Caller must have already called SC2_WriteStart(). */
void SC2_WriteResourcePanelFrames(void) {
    ResourceEnsureLoaded();
    if (!root) return;

    if (res_from_layout) {
        SC2_WriteFrameWithChildren(root);
    } else {
        SC2_WriteFrame(root);
        SC2_WriteFrame(&fallback_minerals_icon);
        SC2_WriteFrame(&fallback_minerals_text);
        SC2_WriteFrame(&fallback_gas_icon);
        SC2_WriteFrame(&fallback_gas_text);
        SC2_WriteFrame(&fallback_supply_text);
    }
}
