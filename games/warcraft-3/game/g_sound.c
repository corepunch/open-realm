#include "g_local.h"
#include "g_unitrow.h"

typedef struct jassSoundRuntime_s {
    struct jassSoundRuntime_s *next;
    HANDLE handle;
    FLOAT volume;
    VECTOR3 position;
    LONG attached_entity;
    DWORD attached_spawn_time;
    BOOL has_position;
} jassSoundRuntime_t;

static jassSoundRuntime_t *jass_sound_runtime;

typedef struct {
    LPCSTR text;
    LPCSTR key;
} commandErrorText_t;

static commandErrorText_t const command_error_texts[] = {
    { "Not enough food", "Nofood" },
    { "Not enough gold", "Nogold" },
    { "Not enough lumber", "Nolumber" },
    { "Not enough mana", "Nomana" },
    { "Spell is not ready yet", "Cooldown" },
    { "Unable to build there", "Cantplace" },
    { "Unable to build so close to the gold mine", "Tooclosetomine" },
    { "Inventory is full", "Inventoryfull" },
};

static jassSoundRuntime_t *G_FindJassSoundRuntime(HANDLE handle, BOOL create) {
    jassSoundRuntime_t *state;

    if (!handle) return NULL;
    FOR_EACH_LIST(jassSoundRuntime_t, item, jass_sound_runtime)
        if (item->handle == handle) return item;
    if (!create || !gi.MemAlloc) return NULL;

    state = gi.MemAlloc(sizeof(*state));
    if (!state) return NULL;
    memset(state, 0, sizeof(*state));
    state->handle = handle;
    state->volume = 1.0f;
    state->attached_entity = -1;
    ADD_TO_LIST(state, jass_sound_runtime);
    return state;
}

void G_JassSoundRuntimeReset(void) {
    while (jass_sound_runtime) {
        jassSoundRuntime_t *state = jass_sound_runtime;
        jass_sound_runtime = state->next;
        if (gi.MemFree) gi.MemFree(state);
    }
}

void G_JassSoundRuntimeInit(HANDLE handle) {
    jassSoundRuntime_t *state = G_FindJassSoundRuntime(handle, true);
    if (!state) return;
    state->volume = 1.0f;
    state->position = (VECTOR3){ 0 };
    state->attached_entity = -1;
    state->attached_spawn_time = 0;
    state->has_position = false;
}

void G_JassSoundSetVolume(HANDLE handle, FLOAT volume) {
    jassSoundRuntime_t *state = G_FindJassSoundRuntime(handle, true);
    if (state) state->volume = MAX(0.0f, MIN(volume, 1.0f));
}

void G_JassSoundSetPosition(HANDLE handle, LPCVECTOR3 position) {
    jassSoundRuntime_t *state = G_FindJassSoundRuntime(handle, true);
    if (!state || !position) return;
    state->position = *position;
    state->attached_entity = -1;
    state->attached_spawn_time = 0;
    state->has_position = true;
}

void G_JassSoundAttach(HANDLE handle, LPEDICT unit) {
    jassSoundRuntime_t *state = G_FindJassSoundRuntime(handle, true);
    if (!state) return;
    state->attached_entity = unit ? (LONG)unit->s.number : -1;
    state->attached_spawn_time = unit ? unit->spawn_time : 0;
    state->has_position = false;
}

void G_JassSoundPlayback(HANDLE handle, jassSoundPlayback_t *playback) {
    jassSoundRuntime_t *state;

    if (!playback) return;
    *playback = (jassSoundPlayback_t){ .volume = 1.0f };
    state = G_FindJassSoundRuntime(handle, false);
    if (!state) return;
    playback->volume = state->volume;
    if (state->attached_entity >= 0 && (DWORD)state->attached_entity < globals.num_edicts) {
        LPEDICT unit = globals.edicts + state->attached_entity;
        if (unit->inuse && unit->spawn_time == state->attached_spawn_time) {
            playback->origin = unit->s.origin;
            playback->emitter = unit;
            playback->positioned = true;
            return;
        }
    }
    if (state->has_position) {
        playback->origin = state->position;
        playback->positioned = true;
    }
}

/* Register one random authored file from a Warcraft sound-data row.  UI sound
 * aliases use the same FileNames/DirectoryBase schema as UnitAckSounds. */
static int G_RegisterSoundRow(UnitAckSounds_t const *row) {
    LPCSTR files, chosen, comma;
    char file[256], path[512];
    DWORD count = 0, pick;

    if (!row || !row->FileNames || !row->FileNames[0]) return 0;
    files = row->FileNames;
    count = 1;
    for (LPCSTR p = files; (p = strchr(p, ',')) != NULL; p++) count++;
    if (!count) return 0;

    pick = (DWORD)(rand() % count);
    chosen = files;
    while (pick--) {
        chosen = strchr(chosen, ',');
        if (!chosen) return 0;
        chosen++;
    }
    comma = strchr(chosen, ',');
    snprintf(file, sizeof(file), "%.*s",
             comma ? (int)(comma - chosen) : (int)strlen(chosen), chosen);
    if (row->DirectoryBase && row->DirectoryBase[0]) {
        size_t n = strlen(row->DirectoryBase);
        snprintf(path, sizeof(path), "%s%s%s", row->DirectoryBase,
                 row->DirectoryBase[n - 1] == '\\' || row->DirectoryBase[n - 1] == '/'
                     ? "" : "\\",
                 file);
    } else {
        snprintf(path, sizeof(path), "%s", file);
    }
    return gi.SoundIndex(path);
}

