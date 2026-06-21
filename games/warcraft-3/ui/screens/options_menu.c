/*
 * ui/screens/options_menu.c — Main-menu options screen controller.
 */

#include <stdlib.h>

#include "../ui_local.h"
#include "../ui_screen.h"
#include "../generated/options_menu.h"

#ifndef SDLK_RETURN
#define SDLK_RETURN 13
#endif
#ifndef SDLK_KP_ENTER
#define SDLK_KP_ENTER 1073741912
#endif

#define OPTIONS_ARRAY_COUNT(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))
#define OPTIONS_GAME_PORT_MIN 1024
#define OPTIONS_GAME_PORT_MAX 49151

typedef enum {
    OPTIONS_PANEL_GAMEPLAY,
    OPTIONS_PANEL_VIDEO,
    OPTIONS_PANEL_SOUND,
} optionsPanel_t;

typedef struct {
    LPCSTR text;
    LONG value;
} optionsMenuItem_t;

typedef struct {
    DWORD width;
    DWORD height;
} optionsResolution_t;

static OptionsMenu_t options_menu;
static optionsPanel_t current_panel = OPTIONS_PANEL_GAMEPLAY;

static BOOL OptionsMenu_LoadScreen(void) {
    return OptionsMenu_Load(&options_menu);
}

static void OptionsMenu_SetHidden(LPFRAMEDEF frame, BOOL hidden) {
    if (frame) {
        UI_SetHidden(frame, hidden);
    }
}

static LPFRAMEDEF OptionsMenu_EnsureEditText(LPFRAMEDEF edit, LPCSTR name) {
    LPFRAMEDEF text;
    LPCFRAMEDEF template;

    if (!edit || !name || !*name) {
        return NULL;
    }
    if (!edit->Edit.TextFrame[0]) {
        snprintf(edit->Edit.TextFrame, sizeof(edit->Edit.TextFrame), "%s", name);
    }
    text = UI_FindChildFrame(edit, edit->Edit.TextFrame);
    if (text) {
        return text;
    }

    text = UI_Spawn(FT_TEXT, edit);
    if (!text) {
        return NULL;
    }
    snprintf(text->Name, sizeof(text->Name), "%s", edit->Edit.TextFrame);
    template = UI_FindFrame("StandardEditBoxTextTemplate");
    if (template) {
        text->DecorateFileNames = template->DecorateFileNames;
        text->Width = template->Width;
        text->Height = template->Height;
        text->Font = template->Font;
    }
    text->Font.Justification.Horizontal = FONT_JUSTIFYLEFT;
    text->Font.Justification.Vertical = FONT_JUSTIFYMIDDLE;
    return text;
}

static LPCSTR OptionsMenu_EditText(LPFRAMEDEF edit) {
    LPFRAMEDEF text = edit ? UI_FindChildFrame(edit, edit->Edit.TextFrame) : NULL;
    return text && text->Text ? text->Text : "";
}

static void OptionsMenu_SetEditText(LPFRAMEDEF edit, LPCSTR text) {
    LPFRAMEDEF text_frame = OptionsMenu_EnsureEditText(edit, "GamePortEditBoxText");

    if (text_frame) {
        UI_SetText(text_frame, "%s", text ? text : "");
    }
}

static LPFRAMEDEF OptionsMenu_PopupTitleText(LPFRAMEDEF popup) {
    LPFRAMEDEF title;
    LPFRAMEDEF text;

    if (!popup) {
        return NULL;
    }
    title = UI_FindChildFrame(popup, popup->Popup.TitleFrame);
    text = title ? UI_FindChildFrame(title, "StandardPopupMenuTitleTextTemplate") : NULL;
    if (!text) {
        text = title ? UI_FindChildFrame(title, "CampaignPopupMenuTitleTextTemplate") : NULL;
    }
    return text ? text : title;
}

static void OptionsMenu_SetPopupTitle(LPFRAMEDEF popup, LPCSTR text) {
    LPFRAMEDEF title = OptionsMenu_PopupTitleText(popup);

    if (title) {
        UI_SetText(title, "%s", text ? text : "");
    }
}

