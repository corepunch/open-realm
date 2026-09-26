/*
 * ui/screens/options_menu.c — Main-menu options screen controller.
 */

#include <stdlib.h>
#include <math.h>

#include "common/video_modes.h"
#include "../menu_local.h"
#include "../menu_screen.h"
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
    cstring_t text;
    int32_t value;
} optionsMenuItem_t;

static cstring_t const opts_lft[] = {
    "GameplayPanel",
    "VideoPanel",
    "SoundPanel",
    NULL,
};

static OptionsMenu_t options_menu;
static optionsPanel_t current_panel = OPTIONS_PANEL_GAMEPLAY;
static bool music_controls_initialized;
static bool music_enabled_value;
static int music_volume_percent;

static bool OptionsMenu_LoadScreen(void) {
    return OptionsMenu_Load(&options_menu);
}

static void OptionsMenu_SetHidden(frameDef_t *frame, bool hidden) {
    if (frame) {
        UI_SetHidden(frame, hidden);
    }
}

static frameDef_t *OptionsMenu_EnsureEditText(frameDef_t *edit, cstring_t name) {
    frameDef_t *text;
    frameDef_t const *template;

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

static cstring_t OptionsMenu_EditText(frameDef_t *edit) {
    frameDef_t *text = edit ? UI_FindChildFrame(edit, edit->Edit.TextFrame) : NULL;
    return text && text->Text ? text->Text : "";
}

static void OptionsMenu_SetEditText(frameDef_t *edit, cstring_t text) {
    frameDef_t *text_frame = OptionsMenu_EnsureEditText(edit, "GamePortEditBoxText");

    if (text_frame) {
        UI_SetText(text_frame, "%s", text ? text : "");
    }
}

static frameDef_t *OptionsMenu_PopupTitleText(frameDef_t *popup) {
    frameDef_t *title;
    frameDef_t *text;

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

static void OptionsMenu_SetPopupTitle(frameDef_t *popup, cstring_t text) {
    frameDef_t *title = OptionsMenu_PopupTitleText(popup);

    if (title) {
        UI_SetText(title, "%s", text ? text : "");
    }
}

static void OptionsMenu_SetPopupItems(frameDef_t *popup,
                                      frameDef_t *menu,
                                      optionsMenuItem_t const *items,
                                      uint32_t count,
                                      uint32_t selected) {
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

static int OptionsMenu_CvarInteger(cstring_t name, int fallback) {
    cstring_t value = mi.Cvar_String(name, NULL);

    return value && *value ? atoi(value) : fallback;
}

static float OptionsMenu_CvarFloat(cstring_t name, float fallback) {
    cstring_t value = mi.Cvar_String(name, NULL);

    return value && *value ? (float)atof(value) : fallback;
}

static void OptionsMenu_SetCheckBox(frameDef_t *frame, bool checked) {
    if (!frame) return;
    frame->CheckBox.Checked = checked;
    if (checked) frame->ui_flags |= UIFLAG_CHECKED;
    else frame->ui_flags &= ~UIFLAG_CHECKED;
}

static bool OptionsMenu_CheckBoxValue(frameDef_t const *frame, bool fallback) {
    if (!frame) return fallback;
    return (frame->ui_flags & UIFLAG_CHECKED) != 0;
}

static int OptionsMenu_MusicSliderPercent(void) {
    float min_value, max_value, value;

    if (!options_menu.MusicVolumeSlider) return music_volume_percent;
    min_value = options_menu.MusicVolumeSlider->Slider.MinValue;
    max_value = options_menu.MusicVolumeSlider->Slider.MaxValue;
    value = options_menu.MusicVolumeSlider->Slider.InitialValue;
    if (max_value <= min_value) return music_volume_percent;
    return MAX(0, MIN(100, (int)floorf((value - min_value) * 100.0f /
        (max_value - min_value) + 0.5f)));
}

static void OptionsMenu_InitMusicControls(void) {
    float slider_value = 100.0f;

    music_enabled_value = OptionsMenu_CvarInteger("s_music", 1) != 0;
    if (options_menu.MusicVolumeSlider &&
        options_menu.MusicVolumeSlider->Slider.MaxValue > options_menu.MusicVolumeSlider->Slider.MinValue) {
        float min_value = options_menu.MusicVolumeSlider->Slider.MinValue;
        float max_value = options_menu.MusicVolumeSlider->Slider.MaxValue;
        slider_value = min_value + OptionsMenu_CvarFloat("s_musicvolume", 1.0f) * (max_value - min_value);
        music_volume_percent = MAX(0, MIN(100, (int)floorf(
            (slider_value - min_value) * 100.0f / (max_value - min_value) + 0.5f)));
    } else music_volume_percent = 100;
    OptionsMenu_SetCheckBox(options_menu.MusicCheckBox, music_enabled_value);
    if (options_menu.MusicVolumeSlider)
        options_menu.MusicVolumeSlider->Slider.InitialValue = slider_value;
    music_controls_initialized = true;
}

static void OptionsMenu_RefreshMusicControls(void) {
    bool enabled;
    int percent;
    char value[32];

    if (!music_controls_initialized) return;
    enabled = OptionsMenu_CheckBoxValue(options_menu.MusicCheckBox, music_enabled_value);
    percent = OptionsMenu_MusicSliderPercent();
    if (enabled != music_enabled_value) {
        music_enabled_value = enabled;
        mi.Cvar_Set("s_music", enabled ? "1" : "0");
    }
    if (percent != music_volume_percent) {
        music_volume_percent = percent;
        snprintf(value, sizeof(value), "%.2f", (double)percent / 100.0);
        mi.Cvar_Set("s_musicvolume", value);
    }
}

static uint32_t OptionsMenu_CvarSelection(cstring_t name, uint32_t fallback, uint32_t count) {
    int value = OptionsMenu_CvarInteger(name, (int)fallback);

    if (value < 0 || value >= (int)count) {
        return fallback < count ? fallback : 0;
    }
    return (uint32_t)value;
}

static void OptionsMenu_SetPopupCvar(frameDef_t *menu, cstring_t name) {
    if (menu) {
        UI_SetOnClick(menu, "seta %s %%u", name);
    }
}

static void OptionsMenu_InitGamePortEditBox(void) {
    cstring_t port = mi.Cvar_String("game_port", "");

    if (options_menu.GamePortEditBox) {
        options_menu.GamePortEditBox->Edit.MaxChars = 5;
    }
    OptionsMenu_SetEditText(options_menu.GamePortEditBox, port);
}

static void OptionsMenu_ApplyGamePort(void) {
    cstring_t text = OptionsMenu_EditText(options_menu.GamePortEditBox);
    int port = text && *text ? atoi(text) : 0;
    char command[64];

    if (port < OPTIONS_GAME_PORT_MIN || port > OPTIONS_GAME_PORT_MAX) {
        OptionsMenu_InitGamePortEditBox();
        return;
    }
    snprintf(command, sizeof(command), "seta game_port %d\n", port);
    if (mi.Cmd_ExecuteText) {
        mi.Cmd_ExecuteText(command);
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
    uint32_t selected;
    char current_resolution[32];

    if (options_menu.ResolutionPopupMenuMenu) {
        UI_MenuClearItems(options_menu.ResolutionPopupMenuMenu);
        FOR_LOOP(i, video_mode_count()) {
            char text[32];

            snprintf(text,
                     sizeof(text),
                     "%ux%u",
                     (unsigned)video_modes[i].width,
                     (unsigned)video_modes[i].height);
            UI_MenuAddItem(options_menu.ResolutionPopupMenuMenu, text, (int32_t)i);
        }
        /* Keep fixed-mode rows aligned with their persisted vid_mode indices.
         * Native is appended so adding it does not renumber existing configs. */
        UI_MenuAddItem(options_menu.ResolutionPopupMenuMenu, "Native", (int32_t)video_mode_count());
        selected = OptionsMenu_CvarSelection("vid_mode", BZ_VIDEO_MODE_DEFAULT, video_mode_count());
        if (OptionsMenu_CvarInteger("vid_native", 0)) {
            snprintf(current_resolution, sizeof(current_resolution), "Native");
        } else {
            snprintf(current_resolution,
                     sizeof(current_resolution),
                     "%ux%u",
                     (unsigned)video_modes[selected].width,
                     (unsigned)video_modes[selected].height);
        }
        OptionsMenu_SetPopupTitle(options_menu.ResolutionMenu, current_resolution);
        UI_SetHidden(options_menu.ResolutionPopupMenuMenu, true);
        UI_SetOnClick(options_menu.ResolutionPopupMenuMenu, "menu_video_mode");
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
    OptionsMenu_InitMusicControls();
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
    mi.Printf("OptionsMenu_Init\n");
    current_panel = OPTIONS_PANEL_GAMEPLAY;
    music_controls_initialized = false;

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
    OptionsMenu_RefreshMusicControls();
}

static void OptionsMenu_Draw(void) {
    if (options_menu.OptionsMenu) {
        UI_DrawFrame(options_menu.OptionsMenu);
    }
}

static void OptionsMenu_KeyEvent(int key, bool down) {
    if (!down) {
        return;
    }
    if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) &&
        UI_EditHasFocus(options_menu.GamePortEditBox)) {
        OptionsMenu_ApplyGamePort();
        UI_ClearEditFocus();
    }
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
    if (mi.Cmd_ExecuteText) {
        mi.Cmd_ExecuteText("vid_apply\nwriteconfig\n");
    }
}

uiScreen_t optionsMenuScreen = {
    .name = "options",
    .left = opts_lft,
    .glue = { .panel = UI_GLUE_OPTIONS, .tab = 1 },
    .load = OptionsMenu_LoadScreen,
    .init = OptionsMenu_Init,
    .shutdown = OptionsMenu_Shutdown,
    .refresh = OptionsMenu_Refresh,
    .draw = OptionsMenu_Draw,
    .key_event = OptionsMenu_KeyEvent,
};
