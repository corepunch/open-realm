/* Trusted binding for a player-selected manual save. JASS cannot address this
 * directory: it is a sibling of the randomized script scope, not part of the
 * restricted Blizzard/ namespace. The recorded digest rejects stale bindings
 * after a manual save file has been replaced outside the save lifecycle. The
 * digest is an integrity check, NOT cryptographic file authentication. */
#define FS_SCOPE_BINDING_MAGIC "ORCSCOPE2"
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
    int error = errno;
    close(scripts);
    if (dir < 0) errno = error;
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

#ifndef _WIN32
/* All OpenRealm instances using this save directory cooperate on this
 * advisory lock. A checkpoint cannot be reclaimed while another instance
 * is restoring it through a player-selected save binding. */
static int FS_ScopeBindingLock(bool write_lock) {
    int directory = FS_ScopeBindingDir(true);
    if (directory < 0) return -1;
    int fd = openat(directory, ".storage.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    close(directory);
    if (fd < 0) return -1;
    if (flock(fd, write_lock ? LOCK_EX : LOCK_SH) == 0) return fd;
    close(fd);
    return -1;
}

static bool FS_ScopeCheckpointReferenced(int bindings, cstring_t id) {
    /* dup shares the directory cursor: after one checkpoint lookup subsequent
     * scans could miss live bindings and reclaim them. Open a fresh cursor. */
    int scan = openat(bindings, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (scan < 0) return true;
    DIR *entries = fdopendir(scan);
    if (!entries) { close(scan); return true; }
    struct dirent *entry;
    bool referenced = false;
    while ((entry = readdir(entries)) != NULL) {
        size_t n = strlen(entry->d_name);
        if (n < 7 || strcmp(entry->d_name + n - 6, ".scope")) continue;
        int fd = openat(bindings, entry->d_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) { referenced = true; break; }
        char line[160] = {0}, bound[33] = {0};
        ssize_t count = read(fd, line, sizeof(line) - 1);
        close(fd);
        if (count < 0 || count >= (ssize_t)sizeof(line) - 1) { referenced = true; break; }
        if (sscanf(line, FS_SCOPE_BINDING_MAGIC " %32s", bound) == 1 && !strcmp(bound, id)) {
            referenced = true; break;
        }
    }
    closedir(entries);
    return referenced;
}

/* Conservative opportunistic GC: prune a few checkpoints with no binding,
 * only after a seven-day grace period, never while another OpenRealm process
 * holds the shared restore lock. Unrecognized directories are left alone. */
static void FS_ScopeCollectOrphans(void) {
    enum { MAX_GC_PER_SAVE = 8, GC_GRACE_SECONDS = 7 * 24 * 60 * 60 };
    int checkpoints = FS_CheckpointRootDir(false);
    int bindings = FS_ScopeBindingDir(false);
    if (checkpoints < 0 || bindings < 0) {
        if (checkpoints >= 0) close(checkpoints);
        if (bindings >= 0) close(bindings);
        return;
    }
    int scan = dup(checkpoints);
    DIR *entries = scan >= 0 ? fdopendir(scan) : NULL;
    if (!entries && scan >= 0) close(scan);
    time_t now = time(NULL);
    unsigned removed = 0;
    if (entries && now > GC_GRACE_SECONDS) {
        struct dirent *entry;
        while (removed < MAX_GC_PER_SAVE && (entry = readdir(entries)) != NULL) {
            cstring_t id = entry->d_name;
            if (strlen(id) != 32) continue;
            bool valid = true;
            for (unsigned i = 0; i < 32; i++) if (!isxdigit((unsigned char)id[i])) valid = false;
            if (!valid) continue;
            struct stat info;
            if (fstatat(checkpoints, id, &info, AT_SYMLINK_NOFOLLOW) != 0 ||
                !S_ISDIR(info.st_mode) || info.st_mtime > now - GC_GRACE_SECONDS ||
                FS_ScopeCheckpointReferenced(bindings, id)) continue;
            int checkpoint = openat(checkpoints, id, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (checkpoint < 0) continue;
            bool empty = FS_CheckpointRemoveTree(checkpoint);
            close(checkpoint);
            if (empty && unlinkat(checkpoints, id, AT_REMOVEDIR) == 0) removed++;
        }
    }
    if (entries) closedir(entries);
    close(bindings);
    close(checkpoints);
}
#endif

/* The last save of a slot might have contained campaign storage while the
 * new one does not. Remove its former binding rather than authorizing an old
 * checkpoint for the new ordinary save. If the bindings directory was never
 * created, do not create it or its lock merely to discover that fact. */
static bool FS_UnbindScriptSaveScope(cstring_t name) {
    PATHSTR leaf, binding;
    if (!FS_ScopeManualName(name, leaf) ||
        snprintf(binding, sizeof(binding), "%s.scope", leaf) >= (int)sizeof(binding)) return false;
#ifndef _WIN32
    int existing = FS_ScopeBindingDir(false);
    if (existing < 0) return errno == ENOENT;
    close(existing);
    int lock = FS_ScopeBindingLock(true);
    if (lock < 0) return false;
    int dir = FS_ScopeBindingDir(false);
    bool ok = dir >= 0 && (unlinkat(dir, binding, 0) == 0 || errno == ENOENT);
    if (dir >= 0) close(dir);
    if (ok) FS_ScopeCollectOrphans();
    flock(lock, LOCK_UN);
    close(lock);
    return ok;
#else
    PATHSTR directory, path, script;
    if (!FS_EnsureSaveDirectory() ||
        snprintf(script, sizeof(script), "%s/script", fs_save_dir) >= (int)sizeof(script) ||
        snprintf(directory, sizeof(directory), "%s/bindings", script) >= (int)sizeof(directory)) return false;
    DWORD attr = GetFileAttributesA(directory);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        /* Do not mask a reparse point or access-denied error as no binding. */
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return false;
        /* Missing bindings are normal; existing script parents must still
         * be ordinary directories, not reparse-point redirections. */
        attr = GetFileAttributesA(script);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        }
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
               (attr & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    }
    if (!FS_ScopeBindingDir(false, directory) ||
        snprintf(path, sizeof(path), "%s/%s", directory, binding) >= (int)sizeof(path)) return false;
    attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    return !(attr & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) && DeleteFileA(path) != 0;
#endif
}

/* Binding is established ONLY by engine code after a successful player-save
 * snapshot. No JASS native can select/restore one of these capabilities. */
static bool FS_BindScriptSaveScopeUnlocked(cstring_t name) {
    PATHSTR leaf, binding;
    uint64_t size, hash;
    if (!FS_ScopeManualName(name, leaf) || !FS_EnsureScriptSession() ||
        !FS_ScopeManualFingerprint(name, &size, &hash) ||
        snprintf(binding, sizeof(binding), "%s.scope", leaf) >= (int)sizeof(binding)) return false;
    /* A failed replacement must not leave an earlier binding authorized for
     * an overwritten manual save (even if its payload hashes the same). */
#ifndef _WIN32
    int revoke_dir = FS_ScopeBindingDir(true);
    if (revoke_dir < 0) return false;
    bool revoked = unlinkat(revoke_dir, binding, 0) == 0 || errno == ENOENT;
    close(revoke_dir);
    if (!revoked) return false;
#else
    PATHSTR revoke_dir, revoke_path;
    if (!FS_ScopeBindingDir(true, revoke_dir) ||
        snprintf(revoke_path, sizeof(revoke_path), "%s/%s", revoke_dir, binding) >= (int)sizeof(revoke_path)) return false;
    DWORD revoke_attr = GetFileAttributesA(revoke_path);
    if (revoke_attr != INVALID_FILE_ATTRIBUTES &&
        ((revoke_attr & FILE_ATTRIBUTE_REPARSE_POINT) || !DeleteFileA(revoke_path))) return false;
#endif
    char checkpoint[33];
    if (!FS_CreateScriptCheckpoint(checkpoint)) return false;
    char line[160];
    int length = snprintf(line, sizeof(line), FS_SCOPE_BINDING_MAGIC " %s %016llx %016llx\n",
                          checkpoint, (unsigned long long)size, (unsigned long long)hash);
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

bool FS_BindScriptSaveScope(cstring_t name) {
    /* Run the read-only probe before taking a lock or creating any folders.
     * Ordinary maps with no persistent script files keep only their .sav. */
    int state = FS_ScriptScopeStorageState();
    if (state < 0) return false;
    if (!state) return FS_UnbindScriptSaveScope(name);
#ifndef _WIN32
    int lock = FS_ScopeBindingLock(true);
    if (lock < 0) return false;
    bool success = FS_BindScriptSaveScopeUnlocked(name);
    if (success) FS_ScopeCollectOrphans();
    flock(lock, LOCK_UN);
    close(lock);
    return success;
#else
    return FS_BindScriptSaveScopeUnlocked(name);
#endif
}

/* A binding is local to a particular manual-save *contents*, not merely its
 * basename. Refuse a stale or malformed binding without trying to adopt it. */
static bool FS_RestoreScriptSaveScopeUnlocked(cstring_t name) {
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
    /* Checkpoint IDs are only adopted through an engine-owned binding; restore
     * them into a fresh scope so named saves cannot modify one another. */
    return FS_CloneScriptCheckpoint(id);
}

bool FS_RestoreScriptSaveScope(cstring_t name) {
#ifndef _WIN32
    int bindings = FS_ScopeBindingDir(false);
    if (bindings < 0) { FS_ResetScriptSaveScope(); return false; }
    close(bindings);
    int lock = FS_ScopeBindingLock(false);
    if (lock < 0) { FS_ResetScriptSaveScope(); return false; }
    bool success = FS_RestoreScriptSaveScopeUnlocked(name);
    flock(lock, LOCK_UN);
    close(lock);
    return success;
#else
    PATHSTR bindings;
    if (!FS_ScopeBindingDir(false, bindings)) { FS_ResetScriptSaveScope(); return false; }
    return FS_RestoreScriptSaveScopeUnlocked(name);
#endif
}

/* Trusted game-module code can query this opaque scope for cache invalidation.
 * It must never be registered as a JASS native. */
cstring_t FS_ScriptScopeIdentity(void) {
    return FS_EnsureScriptSession() ? fs_script_session : "";
}

#ifdef BZ_TESTS
TEST(script_saves, ordinary_save_load_and_delete_create_no_campaign_storage) {
    PATHSTR old_home, old_save, manual;
    char script_dir[MAX_PATHLEN * 2], campaign_dir[MAX_PATHLEN * 2];
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-lazy-ordinary"));
    FS_ResetScriptSaveScope();
    FS_SavePath("ordinary", manual, sizeof(manual));
    FILE *f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("ordinary", 1, 8, f), 8); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("ordinary"));
    snprintf(script_dir, sizeof(script_dir), "%s/script", fs_save_dir);
    snprintf(campaign_dir, sizeof(campaign_dir), "%s/campaigns", fs_save_dir);
    T_ASSERT(!FS_DirectoryExists(script_dir));
    T_ASSERT(!FS_DirectoryExists(campaign_dir));
    T_ASSERT(!FS_RestoreScriptSaveScope("ordinary"));
    T_ASSERT(!FS_DirectoryExists(script_dir));
    T_ASSERT(!FS_DirectoryExists(campaign_dir));
    T_ASSERT(FS_DeleteSave("ordinary"));
    T_ASSERT(!FS_DirectoryExists(script_dir));
    T_ASSERT(!FS_DirectoryExists(campaign_dir));
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}

TEST(script_saves, empty_script_directories_do_not_trigger_checkpoints) {
    PATHSTR old_home, old_save, virtual_file, manual;
    char bindings[MAX_PATHLEN * 2];
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-lazy-empty-dirs"));
    FS_ResetScriptSaveScope();
    /* Path resolution may have allocated directories but did not save data. */
    T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/OrcX01.w3z", virtual_file, sizeof(virtual_file)));
    FS_SavePath("empty-path", manual, sizeof(manual));
    FILE *f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("manual", 1, 6, f), 6); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("empty-path"));
    snprintf(bindings, sizeof(bindings), "%s/script/bindings", fs_save_dir);
    T_ASSERT(!FS_DirectoryExists(bindings));
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}