static int G_RegisterUISound(LPCSTR alias) {
    UnitAckSounds_t const *row;

    if (!alias || !alias[0]) return 0;
    row = G_UISound(alias);
    return G_RegisterSoundRow(row);
}

void G_PlayUISoundForPlayer(LPEDICT clent, LPCSTR alias) {
    int sound;

    /* UI sounds use the reliable owner-only sound packet and remain non-positional. */
    if (!clent || !clent->client || !clent->client->connected || !alias || !alias[0]) return;
    sound = G_RegisterUISound(alias);
    if (sound) gi.Sound(clent, CHAN_OWNER | CHAN_RELIABLE, sound, 1.0f, 0.0f, 0.0f);
}

static LPCSTR G_CommandErrorKeyForText(LPCSTR text) {
    size_t len;

    if (!text) return NULL;
    len = strlen(text);
    FOR_LOOP(i, sizeof(command_error_texts) / sizeof(command_error_texts[0])) {
        size_t base_len = strlen(command_error_texts[i].text);
        if (!strncmp(text, command_error_texts[i].text, base_len) &&
            (len == base_len || (len == base_len + 1 && text[base_len] == '.')))
            return command_error_texts[i].key;
    }
    return NULL;
}

static void G_PlayCommandErrorSound(LPEDICT clent, LPCSTR error_key) {
    LPGAMECLIENT client;
    LPCSTR alias;
    char skin_key[128];

    if (!clent || !(client = clent->client) || !error_key || !error_key[0]) return;
    snprintf(skin_key, sizeof(skin_key), "%sSound", error_key);
    alias = Theme_PlayerString(client, skin_key, NULL);
    if (!alias || !alias[0]) alias = "InterfaceError";
    G_PlayUISoundForPlayer(clent, alias);
}

/* CommandStrings [Errors] owns Warcraft's player-facing command failures.
 * Most entries are a single localized string; the handful of race-specific
 * entries (notably Nofood) store Human, Orc, Undead, Night Elf variants as a
 * comma-separated value. Keep simulation callers on the external error key so
 * text and the matching <Key>Sound skin lookup cannot drift apart. */
static DWORD G_CommandErrorRaceIndex(LPCGAMECLIENT client) {
    if (!client) return 0;
    switch (client->ps.race) {
    case kPlayerRaceHuman: return 0;
    case kPlayerRaceOrc: return 1;
    case kPlayerRaceUndead: return 2;
    case kPlayerRaceNightElf: return 3;
    default: return 0;
    }
}

static LPCSTR G_CommandErrorString(LPCGAMECLIENT client, LPCSTR error_key) {
    static char selected[4][MAX_GAMECACHE_STRING];
    static DWORD cursor;
    char *out = selected[cursor++ & 3];
    LPCSTR value;
    DWORD wanted, index = 0;

    if (!error_key || !error_key[0]) return NULL;
    value = FindConfigValue("Errors", error_key);
    if (!value || !value[0]) return NULL;
    if (!strchr(value, ',')) return G_LevelString(value);

    wanted = G_CommandErrorRaceIndex(client);
    while (*value) {
        LPCSTR begin, end;
        size_t length;
        while (*value == ',' || isspace((unsigned char)*value)) value++;
        begin = value;
        while (*value && *value != ',') value++;
        end = value;
        while (end > begin && isspace((unsigned char)end[-1])) end--;
        if (index++ == wanted) {
            length = MIN((size_t)(end - begin), sizeof(selected[0]) - 1);
            memcpy(out, begin, length);
            out[length] = '\0';
            return G_LevelString(out);
        }
        if (*value == ',') value++;
    }
    return NULL;
}

void G_ShowCommandErrorKey(LPEDICT clent, LPCSTR error_key, LPCSTR fallback) {
    LPCSTR text;

    if (!clent || !clent->client || !error_key || !error_key[0]) return;
    text = G_CommandErrorString(clent->client, error_key);
    if (!text || !text[0]) text = fallback;
    if (text && text[0])
        UI_ShowTransientText(clent, &MAKE(VECTOR2, 0, 0), text, 2.0f);
    G_PlayCommandErrorSound(clent, error_key);
}

void G_ShowCommandErrorText(LPEDICT clent, LPCSTR text) {
    LPCSTR key;

    if (!clent || !text || !text[0]) return;
    key = G_CommandErrorKeyForText(text);
    if (key) {
        G_ShowCommandErrorKey(clent, key, text);
        return;
    }
    UI_ShowTransientText(clent, &MAKE(VECTOR2, 0, 0), text, 2.0f);
    G_PlayUISoundForPlayer(clent, "InterfaceError");
}

void G_QueueReadySound(LPEDICT ent) {
    if (!ent || !ent->sound.num_ready) return;
    ent->sound.owner_pending = ent->sound.ready[rand() % ent->sound.num_ready];
}

void G_QueueOwnerSoundAlias(LPEDICT ent, LPCSTR alias) {
    int sound;

    if (!ent || ent->s.player >= MAX_PLAYERS || !alias || !alias[0]) return;
    sound = G_RegisterUISound(alias);
    if (sound) ent->sound.owner_pending = sound;
}

void G_QueueOwnerUISound(LPEDICT ent, LPCSTR skin_key) {
    LPGAMECLIENT client;
    LPCSTR alias;

    if (!ent || !skin_key || ent->s.player >= MAX_PLAYERS) return;
    client = G_GetPlayerClientByNumber(ent->s.player);
    if (!client || client->ps.number != ent->s.player) return;
    alias = Theme_PlayerString(client, skin_key, NULL);
    if (!alias || !alias[0]) return;
    G_QueueOwnerSoundAlias(ent, alias);
}
