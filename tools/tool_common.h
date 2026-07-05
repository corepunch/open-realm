#ifndef tool_common_h
#define tool_common_h

#include "common/common.h"

#include <stdlib.h>
#include <string.h>

#ifndef TOOL_COMMON_NO_MPQ
static inline HANDLE Tool_AddArchive(HANDLE *archives, size_t count, LPCSTR filename);
static inline HANDLE Tool_OpenFile(HANDLE const *archives, size_t count, LPCSTR fileName);
static inline void Tool_CloseFile(HANDLE file);
static inline bool Tool_ExtractFile(HANDLE const *archives, size_t count, LPCSTR toExtract, LPCSTR extracted);
static inline bool Tool_FileExists(HANDLE const *archives, size_t count, LPCSTR fileName);
static inline void Tool_CloseArchives(HANDLE *archives, size_t count);
#endif

static inline HANDLE Tool_MemAlloc(long size);
static inline void Tool_MemFree(HANDLE mem);
void *Tool_XMalloc(size_t size);
void *Tool_XRealloc(void *ptr, size_t size);
static inline char *Tool_XStrdup(const char *s);

static inline void Tool_NormalizeSlashes(char *path, char slash);
static inline void Tool_TrimEdgeSlashes(char *path);
static inline char *Tool_PathJoin(const char *base, const char *name);
static inline char *Tool_PathParent(const char *path);
static inline const char *Tool_PathBasename(const char *path);
static inline const char *Tool_PathExt(const char *path);

#ifndef TOOL_COMMON_NO_MPQ
#include "../common/mpq.h"
#include "../common/mpq.c"

#include <dirent.h>
#include <sys/stat.h>

/* Recursively walk 'dir', calling cb(archive_path, ud) for every file
 * whose extension is a recognised archive type (.mpq / .SC2Assets /
 * .SC2Data / .SC2Maps).  Caller collects paths, sorts, then opens. */
static inline void Tool_ForEachArchive(const char *dir,
                                       void (*cb)(const char *path, void *ud),
                                       void *ud) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            Tool_ForEachArchive(path, cb, ud);
        } else if (S_ISREG(st.st_mode)) {
            const char *ext = Tool_PathExt(e->d_name);
            if (!strcasecmp(ext, "mpq") ||
                !strcasecmp(ext, "SC2Assets") ||
                !strcasecmp(ext, "SC2Data") ||
                !strcasecmp(ext, "SC2Maps"))
                cb(path, ud);
        }
    }
    closedir(d);
}

static inline HANDLE Tool_AddArchive(HANDLE *archives, size_t count, LPCSTR filename) {
    for (size_t i = 0; i < count; i++) {
        if (archives[i]) {
            continue;
        }
        if (!SFileOpenArchive(filename, 0, 0, &archives[i])) {
            fprintf(stderr, "Can't add archive %s\n", filename);
            return NULL;
        }
        return archives[i];
    }
    fprintf(stderr, "Too many archives\n");
    return NULL;
}

static inline HANDLE Tool_OpenFile(HANDLE const *archives, size_t count, LPCSTR fileName) {
    if (!fileName || !*fileName) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        HANDLE file = NULL;
        if (archives[i] && SFileOpenFileEx(archives[i], fileName, SFILE_OPEN_FROM_MPQ, &file)) {
            return file;
        }
    }
    return NULL;
}

static inline void Tool_CloseFile(HANDLE file) {
    if (file) {
        SFileCloseFile(file);
    }
}

static inline bool Tool_ExtractFile(HANDLE const *archives, size_t count, LPCSTR toExtract, LPCSTR extracted) {
    for (size_t i = 0; i < count; i++) {
        if (archives[i] && SFileExtractFile(archives[i], toExtract, extracted, 0)) {
            return true;
        }
    }
    return false;
}

static inline bool Tool_FileExists(HANDLE const *archives, size_t count, LPCSTR fileName) {
    HANDLE file = Tool_OpenFile(archives, count, fileName);
    if (!file) {
        return false;
    }
    Tool_CloseFile(file);
    return true;
}

static inline void Tool_CloseArchives(HANDLE *archives, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (archives[i]) {
            SFileCloseArchive(archives[i]);
            archives[i] = NULL;
        }
    }
}
#endif

static inline HANDLE Tool_MemAlloc(long size) {
    void *mem = calloc(1, (size_t)size);
    if (!mem) {
        fprintf(stderr, "Out of memory allocating %ld bytes\n", size);
        exit(1);
    }
    return mem;
}

static inline void Tool_MemFree(HANDLE mem) {
    free(mem);
}

void *Tool_XMalloc(size_t size) {
    void *ptr = calloc(1, size ? size : 1);
    if (!ptr) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return ptr;
}