TEST(script_saves, overwriting_campaign_slot_with_plain_save_revokes_checkpoint) {
    PATHSTR old_home, old_save, zone, manual, keep;
    char binding[MAX_PATHLEN * 2];
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-lazy-revoke"));
    FS_ResetScriptSaveScope();
    T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/OrcX01.w3z", zone, sizeof(zone)));
    FILE *f = fopen(zone, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("zone", 1, 4, f), 4); fclose(f); }
    FS_SavePath("reused", manual, sizeof(manual));
    f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("old", 1, 3, f), 3); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("reused"));
    FS_SavePath("independent", keep, sizeof(keep));
    f = fopen(keep, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("keep", 1, 4, f), 4); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("independent"));
    snprintf(binding, sizeof(binding), "%s/script/bindings/reused.sav.scope", fs_save_dir);
    T_ASSERT(FS_FileOnDiskExists(binding));
    FS_ResetScriptSaveScope();
    f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("new", 1, 3, f), 3); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("reused"));
    T_ASSERT(!FS_FileOnDiskExists(binding));
    T_ASSERT(!FS_RestoreScriptSaveScope("reused"));
    T_ASSERT(FS_RestoreScriptSaveScope("independent"));
    T_ASSERT(FS_ScriptSaveExists("Blizzard/Zones/OrcX01.w3z"));
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}

