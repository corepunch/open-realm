/*
 * hud_cinematic.c — Cinematic layer, interface toggle, message overlay.
 *
 * Manages the cinematic letterbox bars, portrait model, speaker/dialogue
 * text, the client_ui_state toggle, the text message overlay, and the
 * layer clear helper.
 */

#include "hud_local.h"
#include "hud_utils.h"
#include "../generated/cinematic_panel.h"

static CinematicPanel_t cin;
static FRAMEDEF msg_overlay_root, msg_overlay_text;
static BOOL cinematic_loaded;
static BOOL msg_overlay_loaded;

static void CinematicEnsureLoaded(void) {
    if (cinematic_loaded) return;
    cinematic_loaded = true;
    CinematicPanel_Load(&cin);
}

/* Construct the message overlay frame tree inline; no FDF needed. */
static BOOL MessageEnsureLoaded(void) {
    if (msg_overlay_loaded) return true;
    msg_overlay_loaded = true;
    UI_InitFrame(&msg_overlay_root, FT_FRAME);
    snprintf(msg_overlay_root.Name, sizeof(msg_overlay_root.Name), "OpenWarcraftMessageOverlay");
    UI_SetAllPoints(&msg_overlay_root);
    UI_InitFrame(&msg_overlay_text, FT_TEXTAREA);
    snprintf(msg_overlay_text.Name, sizeof(msg_overlay_text.Name), "OpenWarcraftMessageText");
    UI_SetParent(&msg_overlay_text, &msg_overlay_root);
    UI_SetSize(&msg_overlay_text, 0.30f, 0.145f);
    UI_SetPoint(&msg_overlay_text, FRAMEPOINT_TOPLEFT, &msg_overlay_root, FRAMEPOINT_TOPLEFT, 0.05f, -0.30f);
    msg_overlay_text.Font.Size = 0.010f;
    msg_overlay_text.Font.Index = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), HUD_FONT_SIZE);
    msg_overlay_text.TextArea.Inset = 0.0f;
    return true;
}

/* Copy the constructed frame so one player's runtime text/position never mutates the shared template. */
static FRAMEDEF MessageFrame(LPCVECTOR2 pos, LPCSTR message) {
    FRAMEDEF frame = msg_overlay_text;
    frame.Text = (LPSTR)message;
    frame.TextLength = strlen(message);
    if (pos && pos->x >= 0.0f && pos->x <= UI_BASE_WIDTH && pos->y >= 0.0f && pos->y <= UI_BASE_HEIGHT)
        UI_SetPoint(&frame, FRAMEPOINT_TOPLEFT, &msg_overlay_root, FRAMEPOINT_TOPLEFT, pos->x, -pos->y);
    return frame;
}

void UI_ClearLayer(LPEDICT ent, DWORD layer) {
    if (!ent) return;
    UI_WriteStart(layer);
    UI_WriteEnd(ent);
}

void UI_ShowInterface(LPEDICT ent, BOOL flag, FLOAT duration) {
    (void)duration;
    if (!ent || !ent->client) return;
    ent->client->ps.client_ui_state = flag ? CLIENT_UI_GAME : CLIENT_UI_CINEMATIC;
    if (flag)
        ent->client->ps.uiflags = 1 << LAYER_CINEMATIC;
    else
        ent->client->ps.uiflags = ~(1u << LAYER_CINEMATIC);
}

__attribute__((visibility("hidden"))) void UI_ShowMainMenu(LPEDICT ent) { (void)ent; }

void UI_ShowGameInterface(LPEDICT ent) {
    UI_WriteCinematicLayer(ent);
}

void UI_ShowText(LPEDICT ent, LPCVECTOR2 pos, LPCSTR text, FLOAT duration) {
    FRAMEDEF frame;
    LPCSTR message;

    (void)duration;
    if (!ent || !MessageEnsureLoaded()) return;
    message = UI_FormatMessageText(UI_LevelStringSafe(text));
    frame = MessageFrame(pos, message);

    UI_WriteStart(LAYER_MESSAGE);
    UI_WriteFrame(&msg_overlay_root);
    UI_WriteFrameWithChildren(&frame, &msg_overlay_root);
    UI_WriteEnd(ent);
}

void UI_WriteCinematicLayer(LPEDICT ent) {
    LPPLAYER ps;

    if (!ent || !ent->client) return;
    ps = &ent->client->ps;

    CinematicEnsureLoaded();

    BOOL has_portrait = ps->cinematic_portrait != 0;
    BOOL has_speaker = ps->texts[PLAYERTEXT_SPEAKER] && ps->texts[PLAYERTEXT_SPEAKER][0];
    BOOL has_dialogue = ps->texts[PLAYERTEXT_DIALOGUE] && ps->texts[PLAYERTEXT_DIALOGUE][0];
    BOOL has_scene = has_portrait || has_speaker || has_dialogue;

    /* Hide the whole scene panel only when there's nothing to show. */
    UI_SetHidden(cin.CinematicScenePanel, !has_scene);
    /* Hide portrait sub-frames individually when there's no portrait. */
    UI_SetHidden(cin.CinematicPortraitBackground, !has_portrait);
    UI_SetHidden(cin.CinematicPortrait, !has_portrait);
    UI_SetHidden(cin.CinematicPortraitCover, !has_portrait);

    if (has_portrait) {
        /* FT_PORTRAIT serialization reads Portrait.model; Texture.Image left the transmitted model at zero. */
        UI_SetPortraitFrameModel(cin.CinematicPortrait, ps->cinematic_portrait);
        cin.CinematicPortrait->Text = has_dialogue ? "Portrait Talk" : "Portrait";
    }

    if (has_speaker) {
        UI_SetText(cin.CinematicSpeakerText, "%s", ps->texts[PLAYERTEXT_SPEAKER]);
        cin.CinematicSpeakerText->Font.Color = MAKE(COLOR32, 252, 211, 18, 255);
    }

    if (has_dialogue) {
        cin.CinematicDialogueText->Stat = MAX_STATS + PLAYERTEXT_DIALOGUE;
        cin.CinematicDialogueText->Font.Color = COLOR32_WHITE;
    }

    UI_WriteLayout(ent, cin.CinematicPanel, LAYER_CINEMATIC);
}
