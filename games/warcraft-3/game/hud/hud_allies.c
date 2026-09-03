/* hud_allies.c — Server-authored Warcraft III Alliance dialog. */
#include "hud_local.h"
#include "../generated/alliance_dialog.h"
#include "../generated/alliance_slot.h"

#define WC3_MAP_LOCK_ALLIANCE_CHANGES   1024u
#define WC3_MAP_ALLIANCE_CHANGES_HIDDEN 2048u
#define ALLIES_MAX_TARGETS PLAYER_NEUTRAL_AGGRESSIVE

typedef struct {
    BOOL active;
    USHORT alliances[MAX_PLAYERS];
    BOOL allied_victory;
} alliesDraft_t;

static AllianceSlot_t alliance_slots[ALLIES_MAX_TARGETS];
static alliesDraft_t allies_drafts[MAX_PLAYERS];
static BOOL alliance_loaded;

static BOOL AlliesEnsureLoaded(void);

void UI_LoadHudAllies(void) {
    alliance_loaded = false;
    AlliesEnsureLoaded();
}

static BOOL AlliesDebugEnabled(void) {
    return gi.CvarString && atoi(gi.CvarString("ui_window_debug", "0")) != 0;
}

static LPCSTR AlliesImageName(DWORD image) {
    LPCSTR value;
    if (!image || !gi.GetConfigstring) return image ? "<unavailable>" : "<none>";
    value = gi.GetConfigstring(CS_IMAGES + image);
    return value && *value ? value : "<empty>";
}

static void AlliesDebugFdfBackdrop(LPCSTR stage, LPCSTR label, LPCFRAMEDEF frame) {
    if (!AlliesDebugEnabled()) return;
    if (!frame) {
        fprintf(stderr, "WC3_ALLIES_UI server stage=%s backdrop=%s MISSING\n", stage, label);
        return;
    }
    fprintf(stderr,
            "WC3_ALLIES_UI server stage=%s backdrop=%s name=\"%s\" type=%u hidden=%u "
            "parent=\"%s\" size=(%.4f,%.4f) color=(%u,%u,%u,%u) alphaMode=%u "
            "bg=%u path=\"%s\" edge=%u path=\"%s\" cornerFlags=%u corner=%.4f "
            "bgSize=%.4f insets=(%.4f,%.4f,%.4f,%.4f) tile=%u blend=%u mirrored=%u decorate=%u\n",
            stage, label, frame->Name, (unsigned)frame->Type, (unsigned)frame->hidden,
            frame->Parent ? frame->Parent->Name : "<none>", frame->Width, frame->Height,
            (unsigned)frame->Color.r, (unsigned)frame->Color.g,
            (unsigned)frame->Color.b, (unsigned)frame->Color.a, (unsigned)frame->AlphaMode,
            (unsigned)frame->Backdrop.Background, AlliesImageName(frame->Backdrop.Background),
            (unsigned)frame->Backdrop.EdgeFile, AlliesImageName(frame->Backdrop.EdgeFile),
            (unsigned)frame->Backdrop.CornerFlags, frame->Backdrop.CornerSize,
            frame->Backdrop.BackgroundSize, frame->Backdrop.BackgroundInsets[0],
            frame->Backdrop.BackgroundInsets[1], frame->Backdrop.BackgroundInsets[2],
            frame->Backdrop.BackgroundInsets[3], (unsigned)frame->Backdrop.TileBackground,
            (unsigned)frame->Backdrop.BlendAll, (unsigned)frame->Backdrop.Mirrored,
            (unsigned)frame->DecorateFileNames);
}