TEST(script_saves, saved_script_file_uses_checkpoint_without_a_zone_transition) {
    PATHSTR old_home, old_save, script_file, manual;
    char binding[MAX_PATHLEN * 2];
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-lazy-flat-file"));
    FS_ResetScriptSaveScope();
    T_ASSERT(FS_ScriptSavePath("script-slot", script_file, sizeof(script_file)));
    FILE *f = fopen(script_file, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("script", 1, 6, f), 6); fclose(f); }
    FS_SavePath("manual", manual, sizeof(manual));
    f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("manual", 1, 6, f), 6); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("manual"));
    snprintf(binding, sizeof(binding), "%s/script/bindings/manual.sav.scope", fs_save_dir);
    T_ASSERT(FS_FileOnDiskExists(binding));
    FS_ResetScriptSaveScope();
    T_ASSERT(FS_RestoreScriptSaveScope("manual"));
    T_ASSERT(FS_ScriptSaveExists("script-slot"));
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}

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
    T_ASSERT(strcmp(FS_ScriptScopeIdentity(), first_scope) != 0);
    T_ASSERT(FS_ScriptSaveExists("Blizzard/Zones/OrcX01.w3z"));
    T_ASSERT(FS_ScriptCachePath("gamecache-Campaigns.w3v.orcgc", other_zone, sizeof(other_zone)));
    T_ASSERT(strcmp(cache, other_zone) != 0);
    f = fopen(other_zone, "rb");
    T_NOT_NULL(f);
    if (f) { char text[6] = {0}; T_EQ(fread(text, 1, 5, f), 5); T_STREQ(text, "cache"); fclose(f); }
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

