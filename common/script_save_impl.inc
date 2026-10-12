/* Engine-owned storage for untrusted Warcraft III save-file natives.
 * Kept in common.c so no game DLL receives direct filesystem privileges.
 * A session nonce is intentionally not exposed to JASS. */
#define FS_SCRIPT_COPY_LIMIT (512u * 1024u * 1024u) // bytes; caps untrusted CopySaveGame source and destination sizes.
#define FS_SCRIPT_COMPONENT_MAX 64 // bytes; bounds each accepted virtual path component.

/* Reset is a trusted playthrough boundary. Live directories have no external
 * references (named saves refer to immutable checkpoints), so the engine can
 * discard the branch it owns; unsuccessful cleanup never blocks a new game. */
static void FS_RemoveCurrentLiveWorkspace(void);
void FS_ResetScriptSaveScope(void) {
    if (fs_script_session[0]) FS_RemoveCurrentLiveWorkspace();
    fs_script_session[0] = '\0';
}

static bool FS_GenerateScriptScopeId(char out[33]) {
    unsigned char id[16];
#ifdef _WIN32
    /* Dynamically load the system RNG to avoid an additional link dependency. */
    HMODULE bcrypt = LoadLibraryA("bcrypt.dll");
    if (!bcrypt) return false;
    typedef LONG (WINAPI *bcryptRandom_t)(void *, unsigned char *, unsigned long, unsigned long);
    bcryptRandom_t random = (bcryptRandom_t)GetProcAddress(bcrypt, "BCryptGenRandom");
    bool success = random && random(NULL, id, (unsigned long)sizeof(id), 2u) >= 0;
    FreeLibrary(bcrypt);
    if (!success) return false;
#else
    FILE *random = fopen("/dev/urandom", "rb");
    if (!random) return false;
    bool success = fread(id, 1, sizeof(id), random) == sizeof(id);
    fclose(random);
    if (!success) return false;
#endif
    for (size_t i = 0; i < sizeof(id); ++i)
        snprintf(out + i * 2, 3, "%02x", id[i]);
    out[32] = '\0';
    return true;
}

static bool FS_EnsureScriptSession(void) {
    return fs_script_session[0] || FS_GenerateScriptScopeId(fs_script_session);
}

/* Only Warcraft's authored 'Blizzard/Zones' and 'Blizzard/<slot>'
 * locations (plus a flat slot) are available to scripts. No parent
 * traversal, drive prefixes, reserved Windows names or arbitrary nesting. */
static bool FS_ScriptComponent(cstring_t name, size_t length) {
    if (!length || length > FS_SCRIPT_COMPONENT_MAX || name[0] == '.' ||
        name[length - 1] == '.' || name[length - 1] == ' ') return false;
    for (size_t i = 0; i < length; ++i) {
        char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == ' ')) return false;
    }
    /* Device names are special even with extensions on Windows. */
    char stem[FS_SCRIPT_COMPONENT_MAX + 1];
    size_t stemlen = 0;
    while (stemlen < length && name[stemlen] != '.') {
        stem[stemlen] = (char)toupper((unsigned char)name[stemlen]);
        stemlen++;
    }
    while (stemlen && stem[stemlen - 1] == ' ') stemlen--;
    stem[stemlen] = '\0';
    if (!strcmp(stem, "CON") || !strcmp(stem, "PRN") || !strcmp(stem, "AUX") || !strcmp(stem, "NUL")) return false;
    if (stemlen == 4 && ((!strncmp(stem, "COM", 3) && stem[3] >= '1' && stem[3] <= '9') ||
                         (!strncmp(stem, "LPT", 3) && stem[3] >= '1' && stem[3] <= '9'))) return false;
    return true;
}

/* Relative directory (depth 0..2) and leaf filename returned separately.
 * Explicitly forbid a root-level directory mutation. */
