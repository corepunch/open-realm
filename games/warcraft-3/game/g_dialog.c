/* Generic Warcraft III JASS choice dialogs.  The universal client only renders
 * svc_window frames and returns an opaque choice; ownership remains in game.dll. */
#include "g_local.h"
#include "hud/hud_local.h"
#include "generated/script_dialog.h"
#include "generated/script_dialog_button.h"

static bool dialog_fdf_warning_printed, button_fdf_warning_printed;

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

/* Store optional presentation metadata without growing this native's argument list. */
jassDialogButton_t *G_JassDialogAddButton(jassDialog_t *dialog, cstring_t label,
        jassDialogButtonOptions_t const *options) {
    if (!G_JassDialog(dialog) || level.dialog_button_count >= MAX_JASS_DIALOG_BUTTONS) return NULL;
    jassDialogButton_t *button = &level.dialog_buttons[level.dialog_button_count++];
    memset(button, 0, sizeof(*button));
    button->inuse = true;
    button->id = level.dialog_button_count;
    button->dialog_id = dialog->id;
    if (options) {
        button->hotkey = options->hotkey;
        button->quit = options->quit;
        button->score_screen = options->score_screen;
    }
    snprintf(button->text, sizeof(button->text), "%s", label ? G_LevelString(label) : "");
    return button;
}

