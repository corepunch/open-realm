/* hud_menu.c — Server-authored in-game pause menu using Blizzard's Esc FDF. */

#include "hud_local.h"
#include <time.h>

#define MENU_SAVE_LIST_TEXT 2048 // bytes; bounded transient list payload; used for serialized save rows
#define MENU_SAVE_ENUM_TEXT 4096 // bytes; bounded double-NUL file list; used for save-directory enumeration

typedef enum {
    MENU_PANEL_MAIN,
    MENU_PANEL_END_GAME,
    MENU_PANEL_CONFIRM_QUIT,
} menuPanel_t;

typedef enum {
    MENU_SAVE_PANEL_SAVE,
    MENU_SAVE_PANEL_LOAD,
} menuSavePanel_t;

typedef struct {
    char text[MENU_SAVE_LIST_TEXT];
    uint32_t used;
} menuSaveRows_t;

static int MenuSaveDebugLevel(void) {
    cstring_t value = gi.CvarString("wc3_save_menu_debug", "0");
    return value ? atoi(value) : 0;
}

static frameDef_t * MenuPanel(menuPanel_t panel) {
    switch (panel) {
        case MENU_PANEL_END_GAME: return hud.menu.EndGamePanel;
        case MENU_PANEL_CONFIRM_QUIT: return hud.menu.ConfirmQuitPanel;
        case MENU_PANEL_MAIN:
        default: return hud.menu.MainPanel;
    }
}

static frameDef_t * MenuSaveListBox(void) {
    return hud.save_list.inuse && hud.save_list.Parent == hud.save_list_art.MapListBox ? &hud.save_list : NULL;
}

static void MenuDefaultSaveName(string_t out, uint32_t out_size) {
    time_t now;
    struct tm const *local;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    now = time(NULL);
    local = localtime(&now);
    if (local && strftime(out, out_size, "%Y-%m-%d_%H-%M-%S", local) > 0)
        return;
    snprintf(out, out_size, "Save Game");
}

static bool MenuSavePanelReady(void) {
    return hud.save_menu.EscMenuSaveGamePanel && hud.save_menu.EscMenuSaveLoadContainer && MenuSaveListBox() &&
           hud.save_list_art.MapListBoxBackdrop && hud.save_list_art.MapListScrollBar &&
           hud.save_menu.SaveOnly && hud.save_menu.LoadOnly &&
           hud.save_menu.SaveGameFileEditBox && hud.save_menu.SaveGameFileEditBoxText &&
           hud.save_menu.SaveGameSaveButton && hud.save_menu.SaveGameCancelButton &&
           hud.save_menu.LoadGameLoadButton && hud.save_menu.LoadGameCancelButton;
}

static bool MenuSaveNameSafe(cstring_t name) {
    uint32_t len;

    if (!name || !*name) return false;
    len = (uint32_t)strlen(name);
    if (len >= CMDARG_LEN || !strcmp(name, ".") || !strcmp(name, "..") ||
        name[len - 1] == '.') return false;
    for (uint32_t i = 0; name[i]; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 0x20 || strchr("\\/:*?\"<>|\t\r\n", ch)) return false;
    }
    return true;
}

static bool MenuAppendSaveRow(menuSaveRows_t *rows, cstring_t display, cstring_t value) {
    int written;

    if (!rows || !display || !value || rows->used >= sizeof(rows->text)) return false;
    written = snprintf(rows->text + rows->used, sizeof(rows->text) - rows->used,
                       "%s%s\t%s", rows->used ? "\n" : "", display, value);
    if (written < 0 || (uint32_t)written >= sizeof(rows->text) - rows->used) return false;
    rows->used += (uint32_t)written;
    return true;
}

/* Build the authored LISTBOX payload as `display\thidden-value` rows. The
 * hidden basename is submitted by the transient-window placeholder expander.
 * Only saves whose headers still resolve to a map are exposed to Load. */
