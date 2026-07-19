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
static BOOL cinematic_loaded;

static void CinematicEnsureLoaded(void) {
    if (cinematic_loaded) return;
    cinematic_loaded = true;
    CinematicPanel_Load(&cin);
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
    FLOAT x = pos ? pos->x : 0.0500f;
    FLOAT y = QUEST_MESSAGE_Y;
    LPCSTR message = NULL;

    (void)duration;
    if (!ent) return;
    if (x < 0.0f || x > UI_BASE_WIDTH) x = 0.0500f;

    UI_WriteStart(LAYER_MESSAGE);
    message = UI_FormatMessageText(UI_LevelStringSafe(text));
    UI_WriteTextAreaFrame(x, y, QUEST_MESSAGE_W, QUEST_MESSAGE_H,
                          message, COLOR32_WHITE, HUD_FONT_SIZE, 0.0f);
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