static void AlliesDebugWireBackdrop(LPCFRAMEDEF frame) {
    BYTE typedata[256] = { 0 };
    UINAME textbuf = { 0 };
    uiFrame_t wire;
    uiBackdrop_t const *bd;

    if (!AlliesDebugEnabled() || !frame) return;
    if (!UI_BuildFrameForWrite(frame, &wire, typedata, sizeof(typedata), textbuf, sizeof(textbuf))) {
        fprintf(stderr, "WC3_ALLIES_UI server stage=wire backdrop=AllianceBackdrop BUILD_FAILED\n");
        return;
    }
    if (wire.buffer.size < sizeof(uiBackdrop_t) || !wire.buffer.data) {
        fprintf(stderr,
                "WC3_ALLIES_UI server stage=wire backdrop=AllianceBackdrop INVALID_BUFFER size=%u\n",
                (unsigned)wire.buffer.size);
        return;
    }
    bd = wire.buffer.data;
    fprintf(stderr,
            "WC3_ALLIES_UI server stage=wire backdrop=AllianceBackdrop type=%u size=(%.4f,%.4f) "
            "color=(%u,%u,%u,%u) bg=%u path=\"%s\" edge=%u path=\"%s\" "
            "cornerFlags=%d corner=%.4f bgSize=%.4f insets=(%.4f,%.4f,%.4f,%.4f) "
            "tile=%u blend=%u mirrored=%u\n",
            (unsigned)wire.flags.type, wire.size.width, wire.size.height,
            (unsigned)wire.color.r, (unsigned)wire.color.g,
            (unsigned)wire.color.b, (unsigned)wire.color.a,
            (unsigned)bd->Background, AlliesImageName(bd->Background),
            (unsigned)bd->EdgeFile, AlliesImageName(bd->EdgeFile),
            (int)bd->CornerFlags, bd->CornerSize, bd->BackgroundSize,
            bd->BackgroundInsets[0], bd->BackgroundInsets[1],
            bd->BackgroundInsets[2], bd->BackgroundInsets[3],
            (unsigned)bd->TileBackground, (unsigned)bd->BlendAll, (unsigned)bd->Mirrored);
}

static BOOL AlliesTargetAvailable(DWORD viewer, DWORD target) {
    if (target >= PLAYER_NEUTRAL_AGGRESSIVE || target == viewer) return false;
    if (level.mapinfo) return level.mapinfo->players[target].used;
    LPGAMECLIENT client = G_GetPlayerClientByNumber(target);
    return client && client->ps.number == target && client->ps.name && *client->ps.name;
}

static LPGAMECLIENT AlliesTargetClient(DWORD target) {
    LPGAMECLIENT client = G_GetPlayerClientByNumber(target);
    return client && client->ps.number == target ? client : NULL;
}

static void AlliesSetSlotPosition(AllianceSlot_t *slot, DWORD row) {
    LPFRAMEDEF root = slot ? slot->AllianceSlot : NULL;
    if (!root || !hud.allies.PlayersHeader) return;
    memset(&root->Points, 0, sizeof(root->Points));
    root->AnyPointsSet = true;
    UI_SetPoint(root, FRAMEPOINT_TOPLEFT, hud.allies.PlayersHeader, FRAMEPOINT_BOTTOMLEFT,
                -0.015f, -0.004f - row * root->Height);
}

/* AllianceDialog.fdf leaves AllianceBackdrop with zero explicit Width/Height.
 * The local FDF renderer can resolve its authored parent-relative layout, but
 * the generic server-authored window path received no usable span and therefore
 * reconstructed a 0x0 rectangle. Pin both corners to the dialog explicitly so
 * the wire layout resolves the backdrop to the full AllianceDialog bounds. */
