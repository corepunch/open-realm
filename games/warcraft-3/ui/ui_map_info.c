/*
 * ui_map_info.c - Warcraft III map info pane runtime layout.
 */

#include "ui_local.h"
#include "generated/map_info_pane.h"

static void UI_SetSizeIfPresent(LPFRAMEDEF frame, FLOAT width, FLOAT height) {
    if (frame) {
        UI_SetSize(frame, width, height);
    }
}

static void UI_SetPointIfPresent(LPFRAMEDEF frame,
                                 UIFRAMEPOINT point,
                                 LPCFRAMEDEF relative,
                                 UIFRAMEPOINT relative_point,
                                 FLOAT x,
                                 FLOAT y)
{
    if (frame && relative) {
        UI_SetPoint(frame, point, relative, relative_point, x, y);
    }
}

static void UI_SetHiddenIfPresent(LPFRAMEDEF frame, BOOL hidden) {
    if (frame) {
        UI_SetHidden(frame, hidden);
    }
}

void UI_LayoutMapInfoPane(LPFRAMEDEF frame) {
    MapInfoPane_t pane;
    FLOAT height;
    BOOL compact;

    if (!frame) {
        return;
    }
    if (!MapInfoPane_Bind(&pane, frame)) {
        return;
    }
    height = frame->Height;
    if (height <= 0.0f && frame->Parent) {
        height = frame->Parent->Height;
    }
    compact = height > 0.0f && height < 0.25f;

    UI_SetPointIfPresent(pane.MaxPlayersIcon, FRAMEPOINT_TOPLEFT, frame, FRAMEPOINT_TOPLEFT, 0.012f, -0.004f);
    UI_SetSizeIfPresent(pane.MapNameValue, 0.218f, 0.020f);
    UI_SetPointIfPresent(pane.MapNameValue, FRAMEPOINT_LEFT, pane.MaxPlayersIcon, FRAMEPOINT_RIGHT, 0.0025f, 0.0f);
    UI_SetHiddenIfPresent(pane.AuthIcon, true);

    UI_SetSizeIfPresent(pane.MinimapImage, 0.13125f, 0.13125f);
    UI_SetPointIfPresent(pane.MinimapImage, FRAMEPOINT_TOP, frame, FRAMEPOINT_TOP, 0.0f, -0.035f);
    UI_SetSizeIfPresent(pane.MinimapImageBackdrop, 0.183125f, 0.183125f);
    UI_SetPointIfPresent(pane.MinimapImageBackdrop, FRAMEPOINT_CENTER, pane.MinimapImage, FRAMEPOINT_CENTER, 0.0f, 0.002275f);

    UI_SetSizeIfPresent(pane.SuggestedPlayersLabel, 0.170f, 0.016f);
    UI_SetPointIfPresent(pane.SuggestedPlayersLabel,
                         FRAMEPOINT_TOPLEFT,
                         frame,
                         FRAMEPOINT_TOPLEFT,
                         0.012f,
                         compact ? -0.163f : -0.195f);
    UI_SetSizeIfPresent(pane.SuggestedPlayersValue, 0.126f, 0.016f);
    UI_SetPointIfPresent(pane.SuggestedPlayersValue,
                         FRAMEPOINT_TOPRIGHT,
                         frame,
                         FRAMEPOINT_TOPRIGHT,
                         -0.012f,
                         compact ? -0.163f : -0.195f);

    UI_SetSizeIfPresent(pane.MapSizeLabel, 0.112f, 0.016f);
    UI_SetPointIfPresent(pane.MapSizeLabel, FRAMEPOINT_TOPLEFT, pane.SuggestedPlayersLabel, FRAMEPOINT_BOTTOMLEFT, 0.0f, -0.002f);
    UI_SetSizeIfPresent(pane.MapSizeValue, 0.126f, 0.016f);
    UI_SetPointIfPresent(pane.MapSizeValue, FRAMEPOINT_TOPRIGHT, pane.SuggestedPlayersValue, FRAMEPOINT_BOTTOMRIGHT, 0.0f, -0.002f);

    UI_SetSizeIfPresent(pane.MapTilesetLabel, 0.112f, 0.016f);
    UI_SetPointIfPresent(pane.MapTilesetLabel, FRAMEPOINT_TOPLEFT, pane.MapSizeLabel, FRAMEPOINT_BOTTOMLEFT, 0.0f, -0.002f);
    UI_SetSizeIfPresent(pane.MapTilesetValue, 0.126f, 0.016f);
    UI_SetPointIfPresent(pane.MapTilesetValue, FRAMEPOINT_TOPRIGHT, pane.MapSizeValue, FRAMEPOINT_BOTTOMRIGHT, 0.0f, -0.002f);

    UI_SetHiddenIfPresent(pane.MapDescLabel, compact);
    UI_SetHiddenIfPresent(pane.MapDescValue, compact);
    if (!compact) {
        UI_SetSizeIfPresent(pane.MapDescLabel, 0.200f, 0.016f);
        UI_SetPointIfPresent(pane.MapDescLabel, FRAMEPOINT_TOPLEFT, pane.MapTilesetLabel, FRAMEPOINT_BOTTOMLEFT, 0.0f, -0.012f);
        UI_SetSizeIfPresent(pane.MapDescValue, 0.245f, 0.080f);
        UI_SetPointIfPresent(pane.MapDescValue, FRAMEPOINT_TOPLEFT, pane.MapDescLabel, FRAMEPOINT_BOTTOMLEFT, 0.0f, -0.002f);
    }
}
