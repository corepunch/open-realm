#include "g_local.h"
#include "g_unitrow.h"

static soundPolicy_t sound_index_policy[MAX_SOUNDS];
static UnitAckSounds_t const *sound_index_row[MAX_SOUNDS];
static uint16_t sound_index_last[MAX_PLAYERS][MAX_SOUNDS];

void G_AcceptSoundVariant(int index, uint32_t owner) {
    if (index <= 0 || index >= MAX_SOUNDS || owner >= MAX_PLAYERS || !sound_index_row[index]) return;
    FOR_LOOP(i, MAX_SOUNDS)
        if (sound_index_row[i] == sound_index_row[index]) sound_index_last[owner][i] = index;
}

bool G_SoundVariantIsLast(int index, uint32_t owner) {
    return index > 0 && index < MAX_SOUNDS && owner < MAX_PLAYERS && sound_index_last[owner][index] == index;
}

static float sound_index_volume[MAX_SOUNDS];
static uint32_t sound_index_duration[MAX_SOUNDS];
static uint8_t sound_index_volume_valid[MAX_SOUNDS];
static uint8_t sound_index_duration_valid[MAX_SOUNDS];

typedef struct {
    cstring_t text;
    cstring_t key;
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
    { "There are no usable corpses nearby", "Cantfindcorpse" },
    { "Inventory is full", "Inventoryfull" },
};

void G_ResetSoundPresentationState(void) {
    memset(sound_index_policy, 0, sizeof(sound_index_policy));
    memset(sound_index_row, 0, sizeof(sound_index_row));
    memset(sound_index_last, 0, sizeof(sound_index_last));
    memset(sound_index_volume, 0, sizeof(sound_index_volume));
    memset(sound_index_duration, 0, sizeof(sound_index_duration));
    memset(sound_index_volume_valid, 0, sizeof(sound_index_volume_valid));
    memset(sound_index_duration_valid, 0, sizeof(sound_index_duration_valid));
}

float G_SoundIndexVolume(int sound_index) {
    if (sound_index > 0 && sound_index < MAX_SOUNDS && sound_index_volume_valid[sound_index])
        return sound_index_volume[sound_index];
    return 1.0f;
}

uint32_t G_SoundIndexDuration(int sound_index) {
    if (sound_index > 0 && sound_index < MAX_SOUNDS && sound_index_duration_valid[sound_index])
        return sound_index_duration[sound_index];
    return 0;
}

void G_JassSoundRuntimeInit(handle_t handle) {
    gsound_t *state = handle;
    if (!state) return;
    state->volume = 1.0f;
    state->position = (VECTOR3){ 0 };
    state->attached_entity = -1;
    state->attached_spawn_time = 0;
    state->has_position = false;
}

void G_JassSoundSetVolume(handle_t handle, float volume) {
    gsound_t *state = handle;
    if (state) state->volume = MAX(0.0f, MIN(volume, 1.0f));
}

void G_JassSoundSetPosition(handle_t handle, LPCVECTOR3 position) {
    gsound_t *state = handle;
    if (!state || !position) return;
    state->position = *position;
    state->attached_entity = -1;
    state->attached_spawn_time = 0;
    state->has_position = true;
}

void G_JassSoundAttach(handle_t handle, LPEDICT unit) {
    gsound_t *state = handle;
    if (!state) return;
    state->attached_entity = unit ? (int32_t)unit->s.number : -1;
    state->attached_spawn_time = unit ? unit->spawn_time : 0;
    state->has_position = false;
}

