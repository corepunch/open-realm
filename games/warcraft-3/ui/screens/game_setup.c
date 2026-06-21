/*
 * ui/screens/game_setup.c - LAN game lobby/player setup screen.
 */

#include <stdlib.h>

#ifndef _WIN32
#include <strings.h>
#endif
#if defined(__has_include)
#if __has_include(<SDL2/SDL_keycode.h>)
#include <SDL2/SDL_keycode.h>
#endif
#endif

#ifndef SDLK_RETURN
#define SDLK_RETURN 13
#define SDLK_KP_ENTER 1073741912
#endif

#include "../ui_local.h"
#include "../ui_screen.h"
#include "../generated/game_chatroom.h"
#include "../generated/map_info_pane.h"
#include "../generated/player_slot.h"

#define GAME_SETUP_CHAT_LINES 16
#define GAME_SETUP_CHAT_LINE 160

typedef struct {
    PlayerSlot_t frames;
    LPFRAMEDEF name_text;
    LPFRAMEDEF race_text;
} gameSetupSlotRow_t;

typedef enum {
    GAME_SETUP_SLOT_OPEN,
    GAME_SETUP_SLOT_HUMAN,
    GAME_SETUP_SLOT_COMPUTER,
    GAME_SETUP_SLOT_CLOSED,
} gameSetupSlotType_t;

typedef struct {
    BOOL visible;
    DWORD map_player;
    gameSetupSlotType_t type;
    playerRace_t race;
    DWORD team;
    DWORD color;
    UINAME name;
} gameSetupSlotConfig_t;

typedef struct {
    GameChatroom_t frames;
    PlayerSlot_t slot_template;
    MapInfoPane_t map_info_template;
    MapInfoPane_t map_info_pane;
    BOOL ready;
    PATHSTR map_path;
    UINAME map_name;
    MAPINFO map_info;
    BOOL have_map_info;
    LPFRAMEDEF root;
    LPFRAMEDEF game_name;
    LPFRAMEDEF start_button;
    LPFRAMEDEF cancel_button;
    LPFRAMEDEF chat_text;
    LPFRAMEDEF team_container;
    char chat_lines[GAME_SETUP_CHAT_LINES][GAME_SETUP_CHAT_LINE];
    BOOL chat_line_own[GAME_SETUP_CHAT_LINES];
    DWORD num_chat_lines;
    BOOL applying_lobby;
    gameSetupSlotRow_t slots[MAX_PLAYERS];
    gameSetupSlotConfig_t configs[MAX_PLAYERS];
} gameSetupState_t;

static gameSetupState_t setup;

static DWORD GameSetup_CountPlayers(LPCMAPINFO info);
static void GameSetup_PublishLobby(void);
static void GameSetup_AppendText(LPSTR out, size_t out_size, size_t *used, LPCSTR text);

static BOOL GameSetup_LoadScreen(void) {
    BOOL ok = true;

    ok = GameChatroom_Load(&setup.frames) && ok;
    ok = PlayerSlot_Load(&setup.slot_template) && ok;
    ok = MapInfoPane_Load(&setup.map_info_template) && ok;
    return ok;
}

