/* Trusted binding for a player-selected manual save. JASS cannot address this
 * directory: it is a sibling of the randomized script scope, not part of the
 * restricted Blizzard/ namespace. The recorded digest rejects stale bindings
 * after a manual save file has been replaced outside the save lifecycle. The
 * digest is an integrity check, NOT cryptographic file authentication. */
#define FS_SCOPE_BINDING_MAGIC "ORCSCOPE1"
#define FS_SCOPE_BINDING_MAX_FILE (512u * 1024u * 1024u)

static bool FS_ScopeManualName(cstring_t name, PATHSTR leaf) {
    if (!name || !*name || strlen(name) > FS_SCRIPT_COMPONENT_MAX ||
        !FS_ScriptComponent(name, strlen(name))) return false;
    if (snprintf(leaf, MAX_PATHLEN, "%s%s", name,
                 FS_PathHasExtension(name, ".sav") ? "" : ".sav") >= MAX_PATHLEN) return false;
    return true;
}

static bool FS_ScopeManualFingerprint(cstring_t name, uint64_t *size_out, uint64_t *hash_out) {
    PATHSTR leaf;
    if (!FS_ScopeManualName(name, leaf) || !FS_EnsureSaveDirectory()) return false;
#ifndef _WIN32
    int root = open(fs_save_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0) return false;
    int fd = openat(root, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    close(root);
    if (fd < 0) return false;
    struct stat statbuf;
    bool regular = !fstat(fd, &statbuf) && S_ISREG(statbuf.st_mode) &&
                   statbuf.st_size >= 0 && statbuf.st_size <= FS_SCOPE_BINDING_MAX_FILE;
    if (!regular) { close(fd); return false; }
#else
    PATHSTR path;
    if (snprintf(path, sizeof(path), "%s/%s", fs_save_dir, leaf) >= (int)sizeof(path)) return false;
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) return false;
    int fd = _open(path, _O_RDONLY | _O_BINARY);
    if (fd < 0) return false;
#endif
    uint64_t hash = UINT64_C(14695981039346656037);
    uint64_t size = 0;
    char bytes[65536];
    bool ok = true;
    for (;;) {
#ifdef _WIN32
        int count = _read(fd, bytes, sizeof(bytes));
#else
        ssize_t count = read(fd, bytes, sizeof(bytes));
#endif
        if (count < 0) { ok = false; break; }
        if (!count) break;
        size += (uint64_t)count;
        if (size > FS_SCOPE_BINDING_MAX_FILE) { ok = false; break; }
        for (size_t i = 0; i < (size_t)count; ++i) {
            hash ^= (unsigned char)bytes[i];
            hash *= UINT64_C(1099511628211);
        }
    }
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
    if (!ok) return false;
    *size_out = size;
    *hash_out = hash;
    return true;
}

/* Binding storage is opened relative to the save directory, with no-follow
 * handles on POSIX. The script scope itself must also already exist. */
#ifndef _WIN32
static int FS_ScopeBindingDir(bool create) {
    if (!FS_EnsureSaveDirectory()) return -1;
    int root = open(fs_save_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0) return -1;
    if (create && mkdirat(root, "script", 0700) != 0 && errno != EEXIST) { close(root); return -1; }
    int scripts = openat(root, "script", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(root);
    if (scripts < 0) return -1;
    if (create && mkdirat(scripts, "bindings", 0700) != 0 && errno != EEXIST) { close(scripts); return -1; }
    int dir = openat(scripts, "bindings", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(scripts);
    return dir;
}
#else
static bool FS_ScopeBindingDir(bool create, PATHSTR path) {
    if (!FS_EnsureSaveDirectory() ||
        snprintf(path, MAX_PATHLEN, "%s/script", fs_save_dir) >= MAX_PATHLEN) return false;
    if (create) _mkdir(path);
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY) || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    strlcat(path, "/bindings", MAX_PATHLEN);
    if (create) _mkdir(path);
    attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) && !(attr & FILE_ATTRIBUTE_REPARSE_POINT);
}
#endif