static uint32_t MenuBuildSaveList(void) {
    char names[MENU_SAVE_ENUM_TEXT] = { 0 };
    menuSaveRows_t rows = { 0 };
    uint32_t count = 0;
    frameDef_t * list = MenuSaveListBox();

    if (!MenuSavePanelReady() || !list) {
        if (MenuSaveDebugLevel())
            fprintf(stderr, "WC3_SAVE_MENU build-list unavailable panel=%d list=%d\n",
                    MenuSavePanelReady(), list != NULL);
        return 0;
    }
    gi.ListSaves(names, sizeof(names));
    if (MenuSaveDebugLevel())
        fprintf(stderr, "WC3_SAVE_MENU enumerate begin list=%s\n", list->Name);
    for (cstring_t name = names; *name; name += strlen(name) + 1) {
        PATHSTR path = { 0 };
        PATHSTR map = { 0 };
        cstring_t display;

        if (!MenuSaveNameSafe(name)) {
            if (MenuSaveDebugLevel())
                fprintf(stderr, "WC3_SAVE_MENU enumerate skip name=\"%s\" reason=unsafe\n", name);
            continue;
        }
        gi.SavePath(name, path, sizeof(path));
        if (!path[0]) {
            if (MenuSaveDebugLevel())
                fprintf(stderr, "WC3_SAVE_MENU enumerate skip name=\"%s\" reason=no-path\n", name);
            continue;
        }
        if (!G_GetSaveMap(path, map, sizeof(map))) {
            if (MenuSaveDebugLevel())
                fprintf(stderr,
                        "WC3_SAVE_MENU enumerate skip name=\"%s\" path=\"%s\" reason=unreadable-header\n",
                        name, path);
            continue;
        }
        display = !strcasecmp(name, "quick") ? "Quick Save" : name;
        if (!MenuAppendSaveRow(&rows, display, name)) {
            if (MenuSaveDebugLevel())
                fprintf(stderr, "WC3_SAVE_MENU enumerate stop reason=row-buffer-full count=%u\n",
                        (unsigned)count);
            break;
        }
        if (MenuSaveDebugLevel())
            fprintf(stderr,
                    "WC3_SAVE_MENU enumerate add index=%u display=\"%s\" value=\"%s\" map=\"%s\"\n",
                    (unsigned)count, display, name, map);
        count++;
    }
    UI_SetText(list, "%s", rows.text);
    if (MenuSaveDebugLevel())
        fprintf(stderr,
                "WC3_SAVE_MENU enumerate done count=%u bytes=%u list=%s size=%.4fx%.4f "
                "font=%u fontSize=%.4f text=\"%s\"\n",
                (unsigned)count, (unsigned)rows.used, list->Name, list->Width, list->Height,
                (unsigned)list->Font.Index, list->Font.Size, rows.text);
    return count;
}

static void MenuSetButton(frameDef_t * button, bool enabled, cstring_t command) {
    if (!button) return;
    UI_SetEnabled(button, enabled);
    if (enabled && command) UI_SetOnClick(button, "%s", command);
    else UI_SetOnClick(button, "");
}

/* Main-menu availability is intentionally independent of the authored list
 * control and save-header parser. A save directory entry is enough to offer
 * the Load panel; that panel performs the stricter readable-save filtering
 * before enabling its Load action. */
static bool MenuAnySaveExists(void) {
    char names[MENU_SAVE_ENUM_TEXT] = { 0 };

    gi.ListSaves(names, sizeof(names));
    for (cstring_t name = names; *name; name += strlen(name) + 1) {
        if (MenuSaveNameSafe(name)) return true;
    }
    return false;
}

static void MenuConfigureMainSaveLoad(void) {
    bool available = MenuSavePanelReady() && G_IsSinglePlayer();

    MenuSetButton(hud.menu.SaveGameButton, available, "menu_save_game");
    {
        bool any_save = MenuAnySaveExists();
        MenuSetButton(hud.menu.LoadGameButton,
                      available && any_save,
                      "menu_load_game");
        if (MenuSaveDebugLevel())
            fprintf(stderr,
                    "WC3_SAVE_MENU main-buttons available=%d anySave=%d saveEnabled=%d loadEnabled=%d\n",
                    available, any_save, available, available && any_save);
    }
}