static void GameSetup_CopyString(LPSTR out, size_t out_size, LPCSTR value) {
    if (!out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%s", value ? value : "");
}

static LPCSTR GameSetup_BaseName(LPCSTR path) {
    LPCSTR base = path;

    if (!path) {
        return "";
    }
    for (LPCSTR p = path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }
    return base;
}

static LPFRAMEDEF GameSetup_FindPopupTitleText(LPFRAMEDEF popup) {
    LPFRAMEDEF title;
    LPFRAMEDEF text;

    if (!popup) {
        return NULL;
    }
    title = UI_FindChildFrame(popup, popup->Popup.TitleFrame);
    text = title ? UI_FindChildFrame(title, "StandardPopupMenuTitleTextTemplate") : NULL;
    return text ? text : title;
}

static void GameSetup_PositionPopupMenuParts(LPFRAMEDEF popup) {
    LPFRAMEDEF title;
    LPFRAMEDEF arrow;
    FLOAT const inset = popup ? popup->Popup.ButtonInset : 0.0f;
    FLOAT arrow_width;
    FLOAT title_width;

    if (!popup) {
        return;
    }

    title = UI_FindChildFrame(popup, popup->Popup.TitleFrame);
    arrow = UI_FindChildFrame(popup, popup->Popup.ArrowFrame);
    arrow_width = arrow && arrow->Width > 0.0f ? arrow->Width : 0.011f;
    title_width = popup->Width - arrow_width - inset * 2.0f;

    if (title) {
        UI_SetSize(title, MAX(0.0f, title_width), popup->Height);
        UI_SetPoint(title, FRAMEPOINT_LEFT, popup, FRAMEPOINT_LEFT, inset, 0.0f);
    }
    if (arrow) {
        UI_SetPoint(arrow, FRAMEPOINT_RIGHT, popup, FRAMEPOINT_RIGHT, -inset, 0.0f);
    }
}

static void GameSetup_SetBackdropTexture(LPFRAMEDEF frame, LPCSTR name, BOOL decorate) {
    if (!frame) {
        return;
    }
    frame->Backdrop.Background = UI_LoadTexture(name, decorate);
    frame->Backdrop.BlendAll = true;
    frame->Color = COLOR32_WHITE;
}

static void GameSetup_SetTextIfPresent(LPFRAMEDEF frame, LPCSTR format, ...) {
    va_list argptr;
    char text[1024];

    if (!frame || !format) {
        return;
    }
    va_start(argptr, format);
    vsnprintf(text, sizeof(text), format, argptr);
    va_end(argptr);
    UI_SetText(frame, "%s", text);
}

static BOOL GameSetup_IsHost(void) {
    LPCSTR connect = uiimport.Cvar_String ? uiimport.Cvar_String("connect", "") : "";

    return !connect || !connect[0];
}

static void GameSetup_UpdateStartButton(void) {
    if (!setup.start_button) {
        return;
    }
    if (GameSetup_IsHost()) {
        UI_SetOnClick(setup.start_button, "menu_game_setup_start");
        UI_SetEnabled(setup.start_button, true);
    } else {
        setup.start_button->OnClick[0] = '\0';
        UI_SetEnabled(setup.start_button, false);
    }
}

static void GameSetup_UpdateSlotControlState(gameSetupSlotRow_t *row) {
    BOOL const host = GameSetup_IsHost();

    if (!row || !row->frames.PlayerSlot) {
        return;
    }
    UI_SetEnabled(row->frames.NameMenu, host);
    UI_SetEnabled(row->frames.RaceMenu, host);
    UI_SetEnabled(row->frames.TeamButton, host);
    UI_SetEnabled(row->frames.ColorButton, host);
}

static LPFRAMEDEF GameSetup_EnsureChatText(void) {
    LPCFRAMEDEF template;

    if (setup.chat_text) {
        return setup.chat_text;
    }
    if (!setup.frames.ChatTextArea) {
        return NULL;
    }
    setup.chat_text = UI_Spawn(FT_TEXT, setup.frames.ChatTextArea);
    if (!setup.chat_text) {
        return NULL;
    }
    snprintf(setup.chat_text->Name, sizeof(setup.chat_text->Name), "GameSetupChatText");
    template = UI_FindFrame("StandardInfoTextTemplate");
    if (template) {
        setup.chat_text->DecorateFileNames = template->DecorateFileNames;
        setup.chat_text->Width = template->Width;
        setup.chat_text->Height = template->Height;
        setup.chat_text->Font = template->Font;
    }
    if (!setup.chat_text->Font.Name[0]) {
        snprintf(setup.chat_text->Font.Name, sizeof(setup.chat_text->Font.Name), "MasterFont");
    }
    if (setup.chat_text->Font.Size <= 0.0f) {
        setup.chat_text->Font.Size = 0.010f;
    }
    if (!setup.chat_text->Font.Color.a) {
        setup.chat_text->Font.Color = COLOR32_WHITE;
    }
    setup.chat_text->Font.Justification.Horizontal = FONT_JUSTIFYLEFT;
    setup.chat_text->Font.Justification.Vertical = FONT_JUSTIFYTOP;
    UI_SetPoint(setup.chat_text,
                FRAMEPOINT_TOPLEFT,
                setup.frames.ChatTextArea,
                FRAMEPOINT_TOPLEFT,
                0.004f,
                -0.004f);
    UI_SetPoint(setup.chat_text,
                FRAMEPOINT_BOTTOMRIGHT,
                setup.frames.ChatTextArea,
                FRAMEPOINT_BOTTOMRIGHT,
                -0.014f,
                0.004f);
    return setup.chat_text;
}

void GameSetup_AddChatMessage(LPCSTR text, BOOL own) {
    char buffer[GAME_SETUP_CHAT_LINES * GAME_SETUP_CHAT_LINE];
    size_t used = 0;

    if (!text || !text[0]) {
        return;
    }
    if (setup.num_chat_lines >= GAME_SETUP_CHAT_LINES) {
        memmove(setup.chat_lines,
                setup.chat_lines + 1,
                sizeof(setup.chat_lines[0]) * (GAME_SETUP_CHAT_LINES - 1));
        memmove(setup.chat_line_own,
                setup.chat_line_own + 1,
                sizeof(setup.chat_line_own[0]) * (GAME_SETUP_CHAT_LINES - 1));
        setup.num_chat_lines = GAME_SETUP_CHAT_LINES - 1;
    }
    GameSetup_CopyString(setup.chat_lines[setup.num_chat_lines],
                         sizeof(setup.chat_lines[setup.num_chat_lines]),
                         text);
    for (char *p = setup.chat_lines[setup.num_chat_lines]; *p; p++) {
        if (*p == '\r' || *p == '\n') {
            *p = ' ';
        }
    }
    setup.chat_line_own[setup.num_chat_lines] = own;
    setup.num_chat_lines++;

    buffer[0] = '\0';
    FOR_LOOP(i, setup.num_chat_lines) {
        if (i > 0) {
            GameSetup_AppendText(buffer, sizeof(buffer), &used, "\n");
        }
        GameSetup_AppendText(buffer,
                             sizeof(buffer),
                             &used,
                             setup.chat_line_own[i] ? "|cffffffff" : "|cffffcc00");
        GameSetup_AppendText(buffer, sizeof(buffer), &used, setup.chat_lines[i]);
        GameSetup_AppendText(buffer, sizeof(buffer), &used, "|r");
    }
    GameSetup_SetTextIfPresent(GameSetup_EnsureChatText(), "%s", buffer);
}

static void GameSetup_FitSlotRow(PlayerSlot_t *row) {
    if (!row || !row->PlayerSlot) {
        return;
    }

    if (row->DownloadValue) {
        UI_SetHidden(row->DownloadValue, true);
    }
    if (row->NameMenu) {
        UI_SetSize(row->NameMenu, 0.168f, row->NameMenu->Height);
        UI_SetPoint(row->NameMenu, FRAMEPOINT_LEFT, row->PlayerSlot, FRAMEPOINT_LEFT, 0.0f, 0.0f);
    }
    if (row->RaceMenu && row->NameMenu) {
        UI_SetSize(row->RaceMenu, 0.085f, row->RaceMenu->Height);
        UI_SetPoint(row->RaceMenu, FRAMEPOINT_LEFT, row->NameMenu, FRAMEPOINT_RIGHT, 0.0f, 0.0f);
    }
    if (row->TeamButton && row->RaceMenu) {
        UI_SetSize(row->TeamButton, 0.085f, row->TeamButton->Height);
        UI_SetPoint(row->TeamButton, FRAMEPOINT_LEFT, row->RaceMenu, FRAMEPOINT_RIGHT, 0.0f, 0.0f);
    }
    if (row->ColorButton && row->TeamButton) {
        UI_SetSize(row->ColorButton, 0.038f, row->ColorButton->Height);
        UI_SetPoint(row->ColorButton, FRAMEPOINT_LEFT, row->TeamButton, FRAMEPOINT_RIGHT, 0.0f, 0.0f);
    }
}

static void GameSetup_SetupSlotRow(gameSetupSlotRow_t *slot) {
    DWORD const index = (DWORD)(slot - setup.slots);

    if (!slot || !slot->frames.PlayerSlot) {
        return;
    }

    GameSetup_FitSlotRow(&slot->frames);
    slot->name_text = GameSetup_FindPopupTitleText(slot->frames.NameMenu);
    slot->race_text = GameSetup_FindPopupTitleText(slot->frames.RaceMenu);

    GameSetup_PositionPopupMenuParts(slot->frames.NameMenu);
    GameSetup_PositionPopupMenuParts(slot->frames.RaceMenu);
    if (slot->frames.NamePopupMenuMenu) {
        UI_MenuClearItems(slot->frames.NamePopupMenuMenu);
        UI_MenuAddItem(slot->frames.NamePopupMenuMenu, "Open", GAME_SETUP_SLOT_OPEN);
        UI_MenuAddItem(slot->frames.NamePopupMenuMenu, "Player", GAME_SETUP_SLOT_HUMAN);
        UI_MenuAddItem(slot->frames.NamePopupMenuMenu, "Computer", GAME_SETUP_SLOT_COMPUTER);
        UI_MenuAddItem(slot->frames.NamePopupMenuMenu, "Closed", GAME_SETUP_SLOT_CLOSED);
        UI_SetOnClick(slot->frames.NamePopupMenuMenu, "menu_game_setup_slot_type %u %%u", (unsigned)index);
        UI_SetHidden(slot->frames.NamePopupMenuMenu, true);
    }
    if (slot->frames.RacePopupMenuMenu) {
        UI_MenuClearItems(slot->frames.RacePopupMenuMenu);
        UI_MenuAddItem(slot->frames.RacePopupMenuMenu, "Random", kPlayerRaceNone);
        UI_MenuAddItem(slot->frames.RacePopupMenuMenu, "Human", kPlayerRaceHuman);
        UI_MenuAddItem(slot->frames.RacePopupMenuMenu, "Orc", kPlayerRaceOrc);
        UI_MenuAddItem(slot->frames.RacePopupMenuMenu, "Undead", kPlayerRaceUndead);
        UI_MenuAddItem(slot->frames.RacePopupMenuMenu, "Night Elf", kPlayerRaceNightElf);
        UI_SetOnClick(slot->frames.RacePopupMenuMenu, "menu_game_setup_slot_race %u %%u", (unsigned)index);
        UI_SetHidden(slot->frames.RacePopupMenuMenu, true);
    }
    UI_SetOnClick(slot->frames.TeamButton, "menu_game_setup_slot_team_next %u", (unsigned)index);
    UI_SetOnClick(slot->frames.ColorButton, "menu_game_setup_slot_color_next %u", (unsigned)index);
}

static void GameSetup_BuildSlotRows(void) {
    LPFRAMEDEF previous = NULL;

    if (!setup.team_container || !setup.slot_template.PlayerSlot || setup.slots[0].frames.PlayerSlot) {
        return;
    }

    FOR_LOOP(i, MAX_PLAYERS) {
        gameSetupSlotRow_t *slot = &setup.slots[i];
        LPFRAMEDEF row;

        row = UI_CloneFrameTree(setup.slot_template.PlayerSlot, setup.team_container);
        if (!row || !PlayerSlot_Bind(&slot->frames, row)) {
            return;
        }
        snprintf(slot->frames.PlayerSlot->Name, sizeof(slot->frames.PlayerSlot->Name), "CreateGamePlayerSlot%u", (unsigned)i);
        UI_SetSize(slot->frames.PlayerSlot, 0.46375f, slot->frames.PlayerSlot->Height > 0.0f ? slot->frames.PlayerSlot->Height : 0.025f);
        if (previous) {
            UI_SetPoint(slot->frames.PlayerSlot, FRAMEPOINT_TOPLEFT, previous, FRAMEPOINT_BOTTOMLEFT, 0.0f, 0.0f);
        } else {
            UI_SetPoint(slot->frames.PlayerSlot,
                        FRAMEPOINT_TOPLEFT,
                        setup.team_container,
                        FRAMEPOINT_TOPLEFT,
                        0.0f,
                        0.0f);
        }
        previous = slot->frames.PlayerSlot;
        GameSetup_SetupSlotRow(slot);
        UI_SetHidden(slot->frames.PlayerSlot, true);
    }
}

static void GameSetup_ClearSlots(void) {
    FOR_LOOP(i, MAX_PLAYERS) {
        if (setup.slots[i].frames.PlayerSlot) {
            UI_SetHidden(setup.slots[i].frames.PlayerSlot, true);
        }
    }
}

static LPCSTR GameSetup_RaceName(playerRace_t race) {
    switch (race) {
        case kPlayerRaceHuman: return "Human";
        case kPlayerRaceOrc: return "Orc";
        case kPlayerRaceUndead: return "Undead";
        case kPlayerRaceNightElf: return "Night Elf";
        case kPlayerRaceNone:
        default:
            return "Random";
    }
}

static LPCSTR GameSetup_SlotTypeName(gameSetupSlotType_t type, LPCSTR human_name) {
    switch (type) {
        case GAME_SETUP_SLOT_HUMAN:
            return human_name && human_name[0] ? human_name : "Player";
        case GAME_SETUP_SLOT_COMPUTER:
            return "Computer";
        case GAME_SETUP_SLOT_CLOSED:
            return "Closed";
        case GAME_SETUP_SLOT_OPEN:
        default:
            return "Open";
    }
}

static void GameSetup_AppendText(LPSTR out, size_t out_size, size_t *used, LPCSTR text) {
    size_t len;

    if (!out || !used || !text || *used >= out_size) {
        return;
    }
    len = strlen(text);
    if (len >= out_size - *used) {
        len = out_size - *used - 1;
    }
    memcpy(out + *used, text, len);
    *used += len;
    out[*used] = '\0';
}

static void GameSetup_AppendQuotedValue(LPSTR out, size_t out_size, size_t *used, LPCSTR value) {
    GameSetup_AppendText(out, out_size, used, "\"");
    for (LPCSTR p = value ? value : ""; *p; p++) {
        char ch[2] = { *p, '\0' };

        if (*p == '\n' || *p == '\r') {
            GameSetup_AppendText(out, out_size, used, " ");
            continue;
        }
        if (*p == '"' || *p == '\\') {
            GameSetup_AppendText(out, out_size, used, "\\");
        }
        GameSetup_AppendText(out, out_size, used, ch);
    }
    GameSetup_AppendText(out, out_size, used, "\"");
}

static void GameSetup_AppendMapCommand(LPSTR out, size_t out_size, size_t *used, LPCSTR map_path) {
    GameSetup_AppendText(out, out_size, used, "map ");
    GameSetup_AppendQuotedValue(out, out_size, used, map_path);
    GameSetup_AppendText(out, out_size, used, "\n");
}

static DWORD GameSetup_LobbySlotCount(void) {
    DWORD slot_count = 0;

    FOR_LOOP(i, MAX_PLAYERS) {
        if (setup.configs[i].visible) {
            slot_count++;
        }
    }
    return slot_count;
}

static void GameSetup_AppendLobbyConfig(LPSTR command, size_t command_size, size_t *used) {
    DWORD slot_count = GameSetup_LobbySlotCount();
    char text[128];

    snprintf(text, sizeof(text), "lobby_config %u %u ",
             (unsigned)LAN_SelectedGameSpeed(),
             (unsigned)slot_count);
    GameSetup_AppendText(command, command_size, used, text);
    GameSetup_AppendQuotedValue(command, command_size, used,
                                setup.map_name[0] ? setup.map_name : setup.map_path);
    GameSetup_AppendText(command, command_size, used, "\n");
}

static void GameSetup_AppendLobbySlot(LPSTR command, size_t command_size, size_t *used, DWORD slot) {
    gameSetupSlotConfig_t const *config;
    char text[256];

    if (slot >= MAX_PLAYERS) {
        return;
    }
    config = &setup.configs[slot];
    snprintf(text, sizeof(text),
             "lobby_slot %u %u %u %u %u %u %u ",
             (unsigned)slot,
             (unsigned)(config->visible ? 1 : 0),
             (unsigned)config->map_player,
             (unsigned)config->type,
             (unsigned)config->race,
             (unsigned)config->team,
             (unsigned)config->color);
    GameSetup_AppendText(command, command_size, used, text);
    GameSetup_AppendQuotedValue(command, command_size, used,
                                config->name[0] ? config->name : GameSetup_SlotTypeName(config->type, NULL));
    GameSetup_AppendText(command, command_size, used, "\n");
}

static void GameSetup_PublishSlot(DWORD slot) {
    char command[512];
    size_t used = 0;

    if (!GameSetup_IsHost() || setup.applying_lobby || !setup.map_path[0]) {
        return;
    }
    command[0] = '\0';
    GameSetup_AppendLobbySlot(command, sizeof(command), &used, slot);
    uiimport.Cmd_ExecuteText(command);
}

static void GameSetup_PublishLobby(void) {
    char command[2048];
    size_t used = 0;
    DWORD slot_count;

    if (!GameSetup_IsHost() || setup.applying_lobby || !setup.map_path[0]) {
        return;
    }
    command[0] = '\0';
    GameSetup_AppendText(command, sizeof(command), &used, "lobby_start ");
    GameSetup_AppendQuotedValue(command, sizeof(command), &used, setup.map_path);
    GameSetup_AppendText(command, sizeof(command), &used, "\n");
    GameSetup_AppendLobbyConfig(command, sizeof(command), &used);
    slot_count = GameSetup_LobbySlotCount();
    FOR_LOOP(i, slot_count) {
        GameSetup_AppendLobbySlot(command, sizeof(command), &used, i);
    }
    uiimport.Cmd_ExecuteText(command);
}

static void GameSetup_SubmitChat(void) {
    LPCSTR text;
    char command[512];
    size_t used = 0;

    if (!UI_EditHasFocus(setup.frames.ChatEditBox)) {
        return;
    }
    text = UI_EditValue(setup.frames.ChatEditBox);
    if (!text || !text[0]) {
        return;
    }

    command[0] = '\0';
    GameSetup_AppendText(command, sizeof(command), &used, "lobby_say ");
    GameSetup_AppendQuotedValue(command, sizeof(command), &used, text);
    GameSetup_AppendText(command, sizeof(command), &used, "\n");
    if (GameSetup_IsHost()) {
        uiimport.Cmd_ExecuteText(command);
    } else if (uiimport.ServerCommand) {
        uiimport.ServerCommand(command);
    }
    UI_SetEditValue(setup.frames.ChatEditBox, "");
    UI_ClearEditFocus();
}

static void GameSetup_ResolveMapString(LPCSTR raw, LPSTR out, DWORD out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!raw || !raw[0]) {
        return;
    }
    if (uiimport.ResolveMapInfoString) {
        uiimport.ResolveMapInfoString(&setup.map_info, raw, out, out_size);
    } else {
        snprintf(out, out_size, "%s", raw);
    }
    if (uiimport.SanitizeMapInfoText) {
        uiimport.SanitizeMapInfoText(out);
    }
}

