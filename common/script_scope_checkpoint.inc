/* Private, immutable checkpoint of a named player's script save namespace.
 * Map JASS cannot address this tree: only the engine owns checkpoint IDs.
 * Cloning on load prevents loading an older manual save from changing another
 * slot's cache, zone saves, or the current live campaign's storage. */
#define FS_CHECKPOINT_MAX_BYTES (512u * 1024u * 1024u)
#define FS_CHECKPOINT_MAX_FILES 2048u

typedef struct {
    uint64_t bytes;
    unsigned files;
} fs_checkpoint_budget_t;

static bool FS_CheckpointEntryName(cstring_t name) {
    size_t len = strlen(name);
    if (!len || len > 128 || !strcmp(name, ".") || !strcmp(name, "..") ||
        name[0] == '.' || name[len - 1] == '.' || name[len - 1] == ' ') return false;
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == ' ')) return false;
    }
    return true;
}

/* A checkpoint stores only the exact objects that untrusted JASS can create:
 * root save files, Blizzard/<slot-or-Zones>/save files, and cache sidecars.
 * Never follow links, special files, or nested structures outside that shape. */
static bool FS_CheckpointAllowed(cstring_t name, unsigned depth, bool directory,
                                 cstring_t top) {
    if (!FS_CheckpointEntryName(name)) return false;
    if (depth == 0) return directory ? (!strcmp(name, "Blizzard") || !strcmp(name, "cache")) : true;
    if (!top) return false;
    if (!strcmp(top, "cache")) return depth == 1 && !directory;
    if (!strcmp(top, "Blizzard")) return depth == 1 ? directory : depth == 2 && !directory;
    return false;
}