void *Tool_XRealloc(void *ptr, size_t size) {
    void *next = realloc(ptr, size ? size : 1);
    if (!next) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return next;
}

static inline char *Tool_XStrdup(const char *s) {
    size_t len = s ? strlen(s) : 0;
    char *copy = Tool_XMalloc(len + 1);
    if (len) {
        memcpy(copy, s, len);
    }
    copy[len] = '\0';
    return copy;
}

static inline void Tool_NormalizeSlashes(char *path, char slash) {
    if (!path) {
        return;
    }
    for (; *path; path++) {
        if (*path == '/' || *path == '\\') {
            *path = slash;
        }
    }
}

static inline void Tool_TrimEdgeSlashes(char *path) {
    size_t len;

    if (!path) {
        return;
    }

    len = strlen(path);
    while (len > 0 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
        path[--len] = '\0';
    }
    while (*path == '\\' || *path == '/') {
        memmove(path, path + 1, strlen(path));
    }
}

static inline char *Tool_PathJoin(const char *base, const char *name) {
    size_t base_len = base ? strlen(base) : 0;
    size_t name_len = name ? strlen(name) : 0;
    bool need_slash = base_len > 0 && name_len > 0;
    char *out = Tool_XMalloc(base_len + name_len + (need_slash ? 2 : 1));

    if (need_slash) {
        snprintf(out, base_len + name_len + 2, "%s/%s", base, name);
    } else if (base_len > 0) {
        snprintf(out, base_len + 1, "%s", base);
    } else {
        snprintf(out, name_len + 1, "%s", name ? name : "");
    }
    return out;
}

static inline char *Tool_PathParent(const char *path) {
    char *copy = Tool_XStrdup(path ? path : "");
    char *slash;

    Tool_NormalizeSlashes(copy, '/');
    slash = strrchr(copy, '/');
    if (slash) {
        *slash = '\0';
    } else {
        copy[0] = '\0';
    }
    return copy;
}

static inline const char *Tool_PathBasename(const char *path) {
    const char *slash;
    const char *back;
    const char *base;

    if (!path) {
        return "";
    }

    slash = strrchr(path, '/');
    back = strrchr(path, '\\');
    base = slash;
    if (!base || (back && back > base)) {
        base = back;
    }
    return base ? base + 1 : path;
}

static inline const char *Tool_PathExt(const char *path) {
    const char *base = Tool_PathBasename(path);
    const char *dot = strrchr(base, '.');
    return dot ? dot + 1 : "";
}

#ifndef TOOL_COMMON_NO_MPQ
static inline HANDLE Tool_ReadFileRaw(LPCSTR filename, LPDWORD size);

/* Tool-specific FS_ReadFile wrapper for Quake 3 pattern */
static inline int Tool_FS_ReadFile(LPCSTR filename, void **buf) {
    if (!buf) return -1;
    DWORD size = 0;
    HANDLE raw = Tool_ReadFileRaw(filename, &size);
    if (!raw) {
        return -1;
    }
    *buf = raw;
    return (int)size;
}

typedef struct {
    HANDLE *archives;
    size_t count;
} ToolSheetHostState;

static inline ToolSheetHostState *Tool_SheetHostState(void) {
    static ToolSheetHostState state;
    return &state;
}

static inline HANDLE Tool_ReadFileRaw(LPCSTR filename, LPDWORD size) {
    ToolSheetHostState *state = Tool_SheetHostState();
    HANDLE file;
    HANDLE buffer;
    DWORD fileSize;

    if (!state->archives || state->count == 0) {
        return NULL;
    }
    file = Tool_OpenFile(state->archives, state->count, filename);
    if (!file) {
        return NULL;
    }
    fileSize = SFileGetFileSize(file, NULL);
    buffer = Tool_MemAlloc((long)fileSize + 1);
    if (!buffer) {
        Tool_CloseFile(file);
        return NULL;
    }
    if (fileSize > 0) {
        SFileReadFile(file, buffer, fileSize, NULL, NULL);
    }
    Tool_CloseFile(file);
    ((char *)buffer)[fileSize] = '\0';
    if (size) {
        *size = fileSize;
    }
    return buffer;
}

static inline void Tool_FS_FreeFile(void *buf) {
    MemFree(buf);
}

static inline void Tool_SetSheetHost(HANDLE *archives, size_t count) {
    ToolSheetHostState *state = Tool_SheetHostState();
    state->archives = archives;
    state->count = count;
    FS_SetSheetHost(&MAKE(SHEETHOST,
        .ReadFile = Tool_ReadFileRaw,
        .FreeFile = Tool_MemFree,
        .MemAlloc = Tool_MemAlloc,
        .MemFree = Tool_MemFree,
    ));
}
#endif

#endif