static void GameSetup_UseResolvedMapTitle(void) {
    UINAME title;

    if (!setup.have_map_info || !setup.map_info.mapName) {
        return;
    }
    GameSetup_ResolveMapString(setup.map_info.mapName, title, sizeof(title));
    if (title[0]) {
        snprintf(setup.map_name, sizeof(setup.map_name), "%s", title);
    }
}

static void GameSetup_SlotName(LPCMAPPLAYER player, BOOL first_human, LPSTR out, DWORD out_size) {
    LPCSTR name;

    if (!player) {
        snprintf(out, out_size, "Open");
        return;
    }
    if (player->playerType == kPlayerTypeComputer) {
        snprintf(out, out_size, "Computer");
        return;
    }
    if (player->playerType == kPlayerTypeHuman && first_human) {
        name = uiimport.Cvar_String ? uiimport.Cvar_String("name", "Player") : "Player";
        snprintf(out, out_size, "%s", name && name[0] ? name : "Player");
        return;
    }
    GameSetup_ResolveMapString(player->playerName, out, out_size);
    if (!out[0]) {
        snprintf(out, out_size, "Open");
    }
}

static DWORD GameSetup_ForceForPlayer(LPCMAPINFO info, DWORD player_index) {
    if (!info || !info->teams) {
        return player_index;
    }

    FOR_LOOP(i, info->num_teams) {
        if (info->teams[i].playerMasks & (1u << player_index)) {
            return i;
        }
    }
    return player_index;
}