/* Two named saves from the same live campaign must not observe each other's
 * later disk-cache updates when restored. Loading also branches to a fresh
 * scope so mutations in one loaded slot cannot rewrite either checkpoint. */
TEST(script_saves, older_named_slot_restores_its_own_cache_checkpoint) {
    PATHSTR old_home, old_save, zone, cache, manual_a, manual_b;
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-slot-checkpoints"));
    FS_ResetScriptSaveScope();

    T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/OrcX01.w3z", zone, sizeof(zone)));
    FILE *f = fopen(zone, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("zone-A", 1, 6, f), 6); fclose(f); }
    T_ASSERT(FS_ScriptCachePath("gamecache-Campaigns.w3v.orcgc", cache, sizeof(cache)));
    f = fopen(cache, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("level-3", 1, 7, f), 7); fclose(f); }
    FS_SavePath("slot-A", manual_a, sizeof(manual_a));
    f = fopen(manual_a, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("manual-A", 1, 8, f), 8); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("slot-A"));

    f = fopen(zone, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("zone-B", 1, 6, f), 6); fclose(f); }
    f = fopen(cache, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("level-5", 1, 7, f), 7); fclose(f); }
    FS_SavePath("slot-B", manual_b, sizeof(manual_b));
    f = fopen(manual_b, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("manual-B", 1, 8, f), 8); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("slot-B"));

    FS_ResetScriptSaveScope(); /* simulate an application restart */
    T_ASSERT(FS_RestoreScriptSaveScope("slot-A"));
    T_ASSERT(FS_ScriptCachePath("gamecache-Campaigns.w3v.orcgc", cache, sizeof(cache)));
    f = fopen(cache, "rb");
    T_NOT_NULL(f);
    if (f) { char v[8] = {0}; T_EQ(fread(v, 1, 7, f), 7); T_STREQ(v, "level-3"); fclose(f); }
    T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/OrcX01.w3z", zone, sizeof(zone)));
    f = fopen(zone, "rb");
    T_NOT_NULL(f);
    if (f) { char v[7] = {0}; T_EQ(fread(v, 1, 6, f), 6); T_STREQ(v, "zone-A"); fclose(f); }
    f = fopen(cache, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("level-9", 1, 7, f), 7); fclose(f); }

    T_ASSERT(FS_RestoreScriptSaveScope("slot-B"));
    T_ASSERT(FS_ScriptCachePath("gamecache-Campaigns.w3v.orcgc", cache, sizeof(cache)));
    f = fopen(cache, "rb");
    T_NOT_NULL(f);
    if (f) { char v[8] = {0}; T_EQ(fread(v, 1, 7, f), 7); T_STREQ(v, "level-5"); fclose(f); }
    T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/OrcX01.w3z", zone, sizeof(zone)));
    f = fopen(zone, "rb");
    T_NOT_NULL(f);
    if (f) { char v[7] = {0}; T_EQ(fread(v, 1, 6, f), 6); T_STREQ(v, "zone-B"); fclose(f); }

    T_ASSERT(FS_RestoreScriptSaveScope("slot-A"));
    T_ASSERT(FS_ScriptCachePath("gamecache-Campaigns.w3v.orcgc", cache, sizeof(cache)));
    f = fopen(cache, "rb");
    T_NOT_NULL(f);
    if (f) { char v[8] = {0}; T_EQ(fread(v, 1, 7, f), 7); T_STREQ(v, "level-3"); fclose(f); }

    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}

