#include "g_local.h"

#include <errno.h>

#define GAMECACHE_FILE_MAGIC "ORGCACHE"
#define GAMECACHE_FILE_VERSION 1
#define GAMECACHE_FILE_SUFFIX ".orcgc"
#define MAX_GAMECACHE_MEMORY_CACHES 8
#define GAMECACHE_DEAD_HERO_RESTORE_HEALTH_FACTOR 0.25f

typedef enum {
    GAMECACHE_STORAGE_DISABLED,
    GAMECACHE_STORAGE_MEMORY,
    GAMECACHE_STORAGE_DISK,
} gameCacheStorageMode_t;

typedef struct {
    bool inuse;
    gameCache_t cache;
} gameCacheMemorySlot_t;

static gameCacheMemorySlot_t gamecache_memory[MAX_GAMECACHE_MEMORY_CACHES];

typedef union {
    float f;
    uint32_t u;
} gameCacheFloatBits_t;

static gameCacheStorageMode_t G_GameCacheStorageMode(void) {
    cstring_t mode = gi.CvarString("wc3_gamecache_mode", "disk");

    if (!strcmp(mode, "disabled")) return GAMECACHE_STORAGE_DISABLED;
    if (!strcmp(mode, "memory")) return GAMECACHE_STORAGE_MEMORY;
    if (!strcmp(mode, "disk")) return GAMECACHE_STORAGE_DISK;
    fprintf(stderr,
            "Game cache: invalid wc3_gamecache_mode '%s' (expected disabled, memory, or disk); using disk\n",
            mode);
    return GAMECACHE_STORAGE_DISK;
}

static gameCacheMemorySlot_t *G_GameCacheMemoryFind(cstring_t campaign) {
    if (!campaign || !*campaign) return NULL;
    FOR_LOOP(i, MAX_GAMECACHE_MEMORY_CACHES) {
        gameCacheMemorySlot_t *slot = gamecache_memory + i;
        if (slot->inuse && !strcmp(slot->cache.campaign, campaign)) return slot;
    }
    return NULL;
}

static bool G_GameCacheMemoryLoad(gameCache_t *cache) {
    gameCacheMemorySlot_t *slot;

    if (!cache || !cache->campaign[0]) return false;
    slot = G_GameCacheMemoryFind(cache->campaign);
    if (!slot) return false;
    *cache = slot->cache;
    cache->dirty = false;
    return true;
}

static bool G_GameCacheMemorySave(gameCache_t const *cache) {
    gameCacheMemorySlot_t *slot;

    if (!cache || !cache->campaign[0]) return false;
    slot = G_GameCacheMemoryFind(cache->campaign);
    if (!slot) {
        FOR_LOOP(i, MAX_GAMECACHE_MEMORY_CACHES) {
            if (!gamecache_memory[i].inuse) {
                slot = gamecache_memory + i;
                slot->inuse = true;
                break;
            }
        }
    }
    if (!slot) {
        fprintf(stderr, "Game cache: in-memory campaign limit %u reached while saving '%s'\n",
                (unsigned)MAX_GAMECACHE_MEMORY_CACHES, cache->campaign);
        return false;
    }
    slot->cache = *cache;
    slot->cache.dirty = false;
    return true;
}

static bool G_GameCacheKeyFits(cstring_t value, size_t size, cstring_t label) {
    if (!value) {
        fprintf(stderr, "Game cache: missing %s\n", label);
        return false;
    }
    if (strlen(value) >= size) {
        fprintf(stderr, "Game cache: %s is too long (%zu >= %zu)\n",
                label, strlen(value), size);
        return false;
    }
    return true;
}

