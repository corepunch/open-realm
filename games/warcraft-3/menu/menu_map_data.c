/*
 * menu_map_data.c - Warcraft III map browser data loaded inside menu.dll.
 */

#include "menu_local.h"
#include "common/mpq.h"

#include <stdlib.h>

#ifndef _WIN32
#include <strings.h>
#endif

static uint32_t UI_SFileReadStringLength(handle_t file) {
    uint32_t filePosition = SFileSetFilePointer(file, 0, 0, FILE_CURRENT);
    uint32_t stringLength = 1;
    while (true) {
        uint8_t ch = 0;
        SFileReadFile(file, &ch, 1, NULL, NULL);
        if (ch == 0)
            break;
        stringLength++;
    }
    SFileSetFilePointer(file, filePosition, 0, FILE_BEGIN);
    return stringLength;
}

static void UI_SFileReadString(handle_t file, string_t *lppString) {
    uint32_t stringLength = UI_SFileReadStringLength(file);
    *lppString = mi.MemAlloc(stringLength);
    SFileReadFile(file, *lppString, stringLength, NULL, NULL);
}

static bool UI_ReadInfoInto(handle_t archive, mapInfo_t *info) {
    handle_t file;

    if (!archive || !info)
        return false;
    if (!SFileOpenFileEx(archive, "war3map.w3i", SFILE_OPEN_FROM_MPQ, &file))
        return false;
    SFileReadFile(file, &info->fileFormat, 4, NULL, NULL);
    SFileReadFile(file, &info->numberOfSaves, sizeof(uint32_t), NULL, NULL);
    SFileReadFile(file, &info->editorVersion, sizeof(uint32_t), NULL, NULL);
    if (info->fileFormat >= 28) {
        SFileReadFile(file, &info->gameVersionMajor, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &info->gameVersionMinor, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &info->gameVersionPatch, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &info->gameVersionBuild, sizeof(uint32_t), NULL, NULL);
    }
    UI_SFileReadString(file, &info->mapName);
    UI_SFileReadString(file, &info->mapAuthor);
    UI_SFileReadString(file, &info->mapDescription);
    UI_SFileReadString(file, &info->playersRecommended);
    SFileReadFile(file, &info->cameraBounds, sizeof(mapCameraBounds_t), NULL, NULL);
    SFileReadFile(file, &info->playableArea, sizeof(size2_t), NULL, NULL);
    SFileReadFile(file, &info->flags, sizeof(uint32_t), NULL, NULL);
    SFileReadFile(file, &info->mainGroundType, sizeof(char), NULL, NULL);
    SFileReadFile(file, &info->campaignBackgroundNumber, sizeof(uint32_t), NULL, NULL);
    if (info->fileFormat >= 25)
        UI_SFileReadString(file, &info->loadingScreenModel);
    UI_SFileReadString(file, &info->loadingScreenText);
    UI_SFileReadString(file, &info->loadingScreenTitle);
    UI_SFileReadString(file, &info->loadingScreenSubtitle);
    if (info->fileFormat >= 25)
        SFileReadFile(file, &info->gameDataSet, sizeof(uint32_t), NULL, NULL);
    else
        SFileReadFile(file, &info->loadingScreenNumber, sizeof(uint32_t), NULL, NULL);
    if (info->fileFormat >= 25)
        UI_SFileReadString(file, &info->prologueScreenModel);
    UI_SFileReadString(file, &info->prologueScreenText);
    UI_SFileReadString(file, &info->prologueScreenTitle);
    UI_SFileReadString(file, &info->prologueScreenSubtitle);
    if (info->fileFormat >= 25) {
        SFileReadFile(file, &info->fogStyle, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &info->fogStartZ, sizeof(float), NULL, NULL);
        SFileReadFile(file, &info->fogEndZ, sizeof(float), NULL, NULL);
        SFileReadFile(file, &info->fogDensity, sizeof(float), NULL, NULL);
        SFileReadFile(file, &info->fogColor, sizeof(color32_t), NULL, NULL);
        SFileReadFile(file, &info->weatherID, sizeof(uint32_t), NULL, NULL);
        UI_SFileReadString(file, &info->soundEnvironment);
        SFileReadFile(file, &info->lightEnvironmentTileset, sizeof(uint8_t), NULL, NULL);
        SFileReadFile(file, &info->waterColor, sizeof(color32_t), NULL, NULL);
    }
    if (info->fileFormat >= 28)
        SFileReadFile(file, &info->scriptType, sizeof(uint32_t), NULL, NULL);
    if (info->fileFormat >= 31) {
        SFileReadFile(file, &info->supportedModes, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &info->gameDataVersion, sizeof(uint32_t), NULL, NULL);
    }
    if (info->fileFormat >= 32) {
        SFileReadFile(file, &info->defaultZoomOverride, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &info->maximumZoomOverride, sizeof(uint32_t), NULL, NULL);
    }
    if (info->fileFormat >= 33)
        SFileReadFile(file, &info->minimumZoomOverride, sizeof(uint32_t), NULL, NULL);

    uint32_t num_players = 0;
    SFileReadFile(file, &num_players, sizeof(uint32_t), NULL, NULL);
    FOR_LOOP(i, num_players) {
        uint32_t playerNumber = 0;
        mapPlayer_t scratch = { 0 };
        mapPlayer_t *player;

        SFileReadFile(file, &playerNumber, sizeof(uint32_t), NULL, NULL);
        player = playerNumber < MAX_PLAYERS ? info->players + playerNumber : &scratch;
        player->used = true;
        SFileReadFile(file, &player->playerType, sizeof(playerType_t), NULL, NULL);
        SFileReadFile(file, &player->playerRace, sizeof(playerRace_t), NULL, NULL);
        SFileReadFile(file, &player->flags, sizeof(uint32_t), NULL, NULL);
        UI_SFileReadString(file, &player->playerName);
        SFileReadFile(file, &player->startingPosition, sizeof(vec2_t), NULL, NULL);
        SFileReadFile(file, &player->allyLowPrioritiesFlags, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &player->allyHighPrioritiesFlags, sizeof(uint32_t), NULL, NULL);
        if (info->fileFormat >= 31) {
            SFileReadFile(file, &player->enemyLowPrioritiesFlags, sizeof(uint32_t), NULL, NULL);
            SFileReadFile(file, &player->enemyHighPrioritiesFlags, sizeof(uint32_t), NULL, NULL);
        }
        if (scratch.playerName)
            mi.MemFree(scratch.playerName);
    }

    SFileReadFile(file, &info->num_teams, sizeof(uint32_t), NULL, NULL);
    info->teams = mi.MemAlloc(sizeof(mapTeam_t) * info->num_teams);
    FOR_LOOP(i, info->num_teams) {
        mapTeam_t *force = &info->teams[i];
        SFileReadFile(file, &force->flags, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &force->playerMasks, sizeof(uint32_t), NULL, NULL);
        UI_SFileReadString(file, &force->name);
    }

    SFileCloseFile(file);
    return true;
}

