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
    { "That building is currently under construction", "UnderConstruction" },
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

/* Resolve one authored file from a Warcraft sound-data row. */
static DWORD G_SoundRowVariantCount(UnitAckSounds_t const *row) {
    DWORD count = 0;
    LPCSTR p;

    if (!row || !row->FileNames || !row->FileNames[0]) return 0;
    count = 1;
    for (p = row->FileNames; (p = strchr(p, ',')) != NULL; p++) count++;
    return count;
}

static BOOL G_SoundRowVariantPath(UnitAckSounds_t const *row, DWORD variant,
                                  LPSTR path, size_t path_size) {
    LPCSTR chosen, comma;
    char file[256];
    DWORD count;

    if (!path || !path_size || !(count = G_SoundRowVariantCount(row)) || variant >= count)
        return false;
    chosen = row->FileNames;
    while (variant--) {
        chosen = strchr(chosen, ',');
        if (!chosen) return false;
        chosen++;
    }
    comma = strchr(chosen, ',');
    snprintf(file, sizeof(file), "%.*s",
             comma ? (int)(comma - chosen) : (int)strlen(chosen), chosen);
    if (row->DirectoryBase && row->DirectoryBase[0]) {
        size_t n = strlen(row->DirectoryBase);
        snprintf(path, path_size, "%s%s%s", row->DirectoryBase,
                 row->DirectoryBase[n - 1] == '\\' || row->DirectoryBase[n - 1] == '/'
                     ? "" : "\\",
                 file);
    } else {
        snprintf(path, path_size, "%s", file);
    }
    return true;
}

static int G_RegisterSoundRowVariant(UnitAckSounds_t const *row, DWORD variant) {
    char path[512];
    return G_SoundRowVariantPath(row, variant, path, sizeof(path)) ? gi.SoundIndex(path) : 0;
}

/* Register one random authored file from a Warcraft sound-data row. */
static int G_RegisterSoundRow(UnitAckSounds_t const *row) {
    DWORD count = G_SoundRowVariantCount(row);
    return count ? G_RegisterSoundRowVariant(row, (DWORD)(rand() % count)) : 0;
}

DWORD G_UnitAckSoundVariantCount(LPCSTR label, LPCSTR suffix) {
    char key[128];
    if (!label || !label[0] || !suffix) return 0;
    snprintf(key, sizeof(key), "%s%s", label, suffix);
    return G_SoundRowVariantCount(G_UnitAckSound(key));
}

int G_UnitAckSoundVariantIndex(LPCSTR label, LPCSTR suffix, DWORD variant) {
    char key[128];
    if (!label || !label[0] || !suffix) return 0;
    snprintf(key, sizeof(key), "%s%s", label, suffix);
    return G_RegisterSoundRowVariant(G_UnitAckSound(key), variant);
}

BOOL G_SoundLabelDescriptor(LPCSTR alias, LPSTR path, size_t path_size,
                            int *sound_index, FLOAT *volume) {
    UnitAckSounds_t const *row = G_KeyedSound(alias);

    if (sound_index) *sound_index = 0;
    if (volume) *volume = 1.0f;
    if (!row || !row->name || !row->name[0] ||
        !G_SoundRowVariantPath(row, 0, path, path_size)) return false;
    if (sound_index) *sound_index = gi.SoundIndex(path);
    if (volume) *volume = MAX(0.0f, MIN(1.0f, row->Volume / 127.0f));
    return true;
}

/* Ability sounds are simulation-triggered presentation. Pick the first authored
 * variant on the server rather than consuming gameplay rand(); presentation
 * variant randomization can move client-side without perturbing simulation RNG. */
static int G_RegisterAbilitySoundRow(UnitAckSounds_t const *row) {
    return G_RegisterSoundRowVariant(row, 0);
}

static int G_RegisterUISound(LPCSTR alias) {
    UnitAckSounds_t const *row;

    if (!alias || !alias[0]) return 0;
    row = G_UISound(alias);
    return G_RegisterSoundRow(row);
}