TEST(script_saves, empty_campaign_scope_can_be_checkpointed) {
    PATHSTR old_home, old_save, manual;
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-empty-checkpoint"));
    FS_ResetScriptSaveScope();
    FS_SavePath("empty", manual, sizeof(manual));
    FILE *f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("empty", 1, 5, f), 5); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("empty"));
    FS_ResetScriptSaveScope();
    T_ASSERT(!FS_RestoreScriptSaveScope("empty"));
    T_ASSERT(!FS_ScriptSaveExists("Blizzard/Zones/OrcX01.w3z"));
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}

TEST(script_saves, leaving_playthrough_reclaims_live_workspace_but_preserves_slot) {
    PATHSTR old_home, old_save, zone, manual, workspace, restored;
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-workspace-cleanup"));
    FS_ResetScriptSaveScope();
    T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/OrcX01.w3z", zone, sizeof(zone)));
    FILE *f = fopen(zone, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("zone", 1, 4, f), 4); fclose(f); }
    T_ASSERT(FS_ScriptRoot(workspace, false));
    T_ASSERT(strstr(workspace, "/campaigns/") != NULL);
    T_ASSERT(strstr(workspace, "/live") != NULL);
    FS_SavePath("slot", manual, sizeof(manual));
    f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("manual", 1, 6, f), 6); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("slot"));
    FS_ResetScriptSaveScope();
#ifdef _WIN32
    T_ASSERT(GetFileAttributesA(workspace) == INVALID_FILE_ATTRIBUTES);
