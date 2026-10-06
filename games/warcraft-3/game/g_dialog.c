/* Generic Warcraft III JASS choice dialogs.  The universal client only renders
 * svc_window frames and returns an opaque choice; ownership remains in game.dll. */
#include "g_local.h"
#include "hud/hud_local.h"

static bool DialogIsPlayer(player_t const *player, uint32_t *number) {
    if (!player || !number || !game.clients) return false;
    FOR_LOOP(i, game.max_clients) if (player == &game.clients[i].ps && i < 32) {
        if (player->number >= 32) return false;
        *number = player->number;
        return true;
    }
    return false;
}

jassDialog_t *G_JassDialogById(uint32_t id) {
    if (!id || id > level.dialog_count || id > MAX_JASS_DIALOGS) return NULL;
    jassDialog_t *dialog = &level.dialogs[id - 1];
    return dialog->inuse && dialog->id == id ? dialog : NULL;
}

jassDialogButton_t *G_JassDialogButtonById(uint32_t id) {
    if (!id || id > level.dialog_button_count || id > MAX_JASS_DIALOG_BUTTONS) return NULL;
    jassDialogButton_t *button = &level.dialog_buttons[id - 1];
    return button->inuse && button->id == id ? button : NULL;
}

jassDialog_t *G_JassDialog(handle_t value) {
    uintptr_t address = (uintptr_t)value, base = (uintptr_t)level.dialogs;
    if (!value || address < base || address >= base + sizeof(level.dialogs) ||
        (address - base) % sizeof(level.dialogs[0])) return NULL;
    jassDialog_t *dialog = value;
    return G_JassDialogById(dialog->id) == dialog ? dialog : NULL;
}

jassDialogButton_t *G_JassDialogButton(handle_t value) {
    uintptr_t address = (uintptr_t)value, base = (uintptr_t)level.dialog_buttons;
    if (!value || address < base || address >= base + sizeof(level.dialog_buttons) ||
        (address - base) % sizeof(level.dialog_buttons[0])) return NULL;
    jassDialogButton_t *button = value;
    return G_JassDialogButtonById(button->id) == button ? button : NULL;
}

jassDialog_t *G_JassDialogCreate(void) {
    if (level.dialog_count >= MAX_JASS_DIALOGS) return NULL;
    jassDialog_t *dialog = &level.dialogs[level.dialog_count++];
    memset(dialog, 0, sizeof(*dialog));
    dialog->id = level.dialog_count;
    dialog->inuse = true;
    return dialog;
}

jassDialogButton_t *G_JassDialogAddButton(jassDialog_t *dialog, cstring_t label,
        int32_t hotkey, bool quit, bool score_screen) {
    if (!G_JassDialog(dialog) || level.dialog_button_count >= MAX_JASS_DIALOG_BUTTONS) return NULL;
    jassDialogButton_t *button = &level.dialog_buttons[level.dialog_button_count++];
    memset(button, 0, sizeof(*button));
    button->inuse = true;
    button->id = level.dialog_button_count;
    button->dialog_id = dialog->id;
    button->hotkey = hotkey;
    button->quit = quit;
    button->score_screen = score_screen;
    snprintf(button->text, sizeof(button->text), "%s", label ? G_LevelString(label) : "");
    return button;
}

void G_JassDialogClear(jassDialog_t *dialog) {
    if (!G_JassDialog(dialog)) return;
    FOR_LOOP(i, game.max_clients) if (i < 32 && (dialog->visible_players & (1u << i))) {
        edict_t *ent = G_GetPlayerEntityByNumber(i);
        if (ent && ent->client) UI_JassDialogHide(ent);
    }
    dialog->visible_players = 0;
    FOR_LOOP(i, level.dialog_button_count) {
        jassDialogButton_t *button = &level.dialog_buttons[i];
        if (button->dialog_id == dialog->id) button->inuse = false;
    }
    dialog->message[0] = '\0';
    /* A displayed dialog is rebuilt only on a subsequent DialogDisplay call. */
}

void G_JassDialogDestroy(jassDialog_t *dialog) {
    if (!G_JassDialog(dialog)) return;
    G_JassDialogClear(dialog);
    dialog->inuse = false;
    dialog->visible_players = 0;
}

