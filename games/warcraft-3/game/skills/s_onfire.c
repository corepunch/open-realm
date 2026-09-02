/* WC3 building damage presentation.  The game selects the authored model and
 * attachment slots so the renderer only consumes server-authored effect data. */
#include "../g_local.h"

typedef struct {
    LPCSTR dir[2];
    LPCSTR prefix[2];
} onFireNames_t;

typedef struct {
    BYTE size;
    BYTE variant;
    USHORT slots;
} onFireStage_t;

static onFireNames_t const onfire_standard = {
    { "SmallBuildingFire", "LargeBuildingFire" },
    { "SmallBuildingFire", "LargeBuildingFire" },
};

static onFireNames_t const onfire_names[] = {
    [kPlayerRaceUndead] = { { "UndeadBuildingFire", "UndeadBuildingFire" },
                            { "UndeadSmallBuildingFire", "UndeadLargeBuildingFire" } },
    [kPlayerRaceNightElf] = { { "NightElfBuildingFire", "NightElfBuildingFire" },
                              { "ElfSmallBuildingFire", "ElfLargeBuildingFire" } },
};

static onFireStage_t const onfire_stage[] = {
    { 0, 2, EFX_SLOT_FIRST | EFX_SLOT_SECOND },
    { 1, 2, EFX_SLOT_FIRST | EFX_SLOT_SECOND | EFX_SLOT_FOURTH | EFX_SLOT_FIFTH },
    { 1, 1, EFX_SLOT_FIRST | EFX_SLOT_SECOND | EFX_SLOT_THIRD | EFX_SLOT_FOURTH | EFX_SLOT_FIFTH },
};

static DWORD onfire_race(LPCSTR name) {
    static struct { LPCSTR name; DWORD race; } const races[] = {
        { STR_HUMAN, kPlayerRaceHuman }, { STR_ORC, kPlayerRaceOrc },
        { STR_UNDEAD, kPlayerRaceUndead }, { STR_NIGHTELF, kPlayerRaceNightElf },
    };

    if (name) FOR_LOOP(i, sizeof(races) / sizeof(*races))
        if (!strcmp(name, races[i].name)) return races[i].race;
    return kPlayerRaceNone;
}

/* Select the only authored race-specific families; Human, Orc, and unknown data use standard assets. */
static onFireNames_t const *onfire_family(DWORD race) {
    if (race == kPlayerRaceUndead || race == kPlayerRaceNightElf) return onfire_names + race;
    return &onfire_standard;
}

static BYTE onfire_level(BYTE health) {
    if (health > 255 * 3 / 4) return 255;
    if (health > 255 / 2) return 0;
    if (health > 255 / 4) return 1;
    return 2;
}

/* Refresh the server-authored fire model whenever health or construction state
 * changes.  Zero clears the effect model so the client removes the previous effect. */
void G_UpdateOnFire(LPEDICT ent) {
    UnitData_t const *data;
    onFireNames_t const *names;
    onFireStage_t const *stage;
    PATHSTR path;
    BYTE level;
    DWORD race;

    ent->s.effect = 0;
    ent->s.effect_flags = 0;
    if (!ent->inuse || !(ent->s.flags & EF_BUILDING) || ent->health.value <= 0.0f ||
        !ent->health.max_value || (ent->animation && !strncasecmp(ent->animation->name, "birth", 5))) return;

    level = onfire_level(compress_stat(&ent->health));
    if (level == 255) return;
    data = G_UnitData(ent->class_id);
    race = onfire_race(data ? data->race : NULL);
    names = onfire_family(race);
    stage = onfire_stage + level;
    snprintf(path, sizeof(path), "Environment\\%s\\%s%d.mdx", names->dir[stage->size],
             names->prefix[stage->size], stage->variant);
    ent->s.effect = G_RegisterModel(path);
    if (!ent->s.effect) {
        fprintf(stderr, "G_UpdateOnFire: failed to register %s\n", path);
        return;
    }
    ent->s.effect_flags = EFX_MODEL | EFX_ATTACH_SLOTS | stage->slots;
}