/* Binding is established ONLY by engine code after a successful player-save
 * snapshot. No JASS native can select/restore one of these capabilities. */
bool FS_BindScriptSaveScope(cstring_t name) {
    PATHSTR leaf, binding;
    uint64_t size, hash;
    if (!FS_ScopeManualName(name, leaf) || !FS_EnsureScriptSession() ||
        !FS_ScopeManualFingerprint(name, &size, &hash) ||
        snprintf(binding, sizeof(binding), "%s.scope", leaf) >= (int)sizeof(binding)) return false;
    char line[160];
    int length = snprintf(line, sizeof(line), FS_SCOPE_BINDING_MAGIC " %s %016llx %016llx\n",
                          fs_script_session, (unsigned long long)size, (unsigned long long)hash);
    if (length <= 0 || length >= (int)sizeof(line)) return false;
#ifndef _WIN32
    int dir = FS_ScopeBindingDir(true);
    if (dir < 0) return false;
    char tmp[160];
    if (snprintf(tmp, sizeof(tmp), ".%s.%s.tmp", leaf, fs_script_session) >= (int)sizeof(tmp)) {
        close(dir); return false;
    }
    int fd = openat(dir, tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) { close(dir); return false; }
    bool ok = write(fd, line, (size_t)length) == length && fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (ok && renameat(dir, tmp, dir, binding) != 0) ok = false;
    if (ok) fsync(dir);
    if (!ok) unlinkat(dir, tmp, 0);
    close(dir);
    return ok;
#else
    PATHSTR directory, path, tmp;
    if (!FS_ScopeBindingDir(true, directory) ||
        snprintf(path, sizeof(path), "%s/%s", directory, binding) >= (int)sizeof(path) ||
        snprintf(tmp, sizeof(tmp), "%s/%s.%s.tmp", directory, binding, fs_script_session) >= (int)sizeof(tmp)) return false;
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(line, 1, (size_t)length, f) == (size_t)length;
    if (fclose(f) != 0) ok = false;
    if (ok && !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) ok = false;
    if (!ok) remove(tmp);
    return ok;
#endif
}

/* A binding is local to a particular manual-save *contents*, not merely its
 * basename. Refuse a stale or malformed binding without trying to adopt it. */