static bool FS_ParseScriptName(cstring_t name, bool directory, PATHSTR parent, PATHSTR leaf) {
    char parts[3][FS_SCRIPT_COMPONENT_MAX + 1] = {{0}};
    size_t count = 0;
    cstring_t segment;
    if (!name || !*name || strlen(name) > 200) return false;
    size_t length = strlen(name);
    if (directory && length > 0 && (name[length - 1] == '/' || name[length - 1] == '\\')) length--;
    segment = name;
    for (cstring_t p = name;; ++p) {
        if ((size_t)(p - name) < length && *p != '/' && *p != '\\') continue;
        size_t len = (size_t)(p - segment);
        if (count == 3 || !FS_ScriptComponent(segment, len)) return false;
        memcpy(parts[count], segment, len);
        count++;
        if ((size_t)(p - name) == length) break;
        segment = p + 1;
    }
    if (directory) {
        if (count != 2 || strcasecmp(parts[0], "Blizzard")) return false;
        strlcpy(parent, "Blizzard", MAX_PATHLEN);
        strlcpy(leaf, parts[1], MAX_PATHLEN);
        return true;
    }
    if (count != 1 && (count != 3 || strcasecmp(parts[0], "Blizzard"))) return false;
    if (count == 1) parent[0] = '\0';
    else snprintf(parent, MAX_PATHLEN, "Blizzard/%s",
                  !strcasecmp(parts[1], "Zones") ? "Zones" : parts[1]);
    strlcpy(leaf, parts[count - 1], MAX_PATHLEN);
    if (!FS_PathHasExtension(leaf, ".sav")) {
        if (strlen(leaf) + 4 >= MAX_PATHLEN) return false;
        strlcat(leaf, ".sav", MAX_PATHLEN);
    }
    return true;
}

/* A playthrough owns one persistent, engine-selected live workspace. A
 * player-selected load creates a *new branch* of that playthrough from a
 * named-save checkpoint; ordinary cross-map loads keep this workspace. */