static bool G_GameCachePath(cstring_t campaign, string_t out, uint32_t out_size) {
    cstring_t base;
    char safe[MAX_PATHLEN];
    char rel[MAX_PATHLEN];
    size_t len;

    if (!campaign || !*campaign || !out || !out_size) {
        return false;
    }
    base = campaign;
    for (cstring_t p = campaign; *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    if (!*base) {
        fprintf(stderr, "Game cache: invalid campaign file '%s'\n", campaign);
        return false;
    }
    len = strlen(base);
    if (len >= sizeof(safe) - 1) {
        fprintf(stderr, "Game cache: campaign file name is too long: '%s'\n", base);
        return false;
    }
    FOR_LOOP(i, len) {
        unsigned char c = (unsigned char)base[i];
        safe[i] = (isalnum(c) || c == '.' || c == '_' || c == '-') ? (char)c : '_';
    }
    safe[len] = '\0';
    if (snprintf(rel, sizeof(rel), "gamecache-%s%s", safe, GAMECACHE_FILE_SUFFIX) >= (int)sizeof(rel)) {
        fprintf(stderr, "Game cache: resolved cache name is too long for '%s'\n", campaign);
        return false;
    }
    gi.UserPath(rel, out, out_size);
    return true;
}

static bool G_GameCacheWriteBytes(FILE *f, void const *data, size_t size) {
    return size == 0 || fwrite(data, 1, size, f) == size;
}

static bool G_GameCacheReadBytes(FILE *f, void *data, size_t size) {
    return size == 0 || fread(data, 1, size, f) == size;
}

static bool G_GameCacheWriteU8(FILE *f, uint8_t value) {
    return G_GameCacheWriteBytes(f, &value, 1);
}

static bool G_GameCacheReadU8(FILE *f, uint8_t *value) {
    return G_GameCacheReadBytes(f, value, 1);
}

static bool G_GameCacheWriteU16(FILE *f, uint16_t value) {
    uint8_t bytes[2] = { (uint8_t)(value & 0xff), (uint8_t)((value >> 8) & 0xff) };
    return G_GameCacheWriteBytes(f, bytes, sizeof(bytes));
}

static bool G_GameCacheReadU16(FILE *f, uint16_t *value) {
    uint8_t bytes[2];
    if (!G_GameCacheReadBytes(f, bytes, sizeof(bytes))) return false;
    *value = (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
    return true;
}

static bool G_GameCacheWriteU32(FILE *f, uint32_t value) {
    uint8_t bytes[4] = {
        (uint8_t)(value & 0xff),
        (uint8_t)((value >> 8) & 0xff),
        (uint8_t)((value >> 16) & 0xff),
        (uint8_t)((value >> 24) & 0xff),
    };
    return G_GameCacheWriteBytes(f, bytes, sizeof(bytes));
}

static bool G_GameCacheReadU32(FILE *f, uint32_t *value) {
    uint8_t bytes[4];
    if (!G_GameCacheReadBytes(f, bytes, sizeof(bytes))) return false;
    *value = (uint32_t)bytes[0] |
             ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) |
             ((uint32_t)bytes[3] << 24);
    return true;
}

static bool G_GameCacheWriteFloat(FILE *f, float value) {
    gameCacheFloatBits_t bits = { .f = value };
    return G_GameCacheWriteU32(f, bits.u);
}

static bool G_GameCacheReadFloat(FILE *f, float *value) {
    gameCacheFloatBits_t bits;
    if (!G_GameCacheReadU32(f, &bits.u)) return false;
    *value = bits.f;
    return true;
}

static bool G_GameCacheWriteString(FILE *f, cstring_t value) {
    size_t len = value ? strlen(value) : 0;
    if (len > 0xffffu) return false;
    return G_GameCacheWriteU16(f, (uint16_t)len) && G_GameCacheWriteBytes(f, value, len);
}

static bool G_GameCacheReadString(FILE *f, string_t value, size_t capacity) {
    uint16_t len;
    if (!G_GameCacheReadU16(f, &len) || len >= capacity) return false;
    if (!G_GameCacheReadBytes(f, value, len)) return false;
    value[len] = '\0';
    return true;
}

static gameCacheEntry_t *G_GameCacheFind(gameCache_t *cache, cstring_t mission, cstring_t key,
                                         gameCacheValueType_t type) {
    if (!cache || !mission || !key) return NULL;
    FOR_LOOP(i, cache->num_entries) {
        gameCacheEntry_t *entry = cache->entries + i;
        if (entry->type == type && !strcmp(entry->mission, mission) && !strcmp(entry->key, key)) {
            return entry;
        }
    }
    return NULL;
}

static gameCacheEntry_t const *G_GameCacheFindConst(gameCache_t const *cache, cstring_t mission, cstring_t key,
                                                    gameCacheValueType_t type) {
    return G_GameCacheFind((gameCache_t *)cache, mission, key, type);
}

static gameCacheEntry_t *G_GameCacheGetOrCreate(gameCache_t *cache, cstring_t mission, cstring_t key,
                                                gameCacheValueType_t type) {
    gameCacheEntry_t *entry;

    if (!cache ||
        !G_GameCacheKeyFits(mission, sizeof(cache->entries[0].mission), "mission key") ||
        !G_GameCacheKeyFits(key, sizeof(cache->entries[0].key), "entry key")) {
        return NULL;
    }
    entry = G_GameCacheFind(cache, mission, key, type);
    if (entry) return entry;
    if (cache->num_entries >= MAX_GAMECACHE_ENTRIES) {
        fprintf(stderr, "Game cache '%s': entry limit %u reached while storing '%s/%s'\n",
                cache->campaign, (unsigned)MAX_GAMECACHE_ENTRIES, mission, key);
        return NULL;
    }
    entry = cache->entries + cache->num_entries++;
    memset(entry, 0, sizeof(*entry));
    strlcpy(entry->mission, mission, sizeof(entry->mission));
    strlcpy(entry->key, key, sizeof(entry->key));
    entry->type = type;
    return entry;
}

static bool G_GameCacheWriteUnit(FILE *f, gameCacheUnit_t const *unit) {
    if (!G_GameCacheWriteU32(f, unit->class_id) ||
        !G_GameCacheWriteU32(f, unit->hero.level) ||
        !G_GameCacheWriteU32(f, unit->hero.str) ||
        !G_GameCacheWriteU32(f, unit->hero.agi) ||
        !G_GameCacheWriteU32(f, unit->hero.intel) ||
        !G_GameCacheWriteU32(f, unit->hero.xp) ||
        !G_GameCacheWriteU32(f, unit->hero.suspend_xp ? 1 : 0) ||
        !G_GameCacheWriteU32(f, unit->hero.skillpoints)) {
        return false;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        if (!G_GameCacheWriteU32(f, unit->abilities[i].code) ||
            !G_GameCacheWriteU32(f, unit->abilities[i].level)) {
            return false;
        }
    }
    if (!G_GameCacheWriteFloat(f, unit->health.value) ||
        !G_GameCacheWriteFloat(f, unit->health.max_value) ||
        !G_GameCacheWriteFloat(f, unit->mana.value) ||
        !G_GameCacheWriteFloat(f, unit->mana.max_value) ||
        !G_GameCacheWriteU32(f, unit->unit_color)) {
        return false;
    }
    FOR_LOOP(i, MAX_INVENTORY) {
        if (!G_GameCacheWriteU32(f, unit->inventory[i].item_id) ||
            !G_GameCacheWriteU32(f, unit->inventory[i].charges)) {
            return false;
        }
    }
    return true;
}

static bool G_GameCacheReadUnit(FILE *f, gameCacheUnit_t *unit) {
    uint32_t suspend_xp;

    memset(unit, 0, sizeof(*unit));
    if (!G_GameCacheReadU32(f, &unit->class_id) ||
        !G_GameCacheReadU32(f, &unit->hero.level) ||
        !G_GameCacheReadU32(f, &unit->hero.str) ||
        !G_GameCacheReadU32(f, &unit->hero.agi) ||
        !G_GameCacheReadU32(f, &unit->hero.intel) ||
        !G_GameCacheReadU32(f, &unit->hero.xp) ||
        !G_GameCacheReadU32(f, &suspend_xp) ||
        !G_GameCacheReadU32(f, &unit->hero.skillpoints)) {
        return false;
    }
    unit->hero.suspend_xp = suspend_xp != 0;
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        if (!G_GameCacheReadU32(f, &unit->abilities[i].code) ||
            !G_GameCacheReadU32(f, &unit->abilities[i].level)) {
            return false;
        }
    }
    if (!G_GameCacheReadFloat(f, &unit->health.value) ||
        !G_GameCacheReadFloat(f, &unit->health.max_value) ||
        !G_GameCacheReadFloat(f, &unit->mana.value) ||
        !G_GameCacheReadFloat(f, &unit->mana.max_value) ||
        !G_GameCacheReadU32(f, &unit->unit_color)) {
        return false;
    }
    FOR_LOOP(i, MAX_INVENTORY) {
        if (!G_GameCacheReadU32(f, &unit->inventory[i].item_id) ||
            !G_GameCacheReadU32(f, &unit->inventory[i].charges)) {
            return false;
        }
    }
    return true;
}