static void UI_AppendTrigStringText(mapTrigStr_t *entry, cstring_t text) {
    uint32_t len, add;

    if (!entry || !text)
        return;
    len = (uint32_t)strlen(entry->text);
    add = (uint32_t)strlen(text);
    if (len + add >= sizeof(entry->text))
        add = sizeof(entry->text) - len - 1;
    if (add) {
        memcpy(entry->text + len, text, add);
        entry->text[len + add] = '\0';
    }
}

static void UI_RemoveTrailingWhitespace(string_t text) {
    size_t len = text ? strlen(text) : 0;

    while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n' || text[len - 1] == '\t' ||
                       text[len - 1] == ' ')) {
        text[--len] = '\0';
    }
}

static cstring_t UI_SkipSpace(cstring_t text) {
    while (text && (*text == ' ' || *text == '\t' || *text == '\r'))
        text++;
    return text;
}

static void UI_ReadLine(cstring_t *cursor, string_t out, uint32_t out_size) {
    uint32_t len = 0;
    cstring_t p;

    if (!cursor || !*cursor || !out || out_size == 0)
        return;
    p = *cursor;
    while (*p && *p != '\n' && *p != '\r') {
        if (len + 1 < out_size)
            out[len++] = *p;
        p++;
    }
    out[len] = '\0';
    while (*p == '\n' || *p == '\r')
        p++;
    *cursor = p;
}