#ifndef _WIN32
static int FS_CampaignWorkspacesFd(bool create) {
    if (!FS_EnsureSaveDirectory()) return -1;
    int root = open(fs_save_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0) return -1;
    if (create && mkdirat(root, "campaigns", 0700) && errno != EEXIST) { close(root); return -1; }
    int campaigns = openat(root, "campaigns", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(root);
    return campaigns;
}

static int FS_ScriptRootFd(bool create) {
    if (!FS_EnsureScriptSession()) return -1;
    int campaigns = FS_CampaignWorkspacesFd(create);
    if (campaigns < 0) return -1;
    if (create && mkdirat(campaigns, fs_script_session, 0700) && errno != EEXIST) {
        close(campaigns); return -1;
    }
    int playthrough = openat(campaigns, fs_script_session,
                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(campaigns);
    if (playthrough < 0) return -1;
    if (create && mkdirat(playthrough, "live", 0700) && errno != EEXIST) {
        close(playthrough); return -1;
    }
    int live = openat(playthrough, "live", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(playthrough);
    return live;
}
#endif

static bool FS_ScriptRoot(PATHSTR out, bool create) {
    PATHSTR campaigns, playthrough;
    if (!FS_EnsureSaveDirectory() || !FS_EnsureScriptSession()) return false;
    if (snprintf(campaigns, sizeof(campaigns), "%s/campaigns", fs_save_dir) >= (int)sizeof(campaigns) ||
        snprintf(playthrough, sizeof(playthrough), "%s/%s", campaigns, fs_script_session) >= (int)sizeof(playthrough) ||
        snprintf(out, MAX_PATHLEN, "%s/live", playthrough) >= MAX_PATHLEN) return false;
#ifndef _WIN32
    int fd = FS_ScriptRootFd(create);
    if (fd < 0) return false;
    close(fd);
#else
    if (create) { _mkdir(campaigns); _mkdir(playthrough); _mkdir(out); }
    PATHSTR layers[3];
    strlcpy(layers[0], campaigns, sizeof(PATHSTR));
    strlcpy(layers[1], playthrough, sizeof(PATHSTR));
    strlcpy(layers[2], out, sizeof(PATHSTR));
    for (unsigned i = 0; i < 3; ++i) {
        DWORD attributes = GetFileAttributesA(layers[i]);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
#endif
    return true;
}

#ifndef _WIN32
static int FS_ScriptDirectory(cstring_t parent, bool create) {
    PATHSTR copy;
    int fd = FS_ScriptRootFd(create);
    if (fd < 0) return -1;
    if (!parent || !*parent) return fd;
    strlcpy(copy, parent, sizeof(copy));
    char *segment = copy;
    for (;;) {
        char *slash = strchr(segment, '/');
        if (slash) *slash = '\0';
        if (create && mkdirat(fd, segment, 0700) != 0 && errno != EEXIST) { close(fd); return -1; }
        int next = openat(fd, segment, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(fd);
        if (next < 0) return -1;
        fd = next;
        if (!slash) break;
        segment = slash + 1;
    }
    return fd;
}
#else
/* On Windows, reject existing reparse points in every component before
 * using ordinary Win32 file APIs. Maps cannot create reparse points through
 * this interface; concurrent privileged filesystem attackers are outside
 * the JASS threat model. */
static bool FS_ScriptWindowsDirectory(cstring_t parent, bool create, PATHSTR out) {
    PATHSTR root, walk;
    if (!FS_ScriptRoot(root, create)) return false;
    strlcpy(walk, root, sizeof(walk));
    if (parent && *parent) {
        char copy[MAX_PATHLEN];
        strlcpy(copy, parent, sizeof(copy));
        char *segment = copy;
        for (;;) {
            char *slash = strchr(segment, '/');
            if (slash) *slash = '\0';
            if (strlen(walk) + strlen(segment) + 2 >= sizeof(walk)) return false;
            strlcat(walk, "/", sizeof(walk));
            strlcat(walk, segment, sizeof(walk));
            if (create) _mkdir(walk);
            DWORD attr = GetFileAttributesA(walk);
            if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY) ||
                (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
            if (!slash) break;
            segment = slash + 1;
        }
    }
    strlcpy(out, walk, MAX_PATHLEN);
    return true;
}
#endif

bool FS_ScriptSavePath(cstring_t name, string_t out, uint32_t out_size) {
    PATHSTR parent, leaf, root;
    if (!out || !out_size) return false;
    out[0] = '\0';
    if (!FS_ParseScriptName(name, false, parent, leaf)) return false;
#ifndef _WIN32
    int dir = FS_ScriptDirectory(parent, true);
    if (dir < 0) return false;
    struct stat st;
    bool safe = fstatat(dir, leaf, &st, AT_SYMLINK_NOFOLLOW) != 0 ? errno == ENOENT : S_ISREG(st.st_mode);
    close(dir);
    if (!safe) return false;
#else
    PATHSTR existing;
    if (!FS_ScriptWindowsDirectory(parent, true, existing)) return false;
    char path[MAX_PATHLEN];
    snprintf(path, sizeof(path), "%s/%s", existing, leaf);
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && ((attr & FILE_ATTRIBUTE_REPARSE_POINT) ||
                                             (attr & FILE_ATTRIBUTE_DIRECTORY))) return false;
#endif
    if (!FS_ScriptRoot(root, false)) return false;
    int len = snprintf(out, out_size, "%s/%s%s%s", root, parent, parent[0] ? "/" : "", leaf);
    if (len < 0 || (uint32_t)len >= out_size) { out[0] = '\0'; return false; }
    return true;
}

/* Game-cache sidecars share the session capability but live outside the
 * JASS save-file namespace, so CopySaveGame/RemoveSaveDirectory cannot touch them. */
bool FS_ScriptCachePath(cstring_t name, string_t out, uint32_t out_size) {
    PATHSTR root;
    if (!out || !out_size) return false;
    out[0] = '\0';
    if (!name || !FS_ScriptComponent(name, strlen(name))) return false;
#ifndef _WIN32
    int dir = FS_ScriptDirectory("cache", true);
    if (dir < 0) return false;
    struct stat st;
    bool safe = fstatat(dir, name, &st, AT_SYMLINK_NOFOLLOW) != 0 ? errno == ENOENT : S_ISREG(st.st_mode);
    close(dir);
    if (!safe) return false;
#else
    PATHSTR directory, file;
    if (!FS_ScriptWindowsDirectory("cache", true, directory)) return false;
    if (snprintf(file, sizeof(file), "%s/%s", directory, name) >= (int)sizeof(file)) return false;
    DWORD attr = GetFileAttributesA(file);
    if (attr != INVALID_FILE_ATTRIBUTES && ((attr & FILE_ATTRIBUTE_REPARSE_POINT) ||
                                             (attr & FILE_ATTRIBUTE_DIRECTORY))) return false;
#endif
    if (!FS_ScriptRoot(root, false)) return false;
    int len = snprintf(out, out_size, "%s/cache/%s", root, name);
    if (len < 0 || (uint32_t)len >= out_size) { out[0] = '\0'; return false; }
    return true;
}

bool FS_ScriptSaveExists(cstring_t name) {
    PATHSTR parent, leaf;
    if (!FS_ParseScriptName(name, false, parent, leaf)) return false;
#ifndef _WIN32
    int dir = FS_ScriptDirectory(parent, false);
    if (dir < 0) return false;
    int fd = openat(dir, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    close(dir);
    if (fd < 0) return false;
    struct stat st;
    bool valid = fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
    close(fd);
    return valid;
#else
    PATHSTR root, file;
    if (!FS_ScriptWindowsDirectory(parent, false, root)) return false;
    snprintf(file, sizeof(file), "%s/%s", root, leaf);
    DWORD attr = GetFileAttributesA(file);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
#endif
}

bool FS_ScriptCopySave(cstring_t source, cstring_t target) {
    PATHSTR fromParent, fromLeaf, toParent, toLeaf;
    if (!FS_ParseScriptName(source, false, fromParent, fromLeaf) ||
        !FS_ParseScriptName(target, false, toParent, toLeaf)) return false;
    if (!strcmp(fromParent, toParent) && !strcmp(fromLeaf, toLeaf)) return FS_ScriptSaveExists(source);
#ifndef _WIN32
    int srcDir = FS_ScriptDirectory(fromParent, false);
    if (srcDir < 0) return false;
    int src = openat(srcDir, fromLeaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    close(srcDir);
    if (src < 0) return false;
    struct stat st;
    if (fstat(src, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > FS_SCRIPT_COPY_LIMIT) {
        close(src); return false;
    }
    int dstDir = FS_ScriptDirectory(toParent, true);
    if (dstDir < 0) { close(src); return false; }
    /* No temp-name collisions or predictable maps-controlled temp files. */
    char tmp[80];
    int output = -1;
    for (unsigned i = 0; i < 32 && output < 0; ++i) {
        unsigned char token[8];
        FILE *random = fopen("/dev/urandom", "rb");
        if (!random) break;
        bool valid = fread(token, 1, sizeof(token), random) == sizeof(token);
        fclose(random);
        if (!valid) break;
        snprintf(tmp, sizeof(tmp), ".copy-%02x%02x%02x%02x%02x%02x%02x%02x",
                 token[0], token[1], token[2], token[3], token[4], token[5], token[6], token[7]);
        output = openat(dstDir, tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (output < 0 && errno != EEXIST) break;
    }
    if (output < 0) { close(dstDir); close(src); return false; }
    bool ok = true;
    char buffer[65536];
    ssize_t count;
    size_t total = 0;
    while ((count = read(src, buffer, sizeof(buffer))) > 0) {
        total += (size_t)count;
        if (total > FS_SCRIPT_COPY_LIMIT) { ok = false; break; }
        ssize_t offset = 0;
        while (offset < count) {
            ssize_t written = write(output, buffer + offset, (size_t)(count - offset));
            if (written <= 0) { ok = false; break; }
            offset += written;
        }
        if (!ok) break;
    }
    if (count < 0 || fsync(output) != 0) ok = false;
    if (close(output) != 0) ok = false;
    close(src);
    struct stat old;
    int dstState = fstatat(dstDir, toLeaf, &old, AT_SYMLINK_NOFOLLOW);
    if ((dstState == 0 && !S_ISREG(old.st_mode)) ||
        (dstState != 0 && errno != ENOENT)) ok = false;
    if (ok && renameat(dstDir, tmp, dstDir, toLeaf) != 0) ok = false;
    if (ok) fsync(dstDir);
    if (!ok) unlinkat(dstDir, tmp, 0);
    close(dstDir);
    return ok;
#else
    PATHSTR from, to;
    if (!FS_ScriptSavePath(source, from, sizeof(from)) ||
        !FS_ScriptSavePath(target, to, sizeof(to)) || !FS_ScriptSaveExists(source)) return false;
    /* Copy to a temporary file then replace, rather than destroying a valid
     * destination on a failed copy. */
    WIN32_FILE_ATTRIBUTE_DATA sourceInfo;
    if (!GetFileAttributesExA(from, GetFileExInfoStandard, &sourceInfo) ||
        sourceInfo.nFileSizeHigh || sourceInfo.nFileSizeLow > FS_SCRIPT_COPY_LIMIT) return false;
    PATHSTR dstDir, temp;
    strlcpy(dstDir, to, sizeof(dstDir));
    char *slash = strrchr(dstDir, '/');
    if (!slash) return false;
    *slash = '\0';
    if (!GetTempFileNameA(dstDir, "w3s", 0, temp)) return false;
    bool ok = CopyFileA(from, temp, FALSE) != 0;
    DWORD current = GetFileAttributesA(to);
    if (current != INVALID_FILE_ATTRIBUTES &&
        ((current & FILE_ATTRIBUTE_REPARSE_POINT) || (current & FILE_ATTRIBUTE_DIRECTORY))) ok = false;
    if (ok) ok = MoveFileExA(temp, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileA(temp);
    return ok;
#endif
}

bool FS_ScriptRenameDirectory(cstring_t source, cstring_t target) {
    PATHSTR aParent, aLeaf, bParent, bLeaf;
    if (!FS_ParseScriptName(source, true, aParent, aLeaf) ||
        !FS_ParseScriptName(target, true, bParent, bLeaf) ||
        !strcasecmp(aLeaf, bLeaf)) return false;
#ifndef _WIN32
    int dir = FS_ScriptDirectory("Blizzard", false);
    if (dir < 0) return false;
    int old = openat(dir, aLeaf, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (old < 0) { close(dir); return false; }
    close(old);
    struct stat st;
    bool safe = fstatat(dir, bLeaf, &st, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
    bool ok = safe && renameat(dir, aLeaf, dir, bLeaf) == 0;
    close(dir);
    return ok;
#else
    PATHSTR dir, from, to;
    if (!FS_ScriptWindowsDirectory("Blizzard", false, dir)) return false;
    snprintf(from, sizeof(from), "%s/%s", dir, aLeaf);
    snprintf(to, sizeof(to), "%s/%s", dir, bLeaf);
    DWORD attr = GetFileAttributesA(from);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY) ||
        (attr & FILE_ATTRIBUTE_REPARSE_POINT) || GetFileAttributesA(to) != INVALID_FILE_ATTRIBUTES) return false;
    return MoveFileA(from, to) != 0;
#endif
}

bool FS_ScriptRemoveDirectory(cstring_t name) {
    PATHSTR parent, leaf;
    if (!FS_ParseScriptName(name, true, parent, leaf)) return false;
#ifndef _WIN32
    int dir = FS_ScriptDirectory("Blizzard", false);
    if (dir < 0) return false;
    int child = openat(dir, leaf, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (child < 0) { close(dir); return false; }
    DIR *entries = fdopendir(child);
    if (!entries) { close(child); close(dir); return false; }
    /* First pass: never delete a nested directory. All owned zone files
     * are regular files; anything else fails closed. */
    bool valid = true;
    struct dirent *entry;
    while ((entry = readdir(entries)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        struct stat st;
        if (fstatat(child, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode)) {
            valid = false; break;
        }
    }
    if (valid) {
        rewinddir(entries);
        while ((entry = readdir(entries)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            if (unlinkat(child, entry->d_name, 0) != 0) { valid = false; break; }
        }
    }
    closedir(entries); /* closes child */
    if (valid && unlinkat(dir, leaf, AT_REMOVEDIR) != 0) valid = false;
    close(dir);
    return valid;
#else
    PATHSTR dir, path, mask;
    if (!FS_ScriptWindowsDirectory("Blizzard", false, dir)) return false;
    if (snprintf(path, sizeof(path), "%s/%s", dir, leaf) >= (int)sizeof(path)) return false;
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY) ||
        (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    if (snprintf(mask, sizeof(mask), "%s/*", path) >= (int)sizeof(mask)) return false;
    WIN32_FIND_DATAA found;
    HANDLE listing = FindFirstFileA(mask, &found);
    if (listing != INVALID_HANDLE_VALUE) {
        bool safe = true;
        do {
            if (!strcmp(found.cFileName, ".") || !strcmp(found.cFileName, "..")) continue;
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { safe = false; break; }
        } while (FindNextFileA(listing, &found));
        FindClose(listing);
        if (!safe) return false;
        listing = FindFirstFileA(mask, &found);
        if (listing == INVALID_HANDLE_VALUE) return false;
        do {
            if (!strcmp(found.cFileName, ".") || !strcmp(found.cFileName, "..")) continue;
            char child[MAX_PATHLEN];
            if (snprintf(child, sizeof(child), "%s/%s", path, found.cFileName) >= (int)sizeof(child) ||
                !DeleteFileA(child)) { FindClose(listing); return false; }
        } while (FindNextFileA(listing, &found));
        FindClose(listing);
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) return false;
    return RemoveDirectoryA(path) != 0;
#endif
}

#ifdef BZ_TESTS
TEST(script_saves, accepts_only_explicit_virtual_paths) {
    PATHSTR parent, leaf;
    T_ASSERT(FS_ParseScriptName("Blizzard\\Zones\\OrcX01.w3z", false, parent, leaf));
    T_STREQ(parent, "Blizzard/Zones");
    T_STREQ(leaf, "OrcX01.w3z.sav");
    T_ASSERT(FS_ParseScriptName("Blizzard/MySlot/OrcX01.w3z", false, parent, leaf));
    T_STREQ(parent, "Blizzard/MySlot");
    T_ASSERT(FS_ParseScriptName("Blizzard/My Save/OrcX01.w3z", false, parent, leaf));
    T_STREQ(parent, "Blizzard/My Save");
    T_ASSERT(FS_ParseScriptName("normal", false, parent, leaf));
    T_STREQ(parent, "");
    T_ASSERT(FS_ParseScriptName("Blizzard/Zones", true, parent, leaf));
    T_ASSERT(FS_ParseScriptName("Blizzard\\Zones\\", true, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("cache/Campaigns.w3v", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Blizzard", true, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Zones", true, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("/etc/passwd", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("C:\\Windows\\win.ini", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("..\\outside", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Blizzard\\..\\outside", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Blizzard\\Zones\\..\\outside", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Blizzard/Zones/sub/file", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Blizzard//Zones/file", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Other/Zones/file", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Blizzard/Zones/CON.w3z", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Blizzard/Zones/COM1.w3z", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Blizzard/Zones/trail.", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Blizzard/Zones/trail ", false, parent, leaf));
    T_ASSERT(!FS_ParseScriptName("Blizzard/Zones/CON .w3z", false, parent, leaf));
}

TEST(script_saves, isolates_manual_saves_and_copies_zone_snapshots) {
    PATHSTR oldHome, oldSave, path, manual, copyPath, cachePath;
    char oldSession[sizeof(fs_script_session)];
    strlcpy(oldHome, fs_home_dir, sizeof(oldHome));
    strlcpy(oldSave, fs_save_dir, sizeof(oldSave));
    strlcpy(oldSession, fs_script_session, sizeof(oldSession));

    FS_SetHomeDirectory(Test_TempPath("script-saves-root"));
    FS_ResetScriptSaveScope();
    bool ok = FS_ScriptSavePath("Blizzard\\Zones\\OrcX01.w3z", path, sizeof(path));
    T_ASSERT(ok);
    if (ok) {
        FILE *f = fopen(path, "wb");
        T_NOT_NULL(f);
        if (f) {
            T_EQ(fwrite("zone-data", 1, 9, f), 9);
            fclose(f);
        }
        T_ASSERT(FS_ScriptSaveExists("Blizzard/Zones/OrcX01.w3z"));
        T_ASSERT(FS_ScriptCopySave("Blizzard\\Zones\\OrcX01.w3z",
                                   "Blizzard\\MySave\\OrcX01.w3z"));
        T_ASSERT(FS_ScriptSaveExists("Blizzard/MySave/OrcX01.w3z"));
        T_ASSERT(FS_ScriptSavePath("Blizzard/MySave/OrcX01.w3z", copyPath, sizeof(copyPath)));
        T_ASSERT(!FS_ScriptCopySave("Blizzard/Zones/no-such-save", "Blizzard/MySave/OrcX01.w3z"));
        FILE *copy = fopen(copyPath, "rb");
        T_NOT_NULL(copy);
        if (copy) {
            char bytes[10] = {0};
            T_EQ(fread(bytes, 1, 9, copy), 9);
            T_STREQ(bytes, "zone-data");
            fclose(copy);
        }
        T_ASSERT(FS_ScriptCachePath("gamecache-Campaigns.w3v.orcgc", cachePath, sizeof(cachePath)));
        T_ASSERT(strstr(cachePath, "/cache/") != NULL);
        T_ASSERT(strcmp(cachePath, copyPath) != 0);
        T_ASSERT(!FS_ScriptCopySave("cache/gamecache-Campaigns.w3v.orcgc", "Blizzard/Zones/copied"));
        FS_SavePath("manual", manual, sizeof(manual));
        T_ASSERT(strcmp(manual, path) != 0);
        T_ASSERT(strstr(path, "/campaigns/") != NULL);
        T_ASSERT(strstr(path, "/live/") != NULL);
        T_ASSERT(!FS_ScriptRemoveDirectory("Blizzard"));
        T_ASSERT(!FS_ScriptRenameDirectory("Blizzard", "Blizzard/Foo"));
        T_ASSERT(FS_ScriptRenameDirectory("Blizzard/MySave", "Blizzard/Archived"));
        T_ASSERT(!FS_ScriptSaveExists("Blizzard/MySave/OrcX01.w3z"));
        T_ASSERT(FS_ScriptSaveExists("Blizzard/Archived/OrcX01.w3z"));
        T_ASSERT(FS_ScriptRemoveDirectory("Blizzard/Archived"));
        T_ASSERT(!FS_ScriptSaveExists("Blizzard/Archived/OrcX01.w3z"));
        T_ASSERT(FS_ScriptRemoveDirectory("Blizzard/Zones"));
        T_ASSERT(!FS_ScriptSaveExists("Blizzard/Zones/OrcX01.w3z"));
    }
    strlcpy(fs_home_dir, oldHome, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, oldSave, sizeof(fs_save_dir));
    strlcpy(fs_script_session, oldSession, sizeof(fs_script_session));
}

TEST(script_saves, new_session_cannot_access_previous_session) {
    PATHSTR oldHome, oldSave, oldPath, newPath;
    char oldSession[sizeof(fs_script_session)];
    strlcpy(oldHome, fs_home_dir, sizeof(oldHome));
    strlcpy(oldSave, fs_save_dir, sizeof(oldSave));
    strlcpy(oldSession, fs_script_session, sizeof(oldSession));
    FS_SetHomeDirectory(Test_TempPath("script-saves-isolation"));
    FS_ResetScriptSaveScope();
    bool ok = FS_ScriptSavePath("Blizzard/Zones/a.w3z", oldPath, sizeof(oldPath));
    T_ASSERT(ok);
    if (ok) {
        FILE *f = fopen(oldPath, "wb");
        T_NOT_NULL(f);
        if (f) fclose(f);
        FS_ResetScriptSaveScope();
        T_ASSERT(!FS_ScriptSaveExists("Blizzard/Zones/a.w3z"));
        T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/a.w3z", newPath, sizeof(newPath)));
        T_ASSERT(strcmp(oldPath, newPath) != 0);
    }
    strlcpy(fs_home_dir, oldHome, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, oldSave, sizeof(fs_save_dir));
    strlcpy(fs_script_session, oldSession, sizeof(fs_script_session));
}

#ifndef _WIN32
TEST(script_saves, rejects_symlink_targets_and_parent_directories) {
    PATHSTR oldHome, oldSave, path, linkPath, parent;
    char oldSession[sizeof(fs_script_session)];
    strlcpy(oldHome, fs_home_dir, sizeof(oldHome));
    strlcpy(oldSave, fs_save_dir, sizeof(oldSave));
    strlcpy(oldSession, fs_script_session, sizeof(oldSession));
    FS_SetHomeDirectory(Test_TempPath("script-saves-symlinks"));
    FS_ResetScriptSaveScope();
    bool ok = FS_ScriptSavePath("Blizzard/Zones/good.w3z", path, sizeof(path));
    T_ASSERT(ok);
    if (ok) {
        char *suffix = strstr(path, "/Zones/good.w3z.sav");
        T_NOT_NULL(suffix);
        if (suffix) {
            size_t prefix = (size_t)(suffix - path);
            memcpy(parent, path, prefix);
            parent[prefix] = '\0';
            T_ASSERT(strlen(parent) + sizeof("/Evil") <= sizeof(linkPath));
            strlcpy(linkPath, parent, sizeof(linkPath));
            strlcat(linkPath, "/Evil", sizeof(linkPath));
            T_EQ(symlink(Test_TempPath("outside"), linkPath), 0);
            T_ASSERT(!FS_ScriptSavePath("Blizzard/Evil/malicious.w3z", path, sizeof(path)));
        }
        T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/symlink.w3z", linkPath, sizeof(linkPath)));
        T_EQ(symlink(Test_TempPath("outside-file"), linkPath), 0);
        T_ASSERT(!FS_ScriptSaveExists("Blizzard/Zones/symlink.w3z"));
        T_ASSERT(!FS_ScriptSavePath("Blizzard/Zones/symlink.w3z", path, sizeof(path)));
        T_ASSERT(!FS_ScriptCopySave("Blizzard/Zones/symlink.w3z", "Blizzard/Zones/copy.w3z"));
    }
    strlcpy(fs_home_dir, oldHome, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, oldSave, sizeof(fs_save_dir));
    strlcpy(fs_script_session, oldSession, sizeof(fs_script_session));
}
#endif
#endif /* BZ_TESTS */
