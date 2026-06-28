/*
 * ui_map_data.c - Warcraft III map browser data loaded inside ui.dll.
 */

#include "ui_local.h"
#include "common/mpq.h"

#include <stdlib.h>

#ifndef _WIN32
#include <strings.h>
#endif

static DWORD UI_SFileReadStringLength(HANDLE file) {
    DWORD filePosition = SFileSetFilePointer(file, 0, 0, FILE_CURRENT);
    DWORD stringLength = 1;
    while (true) {
        BYTE ch = 0;
        SFileReadFile(file, &ch, 1, NULL, NULL);
        if (ch == 0)
            break;
        stringLength++;
    }
    SFileSetFilePointer(file, filePosition, 0, FILE_BEGIN);
    return stringLength;
}

static void UI_SFileReadString(HANDLE file, LPSTR *lppString) {
    DWORD stringLength = UI_SFileReadStringLength(file);
    *lppString = uiimport.MemAlloc(stringLength);
    SFileReadFile(file, *lppString, stringLength, NULL, NULL);
}

static BOOL UI_ReadInfoInto(HANDLE archive, LPMAPINFO info) {
    HANDLE file;

    if (!archive || !info)
        return false;
    if (!SFileOpenFileEx(archive, "war3map.w3i", SFILE_OPEN_FROM_MPQ, &file))
        return false;
    SFileReadFile(file, &info->fileFormat, 4, NULL, NULL);
    SFileReadFile(file, &info->numberOfSaves, sizeof(DWORD), NULL, NULL);
    SFileReadFile(file, &info->editorVersion, sizeof(DWORD), NULL, NULL);
    if (info->fileFormat >= 28) {
        SFileReadFile(file, &info->gameVersionMajor, sizeof(DWORD), NULL, NULL);
        SFileReadFile(file, &info->gameVersionMinor, sizeof(DWORD), NULL, NULL);
        SFileReadFile(file, &info->gameVersionPatch, sizeof(DWORD), NULL, NULL);
        SFileReadFile(file, &info->gameVersionBuild, sizeof(DWORD), NULL, NULL);
    }
    UI_SFileReadString(file, &info->mapName);
    UI_SFileReadString(file, &info->mapAuthor);
    UI_SFileReadString(file, &info->mapDescription);
    UI_SFileReadString(file, &info->playersRecommended);
    SFileReadFile(file, &info->cameraBounds, sizeof(mapCameraBounds_t), NULL, NULL);
    SFileReadFile(file, &info->playableArea, sizeof(size2_t), NULL, NULL);
    SFileReadFile(file, &info->flags, sizeof(DWORD), NULL, NULL);
    SFileReadFile(file, &info->mainGroundType, sizeof(char), NULL, NULL);
    SFileReadFile(file, &info->campaignBackgroundNumber, sizeof(DWORD), NULL, NULL);
    if (info->fileFormat >= 25)
        UI_SFileReadString(file, &info->loadingScreenModel);
    UI_SFileReadString(file, &info->loadingScreenText);
    UI_SFileReadString(file, &info->loadingScreenTitle);
    UI_SFileReadString(file, &info->loadingScreenSubtitle);
    if (info->fileFormat >= 25)
        SFileReadFile(file, &info->gameDataSet, sizeof(DWORD), NULL, NULL);
    else
        SFileReadFile(file, &info->loadingScreenNumber, sizeof(DWORD), NULL, NULL);
    if (info->fileFormat >= 25)
        UI_SFileReadString(file, &info->prologueScreenModel);
    UI_SFileReadString(file, &info->prologueScreenText);
    UI_SFileReadString(file, &info->prologueScreenTitle);
    UI_SFileReadString(file, &info->prologueScreenSubtitle);
    if (info->fileFormat >= 25) {
        SFileReadFile(file, &info->fogStyle, sizeof(DWORD), NULL, NULL);
        SFileReadFile(file, &info->fogStartZ, sizeof(FLOAT), NULL, NULL);
        SFileReadFile(file, &info->fogEndZ, sizeof(FLOAT), NULL, NULL);
        SFileReadFile(file, &info->fogDensity, sizeof(FLOAT), NULL, NULL);
        SFileReadFile(file, &info->fogColor, sizeof(COLOR32), NULL, NULL);
        SFileReadFile(file, &info->weatherID, sizeof(DWORD), NULL, NULL);
        UI_SFileReadString(file, &info->soundEnvironment);
        SFileReadFile(file, &info->lightEnvironmentTileset, sizeof(BYTE), NULL, NULL);
        SFileReadFile(file, &info->waterColor, sizeof(COLOR32), NULL, NULL);
    }
    if (info->fileFormat >= 28)
        SFileReadFile(file, &info->scriptType, sizeof(DWORD), NULL, NULL);
    if (info->fileFormat >= 31) {
        SFileReadFile(file, &info->supportedModes, sizeof(DWORD), NULL, NULL);
        SFileReadFile(file, &info->gameDataVersion, sizeof(DWORD), NULL, NULL);
    }
    if (info->fileFormat >= 32) {
        SFileReadFile(file, &info->defaultZoomOverride, sizeof(DWORD), NULL, NULL);
        SFileReadFile(file, &info->maximumZoomOverride, sizeof(DWORD), NULL, NULL);
    }
    if (info->fileFormat >= 33)
        SFileReadFile(file, &info->minimumZoomOverride, sizeof(DWORD), NULL, NULL);

    DWORD num_players = 0;
    SFileReadFile(file, &num_players, sizeof(DWORD), NULL, NULL);
    FOR_LOOP(i, num_players) {
        DWORD playerNumber = 0;
        mapPlayer_t scratch = { 0 };
        mapPlayer_t *player;

        SFileReadFile(file, &playerNumber, sizeof(DWORD), NULL, NULL);
        player = playerNumber < MAX_PLAYERS ? info->players + playerNumber : &scratch;
        player->used = true;
        SFileReadFile(file, &player->playerType, sizeof(playerType_t), NULL, NULL);
        SFileReadFile(file, &player->playerRace, sizeof(playerRace_t), NULL, NULL);
        SFileReadFile(file, &player->flags, sizeof(DWORD), NULL, NULL);
        UI_SFileReadString(file, &player->playerName);
        SFileReadFile(file, &player->startingPosition, sizeof(VECTOR2), NULL, NULL);
        SFileReadFile(file, &player->allyLowPrioritiesFlags, sizeof(DWORD), NULL, NULL);
        SFileReadFile(file, &player->allyHighPrioritiesFlags, sizeof(DWORD), NULL, NULL);
        if (info->fileFormat >= 31) {
            SFileReadFile(file, &player->enemyLowPrioritiesFlags, sizeof(DWORD), NULL, NULL);
            SFileReadFile(file, &player->enemyHighPrioritiesFlags, sizeof(DWORD), NULL, NULL);
        }
        if (scratch.playerName)
            uiimport.MemFree(scratch.playerName);
    }

    SFileReadFile(file, &info->num_teams, sizeof(DWORD), NULL, NULL);
    info->teams = uiimport.MemAlloc(sizeof(mapTeam_t) * info->num_teams);
    FOR_LOOP(i, info->num_teams) {
        mapTeam_t *force = &info->teams[i];
        SFileReadFile(file, &force->flags, sizeof(DWORD), NULL, NULL);
        SFileReadFile(file, &force->playerMasks, sizeof(DWORD), NULL, NULL);
        UI_SFileReadString(file, &force->name);
    }

    SFileCloseFile(file);
    return true;
}