static BOOL GameSetup_FixedPlayerSettings(void) {
    return setup.have_map_info &&
           (setup.map_info.flags & fixed_player_setting_for_custom_forces) != 0;
}

static DWORD GameSetup_VisibleSlotCount(void) {
    DWORD count = 0;

    FOR_LOOP(i, MAX_PLAYERS) {
        if (setup.configs[i].visible) {
            count++;
        }
    }
    return count;
}

static DWORD GameSetup_DefaultTeamForSlot(LPCMAPINFO info, DWORD map_player, DWORD row_index) {
    if (setup.have_map_info &&
        (setup.map_info.flags & use_custom_forces) != 0 &&
        (setup.map_info.flags & melee_map) == 0) {
        return GameSetup_ForceForPlayer(info, map_player);
    }
    return row_index;
}

static DWORD GameSetup_SelectableTeamCount(void) {
    DWORD count = GameSetup_VisibleSlotCount();

    if (count == 0 && setup.have_map_info) {
        count = GameSetup_CountPlayers(&setup.map_info);
    }
    return MAX(1, count);
}

static void GameSetup_DrawSlotConfig(DWORD row_index) {
    gameSetupSlotRow_t *row;
    gameSetupSlotConfig_t *config;
    char color_path[128];
    char team[32];

    if (row_index >= MAX_PLAYERS) {
        return;
    }
    row = &setup.slots[row_index];
    config = &setup.configs[row_index];
    if (!row->frames.PlayerSlot || !config->visible) {
        if (row->frames.PlayerSlot) {
            UI_SetHidden(row->frames.PlayerSlot, true);
        }
        return;
    }

    snprintf(team, sizeof(team), "Team %u", (unsigned)config->team + 1);
    GameSetup_SetTextIfPresent(row->name_text,
                               "%s",
                               GameSetup_SlotTypeName(config->type, config->name));
    GameSetup_SetTextIfPresent(row->race_text, "%s", GameSetup_RaceName(config->race));
    GameSetup_SetTextIfPresent(row->frames.TeamButtonTitle, "%s", team);
    if (row->frames.ColorButtonValue) {
        snprintf(color_path, sizeof(color_path),
                 "ReplaceableTextures\\TeamColor\\TeamColor%02u.blp",
                 (unsigned)(config->color % 16));
        GameSetup_SetBackdropTexture(row->frames.ColorButtonValue, color_path, false);
    }
    GameSetup_UpdateSlotControlState(row);
    UI_SetHidden(row->frames.PlayerSlot, false);
}