static void OptionsMenu_SetPopupItems(LPFRAMEDEF popup,
                                      LPFRAMEDEF menu,
                                      optionsMenuItem_t const *items,
                                      DWORD count,
                                      DWORD selected) {
    if (!menu || !items || !count) {
        return;
    }
    UI_MenuClearItems(menu);
    FOR_LOOP(i, count) {
        UI_MenuAddItem(menu, UI_GetString(items[i].text), items[i].value);
    }
    UI_SetHidden(menu, true);
    if (selected >= count) {
        selected = 0;
    }
    OptionsMenu_SetPopupTitle(popup, UI_GetString(items[selected].text));
}

static int OptionsMenu_CvarInteger(LPCSTR name, int fallback) {
    LPCSTR value = uiimport.Cvar_String ? uiimport.Cvar_String(name, NULL) : NULL;

    return value && *value ? atoi(value) : fallback;
}

static DWORD OptionsMenu_CvarSelection(LPCSTR name, DWORD fallback, DWORD count) {
    int value = OptionsMenu_CvarInteger(name, (int)fallback);

    if (value < 0 || value >= (int)count) {
        return fallback < count ? fallback : 0;
    }
    return (DWORD)value;
}

static void OptionsMenu_SetPopupCvar(LPFRAMEDEF menu, LPCSTR name) {
    if (menu) {
        UI_SetOnClick(menu, "seta %s %%u", name);
    }
}

static void OptionsMenu_InitGamePortEditBox(void) {
    LPCSTR port = uiimport.Cvar_String ? uiimport.Cvar_String("game_port", "") : "";

    if (options_menu.GamePortEditBox) {
        options_menu.GamePortEditBox->Edit.MaxChars = 5;
    }
    OptionsMenu_SetEditText(options_menu.GamePortEditBox, port);
}

static void OptionsMenu_ApplyGamePort(void) {
    LPCSTR text = OptionsMenu_EditText(options_menu.GamePortEditBox);
    int port = text && *text ? atoi(text) : 0;
    char command[64];

    if (port < OPTIONS_GAME_PORT_MIN || port > OPTIONS_GAME_PORT_MAX) {
        OptionsMenu_InitGamePortEditBox();
        return;
    }
    snprintf(command, sizeof(command), "seta game_port %d\n", port);
    if (uiimport.Cmd_ExecuteText) {
        uiimport.Cmd_ExecuteText(command);
    }
}

