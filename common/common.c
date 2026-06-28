#include "common.h"

#include "mpq.h"
#include <stdlib.h>

#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#include <strings.h>
#endif

#include <sys/stat.h>

extern void Key_Init(void);

#define MAXPRINTMSG 4096
#define MAX_FS_MAPS 4096
#define MAX_NUM_ARGVS 64
#define MAX_ARG_CHARS 1024

static int com_argc;
static LPCSTR com_argv[MAX_NUM_ARGVS + 1];

const LPCSTR WarcraftSheets[] = {
    "Units\\unitUI.slk",
    "Units\\AbilityData.slk",
    "Units\\UnitWeapons.slk",
    "Units\\ItemData.slk",
    "Units\\UnitMetaData.slk",
    "Units\\UnitData.slk",
    "Units\\UnitBalance.slk",
    "Units\\UnitAbilities.slk",
    "Units\\UpgradeData.slk",
    "Units\\DestructableData.slk",

    "Doodads\\Doodads.slk",

    "TerrainArt\\Weather.slk",
    "TerrainArt\\CliffTypes.slk",
    "TerrainArt\\Water.slk",
    "TerrainArt\\Terrain.slk",

    "Splats\\UberSplatData.slk",
    "Splats\\LightningData.slk",
    "Splats\\T_SpawnData.slk",
    "Splats\\SplatData.slk",
    "Splats\\T_SplatData.slk",
    "Splats\\SpawnData.slk",

    "UI\\SoundInfo\\MIDISounds.slk",
    "UI\\SoundInfo\\AnimLookups.slk",
    "UI\\SoundInfo\\EnvironmentSounds.slk",
    "UI\\SoundInfo\\PortraitAnims.slk",
    "UI\\SoundInfo\\AbilitySounds.slk",
    "UI\\SoundInfo\\UnitCombatSounds.slk",
    "UI\\SoundInfo\\EAXDefs.slk",
    "UI\\SoundInfo\\DialogSounds.slk",
    "UI\\SoundInfo\\AnimSounds.slk",
    "UI\\SoundInfo\\AmbienceSounds.slk",
    "UI\\SoundInfo\\UISounds.slk",
    "UI\\SoundInfo\\UnitAckSounds.slk",

    NULL
};

#define MAX_ARCHIVES 64
#define MAX_GAME_DIRS 16

static HANDLE archives[MAX_ARCHIVES] = { 0 };
static PATHSTR archiveNames[MAX_ARCHIVES];
static PATHSTR gameDirs[MAX_GAME_DIRS];

typedef void (*fsDiskEntryFunc_t)(LPCSTR name, LPCSTR path, BOOL isDirectory, BOOL isFile, void *userData);

static void FS_NormalizePath(LPCSTR in, LPSTR out, DWORD out_size) {
    DWORD i;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!in) {
        return;
    }
    snprintf(out, out_size, "%s", in);
    for (i = 0; out[i]; i++) {
        if (out[i] == '/') {
            out[i] = '\\';
        }
    }
}

static BOOL FS_PathHasExtension(LPCSTR filename, LPCSTR extension) {
    size_t filenameLen;
    size_t extensionLen;

    if (!filename || !extension) {
        return false;
    }
    filenameLen = strlen(filename);
    extensionLen = strlen(extension);
    if (filenameLen < extensionLen) {
        return false;
    }
    return !strcasecmp(filename + filenameLen - extensionLen, extension);
}

static BOOL FS_IsMapPath(LPCSTR filename) {
#ifdef SC2
    return FS_PathHasExtension(filename, ".SC2Map") ||
           FS_PathHasExtension(filename, ".s2ma") ||
           FS_PathHasExtension(filename, ".SC2Components");
#else
    return FS_PathHasExtension(filename, ".w3m") || FS_PathHasExtension(filename, ".w3x");
#endif
}

static size_t FS_MapExtensionLength(LPCSTR filename) {
#ifdef SC2
    if (FS_PathHasExtension(filename, ".SC2Components")) return strlen(".SC2Components");
    if (FS_PathHasExtension(filename, ".SC2Map")) return strlen(".SC2Map");
    if (FS_PathHasExtension(filename, ".s2ma")) return strlen(".s2ma");
#else
    if (FS_PathHasExtension(filename, ".w3m")) return strlen(".w3m");
    if (FS_PathHasExtension(filename, ".w3x")) return strlen(".w3x");
#endif
    return 0;
}

static BOOL FS_IsExplicitMapPath(LPCSTR name) {
    return name &&
           (strchr(name, '/') ||
            strchr(name, '\\') ||
            FS_IsMapPath(name));
}