void G_JassDialogDisplay(player_t *player, jassDialog_t *dialog, bool visible) {
    uint32_t index;
    if (!G_JassDialog(dialog) || !DialogIsPlayer(player, &index)) return;
    if (visible) {
        /* The client has a single authoritative choice window per player. */
        FOR_LOOP(i, level.dialog_count) if (level.dialogs[i].id != dialog->id)
            level.dialogs[i].visible_players &= ~(1u << index);
        dialog->visible_players |= 1u << index;
    } else {
        bool was_visible = (dialog->visible_players & (1u << index)) != 0;
        dialog->visible_players &= ~(1u << index);
        if (was_visible) {
            edict_t *ent = G_GetPlayerEntityByNumber(index);
            if (ent && ent->client) UI_JassDialogHide(ent);
        }
    }
    if (visible) {
        edict_t *ent = G_GetPlayerEntityByNumber(player->number);
        if (ent && ent->client) UI_JassDialogShow(ent, dialog);
    }
}

void G_JassDialogClick(edict_t *ent, uint32_t dialog_id, uint32_t button_id) {
    jassDialog_t *dialog = G_JassDialogById(dialog_id);
    jassDialogButton_t *button = G_JassDialogButtonById(button_id);
    uint32_t player;
    if (!ent || !ent->client || !dialog || !button || button->dialog_id != dialog_id ||
        !DialogIsPlayer(&ent->client->ps, &player) ||
        !(dialog->visible_players & (1u << player))) return;
    dialog->visible_players &= ~(1u << player); /* reject duplicate commands */
    FOR_EACH_EVENT(registration) {
        if (!registration->trigger || registration->dialog_id != dialog_id) continue;
        if (registration->type != EVENT_DIALOG_CLICK &&
            registration->type != EVENT_DIALOG_BUTTON_CLICK) continue;
        if (registration->button_id && registration->button_id != button_id) continue;
        gameEvent_t *event = G_PublishEvent(NULL, registration->type);
        if (!event) break;
        event->responseTo = registration;
        event->dialog_id = dialog_id;
        event->button_id = button_id;
        event->dialog_player = player + 1;
    }
    /* Quit buttons are tracked as distinct choices; retail end-game policy
     * must be implemented separately, not guessed from a client UI callback. */
}

static void DialogStyleText(frameDef_t *frame) {
    if (!frame) return;
    if (!frame->Font.Index)
        frame->Font.Index = gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
    if (!frame->Font.Color.a) frame->Font.Color = COLOR32_WHITE;
}

static void DialogApplyBackdrop(frameDef_t *root) {
    frameDef_t *backdrop;
    if (!root) return;

    backdrop = root->DialogBackdropName[0]
        ? UI_FindChildFrame(root, root->DialogBackdropName) : NULL;
    if (!backdrop) {
        backdrop = UI_Spawn(FT_BACKDROP, root);
        if (!backdrop) return;
        snprintf(backdrop->Name, sizeof(backdrop->Name), "JassChoiceDialogBackdrop");
    }

    /* ScriptDialog is not present in every Warcraft data set. Always author
     * the same race-skinned chrome used by the in-game menu, including when
     * its FDF template exists but leaves the backdrop unskinned. */
    backdrop->Type = FT_BACKDROP;
    backdrop->Backdrop.Background = UI_LoadTexture("EscMenuBackground", true);
    backdrop->Backdrop.EdgeFile = UI_LoadTexture("EscMenuBorder", true);
    backdrop->DecorateFileNames = true;
    UI_SetPoint(backdrop, FRAMEPOINT_TOPLEFT, root, FRAMEPOINT_TOPLEFT, 0, 0);
    UI_SetPoint(backdrop, FRAMEPOINT_BOTTOMRIGHT, root, FRAMEPOINT_BOTTOMRIGHT, 0, 0);
    root->DialogBackdrop = backdrop;
    snprintf(root->DialogBackdropName, sizeof(root->DialogBackdropName), "%s", backdrop->Name);
}

/* Client windows accept a server-authored FDF frame tree. The ScriptDialog
 * template provides Warcraft styling when it is present in the game archives. */