static bool G_GameCacheWriteEntry(FILE *f, gameCacheEntry_t const *entry) {
    if (!G_GameCacheWriteU8(f, (uint8_t)entry->type) ||
        !G_GameCacheWriteString(f, entry->mission) ||
        !G_GameCacheWriteString(f, entry->key)) {
        return false;
    }
    switch (entry->type) {
    case GAMECACHE_INTEGER:
        return G_GameCacheWriteU32(f, (uint32_t)entry->value.integer);
    case GAMECACHE_REAL:
        return G_GameCacheWriteFloat(f, entry->value.real);
    case GAMECACHE_BOOLEAN:
        return G_GameCacheWriteU32(f, entry->value.boolean ? 1 : 0);
    case GAMECACHE_STRING:
        return G_GameCacheWriteString(f, entry->value.string);
    case GAMECACHE_UNIT:
        return G_GameCacheWriteUnit(f, &entry->value.unit);
    default:
        return false;
    }
}

static bool G_GameCacheReadEntry(FILE *f, gameCacheEntry_t *entry) {
    uint8_t type;
    uint32_t value;

    memset(entry, 0, sizeof(*entry));
    if (!G_GameCacheReadU8(f, &type) ||
        type < GAMECACHE_INTEGER || type > GAMECACHE_STRING ||
        !G_GameCacheReadString(f, entry->mission, sizeof(entry->mission)) ||
        !G_GameCacheReadString(f, entry->key, sizeof(entry->key))) {
        return false;
    }
    entry->type = (gameCacheValueType_t)type;
    switch (entry->type) {
    case GAMECACHE_INTEGER:
        if (!G_GameCacheReadU32(f, &value)) return false;
        entry->value.integer = (int32_t)value;
        return true;
    case GAMECACHE_REAL:
        return G_GameCacheReadFloat(f, &entry->value.real);
    case GAMECACHE_BOOLEAN:
        if (!G_GameCacheReadU32(f, &value)) return false;
        entry->value.boolean = value != 0;
        return true;
    case GAMECACHE_STRING:
        return G_GameCacheReadString(f, entry->value.string, sizeof(entry->value.string));
    case GAMECACHE_UNIT:
        return G_GameCacheReadUnit(f, &entry->value.unit);
    default:
        return false;
    }
}

