/*
 * hud_cinematic.c — Cinematic layer, gameplay transmissions, interface toggle,
 * and timed message overlay.
 *
 * SetCinematicScene is presentation state, not the UI-mode switch.  When the
 * normal interface is active a transmission temporarily owns the ordinary
 * portrait/message layers; when cinematic mode is active the same state is
 * rendered through CinematicPanel.
 */

#include "hud_local.h"
#include "hud_utils.h"

#define WC3_MESSAGE_CHARS_PER_SECOND 6.0f
#define WC3_MESSAGE_BASE_DURATION 5.0f

void UI_LoadHudCinematic(void) {
    if (hud.cinematic.CinematicPanel) return;
    CinematicPanel_Load(&hud.cinematic);
}

/* Construct the message overlay frame tree inline; no FDF needed. */
void UI_LoadHudMessage(void) {
    if (hud.msg_text.Name[0]) return;
    UI_InitFrame(&hud.msg_root, FT_FRAME);
    snprintf(hud.msg_root.Name, sizeof(hud.msg_root.Name), "OpenWarcraftMessageOverlay");
    UI_SetAllPoints(&hud.msg_root);
    UI_InitFrame(&hud.msg_text, FT_TEXTAREA);
    snprintf(hud.msg_text.Name, sizeof(hud.msg_text.Name), "OpenWarcraftMessageText");
    UI_SetParent(&hud.msg_text, &hud.msg_root);
    UI_SetSize(&hud.msg_text, 0.30f, 0.145f);
    UI_SetPoint(&hud.msg_text, FRAMEPOINT_TOPLEFT, &hud.msg_root, FRAMEPOINT_TOPLEFT, 0.05f, -0.30f);
    hud.msg_text.Font.Size = 0.010f;
    hud.msg_text.Font.Index = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), HUD_FONT_SIZE);
    hud.msg_text.TextArea.Inset = 0.0f;
}

/* Copy the constructed frame so one player's runtime text/position never mutates the shared template. */
static FRAMEDEF MessageFrame(LPCVECTOR2 pos, LPCSTR message) {
    FRAMEDEF frame = hud.msg_text;
    frame.Text = (LPSTR)message;
    frame.TextLength = strlen(message);
    /* WC3 DisplayTextToPlayer standard position is center-left (y=0.30 from screen top).
     * JASS y=0 is the baseline; positive y shifts the text upward.
     * WarSmash anchors messages at FramePoint.LEFT, y=0 (screen center) regardless of JASS y. */
    if (pos && pos->x >= 0.0f && pos->x <= UI_BASE_WIDTH && pos->y >= 0.0f && pos->y <= UI_BASE_HEIGHT)
        UI_SetPoint(&frame, FRAMEPOINT_TOPLEFT, &hud.msg_root, FRAMEPOINT_TOPLEFT, pos->x, -(0.30f - pos->y));
    return frame;
}

static BOOL HasTransmission(LPGAMECLIENT client) {
    LPPLAYER ps;

    if (!client) return false;
    ps = &client->ps;
    return ps->cinematic_portrait ||
           (ps->texts[PLAYERTEXT_SPEAKER] && ps->texts[PLAYERTEXT_SPEAKER][0]) ||
           (ps->texts[PLAYERTEXT_DIALOGUE] && ps->texts[PLAYERTEXT_DIALOGUE][0]);
}

static BOOL TransmissionTalking(LPGAMECLIENT client) {
    return client && client->cinematic_voice_end_time &&
           G_Time() < client->cinematic_voice_end_time;
}

static void WriteMessageLayer(LPEDICT ent, LPCVECTOR2 pos, LPCSTR message) {
    FRAMEDEF frame;

    if (!ent || !hud.msg_text.Name[0]) return;
    UI_WriteStart(LAYER_MESSAGE);
    if (message && *message) {
        frame = MessageFrame(pos, message);
        UI_WriteFrame(&hud.msg_root);
        UI_WriteFrameWithChildren(&frame, &hud.msg_root);
    }
    UI_WriteEnd(ent);
}

static void WriteStoredMessageLayer(LPEDICT ent) {
    LPGAMECLIENT client;

    if (!ent || !ent->client) return;
    client = ent->client;
    WriteMessageLayer(ent,
                      client->message.end_time ? &client->message.position : NULL,
                      client->message.end_time ? client->message.text : NULL);
}