static LPCSTR G_AbilitySoundAlias(DWORD ability_id, BOOL looped) {
    char classname[5];
    LPCSTR field = looped ? "Effectsoundlooped" : "Effectsound";
    LPCSTR value;
    AbilityData_t const *ability;
    AbilityBuffData_t const *buff;

    if (!ability_id) return NULL;
    memcpy(classname, &ability_id, 4);
    classname[4] = '\0';
    value = FindConfigValue(classname, field);
    if (value && *value && strcmp(value, "-") && strcmp(value, "_")) return value;

    ability = G_AbilityData(ability_id);
    if (ability->code && ability->code != ability_id) {
        memcpy(classname, &ability->code, 4);
        classname[4] = '\0';
        value = FindConfigValue(classname, field);
        if (value && *value && strcmp(value, "-") && strcmp(value, "_")) return value;
    }

    /* Effect objects such as Blizzard's EfctID live in AbilityBuffData.slk. */
    buff = G_AbilityBuffData(ability_id);
    if (buff->id == ability_id) {
        value = looped ? buff->effectSoundLooped : buff->effectSound;
        if (value && *value && strcmp(value, "-") && strcmp(value, "_")) return value;
        if (buff->code && buff->code != ability_id) {
            AbilityBuffData_t const *base = G_AbilityBuffData(buff->code);
            if (base->id == buff->code) {
                value = looped ? base->effectSoundLooped : base->effectSound;
                if (value && *value && strcmp(value, "-") && strcmp(value, "_")) return value;
            }
        }
    }
    return NULL;
}

int G_AbilityEffectSoundIndex(DWORD ability_id, BOOL looped) {
    LPCSTR alias = G_AbilitySoundAlias(ability_id, looped);
    return alias ? G_RegisterAbilitySoundRow(G_AbilitySound(alias)) : 0;
}

void G_PlayAbilityEffectSound(DWORD ability_id, LPCVECTOR2 point) {
    LPCSTR alias = G_AbilitySoundAlias(ability_id, false);
    UnitAckSounds_t const *row = alias ? G_AbilitySound(alias) : NULL;
    int sound = row ? G_RegisterAbilitySoundRow(row) : 0;
    if (sound && point) {
        VECTOR3 origin = { point->x, point->y, CM_GetHeightAtPoint(point->x, point->y) };
        FLOAT volume = MAX(0.0f, MIN(1.0f, row->Volume / 127.0f));
        gi.PositionedSound(&origin, NULL, CHAN_RELIABLE, sound, volume, 1.0f, 0.0f);
    }
}


static LPCSTR G_ArmorSoundSuffix(LPCEDICT target) {
    LONG armor;

    if (!target) return NULL;
    if (G_IsDestructable(target)) armor = target->data.DestructableData->armor;
    else if (target->data.UnitUI) armor = target->data.UnitUI->armorType;
    else return NULL;
    switch (armor) {
    case 1: return "Flesh";
    case 2: return "Metal";
    case 3: return "Wood";
    case 4: return "Ethereal";
    case 5: return "Stone";
    default: return NULL;
    }
}

void G_PlayCombatImpactSound(LPEDICT attacker, LPEDICT target) {
    UnitAckSounds_t const *row;
    LPCSTR weapon, armor;
    char key[128];
    int sound;
    FLOAT volume;

    if (!attacker || !target || !attacker->data.UnitWeapons) return;
    weapon = attacker->data.UnitWeapons->attack1.weaponSound;
    armor = G_ArmorSoundSuffix(target);
    if (!weapon || !weapon[0] || weapon[0] == '_' || !armor) return;
    snprintf(key, sizeof(key), "%s%s", weapon, armor);
    row = G_UnitCombatSound(key);
    sound = G_RegisterSoundRow(row);
    if (!sound) return;
    volume = MAX(0.0f, MIN(1.0f, row->Volume / 127.0f));
    gi.Sound(target, CHAN_WEAPON, sound, volume, 1.0f, 0.0f);
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