static void UI_MapRemoveBom(string_t buffer) {
    unsigned char *bytes = (unsigned char *)buffer;

    if (bytes && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        memmove(buffer, buffer + 3, strlen(buffer + 3) + 1);
}

static void UI_ReadStringsInto(handle_t archive, mapInfo_t *info) {
    handle_t file;
    uint32_t size;
    string_t buffer;
    cstring_t cursor;
    mapTrigStr_t *entry = NULL;
    bool reading_data = false;
    char line[MAX_TRIGSTR_LENGTH];

    if (!archive || !info || !SFileOpenFileEx(archive, "war3map.wts", SFILE_OPEN_FROM_MPQ, &file))
        return;
    size = SFileGetFileSize(file, NULL);
    buffer = mi.MemAlloc(size + 1);
    if (!buffer) {
        SFileCloseFile(file);
        return;
    }
    SFileReadFile(file, buffer, size, NULL, NULL);
    buffer[size] = '\0';
    SFileCloseFile(file);

    UI_MapRemoveBom(buffer);
    cursor = buffer;
    while (*cursor) {
        cstring_t trimmed;

        UI_ReadLine(&cursor, line, sizeof(line));
        trimmed = UI_SkipSpace(line);
        UI_RemoveTrailingWhitespace(line);
        if (!reading_data) {
            if (!strncmp(trimmed, "STRING ", 7)) {
                if (entry)
                    mi.MemFree(entry);
                entry = mi.MemAlloc(sizeof(*entry));
                memset(entry, 0, sizeof(*entry));
                entry->id = (uint32_t)strtoul(trimmed + 7, NULL, 10);
            } else if (entry && *trimmed == '{') {
                reading_data = true;
            }
            continue;
        }
        if (*trimmed == '}') {
            ADD_TO_LIST(entry, info->strings);
            entry = NULL;
            reading_data = false;
            continue;
        }
        UI_AppendTrigStringText(entry, line);
    }
    if (entry) {
        if (reading_data) {
            ADD_TO_LIST(entry, info->strings);
        } else {
            mi.MemFree(entry);
        }
    }
    mi.MemFree(buffer);
}

static bool UI_OpenMapArchive(cstring_t mapFilename, handle_t *mapArchive, void **mapData) {
    int mapSize;

    if (!mapFilename || !mapArchive || !mapData || !mi.FS_ReadFile || !mi.FS_FreeFile)
        return false;
    *mapArchive = NULL;
    *mapData = NULL;
    mapSize = mi.FS_ReadFile(mapFilename, mapData);
    if (!*mapData || mapSize <= 0)
        return false;
    if (!SFileOpenArchiveFromMemory(*mapData, (uint32_t)mapSize, 0, mapArchive)) {
        mi.FS_FreeFile(*mapData);
        *mapData = NULL;
        return false;
    }
    return true;
}

bool UI_ReadMapInfo(cstring_t mapFilename, mapInfo_t *info) {
    handle_t mapArchive;
    void *mapData;

    if (!info)
        return false;
    memset(info, 0, sizeof(*info));
    if (!UI_OpenMapArchive(mapFilename, &mapArchive, &mapData))
        return false;
    if (!UI_ReadInfoInto(mapArchive, info)) {
        SFileCloseArchive(mapArchive);
        mi.FS_FreeFile(mapData);
        return false;
    }
    UI_ReadStringsInto(mapArchive, info);
    SFileCloseArchive(mapArchive);
    mi.FS_FreeFile(mapData);
    return true;
}

bool UI_FindMapPreviewTexture(cstring_t mapFilename, string_t out, uint32_t out_size) {
    static cstring_t candidates[] = { "war3mapPreview.tga", "war3mapMap.blp", "war3mapMap.tga", NULL };
    handle_t mapArchive;
    void *mapData;
    bool found = false;

    if (!out || out_size == 0)
        return false;
    out[0] = '\0';
    if (!UI_OpenMapArchive(mapFilename, &mapArchive, &mapData))
        return false;
    for (uint32_t i = 0; candidates[i]; i++) {
        handle_t file;

        if (!SFileOpenFileEx(mapArchive, candidates[i], SFILE_OPEN_FROM_MPQ, &file))
            continue;
        SFileCloseFile(file);
        snprintf(out, out_size, "%s\\%s", mapFilename, candidates[i]);
        found = true;
        break;
    }
    SFileCloseArchive(mapArchive);
    mi.FS_FreeFile(mapData);
    return found;
}

void UI_FreeMapInfo(mapInfo_t *mapInfo) {
    mapTrigStr_t *string = mapInfo ? mapInfo->strings : NULL;

    if (!mapInfo)
        return;
    FOR_LOOP(i, MAX_PLAYERS) SAFE_DELETE(mapInfo->players[i].playerName, mi.MemFree);
    FOR_LOOP(i, mapInfo->num_teams) SAFE_DELETE(mapInfo->teams[i].name, mi.MemFree);
    SAFE_DELETE(mapInfo->mapName, mi.MemFree);
    SAFE_DELETE(mapInfo->mapAuthor, mi.MemFree);
    SAFE_DELETE(mapInfo->mapDescription, mi.MemFree);
    SAFE_DELETE(mapInfo->playersRecommended, mi.MemFree);
    SAFE_DELETE(mapInfo->loadingScreenModel, mi.MemFree);
    SAFE_DELETE(mapInfo->loadingScreenText, mi.MemFree);
    SAFE_DELETE(mapInfo->loadingScreenTitle, mi.MemFree);
    SAFE_DELETE(mapInfo->loadingScreenSubtitle, mi.MemFree);
    SAFE_DELETE(mapInfo->prologueScreenModel, mi.MemFree);
    SAFE_DELETE(mapInfo->prologueScreenText, mi.MemFree);
    SAFE_DELETE(mapInfo->prologueScreenTitle, mi.MemFree);
    SAFE_DELETE(mapInfo->prologueScreenSubtitle, mi.MemFree);
    SAFE_DELETE(mapInfo->soundEnvironment, mi.MemFree);
    SAFE_DELETE(mapInfo->teams, mi.MemFree);
    while (string) {
        mapTrigStr_t *next = string->next;
        mi.MemFree(string);
        string = next;
    }
    memset(mapInfo, 0, sizeof(*mapInfo));
}

static cstring_t UI_PathBaseFileName(cstring_t path) {
    cstring_t base = path ? path : "";

    for (cstring_t p = base; *p; p++) {
        if (*p == '\\' || *p == '/')
            base = p + 1;
    }
    return base;
}

void UI_DefaultMapName(cstring_t path, string_t out, uint32_t out_size) {
    uint32_t len;

    if (!out || out_size == 0)
        return;
    snprintf(out, out_size, "%s", UI_PathBaseFileName(path));
    len = (uint32_t)strlen(out);
    if (len > 4 && out[len - 4] == '.')
        out[len - 4] = '\0';
}

void UI_ResolveMapInfoString(mapInfo_t const *info, cstring_t text, string_t out, uint32_t out_size) {
    uint32_t id;

    if (!out || out_size == 0)
        return;
    if (!text) {
        out[0] = '\0';
        return;
    }
    if (strncmp(text, "TRIGSTR_", 8)) {
        snprintf(out, out_size, "%s", text);
        return;
    }
    id = (uint32_t)strtoul(text + 8, NULL, 10);
    for (mapTrigStr_t const *string = info ? info->strings : NULL; string; string = string->next) {
        if (string->id == id) {
            snprintf(out, out_size, "%s", string->text);
            return;
        }
    }
    snprintf(out, out_size, "%s", text);
}

bool UI_MapNameMatchesFile(cstring_t name, cstring_t path) {
    PATHSTR base;
    size_t len;

    if (!name || !path || !*name)
        return false;
    snprintf(base, sizeof(base), "%s", UI_PathBaseFileName(path));
    len = strlen(base);
    if (len > 4 && (!strcasecmp(base + len - 4, ".w3m") || !strcasecmp(base + len - 4, ".w3x")))
        base[len - 4] = '\0';
    return !strcasecmp(name, base);
}

cstring_t UI_MapTilesetName(uint8_t tileset) {
    switch (tileset) {
        case 'A': return "Ashenvale";
        case 'B': return "Barrens";
        case 'C': return "Felwood";
        case 'D': return "Dungeon";
        case 'F': return "Lordaeron Fall";
        case 'G': return "Underground";
        case 'I': return "Icecrown Glacier";
        case 'J': return "Dalaran";
        case 'K': return "Black Citadel";
        case 'L': return "Lordaeron Summer";
        case 'N': return "Northrend";
        case 'O': return "Outland";
        case 'Q': return "Village Fall";
        case 'V': return "Village";
        case 'W': return "Lordaeron Winter";
        case 'X': return "Dalaran Ruins";
        case 'Y': return "Cityscape";
        case 'Z': return "Sunken Ruins";
        default: return NULL;
    }
}

cstring_t UI_MapSizeName(uint32_t width, uint32_t height) {
    uint32_t largest = MAX(width, height);

    if (largest <= 96)
        return "Small";
    if (largest <= 128)
        return "Medium";
    if (largest <= 160)
        return "Large";
    return "Huge";
}

void UI_SanitizeMapListField(string_t text) {
    if (!text)
        return;
    for (string_t p = text; *p; p++) {
        if (*p == '\n' || *p == '\r' || *p == '\t')
            *p = ' ';
    }
}

void UI_SanitizeMapInfoText(string_t text) {
    if (!text)
        return;
    for (string_t p = text; *p; p++) {
        if (*p == '\t')
            *p = ' ';
    }
}
