#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define FIRE_LAVA MAKEFOURCC('A','N','l','m')
#define FIRE_CONTROLLER MAKEFOURCC('L','v','S','p')
#define FIRE_INCINERATE MAKEFOURCC('A','N','i','a')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

/* Nonstock DataB/C/D/E/F: 2-second delay, 3 hits, 25% HP,
 * 8-second life extension and 1 generation. */
static char const firelord_lava_slk[] =
    "ID;PWXL;N;E\nB;X13;Y2;D0\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\n"
    "C;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"DataC1\"\n"
    "C;Y1;X8;K\"DataD1\"\nC;Y1;X9;K\"DataE1\"\n"
    "C;Y1;X10;K\"DataF1\"\nC;Y1;X11;K\"UnitID1\"\n"
    "C;Y1;X12;K\"BuffID1\"\nC;Y1;X13;K\"targs\"\n"
    "C;Y2;X1;K\"ANlm\"\nC;Y2;X2;K\"ANlm\"\nC;Y2;X3;K1\n"
    "C;Y2;X4;K30\nC;Y2;X5;K1\nC;Y2;X6;K2\n"
    "C;Y2;X7;K3\nC;Y2;X8;K0.25\nC;Y2;X9;K8\n"
    "C;Y2;X10;K1\nC;Y2;X11;K\"hfoo\"\n"
    "C;Y2;X12;K\"BNlm\"\nC;Y2;X13;K\"enemy,neutral,air,ground,ward\"\nE\n";

static char const firelord_arrow_slk[] =
    "ID;PWXL;N;E\nB;X10;Y2;D0\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\n"
    "C;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"Dur1\"\n"
    "C;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"DataB1\"\n"
    "C;Y1;X10;K\"Cool1\"\n"
    "C;Y2;X1;K\"ANia\"\nC;Y2;X2;K\"ANia\"\nC;Y2;X3;K1\n"
    "C;Y2;X4;K\"enemy,neutral,organic,nonancient\"\n"
    "C;Y2;X5;K9\nC;Y2;X6;K7\nC;Y2;X7;K4\n"
    "C;Y2;X8;K\"BNic\"\nC;Y2;X9;K30\nC;Y2;X10;K0\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *caster, *lava;
} firelordFix_t;

static firelordFix_t firelord_lava_setup(void) {
    firelordFix_t fix;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    fix.rows = parse_slk_string(firelord_lava_slk);
    fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.caster = alloc_test_unit(FS_SLKKey("Hpal"), 128, 128);
    fix.lava = alloc_test_unit(FS_SLKKey("hfoo"), 192, 128);
    fix.caster->s.player = fix.lava->s.player = 0;
    fix.caster->targtype = fix.lava->targtype = TARG_GROUND;
    fix.lava->owner = fix.caster;
    fix.lava->summon_ability = FIRE_LAVA;
    fix.lava->health.value = 40;
    fix.lava->health.max_value = 100;
    S_SpellApplyTimedLife(fix.lava, 1, 30);
    S_LavaSpawnRegister(fix.lava, fix.caster, FIRE_LAVA, 1);
    return fix;
}
static void firelord_lava_done(firelordFix_t fix) {
    G_SetSLKRows("AbilityData", fix.old);
    free_slk_rows(fix.rows);
}
static edict_t *firelord_marker(edict_t *lava) {
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == FIRE_CONTROLLER && ent->owner == lava)
        return ent;
    return NULL;
}

TEST(wc3_spell, lava_spawn_counts_successful_hits_and_splits_after_authored_delay) {
    firelordFix_t fix = firelord_lava_setup();
    edict_t *marker = firelord_marker(fix.lava), *child = NULL;
    uint32_t const now = G_Time();
    heroabilitystatus_t *life = unit_findstatus(fix.lava, FS_SLKKey("BTLF"));
    T_NOT_NULL(marker); T_NOT_NULL(life);
    if (!marker || !life) { firelord_lava_done(fix); return; }
    uint32_t const original_expiry = life->timestamp;
    S_LavaSpawnAttackHit(fix.lava);
    S_LavaSpawnAttackHit(fix.lava);
    T_EQ(marker->damage, 2u);
    T_NULL(marker->think);
    S_LavaSpawnAttackHit(fix.lava);
    T_EQ(marker->damage, 3u);
    T_ASSERT(marker->think == lava_spawn_split_think);
    T_EQ(marker->freetime, now + 2000u);
    level.time = now + 1999;
    lava_spawn_split_think(marker);
    T_EQ(marker->resources, 0u);
    level.time++;
    lava_spawn_split_think(marker);
    if (marker->resources) {
        T_EQ(marker->resources, 1u);
        T_NULL(marker->think);
        T_EQ(life->timestamp, original_expiry + 8000u);
        T_FEQ(fix.lava->health.value, 65.0f, 0.001f);
        FILTER_EDICTS(unit, unit->inuse && unit != fix.lava &&
                      unit->summon_ability == FIRE_LAVA && unit->owner == fix.caster)
            child = unit;
        T_NOT_NULL(child);
        if (child) {
            T_NOT_NULL(firelord_marker(child));
            T_EQ(unit_findstatus(child, FS_SLKKey("BTLF"))->timestamp, life->timestamp);
        }
        FOR_LOOP(i, 10) S_LavaSpawnAttackHit(fix.lava);
        T_NULL(marker->think); /* generation cap, no second split */
    }
    firelord_lava_done(fix);
}

