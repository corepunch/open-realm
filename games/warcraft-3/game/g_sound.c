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
    if (!text) return NULL;
    if (!strcmp(text, "Not enough food") || !strcmp(text, "Not enough food.")) return "Nofood";
    if (!strcmp(text, "Not enough gold") || !strcmp(text, "Not enough gold.")) return "Nogold";
    if (!strcmp(text, "Not enough lumber") || !strcmp(text, "Not enough lumber.")) return "Nolumber";
    if (!strcmp(text, "Not enough mana") || !strcmp(text, "Not enough mana.")) return "Nomana";
    if (!strcmp(text, "Spell is not ready yet") || !strcmp(text, "Spell is not ready yet.")) return "Cooldown";
    if (!strcmp(text, "Unable to build there") || !strcmp(text, "Unable to build there.")) return "Cantplace";
    if (!strcmp(text, "Inventory is full") || !strcmp(text, "Inventory is full.")) return "Inventoryfull";
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

void G_ShowCommandErrorText(LPEDICT clent, LPCSTR text) {
    LPCSTR key;

    if (!clent || !text || !text[0]) return;
    UI_ShowTransientText(clent, &MAKE(VECTOR2, 0, 0), text, 2.0f);
    key = G_CommandErrorKeyForText(text);
    if (key) G_PlayCommandErrorSound(clent, key);
    else G_PlayUISoundForPlayer(clent, "InterfaceError");
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
