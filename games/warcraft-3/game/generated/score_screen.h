/* ScoreScreen.fdf named-frame bindings. Based on the retail RoC/TFT FDF
 * inventory; regenerate with fdfbindgen when the extracted source is available.
 * No Blizzard source/assets are bundled here. */
#ifndef WC3_SCORE_SCREEN_BINDINGS_H
#define WC3_SCORE_SCREEN_BINDINGS_H

#include "../g_local.h"

typedef struct {
    frameDef_t *ScoreScreenFrame;
    frameDef_t *ScoreScreenMainPanel;
    frameDef_t *ScoreScreenBackground;
    frameDef_t *ScoreScreenPageTab;
    frameDef_t *ScoreScreenRaceArt;
    frameDef_t *ScoreScreenBarTop;
    frameDef_t *ScoreScreen4ColumnFrame;
    frameDef_t *ScoreScreen5ColumnFrame;
    frameDef_t *ScoreScreen4ColumnHeader[4];
    frameDef_t *ScoreScreen5ColumnHeader[5];
    frameDef_t *ScoreScreenTabButton[4];
    frameDef_t *ScoreScreenTabText[4];
    frameDef_t *ScoreScreenTabTextSelected;
    frameDef_t *ScoreScreenOKButton;
    frameDef_t *ScoreScreenScrollBar;
    /* Retail column controls are siblings/parents of their header text. */
    frameDef_t *ScoreScreen4ColumnButton[4];
    frameDef_t *ScoreScreen5ColumnButton[5];
    frameDef_t *ScoreScreenTeamsButton; /* confirmed name in retail inventory */
    frameDef_t *ScoreScreenTeamColorButton; /* optional: exact FDF spelling pending source */
    frameDef_t *ScoreScreenSaveReplayButton;
    frameDef_t *ScoreScreenReplaySavedButton;
    frameDef_t *ScoreScreenElapsedValue; /* optional binding: extracted FDF not provided */
} ScoreScreen_t;

static inline bool ScoreScreen_Bind(ScoreScreen_t *out, frameDef_t *root) {
    bool ok = true;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->ScoreScreenFrame = root;
    if (!root) { BZ_FDF_REPORT_MISSING("ScoreScreenFrame"); return false; }
    BZ_FDF_BIND_CHILD(out, ScoreScreenMainPanel, root, "ScoreScreenMainPanel");
    BZ_FDF_BIND_CHILD(out, ScoreScreenBackground, root, "ScoreScreenBackground");
    BZ_FDF_BIND_CHILD(out, ScoreScreenPageTab, root, "ScoreScreenPageTab");
    BZ_FDF_BIND_CHILD(out, ScoreScreenRaceArt, root, "ScoreScreenRaceArt");
    BZ_FDF_BIND_CHILD(out, ScoreScreenBarTop, root, "ScoreScreenBarTop");
    BZ_FDF_BIND_CHILD(out, ScoreScreen4ColumnFrame, root, "ScoreScreen4ColumnFrame");
    BZ_FDF_BIND_CHILD(out, ScoreScreen5ColumnFrame, root, "ScoreScreen5ColumnFrame");
    BZ_FDF_BIND_CHILD(out, ScoreScreenTabTextSelected, root, "ScoreScreenTabTextSelected");
    BZ_FDF_BIND_CHILD(out, ScoreScreenOKButton, root, "ScoreScreenOkButton");
    BZ_FDF_BIND_CHILD(out, ScoreScreenScrollBar, root, "ScoreScreenScrollBar");
    out->ScoreScreenTeamsButton = UI_FindChildFrame(root, "ScoreScreenTeamsButton");
    /* The game-data agent confirmed a team-colour button, but supplied no
     * exact identifier. Keep this binding optional until raw FDF is provided. */
    out->ScoreScreenTeamColorButton = UI_FindChildFrame(root, "ScoreScreenTeamColorButton");
    out->ScoreScreenSaveReplayButton = UI_FindChildFrame(root, "ScoreScreenSaveReplayButton");
    out->ScoreScreenReplaySavedButton = UI_FindChildFrame(root, "ScoreScreenReplaySavedButton");
    out->ScoreScreenElapsedValue = UI_FindChildFrame(root, "ScoreScreenElapsedTimeValue");
    if (!out->ScoreScreenElapsedValue)
        out->ScoreScreenElapsedValue = UI_FindChildFrame(root, "ScoreScreenElapsedTimeText");
    for (uint32_t i = 0; i < 4; i++) {
        char name[48];
        snprintf(name, sizeof(name), "ScoreScreenTabButton%u", (unsigned)i);
        out->ScoreScreenTabButton[i] = UI_FindChildFrame(root, name);
        if (!out->ScoreScreenTabButton[i]) { BZ_FDF_REPORT_MISSING(name); ok = false; }
        snprintf(name, sizeof(name), "ScoreScreenTabText%u", (unsigned)i);
        out->ScoreScreenTabText[i] = UI_FindChildFrame(root, name);
        if (!out->ScoreScreenTabText[i]) { BZ_FDF_REPORT_MISSING(name); ok = false; }
        snprintf(name, sizeof(name), "ScoreScreen4ColumnHeader%u", (unsigned)i);
        out->ScoreScreen4ColumnHeader[i] = UI_FindChildFrame(root, name);
        snprintf(name, sizeof(name), "ScoreScreen4ColumnButton%u", (unsigned)i);
        out->ScoreScreen4ColumnButton[i] = UI_FindChildFrame(root, name);
        if (!out->ScoreScreen4ColumnButton[i]) { BZ_FDF_REPORT_MISSING(name); ok = false; }
        snprintf(name, sizeof(name), "ScoreScreen4ColumnHeader%u", (unsigned)i);
        if (!out->ScoreScreen4ColumnHeader[i]) { BZ_FDF_REPORT_MISSING(name); ok = false; }
    }
    for (uint32_t i = 0; i < 5; i++) {
        char name[48];
        snprintf(name, sizeof(name), "ScoreScreen5ColumnHeader%u", (unsigned)i);
        out->ScoreScreen5ColumnHeader[i] = UI_FindChildFrame(root, name);
        snprintf(name, sizeof(name), "ScoreScreen5ColumnButton%u", (unsigned)i);
        out->ScoreScreen5ColumnButton[i] = UI_FindChildFrame(root, name);
        if (!out->ScoreScreen5ColumnButton[i]) { BZ_FDF_REPORT_MISSING(name); ok = false; }
        snprintf(name, sizeof(name), "ScoreScreen5ColumnHeader%u", (unsigned)i);
        if (!out->ScoreScreen5ColumnHeader[i]) { BZ_FDF_REPORT_MISSING(name); ok = false; }
    }
    return ok;
}

static inline bool ScoreScreen_Load(ScoreScreen_t *out) {
    /* RoC localization may ship GlobalStrings in an overlay/locale MPQ. Its
     * absence must not reject an otherwise valid retail score FDF. */
    (void)UI_EnsureFDF("UI\\FrameDef\\GlobalStrings.fdf");
    return out &&
        UI_EnsureFDF("UI\\FrameDef\\Glue\\StandardTemplates.fdf") &&
        UI_EnsureFDF("UI\\FrameDef\\UI\\EscMenuTemplates.fdf") &&
        UI_EnsureFDF("UI\\FrameDef\\Glue\\ScoreScreen.fdf") &&
        ScoreScreen_Bind(out, UI_FindFrame("ScoreScreenFrame"));
}

#endif