static void WriteGameplayTransmissionPortrait(LPEDICT ent) {
    LPGAMECLIENT client;
    uiFrame_t frame;

    if (!ent || !ent->client) return;
    client = ent->client;

    UI_WriteStart(LAYER_PORTRAIT);
    if (client->ps.cinematic_portrait) {
        memset(&frame, 0, sizeof(frame));
        frame.flags.type = FT_PORTRAIT;
        frame.color = COLOR32_WHITE;
        frame.tex.index = client->ps.cinematic_portrait;
        frame.stat = client->ps.stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR];
        frame.text = TransmissionTalking(client) ? "Portrait Talk" : "Portrait";
        UI_SetFrameRect(&frame, 0.211f, 0.4865f, 0.0835f, 0.085f);
        UI_WriteProxyFrame(&frame, NULL, 0);
    }
    UI_WriteEnd(ent);
}

static void WriteGameplayTransmissionMessage(LPEDICT ent) {
    LPGAMECLIENT client;
    LPCSTR speaker, dialogue;
    char message[1200];

    if (!ent || !ent->client) return;
    client = ent->client;
    speaker = client->ps.texts[PLAYERTEXT_SPEAKER];
    dialogue = client->ps.texts[PLAYERTEXT_DIALOGUE];

    if (speaker && *speaker && dialogue && *dialogue) {
        snprintf(message, sizeof(message), "|cffffcc00%s:|r %s", speaker, dialogue);
    } else if (speaker && *speaker) {
        snprintf(message, sizeof(message), "|cffffcc00%s|r", speaker);
    } else {
        snprintf(message, sizeof(message), "%s", dialogue && *dialogue ? dialogue : "");
    }
    WriteMessageLayer(ent, NULL, UI_FormatMessageText(message));
}

void UI_ClearLayer(LPEDICT ent, DWORD layer) {
    if (!ent) return;
    UI_WriteStart(layer);
    UI_WriteEnd(ent);
}

void UI_InvalidateDialoguePresentation(LPEDICT ent) {
    if (ent && ent->client) ent->client->presentation_dirty = true;
}

void UI_WriteDialoguePresentation(LPEDICT ent) {
    LPGAMECLIENT client;

    if (!ent || !ent->client) return;
    client = ent->client;

    /* G_LoadMap normally binds both roots. Keep presentation APIs usable before
     * map load, matching the former per-panel lazy loaders. */
    if (!hud.cinematic.CinematicPanel) UI_LoadHudCinematic();
    if (!hud.msg_text.Name[0]) UI_LoadHudMessage();

    /* svc_layout is a server->client write and is not valid until ClientBegin
     * has completed.  JASS-facing state mutations retain presentation_dirty,
     * so skipping this transport write does not discard presentation state. */
    if (!client->connected) return;

    if (client->ps.client_ui_state == CLIENT_UI_CINEMATIC) {
        UI_WriteCinematicLayer(ent);
        return;
    }
    if (client->ps.client_ui_state != CLIENT_UI_GAME) return;

    if (HasTransmission(client)) {
        WriteGameplayTransmissionPortrait(ent);
        WriteGameplayTransmissionMessage(ent);
    } else {
        UI_WriteSelectedPortraitLayer(ent);
        WriteStoredMessageLayer(ent);
    }
}

void UI_ShowInterface(LPEDICT ent, BOOL flag, FLOAT duration) {
    (void)duration;
    if (!ent || !ent->client) return;
    ent->client->ps.client_ui_state = flag ? CLIENT_UI_GAME : CLIENT_UI_CINEMATIC;
    if (flag)
        ent->client->ps.uiflags = 1 << LAYER_CINEMATIC;
    else
        ent->client->ps.uiflags = ~(1u << LAYER_CINEMATIC);
    UI_InvalidateDialoguePresentation(ent);
}

void UI_ShowGameInterface(LPEDICT ent) {
    UI_WriteDialoguePresentation(ent);
    if (ent && ent->client && ent->client->connected)
        ent->client->presentation_dirty = false;
}