void UI_LoadHudMenu(void) {
    if (hud.menu.EscMenuBackdrop) return;
    if (!EscMenuMainPanelGame_Load(&hud.menu)) return;
    if (!hud.menu.EscMenuBackdrop) {
        fprintf(stderr, "WC3 menu: missing EscMenuBackdrop\n");
        return;
    }

    /* Save/load is optional at bind time so reduced test/UI data can still use
     * the rest of the pause menu. Retail Warcraft data provides this panel. */
    EscMenuSaveGamePanel_Load(&hud.save_menu);
    if (hud.save_menu.FileListFrame && MapListBox_Load(&hud.save_list_art)) {
        frameDef_t * root = UI_CloneFrameTree(hud.save_list_art.MapListBox, hud.save_menu.FileListFrame);
        if (!root || !MapListBox_Bind(&hud.save_list_art, root)) {
            fprintf(stderr, "WC3 menu: failed to instantiate MapListBox in FileListFrame\n");
        } else {
            UI_SetPoint(root, FRAMEPOINT_TOPLEFT, hud.save_menu.FileListFrame, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
            UI_SetPoint(root, FRAMEPOINT_BOTTOMRIGHT, hud.save_menu.FileListFrame, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
            /* Retail supplies list state inside this chooser; using only the empty slot lost its backdrop and scrollbar. */
            UI_InitFrame(&hud.save_list, FT_LISTBOX);
            snprintf(hud.save_list.Name, sizeof(hud.save_list.Name), "SaveFileList");
            UI_SetParent(&hud.save_list, root);
            UI_SetPoint(&hud.save_list, FRAMEPOINT_TOPLEFT, root, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
            /* The native chooser stops above the adjacent filename box; the old full-height child left only a 0.002 gap. */
            UI_SetPoint(&hud.save_list, FRAMEPOINT_BOTTOMRIGHT, root, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.010f);
            /* CONTROL art is embedded during serialization, so copy its authored backdrop onto the native list payload. */
            hud.save_list.Texture = hud.save_list_art.MapListBoxBackdrop->Texture;
            FOR_LOOP(i, 4)
                hud.save_list.ListBox.Border = MAX(hud.save_list.ListBox.Border,
                                                   hud.save_list_art.MapListBoxBackdrop->Backdrop.BackgroundInsets[i]);
            hud.save_list.ListBox.EditTarget = hud.save_menu.SaveGameFileEditBox;
            UI_SetParent(hud.save_list_art.MapListScrollBar, &hud.save_list);
            /* MapListBox.fdf supplies only TOPRIGHT; retail list setup stretches the scrollbar to the chooser height. */
            UI_SetPoint(hud.save_list_art.MapListScrollBar, FRAMEPOINT_TOPRIGHT, &hud.save_list, FRAMEPOINT_TOPRIGHT, 0.0f, 0.0f);
            UI_SetPoint(hud.save_list_art.MapListScrollBar, FRAMEPOINT_BOTTOMRIGHT, &hud.save_list, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
            if (hud.save_menu.SaveGameFileEditBoxText)
                hud.save_list.Font = hud.save_menu.SaveGameFileEditBoxText->Font;
        }
    }

    UI_SetParent(hud.menu.EscMenuBackdrop, hud.menu.EscMenuMainPanel);
    /* The separate backdrop root loads after the panels. Nest all Esc-menu
     * panel subtrees below it so decoration serializes/draws before controls;
     * explicit anchors still target the bounded controller. */
    UI_SetParent(hud.menu.MainPanel, hud.menu.EscMenuBackdrop);
    UI_SetParent(hud.menu.EndGamePanel, hud.menu.EscMenuBackdrop);
    UI_SetParent(hud.menu.ConfirmQuitPanel, hud.menu.EscMenuBackdrop);
    UI_SetParent(hud.menu.HelpPanel, hud.menu.EscMenuBackdrop);
    UI_SetParent(hud.menu.TipsPanel, hud.menu.EscMenuBackdrop);
    UI_SetParent(hud.save_menu.EscMenuSaveGamePanel, hud.menu.EscMenuBackdrop);
    UI_SetHidden(hud.save_menu.EscMenuSaveGamePanel, true);

    /* Warsmash leaves these authored controls present but disabled. OpenRealm
     * wires the retail Save/Load panel to the existing serializer while keeping
     * unsupported Delete/overwrite-confirm behavior disabled for now.
     * Restart is backed by the same deferred current-map reload used by the
     * JASS RestartGame native and the game-result dialog. */
    MenuConfigureMainSaveLoad();
    UI_SetEnabled(hud.menu.OptionsButton, false);
    UI_SetEnabled(hud.menu.HelpButton, false);
    UI_SetEnabled(hud.menu.TipsButton, false);

    UI_SetText(hud.menu.PauseButtonText, "Resume Game");
    UI_SetText(hud.menu.ReturnButtonText, "Return to Game");
    UI_SetText(hud.menu.RestartButtonText, "Restart Mission");
    UI_SetOnClick(hud.menu.PauseButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_SetOnClick(hud.menu.ReturnButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_SetOnClick(hud.menu.EndGameButton, "menu_endgame");
    UI_SetOnClick(hud.menu.PreviousButton, "menu");
    UI_SetOnClick(hud.menu.RestartButton,
        UI_WINDOW_CLOSE_COMMAND_PREFIX "menu_restart");
    UI_SetOnClick(hud.menu.QuitButton,
        UI_WINDOW_CLOSE_COMMAND_PREFIX "menu_quit_game");
    UI_SetOnClick(hud.menu.ExitButton, "menu_confirm_exit");
    UI_SetOnClick(hud.menu.ConfirmQuitCancelButton, "menu_endgame");
    UI_SetOnClick(hud.menu.ConfirmQuitQuitButton, UI_WINDOW_QUIT_ACTION);

    if (MenuSavePanelReady()) {
        hud.save_menu.SaveGameFileEditBox->Edit.MaxChars = CMDARG_LEN - 1;
        UI_SetText(hud.save_menu.SaveGameFileEditBoxText, "");

        MenuSetButton(hud.save_menu.SaveGameSaveButton, true,
                      UI_WINDOW_CLOSE_COMMAND_PREFIX
                      "menu_save_named \"{SaveGameFileEditBox}\"");
        MenuSetButton(hud.save_menu.SaveGameDeleteButton, false, NULL);
        MenuSetButton(hud.save_menu.SaveGameCancelButton, true, "menu");
        MenuSetButton(hud.save_menu.LoadGameCancelButton, true, "menu");
    }
}

static void MenuSelectPanel(menuPanel_t panel) {
    frameDef_t * active = MenuPanel(panel);

    MenuConfigureMainSaveLoad();
    UI_SetHidden(hud.menu.EscMenuMainPanel, false);
    UI_SetHidden(hud.menu.EscMenuBackdrop, false);
    UI_SetHidden(hud.menu.MainPanel, panel != MENU_PANEL_MAIN);
    UI_SetHidden(hud.menu.EndGamePanel, panel != MENU_PANEL_END_GAME);
    UI_SetHidden(hud.menu.ConfirmQuitPanel, panel != MENU_PANEL_CONFIRM_QUIT);
    UI_SetHidden(hud.menu.HelpPanel, true);
    UI_SetHidden(hud.menu.TipsPanel, true);
    UI_SetHidden(hud.save_menu.EscMenuSaveGamePanel, true);

    /* Warsmash sizes both the wrapper and backdrop from the active authored
     * panel. Keep the latest OpenRealm centering policy while updating those
     * dimensions for EndGame/ConfirmQuit instead of retaining MainPanel size. */
    if (active && active->Width > 0.0f && active->Height > 0.0f) {
        UI_SetSize(hud.menu.EscMenuMainPanel, active->Width, active->Height);
        UI_SetSize(hud.menu.EscMenuBackdrop, active->Width, active->Height);
    }
    UI_CenterFrame(hud.menu.EscMenuMainPanel);
    UI_CenterFrame(hud.menu.EscMenuBackdrop);
}

static void MenuWrite(edict_t * ent, menuPanel_t panel) {
    if (!ent || !ent->client || !ent->client->connected) return;
    /* Decorated Esc-menu art is resolved while the FDF is first loaded, so
     * establish the recipient's race skin before entering the template cache. */
    UI_SetCurrentClient(ent->client);
    if (!hud.menu.EscMenuBackdrop) {
        UI_SetCurrentClient(NULL);
        return;
    }
    /* Restarting the authoritative current map is a single-player mission
     * operation. Keep the authored button visible in multiplayer but disabled,
     * and enforce the same policy again in the command handler. */
    UI_SetEnabled(hud.menu.RestartButton, G_IsSinglePlayer());
    MenuSelectPanel(panel);
    UI_WriteWindow(ent, hud.menu.EscMenuMainPanel, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_MENU, .class_id = BZ_WC3_WINDOW_MENU,
        .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE));
    UI_SetCurrentClient(NULL);
}

static void MenuSelectSavePanel(menuSavePanel_t panel) {
    bool saving = panel == MENU_SAVE_PANEL_SAVE;
    frameDef_t * list = MenuSaveListBox();
    uint32_t saves = MenuBuildSaveList();
    bool can_load = G_IsSinglePlayer() && list && saves > 0;
    frameDef_t * root = hud.save_menu.EscMenuSaveGamePanel;
    char load_command[128] = { 0 };
    char default_name[CMDARG_LEN] = { 0 };

    UI_SetHidden(root, false);
    UI_SetHidden(hud.menu.MainPanel, true);
    UI_SetHidden(hud.menu.EndGamePanel, true);
    UI_SetHidden(hud.menu.ConfirmQuitPanel, true);
    UI_SetHidden(hud.menu.HelpPanel, true);
    UI_SetHidden(hud.menu.TipsPanel, true);
    UI_SetHidden(hud.save_menu.SaveOnly, !saving);
    UI_SetHidden(hud.save_menu.LoadOnly, saving);
    /* Retail switches these mutually exclusive controller containers; leaving them visible overlaps their controls. */
    UI_SetHidden(hud.save_menu.EscMenuOverwriteContainer, true);
    UI_SetHidden(hud.save_menu.EscMenuDeleteContainer, true);
    UI_SetSize(hud.menu.EscMenuMainPanel, hud.save_menu.EscMenuSaveLoadContainer->Width,
               hud.save_menu.EscMenuSaveLoadContainer->Height);
    UI_SetSize(hud.menu.EscMenuBackdrop, hud.save_menu.EscMenuSaveLoadContainer->Width,
               hud.save_menu.EscMenuSaveLoadContainer->Height);
    UI_CenterFrame(hud.menu.EscMenuMainPanel);
    UI_CenterFrame(hud.menu.EscMenuBackdrop);

    if (saving) {
        MenuDefaultSaveName(default_name, sizeof(default_name));
        UI_SetText(hud.save_menu.SaveGameFileEditBoxText, "%s", default_name);
    }

    if (can_load) {
        snprintf(load_command, sizeof(load_command),
                 UI_WINDOW_CLOSE_COMMAND_PREFIX
                 "menu_load_named \"{%s}\"",
                 list->Name);
    }
    MenuSetButton(hud.save_menu.LoadGameLoadButton, !saving && can_load, can_load ? load_command : NULL);
    MenuSetButton(hud.save_menu.SaveGameDeleteButton, saving && saves > 0,
                  saving && saves > 0 ? "menu_delete_named \"{SaveFileList}\"" : NULL);

    if (MenuSaveDebugLevel())
        fprintf(stderr,
                "WC3_SAVE_MENU panel mode=%s default=\"%s\" saves=%u "
                "list=%s listType=%d listSize=%.4fx%.4f loadEnabled=%d command=\"%s\"\n",
                saving ? "save" : "load", saving ? default_name : "",
                (unsigned)saves, list ? list->Name : "<missing>",
                list ? (int)list->Type : -1,
                list ? list->Width : 0.0f, list ? list->Height : 0.0f,
                !saving && can_load, can_load ? load_command : "");

}

static void MenuWriteSavePanel(edict_t * ent, menuSavePanel_t panel) {
    if (!ent || !ent->client || !ent->client->connected ||
        !G_IsSinglePlayer() || !MenuSavePanelReady()) return;

    UI_SetCurrentClient(ent->client);
    MenuSelectSavePanel(panel);
    /* Reuse the same unique menu identity as MainPanel. Replacing the window
     * preserves modal ownership while Main <-> Save/Load transitions occur. */
    UI_WriteWindowStart(&MAKE(uiWindowDef_t, .id = BZ_WC3_WINDOW_MENU, .class_id = BZ_WC3_WINDOW_MENU, .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE));
    UI_WriteFrameWithChildren(hud.menu.EscMenuMainPanel, NULL);
    UI_WriteFrame(&hud.save_list);
    UI_WriteFrameWithChildren(hud.save_list_art.MapListScrollBar, &hud.save_list);
    UI_WriteWindowEnd(ent);
    UI_SetCurrentClient(NULL);
}

void UI_ShowMainMenu(edict_t * ent) {
    MenuWrite(ent, MENU_PANEL_MAIN);
}

void UI_ShowGameMenuEndGame(edict_t * ent) {
    MenuWrite(ent, MENU_PANEL_END_GAME);
}

void UI_ShowGameMenuConfirmExit(edict_t * ent) {
    MenuWrite(ent, MENU_PANEL_CONFIRM_QUIT);
}

void UI_ShowGameMenuSave(edict_t * ent) {
    MenuWriteSavePanel(ent, MENU_SAVE_PANEL_SAVE);
}

void UI_ShowGameMenuLoad(edict_t * ent) {
    MenuWriteSavePanel(ent, MENU_SAVE_PANEL_LOAD);
}