static void OptionsMenu_InitVideoMenus(void) {
    static optionsMenuItem_t const quality_items[] = {
        { "LOW", 0 },
        { "MEDIUM", 1 },
        { "HIGH", 2 },
    };
    static optionsMenuItem_t const toggle_items[] = {
        { "OFF", 0 },
        { "ON", 1 },
    };
    static optionsResolution_t const resolutions[] = {
        { 640, 480 },
        { 800, 600 },
        { 1024, 768 },
        { 1152, 864 },
        { 1280, 720 },
        { 1280, 960 },
        { 1280, 1024 },
        { 1366, 768 },
        { 1600, 900 },
        { 1600, 1200 },
        { 1920, 1080 },
        { 1920, 1200 },
        { 2560, 1440 },
    };
    DWORD selected;
    char current_resolution[32];

    if (options_menu.ResolutionPopupMenuMenu) {
        UI_MenuClearItems(options_menu.ResolutionPopupMenuMenu);
        FOR_LOOP(i, OPTIONS_ARRAY_COUNT(resolutions)) {
            char text[32];

            snprintf(text,
                     sizeof(text),
                     "%ux%u",
                     (unsigned)resolutions[i].width,
                     (unsigned)resolutions[i].height);
            UI_MenuAddItem(options_menu.ResolutionPopupMenuMenu, text, (LONG)i);
        }
        selected = OptionsMenu_CvarSelection("vid_mode", 2, OPTIONS_ARRAY_COUNT(resolutions));
        snprintf(current_resolution,
                 sizeof(current_resolution),
                 "%ux%u",
                 (unsigned)resolutions[selected].width,
                 (unsigned)resolutions[selected].height);
        OptionsMenu_SetPopupTitle(options_menu.ResolutionMenu, current_resolution);
        UI_SetHidden(options_menu.ResolutionPopupMenuMenu, true);
        OptionsMenu_SetPopupCvar(options_menu.ResolutionPopupMenuMenu, "vid_mode");
    }

    OptionsMenu_SetPopupItems(options_menu.ModelDetailMenu,
                              options_menu.ModelDetailPopupMenuMenu,
                              quality_items,
                              OPTIONS_ARRAY_COUNT(quality_items),
                              OptionsMenu_CvarSelection("r_model_detail", 2, OPTIONS_ARRAY_COUNT(quality_items)));
    OptionsMenu_SetPopupCvar(options_menu.ModelDetailPopupMenuMenu, "r_model_detail");
    OptionsMenu_SetPopupItems(options_menu.AnimQualityMenu,
                              options_menu.AnimQualityPopupMenuMenu,
                              quality_items,
                              OPTIONS_ARRAY_COUNT(quality_items),
                              OptionsMenu_CvarSelection("r_anim_quality", 2, OPTIONS_ARRAY_COUNT(quality_items)));
    OptionsMenu_SetPopupCvar(options_menu.AnimQualityPopupMenuMenu, "r_anim_quality");
    OptionsMenu_SetPopupItems(options_menu.TextureQualityMenu,
                              options_menu.TextureQualityPopupMenuMenu,
                              quality_items,
                              OPTIONS_ARRAY_COUNT(quality_items),
                              OptionsMenu_CvarSelection("r_texture_quality", 2, OPTIONS_ARRAY_COUNT(quality_items)));
    OptionsMenu_SetPopupCvar(options_menu.TextureQualityPopupMenuMenu, "r_texture_quality");
    OptionsMenu_SetPopupItems(options_menu.ParticlesMenu,
                              options_menu.ParticlesPopupMenuMenu,
                              quality_items,
                              OPTIONS_ARRAY_COUNT(quality_items),
                              OptionsMenu_CvarSelection("r_particles", 2, OPTIONS_ARRAY_COUNT(quality_items)));
    OptionsMenu_SetPopupCvar(options_menu.ParticlesPopupMenuMenu, "r_particles");
    OptionsMenu_SetPopupItems(options_menu.LightsMenu,
                              options_menu.LightsPopupMenuMenu,
                              quality_items,
                              OPTIONS_ARRAY_COUNT(quality_items),
                              OptionsMenu_CvarSelection("r_lights", 2, OPTIONS_ARRAY_COUNT(quality_items)));
    OptionsMenu_SetPopupCvar(options_menu.LightsPopupMenuMenu, "r_lights");
    OptionsMenu_SetPopupItems(options_menu.ShadowsMenu,
                              options_menu.ShadowsPopupMenuMenu,
                              toggle_items,
                              OPTIONS_ARRAY_COUNT(toggle_items),
                              OptionsMenu_CvarSelection("r_unit_shadows", 1, OPTIONS_ARRAY_COUNT(toggle_items)));
    OptionsMenu_SetPopupCvar(options_menu.ShadowsPopupMenuMenu, "r_unit_shadows");
    OptionsMenu_SetPopupItems(options_menu.OcclusionMenu,
                              options_menu.OcclusionPopupMenuMenu,
                              toggle_items,
                              OPTIONS_ARRAY_COUNT(toggle_items),
                              OptionsMenu_CvarSelection("r_occlusion", 1, OPTIONS_ARRAY_COUNT(toggle_items)));
    OptionsMenu_SetPopupCvar(options_menu.OcclusionPopupMenuMenu, "r_occlusion");
}

static void OptionsMenu_InitGameplayMenus(void) {
    static optionsMenuItem_t const chat_support_items[] = {
        { "Default", 0 },
        { "English", 1 },
        { "German", 2 },
        { "English UK", 3 },
        { "Spanish", 4 },
        { "French", 5 },
        { "Italian", 6 },
        { "Korean", 7 },
        { "Polish", 8 },
        { "Russian", 9 },
        { "PRC Chinese", 10 },
        { "Taiwan Chinese", 11 },
    };

    OptionsMenu_InitGamePortEditBox();
    OptionsMenu_SetPopupItems(options_menu.ChatSupportMenu,
                              options_menu.ChatSupportPopupMenuMenu,
                              chat_support_items,
                              OPTIONS_ARRAY_COUNT(chat_support_items),
                              OptionsMenu_CvarSelection("ui_chat_support", 0, OPTIONS_ARRAY_COUNT(chat_support_items)));
    OptionsMenu_SetPopupCvar(options_menu.ChatSupportPopupMenuMenu, "ui_chat_support");
}