static bool G_GameCacheLoadDisk(gameCache_t *cache) {
    PATHSTR path;
    FILE *f;
    char magic[sizeof(GAMECACHE_FILE_MAGIC) - 1];
    uint32_t version;
    uint32_t count;

    if (!G_GameCachePath(cache->campaign, path, sizeof(path))) return false;
    errno = 0;
    f = fopen(path, "rb");
    if (!f) {
        if (errno != ENOENT) {
            fprintf(stderr, "Game cache '%s': cannot open '%s' for reading: %s\n",
                    cache->campaign, path, strerror(errno));
        }
        return false;
    }
    if (!G_GameCacheReadBytes(f, magic, sizeof(magic)) ||
        memcmp(magic, GAMECACHE_FILE_MAGIC, sizeof(magic)) ||
        !G_GameCacheReadU32(f, &version) || version != GAMECACHE_FILE_VERSION ||
        !G_GameCacheReadU32(f, &count) || count > MAX_GAMECACHE_ENTRIES) {
        fprintf(stderr, "Game cache '%s': invalid or unsupported file '%s'\n", cache->campaign, path);
        fclose(f);
        return false;
    }
    cache->num_entries = 0;
    FOR_LOOP(i, count) {
        gameCacheEntry_t *entry = cache->entries + cache->num_entries;
        if (!G_GameCacheReadEntry(f, entry)) {
            fprintf(stderr, "Game cache '%s': truncated/corrupt entry %u in '%s'\n",
                    cache->campaign, (unsigned)i, path);
            cache->num_entries = 0;
            fclose(f);
            return false;
        }
        cache->num_entries++;
    }
    fclose(f);
    cache->dirty = false;
    return true;
}