#else
    struct stat st;
    T_ASSERT(stat(workspace, &st) != 0 && errno == ENOENT);
#endif
    T_ASSERT(FS_RestoreScriptSaveScope("slot"));
    T_ASSERT(FS_ScriptRoot(restored, false));
    T_ASSERT(strcmp(workspace, restored) != 0);
    T_ASSERT(FS_ScriptSaveExists("Blizzard/Zones/OrcX01.w3z"));
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}

TEST(script_saves, checkpoint_restores_exact_large_file_after_compression_or_reflink) {
    PATHSTR old_home, old_save, cache, manual;
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-workspace-compression"));
    FS_ResetScriptSaveScope();
    T_ASSERT(FS_ScriptCachePath("gamecache-Campaigns.w3v.orcgc", cache, sizeof(cache)));
    FILE *f = fopen(cache, "wb");
    T_NOT_NULL(f);
    if (f) {
        char bytes[1024]; memset(bytes, 0x5A, sizeof(bytes));
        for (int i = 0; i < 96; i++) T_EQ(fwrite(bytes, 1, sizeof(bytes), f), sizeof(bytes));
        fclose(f);
    }
    FS_SavePath("compressed", manual, sizeof(manual));
    f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("manual", 1, 6, f), 6); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("compressed"));
    FS_ResetScriptSaveScope();
    T_ASSERT(FS_RestoreScriptSaveScope("compressed"));
    T_ASSERT(FS_ScriptCachePath("gamecache-Campaigns.w3v.orcgc", cache, sizeof(cache)));
    f = fopen(cache, "rb");
    T_NOT_NULL(f);
    if (f) {
        char bytes[1024];
        for (int i = 0; i < 96; i++) {
            T_EQ(fread(bytes, 1, sizeof(bytes), f), sizeof(bytes));
            for (size_t n = 0; n < sizeof(bytes); ++n) T_EQ(bytes[n], 0x5A);
        }
        T_EQ(fgetc(f), EOF);
        fclose(f);
    }
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}

#ifndef _WIN32
TEST(script_saves, checkpoint_symlink_is_rejected_on_restore) {
    PATHSTR old_home, old_save, zone, manual;
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-binding-symlink"));
    FS_ResetScriptSaveScope();
    T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/zone.w3z", zone, sizeof(zone)));
    FILE *zone_file = fopen(zone, "wb");
    T_NOT_NULL(zone_file);
    if (zone_file) { T_EQ(fwrite("zone", 1, 4, zone_file), 4); fclose(zone_file); }
    FS_SavePath("slot", manual, sizeof(manual));
    FILE *f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("file", 1, 4, f), 4); fclose(f); }
    T_ASSERT(FS_BindScriptSaveScope("slot"));
    int bindings = FS_ScopeBindingDir(false);
    T_ASSERT(bindings >= 0);
    char contents[160] = {0}, id[33] = {0};
    int binding = openat(bindings, "slot.sav.scope", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    T_ASSERT(binding >= 0);
    if (binding >= 0) {
        ssize_t count = read(binding, contents, sizeof(contents) - 1);
        T_ASSERT(count > 0);
        close(binding);
    }
    if (bindings >= 0) close(bindings);
    T_EQ(sscanf(contents, FS_SCOPE_BINDING_MAGIC " %32s", id), 1);
    int checkpoints = FS_CheckpointRootDir(false);
    T_ASSERT(checkpoints >= 0);
    if (checkpoints >= 0) {
        T_EQ(renameat(checkpoints, id, checkpoints, "replaced-checkpoint"), 0);
        T_EQ(symlinkat(Test_TempPath("outside-checkpoint"), checkpoints, id), 0);
        close(checkpoints);
    }
    FS_ResetScriptSaveScope();
    T_ASSERT(!FS_RestoreScriptSaveScope("slot"));
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}