static void AlliesPinBackdropToDialog(void) {
    LPFRAMEDEF backdrop = hud.allies.AllianceBackdrop;
    LPFRAMEDEF dialog = hud.allies.AllianceDialog;

    if (!backdrop || !dialog || backdrop->Type != FT_BACKDROP) return;
    memset(&backdrop->Points, 0, sizeof(backdrop->Points));
    backdrop->AnyPointsSet = true;
    UI_SetPoint(backdrop, FRAMEPOINT_TOPLEFT, dialog, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    UI_SetPoint(backdrop, FRAMEPOINT_BOTTOMRIGHT, dialog, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
    UI_SetHidden(backdrop, false);
    if (AlliesDebugEnabled()) {
        fprintf(stderr,
                "WC3_ALLIES_UI server stage=pin_backdrop parent=\"%s\" "
                "top_left=%u bottom_right=%u size=(%.4f,%.4f)\n",
                dialog->Name,
                (unsigned)(backdrop->Points.x[FPP_MIN].used && backdrop->Points.y[FPP_MAX].used),
                (unsigned)(backdrop->Points.x[FPP_MAX].used && backdrop->Points.y[FPP_MIN].used),
                backdrop->Width, backdrop->Height);
    }
}

static BOOL AlliesEnsureLoaded(void) {
    AllianceSlot_t slot_template;

    if (alliance_loaded) return hud.allies.AllianceDialog != NULL;
    alliance_loaded = true;
    if (!AllianceDialog_Load(&hud.allies) || !AllianceSlot_Load(&slot_template)) {
        if (AlliesDebugEnabled())
            fprintf(stderr, "WC3_ALLIES_UI server stage=load AllianceDialog/AllianceSlot failed\n");
        return false;
    }
    if (AlliesDebugEnabled()) {
        fprintf(stderr,
                "WC3_ALLIES_UI server stage=load dialog=\"%s\" type=%u hidden=%u size=(%.4f,%.4f) "
                "backdrop_ptr=%p slot_template=\"%s\"\n",
                hud.allies.AllianceDialog ? hud.allies.AllianceDialog->Name : "<missing>",
                hud.allies.AllianceDialog ? (unsigned)hud.allies.AllianceDialog->Type : 0u,
                hud.allies.AllianceDialog ? (unsigned)hud.allies.AllianceDialog->hidden : 0u,
                hud.allies.AllianceDialog ? hud.allies.AllianceDialog->Width : 0.0f,
                hud.allies.AllianceDialog ? hud.allies.AllianceDialog->Height : 0.0f,
                (void *)hud.allies.AllianceBackdrop,
                slot_template.AllianceSlot ? slot_template.AllianceSlot->Name : "<missing>");
    }

    UI_CenterFrame(hud.allies.AllianceDialog);
    AlliesPinBackdropToDialog();
    /* OpenRealm exposes at most eleven other regular players and the stock
     * AllianceSlot stride fits all eleven above Allied Victory.  The retail
     * dialog's optional scrollbar is therefore unused here; keeping its
     * unbound control tree visible only creates orphan arrow/thumb controls. */
    {
        LPFRAMEDEF scrollbar = UI_FindFrameNear(hud.allies.AllianceDialog, "AllianceDialogScrollBar");
        if (scrollbar) UI_SetHidden(scrollbar, true);
    }
    UI_SetOnClick(hud.allies.AllianceAcceptButton,
                  UI_WINDOW_CLOSE_COMMAND_PREFIX "allies_accept");
    UI_SetOnClick(hud.allies.AllianceCancelButton,
                  UI_WINDOW_CLOSE_COMMAND_PREFIX "allies_cancel");

    FOR_LOOP(i, ALLIES_MAX_TARGETS) {
        LPFRAMEDEF root = UI_CloneFrameTree(slot_template.AllianceSlot, hud.allies.AllianceDialog);
        if (!root || !AllianceSlot_Bind(&alliance_slots[i], root)) return false;
        AlliesSetSlotPosition(&alliance_slots[i], i);
        UI_SetHidden(root, true);
    }
    return true;
}

static alliesDraft_t *AlliesDraft(LPEDICT ent) {
    DWORD player;
    if (!ent || !ent->client) return NULL;
    player = ent->client->ps.number;
    return player < MAX_PLAYERS ? &allies_drafts[player] : NULL;
}

static void AlliesBeginDraft(LPEDICT ent) {
    alliesDraft_t *draft = AlliesDraft(ent);
    DWORD player;
    if (!draft) return;
    player = ent->client->ps.number;
    memset(draft, 0, sizeof(*draft));
    draft->active = true;
    memcpy(draft->alliances, level.alliances[player], sizeof(draft->alliances));
    draft->allied_victory = ent->client->ps.stats[PLAYERSTATE_ALLIED_VICTORY] != 0;
}

static void AlliesSetCheckBox(LPFRAMEDEF frame, BOOL checked, BOOL enabled, LPCSTR command) {
    if (!frame) return;
    frame->CheckBox.Checked = checked;
    if (enabled && command) UI_SetOnClick(frame, "%s", command);
    else UI_SetOnClick(frame, "");
}

static void AlliesPopulateSlot(AllianceSlot_t *slot, DWORD target,
                               alliesDraft_t const *draft, BOOL controls_visible,
                               BOOL controls_enabled) {
    LPGAMECLIENT target_client = AlliesTargetClient(target);
    DWORD const mask = draft->alliances[target];
    char color_art[MAX_PATHLEN];
    char command[64];
    LPCSTR name = target_client && target_client->jass.name[0]
        ? target_client->jass.name
        : target_client && target_client->ps.name ? target_client->ps.name : "";
    DWORD color = target_client ? target_client->ps.color : target;

    UI_SetHidden(slot->AllianceSlot, false);
    UI_SetText(slot->PlayerNameLabel, "%s", name);
    snprintf(color_art, sizeof(color_art), "ReplaceableTextures\\TeamColor\\TeamColor%02u.blp",
             (unsigned)color);
    slot->ColorBackdrop->Backdrop.Background = UI_LoadTexture(color_art, false);
    slot->ColorBackdrop->Backdrop.BlendAll = true;

    UI_SetHidden(slot->AllyCheckBox, !controls_visible);
    UI_SetHidden(slot->VisionCheckBox, !controls_visible);
    UI_SetHidden(slot->UnitsCheckBox, !controls_visible);

    snprintf(command, sizeof(command), "allies_toggle %u %u", (unsigned)target,
             (unsigned)ALLIANCE_PASSIVE);
    AlliesSetCheckBox(slot->AllyCheckBox, (mask & (1u << ALLIANCE_PASSIVE)) != 0,
                      controls_enabled, command);
    snprintf(command, sizeof(command), "allies_toggle %u %u", (unsigned)target,
             (unsigned)ALLIANCE_SHARED_VISION);
    AlliesSetCheckBox(slot->VisionCheckBox, (mask & (1u << ALLIANCE_SHARED_VISION)) != 0,
                      controls_enabled, command);
    snprintf(command, sizeof(command), "allies_toggle %u %u", (unsigned)target,
             (unsigned)ALLIANCE_SHARED_CONTROL);
    AlliesSetCheckBox(slot->UnitsCheckBox, (mask & (1u << ALLIANCE_SHARED_CONTROL)) != 0,
                      controls_enabled, command);

    /* Resource trading remains intentionally inert until OpenRealm has an
     * authoritative transfer command and Warcraft-compatible validation. */
    UI_SetOnClick(slot->GoldBackdrop, "");
    UI_SetOnClick(slot->LumberBackdrop, "");
}

static void AlliesWriteDraft(LPEDICT ent) {
    alliesDraft_t *draft = AlliesDraft(ent);
    DWORD const player = ent->client->ps.number;
    BOOL const hidden = (level.setup.map_flags & WC3_MAP_ALLIANCE_CHANGES_HIDDEN) != 0;
    BOOL const locked = (level.setup.map_flags & WC3_MAP_LOCK_ALLIANCE_CHANGES) != 0;
    DWORD row = 0;

    if (!draft || !draft->active || !AlliesEnsureLoaded()) return;

    UI_SetHidden(hud.allies.AllyHeader, hidden);
    UI_SetHidden(hud.allies.VisionHeader, hidden);
    UI_SetHidden(hud.allies.UnitsHeader, hidden);
    UI_SetHidden(hud.allies.AlliedVictoryCheckBox, hidden);
    UI_SetHidden(hud.allies.AlliedVictoryLabel, hidden);
    AlliesSetCheckBox(hud.allies.AlliedVictoryCheckBox, draft->allied_victory,
                      !hidden && !locked, "allies_toggle_victory");

    FOR_LOOP(i, ALLIES_MAX_TARGETS) UI_SetHidden(alliance_slots[i].AllianceSlot, true);
    FOR_LOOP(target, PLAYER_NEUTRAL_AGGRESSIVE) {
        if (!AlliesTargetAvailable(player, target) || row >= ALLIES_MAX_TARGETS) continue;
        AlliesSetSlotPosition(&alliance_slots[row], row);
        AlliesPopulateSlot(&alliance_slots[row], target, draft, !hidden, !hidden && !locked);
        row++;
    }

    UI_SetCurrentClient(ent->client);
    if (AlliesDebugEnabled()) {
        fprintf(stderr,
                "WC3_ALLIES_UI server stage=write player=%u rows=%u map_flags=%08x dialog_hidden=%u backdrop_hidden=%u\n",
                (unsigned)player, (unsigned)row, (unsigned)level.setup.map_flags,
                hud.allies.AllianceDialog ? (unsigned)hud.allies.AllianceDialog->hidden : 0u,
                hud.allies.AllianceBackdrop ? (unsigned)hud.allies.AllianceBackdrop->hidden : 0u);
        AlliesDebugFdfBackdrop("write", "AllianceBackdrop", hud.allies.AllianceBackdrop);
        AlliesDebugWireBackdrop(hud.allies.AllianceBackdrop);
    }
    UI_WriteWindow(ent, hud.allies.AllianceDialog, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_ALLIES,
        .class_id = BZ_WC3_WINDOW_ALLIES,
        .flags = UI_WINDOW_MOVABLE | UI_WINDOW_MODAL | UI_WINDOW_UNIQUE | UI_WINDOW_NO_PAUSE));
    UI_SetCurrentClient(NULL);
}

void UI_ShowAllies(LPEDICT ent) {
    if (!ent || !ent->client || !ent->client->connected) return;
    UI_SetCurrentClient(ent->client);
    if (!AlliesEnsureLoaded()) { UI_SetCurrentClient(NULL); return; }
    UI_SetCurrentClient(NULL);
    AlliesBeginDraft(ent);
    AlliesWriteDraft(ent);
}

void UI_AlliesToggle(LPEDICT ent, DWORD target, PLAYERALLIANCE type) {
    alliesDraft_t *draft = AlliesDraft(ent);
    DWORD const player = ent && ent->client ? ent->client->ps.number : MAX_PLAYERS;
    DWORD flag;

    if (!draft || !draft->active || player >= MAX_PLAYERS ||
        (level.setup.map_flags & (WC3_MAP_LOCK_ALLIANCE_CHANGES | WC3_MAP_ALLIANCE_CHANGES_HIDDEN)) ||
        !AlliesTargetAvailable(player, target)) return;
    if (type != ALLIANCE_PASSIVE && type != ALLIANCE_SHARED_VISION && type != ALLIANCE_SHARED_CONTROL) return;

    flag = 1u << type;
    draft->alliances[target] ^= flag;
    if (type == ALLIANCE_SHARED_CONTROL && (draft->alliances[target] & flag)) {
        draft->alliances[target] |= (1u << ALLIANCE_PASSIVE) | (1u << ALLIANCE_SHARED_VISION);
    } else if ((type == ALLIANCE_PASSIVE || type == ALLIANCE_SHARED_VISION) &&
               !(draft->alliances[target] & flag)) {
        draft->alliances[target] &= ~(1u << ALLIANCE_SHARED_CONTROL);
    }
    AlliesWriteDraft(ent);
}

void UI_AlliesToggleVictory(LPEDICT ent) {
    alliesDraft_t *draft = AlliesDraft(ent);
    if (!draft || !draft->active ||
        (level.setup.map_flags & (WC3_MAP_LOCK_ALLIANCE_CHANGES | WC3_MAP_ALLIANCE_CHANGES_HIDDEN))) return;
    draft->allied_victory = !draft->allied_victory;
    AlliesWriteDraft(ent);
}

void UI_AlliesAccept(LPEDICT ent) {
    alliesDraft_t *draft = AlliesDraft(ent);
    DWORD const player = ent && ent->client ? ent->client->ps.number : MAX_PLAYERS;

    if (!draft || !draft->active || player >= MAX_PLAYERS) return;
    if (!(level.setup.map_flags & (WC3_MAP_LOCK_ALLIANCE_CHANGES | WC3_MAP_ALLIANCE_CHANGES_HIDDEN))) {
        FOR_LOOP(target, PLAYER_NEUTRAL_AGGRESSIVE) {
            LPGAMECLIENT target_client;
            if (!AlliesTargetAvailable(player, target)) continue;
            target_client = AlliesTargetClient(target);
            if (!target_client) continue;
            PLAYERALLIANCE const types[] = {
                ALLIANCE_PASSIVE, ALLIANCE_SHARED_VISION, ALLIANCE_SHARED_CONTROL,
            };
            FOR_LOOP(i, sizeof(types) / sizeof(types[0])) {
                PLAYERALLIANCE const type = types[i];
                BOOL const value = (draft->alliances[target] & (1u << type)) != 0;
                if (!!G_GetPlayerAlliance(&ent->client->ps, &target_client->ps, type) != value)
                    G_SetPlayerAlliance(&ent->client->ps, &target_client->ps, type, value);
            }
        }
        ent->client->ps.stats[PLAYERSTATE_ALLIED_VICTORY] = draft->allied_victory ? 1 : 0;
    }
    draft->active = false;
}

void UI_AlliesCancel(LPEDICT ent) {
    alliesDraft_t *draft = AlliesDraft(ent);
    if (draft) draft->active = false;
}