static LPCSTR FS_BaseName(LPCSTR path) {
    LPCSTR base;

    if (!path) {
        return "";
    }
    base = path;
    for (LPCSTR p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

static BOOL FS_MapBaseEquals(LPCSTR path, LPCSTR name) {
    LPCSTR base = FS_BaseName(path);
    size_t nameLen = strlen(name ? name : "");
    size_t baseLen = strlen(base);
    size_t extLen = FS_MapExtensionLength(base);

    if (!name || !*name || extLen == 0 || baseLen <= extLen) {
        return false;
    }
    baseLen -= extLen;
    return nameLen == baseLen && !strncasecmp(base, name, baseLen);
}

static int FS_CompareMapPaths(const void *a, const void *b) {
    PATHSTR const *pa = a;
    PATHSTR const *pb = b;

    return strcasecmp(*pa, *pb);
}

static BOOL FS_MapListHas(PATHSTR *maps, DWORD count, LPCSTR path) {
    FOR_LOOP(i, count) {
        if (!strcasecmp(maps[i], path)) {
            return true;
        }
    }
    return false;
}

#ifdef SC2
static LPCSTR FS_FindSC2MapExtension(LPCSTR path) {
    static LPCSTR const ext = ".SC2Map";
    size_t extLen = strlen(ext);

    if (!path) {
        return NULL;
    }
    for (LPCSTR p = path; *p; p++) {
        if (!strncasecmp(p, ext, extLen) &&
            (p[extLen] == '\0' || p[extLen] == '/' || p[extLen] == '\\')) {
            return p;
        }
    }
    return NULL;
}

static BOOL FS_CollapseSC2MapPath(LPCSTR path, LPSTR out, DWORD out_size) {
    LPCSTR map_ext;
    size_t len;

    if (!path || !out || out_size == 0) {
        return false;
    }
    map_ext = FS_FindSC2MapExtension(path);
    if (!map_ext) {
        return false;
    }
    len = (size_t)(map_ext - path) + strlen(".SC2Map");
    if (len >= out_size) {
        return false;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return true;
}
#endif

HANDLE FS_AddArchive(LPCSTR filename) {
    if (!filename || !*filename) {
        return NULL;
    }
    FOR_LOOP(i, MAX_ARCHIVES) {
        if (archives[i] && !strcasecmp(archiveNames[i], filename)) {
            return archives[i];
        }
    }
    FOR_LOOP(i, MAX_ARCHIVES) {
        if (archives[i])
            continue;
        SFileOpenArchive(filename, 0, 0, &archives[i]);
        if (!archives[i]) {
            fprintf(stderr, "Can't add archive %s\n", filename);
        } else {
            snprintf(archiveNames[i], sizeof(archiveNames[i]), "%s", filename);
            fprintf(stderr, "Added archive '%s'.\n", filename);
        }
        return archives[i];
    }
    return NULL;
}

static BOOL FS_StatPath(LPCSTR filename, BOOL *isDirectory, BOOL *isFile) {
#ifdef _WIN32
    struct _stat st;

    if (!filename || !*filename) {
        return false;
    }
    if (_stat(filename, &st) != 0) {
        return false;
    }
#else
    struct stat st;

    if (!filename || !*filename) {
        return false;
    }
    if (stat(filename, &st) != 0) {
        return false;
    }
#endif
    if (isDirectory) {
#ifdef _WIN32
        *isDirectory = (st.st_mode & _S_IFDIR) != 0;
#else
        *isDirectory = S_ISDIR(st.st_mode);
#endif
    }
    if (isFile) {
#ifdef _WIN32
        *isFile = (st.st_mode & _S_IFREG) != 0;
#else
        *isFile = S_ISREG(st.st_mode);
#endif
    }
    return true;
}

static BOOL FS_DirectoryExists(LPCSTR filename) {
    BOOL isDirectory = false;

    return FS_StatPath(filename, &isDirectory, NULL) && isDirectory;
}

static BOOL FS_FileOnDiskExists(LPCSTR filename) {
    BOOL isFile = false;

    return FS_StatPath(filename, NULL, &isFile) && isFile;
}

static BOOL FS_HasExtension(LPCSTR filename, LPCSTR extension) {
    size_t filenameLen;
    size_t extensionLen;

    if (!filename || !extension) {
        return false;
    }
    filenameLen = strlen(filename);
    extensionLen = strlen(extension);
    if (filenameLen < extensionLen) {
        return false;
    }
    return !strcasecmp(filename + filenameLen - extensionLen, extension);
}

static BOOL FS_IsArchiveExtensionAt(LPCSTR path, size_t dot) {
#ifdef SC2
    static LPCSTR const extensions[] = {
        ".mpq",
        ".SC2Map",
        ".s2ma",
        ".SC2Assets",
        ".SC2Data",
        ".SC2Maps",
        NULL
    };
#else
    static LPCSTR const extensions[] = { ".mpq", ".w3m", ".w3x", NULL };
#endif

    if (!path || path[dot] != '.') {
        return false;
    }
    for (DWORD i = 0; extensions[i]; i++) {
        size_t len = strlen(extensions[i]);

        if (strncasecmp(path + dot, extensions[i], len)) {
            continue;
        }
        return path[dot + len] == '/' || path[dot + len] == '\\';
    }
    return false;
}

static BOOL FS_SplitNestedArchivePath(LPCSTR filename,
                                      LPSTR outer,
                                      DWORD outer_size,
                                      LPCSTR *inner) {
    if (!filename || !outer || outer_size == 0 || !inner) {
        return false;
    }
    for (size_t i = 0; filename[i]; i++) {
        size_t outerLen;
        size_t extLen;

        if (!FS_IsArchiveExtensionAt(filename, i)) {
            continue;
        }
        extLen = strcspn(filename + i, "/\\");
        outerLen = i + extLen;
        if (outerLen >= outer_size) {
            return false;
        }
        memcpy(outer, filename, outerLen);
        outer[outerLen] = '\0';
        *inner = filename + outerLen + 1;
        while (**inner == '/' || **inner == '\\') {
            (*inner)++;
        }
        return **inner != '\0';
    }
    return false;
}

static void FS_MakeDiskPath(LPCSTR root, LPCSTR filename, LPSTR out, DWORD out_size) {
    DWORD len;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!root || !filename) {
        return;
    }
    snprintf(out, out_size, "%s/%s", root, filename);
    len = (DWORD)strlen(root);
    for (LPSTR p = out + len; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
}

static void FS_ForEachDiskEntry(LPCSTR dirname, fsDiskEntryFunc_t func, void *userData) {
    if (!dirname || !*dirname || !func) {
        return;
    }

#ifdef _WIN32
    char pattern[MAX_PATHLEN * 2];
    struct _finddata_t entry;
    intptr_t handle;

    snprintf(pattern, sizeof(pattern), "%s/*", dirname);
    handle = _findfirst(pattern, &entry);
    if (handle == -1) {
        return;
    }
    do {
        char path[MAX_PATHLEN * 2];
        BOOL isDirectory;

        if (!strcmp(entry.name, ".") || !strcmp(entry.name, "..")) {
            continue;
        }
        FS_MakeDiskPath(dirname, entry.name, path, sizeof(path));
        isDirectory = (entry.attrib & _A_SUBDIR) != 0;
        func(entry.name, path, isDirectory, !isDirectory, userData);
    } while (_findnext(handle, &entry) == 0);
    _findclose(handle);
#else
    DIR *dir = opendir(dirname);
    struct dirent *entry;

    if (!dir) {
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        char path[MAX_PATHLEN * 2];
        BOOL isDirectory = false;
        BOOL isFile = false;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        FS_MakeDiskPath(dirname, entry->d_name, path, sizeof(path));
        if (!FS_StatPath(path, &isDirectory, &isFile)) {
            continue;
        }
        func(entry->d_name, path, isDirectory, isFile, userData);
    }
    closedir(dir);
#endif
}

static void FS_AddGameDirectory(LPCSTR dirname) {
    if (!dirname || !*dirname) {
        return;
    }
    FOR_LOOP(i, MAX_GAME_DIRS) {
        if (gameDirs[i][0] && !strcasecmp(gameDirs[i], dirname)) {
            return;
        }
    }
    FOR_LOOP(i, MAX_GAME_DIRS) {
        if (gameDirs[i][0]) {
            continue;
        }
        snprintf(gameDirs[i], sizeof(gameDirs[i]), "%s", dirname);
        fprintf(stderr, "Added game directory '%s'.\n", dirname);
        return;
    }
}

static int FS_ComparePaths(const void *a, const void *b) {
    PATHSTR const *pa = a;
    PATHSTR const *pb = b;
    LPCSTR abase = FS_BaseName(*pa);
    LPCSTR bbase = FS_BaseName(*pb);
    int cmp = strcasecmp(abase, bbase);

    return cmp ? cmp : strcasecmp(*pa, *pb);
}

typedef struct {
    PATHSTR *paths;
    DWORD maxPaths;
    DWORD count;
} fsArchiveScan_t;

static void FS_AddArchiveScanDirectoryEntry(LPCSTR name,
                                            LPCSTR path,
                                            BOOL isDirectory,
                                            BOOL isFile,
                                            void *userData);

static void FS_AddArchiveScanEntry(LPCSTR name, LPCSTR path, BOOL isDirectory, BOOL isFile, void *userData) {
    fsArchiveScan_t *scan = userData;

    if (!scan) {
        return;
    }
    (void)isDirectory;
    if (isFile && scan->count < scan->maxPaths &&
#ifdef SC2
        (FS_HasExtension(name, ".mpq") ||
         FS_HasExtension(name, ".SC2Assets") ||
         FS_HasExtension(name, ".SC2Data") ||
         FS_HasExtension(name, ".SC2Maps"))
#else
        FS_HasExtension(name, ".mpq")
#endif
        ) {
#ifndef SC2
        LPCSTR base = FS_BaseName(path);

        if (!Cvar_Integer("fs_expansion", 0) &&
            base && !strncasecmp(base, "War3x", 5)) {
            fprintf(stderr, "Skipping expansion archive '%s' (set fs_expansion 1 or use -tft to mount it).\n", path);
            return;
        }
#endif
        snprintf(scan->paths[scan->count++], sizeof(PATHSTR), "%s", path);
    }
}

static void FS_AddArchiveScanDirectory(LPCSTR dirname, fsArchiveScan_t *scan) {
    if (!dirname || !scan || scan->count >= scan->maxPaths) {
        return;
    }
    FS_ForEachDiskEntry(dirname, FS_AddArchiveScanEntry, scan);
    FS_ForEachDiskEntry(dirname, FS_AddArchiveScanDirectoryEntry, scan);
}

static void FS_AddArchiveScanDirectoryEntry(LPCSTR name,
                                            LPCSTR path,
                                            BOOL isDirectory,
                                            BOOL isFile,
                                            void *userData) {
    (void)name;
    (void)isFile;
    if (isDirectory) {
        FS_AddArchiveScanDirectory(path, userData);
    }
}

BOOL FS_AddDataDirectory(LPCSTR dirname) {
    PATHSTR archivePaths[MAX_ARCHIVES];
    fsArchiveScan_t scan = { archivePaths, MAX_ARCHIVES, 0 };
    DWORD mountedCount = 0;

    if (!FS_DirectoryExists(dirname)) {
        return false;
    }

    FS_AddGameDirectory(dirname);
    FS_AddArchiveScanDirectory(dirname, &scan);

    qsort(archivePaths, scan.count, sizeof(archivePaths[0]), FS_ComparePaths);
    FOR_LOOP(i, scan.count) {
        if (FS_AddArchive(archivePaths[i])) {
            mountedCount++;
        }
    }
    (void)mountedCount;
    return true;
}

#if 0
static void ExtractStarCraft2(void) {
    HANDLE archive;
    SFileOpenArchive("/Users/igor/Documents/SC2Install/Installer Tome 1.MPQ", 0, 0, &archive);
//    SFileOpenArchive("/Users/igor/Desktop/terrain.MPQ", 0, 0, &archive);
//    SFileOpenArchive("/Users/igor/Documents/StarCraft2/Campaigns/Liberty.SC2Campaign/base.SC2Assets", 0, 0, &archive);
//    SFileOpenArchive("/Users/igor/Documents/StarCraft2/Mods/Liberty.SC2Mod/base.SC2Assets", 0, 0, &archive);
//    SFileOpenArchive("/Users/igor/Documents/StarCraft2/Mods/Liberty.SC2Mod/base.SC2Data", 0, 0, &archive);
//    SFileOpenArchive("/Users/igor/Downloads/War3-1.27-Installer-enUS-TFT/Installer Tome.mpq", 0, 0, &archive);
//    SFileExtractFile(archive, "Assets\\Units\\Terran\\MarineTychus\\MarineTychus.m3", "/Users/igor/Desktop/MarineTychus.m3", 0);
//    SFileExtractFile(archive, "Assets\\Textures\\SpecialOps_Dropship_Diffuse.dds", "/Users/igor/Desktop/SpecialOps_Dropship_Diffuse.dds", 0);
    
    SFILE_FIND_DATA findData;
    HANDLE handle = SFileFindFirstFile(archive, "*", &findData, 0);
    if (handle) {
        LPCSTR skip[] = { NULL };// ".m3", ".ogg", ".ogv", ".fx", ".bls", ".gfx", ".wav", ".dds", ".tga", "\\Cache\\", NULL };
        do {
            for (LPCSTR *s = skip; *s; s++) {
                if (strstr(findData.cFileName, *s))
                    goto skip_print;
            }
            if (strstr(findData.cFileName, ".galaxy")) {
//                printf("%s\n", findData.cFileName);
            }
            if (strstr(findData.cFileName, "MapScript.galaxy")) {
                HANDLE file;
                SFileOpenFileEx(archive, findData.cFileName, SFILE_OPEN_FROM_MPQ, &file);
                char ch;
                while (SFileReadFile(file, &ch, 1, NULL, NULL)) {
                    printf("%c", ch);
                }
                SFileCloseFile(file);
                printf("\n");
            }

        skip_print:;
        } while (SFileFindNextFile(handle, &findData));
        SFileFindClose(handle);
    }

//    LoadModel(archive, "Assets\\Cliffs\\CliffMade0\\CliffMade0_ABAB_01.m3");
//    SFileExtractFile(archive, "Repack-MPQ\\fileset.base#Campaigns#Liberty.SC2Campaign#Base.SC2Maps\\Maps\\Campaign\\TRaynor01.SC2Map\\t3Terrain.xml", "/Users/igor/Desktop/t3Terrain.xml", 0);

    exit(0);
}
#endif

static BOOL filelock = false;
void PF_Sleep(DWORD msec);

typedef struct fsFind_s {
    DWORD archiveIndex;
    HANDLE current;
    PATHSTR mask;
    PATHSTR *looseFiles;
    DWORD looseCount;
    DWORD looseIndex;
} fsFind_t;

static BOOL FS_FindFilenameMatches(LPCSTR filename, LPCSTR mask) {
    PATHSTR normalizedFilename;
    PATHSTR normalizedMask;
    size_t mask_len;

    if (!mask || !mask[0] || !strcmp(mask, "*")) {
        return true;
    }
    FS_NormalizePath(filename, normalizedFilename, sizeof(normalizedFilename));
    FS_NormalizePath(mask, normalizedMask, sizeof(normalizedMask));
    mask_len = strlen(normalizedMask);
    if (mask_len > 0 && normalizedMask[mask_len - 1] == '*') {
        return !strncasecmp(normalizedFilename, normalizedMask, mask_len - 1);
    }
    return !strcasecmp(normalizedFilename, normalizedMask);
}

static BOOL FS_FindAppendLoose(fsFind_t *find, LPCSTR name) {
    PATHSTR *next;

    if (!find || !name || !*name) {
        return false;
    }
    next = realloc(find->looseFiles, (find->looseCount + 1) * sizeof(*next));
    if (!next) {
        return false;
    }
    find->looseFiles = next;
    snprintf(find->looseFiles[find->looseCount++], sizeof(PATHSTR), "%s", name);
    return true;
}

typedef struct {
    fsFind_t *find;
    LPCSTR root;
    LPCSTR rel;
} fsLooseCollect_t;

static void FS_FindCollectLooseDir(fsFind_t *find, LPCSTR root, LPCSTR rel);

static void FS_FindCollectLooseEntry(LPCSTR name, LPCSTR path, BOOL isDirectory, BOOL isFile, void *userData) {
    fsLooseCollect_t *collect = userData;
    char childRel[MAX_PATHLEN * 2];

    (void)path;
    if (!collect || !collect->find || !name || !*name) {
        return;
    }
    snprintf(childRel,
             sizeof(childRel),
             "%s%s%s",
             collect->rel,
             *collect->rel ? "\\" : "",
             name);
    if (isDirectory) {
        if (FS_IsMapPath(childRel)) {
            FS_FindAppendLoose(collect->find, childRel);
        }
        FS_FindCollectLooseDir(collect->find, collect->root, childRel);
    } else if (isFile && FS_FindFilenameMatches(childRel, collect->find->mask)) {
        FS_FindAppendLoose(collect->find, childRel);
    }
}

static void FS_FindCollectLooseDir(fsFind_t *find, LPCSTR root, LPCSTR rel) {
    char path[MAX_PATHLEN * 2];
    fsLooseCollect_t collect = { find, root, rel };

    if (!find || !root || !rel) {
        return;
    }
    FS_MakeDiskPath(root, rel, path, sizeof(path));
    FS_ForEachDiskEntry(path, FS_FindCollectLooseEntry, &collect);
}

static HANDLE FS_OpenNestedLooseFile(LPCSTR filename);

HANDLE FS_OpenFile(LPCSTR fileName) {
    while (filelock) {
        PF_Sleep(10);
    }
    filelock = true;
    if (!fileName || !*fileName) {
        filelock = false;
        return NULL;
    }
    for (int i = MAX_ARCHIVES - 1; i >= 0; i--) {
        HANDLE file;
        if (!archives[i]) {
            continue;
        }
        if (SFileOpenFileEx(archives[i], fileName, SFILE_OPEN_FROM_MPQ, &file)) {
            return file;
        }
    }
    HANDLE looseNestedFile = FS_OpenNestedLooseFile(fileName);
    if (looseNestedFile) {
        return looseNestedFile;
    }
    filelock = false;
    return NULL;
}

bool FS_FileExists(LPCSTR fileName) {
    HANDLE file = FS_OpenFile(fileName);
    if (file) {
        FS_CloseFile(file);
        return true;
    }
    FOR_LOOP(i, MAX_GAME_DIRS) {
        char path[MAX_PATHLEN * 2];

        if (!gameDirs[i][0]) {
            continue;
        }
        FS_MakeDiskPath(gameDirs[i], fileName, path, sizeof(path));
        if (FS_FileOnDiskExists(path)) {
            return true;
        }
#ifdef SC2
        if (FS_PathHasExtension(fileName, ".SC2Components") && FS_DirectoryExists(path)) {
            return true;
        }
#endif
    }
    return false;
}

HANDLE FS_ReadLooseFile(LPCSTR filename, LPDWORD size, DWORD extraBytes) {
    FOR_LOOP(i, MAX_GAME_DIRS) {
        char path[MAX_PATHLEN * 2];
        FILE *file;
        long fileSize;
        LPBYTE buffer;

        if (!gameDirs[i][0]) {
            continue;
        }
        FS_MakeDiskPath(gameDirs[i], filename, path, sizeof(path));
        file = fopen(path, "rb");
        if (!file) {
            continue;
        }
        if (fseek(file, 0, SEEK_END) != 0) {
            fclose(file);
            continue;
        }
        fileSize = ftell(file);
        if (fileSize < 0 || fseek(file, 0, SEEK_SET) != 0) {
            fclose(file);
            continue;
        }
        buffer = MemAlloc(fileSize + extraBytes);
        if (!buffer) {
            fclose(file);
            return NULL;
        }
        if (fileSize > 0 && fread(buffer, 1, fileSize, file) != (size_t)fileSize) {
            MemFree(buffer);
            fclose(file);
            return NULL;
        }
        fclose(file);
        if (extraBytes > 0) {
            memset(buffer + fileSize, 0, extraBytes);
        }
        if (size) {
            *size = (DWORD)fileSize;
        }
        return buffer;
    }
    return NULL;
}

static HANDLE FS_OpenNestedLooseFile(LPCSTR filename) {
    char outer[MAX_PATHLEN * 2];
    LPCSTR inner;
    DWORD outerSize = 0;
    BYTE *outerData;
    HANDLE file;

    if (!FS_SplitNestedArchivePath(filename, outer, sizeof(outer), &inner)) {
        return NULL;
    }

    outerData = FS_ReadLooseFile(outer, &outerSize, 0);
    if (!outerData || outerSize == 0) {
        if (outerData) {
            MemFree(outerData);
        }
        return NULL;
    }

    if (SFileOpenFileFromArchiveMemory(outerData, outerSize, inner, SFILE_OPEN_FROM_MPQ, &file)) {
        return file;
    }

    MemFree(outerData);
    return NULL;
}

void FS_CloseFile(HANDLE file) {
    SFileCloseFile(file);
    filelock = false;
}

// Quake 3-style FS_ReadFile: returns size, allocates buffer via buf pointer
int FS_ReadFileQ3(LPCSTR filename, void **buf) {
    if (!buf) {
        return -1;
    }
    DWORD size = 0;
    *buf = FS_ReadFile(filename, &size);
    if (!*buf) {
        return -1;
    }
    return (int)size;
}

HANDLE FS_ReadFile(LPCSTR filename, LPDWORD size) {
    HANDLE fp = FS_OpenFile(filename);
    if (!fp) {
        return FS_ReadLooseFile(filename, size, 0);
    }
    *size = SFileGetFileSize(fp, NULL);
    LPSTR buffer = MemAlloc(*size + 1);
    SFileReadFile(fp, buffer, *size, NULL, NULL);
    buffer[*size] = '\0';
    FS_CloseFile(fp);
    return buffer;
}

void FS_FreeFile(void *buf) {
    MemFree(buf);
}

static BOOL FS_FindAdvance(fsFind_t *find, SFILE_FIND_DATA *findData) {
    if (find && findData && find->looseIndex < find->looseCount) {
        LPCSTR name = find->looseFiles[find->looseIndex++];

        memset(findData, 0, sizeof(*findData));
        snprintf(findData->cFileName, sizeof(findData->cFileName), "%s", name);
        findData->szPlainName = findData->cFileName;
        return true;
    }
    while (find && find->archiveIndex < MAX_ARCHIVES) {
        if (find->current && SFileFindNextFile(find->current, findData)) {
            return true;
        }
        if (find->current) {
            SFileFindClose(find->current);
            find->current = NULL;
        }
        find->archiveIndex++;
        while (find->archiveIndex < MAX_ARCHIVES && !archives[find->archiveIndex]) {
            find->archiveIndex++;
        }
        if (find->archiveIndex < MAX_ARCHIVES) {
            find->current = SFileFindFirstFile(archives[find->archiveIndex], find->mask, findData, NULL);
            if (find->current) {
                return true;
            }
        }
    }
    return false;
}

HANDLE FS_FindFirstFile(LPCSTR mask, SFILE_FIND_DATA *findData) {
    fsFind_t *find;

    while (filelock) {
        PF_Sleep(10);
    }
    filelock = true;
    if (!mask || !*mask || !findData) {
        filelock = false;
        return NULL;
    }

    find = calloc(1, sizeof(*find));
    if (!find) {
        filelock = false;
        return NULL;
    }
    snprintf(find->mask, sizeof(find->mask), "%s", mask);
    LPCSTR looseRoot = (!strncasecmp(mask, "Maps\\", 5) || !strncasecmp(mask, "Maps/", 5)) ? "Maps" : "";
    FOR_LOOP(i, MAX_GAME_DIRS) {
        if (gameDirs[i][0]) {
            FS_FindCollectLooseDir(find, gameDirs[i], looseRoot);
        }
    }
    find->archiveIndex = 0;
    while (find->archiveIndex < MAX_ARCHIVES && !archives[find->archiveIndex]) {
        find->archiveIndex++;
    }
    if (find->archiveIndex < MAX_ARCHIVES) {
        find->current = SFileFindFirstFile(archives[find->archiveIndex], find->mask, findData, NULL);
        if (find->current) {
            return find;
        }
    }
    if (FS_FindAdvance(find, findData)) {
        return find;
    }
    free(find);
    filelock = false;
    return NULL;
}

BOOL FS_FindNextFile(HANDLE handle, SFILE_FIND_DATA *findData) {
    return FS_FindAdvance(handle, findData);
}

BOOL FS_FindClose(HANDLE handle) {
    fsFind_t *find = handle;

    if (find) {
        if (find->current) {
            SFileFindClose(find->current);
        }
        free(find->looseFiles);
        free(find);
    }
    filelock = false;
    return true;
}

DWORD FS_ListMaps(fsMapListFunc_t func, void *userData) {
    PATHSTR *maps;
    SFILE_FIND_DATA findData;
    HANDLE handle;
    DWORD count = 0;

    maps = calloc(MAX_FS_MAPS, sizeof(*maps));
    if (!maps) {
        return 0;
    }
    handle = FS_FindFirstFile("Maps\\*", &findData);
    while (handle && count < MAX_FS_MAPS) {
        PATHSTR path;
#ifdef SC2
        PATHSTR mapPath;
#endif

        FS_NormalizePath(findData.cFileName, path, sizeof(path));
#ifdef SC2
        if (FS_CollapseSC2MapPath(path, mapPath, sizeof(mapPath))) {
            snprintf(path, sizeof(path), "%s", mapPath);
        }
#endif
        if (FS_IsMapPath(path) && !FS_MapListHas(maps, count, path)) {
            snprintf(maps[count++], sizeof(maps[count]), "%s", path);
        }
        if (!FS_FindNextFile(handle, &findData)) {
            break;
        }
    }
    if (handle) {
        FS_FindClose(handle);
    }
    qsort(maps, count, sizeof(maps[0]), FS_CompareMapPaths);
    if (func) {
        FOR_LOOP(i, count) {
            func(maps[i], userData);
        }
    }
    free(maps);
    return count;
}

typedef struct {
    LPCSTR name;
    LPSTR out;
    DWORD out_size;
    DWORD matches;
} fsMapResolveState_t;

static void FS_ResolveMapPathCallback(LPCSTR path, void *userData) {
    fsMapResolveState_t *resolve = userData;

    if (!resolve || !FS_MapBaseEquals(path, resolve->name)) {
        return;
    }
    if (resolve->matches == 0 && resolve->out && resolve->out_size > 0) {
        snprintf(resolve->out, resolve->out_size, "%s", path);
    }
    resolve->matches++;
}

fsMapResolve_t FS_ResolveMapPath(LPCSTR name, LPSTR out, DWORD out_size) {
    fsMapResolveState_t resolve;
    PATHSTR normalized;

    if (out && out_size > 0) {
        out[0] = '\0';
    }
    if (!name || !*name) {
        return FS_MAP_RESOLVE_NOT_FOUND;
    }
    if (FS_IsExplicitMapPath(name)) {
        FS_NormalizePath(name, normalized, sizeof(normalized));
        if (out && out_size > 0) {
            snprintf(out, out_size, "%s", normalized);
        }
        return FS_FileExists(normalized) ? FS_MAP_RESOLVE_OK : FS_MAP_RESOLVE_NOT_FOUND;
    }

    memset(&resolve, 0, sizeof(resolve));
    resolve.name = name;
    resolve.out = out;
    resolve.out_size = out_size;
    FS_ListMaps(FS_ResolveMapPathCallback, &resolve);
    if (resolve.matches == 1) {
        return FS_MAP_RESOLVE_OK;
    }
    if (resolve.matches > 1) {
        return FS_MAP_RESOLVE_AMBIGUOUS;
    }
    return FS_MAP_RESOLVE_NOT_FOUND;
}

bool FS_ExtractFile(LPCSTR toExtract, LPCSTR extracted) {
    FOR_LOOP(i, MAX_ARCHIVES) {
        if (SFileExtractFile(archives[i], toExtract, extracted, 0))
            return true;
    }
    return false;
}

void FS_Init(void) {
//    ExtractStarCraft2();
    
//    FS_AddArchive("/Users/igor/Documents/Warcraft III Demo/war3.mpq");
//    FS_AddArchive("/Users/igor/Documents/StarCraft2/Campaigns/Liberty.SC2Campaign/base.SC2Assets");

//    FS_ExtractFile("Scripts\\common.j", "/Users/igor/Desktop/common.j");
//    FS_ExtractFile("Units\\UnitAbilities.slk", "/Users/igor/Desktop/UnitAbilities.slk");
//    FS_ExtractFile("Units\\UnitData.slk", "/Users/igor/Desktop/UnitData.slk");
//    FS_ExtractFile("Units\\UnitUI.slk", "/Users/igor/Desktop/UnitUI.slk");
//    FS_ExtractFile("Units\\UnitMetaData.slk", "/Users/igor/Desktop/UnitMetaData.slk");
//    FS_ExtractFile("Units\\UnitBalance.slk", "/Users/igor/Desktop/UnitBalance.slk");
//    FS_ExtractFile("Units\\UnitWeapons.slk", "/Users/igor/Desktop/UnitWeapons.slk");
//    FS_ExtractFile("Units\\AbilityData.slk", "/Users/igor/Desktop/AbilityData.slk");
//    FS_ExtractFile("Units\\ItemData.slk", "/Users/igor/Desktop/ItemData.slk");
//    FS_ExtractFile("Splats\\SplatData.slk", "/Users/igor/Desktop/SplatData.slk");
//    FS_ExtractFile("Splats\\UberSplatData.slk", "/Users/igor/Desktop/UberSplatData.slk");
//    FS_ExtractFile("Units\\MiscData.txt", "/Users/igor/Desktop/MiscData.txt");
//    FS_ExtractFile("Abilities\\Weapons\\FireBallMissile\\FireBallMissile.mdx", "/Users/igor/Desktop/FireBallMissile.mdx");
//    FS_ExtractFile("UI\\FrameDef\\GlobalStrings.fdf", "/Users/igor/Desktop/GlobalStrings.fdf");
//    FS_ExtractFile("UI\\FrameDef\\UI\\ConsoleUI.fdf", "/Users/igor/Desktop/ConsoleUI.fdf");

#if 0
    SFILE_FIND_DATA findData;
    HANDLE handle = SFileFindFirstFile(archives[0], "*", &findData, 0);
    if (handle) {
         do {
//             if(strstr(findData.cFileName, ".fdf")) {
//                 printf("%s\n", findData.cFileName);
//             }
#if 1
//             if (strstr(findData.cFileName, "EscMenuTemplates")||strstr(findData.cFileName, "QuestDialog")){
//             if (strstr(findData.cFileName, "Blizzard.j")) {
//             if (strstr(findData.cFileName, "EscMenuTemplates") ||
//                strstr(findData.cFileName, "CinematicPanel")) {
              if (strstr(findData.cFileName, ".txt")){
                 HANDLE file;
                 SFileOpenFileEx(archives[0], findData.cFileName, SFILE_OPEN_FROM_MPQ, &file);
                 char ch;
                 while (SFileReadFile(file, &ch, 1, NULL, NULL)) {
                     printf("%c", ch);
                 }
                 SFileCloseFile(file);
                 printf("\n");
             }
#endif
         } while (SFileFindNextFile(handle, &findData));
         SFileFindClose(handle);
     }
#endif
//    for (LPCSTR* s = WarcraftSheets; *s; s++) {
//        printf("%s\n", *s);
//        FS_ReadSheet(*s);
//    }

//    void FPrintSheetStructs(LPCSTR *fileNames);
//    FPrintSheetStructs(WarcraftSheets);
}

void FS_Shutdown(void) {
    FOR_LOOP(i, MAX_ARCHIVES) {
        SFileCloseArchive(archives[i]);
        archives[i] = NULL;
        archiveNames[i][0] = '\0';
    }
    FOR_LOOP(i, MAX_GAME_DIRS) {
        gameDirs[i][0] = '\0';
    }
}

BOMStatus PF_TextRemoveBom(LPSTR buffer) {
    unsigned char utf8_bom[] = { 0xEF, 0xBB, 0xBF };
    unsigned char utf16le_bom[] = { 0xFF, 0xFE };
    unsigned char utf16be_bom[] = { 0xFE, 0xFF };

    if (!buffer) {
        return INVALID_BOM;
    }
    size_t len = strlen(buffer);
    if (len >= 3 && memcmp(buffer, utf8_bom, 3) == 0) {
        memmove(buffer, buffer + 3, len - 3 + 1);
        return UTF8_BOM_FOUND;
    } else if (len >= 2 && memcmp(buffer, utf16le_bom, 2) == 0) {
        memmove(buffer, buffer + 2, len - 2 + 1);
        return UTF16LE_BOM_FOUND;
    } else if (len >= 2 && memcmp(buffer, utf16be_bom, 2) == 0) {
        memmove(buffer, buffer + 2, len - 2 + 1);
        return UTF16BE_BOM_FOUND;
    }
    return NO_BOM;
}

HANDLE MemAlloc(long size) {
    HANDLE mem = malloc(size);
//    printf("Alloc (%d) %llx\n", size, mem);
    memset(mem, 0, size);
    return mem;
}

void MemFree(HANDLE mem) {
//    printf("Free %llx\n", mem);
    free(mem);
}

void Com_Quit(void) {
    if (Cvar_Integer("com_frame_limit", 0) <= 0) {
        Cvar_WriteConfig(Cvar_String("config", ""));
    }
    CL_Shutdown();
    SV_Shutdown();
    NET_Shutdown();
    FS_Shutdown();
    Sys_Quit();
}

void COM_InitArgv(int argc, LPCSTR *argv) {
    int i;

    if (argc > MAX_NUM_ARGVS) {
        Com_Error(ERR_FATAL, "argc > MAX_NUM_ARGVS");
    }
    com_argc = argc;
    for (i = 0; i < argc; i++) {
        if (!argv[i] || strlen(argv[i]) >= MAX_ARG_CHARS) {
            com_argv[i] = "";
        } else {
            com_argv[i] = argv[i];
        }
    }
}

int COM_Argc(void) {
    return com_argc;
}

LPCSTR COM_Argv(int arg) {
    if (arg < 0 || arg >= com_argc || !com_argv[arg]) {
        return "";
    }
    return com_argv[arg];
}

void COM_ClearArgv(int arg) {
    if (arg < 0 || arg >= com_argc || !com_argv[arg]) {
        return;
    }
    com_argv[arg] = "";
}

typedef struct {
    LPCSTR name;
    DWORD count;
} comMapMatchPrint_t;

static void Com_PrintMapCallback(LPCSTR path, void *userData) {
    DWORD *count = userData;

    fprintf(stderr, "%s\n", path);
    if (count) {
        (*count)++;
    }
}

static void Com_PrintMapMatchCallback(LPCSTR path, void *userData) {
    comMapMatchPrint_t *matches = userData;

    if (!matches || !FS_MapBaseEquals(path, matches->name)) {
        return;
    }
    fprintf(stderr, "  %s\n", path);
    matches->count++;
}

bool Com_ResolveMapArgument(LPCSTR arg, LPSTR out, DWORD out_size) {
    fsMapResolve_t status;

    status = FS_ResolveMapPath(arg, out, out_size);
    if (status == FS_MAP_RESOLVE_OK) {
        return true;
    }
    if (status == FS_MAP_RESOLVE_AMBIGUOUS) {
        comMapMatchPrint_t matches = { arg, 0 };

        fprintf(stderr, "Map name \"%s\" is ambiguous:\n", arg);
        FS_ListMaps(Com_PrintMapMatchCallback, &matches);
        return false;
    }
    fprintf(stderr, "Can't find map %s\n", arg ? arg : "");
    fprintf(stderr, "Run \"maps\" to list available maps.\n");
    return false;
}

void MenuAction(LPCSTR action, LPCSTR arg) {
    if (!strcmp(action, "map")) {
        PATHSTR map;

        if (!arg || !*arg) {
            return;
        }
        if (!Com_ResolveMapArgument(arg, map, sizeof(map))) {
            return;
        }
        CL_SetGameplayBindings();
        CL_BeginLoadingMap(map);
        SV_Map(map);
    } else if (!strcmp(action, "quit")) {
        Com_Quit();
    }
}

static void Com_Maps_f(void) {
    DWORD count = 0;

    FS_ListMaps(Com_PrintMapCallback, &count);
    fprintf(stderr, "%u maps\n", count);
}

static void Com_Path_f(void) {
    fprintf(stderr, "Current search path:\n");
    FOR_LOOP(i, MAX_GAME_DIRS) {
        if (gameDirs[i][0]) {
            fprintf(stderr, "%s\n", gameDirs[i]);
        }
    }
    FOR_LOOP(i, MAX_ARCHIVES) {
        if (archives[i]) {
            fprintf(stderr, "%s\n", archiveNames[i]);
        }
    }
}

static BOOL Com_DirExtensionMatches(LPCSTR path, LPCSTR extension) {
    char dotted[64];

    if (!extension || !*extension) {
        return true;
    }
    if (*extension == '.') {
        return FS_PathHasExtension(path, extension);
    }
    snprintf(dotted, sizeof(dotted), ".%s", extension);
    return FS_PathHasExtension(path, dotted);
}

static void Com_Dir_f(void) {
    LPCSTR path = Cmd_Argc() > 1 ? Cmd_Argv(1) : "*";
    LPCSTR extension = Cmd_Argc() > 2 ? Cmd_Argv(2) : NULL;
    char mask[MAX_PATHLEN * 2];
    SFILE_FIND_DATA findData;
    HANDLE handle;
    DWORD count = 0;

    if (strchr(path, '*')) {
        snprintf(mask, sizeof(mask), "%s", path);
    } else {
        snprintf(mask, sizeof(mask), "%s\\*", path);
    }
    handle = FS_FindFirstFile(mask, &findData);
    while (handle) {
        PATHSTR normalized;

        FS_NormalizePath(findData.cFileName, normalized, sizeof(normalized));
        if (Com_DirExtensionMatches(normalized, extension)) {
            fprintf(stderr, "%s\n", normalized);
            count++;
        }
        if (!FS_FindNextFile(handle, &findData)) {
            break;
        }
    }
    if (handle) {
        FS_FindClose(handle);
    }
    fprintf(stderr, "%u files\n", count);
}

static void Com_Map_f(void) {
    if (Cmd_Argc() < 2) {
        fprintf(stderr, "Usage: map <map>\n");
        return;
    }
    MenuAction("map", Cmd_ArgsFrom(1));
}

void Com_Init(int argc, LPCSTR *argv) {
    COM_InitArgv(argc, argv);
    Cbuf_Init();
    Cvar_Init();
    FS_SetSheetHost(&MAKE(SHEETHOST,
        .ReadFile = FS_ReadFile,
        .FreeFile = FS_FreeFile,
        .MemAlloc = MemAlloc,
        .MemFree = MemFree,
    ));
    Key_Init();
    Cmd_AddCommand("map", Com_Map_f);
    Cmd_AddCommand("maps", Com_Maps_f);
    Cmd_AddCommand("path", Com_Path_f);
    Cmd_AddCommand("dir", Com_Dir_f);
    Cvar_ApplyConfigCommandLine(argc, argv);
    Cbuf_AddEarlyCommands(false);
    Cbuf_Execute();
    FS_Init();
    Cvar_LoadConfig("share/default.cfg");
    Cbuf_Execute();
#ifdef WOW
    Cvar_LoadConfig("share/openwow.cfg");
#else
    Cvar_LoadConfig("share/openwarcraft3.cfg");
#endif
    Cbuf_Execute();
    Cvar_LoadConfig(Cvar_String("config", ""));
    Cbuf_Execute();
    Cvar_LoadConfig("share/autoexec.cfg");
    Cbuf_Execute();
    Cvar_Set("map", "");
    Cvar_Set("connect", "");
    Cvar_ApplyCommandLine(argc, argv);
    Cbuf_AddEarlyCommands(true);
    Cbuf_Execute();
    /* Leave generic +commands queued until client/UI modules register commands. */
}

void Com_Error(errorCode_t code, LPCSTR fmt, ...) {
    va_list argptr;
    static char msg[MAXPRINTMSG];
    static bool recursive;

    if (recursive) {
        fprintf(stderr, "recursive error after: %s", msg);
    }
    
    recursive = true;

    va_start(argptr,fmt);
    vsprintf(msg,fmt,argptr);
    va_end(argptr);
    
    switch (code) {
        case ERR_QUIT:
//            CL_Drop ();
            recursive = false;
//            longjmp (abortframe, -1);
            break;
        case ERR_DROP:
            break;
        default:
            break;
    }
    
    fprintf(stderr, "%s", msg);
}