#ifndef _WIN32
static int FS_CheckpointRootDir(bool create) {
    if (!FS_EnsureSaveDirectory()) return -1;
    int save = open(fs_save_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (save < 0) return -1;
    if (create && mkdirat(save, "script", 0700) && errno != EEXIST) { close(save); return -1; }
    int scripts = openat(save, "script", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(save);
    if (scripts < 0) return -1;
    if (create && mkdirat(scripts, "checkpoints", 0700) && errno != EEXIST) {
        close(scripts); return -1;
    }
    int checkpoints = openat(scripts, "checkpoints", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(scripts);
    return checkpoints;
}

static bool FS_CheckpointCopyTree(int source, int target, unsigned depth,
                                  cstring_t top, fs_checkpoint_budget_t *budget) {
    int source_scan = dup(source);
    if (source_scan < 0) return false;
    DIR *entries = fdopendir(source_scan);
    if (!entries) { close(source_scan); return false; }
    bool ok = true;
    struct dirent *entry;
    errno = 0;
    while (ok && (entry = readdir(entries)) != NULL) {
        cstring_t name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        struct stat info;
        if (fstatat(source, name, &info, AT_SYMLINK_NOFOLLOW) != 0) { ok = false; break; }
        bool directory = S_ISDIR(info.st_mode);
        if ((!directory && !S_ISREG(info.st_mode)) ||
            !FS_CheckpointAllowed(name, depth, directory, top)) { ok = false; break; }
        if (directory) {
            if (mkdirat(target, name, 0700) != 0) { ok = false; break; }
            int from = openat(source, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            int to = openat(target, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (from < 0 || to < 0) ok = false;
            else ok = FS_CheckpointCopyTree(from, to, depth + 1, depth == 0 ? name : top, budget);
            if (from >= 0) close(from);
            if (to >= 0) close(to);
        } else {
            if (info.st_size < 0 || (uint64_t)info.st_size > FS_CHECKPOINT_MAX_BYTES - budget->bytes ||
                budget->files >= FS_CHECKPOINT_MAX_FILES) { ok = false; break; }
            int from = openat(source, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            int to = openat(target, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            if (from < 0 || to < 0) ok = false;
            if (ok) {
                struct stat opened;
                if (fstat(from, &opened) != 0 || !S_ISREG(opened.st_mode) ||
                    opened.st_size != info.st_size) ok = false;
            }
            char bytes[65536];
            uint64_t copied = 0;
            while (ok) {
                ssize_t n = read(from, bytes, sizeof(bytes));
                if (n < 0) { ok = false; break; }
                if (n == 0) break;
                copied += (uint64_t)n;
                if (copied > (uint64_t)info.st_size) { ok = false; break; }
                ssize_t offset = 0;
                while (offset < n) {
                    ssize_t written = write(to, bytes + offset, (size_t)(n - offset));
                    if (written <= 0) { ok = false; break; }
                    offset += written;
                }
            }
            if (copied != (uint64_t)info.st_size) ok = false;
            if (to >= 0 && fsync(to) != 0) ok = false;
            if (from >= 0) close(from);
            if (to >= 0 && close(to) != 0) ok = false;
            if (!ok) break;
            budget->bytes += copied;
            budget->files++;
        }
        errno = 0;
    }
    if (errno) ok = false;
    if (ok && fsync(target) != 0) ok = false;
    closedir(entries);
    return ok;
}

/* Failed/old private checkpoints contain only regular files and controlled
 * directories, but use no-follow handles even when cleaning them up. */
static bool FS_CheckpointRemoveTree(int root) {
    int scan = dup(root);
    if (scan < 0) return false;
    DIR *entries = fdopendir(scan);
    if (!entries) { close(scan); return false; }
    bool ok = true;
    struct dirent *entry;
    while ((entry = readdir(entries)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        struct stat info;
        if (fstatat(root, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) { ok = false; continue; }
        if (S_ISDIR(info.st_mode)) {
            int child = openat(root, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (child < 0) { ok = false; continue; }
            bool removed = FS_CheckpointRemoveTree(child);
            close(child);
            if (!removed || unlinkat(root, entry->d_name, AT_REMOVEDIR) != 0) ok = false;
        } else if (S_ISREG(info.st_mode)) {
            if (unlinkat(root, entry->d_name, 0) != 0) ok = false;
        } else ok = false;
    }
    closedir(entries);
    return ok;
}

static bool FS_CreateScriptCheckpoint(char id[33]) {
    int parent = FS_CheckpointRootDir(true);
    if (parent < 0) return false;
    int source = FS_ScriptRootFd(true);
    bool ok = source >= 0 && FS_GenerateScriptScopeId(id) && mkdirat(parent, id, 0700) == 0;
    int target = ok ? openat(parent, id, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
    if (target < 0) ok = false;
    if (ok) {
        fs_checkpoint_budget_t budget = {0};
        ok = FS_CheckpointCopyTree(source, target, 0, NULL, &budget);
    }
    if (source >= 0) close(source);
    if (target >= 0) close(target);
    if (!ok && target >= 0) {
        int failed = openat(parent, id, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (failed >= 0) { FS_CheckpointRemoveTree(failed); close(failed); unlinkat(parent, id, AT_REMOVEDIR); }
    }
    close(parent);
    return ok;
}

static bool FS_CloneScriptCheckpoint(cstring_t checkpoint) {
    char live[33];
    int parent = FS_CheckpointRootDir(false);
    if (parent < 0 || !FS_GenerateScriptScopeId(live)) { if (parent >= 0) close(parent); return false; }
    int source = openat(parent, checkpoint, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(parent);
    if (source < 0) return false;
    /* Exclusive creation: never overwrite or remove an already existing
     * scope, even if the random ID happened to collide. */
    int save = open(fs_save_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    int scripts = save >= 0 ? openat(save, "script", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
    if (save >= 0) close(save);
    bool created = scripts >= 0 && mkdirat(scripts, live, 0700) == 0;
    if (scripts >= 0) close(scripts);
    if (!created) { close(source); return false; }
    strlcpy(fs_script_session, live, sizeof(fs_script_session));
    int target = FS_ScriptRootFd(false);
    bool ok = target >= 0;
    if (ok) {
        fs_checkpoint_budget_t budget = {0};
        ok = FS_CheckpointCopyTree(source, target, 0, NULL, &budget);
    }
    close(source);
    if (target >= 0) close(target);
    if (!ok) {
        int failed = FS_ScriptRootFd(false);
        if (failed >= 0) {
            FS_CheckpointRemoveTree(failed);
            close(failed);
#ifndef _WIN32
            int scripts = open(fs_save_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (scripts >= 0) {
                int root = openat(scripts, "script", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (root >= 0) { unlinkat(root, live, AT_REMOVEDIR); close(root); }
                close(scripts);
            }
#endif
        }
        FS_ResetScriptSaveScope();
    }
    return ok;
}
#else
/* Windows keeps the same namespace allowlist and rejects reparse points.
 * The Win32 implementation is restricted to ordinary files/dirs. */
static bool FS_CheckpointWinRoot(bool create, PATHSTR path) {
    if (!FS_EnsureSaveDirectory() ||
        snprintf(path, MAX_PATHLEN, "%s/script", fs_save_dir) >= MAX_PATHLEN) return false;
    if (create) _mkdir(path);
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY) ||
        (attr & FILE_ATTRIBUTE_REPARSE_POINT) ||
        strlcat(path, "/checkpoints", MAX_PATHLEN) >= MAX_PATHLEN) return false;
    if (create) _mkdir(path);
    attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attr & FILE_ATTRIBUTE_REPARSE_POINT);
}

static bool FS_CheckpointWinCopy(cstring_t from, cstring_t to, unsigned depth,
                                  cstring_t top, fs_checkpoint_budget_t *budget) {
    char pattern[MAX_PATHLEN];
    if (snprintf(pattern, sizeof(pattern), "%s/*", from) >= (int)sizeof(pattern)) return false;
    WIN32_FIND_DATAA entry;
    HANDLE listing = FindFirstFileA(pattern, &entry);
    if (listing == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    bool ok = true;
    do {
        cstring_t name = entry.cFileName;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        bool directory = (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !FS_CheckpointAllowed(name, depth, directory, top)) { ok = false; break; }
        char src[MAX_PATHLEN], dst[MAX_PATHLEN];
        if (snprintf(src, sizeof(src), "%s/%s", from, name) >= (int)sizeof(src) ||
            snprintf(dst, sizeof(dst), "%s/%s", to, name) >= (int)sizeof(dst)) { ok = false; break; }
        if (directory) {
            if (_mkdir(dst) != 0) { ok = false; break; }
            ok = FS_CheckpointWinCopy(src, dst, depth + 1, depth == 0 ? name : top, budget);
        } else {
            uint64_t size = ((uint64_t)entry.nFileSizeHigh << 32) | entry.nFileSizeLow;
            if (size > FS_CHECKPOINT_MAX_BYTES - budget->bytes ||
                budget->files >= FS_CHECKPOINT_MAX_FILES || !CopyFileA(src, dst, TRUE)) { ok = false; break; }
            budget->bytes += size;
            budget->files++;
        }
    } while (ok && FindNextFileA(listing, &entry));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = false;
    FindClose(listing);
    return ok;
}

static bool FS_CreateScriptCheckpoint(char id[33]) {
    PATHSTR base, source, target;
    if (!FS_CheckpointWinRoot(true, base) || !FS_ScriptRoot(source, true) ||
        !FS_GenerateScriptScopeId(id) ||
        snprintf(target, sizeof(target), "%s/%s", base, id) >= (int)sizeof(target) ||
        _mkdir(target) != 0) return false;
    fs_checkpoint_budget_t budget = {0};
    return FS_CheckpointWinCopy(source, target, 0, NULL, &budget);
}

static bool FS_CloneScriptCheckpoint(cstring_t checkpoint) {
    PATHSTR base, source, target;
    char live[33];
    if (!FS_CheckpointWinRoot(false, base) ||
        snprintf(source, sizeof(source), "%s/%s", base, checkpoint) >= (int)sizeof(source) ||
        !FS_GenerateScriptScopeId(live)) return false;
    DWORD attr = GetFileAttributesA(source);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY) ||
        (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    strlcpy(fs_script_session, live, sizeof(fs_script_session));
    bool ok = FS_ScriptRoot(target, true);
    if (ok) {
        fs_checkpoint_budget_t budget = {0};
        ok = FS_CheckpointWinCopy(source, target, 0, NULL, &budget);
    }
    if (!ok) FS_ResetScriptSaveScope();
    return ok;
}
#endif