void G_GameCacheInit(gameCache_t *cache, cstring_t campaign) {
    gameCacheStorageMode_t mode;

    if (!cache) return;
    memset(cache, 0, sizeof(*cache));
    if (!campaign || !*campaign) {
        fprintf(stderr, "InitGameCache: empty campaign file\n");
        return;
    }
    if (strlen(campaign) >= sizeof(cache->campaign)) {
        fprintf(stderr, "InitGameCache: campaign file is too long (%zu >= %zu)\n",
                strlen(campaign), sizeof(cache->campaign));
        return;
    }
    strlcpy(cache->campaign, campaign, sizeof(cache->campaign));
    mode = G_GameCacheStorageMode();
    if (mode == GAMECACHE_STORAGE_DISABLED) return;
    if (G_GameCacheMemoryLoad(cache)) return;
    if (mode == GAMECACHE_STORAGE_DISK && G_GameCacheLoadDisk(cache)) {
        G_GameCacheMemorySave(cache);
    }
}

static bool G_GameCacheSaveDisk(gameCache_t *cache) {
    PATHSTR path;
    PATHSTR tmp;
    PATHSTR backup;
    FILE *f;
    bool ok = true;
    bool had_backup = false;

    if (!cache || !cache->campaign[0] || !G_GameCachePath(cache->campaign, path, sizeof(path))) {
        return false;
    }
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        fprintf(stderr, "Game cache '%s': temporary path is too long\n", cache->campaign);
        return false;
    }
    f = fopen(tmp, "wb");
    if (!f) {
        fprintf(stderr, "Game cache '%s': cannot open '%s' for writing: %s\n",
                cache->campaign, tmp, strerror(errno));
        return false;
    }
    ok = G_GameCacheWriteBytes(f, GAMECACHE_FILE_MAGIC, sizeof(GAMECACHE_FILE_MAGIC) - 1) &&
         G_GameCacheWriteU32(f, GAMECACHE_FILE_VERSION) &&
         G_GameCacheWriteU32(f, cache->num_entries);
    for (uint32_t i = 0; ok && i < cache->num_entries; i++) {
        ok = G_GameCacheWriteEntry(f, cache->entries + i);
    }
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        fprintf(stderr, "Game cache '%s': failed while writing '%s'\n", cache->campaign, tmp);
        remove(tmp);
        return false;
    }
    if (snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) {
        fprintf(stderr, "Game cache '%s': backup path is too long\n", cache->campaign);
        remove(tmp);
        return false;
    }
    remove(backup);
    errno = 0;
    if (rename(path, backup) == 0) {
        had_backup = true;
    } else if (errno != ENOENT) {
        fprintf(stderr, "Game cache '%s': cannot preserve previous '%s': %s\n",
                cache->campaign, path, strerror(errno));
        remove(tmp);
        return false;
    }
    if (rename(tmp, path) != 0) {
        int const install_errno = errno;
        if (had_backup && rename(backup, path) != 0) {
            fprintf(stderr, "Game cache '%s': failed to restore previous '%s': %s\n",
                    cache->campaign, path, strerror(errno));
        }
        fprintf(stderr, "Game cache '%s': cannot install '%s': %s\n",
                cache->campaign, path, strerror(install_errno));
        remove(tmp);
        return false;
    }
    if (had_backup) remove(backup);
    cache->dirty = false;
    return true;
}

bool G_GameCacheSave(gameCache_t *cache) {
    gameCacheStorageMode_t const mode = G_GameCacheStorageMode();

    if (mode == GAMECACHE_STORAGE_DISABLED) {
        if (!cache || !cache->campaign[0]) return false;
        cache->dirty = false;
        return true;
    }
    if (mode == GAMECACHE_STORAGE_MEMORY) {
        if (!G_GameCacheMemorySave(cache)) return false;
        cache->dirty = false;
        return true;
    }
    if (!G_GameCacheSaveDisk(cache)) return false;
    return G_GameCacheMemorySave(cache);
}