TEST(script_saves, checkpoint_rejects_symlinks_in_live_cache) {
    PATHSTR old_home, old_save, cache, manual;
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-checkpoint-link"));
    FS_ResetScriptSaveScope();
    T_ASSERT(FS_ScriptCachePath("gamecache-Campaigns.w3v.orcgc", cache, sizeof(cache)));
    T_EQ(symlink(Test_TempPath("outside-cache"), cache), 0);
    FS_SavePath("unsafe", manual, sizeof(manual));
    FILE *f = fopen(manual, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("manual", 1, 6, f), 6); fclose(f); }
    T_ASSERT(!FS_BindScriptSaveScope("unsafe"));
    T_ASSERT(!FS_RestoreScriptSaveScope("unsafe"));
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}

TEST(script_saves, collection_preserves_all_bound_checkpoints_and_removes_orphans) {
    PATHSTR old_home, old_save, manual, zone;
    char old_session[sizeof(fs_script_session)];
    strlcpy(old_home, fs_home_dir, sizeof(old_home));
    strlcpy(old_save, fs_save_dir, sizeof(old_save));
    strlcpy(old_session, fs_script_session, sizeof(old_session));
    FS_SetHomeDirectory(Test_TempPath("script-checkpoint-collection"));
    FS_ResetScriptSaveScope();
    T_ASSERT(FS_ScriptSavePath("Blizzard/Zones/zone.w3z", zone, sizeof(zone)));
    FILE *f = fopen(zone, "wb");
    T_NOT_NULL(f);
    if (f) { T_EQ(fwrite("zone", 1, 4, f), 4); fclose(f); }
    cstring_t names[] = { "bound-a", "bound-b", "orphan" };
    for (unsigned i = 0; i < 3; i++) {
        FS_SavePath(names[i], manual, sizeof(manual));
        f = fopen(manual, "wb");
        T_NOT_NULL(f);
        if (f) { T_EQ(fwrite(names[i], 1, strlen(names[i]), f), strlen(names[i])); fclose(f); }
        T_ASSERT(FS_BindScriptSaveScope(names[i]));
    }
    T_ASSERT(FS_DeleteSave("orphan"));
    int checkpoints = FS_CheckpointRootDir(false);
    T_ASSERT(checkpoints >= 0);
    int scan = checkpoints >= 0 ? dup(checkpoints) : -1;
    DIR *entries = scan >= 0 ? fdopendir(scan) : NULL;
    T_NOT_NULL(entries);
    if (!entries && scan >= 0) close(scan);
    unsigned count = 0;
    struct timespec age[2] = { { time(NULL) - 8 * 24 * 60 * 60, 0 },
                              { time(NULL) - 8 * 24 * 60 * 60, 0 } };
    struct dirent *entry;
    while (entries && (entry = readdir(entries))) {
        if (strlen(entry->d_name) != 32) continue;
        T_EQ(utimensat(checkpoints, entry->d_name, age, AT_SYMLINK_NOFOLLOW), 0);
        count++;
    }
    T_EQ(count, 3);
    if (entries) closedir(entries);
    if (checkpoints >= 0) close(checkpoints);
    FS_ScopeCollectOrphans();
    for (unsigned i = 0; i < 2; i++) {
        T_ASSERT(FS_RestoreScriptSaveScope(names[i]));
        T_ASSERT(FS_ScriptSaveExists("Blizzard/Zones/zone.w3z"));
    }
    checkpoints = FS_CheckpointRootDir(false);
    scan = checkpoints >= 0 ? dup(checkpoints) : -1;
    entries = scan >= 0 ? fdopendir(scan) : NULL;
    T_NOT_NULL(entries);
    if (!entries && scan >= 0) close(scan);
    count = 0;
    while (entries && (entry = readdir(entries)))
        if (strlen(entry->d_name) == 32) count++;
    T_EQ(count, 2);
    if (entries) closedir(entries);
    if (checkpoints >= 0) close(checkpoints);
    FS_ResetScriptSaveScope();
    strlcpy(fs_home_dir, old_home, sizeof(fs_home_dir));
    strlcpy(fs_save_dir, old_save, sizeof(fs_save_dir));
    strlcpy(fs_script_session, old_session, sizeof(fs_script_session));
}
#endif
#endif /* BZ_TESTS */