static void UI_AppendTrigStringText(mapTrigStr_t *entry, LPCSTR text) {
    DWORD len, add;

    if (!entry || !text)
        return;
    len = (DWORD)strlen(entry->text);
    add = (DWORD)strlen(text);
    if (len + add >= sizeof(entry->text))
        add = sizeof(entry->text) - len - 1;
    if (add) {
        memcpy(entry->text + len, text, add);
        entry->text[len + add] = '\0';
    }
}

static void UI_RemoveTrailingWhitespace(LPSTR text) {
    size_t len = text ? strlen(text) : 0;

    while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n' || text[len - 1] == '\t' ||
                       text[len - 1] == ' ')) {
        text[--len] = '\0';
    }
}

static LPCSTR UI_SkipSpace(LPCSTR text) {
    while (text && (*text == ' ' || *text == '\t' || *text == '\r'))
        text++;
    return text;
}

static void UI_ReadLine(LPCSTR *cursor, LPSTR out, DWORD out_size) {
    DWORD len = 0;
    LPCSTR p;

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

static void UI_MapRemoveBom(LPSTR buffer) {
    unsigned char *bytes = (unsigned char *)buffer;

    if (bytes && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        memmove(buffer, buffer + 3, strlen(buffer + 3) + 1);
}

static void UI_ReadStringsInto(HANDLE archive, LPMAPINFO info) {
    HANDLE file;
    DWORD size;
    LPSTR buffer;
    LPCSTR cursor;
    mapTrigStr_t *entry = NULL;
    BOOL reading_data = false;
    char line[MAX_TRIGSTR_LENGTH];

    if (!archive || !info || !SFileOpenFileEx(archive, "war3map.wts", SFILE_OPEN_FROM_MPQ, &file))
        return;
    size = SFileGetFileSize(file, NULL);
    buffer = uiimport.MemAlloc(size + 1);
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
        LPCSTR trimmed;

        UI_ReadLine(&cursor, line, sizeof(line));
        trimmed = UI_SkipSpace(line);
        UI_RemoveTrailingWhitespace(line);
        if (!reading_data) {
            if (!strncmp(trimmed, "STRING ", 7)) {
                if (entry)
                    uiimport.MemFree(entry);
                entry = uiimport.MemAlloc(sizeof(*entry));
                memset(entry, 0, sizeof(*entry));
                entry->id = (DWORD)strtoul(trimmed + 7, NULL, 10);
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
            uiimport.MemFree(entry);
        }
    }
    uiimport.MemFree(buffer);
}

static BOOL UI_OpenMapArchive(LPCSTR mapFilename, HANDLE *mapArchive, void **mapData) {
    int mapSize;

    if (!mapFilename || !mapArchive || !mapData || !uiimport.FS_ReadFile || !uiimport.FS_FreeFile)
        return false;
    *mapArchive = NULL;
    *mapData = NULL;
    mapSize = uiimport.FS_ReadFile(mapFilename, mapData);
    if (!*mapData || mapSize <= 0)
        return false;
    if (!SFileOpenArchiveFromMemory(*mapData, (DWORD)mapSize, 0, mapArchive)) {
        uiimport.FS_FreeFile(*mapData);
        *mapData = NULL;
        return false;
    }
    return true;
}

BOOL UI_ReadMapInfo(LPCSTR mapFilename, LPMAPINFO info) {
    HANDLE mapArchive;
    void *mapData;

    if (!info)
        return false;
    memset(info, 0, sizeof(*info));
    if (!UI_OpenMapArchive(mapFilename, &mapArchive, &mapData))
        return false;
    if (!UI_ReadInfoInto(mapArchive, info)) {
        SFileCloseArchive(mapArchive);
        uiimport.FS_FreeFile(mapData);
        return false;
    }
    UI_ReadStringsInto(mapArchive, info);
    SFileCloseArchive(mapArchive);
    uiimport.FS_FreeFile(mapData);
    return true;
}

BOOL UI_FindMapPreviewTexture(LPCSTR mapFilename, LPSTR out, DWORD out_size) {
    static LPCSTR candidates[] = { "war3mapPreview.tga", "war3mapMap.blp", "war3mapMap.tga", NULL };
    HANDLE mapArchive;
    void *mapData;
    BOOL found = false;

    if (!out || out_size == 0)
        return false;
    out[0] = '\0';
    if (!UI_OpenMapArchive(mapFilename, &mapArchive, &mapData))
        return false;
    for (DWORD i = 0; candidates[i]; i++) {
        HANDLE file;

        if (!SFileOpenFileEx(mapArchive, candidates[i], SFILE_OPEN_FROM_MPQ, &file))
            continue;
        SFileCloseFile(file);
        snprintf(out, out_size, "%s\\%s", mapFilename, candidates[i]);
        found = true;
        break;
    }
    SFileCloseArchive(mapArchive);
    uiimport.FS_FreeFile(mapData);
    return found;
}

void UI_FreeMapInfo(LPMAPINFO mapInfo) {
    mapTrigStr_t *string = mapInfo ? mapInfo->strings : NULL;

    if (!mapInfo)
        return;
    FOR_LOOP(i, MAX_PLAYERS) {
        if (mapInfo->players[i].playerName)
            uiimport.MemFree(mapInfo->players[i].playerName);
    }
    FOR_LOOP(i, mapInfo->num_teams) {
        if (mapInfo->teams[i].name)
            uiimport.MemFree(mapInfo->teams[i].name);
    }
    if (mapInfo->mapName) uiimport.MemFree(mapInfo->mapName);
    if (mapInfo->mapAuthor) uiimport.MemFree(mapInfo->mapAuthor);
    if (mapInfo->mapDescription) uiimport.MemFree(mapInfo->mapDescription);
    if (mapInfo->playersRecommended) uiimport.MemFree(mapInfo->playersRecommended);
    if (mapInfo->loadingScreenModel) uiimport.MemFree(mapInfo->loadingScreenModel);
    if (mapInfo->loadingScreenText) uiimport.MemFree(mapInfo->loadingScreenText);
    if (mapInfo->loadingScreenTitle) uiimport.MemFree(mapInfo->loadingScreenTitle);
    if (mapInfo->loadingScreenSubtitle) uiimport.MemFree(mapInfo->loadingScreenSubtitle);
    if (mapInfo->prologueScreenModel) uiimport.MemFree(mapInfo->prologueScreenModel);
    if (mapInfo->prologueScreenText) uiimport.MemFree(mapInfo->prologueScreenText);
    if (mapInfo->prologueScreenTitle) uiimport.MemFree(mapInfo->prologueScreenTitle);
    if (mapInfo->prologueScreenSubtitle) uiimport.MemFree(mapInfo->prologueScreenSubtitle);
    if (mapInfo->soundEnvironment) uiimport.MemFree(mapInfo->soundEnvironment);
    if (mapInfo->teams) uiimport.MemFree(mapInfo->teams);
    while (string) {
        mapTrigStr_t *next = string->next;
        uiimport.MemFree(string);
        string = next;
    }
    memset(mapInfo, 0, sizeof(*mapInfo));
}

static LPCSTR UI_PathBaseFileName(LPCSTR path) {
    LPCSTR base = path ? path : "";

    for (LPCSTR p = base; *p; p++) {
        if (*p == '\\' || *p == '/')
            base = p + 1;
    }
    return base;
}

void UI_DefaultMapName(LPCSTR path, LPSTR out, DWORD out_size) {
    DWORD len;

    if (!out || out_size == 0)
        return;
    snprintf(out, out_size, "%s", UI_PathBaseFileName(path));
    len = (DWORD)strlen(out);
    if (len > 4 && out[len - 4] == '.')
        out[len - 4] = '\0';
}

void UI_ResolveMapInfoString(LPCMAPINFO info, LPCSTR text, LPSTR out, DWORD out_size) {
    DWORD id;

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
    id = (DWORD)strtoul(text + 8, NULL, 10);
    for (mapTrigStr_t const *string = info ? info->strings : NULL; string; string = string->next) {
        if (string->id == id) {
            snprintf(out, out_size, "%s", string->text);
            return;
        }
    }
    snprintf(out, out_size, "%s", text);
}

BOOL UI_MapNameMatchesFile(LPCSTR name, LPCSTR path) {
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

LPCSTR UI_MapTilesetName(BYTE tileset) {
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

LPCSTR UI_MapSizeName(DWORD width, DWORD height) {
    DWORD largest = MAX(width, height);

    if (largest <= 96)
        return "Small";
    if (largest <= 128)
        return "Medium";
    if (largest <= 160)
        return "Large";
    return "Huge";
}

void UI_SanitizeMapListField(LPSTR text) {
    if (!text)
        return;
    for (LPSTR p = text; *p; p++) {
        if (*p == '\n' || *p == '\r' || *p == '\t')
            *p = ' ';
    }
}

void UI_SanitizeMapInfoText(LPSTR text) {
    if (!text)
        return;
    for (LPSTR p = text; *p; p++) {
        if (*p == '\t')
            *p = ' ';
    }
}
