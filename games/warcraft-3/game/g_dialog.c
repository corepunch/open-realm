/* Generic Warcraft III JASS choice dialogs.  The universal client only renders
 * svc_window frames and returns an opaque choice; ownership remains in game.dll. */
#include "g_local.h"
#include "hud/hud_local.h"
#include "generated/script_dialog.h"
#include "generated/script_dialog_button.h"

/* Common initial sequence of jassDialog_t and jassDialogButton_t, so one allocator serves both pools. */
typedef struct {
    bool inuse;
    uint32_t id;
} dialogSlot_t;

_Static_assert(offsetof(jassDialog_t, id) == offsetof(dialogSlot_t, id), "dialog slot layout");
_Static_assert(offsetof(jassDialogButton_t, id) == offsetof(dialogSlot_t, id), "button slot layout");
_Static_assert(MAX_JASS_DIALOG_BUTTONS < 1u << JASS_DIALOG_SLOT_BITS, "slot index must fit below the generation");

static bool dialog_fdf_warning_printed, button_fdf_warning_printed;

static bool DialogDebugEnabled(void) {
    return gi.CvarString && atoi(gi.CvarString("wc3_dialog_debug", "0")) != 0;
}

static void DialogDebug(cstring_t format, ...) {
    va_list args;
    if (!DialogDebugEnabled()) return;
    fprintf(stderr, "WC3_DIALOG ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

static bool DialogIsPlayer(player_t const *player, uint32_t *number) {
    if (!player || !number || !game.clients) return false;
    FOR_LOOP(i, game.max_clients) if (player == &game.clients[i].ps && i < 32) {
        if (player->number >= 32) return false;
        *number = player->number;
        return true;
    }
    return false;
}

/* Slot 0 in the low bits wraps to UINT32_MAX and fails the bound; the full id must match the live generation. */
static uint32_t DialogSlot(uint32_t id) { return (id & ((1u << JASS_DIALOG_SLOT_BITS) - 1)) - 1; }

jassDialog_t *G_JassDialogById(uint32_t id) {
    uint32_t slot = DialogSlot(id);
    if (slot >= level.dialog_count || slot >= MAX_JASS_DIALOGS) return NULL;
    jassDialog_t *dialog = &level.dialogs[slot];
    return dialog->inuse && dialog->id == id ? dialog : NULL;
}

jassDialogButton_t *G_JassDialogButtonById(uint32_t id) {
    uint32_t slot = DialogSlot(id);
    if (slot >= level.dialog_button_count || slot >= MAX_JASS_DIALOG_BUTTONS) return NULL;
    jassDialogButton_t *button = &level.dialog_buttons[slot];
    return button->inuse && button->id == id ? button : NULL;
}

/* Reuse the first free slot (append otherwise) and advance its generation, so ids of a released occupant held by
 * registrations, events, saves or a late client click never match the new one. Previously Clear/Destroy only
 * cleared inuse and the pools were append-only, so Clear+AddButton loops silently ran out of handles. */
static void *DialogAllocSlot(void *base, size_t size, uint32_t *count, uint32_t max, cstring_t kind) {
    uint32_t i = 0;
    while (i < *count && ((dialogSlot_t *)((uint8_t *)base + i * size))->inuse) ++i;
    if (i >= max) {
        fprintf(stderr, "WC3 JASS dialog: all %u %s slots are in use\n", max, kind);
        return NULL;
    }
    dialogSlot_t *slot = (dialogSlot_t *)((uint8_t *)base + i * size);
    uint32_t gen = (slot->id >> JASS_DIALOG_SLOT_BITS) + 1;
    memset(slot, 0, size);
    slot->inuse = true;
    slot->id = (gen << JASS_DIALOG_SLOT_BITS) | (i + 1);
    *count = MAX(*count, i + 1);
    return slot;
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
    jassDialog_t *dialog = DialogAllocSlot(level.dialogs, sizeof(*level.dialogs), &level.dialog_count, MAX_JASS_DIALOGS, "dialog");
    DialogDebug("create dialog=%u success=%u", dialog ? dialog->id : 0, dialog != NULL);
    return dialog;
}

void G_JassDialogSetMessage(jassDialog_t *dialog, cstring_t message) {
    if (!G_JassDialog(dialog)) {
        DialogDebug("set_message rejected reason=invalid_dialog");
        return;
    }
    snprintf(dialog->message, sizeof(dialog->message), "%s", message ? G_LevelString(message) : "");
    DialogDebug("set_message dialog=%u message=\"%s\"", dialog->id, dialog->message);
}

/* Store optional presentation metadata without growing this native's argument list. */
jassDialogButton_t *G_JassDialogAddButton(jassDialog_t *dialog, cstring_t label,
        jassDialogButtonOptions_t const *options) {
    if (!G_JassDialog(dialog)) return NULL;
    jassDialogButton_t *button = DialogAllocSlot(level.dialog_buttons, sizeof(*level.dialog_buttons), &level.dialog_button_count, MAX_JASS_DIALOG_BUTTONS, "button");
    if (!button) return NULL;
    button->dialog_id = dialog->id;
    if (options) {
        button->hotkey = options->hotkey;
        button->quit = options->quit;
        button->score_screen = options->score_screen;
    }
    snprintf(button->text, sizeof(button->text), "%s", label ? G_LevelString(label) : "");
    DialogDebug("add_button dialog=%u button=%u label=\"%s\" hotkey=%d quit=%u score_screen=%u",
        dialog->id, button->id, button->text, button->hotkey, button->quit, button->score_screen);
    return button;
}

void G_JassDialogClear(jassDialog_t *dialog) {
    if (!G_JassDialog(dialog)) return;
    DialogDebug("clear dialog=%u visible_players=0x%08x", dialog->id, dialog->visible_players);
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
    DialogDebug("destroy dialog=%u", dialog->id);
    G_JassDialogClear(dialog);
    dialog->inuse = false;
    dialog->visible_players = 0;
}

void G_JassDialogDisplay(player_t *player, jassDialog_t *dialog, bool visible) {
    uint32_t index;
    if (!G_JassDialog(dialog) || !DialogIsPlayer(player, &index)) {
        DialogDebug("display rejected dialog=%u visible=%u reason=invalid_dialog_or_player",
            dialog ? dialog->id : 0, visible);
        return;
    }
    edict_t *ent = G_GetPlayerEntityByNumber(index);
    bool was_visible = dialog->visible_players & (1u << index);
    DialogDebug("display dialog=%u player=%u visible=%u was_visible=%u entity=%u",
        dialog->id, index, visible, was_visible, ent && ent->client);
    if (visible) {
        /* A script-authored choice dialog is the result presentation for this
         * player. Do not also flush the temporary native result fallback. */
        if (ent && ent->client && ent->client->jass.pending_game_result) {
            G_GameResultDebug("fallback suppressed player=%u result=%u reason=jass_choice_dialog dialog=%u",
                index, (unsigned)ent->client->jass.pending_game_result - 1, dialog->id);
            ent->client->jass.pending_game_result = 0;
            ent->client->jass.pending_game_result_event = 0;
        }
        /* The client has a single authoritative choice window per player. */
        FOR_LOOP(i, level.dialog_count) if (level.dialogs[i].id != dialog->id)
            level.dialogs[i].visible_players &= ~(1u << index);
        dialog->visible_players |= 1u << index;
        if (ent && ent->client) UI_JassDialogShow(ent, dialog);
    } else {
        dialog->visible_players &= ~(1u << index);
        if (was_visible && ent && ent->client) UI_JassDialogHide(ent);
    }
}

void G_JassDialogClick(edict_t *ent, uint32_t dialog_id, uint32_t button_id) {
    jassDialog_t *dialog = G_JassDialogById(dialog_id);
    jassDialogButton_t *button = G_JassDialogButtonById(button_id);
    uint32_t player;
    if (!ent || !ent->client || !dialog || !button || button->dialog_id != dialog_id ||
        !DialogIsPlayer(&ent->client->ps, &player) ||
        !(dialog->visible_players & (1u << player))) {
        DialogDebug("click rejected dialog=%u button=%u entity=%u reason=stale_or_not_visible",
            dialog_id, button_id, ent && ent->client);
        return;
    }
    DialogDebug("click dialog=%u button=%u player=%u label=\"%s\"", dialog_id, button_id, player, button->text);
    dialog->visible_players &= ~(1u << player); /* reject duplicate commands */
    uint32_t published = 0;
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
        ++published;
    }
    DialogDebug("click dispatched dialog=%u button=%u player=%u events=%u", dialog_id, button_id, player, published);
    /* Blizzard's single-player result dialogs pause simulation before the
     * player chooses. Their button callbacks therefore cannot wait for the
     * ordinary next-frame event pass. Drain the published choice now, just as
     * the result handoff drains result events while paused. */
    if (published && level.script_paused && level.vm) {
        DialogDebug("click drain paused events dialog=%u button=%u", dialog_id, button_id);
        G_RunEvents();
        jass_runevents(level.vm);
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
    DialogDebug("ui_show dialog=%u player=%u message=\"%s\"", dialog->id,
        ent->client->ps.number, dialog->message);
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
    if (omitted) fprintf(stderr, "WC3 JASS dialog %u: omitted %u button(s); UI capacity is %d\n", dialog->id, omitted, MAX_JASS_DIALOG_UI_BUTTONS);
    DialogDebug("ui_build dialog=%u player=%u stock_dialog=%u stock_button=%u buttons=%u omitted=%u size=%.3fx%.3f",
        dialog->id, ent->client->ps.number, stock_dialog, stock_button, n, omitted, root->Width,
        MAX(root->Height, content_height + 0.012f));
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
    DialogDebug("ui_hide player=%u", ent->client->ps.number);
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
