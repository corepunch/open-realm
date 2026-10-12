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

static const uint8_t fs_checkpoint_gzip_magic[8] = {'O','R','C','P','G','Z','1','!'};

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

/* Checkpoint-local compressed envelope. These files are not exposed to JASS:
 * restoration expands them to the original bytes in the playthrough's live
 * scope. Older uncompressed checkpoint files are still readable. */

static bool FS_CheckpointCompressFile(int input, int output, uint64_t input_size) {
    if (write(output, fs_checkpoint_gzip_magic, sizeof(fs_checkpoint_gzip_magic)) !=
        (ssize_t)sizeof(fs_checkpoint_gzip_magic)) return false;
    int gzip_fd = dup(output);
    if (gzip_fd < 0) return false;
    gzFile gz = gzdopen(gzip_fd, "wb1");
    if (!gz) { close(gzip_fd); return false; }
    char buffer[65536];
    uint64_t consumed = 0;
    bool ok = true;
    while (consumed < input_size && ok) {
        size_t remaining = (size_t)MIN((uint64_t)sizeof(buffer), input_size - consumed);
        ssize_t got = read(input, buffer, remaining);
        if (got <= 0 || gzwrite(gz, buffer, (unsigned)got) != (int)got) ok = false;
        else consumed += (uint64_t)got;
    }
    if (gzclose(gz) != Z_OK) ok = false;
    return ok && consumed == input_size;
}

static bool FS_CheckpointExpandFile(int input, int output, uint64_t max_size) {
    int gzip_fd = dup(input);
    if (gzip_fd < 0) return false;
    gzFile gz = gzdopen(gzip_fd, "rb");
    if (!gz) { close(gzip_fd); return false; }
    char buffer[65536];
    uint64_t expanded = 0;
    bool ok = true;
    for (;;) {
        int got = gzread(gz, buffer, sizeof(buffer));
        if (got < 0 || (uint64_t)got > max_size - expanded) { ok = false; break; }
        if (!got) { ok = gzeof(gz) != 0; break; }
        ssize_t offset = 0;
        while (offset < got) {
            ssize_t n = write(output, buffer + offset, (size_t)(got - offset));
            if (n <= 0) { ok = false; break; }
            offset += n;
        }
        if (!ok) break;
        expanded += (uint64_t)got;
    }
    if (gzclose(gz) != Z_OK) ok = false;
    return ok;
}

