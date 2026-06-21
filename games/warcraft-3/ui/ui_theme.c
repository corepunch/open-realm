/*
 * ui_theme.c - Warcraft III skin/theme lookup.
 */

#include <ctype.h>
#include <stdlib.h>
#include "ui_local.h"

#define MAX_THEME_ENTRIES 1024

typedef struct {
    UINAME category;
    UINAME key;
    PATHSTR value;
} themeEntry_t;

static themeEntry_t theme_entries[MAX_THEME_ENTRIES];
static DWORD theme_count = 0;

static char *UI_ThemeTrim(char *text) {
    text += strspn(text, " \t\r\n");
    for (char *end = text + strlen(text); end > text && isspace((unsigned char)end[-1]); )
        *--end = '\0';
    return text;
}

void UI_ClearTheme(void) {
    memset(theme_entries, 0, sizeof(theme_entries));
    theme_count = 0;
}

void UI_LoadTheme(LPCSTR fileName) {
    void *buffer = NULL;
    int size = uiimport.FS_ReadFile(fileName, &buffer);
    LPSTR text;
    char *cursor;
    UINAME category = "Default";

    UI_ClearTheme();

    if (size < 0 || !buffer) {
        return;
    }

    text = uiimport.MemAlloc((DWORD)size + 1);
    if (!text) {
        uiimport.FS_FreeFile(buffer);
        return;
    }
    memcpy(text, buffer, (size_t)size);
    text[size] = '\0';
    uiimport.FS_FreeFile(buffer);

    cursor = text;
    while (*cursor && theme_count < MAX_THEME_ENTRIES) {
        char *line = cursor;
        char *eq;
        char *key;
        char *value;

        while (*cursor && *cursor != '\n' && *cursor != '\r') {
            cursor++;
        }
        if (*cursor) {
            *cursor++ = '\0';
            while (*cursor == '\n' || *cursor == '\r') {
                cursor++;
            }
        }

        key = UI_ThemeTrim(line);
        if (!*key || *key == '/' || *key == '#') {
            continue;
        }
        if (*key == '[') {
            char *end = strchr(key + 1, ']');
            if (end) {
                *end = '\0';
                snprintf(category, sizeof(category), "%s", UI_ThemeTrim(key + 1));
            }
            continue;
        }
        eq = strchr(key, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        value = UI_ThemeTrim(eq + 1);
        key = UI_ThemeTrim(key);
        if (!*key || !*value) {
            continue;
        }

        snprintf(theme_entries[theme_count].category, sizeof(theme_entries[theme_count].category), "%s", category);
        snprintf(theme_entries[theme_count].key, sizeof(theme_entries[theme_count].key), "%s", key);
        snprintf(theme_entries[theme_count].value, sizeof(theme_entries[theme_count].value), "%s", value);
        theme_count++;
    }

    uiimport.MemFree(text);
}

static LPCSTR UI_FindThemeValue(LPCSTR entry, LPCSTR category) {
    FOR_LOOP(i, theme_count) {
        if (!strcmp(theme_entries[i].key, entry) &&
            (!category || !strcmp(theme_entries[i].category, category))) {
            return theme_entries[i].value;
        }
    }
    return NULL;
}

static LPCSTR UI_ThemeRaceCategory(DWORD race) {
    switch (race) {
        case kPlayerRaceHuman: return "Human";
        case kPlayerRaceOrc: return "Orc";
        case kPlayerRaceUndead: return "Undead";
        case kPlayerRaceNightElf: return "NightElf";
        default: return NULL;
    }
}

static LPCSTR UI_ThemeEffectiveCategory(LPCSTR category) {
    LPCPLAYER ps;
    LPCSTR player_category;

    if (!category || !*category || strcmp(category, "Default")) {
        return category;
    }

    ps = uiimport.GetPlayerState();
    player_category = ps ? UI_ThemeRaceCategory(ps->race) : NULL;
    return player_category ? player_category : category;
}

LPCSTR Theme_String(LPCSTR entry, LPCSTR category) {
    LPCSTR filename = NULL;
    char versioned[128];
    LPCSTR fallback = "Default";

    if (!category || !*category) {
        category = fallback;
    }
    category = UI_ThemeEffectiveCategory(category);

    filename = UI_FindThemeValue(entry, category);
    if (!filename && strcmp(category, fallback)) {
        filename = UI_FindThemeValue(entry, fallback);
    }

    if (filename) {
        return filename;
    }

    snprintf(versioned, sizeof(versioned), "%s_V1", entry);
    filename = UI_FindThemeValue(versioned, category);
    if (!filename && strcmp(category, fallback)) {
        filename = UI_FindThemeValue(versioned, fallback);
    }
    if (filename) {
        return filename;
    }

    snprintf(versioned, sizeof(versioned), "%s_V0", entry);
    filename = UI_FindThemeValue(versioned, category);
    if (!filename && strcmp(category, fallback)) {
        filename = UI_FindThemeValue(versioned, fallback);
    }
    if (filename) {
        return filename;
    }

    return entry;
}

FLOAT Theme_Float(LPCSTR entry, LPCSTR category) {
    return atof(Theme_String(entry, category));
}

COLOR32 Theme_ListBoxSelectionColor(void) {
    return MAKE(COLOR32, 0, 0, 255, 255);
}

COLOR32 Theme_ListBoxTextColor(void) {
    return COLOR32_WHITE;
}

COLOR32 Theme_ListBoxSelectedTextColor(void) {
    return MAKE(COLOR32, 252, 210, 17, 255);
}

COLOR32 Theme_ListBoxIconTextColor(void) {
    return MAKE(COLOR32, 252, 210, 17, 255);
}