static void GameSetup_DrawSlotConfigs(void) {
    FOR_LOOP(i, MAX_PLAYERS) {
        GameSetup_DrawSlotConfig(i);
    }
}

static void GameSetup_PopulateSlots(void) {
    DWORD visible = 0;
    BOOL first_human = true;

    GameSetup_ClearSlots();
    memset(setup.configs, 0, sizeof(setup.configs));
    if (!setup.have_map_info) {
        return;
    }

    FOR_LOOP(i, MAX_PLAYERS) {
        mapPlayer_t const *player = &setup.map_info.players[i];
        char name[96];
        BOOL is_first_human;

        if (!player->used ||
            player->playerType == kPlayerTypeNone ||
            player->playerType == kPlayerTypeNeutral) {
            continue;
        }
        if (visible >= MAX_PLAYERS) {
            break;
        }

        is_first_human = player->playerType == kPlayerTypeHuman && first_human;
        if (is_first_human) {
            first_human = false;
        }

        GameSetup_SlotName(player, is_first_human, name, sizeof(name));

        setup.configs[visible].visible = true;
        setup.configs[visible].map_player = i;
        setup.configs[visible].type = player->playerType == kPlayerTypeComputer
            ? GAME_SETUP_SLOT_COMPUTER
            : is_first_human ? GAME_SETUP_SLOT_HUMAN : GAME_SETUP_SLOT_OPEN;
        setup.configs[visible].race = player->playerRace;
        setup.configs[visible].team = GameSetup_DefaultTeamForSlot(&setup.map_info, i, visible);
        setup.configs[visible].color = i;
        GameSetup_CopyString(setup.configs[visible].name, sizeof(setup.configs[visible].name), name);
        visible++;
    }
    GameSetup_DrawSlotConfigs();
}