static void UI_ShowTextInternal(LPEDICT ent, LPCVECTOR2 pos, LPCSTR text, FLOAT duration,
                                BOOL record_in_log) {
    LPGAMECLIENT client;
    LPCSTR resolved, message;

    if (!ent || !ent->client) return;
    /* G_LoadMap eagerly binds the HUD, but JASS tests and startup callbacks can
     * reach this API before a map exists; preserve the old lazy-init contract. */
    if (!hud.msg_text.Name[0]) UI_LoadHudMessage();
    if (!hud.msg_text.Name[0]) return;
    client = ent->client;
    resolved = UI_LevelStringSafe(text);
    message = UI_FormatMessageText(resolved);

    if (duration < 0.0f)
        duration = (FLOAT)strlen(resolved) / WC3_MESSAGE_CHARS_PER_SECOND + WC3_MESSAGE_BASE_DURATION;
    if (duration <= 0.0f) {
        UI_ClearTextMessages(ent);
        return;
    }

    client->message.position = pos ? *pos : MAKE(VECTOR2, 0.05f, 0.0f);
    client->message.end_time = G_Time() + MAX(1u, (DWORD)(duration * 1000.0f));
    snprintf(client->message.text, sizeof(client->message.text), "%s", message);
    if (record_in_log) UI_MessageLogAppend(ent, client->message.text);

    /* A gameplay transmission owns LAYER_MESSAGE while active.  Preserve an
     * ordinary message started underneath it and reveal that message when the
     * transmission ends if its own lifetime has not expired. */
    if (client->ps.client_ui_state == CLIENT_UI_GAME && HasTransmission(client)) return;
    UI_InvalidateDialoguePresentation(ent);
}

void UI_ShowText(LPEDICT ent, LPCVECTOR2 pos, LPCSTR text, FLOAT duration) {
    UI_ShowTextInternal(ent, pos, text, duration, true);
}

void UI_ShowTransientText(LPEDICT ent, LPCVECTOR2 pos, LPCSTR text, FLOAT duration) {
    UI_ShowTextInternal(ent, pos, text, duration, false);
}

void UI_ClearTextMessages(LPEDICT ent) {
    LPGAMECLIENT client;

    if (!ent || !ent->client) return;
    client = ent->client;
    memset(&client->message, 0, sizeof(client->message));
    if (client->ps.client_ui_state == CLIENT_UI_GAME && HasTransmission(client)) return;
    UI_InvalidateDialoguePresentation(ent);
}

void UI_WriteCinematicLayer(LPEDICT ent) {
    LPGAMECLIENT client;
    LPPLAYER ps;

    if (!ent || !ent->client) return;
    client = ent->client;
    ps = &client->ps;

    if (!hud.cinematic.CinematicPanel) {
        /* A pre-map presentation has no authored cinematic root; clear the
         * client layer so the state transition still reaches the network. */
        UI_ClearLayer(ent, LAYER_CINEMATIC);
        return;
    }

    BOOL has_portrait = ps->cinematic_portrait != 0;
    BOOL has_speaker = ps->texts[PLAYERTEXT_SPEAKER] && ps->texts[PLAYERTEXT_SPEAKER][0];
    BOOL has_dialogue = ps->texts[PLAYERTEXT_DIALOGUE] && ps->texts[PLAYERTEXT_DIALOGUE][0];
    BOOL has_scene = has_portrait || has_speaker || has_dialogue;

    /* Hide the whole scene panel only when there's nothing to show. */
    UI_SetHidden(hud.cinematic.CinematicScenePanel, !has_scene);
    /* Hide portrait sub-frames individually when there's no portrait. */
    UI_SetHidden(hud.cinematic.CinematicPortraitBackground, !has_portrait);
    UI_SetHidden(hud.cinematic.CinematicPortrait, !has_portrait);
    UI_SetHidden(hud.cinematic.CinematicPortraitCover, !has_portrait);
    UI_SetHidden(hud.cinematic.CinematicSpeakerText, !has_speaker);
    UI_SetHidden(hud.cinematic.CinematicDialogueText, !has_dialogue);

    if (has_portrait) {
        /* FT_PORTRAIT serialization reads Portrait.model; Texture.Image left the transmitted model at zero. */
        UI_SetPortraitFrameModel(hud.cinematic.CinematicPortrait, ps->cinematic_portrait);
        hud.cinematic.CinematicPortrait->Stat = ps->stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR];
        hud.cinematic.CinematicPortrait->Text = TransmissionTalking(client) ? "Portrait Talk" : "Portrait";
    }

    if (has_speaker) {
        UI_SetText(hud.cinematic.CinematicSpeakerText, "%s", ps->texts[PLAYERTEXT_SPEAKER]);
        hud.cinematic.CinematicSpeakerText->Font.Color = MAKE(COLOR32, 252, 211, 18, 255);
    }

    if (has_dialogue) {
        hud.cinematic.CinematicDialogueText->Stat = MAX_STATS + PLAYERTEXT_DIALOGUE;
        hud.cinematic.CinematicDialogueText->Font.Color = COLOR32_WHITE;
    }

    UI_WriteLayout(ent, hud.cinematic.CinematicPanel, LAYER_CINEMATIC);
}