static void G_GameCacheRemoveAt(gameCache_t *cache, uint32_t index) {
    if (!cache || index >= cache->num_entries) return;
    if (index + 1 < cache->num_entries) {
        memmove(cache->entries + index, cache->entries + index + 1,
                (cache->num_entries - index - 1) * sizeof(cache->entries[0]));
    }
    cache->num_entries--;
    memset(cache->entries + cache->num_entries, 0, sizeof(cache->entries[0]));
    cache->dirty = true;
}

void G_GameCacheFlush(gameCache_t *cache) {
    if (!cache) return;
    memset(cache->entries, 0, sizeof(cache->entries));
    cache->num_entries = 0;
    cache->dirty = true;
}

void G_GameCacheFlushMission(gameCache_t *cache, cstring_t mission) {
    if (!cache || !mission) return;
    for (uint32_t i = 0; i < cache->num_entries;) {
        if (!strcmp(cache->entries[i].mission, mission)) {
            G_GameCacheRemoveAt(cache, i);
        } else {
            i++;
        }
    }
}

void G_GameCacheFlushEntry(gameCache_t *cache, cstring_t mission, cstring_t key, gameCacheValueType_t type) {
    if (!cache || !mission || !key) return;
    FOR_LOOP(i, cache->num_entries) {
        gameCacheEntry_t const *entry = cache->entries + i;
        if (entry->type == type && !strcmp(entry->mission, mission) && !strcmp(entry->key, key)) {
            G_GameCacheRemoveAt(cache, i);
            return;
        }
    }
}

bool G_GameCacheStoreInteger(gameCache_t *cache, cstring_t mission, cstring_t key, int32_t value) {
    gameCacheEntry_t *entry = G_GameCacheGetOrCreate(cache, mission, key, GAMECACHE_INTEGER);
    if (!entry) return false;
    entry->value.integer = value;
    cache->dirty = true;
    return true;
}

bool G_GameCacheStoreReal(gameCache_t *cache, cstring_t mission, cstring_t key, float value) {
    gameCacheEntry_t *entry = G_GameCacheGetOrCreate(cache, mission, key, GAMECACHE_REAL);
    if (!entry) return false;
    entry->value.real = value;
    cache->dirty = true;
    return true;
}

bool G_GameCacheStoreBoolean(gameCache_t *cache, cstring_t mission, cstring_t key, bool value) {
    gameCacheEntry_t *entry = G_GameCacheGetOrCreate(cache, mission, key, GAMECACHE_BOOLEAN);
    if (!entry) return false;
    entry->value.boolean = value;
    cache->dirty = true;
    return true;
}

bool G_GameCacheStoreString(gameCache_t *cache, cstring_t mission, cstring_t key, cstring_t value) {
    gameCacheEntry_t *entry;
    if (!value || strlen(value) >= MAX_GAMECACHE_STRING) {
        fprintf(stderr, "Game cache '%s': stored string '%s/%s' is too long or null\n",
                cache ? cache->campaign : "", mission ? mission : "", key ? key : "");
        return false;
    }
    entry = G_GameCacheGetOrCreate(cache, mission, key, GAMECACHE_STRING);
    if (!entry) return false;
    strlcpy(entry->value.string, value, sizeof(entry->value.string));
    cache->dirty = true;
    return true;
}

bool G_GameCacheStoreUnit(gameCache_t *cache, cstring_t mission, cstring_t key, edict_t const *unit) {
    gameCacheEntry_t *entry;
    gameCacheUnit_t *saved;

    if (!unit || !unit->inuse || !unit->class_id) return false;
    entry = G_GameCacheGetOrCreate(cache, mission, key, GAMECACHE_UNIT);
    if (!entry) return false;
    saved = &entry->value.unit;
    memset(saved, 0, sizeof(*saved));
    saved->class_id = unit->class_id;
    saved->hero = unit->hero;
    memcpy(saved->abilities, unit->heroabilities, sizeof(saved->abilities));
    saved->health = unit->health;
    saved->mana = unit->mana;
    saved->unit_color = unit->unit_color;
    FOR_LOOP(i, MAX_INVENTORY) {
        edict_t const *item = unit->inventory[i];
        if (!item) continue;
        saved->inventory[i].item_id = item->class_id;
        saved->inventory[i].charges = item->item.charges;
    }
    cache->dirty = true;
    return true;
}