static void GameSetup_BindMapInfoPane(LPFRAMEDEF container) {
    LPFRAMEDEF root;

    if (setup.map_info_pane.MapInfoPane || !container) {
        return;
    }

    if (!setup.map_info_template.MapInfoPane) {
        uiimport.Printf("GameSetup_BindMapInfoPane: MapInfoPane missing\n");
        return;
    }

    root = UI_CloneFrameTree(setup.map_info_template.MapInfoPane, container);
    if (!root || !MapInfoPane_Bind(&setup.map_info_pane, root)) {
        return;
    }
    UI_SetPoint(setup.map_info_pane.MapInfoPane, FRAMEPOINT_TOPLEFT, container, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    UI_SetPoint(setup.map_info_pane.MapInfoPane, FRAMEPOINT_BOTTOMRIGHT, container, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
    UI_LayoutMapInfoPane(setup.map_info_pane.MapInfoPane);
}

static DWORD GameSetup_CountPlayers(LPCMAPINFO info) {
    DWORD count = 0;

    FOR_LOOP(i, MAX_PLAYERS) {
        if (info && info->players[i].used &&
            info->players[i].playerType != kPlayerTypeNone &&
            info->players[i].playerType != kPlayerTypeNeutral) {
            count++;
        }
    }
    return count;
}

static void GameSetup_UpdateMapInfo(void) {
    char value[512];
    LPCSTR tileset;

    if (!setup.have_map_info) {
        if (setup.map_info_pane.MinimapImage) {
            UI_SetHidden(setup.map_info_pane.MinimapImage, true);
        }
        GameSetup_SetTextIfPresent(setup.map_info_pane.MaxPlayersValue, " ");
        GameSetup_SetTextIfPresent(setup.map_info_pane.MapNameValue, "%s", setup.map_name[0] ? setup.map_name : " ");
        GameSetup_SetTextIfPresent(setup.map_info_pane.SuggestedPlayersValue, "%s", UI_GetString("UNKNOWNMAP_SUGGESTEDPLAYERS"));
        GameSetup_SetTextIfPresent(setup.map_info_pane.MapSizeValue, "%s", UI_GetString("UNKNOWNMAP_MAPSIZE"));
        GameSetup_SetTextIfPresent(setup.map_info_pane.MapTilesetValue, "%s", UI_GetString("UNKNOWNMAP_TILESET"));
        GameSetup_SetTextIfPresent(setup.map_info_pane.MapDescValue, "%s", UI_GetString("UNKNOWNMAP_DESCRIPTION"));
        return;
    }

    GameSetup_SetTextIfPresent(setup.map_info_pane.MaxPlayersValue, "%u", (unsigned)GameSetup_CountPlayers(&setup.map_info));
    GameSetup_SetTextIfPresent(setup.map_info_pane.MapNameValue, "%s", setup.map_name[0] ? setup.map_name : setup.map_path);

    uiimport.ResolveMapInfoString(&setup.map_info,
                                  setup.map_info.playersRecommended,
                                  value,
                                  sizeof(value));
    uiimport.SanitizeMapInfoText(value);
    GameSetup_SetTextIfPresent(setup.map_info_pane.SuggestedPlayersValue, "%s", value);

    GameSetup_SetTextIfPresent(setup.map_info_pane.MapSizeValue,
                               "%s",
                               uiimport.MapSizeName(setup.map_info.playableArea.width,
                                                    setup.map_info.playableArea.height));

    tileset = uiimport.MapTilesetName((BYTE)setup.map_info.mainGroundType);
    GameSetup_SetTextIfPresent(setup.map_info_pane.MapTilesetValue, "%s", tileset ? tileset : UI_GetString("UNKNOWNMAP_TILESET"));

    uiimport.ResolveMapInfoString(&setup.map_info,
                                  setup.map_info.mapDescription,
                                  value,
                                  sizeof(value));
    uiimport.SanitizeMapInfoText(value);
    GameSetup_SetTextIfPresent(setup.map_info_pane.MapDescValue, "%s", value[0] ? value : UI_GetString("UNKNOWNMAP_DESCRIPTION"));

    if (setup.map_info_pane.MinimapImage && uiimport.FindMapPreviewTexture) {
        PATHSTR preview;

        if (uiimport.FindMapPreviewTexture(setup.map_path, preview, sizeof(preview))) {
            UI_SetTexture(setup.map_info_pane.MinimapImage, preview, false);
            UI_SetHidden(setup.map_info_pane.MinimapImage, false);
        } else {
            UI_SetHidden(setup.map_info_pane.MinimapImage, true);
        }
    }
}

static void GameSetup_LoadSelectedMap(void) {
    LPCSTR selected_path = LAN_SelectedMapPath();
    LPCSTR selected_name = LAN_SelectedMapName();
    LPCSTR debug_path = NULL;

    if (setup.have_map_info) {
        uiimport.FreeMapInfo(&setup.map_info);
        setup.have_map_info = false;
    }
    setup.map_path[0] = '\0';
    setup.map_name[0] = '\0';

    if (selected_path) {
        snprintf(setup.map_path, sizeof(setup.map_path), "%s", selected_path);
    }
    if (selected_name) {
        snprintf(setup.map_name, sizeof(setup.map_name), "%s", selected_name);
    }

    if (!setup.map_path[0] && uiimport.Cvar_String) {
        debug_path = uiimport.Cvar_String("ui_game_setup_map", "");
        if (debug_path && debug_path[0]) {
            snprintf(setup.map_path, sizeof(setup.map_path), "%s", debug_path);
            snprintf(setup.map_name, sizeof(setup.map_name), "%s", GameSetup_BaseName(debug_path));
        }
    }

    if (setup.map_path[0] && uiimport.ReadMapInfo) {
        setup.have_map_info = uiimport.ReadMapInfo(setup.map_path, &setup.map_info);
        GameSetup_UseResolvedMapTitle();
    }
}

void GameSetup_LoadMap(LPCSTR map_path) {
    if (!map_path || !map_path[0]) {
        return;
    }

    if (setup.have_map_info) {
        uiimport.FreeMapInfo(&setup.map_info);
        setup.have_map_info = false;
    }
    snprintf(setup.map_path, sizeof(setup.map_path), "%s", map_path);
    snprintf(setup.map_name, sizeof(setup.map_name), "%s", GameSetup_BaseName(map_path));
    if (uiimport.ReadMapInfo) {
        setup.have_map_info = uiimport.ReadMapInfo(setup.map_path, &setup.map_info);
        GameSetup_UseResolvedMapTitle();
    }
    GameSetup_SetTextIfPresent(setup.game_name, "%s", setup.map_name[0] ? setup.map_name : "Local Game");
    GameSetup_UpdateMapInfo();
    GameSetup_PopulateSlots();
    GameSetup_PublishLobby();
}

static void GameSetup_BuildFrames(void) {
    setup.ready = false;
    setup.root = setup.frames.GameChatroom;
    if (!setup.root) {
        uiimport.Printf("GameSetup_BuildFrames: GameChatroom missing\n");
        return;
    }
    UI_SetAllPoints(setup.root);

    setup.game_name = setup.frames.GameNameValue;
    setup.start_button = setup.frames.StartGameButton;
    setup.cancel_button = setup.frames.CancelButton;
    setup.team_container = setup.frames.TeamSetupContainer;

    GameSetup_BuildSlotRows();
    GameSetup_BindMapInfoPane(setup.frames.MapInfoPaneContainer);

    GameSetup_EnsureChatText();
    GameSetup_UpdateStartButton();
    UI_SetOnClick(setup.cancel_button, "menu_startserver");
    setup.ready = true;
}

static void GameSetup_Init(void) {
    uiimport.Printf("GameSetup_Init\n");
    UI_PreloadGlueSceneModels();
    if (!setup.root) {
        GameSetup_BuildFrames();
    }
    if (!setup.ready) {
        return;
    }
    GameSetup_LoadSelectedMap();
    GameSetup_SetTextIfPresent(setup.game_name, "%s", setup.map_name[0] ? setup.map_name : "Local Game");
    GameSetup_UpdateMapInfo();
    GameSetup_PopulateSlots();
    GameSetup_UpdateStartButton();
    GameSetup_PublishLobby();
}

static void GameSetup_Shutdown(void) {
}

static void GameSetup_Refresh(int msec) {
    (void)msec;
}

static void GameSetup_Draw(void) {
    if (!setup.ready) {
        return;
    }

    UI_DrawGlueScene("MultiplayerPreGameChat Stand");
    UI_DrawFrame(setup.root);
}

static void GameSetup_KeyEvent(int key, BOOL down) {
    if (!down) {
        return;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        GameSetup_SubmitChat();
    }
}

static void GameSetup_MouseEvent(int x, int y, int buttons) {
    (void)x;
    (void)y;
    (void)buttons;
}

BOOL GameSetup_StartGame(void) {
    char command[8192];
    DWORD slot_count;
    size_t used = 0;

    if (!GameSetup_IsHost() || !setup.map_path[0]) {
        return false;
    }
    command[0] = '\0';
    slot_count = GameSetup_LobbySlotCount();
    GameSetup_AppendLobbyConfig(command, sizeof(command), &used);
    FOR_LOOP(i, slot_count) {
        GameSetup_AppendLobbySlot(command, sizeof(command), &used, i);
    }
    GameSetup_AppendMapCommand(command, sizeof(command), &used, setup.map_path);
    uiimport.Cmd_ExecuteText(command);
    return true;
}

void GameSetup_SetSlotType(DWORD slot, DWORD value) {
    if (!GameSetup_IsHost()) {
        return;
    }
    if (slot >= MAX_PLAYERS || !setup.configs[slot].visible) {
        return;
    }
    if (value > GAME_SETUP_SLOT_CLOSED) {
        return;
    }
    setup.configs[slot].type = (gameSetupSlotType_t)value;
    if (setup.configs[slot].type == GAME_SETUP_SLOT_HUMAN && !setup.configs[slot].name[0]) {
        snprintf(setup.configs[slot].name, sizeof(setup.configs[slot].name), "Player");
    }
    GameSetup_DrawSlotConfig(slot);
    GameSetup_PublishSlot(slot);
}

void GameSetup_SetSlotRace(DWORD slot, DWORD value) {
    if (!GameSetup_IsHost()) {
        return;
    }
    if (slot >= MAX_PLAYERS || !setup.configs[slot].visible || value > kPlayerRaceNightElf) {
        return;
    }
    setup.configs[slot].race = (playerRace_t)value;
    GameSetup_DrawSlotConfig(slot);
    GameSetup_PublishSlot(slot);
}

void GameSetup_CycleSlotTeam(DWORD slot) {
    DWORD max_teams;

    if (!GameSetup_IsHost() ||
        slot >= MAX_PLAYERS ||
        !setup.configs[slot].visible ||
        GameSetup_FixedPlayerSettings()) {
        return;
    }
    max_teams = GameSetup_SelectableTeamCount();
    setup.configs[slot].team = (setup.configs[slot].team + 1) % max_teams;
    GameSetup_DrawSlotConfig(slot);
    GameSetup_PublishSlot(slot);
}

void GameSetup_CycleSlotColor(DWORD slot) {
    if (!GameSetup_IsHost()) {
        return;
    }
    if (slot >= MAX_PLAYERS || !setup.configs[slot].visible) {
        return;
    }
    setup.configs[slot].color = (setup.configs[slot].color + 1) % 16;
    GameSetup_DrawSlotConfig(slot);
    GameSetup_PublishSlot(slot);
}

void GameSetup_UpdateLobbySetup(lobbyState_t const *state) {
    BOOL changed_map;

    if (!state || !state->active || !state->map_path[0]) {
        return;
    }
    setup.applying_lobby = true;
    changed_map = strcmp(setup.map_path, state->map_path) != 0;
    if (changed_map || !setup.ready) {
        GameSetup_LoadMap(state->map_path);
    }
    snprintf(setup.map_path, sizeof(setup.map_path), "%s", state->map_path);
    snprintf(setup.map_name, sizeof(setup.map_name), "%s",
             state->map_name[0] ? state->map_name : GameSetup_BaseName(state->map_path));
    GameSetup_SetTextIfPresent(setup.game_name, "%s", setup.map_name[0] ? setup.map_name : "Local Game");
    memset(setup.configs, 0, sizeof(setup.configs));
    FOR_LOOP(i, state->slot_count) {
        lobbySlot_t const *slot = &state->slots[i];

        setup.configs[i].visible = slot->visible;
        setup.configs[i].map_player = slot->map_player;
        setup.configs[i].type = (gameSetupSlotType_t)slot->type;
        setup.configs[i].race = slot->race;
        setup.configs[i].team = slot->team;
        setup.configs[i].color = slot->color;
        GameSetup_CopyString(setup.configs[i].name, sizeof(setup.configs[i].name), slot->name);
    }
    GameSetup_UpdateMapInfo();
    GameSetup_DrawSlotConfigs();
    GameSetup_UpdateStartButton();
    setup.applying_lobby = false;
}

uiScreen_t gameSetupScreen = {
    .name = "game-setup",
    .load = GameSetup_LoadScreen,
    .init = GameSetup_Init,
    .shutdown = GameSetup_Shutdown,
    .refresh = GameSetup_Refresh,
    .draw = GameSetup_Draw,
    .key_event = GameSetup_KeyEvent,
    .mouse_event = GameSetup_MouseEvent,
};