TEST(wc3_spell, lava_spawn_retires_pending_split_when_summon_dies) {
    firelordFix_t fix = firelord_lava_setup();
    edict_t *marker = firelord_marker(fix.lava);
    T_NOT_NULL(marker);
    FOR_LOOP(i, 3) S_LavaSpawnAttackHit(fix.lava);
    T_ASSERT(marker && marker->think == lava_spawn_split_think);
    /* Retire the controller through the same direct RemoveUnit lifecycle. */
    S_LavaSpawnRelease(fix.lava);
    T_ASSERT(!marker || !marker->inuse);
    firelord_lava_done(fix);
}

TEST(wc3_save, lava_spawn_pending_split_controller_survives_save) {
    cstring_t const path = Test_TempPath("wc3-lava-pending-split.bin");
    firelordFix_t fix = firelord_lava_setup();
    edict_t *marker = firelord_marker(fix.lava);
    T_NOT_NULL(marker);
    if (!marker) { firelord_lava_done(fix); return; }
    FOR_LOOP(i, 3) S_LavaSpawnAttackHit(fix.lava);
    T_ASSERT(WriteGame(path));
    marker->think = NULL; marker->damage = 0; marker->resources = 9;
    marker->owner = NULL; marker->goalentity = NULL;
    T_ASSERT(ReadGame(path));
    T_ASSERT(marker->think == lava_spawn_split_think);
    T_EQ(marker->damage, 3u);
    T_EQ(marker->resources, 0u);
    T_ASSERT(marker->owner == fix.lava);
    T_ASSERT(marker->goalentity == fix.caster);
    remove(path);
    firelord_lava_done(fix);
}

TEST(wc3_spell, incinerate_arrow_requires_autocast_and_pays_once_per_hit) {
    slkTestData_t *rows, *old;
    edict_t *attacker, *target;
    reset_entities(); setup_test_world(); level.time = 1000;
    rows = parse_slk_string(firelord_arrow_slk);
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    attacker = alloc_test_unit(FS_SLKKey("Hpal"), 128, 128);
    target = alloc_test_unit(FS_SLKKey("hfoo"), 192, 128);
    attacker->s.player = 0; target->s.player = 1;
    attacker->targtype = target->targtype = TARG_GROUND;
    attacker->unit_state->ranks[0] = MAKE(heroability_t, .code = FIRE_INCINERATE, .level = 1);
    attacker->mana.value = attacker->mana.max_value = 50;
    S_IncinerateOnHit(attacker, target);
    T_EQ(G_UnitStatusLevel(target, FS_SLKKey("BNic")), 0u);
    T_FEQ(attacker->mana.value, 50, 0.001f);
    T_ASSERT(G_SetUnitAutocast(attacker, FIRE_INCINERATE, true));
    S_IncinerateOnHit(attacker, target);
    T_EQ(G_UnitStatusLevel(target, FS_SLKKey("BNic")), 1u);
    T_FEQ(attacker->mana.value, 41, 0.001f);
    S_IncinerateOnHit(attacker, target);
    T_EQ(G_UnitStatusLevel(target, FS_SLKKey("BNic")), 2u);
    T_FEQ(attacker->mana.value, 32, 0.001f);
    attacker->mana.value = 0;
    S_IncinerateOnHit(attacker, target);
    T_EQ(G_UnitStatusLevel(target, FS_SLKKey("BNic")), 2u);
    T_ASSERT(G_SetUnitAutocast(attacker, FIRE_INCINERATE, false));
    attacker->mana.value = 50;
    S_IncinerateOnHit(attacker, target);
    T_EQ(G_UnitStatusLevel(target, FS_SLKKey("BNic")), 2u);
    T_FEQ(attacker->mana.value, 50, 0.001f);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}
#endif /* BZ_TESTS */