void G_JassDialogClear(jassDialog_t *dialog) {
    if (!G_JassDialog(dialog)) return;
    FOR_LOOP(i, game.max_clients) {
        uint32_t number = game.clients[i].ps.number;
        if (number < 32 && (dialog->visible_players & (1u << number))) {
            edict_t *ent = G_GetPlayerEntityByNumber(number);
            if (ent && ent->client) UI_JassDialogHide(ent);
        }
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

/* Supply race-skinned chrome and bounds for the dialog's backdrop. */
static bool DialogApplyBackdrop(frameDef_t *root, frameDef_t *backdrop) {
    if (!root) return false;

    if (!backdrop) {
        backdrop = UI_Spawn(FT_BACKDROP, root);
        if (!backdrop) return false;
        snprintf(backdrop->Name, sizeof(backdrop->Name), "JassChoiceDialogBackdrop");
    }

    /* ScriptDialog is not present in every Warcraft data set. Always author
     * the same race-skinned chrome used by the in-game menu, including when
     * its FDF template exists but leaves the backdrop unskinned. */
    backdrop->Type = FT_BACKDROP;
    backdrop->Backdrop.Background = UI_LoadTexture("EscMenuBackground", true);
    backdrop->Backdrop.EdgeFile = UI_LoadTexture("EscMenuBorder", true);
    backdrop->DecorateFileNames = true;
    /* HACK: retail ScriptDialog FDF omits backdrop anchors; the client requires explicit bounds to fill the dialog. */
    UI_SetPoint(backdrop, FRAMEPOINT_TOPLEFT, root, FRAMEPOINT_TOPLEFT, 0, 0);
    UI_SetPoint(backdrop, FRAMEPOINT_BOTTOMRIGHT, root, FRAMEPOINT_BOTTOMRIGHT, 0, 0);
    root->DialogBackdrop = backdrop;
    snprintf(root->DialogBackdropName, sizeof(root->DialogBackdropName), "%s", backdrop->Name);
    return true;
}

/* Client windows accept a server-authored FDF frame tree. The ScriptDialog
 * template provides Warcraft styling when it is present in the game archives. */
void UI_JassDialogShow(edict_t *ent, jassDialog_t const *dialog) {
    frameDef_t *root = NULL, *message, *source, *previous = NULL;
    ScriptDialog_t dialog_frames, clone_frames;
    ScriptDialogButton_t button_template, button_frames;
    bool stock_dialog, stock_button;
    float content_height = 0.10f;
    uint32_t n = 0, omitted = 0;
    if (!ent || !ent->client || !dialog) return;
    stock_dialog = UI_EnsureFDF("UI\\FrameDef\\UI\\ScriptDialog.fdf") && ScriptDialog_Load(&dialog_frames);
    stock_button = stock_dialog && ScriptDialogButton_Load(&button_template);
    if (!stock_dialog && !dialog_fdf_warning_printed) {
        fprintf(stderr, "WC3 JASS dialog: ScriptDialog FDF unavailable; using native frame types\n");
        dialog_fdf_warning_printed = true;
    }
    if (stock_dialog && !stock_button && !button_fdf_warning_printed) {
        fprintf(stderr, "WC3 JASS dialog: ScriptDialogButton FDF unavailable; using native button frames\n");
        button_fdf_warning_printed = true;
    }
    source = stock_dialog ? dialog_frames.ScriptDialog : NULL;
    root = source ? UI_CloneFrameTree(source, NULL) : UI_Spawn(FT_DIALOG, NULL);
    if (!root) {
        fprintf(stderr, "WC3 JASS dialog %u: failed to allocate a dialog frame\n", dialog->id);
        return;
    }
    snprintf(root->Name, sizeof(root->Name), "JassChoiceDialog");
    if (source && !ScriptDialog_Bind(&clone_frames, root)) {
        fprintf(stderr, "WC3 JASS dialog %u: cloned ScriptDialog binding is incomplete\n", dialog->id);
        goto cleanup;
    }
    if (!root->Width || !root->Height) {
        /* TODO: retail ScriptDialog dimensions are absent in some data sets; retain a stock-shaped native fallback. */
        UI_SetSize(root, 0.288f, 0.17f);
    }
    if (!DialogApplyBackdrop(root, source ? clone_frames.ScriptDialogBackdrop : NULL)) {
        fprintf(stderr, "WC3 JASS dialog %u: failed to allocate backdrop\n", dialog->id);
        goto cleanup;
    }
    UI_CenterFrame(root);
    message = source ? clone_frames.ScriptDialogText : NULL;
    if (!message) message = UI_Spawn(FT_TEXT, root);
    if (!message) {
        fprintf(stderr, "WC3 JASS dialog %u: failed to allocate message text\n", dialog->id);
        goto cleanup;
    }
    snprintf(message->Name, sizeof(message->Name), "JassChoiceMessage");
    DialogStyleText(message);
    UI_SetText(message, "%s", dialog->message);
    FOR_LOOP(i, level.dialog_button_count) {
        jassDialogButton_t const *entry = &level.dialog_buttons[i];
        frameDef_t *button, *text;
        if (!entry->inuse || entry->dialog_id != dialog->id) continue;
        if (n >= MAX_JASS_DIALOG_UI_BUTTONS) { ++omitted; continue; }
        if (stock_button) button = UI_CloneFrameTree(button_template.ScriptDialogButton, root);
        else button = UI_Spawn(FT_GLUETEXTBUTTON, root);
        if (!button) {
            fprintf(stderr, "WC3 JASS dialog %u: failed to allocate button %u\n", dialog->id, entry->id);
            goto cleanup;
        }
        if (stock_button && !ScriptDialogButton_Bind(&button_frames, button)) {
            fprintf(stderr, "WC3 JASS dialog %u: cloned button %u binding is incomplete\n", dialog->id, entry->id);
            goto cleanup;
        }
        snprintf(button->Name, sizeof(button->Name), "JassChoiceButton%u", n);
        if (!button->Width || !button->Height) {
            /* TODO: this fallback applies only when no button dimensions exist in the loaded stock FDF. */
            UI_SetSize(button, 0.159f, 0.031f);
        }
        /* HACK: ScriptDialog FDF supplies a button template but no row container or dynamic anchors. */
        if (previous) UI_SetPoint(button, FRAMEPOINT_TOP, previous, FRAMEPOINT_BOTTOM, 0, -0.004f);
        else UI_SetPoint(button, FRAMEPOINT_TOP, root, FRAMEPOINT_TOP, 0, -0.10f);
        content_height += button->Height + 0.004f;
        previous = button;
        text = stock_button ? button_frames.ScriptDialogButtonText : NULL;
        if (!text) text = UI_Spawn(FT_TEXT, button);
        if (!text) {
            fprintf(stderr, "WC3 JASS dialog %u: failed to allocate label for button %u\n", dialog->id, entry->id);
            goto cleanup;
        }
        snprintf(text->Name, sizeof(text->Name), "JassChoiceButtonText%u", n);
        UI_SetPoint(text, FRAMEPOINT_CENTER, button, FRAMEPOINT_CENTER, 0, 0);
        DialogStyleText(text);
        UI_SetText(text, "%s", entry->text);
        snprintf(button->Button.NormalText.frame, sizeof(button->Button.NormalText.frame), "%s", text->Name);
        UI_SetOnClick(button, UI_WINDOW_CLOSE_COMMAND_PREFIX "jassdialog %u %u", dialog->id, entry->id);
        ++n;
    }
    if (omitted) fprintf(stderr, "WC3 JASS dialog %u: omitted %u button(s); UI capacity is 12\n", dialog->id, omitted);
    /* HACK: expand the template around its generated rows; stock FDF has no variable-height choice layout. */
    UI_SetSize(root, root->Width, MAX(root->Height, content_height + 0.012f));
    UI_SetCurrentClient(ent->client);
    UI_WriteWindow(ent, root, &MAKE(uiWindowDef_t, .id = WC3_JASS_DIALOG_WINDOW,
        .class_id = WC3_JASS_DIALOG_WINDOW,
        .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE | UI_WINDOW_NO_PAUSE | UI_WINDOW_NO_ESCAPE));
    UI_SetCurrentClient(NULL);
cleanup:
    UI_FreeFrameTree(root);
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