bool FS_RestoreScriptSaveScope(cstring_t name) {
    PATHSTR leaf, binding;
    char line[160], id[33] = {0}, extra;
    unsigned long long saved_size = 0, saved_hash = 0;
    uint64_t size, hash;
    FS_ResetScriptSaveScope();
    if (!FS_ScopeManualName(name, leaf) ||
        snprintf(binding, sizeof(binding), "%s.scope", leaf) >= (int)sizeof(binding) ||
        !FS_ScopeManualFingerprint(name, &size, &hash)) return false;
#ifndef _WIN32
    int dir = FS_ScopeBindingDir(false);
    if (dir < 0) return false;
    int fd = openat(dir, binding, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    close(dir);
    if (fd < 0) return false;
    ssize_t count = read(fd, line, sizeof(line) - 1);
    close(fd);
    if (count <= 0 || count >= (ssize_t)sizeof(line) - 1) return false;
    line[count] = '\0';
#else
    PATHSTR directory, path;
    if (!FS_ScopeBindingDir(false, directory) ||
        snprintf(path, sizeof(path), "%s/%s", directory, binding) >= (int)sizeof(path)) return false;
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t count = fread(line, 1, sizeof(line) - 1, f);
    bool eof = feof(f) != 0;
    fclose(f);
    if (!count || (count == sizeof(line) - 1 && !eof)) return false;
    line[count] = '\0';
#endif
    if (sscanf(line, FS_SCOPE_BINDING_MAGIC " %32s %llx %llx %c",
               id, &saved_size, &saved_hash, &extra) != 3 ||
        strlen(id) != 32 || saved_size != size || saved_hash != hash) return false;
    for (unsigned i = 0; i < 32; ++i) if (!isxdigit((unsigned char)id[i])) return false;
    /* Adopt only an existing directory; never let a forged binding create a
     * new location or escape the script root through a symlink. */
    strlcpy(fs_script_session, id, sizeof(fs_script_session));
    PATHSTR root;
    if (!FS_ScriptRoot(root, false)) { FS_ResetScriptSaveScope(); return false; }
    return true;
}

/* Trusted game-module code can query this opaque scope for cache invalidation.
 * It must never be registered as a JASS native. */
cstring_t FS_ScriptScopeIdentity(void) {
    return FS_EnsureScriptSession() ? fs_script_session : "";
}

#ifdef BZ_TESTS
TEST(script_saves, manual_save_reauthorizes_only_its_previous_scope) {
    PATHSTR old_home, old_save, manual, zone, other_zone, cache;
    char old_session[sizeof(fs_script_session)], first_scope[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-save-binding"));
    FS_ResetScriptSaveScope();
    T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/OrcX01.w3z", zone, sizeof(zone)));
    FILE *f = fopen(zone, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("zone", 1, 4, f), 4); fclose(f); }
    T_ASSERT(FS_ScriptCachePath("gamecache-Campaigns.w3v.orcgc", cache, sizeof(cache)));
    f = fopen(cache, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("cache", 1, 5, f), 5); fclose(f); }
    strlcpy(first_scope, FS_ScriptScopeIdentity(), sizeof(first_scope));
    FS_SavePath("save-slot", manual, sizeof(manual));
    f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("manual-data", 1, 11, f), 11); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("save-slot"));
    FS_ResetScriptSaveScope();
    T_ASSERT(!FS_ScriptSaveExists("Blizzard/Zones/OrcX01.w3z"));
    T_ASSERT(FS_RestoreScriptSaveScope("save-slot"));
    T_STREQ(FS_ScriptScopeIdentity(), first_scope);
    T_ASSERT(FS_ScriptSaveExists("Blizzard/Zones/OrcX01.w3z"));
    T_ASSERT(FS_ScriptCachePath("gamecache-Campaigns.w3v.orcgc", other_zone, sizeof(other_zone)));
    T_STREQ(cache, other_zone);
    FS_ResetScriptSaveScope();
    T_ASSERT(!FS_RestoreScriptSaveScope("missing-slot"));
    T_ASSERT(!FS_RestoreScriptSaveScope("../save-slot"));
    f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("changed-data", 1, 12, f), 12); fclose(f); }
    T_ASSERT(!FS_RestoreScriptSaveScope("save-slot"));
    T_ASSERT(!FS_ScriptSaveExists("Blizzard/Zones/OrcX01.w3z"));
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}

#ifndef _WIN32
TEST(script_saves, forged_scope_directory_symlink_is_rejected) {
    PATHSTR old_home, old_save, zone, manual, root;
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-binding-symlink"));
    FS_ResetScriptSaveScope();
    T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/zone.w3z", zone, sizeof(zone)));
    FS_SavePath("slot", manual, sizeof(manual));
    FILE *f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("file", 1, 4, f), 4); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("slot"));
    T_ASSERT(FS_ScriptRoot(root, false));
    /* An attacker that can manipulate the host filesystem must not trick
     * scope adoption into following a replaced script-scope directory. */
    char renamed[MAX_PATHLEN];
    if (snprintf(renamed, sizeof(renamed), "%s.old", root) < (int)sizeof(renamed) &&
        rename(root, renamed) == 0) {
        T_EQ(symlink(Test_TempPath("outside-scope"), root), 0);
        FS_ResetScriptSaveScope();
        T_ASSERT(!FS_RestoreScriptSaveScope("slot"));
    }
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}
#endif
#endif /* BZ_TESTS */