void G_JassSoundPlayback(handle_t handle, jassSoundPlayback_t *playback) {
    gsound_t *state = handle;

    if (!playback) return;
    *playback = (jassSoundPlayback_t){ .volume = 1.0f };
    if (!state) return;
    playback->volume = state->volume;
    if (state->attached_entity >= 0 && (uint32_t)state->attached_entity < globals.num_edicts) {
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
static uint32_t G_SoundRowVariantCount(UnitAckSounds_t const *row) {
    uint32_t count = 0;
    cstring_t p;

    if (!row || !row->FileNames || !row->FileNames[0]) return 0;
    count = 1;
    for (p = row->FileNames; (p = strchr(p, ',')) != NULL; p++) count++;
    return count;
}

static bool G_SoundRowVariantPath(UnitAckSounds_t const *row, uint32_t variant,
                                  string_t path, size_t path_size) {
    cstring_t chosen, comma;
    char file[256];
    uint32_t count;

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

/* Default channel budgets recovered at 6fab5ec8 (1.27.1.7085). These are
 * game policy, not mixer constants. SLK Channel is a numeric string. */
static soundPolicy_t G_SoundRowPolicy(UnitAckSounds_t const *row, uint32_t variant) {
    static uint8_t const limits[] = {16,3,3,3,3,8,2,3,5,3,3,8,1,6,2,2};
    static struct { cstring_t name; uint16_t flag; } const flags[] = {
        {"CHANNELFULLPREEMPT", SOUND_CHANNEL_PREEMPT}, {"CHANNELFULLPREEMPTOLDEST", SOUND_CHANNEL_OLDEST},
        {"LISTFULLPREEMPT", SOUND_LIST_PREEMPT}, {"LISTFULLPREEMPTOLDEST", SOUND_LIST_OLDEST},
        {"NODUPLICATES", SOUND_NO_DUPLICATES}, {"DUPLICATEPREEMPT", SOUND_DUPLICATE_PREEMPT},
        {"NODUPEUSERNAMES", SOUND_NO_DUPLICATE_USERS}, {"DUPUSERNAMEPREEMPT", SOUND_USER_PREEMPT},
        {"IGNOREUSERNAME", SOUND_IGNORE_USER}
    };
    soundPolicy_t policy = { .priority = (uint32_t)MAX(0, row->Priority), .max_total = 24, .max_duplicates = 4 };
    char *end;
    long channel = row->Channel && row->Channel[0] ? strtol(row->Channel, &end, 10) : 0;
    if (channel < 0 || channel >= sizeof(limits) || (row->Channel && row->Channel[0] && *end)) {
        fprintf(stderr, "WC3 sound %s: invalid Channel '%s'\n", row->name, row->Channel);
        return (soundPolicy_t){0};
    }
    policy.group = channel;
    policy.max_channel = limits[channel];
    for (cstring_t word = row->Flags; word && *word;) {
        cstring_t comma = strchr(word, ',');
        size_t len = comma ? (size_t)(comma - word) : strlen(word);
        FOR_LOOP(i, sizeof(flags) / sizeof(flags[0]))
            if (strlen(flags[i].name) == len && !strncmp(word, flags[i].name, len)) policy.flags |= flags[i].flag;
        if (len == strlen("SCALEPRIORITY") && !strncmp(word, "SCALEPRIORITY", len)) policy.priority += variant;
        word = comma ? comma + 1 : NULL;
    }
    return policy;
}

soundPolicy_t const *G_SoundIndexPolicy(int index) {
    return index > 0 && index < MAX_SOUNDS && sound_index_policy[index].max_total ? &sound_index_policy[index] : NULL;
}

void G_PlaySound(LPCVECTOR3 origin, LPEDICT ent, int channel, int index, float volume, float attenuation, float timeofs) {
    soundPolicy_t const *registered = G_SoundIndexPolicy(index);
    uint32_t request = ent && ent->sound.pending == index && (channel & CHAN_OWNER) ? G_UnitResponseRequest(ent, index) : 0;
    if (registered || request) {
        /* Raw response paths have no authored row; retain generic capacity rules. */
        soundPolicy_t policy = registered ? *registered : (soundPolicy_t){ .priority = SOUND_PRIORITY(channel),
            .max_channel = 24, .max_total = 24, .max_duplicates = 4 };
        policy.request = request;
        /* Retail passes the unit pointer as username; an entity number is its
         * process-independent equivalent on our wire. Spatial origin is separate. */
        policy.user = ent ? ent->s.number : 0;
        if (ent && (channel & CHAN_OWNER) && (channel & 7) == CHAN_VOICE) policy.cooldown_ms = 250;
        gi.SoundPolicy(origin, ent, channel, index, volume, attenuation, timeofs, &policy);
    } else if (origin) gi.PositionedSound(origin, ent, channel, index, volume, attenuation, timeofs);
    else gi.Sound(ent, channel, index, volume, attenuation, timeofs);
}

static int G_RegisterSoundRowVariant(UnitAckSounds_t const *row, uint32_t variant) {
    char path[512];
    int sound;

    if (!G_SoundRowVariantPath(row, variant, path, sizeof(path))) return 0;
    soundPolicy_t policy = G_SoundRowPolicy(row, variant);
    if (!policy.max_total) return 0;
    char alias[256];
    if (snprintf(alias, sizeof(alias), "%s#%u", row->name, (unsigned)variant) >= sizeof(alias)) {
        fprintf(stderr, "WC3 sound alias too long: %s\n", row->name);
        return 0;
    }
    sound = gi.SoundIndexAlias(path, alias);
    if (sound > 0 && sound < MAX_SOUNDS) {
        sound_index_policy[sound] = policy;
        sound_index_row[sound] = row;
        sound_index_volume[sound] = MAX(0.0f, MIN(1.0f, row->Volume / 127.0f));
        sound_index_volume_valid[sound] = true;
        if (!sound_index_duration_valid[sound]) {
            sound_index_duration[sound] = (uint32_t)MAX(0, G_SoundFileDuration(path));
            sound_index_duration_valid[sound] = true;
        }
    }
    return sound;
}

/* Register one random authored file from a Warcraft sound-data row. */
static int G_RegisterSoundRow(UnitAckSounds_t const *row) {
    uint32_t count = G_SoundRowVariantCount(row);
    return count ? G_RegisterSoundRowVariant(row, (uint32_t)(rand() % count)) : 0;
}

uint32_t G_UnitAckSoundVariantCount(cstring_t label, cstring_t suffix) {
    char key[128];
    if (!label || !label[0] || !suffix) return 0;
    snprintf(key, sizeof(key), "%s%s", label, suffix);
    return G_SoundRowVariantCount(G_UnitAckSound(key));
}

int G_UnitAckSoundVariantIndex(cstring_t label, cstring_t suffix, uint32_t variant) {
    char key[128];
    if (!label || !label[0] || !suffix) return 0;
    snprintf(key, sizeof(key), "%s%s", label, suffix);
    return G_RegisterSoundRowVariant(G_UnitAckSound(key), variant);
}

uint32_t G_UnitCombatSoundVariantCount(cstring_t key) {
    return G_SoundRowVariantCount(G_UnitCombatSound(key));
}

int G_UnitCombatSoundVariantIndex(cstring_t key, uint32_t variant) {
    return G_RegisterSoundRowVariant(G_UnitCombatSound(key), variant);
}

bool G_SoundLabelDescriptor(cstring_t alias, string_t path, size_t path_size,
                            int *sound_index, float *volume) {
    UnitAckSounds_t const *row = G_KeyedSound(alias);

    if (sound_index) *sound_index = 0;
    if (volume) *volume = 1.0f;
    if (!row || !row->name || !row->name[0]) return false;
    if (volume) *volume = MAX(0.0f, MIN(1.0f, row->Volume / 127.0f));
    if (!path) return true;
    if (!path_size || !G_SoundRowVariantPath(row, 0, path, path_size)) return false;
    if (sound_index) *sound_index = G_RegisterSoundRowVariant(row, 0);
    return true;
}

static int G_SoundLabelIndex(cstring_t alias) {
    UnitAckSounds_t const *row = G_KeyedSound(alias);
    return row ? G_RegisterSoundRowVariant(row, 0) : 0;
}

void G_SetConstructionLoopSound(LPEDICT building, bool active) {
    UnitProfile_t const *profile;
    cstring_t alias;
    int sound;

    if (!building) return;
    building->s.sound = 0;
    if (!active) return;
    profile = building->data.UnitProfile;
    alias = profile ? profile->buildingSoundLabel : NULL;
    if (!alias || !alias[0] || !strcmp(alias, "_") || !strcasecmp(alias, "None")) return;
    sound = G_SoundLabelIndex(alias);
    if (sound > 0 && sound < MAX_SOUNDS) building->s.sound = (uint16_t)sound;
}

/* Ability sounds are simulation-triggered presentation. Pick the first authored
 * variant on the server rather than consuming gameplay rand(); presentation
 * variant randomization can move client-side without perturbing simulation RNG. */
static int G_RegisterAbilitySoundRow(UnitAckSounds_t const *row) {
    return G_RegisterSoundRowVariant(row, 0);
}

static int G_RegisterUISound(cstring_t alias) {
    UnitAckSounds_t const *row;

    if (!alias || !alias[0]) return 0;
    row = G_UISound(alias);
    return G_RegisterSoundRow(row);
}

int G_UISoundIndex(cstring_t alias) {
    return G_RegisterUISound(alias);
}

static cstring_t G_AbilitySoundAlias(uint32_t ability_id, bool looped) {
    char classname[5];
    cstring_t field = looped ? "Effectsoundlooped" : "Effectsound";
    cstring_t value;
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

int G_AbilityEffectSoundIndex(uint32_t ability_id, bool looped) {
    cstring_t alias = G_AbilitySoundAlias(ability_id, looped);
    return alias ? G_RegisterAbilitySoundRow(G_AbilitySound(alias)) : 0;
}

void G_PlayAbilityEffectSound(uint32_t ability_id, LPCVECTOR2 point) {
    cstring_t alias = G_AbilitySoundAlias(ability_id, false);
    UnitAckSounds_t const *row = alias ? G_AbilitySound(alias) : NULL;
    int sound = row ? G_RegisterAbilitySoundRow(row) : 0;
    if (sound && point) {
        VECTOR3 origin = { point->x, point->y, CM_GetHeightAtPoint(point->x, point->y) };
        float volume = MAX(0.0f, MIN(1.0f, row->Volume / 127.0f));
        G_PlaySound(&origin, NULL, CHAN_RELIABLE, sound, volume, 1.0f, 0.0f);
    }
}


static cstring_t G_ArmorSoundSuffix(LPCEDICT target) {
    int32_t armor;

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
    cstring_t weapon, armor;
    char key[128];
    int sound;
    float volume;

    if (!attacker || !target || !attacker->data.UnitWeapons) return;
    weapon = attacker->data.UnitWeapons->attack1.weaponSound;
    armor = G_ArmorSoundSuffix(target);
    if (!weapon || !weapon[0] || weapon[0] == '_' || !armor) return;
    snprintf(key, sizeof(key), "%s%s", weapon, armor);
    row = G_UnitCombatSound(key);
    sound = G_RegisterSoundRow(row);
    if (!sound) return;
    volume = G_SoundIndexVolume(sound);
    G_PlaySound(NULL, target, CHAN_WEAPON, sound, volume, 1.0f, 0.0f);
}

void G_PlayUISoundForPlayer(LPEDICT clent, cstring_t alias) {
    int sound;

    /* UI sounds use the reliable owner-only sound packet and remain non-positional. */
    if (!clent || !clent->client || !clent->client->connected || !alias || !alias[0]) return;
    sound = G_RegisterUISound(alias);
    if (sound) G_PlaySound(NULL, clent, CHAN_OWNER | CHAN_RELIABLE, sound, G_SoundIndexVolume(sound), 0.0f, 0.0f);
}

static cstring_t G_CommandErrorKeyForText(cstring_t text) {
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

static void G_PlayCommandErrorSound(LPEDICT clent, cstring_t error_key) {
    LPGAMECLIENT client;
    cstring_t alias;
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
static uint32_t G_CommandErrorRaceIndex(LPCGAMECLIENT client) {
    if (!client) return 0;
    switch (client->ps.race) {
    case kPlayerRaceHuman: return 0;
    case kPlayerRaceOrc: return 1;
    case kPlayerRaceUndead: return 2;
    case kPlayerRaceNightElf: return 3;
    default: return 0;
    }
}

static cstring_t G_CommandErrorString(LPCGAMECLIENT client, cstring_t error_key) {
    static char selected[4][MAX_GAMECACHE_STRING];
    static uint32_t cursor;
    char *out = selected[cursor++ & 3];
    cstring_t value;
    uint32_t wanted, index = 0;

    if (!error_key || !error_key[0]) return NULL;
    value = FindConfigValue("Errors", error_key);
    if (!value || !value[0]) return NULL;
    if (!strchr(value, ',')) return G_LevelString(value);

    wanted = G_CommandErrorRaceIndex(client);
    while (*value) {
        cstring_t begin, end;
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

void G_ShowCommandErrorKey(LPEDICT clent, cstring_t error_key, cstring_t fallback) {
    cstring_t text;

    if (!clent || !clent->client || !error_key || !error_key[0]) return;
    text = G_CommandErrorString(clent->client, error_key);
    if (!text || !text[0]) text = fallback;
    if (text && text[0])
        UI_ShowTransientText(clent, &MAKE(VECTOR2, 0, 0), text, 2.0f);
    G_PlayCommandErrorSound(clent, error_key);
}

void G_ShowCommandErrorText(LPEDICT clent, cstring_t text) {
    cstring_t key;

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

void G_QueueOwnerSoundAlias(LPEDICT ent, cstring_t alias) {
    int sound;

    if (!ent || ent->s.player >= MAX_PLAYERS || !alias || !alias[0]) return;
    sound = G_RegisterUISound(alias);
    if (sound) ent->sound.owner_pending = sound;
}

void G_QueueOwnerUISound(LPEDICT ent, cstring_t skin_key) {
    LPGAMECLIENT client;
    cstring_t alias;

    if (!ent || !skin_key || ent->s.player >= MAX_PLAYERS) return;
    client = G_GetPlayerClientByNumber(ent->s.player);
    if (!client || client->ps.number != ent->s.player) return;
    alias = Theme_PlayerString(client, skin_key, NULL);
    if (!alias || !alias[0]) return;
    G_QueueOwnerSoundAlias(ent, alias);
}
