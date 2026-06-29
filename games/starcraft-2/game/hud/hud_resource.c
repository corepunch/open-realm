/*
 * hud_resource.c — Resource panel (minerals, vespene gas, supply).
 *
 * Loads layout from ResourcePanel.SC2Layout (via SC2_EnsureLayout),
 * sets stat bindings for dynamic data, and sends via svc_layout.
 * Falls back to a programmatic layout if the .SC2Layout file is unavailable.
 */

#include "hud_local.h"
#include "../generated/resource_panel.h"

static ResourcePanel_t res;
static BOOL res_loaded;
static BOOL res_from_layout;
static SC2FRAMEDEF fallback_resource_frame;
static SC2FRAMEDEF fallback_minerals_icon;
static SC2FRAMEDEF fallback_minerals_text;
static SC2FRAMEDEF fallback_gas_icon;
static SC2FRAMEDEF fallback_gas_text;
static SC2FRAMEDEF fallback_supply_text;
static SC2FRAMEDEF fallback_supply_max_text;
static SC2FRAMEDEF fallback_supply_icon;

static void ResourceEnsureLoaded(void) {
    if (res_loaded) return;
    res_loaded = true;

    if (ResourcePanel_Load(&res)) {
        res_from_layout = true;
        if (res.MineralsText) res.MineralsText->Stat = PLAYERSTATE_RESOURCE_GOLD;
        if (res.GasText)      res.GasText->Stat = PLAYERSTATE_RESOURCE_LUMBER;
        if (res.SupplyText)   res.SupplyText->Stat = PLAYERSTATE_RESOURCE_FOOD_USED;
        if (res.SupplyMaxText) res.SupplyMaxText->Stat = PLAYERSTATE_RESOURCE_FOOD_CAP;
        return;
    }

    res_from_layout = false;
    fprintf(stderr, "SC2_HUD: ResourcePanel.SC2Layout not found, using fallback\n");

    /* Point the ResourcePanel_t pointers at static fallback storage so SC2_* calls
       receive a valid (non-NULL) LPSC2FRAMEDEF. */
    res.ResourcePanelFrame = &fallback_resource_frame;
    res.MineralsIcon       = &fallback_minerals_icon;
    res.MineralsText       = &fallback_minerals_text;
    res.GasIcon            = &fallback_gas_icon;
    res.GasText            = &fallback_gas_text;
    res.SupplyText         = &fallback_supply_text;
    res.SupplyMaxText      = &fallback_supply_max_text;
    res.SupplyIcon         = &fallback_supply_icon;

    SC2_InitFrame(res.ResourcePanelFrame, FT_FRAME);
    SC2_SetPoint(res.ResourcePanelFrame, FPP_MIN, FPP_MIN, NULL, FPP_MIN, 0.375f, 0.0f);
    SC2_SetSize(res.ResourcePanelFrame, 0.25f, 0.025f);

    SC2_InitFrame(res.MineralsIcon, FT_TEXTURE);
    SC2_SetParent(res.MineralsIcon, res.ResourcePanelFrame);
    SC2_SetPoint(res.MineralsIcon, FPP_MIN, FPP_MIN, res.ResourcePanelFrame, FPP_MIN, 0.0f, 0.0f);
    SC2_SetSize(res.MineralsIcon, 0.02f, 0.02f);

    SC2_InitFrame(res.MineralsText, FT_TEXT);
    SC2_SetParent(res.MineralsText, res.ResourcePanelFrame);
    SC2_SetPoint(res.MineralsText, FPP_MIN, FPP_MIN, res.MineralsIcon, FPP_MAX, 0.002f, 0.0f);
    SC2_SetSize(res.MineralsText, 0.04f, 0.02f);

    SC2_InitFrame(res.GasIcon, FT_TEXTURE);
    SC2_SetParent(res.GasIcon, res.ResourcePanelFrame);
    SC2_SetPoint(res.GasIcon, FPP_MIN, FPP_MIN, res.MineralsText, FPP_MAX, 0.01f, 0.0f);
    SC2_SetSize(res.GasIcon, 0.02f, 0.02f);

    SC2_InitFrame(res.GasText, FT_TEXT);
    SC2_SetParent(res.GasText, res.ResourcePanelFrame);
    SC2_SetPoint(res.GasText, FPP_MIN, FPP_MIN, res.GasIcon, FPP_MAX, 0.002f, 0.0f);
    SC2_SetSize(res.GasText, 0.04f, 0.02f);

    SC2_InitFrame(res.SupplyText, FT_TEXT);
    SC2_SetParent(res.SupplyText, res.ResourcePanelFrame);
    SC2_SetPoint(res.SupplyText, FPP_MIN, FPP_MIN, res.GasText, FPP_MAX, 0.015f, 0.0f);
    SC2_SetSize(res.SupplyText, 0.04f, 0.02f);

    SC2_InitFrame(res.SupplyMaxText, FT_TEXT);
    SC2_SetParent(res.SupplyMaxText, res.ResourcePanelFrame);
    SC2_SetPoint(res.SupplyMaxText, FPP_MIN, FPP_MIN, res.SupplyText, FPP_MAX, 0.002f, 0.0f);
    SC2_SetSize(res.SupplyMaxText, 0.04f, 0.02f);

    res.MineralsText->Stat = PLAYERSTATE_RESOURCE_GOLD;
    res.GasText->Stat = PLAYERSTATE_RESOURCE_LUMBER;
    res.SupplyText->Stat = PLAYERSTATE_RESOURCE_FOOD_USED;
    res.SupplyMaxText->Stat = PLAYERSTATE_RESOURCE_FOOD_CAP;
}

void SC2_WriteResourcePanel(LPEDICT ent) {
    ResourceEnsureLoaded();
    if (!res.ResourcePanelFrame) return;

    if (res_from_layout) {
        SC2_WriteLayout(ent, res.ResourcePanelFrame, LAYER_CONSOLE);
    } else {
        SC2_WriteStart(LAYER_CONSOLE);
        SC2_WriteFrame(res.ResourcePanelFrame);
        SC2_WriteFrame(res.MineralsIcon);
        SC2_WriteFrame(res.MineralsText);
        SC2_WriteFrame(res.GasIcon);
        SC2_WriteFrame(res.GasText);
        SC2_WriteFrame(res.SupplyText);
        SC2_WriteFrame(res.SupplyMaxText);
        SC2_WriteFrame(res.SupplyIcon);
        SC2_WriteEnd(ent);
    }
}