static void OptionsMenu_InitSoundMenus(void) {
    static optionsMenuItem_t const provider_items[] = {
        { "Miles Fast 2D Positional Audio", 0 },
        { "Miles Emulated 3D", 1 },
        { "Creative Labs EAX 2 (tm)", 2 },
        { "Dolby Surround", 3 },
    };

    OptionsMenu_SetPopupItems(options_menu.ProviderMenu,
                              options_menu.ProviderPopupMenuMenu,
                              provider_items,
                              OPTIONS_ARRAY_COUNT(provider_items),
                              OptionsMenu_CvarSelection("s_provider", 1, OPTIONS_ARRAY_COUNT(provider_items)));
    OptionsMenu_SetPopupCvar(options_menu.ProviderPopupMenuMenu, "s_provider");
}

static void OptionsMenu_InitPopupMenus(void) {
    OptionsMenu_InitGameplayMenus();
    OptionsMenu_InitVideoMenus();
    OptionsMenu_InitSoundMenus();
}

static void OptionsMenu_SetPanel(optionsPanel_t panel) {
    current_panel = panel;

    OptionsMenu_SetHidden(options_menu.GameplayPanel, panel != OPTIONS_PANEL_GAMEPLAY);
    OptionsMenu_SetHidden(options_menu.VideoPanel, panel != OPTIONS_PANEL_VIDEO);
    OptionsMenu_SetHidden(options_menu.SoundPanel, panel != OPTIONS_PANEL_SOUND);
}

static void OptionsMenu_Init(void) {
    uiimport.Printf("OptionsMenu_Init\n");
    UI_PreloadGlueSceneModels();
    current_panel = OPTIONS_PANEL_GAMEPLAY;

    UI_SetOnClick(options_menu.GameplayButton, "menu_options_gameplay");
    UI_SetOnClick(options_menu.VideoButton, "menu_video");
    UI_SetOnClick(options_menu.SoundButton, "menu_options_sound");
    UI_SetOnClick(options_menu.OKButton, "menu_options_apply");
    UI_SetOnClick(options_menu.CancelButton, "menu_main");
    UI_SetOnClick(options_menu.ConfirmOKButton, "menu_main");
    UI_SetOnClick(options_menu.ConfirmCancelButton, "menu_options");

    OptionsMenu_InitPopupMenus();
    OptionsMenu_SetHidden(options_menu.OptionsConfirmDialog, true);
    OptionsMenu_SetPanel(current_panel);
}

static void OptionsMenu_Shutdown(void) {
}

static void OptionsMenu_Refresh(int msec) {
    (void)msec;
}

static void OptionsMenu_Draw(void) {
    UI_DrawGlueSceneLayers("Options Stand Alternate", "Options Stand");
    if (options_menu.OptionsMenu) {
        UI_DrawFrame(options_menu.OptionsMenu);
    }
}

static void OptionsMenu_KeyEvent(int key, BOOL down) {
    if (!down) {
        return;
    }
    if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) &&
        UI_EditHasFocus(options_menu.GamePortEditBox)) {
        OptionsMenu_ApplyGamePort();
        UI_ClearEditFocus();
    }
}

static void OptionsMenu_MouseEvent(int x, int y, int buttons) {
    (void)x;
    (void)y;
    (void)buttons;
}

void OptionsMenu_ShowGameplay(void) {
    OptionsMenu_SetPanel(OPTIONS_PANEL_GAMEPLAY);
}

void OptionsMenu_ShowVideo(void) {
    OptionsMenu_SetPanel(OPTIONS_PANEL_VIDEO);
}

void OptionsMenu_ShowSound(void) {
    OptionsMenu_SetPanel(OPTIONS_PANEL_SOUND);
}

void OptionsMenu_ShowKeys(void) {
    OptionsMenu_SetPanel(OPTIONS_PANEL_GAMEPLAY);
}

void OptionsMenu_Apply(void) {
    OptionsMenu_ApplyGamePort();
}

uiScreen_t optionsMenuScreen = {
    .name = "options",
    .load = OptionsMenu_LoadScreen,
    .init = OptionsMenu_Init,
    .shutdown = OptionsMenu_Shutdown,
    .refresh = OptionsMenu_Refresh,
    .draw = OptionsMenu_Draw,
    .key_event = OptionsMenu_KeyEvent,
    .mouse_event = OptionsMenu_MouseEvent,
};