static bool FS_CheckpointCopyTree(int source, int target, unsigned depth,
                                  cstring_t top, fs_checkpoint_budget_t *budget, bool snapshot) {
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
            else ok = FS_CheckpointCopyTree(from, to, depth + 1, depth == 0 ? name : top, budget, snapshot);
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
            /* Reflink first: shared physical blocks without shared inodes.
             * On unsupported filesystems, compress immutable snapshots. Live
             * files are always restored byte-for-byte (never gzip-wrapped). */
            bool completed = false;
            uint8_t prefix[sizeof(fs_checkpoint_gzip_magic)] = {0};
            if (ok && !snapshot) {
                ssize_t seen = read(from, prefix, sizeof(prefix));
                if (seen < 0) ok = false;
                if (ok && seen == sizeof(prefix) &&
                    !memcmp(prefix, fs_checkpoint_gzip_magic, sizeof(prefix))) {
                    completed = FS_CheckpointExpandFile(from, to, FS_CHECKPOINT_MAX_BYTES - budget->bytes);
                    if (!completed) ok = false;
                }
                if (ok && !completed && lseek(from, 0, SEEK_SET) != 0) ok = false;
            }
#if defined(__linux__) && defined(FICLONE)
            if (ok && !completed && ioctl(to, FICLONE, from) == 0) completed = true;
#endif
            if (ok && snapshot && !completed) {
                /* A failed clone must not leave a partial target or an offset
                 * inherited by the compression writer. */
                if (ftruncate(to, 0) != 0 || lseek(to, 0, SEEK_SET) != 0 ||
                    lseek(from, 0, SEEK_SET) != 0) ok = false;
                if (ok) completed = FS_CheckpointCompressFile(from, to, (uint64_t)info.st_size);
                struct stat packed;
                if (completed && fstat(to, &packed) == 0 && packed.st_size >= 0 &&
                    (uint64_t)packed.st_size >= (uint64_t)info.st_size) completed = false;
                if (!completed && (ftruncate(to, 0) != 0 || lseek(to, 0, SEEK_SET) != 0 ||
                                   lseek(from, 0, SEEK_SET) != 0)) ok = false;
            }
            char bytes[65536];
            uint64_t copied = completed ? (uint64_t)info.st_size : 0;
            while (ok && !completed) {
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
            struct stat result;
            if (ok && (fstat(to, &result) != 0 || result.st_size < 0 ||
                (uint64_t)result.st_size > FS_CHECKPOINT_MAX_BYTES - budget->bytes)) ok = false;
            if (to >= 0 && fsync(to) != 0) ok = false;
            if (from >= 0) close(from);
            if (to >= 0 && close(to) != 0) ok = false;
            if (!ok) break;
            budget->bytes += snapshot ? copied : (uint64_t)result.st_size;
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

/* A live playthrough is disposable after leaving it. Checkpoints remain
 * intact; they are immutable and reached only through player-save bindings.
 * The removal cannot recurse into symlinks or uncontrolled object kinds. */
static void FS_RemoveCurrentLiveWorkspace(void) {
    int campaigns = FS_CampaignWorkspacesFd(false);
    if (campaigns < 0) return;
    int branch = openat(campaigns, fs_script_session,
                        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (branch >= 0) {
        int live = openat(branch, "live", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (live >= 0) {
            bool removed = FS_CheckpointRemoveTree(live);
            close(live);
            if (removed) unlinkat(branch, "live", AT_REMOVEDIR);
        }
        close(branch);
        unlinkat(campaigns, fs_script_session, AT_REMOVEDIR);
    }
    close(campaigns);
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
        ok = FS_CheckpointCopyTree(source, target, 0, NULL, &budget, true);
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
    int campaigns = FS_CampaignWorkspacesFd(true);
    bool created = campaigns >= 0 && mkdirat(campaigns, live, 0700) == 0;
    if (campaigns >= 0) close(campaigns);
    if (!created) { close(source); return false; }
    strlcpy(fs_script_session, live, sizeof(fs_script_session));
    int target = FS_ScriptRootFd(true);
    bool ok = target >= 0;
    if (ok) {
        fs_checkpoint_budget_t budget = {0};
        ok = FS_CheckpointCopyTree(source, target, 0, NULL, &budget, false);
    }
    close(source);
    if (target >= 0) close(target);
    if (!ok) FS_ResetScriptSaveScope();
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

/* Windows lacks a portable reflink API here. Compress checkpoint files
 * opportunistically, retaining ordinary copies when compression is not
 * worthwhile or fails. Restore always expands into a normal writable file. */
static bool FS_CheckpointWinCompress(cstring_t src, cstring_t dst, uint64_t original_size) {
    FILE *input = fopen(src, "rb");
    FILE *header = fopen(dst, "wb");
    bool ok = input && header &&
              fwrite(fs_checkpoint_gzip_magic, 1, sizeof(fs_checkpoint_gzip_magic), header) ==
              sizeof(fs_checkpoint_gzip_magic);
    if (header && fclose(header)) ok = false;
    if (!ok) { if (input) fclose(input); DeleteFileA(dst); return false; }
    gzFile gz = gzopen(dst, "ab1");
    if (!gz) { fclose(input); DeleteFileA(dst); return false; }
    char bytes[65536];
    uint64_t copied = 0;
    while (ok && copied < original_size) {
        size_t want = (size_t)MIN(original_size - copied, (uint64_t)sizeof(bytes));
        size_t n = fread(bytes, 1, want, input);
        if (n == 0 || gzwrite(gz, bytes, (unsigned)n) != (int)n) { ok = false; break; }
        copied += n;
    }
    if (gzclose(gz) != Z_OK) ok = false;
    if (fclose(input)) ok = false;
    if (!ok || copied != original_size) { DeleteFileA(dst); return false; }
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(dst, GetFileExInfoStandard, &data) ||
        data.nFileSizeHigh || data.nFileSizeLow >= original_size) {
        DeleteFileA(dst);
        return false;
    }
    return true;
}

static bool FS_CheckpointWinExpand(cstring_t src, cstring_t dst, uint64_t limit) {
    FILE *input = fopen(src, "rb");
    if (!input) return false;
    uint8_t marker[sizeof(fs_checkpoint_gzip_magic)];
    bool ok = fread(marker, 1, sizeof(marker), input) == sizeof(marker) &&
              !memcmp(marker, fs_checkpoint_gzip_magic, sizeof(marker));
    int gzip_fd = ok ? _dup(_fileno(input)) : -1;
    gzFile gz = gzip_fd >= 0 ? gzdopen(gzip_fd, "rb") : NULL;
    if (!gz) { if (gzip_fd >= 0) _close(gzip_fd); fclose(input); return false; }
    FILE *output = fopen(dst, "wb");
    if (!output) { gzclose(gz); fclose(input); return false; }
    char bytes[65536];
    uint64_t written = 0;
    while (ok) {
        int n = gzread(gz, bytes, sizeof(bytes));
        if (n < 0 || (uint64_t)n > limit - written) { ok = false; break; }
        if (!n) { ok = gzeof(gz) != 0; break; }
        if (fwrite(bytes, 1, (size_t)n, output) != (size_t)n) { ok = false; break; }
        written += (uint64_t)n;
    }
    if (gzclose(gz) != Z_OK) ok = false;
    if (fclose(output)) ok = false;
    fclose(input);
    if (!ok) DeleteFileA(dst);
    return ok;
}

static bool FS_CheckpointWinCopy(cstring_t from, cstring_t to, unsigned depth,
                                  cstring_t top, fs_checkpoint_budget_t *budget, bool snapshot) {
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
            ok = FS_CheckpointWinCopy(src, dst, depth + 1, depth == 0 ? name : top, budget, snapshot);
        } else {
            uint64_t size = ((uint64_t)entry.nFileSizeHigh << 32) | entry.nFileSizeLow;
            if (size > FS_CHECKPOINT_MAX_BYTES - budget->bytes ||
                budget->files >= FS_CHECKPOINT_MAX_FILES) { ok = false; break; }
            bool copied = false;
            if (snapshot) copied = FS_CheckpointWinCompress(src, dst, size);
            else {
                FILE *peek = fopen(src, "rb");
                uint8_t marker[sizeof(fs_checkpoint_gzip_magic)];
                size_t n = peek ? fread(marker, 1, sizeof(marker), peek) : 0;
                if (peek) fclose(peek);
                if (n == sizeof(marker) && !memcmp(marker, fs_checkpoint_gzip_magic, n)) {
                    copied = FS_CheckpointWinExpand(src, dst, FS_CHECKPOINT_MAX_BYTES - budget->bytes);
                    if (!copied) { ok = false; break; }
                }
            }
            if (!copied && !CopyFileA(src, dst, TRUE)) { ok = false; break; }
            if (!snapshot) {
                WIN32_FILE_ATTRIBUTE_DATA restored;
                if (!GetFileAttributesExA(dst, GetFileExInfoStandard, &restored) ||
                    restored.nFileSizeHigh || restored.nFileSizeLow > FS_CHECKPOINT_MAX_BYTES - budget->bytes) {
                    ok = false; break;
                }
                budget->bytes += restored.nFileSizeLow;
            } else budget->bytes += size;
            budget->files++;
        }
    } while (ok && FindNextFileA(listing, &entry));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = false;
    FindClose(listing);
    return ok;
}

static bool FS_CheckpointWinRemove(cstring_t path) {
    char pattern[MAX_PATHLEN];
    if (snprintf(pattern, sizeof(pattern), "%s/*", path) >= (int)sizeof(pattern)) return false;
    WIN32_FIND_DATAA entry;
    HANDLE listing = FindFirstFileA(pattern, &entry);
    if (listing == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    bool ok = true;
    do {
        cstring_t name = entry.cFileName;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { ok = false; break; }
        char child[MAX_PATHLEN];
        if (snprintf(child, sizeof(child), "%s/%s", path, name) >= (int)sizeof(child)) { ok = false; break; }
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!FS_CheckpointWinRemove(child) || !RemoveDirectoryA(child)) { ok = false; break; }
        } else if (!DeleteFileA(child)) { ok = false; break; }
    } while (FindNextFileA(listing, &entry));
    FindClose(listing);
    return ok;
}

static void FS_RemoveCurrentLiveWorkspace(void) {
    PATHSTR branch, live;
    if (!FS_EnsureSaveDirectory() ||
        snprintf(branch, sizeof(branch), "%s/campaigns/%s", fs_save_dir, fs_script_session) >= (int)sizeof(branch) ||
        snprintf(live, sizeof(live), "%s/live", branch) >= (int)sizeof(live)) return;
    DWORD attr = GetFileAttributesA(live);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY) ||
        (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return;
    if (FS_CheckpointWinRemove(live) && RemoveDirectoryA(live)) RemoveDirectoryA(branch);
}

static bool FS_CreateScriptCheckpoint(char id[33]) {
    PATHSTR base, source, target;
    if (!FS_CheckpointWinRoot(true, base) || !FS_ScriptRoot(source, true) ||
        !FS_GenerateScriptScopeId(id) ||
        snprintf(target, sizeof(target), "%s/%s", base, id) >= (int)sizeof(target) ||
        _mkdir(target) != 0) return false;
    fs_checkpoint_budget_t budget = {0};
    return FS_CheckpointWinCopy(source, target, 0, NULL, &budget, true);
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
    /* Windows fallback uses ordinary file copies: never change the old
     * checkpoint, and refuse to reuse a branch directory after a collision. */
    PATHSTR playthrough, campaigns;
    if (snprintf(campaigns, sizeof(campaigns), "%s/campaigns", fs_save_dir) >= (int)sizeof(campaigns) ||
        snprintf(playthrough, sizeof(playthrough), "%s/%s", campaigns, live) >= (int)sizeof(playthrough)) return false;
    _mkdir(campaigns);
    DWORD campaigns_attr = GetFileAttributesA(campaigns);
    if (campaigns_attr == INVALID_FILE_ATTRIBUTES || !(campaigns_attr & FILE_ATTRIBUTE_DIRECTORY) ||
        (campaigns_attr & FILE_ATTRIBUTE_REPARSE_POINT) ||
        GetFileAttributesA(playthrough) != INVALID_FILE_ATTRIBUTES || _mkdir(playthrough) != 0) return false;
    strlcpy(fs_script_session, live, sizeof(fs_script_session));
    bool ok = FS_ScriptRoot(target, true);
    if (ok) {
        fs_checkpoint_budget_t budget = {0};
        ok = FS_CheckpointWinCopy(source, target, 0, NULL, &budget, false);
    }
    if (!ok) FS_ResetScriptSaveScope();
    return ok;
}
#endif