void UI_JassDialogShow(edict_t *ent, jassDialog_t const *dialog) {
    frameDef_t *root, *message, *source;
    uint32_t n = 0;
    if (!ent || !ent->client || !dialog) return;
    UI_EnsureFDF("UI\\FrameDef\\UI\\ScriptDialog.fdf");
    UI_EnsureFDF("UI\\FrameDef\\Glue\\StandardTemplates.fdf");
    source = UI_FindFrame("ScriptDialog");
    root = source ? UI_CloneFrameTree(source, NULL) : UI_Spawn(FT_DIALOG, NULL);
    if (!root) return;
    snprintf(root->Name, sizeof(root->Name), "JassChoiceDialog");
    UI_SetSize(root, 0.36f, 0.28f);
    DialogApplyBackdrop(root);
    UI_CenterFrame(root);
    message = UI_FindFrameNear(root, "ScriptDialogText");
    if (!message) message = UI_Spawn(FT_TEXT, root);
    if (!message) return;
    snprintf(message->Name, sizeof(message->Name), "JassChoiceMessage");
    UI_SetSize(message, 0.30f, 0.06f);
    UI_SetPoint(message, FRAMEPOINT_TOP, root, FRAMEPOINT_TOP, 0, -0.025f);
    DialogStyleText(message);
    UI_SetText(message, "%s", dialog->message);
    FOR_LOOP(i, level.dialog_button_count) {
        jassDialogButton_t const *entry = &level.dialog_buttons[i];
        frameDef_t *button, *text;
        if (!entry->inuse || entry->dialog_id != dialog->id || n >= 12) continue;
        source = UI_FindFrame("ScriptDialogButton");
        if (!source) source = UI_FindFrame("StandardButtonTemplate");
        button = source ? UI_CloneFrameTree(source, root) : UI_Spawn(FT_GLUETEXTBUTTON, root);
        if (!button) break;
        snprintf(button->Name, sizeof(button->Name), "JassChoiceButton%u", n);
        UI_SetSize(button, 0.24f, 0.027f);
        UI_SetPoint(button, FRAMEPOINT_TOP, root, FRAMEPOINT_TOP, 0, -0.10f - n * 0.032f);
        text = button->Button.NormalText.frame[0]
            ? UI_FindFrameNear(button, button->Button.NormalText.frame)
            : NULL;
        if (!text) text = UI_Spawn(FT_TEXT, button);
        if (text) {
            snprintf(text->Name, sizeof(text->Name), "JassChoiceButtonText%u", n);
            UI_SetSize(text, 0.23f, 0.027f);
            UI_SetPoint(text, FRAMEPOINT_CENTER, button, FRAMEPOINT_CENTER, 0, 0);
            DialogStyleText(text);
            UI_SetText(text, "%s", entry->text);
            snprintf(button->Button.NormalText.frame, sizeof(button->Button.NormalText.frame), "%s", text->Name);
        }
        UI_SetOnClick(button, UI_WINDOW_CLOSE_COMMAND_PREFIX "jassdialog %u %u", dialog->id, entry->id);
        ++n;
    }
    UI_SetSize(root, 0.36f, MAX(0.17f, 0.115f + n * 0.032f));
    UI_SetCurrentClient(ent->client);
    UI_WriteWindow(ent, root, &MAKE(uiWindowDef_t, .id = WC3_JASS_DIALOG_WINDOW,
        .class_id = WC3_JASS_DIALOG_WINDOW,
        .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE | UI_WINDOW_NO_PAUSE | UI_WINDOW_NO_ESCAPE));
    UI_SetCurrentClient(NULL);
}

void UI_JassDialogHide(edict_t *ent) {
    if (!ent || !ent->client) return;
    gi.Write(PF_BYTE, &(int32_t){svc_window});
    gi.Write(PF_BYTE, &(int32_t){UI_WINDOW_CLOSE});
    gi.Write(PF_LONG, &(uint32_t){WC3_JASS_DIALOG_WINDOW});
    gi.unicast(ent);
}

void UI_JassDialogRestore(edict_t *ent) {
    uint32_t player;
    if (!ent || !ent->client || !DialogIsPlayer(&ent->client->ps, &player)) return;
    FOR_LOOP(i, level.dialog_count) {
        jassDialog_t *dialog = &level.dialogs[i];
        if (dialog->inuse && (dialog->visible_players & (1u << player))) {
            UI_JassDialogShow(ent, dialog);
            return;
        }
    }
}