bool G_GameCacheHave(gameCache_t const *cache, cstring_t mission, cstring_t key, gameCacheValueType_t type) {
    return G_GameCacheFindConst(cache, mission, key, type) != NULL;
}

int32_t G_GameCacheGetInteger(gameCache_t const *cache, cstring_t mission, cstring_t key) {
    gameCacheEntry_t const *entry = G_GameCacheFindConst(cache, mission, key, GAMECACHE_INTEGER);
    return entry ? entry->value.integer : 0;
}

float G_GameCacheGetReal(gameCache_t const *cache, cstring_t mission, cstring_t key) {
    gameCacheEntry_t const *entry = G_GameCacheFindConst(cache, mission, key, GAMECACHE_REAL);
    return entry ? entry->value.real : 0.0f;
}

bool G_GameCacheGetBoolean(gameCache_t const *cache, cstring_t mission, cstring_t key) {
    gameCacheEntry_t const *entry = G_GameCacheFindConst(cache, mission, key, GAMECACHE_BOOLEAN);
    return entry ? entry->value.boolean : false;
}

cstring_t G_GameCacheGetString(gameCache_t const *cache, cstring_t mission, cstring_t key) {
    gameCacheEntry_t const *entry = G_GameCacheFindConst(cache, mission, key, GAMECACHE_STRING);
    return entry ? entry->value.string : "";
}

edict_t *G_GameCacheRestoreUnit(gameCache_t const *cache, cstring_t mission, cstring_t key,
                              uint32_t player, vec2_t const *location, float facing) {
    gameCacheEntry_t const *entry = G_GameCacheFindConst(cache, mission, key, GAMECACHE_UNIT);
    gameCacheUnit_t const *saved;
    edict_t *unit;

    if (!entry || !location) return NULL;
    saved = &entry->value.unit;
    unit = SP_SpawnAtLocation(saved->class_id, player, location);
    if (!unit) return NULL;
    unit->s.angle = DEG2RAD(facing);

    if (saved->hero.level) {
        G_HeroApplyLevel(unit, saved->hero.level);
    }
    unit->hero = saved->hero;
    memcpy(unit->heroabilities, saved->abilities, sizeof(unit->heroabilities));
    G_RecomputeHeroStats(unit);
    unit->health = saved->health;
    /* RestoreUnit creates a fresh living entity; a cached dead Hero must not
     * remain at zero health or Human09 cannot issue its opening movement. */
    if (G_UnitIsHero(unit) && unit->health.value <= 0.0f) {
        unit->health.value =
            unit->health.max_value * GAMECACHE_DEAD_HERO_RESTORE_HEALTH_FACTOR;
    }
    unit->mana = saved->mana;
    unit->unit_color = saved->unit_color;
    {
        uint32_t color;
        if (G_GetUnitColorOverride(unit, &color)) G_SetUnitTeamColor(unit, color);
        else G_InitializeUnitTeamColor(unit);
    }

    FOR_LOOP(i, MAX_INVENTORY) {
        gameCacheItem_t const *saved_item = saved->inventory + i;
        edict_t *item;
        if (!saved_item->item_id) continue;
        item = SP_SpawnAtLocation(saved_item->item_id, player, &unit->s.origin2);
        if (!item) {
            fprintf(stderr, "Game cache '%s': failed to restore item %.4s in slot %u\n",
                    cache->campaign, (cstring_t)&saved_item->item_id, (unsigned)i);
            continue;
        }
        G_SetItemCharges(item, saved_item->charges);
        if (!G_AddItemToSlotInternal(unit, item, i, false)) {
            fprintf(stderr, "Game cache '%s': cannot restore item %.4s into slot %u of %.4s\n",
                    cache->campaign, (cstring_t)&saved_item->item_id, (unsigned)i,
                    (cstring_t)&saved->class_id);
            G_RemoveItem(item);
        }
    }
    return unit;
}
