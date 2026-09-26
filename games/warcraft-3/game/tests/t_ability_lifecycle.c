#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"

LPEDICT alloc_test_unit(uint32_t code, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

static UnitAbilities_t review_abilities = { .abilList = "AHfs,AHbz,AHdr,ANdr,AEtq,AHtb,AHre,AUfn,AHwe,Acan,AHmt" };
static char blizzard_effect_model[MAX_PATHLEN];

static int review_capture_model(cstring_t model) {
    strlcpy(blizzard_effect_model, model ? model : "", sizeof(blizzard_effect_model));
    return 77;
}

static char blizzard_sound_path[MAX_PATHLEN];
static uint32_t blizzard_sound_calls;
static int blizzard_sound_index;
static LPEDICT blizzard_sound_emitter;
static float blizzard_sound_volume;

static int review_capture_sound_index(cstring_t path) {
    strlcpy(blizzard_sound_path, path ? path : "", sizeof(blizzard_sound_path));
    return 91;
}
static int review_capture_sound_index_alias(cstring_t path, cstring_t alias) { (void)alias; return review_capture_sound_index(path); }


static void review_capture_sound_policy(LPCVECTOR3 origin, LPEDICT emitter, int channel, int sound, float volume, float attenuation, float timeofs, soundPolicy_t const *policy) {
    T_NOT_NULL(policy);
    T_EQ(policy->max_total, 24);
    T_EQ(policy->flags, SOUND_IGNORE_USER);
    (void)origin; (void)channel; (void)attenuation; (void)timeofs;
    blizzard_sound_calls++;
    blizzard_sound_index = sound;
    blizzard_sound_emitter = emitter;
    blizzard_sound_volume = volume;
}

static char const review_slk[] =
    "ID;PWXL;N;EBB;Y12;X17\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"targs\"\n"
    "C;Y1;X4;K\"Cost1\"\n"
    "C;Y1;X5;K\"Rng1\"\n"
    "C;Y1;X6;K\"Dur1\"\n"
    "C;Y1;X7;K\"HeroDur1\"\n"
    "C;Y1;X8;K\"Area1\"\n"
    "C;Y1;X9;K\"DataA1\"\n"
    "C;Y1;X10;K\"DataB1\"\n"
    "C;Y1;X11;K\"DataC1\"\n"
    "C;Y1;X12;K\"DataD1\"\n"
    "C;Y1;X13;K\"DataE1\"\n"
    "C;Y1;X14;K\"BuffID1\"\n"
    "C;Y1;X15;K\"UnitID1\"\n"
    "C;Y1;X17;K\"EfctID1\"\n"
    "C;Y2;X1;K\"AHfs\"\n"
    "C;Y2;X2;K\"AHfs\"\n"
    "C;Y2;X3;K\"ground,enemy\"\n"
    "C;Y2;X4;K\"0\"\n"
    "C;Y2;X5;K\"800\"\n"
    "C;Y2;X6;K\"9\"\n"
    "C;Y2;X7;K\"2.67\"\n"
    "C;Y2;X8;K\"200\"\n"
    "C;Y2;X9;K\"15\"\n"
    "C;Y2;X10;K\"0.33\"\n"
    "C;Y2;X11;K\"4\"\n"
    "C;Y2;X12;K\"1\"\n"
    "C;Y2;X13;K\"0.75\"\n"
    "C;Y2;X14;K\"BHfs\"\n"
    "C;Y3;X1;K\"AHbz\"\n"
    "C;Y3;X2;K\"AHbz\"\n"
    "C;Y3;X3;K\"ground,structure,enemy\"\n"
    "C;Y3;X4;K\"0\"\n"
    "C;Y3;X5;K\"800\"\n"
    "C;Y3;X6;K\"0\"\n"
    "C;Y3;X7;K\"0\"\n"
    "C;Y3;X8;K\"200\"\n"
    "C;Y3;X9;K\"6\"\n"
    "C;Y3;X10;K\"30\"\n"
    "C;Y3;X11;K\"6\"\n"
    "C;Y3;X12;K\"0.5\"\n"
    "C;Y3;X13;K\"0\"\n"
    "C;Y3;X14;K\"BHbd,BHbz\"\n"
    "C;Y3;X17;K\"Biml\"\n"
    "C;Y4;X1;K\"AHdr\"\n"
    "C;Y4;X2;K\"AHdr\"\n"
    "C;Y4;X3;K\"air,ground,enemy,friend\"\n"
    "C;Y4;X4;K\"0\"\n"
    "C;Y4;X5;K\"600\"\n"
    "C;Y4;X6;K\"6\"\n"
    "C;Y4;X7;K\"6\"\n"
    "C;Y4;X8;K\"800\"\n"
    "C;Y4;X9;K\"0\"\n"
    "C;Y4;X10;K\"15\"\n"
    "C;Y4;X11;K\"1\"\n"
    "C;Y4;X12;K\"0\"\n"
    "C;Y4;X13;K\"30\"\n"
    "C;Y4;X14;K\"\"\n"
    "C;Y5;X1;K\"ANdr\"\n"
    "C;Y5;X2;K\"AHdr\"\n"
    "C;Y5;X3;K\"air,ground,enemy\"\n"
    "C;Y5;X4;K\"0\"\n"
    "C;Y5;X5;K\"500\"\n"
    "C;Y5;X6;K\"8\"\n"
    "C;Y5;X7;K\"8\"\n"
    "C;Y5;X8;K\"800\"\n"
    "C;Y5;X9;K\"30\"\n"
    "C;Y5;X10;K\"0\"\n"
    "C;Y5;X11;K\"1\"\n"
    "C;Y5;X12;K\"0\"\n"
    "C;Y5;X13;K\"0\"\n"
    "C;Y5;X14;K\"\"\n"
    "C;Y6;X1;K\"AEtq\"\n"
    "C;Y6;X2;K\"AEtq\"\n"
    "C;Y6;X3;K\"air,ground,friend\"\n"
    "C;Y6;X4;K\"0\"\n"
    "C;Y6;X5;K\"0\"\n"
    "C;Y6;X6;K\"10\"\n"
    "C;Y6;X7;K\"10\"\n"
    "C;Y6;X8;K\"500\"\n"
    "C;Y6;X9;K\"20\"\n"
    "C;Y6;X10;K\"1\"\n"
    "C;Y6;X11;K\"0\"\n"
    "C;Y6;X12;K\"0\"\n"
    "C;Y6;X13;K\"0\"\n"
    "C;Y6;X14;K\"\"\n"
    "C;Y7;X1;K\"AHtb\"\n"
    "C;Y7;X2;K\"AHtb\"\n"
    "C;Y7;X3;K\"air,ground,enemy\"\n"
    "C;Y7;X4;K\"0\"\n"
    "C;Y7;X5;K\"600\"\n"
    "C;Y7;X6;K\"5\"\n"
    "C;Y7;X7;K\"3\"\n"
    "C;Y7;X8;K\"0\"\n"
    "C;Y7;X9;K\"100\"\n"
    "C;Y7;X10;K\"0\"\n"
    "C;Y7;X11;K\"0\"\n"
    "C;Y7;X12;K\"0\"\n"
    "C;Y7;X13;K\"0\"\n"
    "C;Y7;X14;K\"BPSE\"\n"
    "C;Y8;X1;K\"AHre\"\n"
    "C;Y8;X2;K\"AHre\"\n"
    "C;Y8;X3;K\"ground,friend,dead\"\n"
    "C;Y8;X4;K\"0\"\n"
    "C;Y8;X5;K\"400\"\n"
    "C;Y8;X6;K\"0\"\n"
    "C;Y8;X7;K\"0\"\n"
    "C;Y8;X8;K\"900\"\n"
    "C;Y8;X9;K\"6\"\n"
    "C;Y8;X10;K\"0\"\n"
    "C;Y8;X11;K\"0\"\n"
    "C;Y8;X12;K\"0\"\n"
    "C;Y8;X13;K\"0\"\n"
    "C;Y8;X14;K\"\"\n"
    "C;Y9;X1;K\"AUfn\"\n"
    "C;Y9;X2;K\"AUfn\"\n"
    "C;Y9;X3;K\"air,ground,enemy\"\n"
    "C;Y9;X4;K\"0\"\n"
    "C;Y9;X5;K\"800\"\n"
    "C;Y9;X6;K\"4\"\n"
    "C;Y9;X7;K\"2\"\n"
    "C;Y9;X8;K\"200\"\n"
    "C;Y9;X9;K\"50\"\n"
    "C;Y9;X10;K\"100\"\n"
    "C;Y9;X11;K\"0\"\n"
    "C;Y9;X12;K\"0\"\n"
    "C;Y9;X13;K\"0\"\n"
    "C;Y9;X14;K\"Bfro\"\n"
    "C;Y10;X1;K\"AHwe\"\n"
    "C;Y10;X2;K\"AHwe\"\n"
    "C;Y10;X3;K\"\"\n"
    "C;Y10;X4;K\"0\"\n"
    "C;Y10;X5;K\"0\"\n"
    "C;Y10;X6;K\"30\"\n"
    "C;Y10;X7;K\"30\"\n"
    "C;Y10;X8;K\"200\"\n"
    "C;Y10;X9;K\"2\"\n"
    "C;Y10;X10;K\"7\"\n"
    "C;Y10;X11;K\"0\"\n"
    "C;Y10;X12;K\"0\"\n"
    "C;Y10;X13;K\"0\"\n"
    "C;Y10;X14;K\"BHwe\"\n"
    "C;Y10;X15;K\"hfoo\"\n"
    "C;Y11;X1;K\"Acan\"\n"
    "C;Y11;X2;K\"Acan\"\n"
    "C;Y11;X3;K\"ground,dead,organic\"\n"
    "C;Y11;X4;K\"0\"\n"
    "C;Y11;X5;K\"50\"\n"
    "C;Y11;X6;K\"33\"\n"
    "C;Y11;X7;K\"33\"\n"
    "C;Y11;X8;K\"0\"\n"
    "C;Y11;X9;K\"10\"\n"
    "C;Y11;X10;K\"800\"\n"
    "C;Y11;X11;K\"0\"\n"
    "C;Y11;X12;K\"0\"\n"
    "C;Y11;X13;K\"0\"\n"
    "C;Y11;X14;K\"\"\n"
    "C;Y12;X1;K\"AHmt\"\n"
    "C;Y12;X2;K\"AHmt\"\n"
    "C;Y12;X3;K\"ground,structure,friend\"\n"
    "C;Y12;X4;K\"0\"\n"
    "C;Y12;X5;K\"5000\"\n"
    "C;Y12;X6;K\"0\"\n"
    "C;Y12;X7;K\"0\"\n"
    "C;Y12;X8;K\"800\"\n"
    "C;Y12;X9;K\"3\"\n"
    "C;Y12;X10;K\"3\"\n"
    "C;Y12;X11;K\"1\"\n"
    "C;Y12;X12;K\"0\"\n"
    "C;Y12;X13;K\"0\"\n"
    "C;Y12;X14;K\"\"\n"
    "C;Y1;X16;K\"Cast1\"\n"
    "C;Y2;X16;K1.33\n"
    "C;Y3;X16;K1\n"
    "E\n"
;

/* Independent minimal units drive ordinary casting and the production thinker scheduler. */
static LPEDICT review_unit(uint32_t owner, float x) {
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, 0);
    ent->spawn_time = G_Time(); ent->svflags |= SVF_MONSTER; ent->s.player = owner; ent->targtype = TARG_GROUND;
    ent->health.value = ent->health.max_value = 1000;
    ent->mana.value = 100; ent->mana.max_value = 1000;
    ent->stand = unit_stand; unit_stand(ent);
    return ent;
}

static LPEDICT review_setup(void) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    LPEDICT caster = review_unit(0, 0);
    caster->data.UnitAbilities = &review_abilities;
    return caster;
}

static LPEDICT review_thinker(LPEDICT caster) {
    FILTER_EDICTS(ent, ent->owner == caster && ent->think) return ent;
    return NULL;
}

/* Stock AHfs starts burning promptly; DataA is damage per full-damage pulse, not a 15-second delay. */
TEST(wc3_ability_lifecycle, flame_strike_stock_data_burns_within_two_seconds) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2);
    LPEDICT thinker = review_thinker(caster);
    level.time += 2000;
    if (thinker) G_RunEntity(thinker);
    float hp = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_NOT_NULL(thinker); T_ASSERT(hp < 1000);
}

/* Once a flame pulse runs, another server frame must not count as another full burn tick. */
TEST(wc3_ability_lifecycle, flame_strike_does_not_burn_every_server_frame) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2);
    LPEDICT thinker = review_thinker(caster);
    float hp = 0;
    if (thinker) {
        level.time = thinker->freetime; G_RunEntity(thinker); hp = enemy->health.value;
        level.time += FRAMETIME; G_RunEntity(thinker);
    }
    float after = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_NOT_NULL(thinker); T_FEQ(after, hp, .001f);
}

/* Stock Blizzard authors six waves and a zero Dur; zero must not truncate the cast to one second. */
TEST(wc3_ability_lifecycle, blizzard_stock_zero_duration_keeps_all_six_waves) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2);
    LPEDICT thinker = review_thinker(caster);
    /* Six authored waves now have two deadlines each: shard presentation,
     * then the fixed 0.8-second damage phase. */
    FOR_LOOP(i, 12) {
        if (!thinker || !thinker->inuse) break;
        level.time = thinker->freetime;
        G_RunEntity(thinker);
    }
    float hp = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_FEQ(hp, 820, .001f); T_ASSERT(!thinker->inuse);
}

/* Blizzard resolves shard presentation through the authored EfctID object,
 * not through AHbz's own Func art.  Biml's fixture EffectArt is distinctive. */
TEST(wc3_ability_lifecycle, blizzard_shards_use_authored_effect_object) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    int (*old_model_index)(cstring_t) = gi.ModelIndex;
    blizzard_effect_model[0] = '\0';
    gi.ModelIndex = review_capture_model;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    level.time = thinker->freetime; G_RunEntity(thinker);
    T_STREQ(blizzard_effect_model, "TestUI\\Models\\quad_sprite.mdx");
    gi.ModelIndex = old_model_index;
    S_SpellCancelChannel(caster);
    if (thinker && thinker->inuse) G_RunEntity(thinker);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Blizzard's authored EfctID also owns Effectsound.  AbilitySounds.slk is
 * a separate retail table, and Warsmash emits that sound once per shard. */
TEST(wc3_ability_lifecycle, blizzard_shards_use_authored_ability_sound) {
    static char const buff_slk[] =
        "ID;PWXL;N;E\n"
        "B;X2;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\n"
        "C;Y1;X2;K\"Effectsound\"\n"
        "C;Y2;X1;K\"Biml\"\n"
        "C;Y2;X2;K\"BlizzardTest\"\n"
        "E\n";
    static char const sound_slk[] =
        "ID;PWXL;N;E\n"
        "B;X5;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y1;X4;K\"Volume\"\n"
        "C;Y1;X5;K\"Flags\"\n"
        "C;Y2;X5;K\"IGNOREUSERNAME\"\n"
        "C;Y2;X1;K\"BlizzardTest\"\n"
        "C;Y2;X2;K\"blizzard.wav\"\n"
        "C;Y2;X3;K\"TestUI\\Sounds\\\"\n"
        "C;Y2;X4;K\"63.5\"\n"
        "E\n";
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *ability_rows = parse_slk_string(review_slk), *old_ability = G_SetSLKRows("AbilityData", ability_rows);
    slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
    slkTestData_t *sound_rows = parse_slk_string(sound_slk), *old_sound_rows = G_SetSLKRows("AbilitySounds", sound_rows);
    int (*old_sound_index)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;
    __typeof__(gi.SoundPolicy) old_sound_policy = gi.SoundPolicy;

    blizzard_sound_path[0] = '\0'; blizzard_sound_calls = 0; blizzard_sound_index = 0;
    blizzard_sound_emitter = NULL; blizzard_sound_volume = 0.0f;
    gi.SoundIndex = review_capture_sound_index; gi.SoundIndexAlias = review_capture_sound_index_alias; gi.SoundPolicy = review_capture_sound_policy;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    level.time = thinker->freetime; G_RunEntity(thinker);
    T_EQ(blizzard_sound_calls, 6); T_EQ(blizzard_sound_index, 91);
    T_NULL(blizzard_sound_emitter);
    T_STREQ(blizzard_sound_path, "TestUI\\Sounds\\blizzard.wav");
    T_FEQ(blizzard_sound_volume, 0.5f, .001f);

    gi.SoundIndex = old_sound_index; gi.SoundIndexAlias = old_sound_alias; gi.SoundPolicy = old_sound_policy;
    S_SpellCancelChannel(caster);
    if (thinker && thinker->inuse) G_RunEntity(thinker);
    G_SetSLKRows("AbilitySounds", old_sound_rows); free_slk_rows(sound_rows);
    G_SetSLKRows("AbilityBuffData", old_buff); free_slk_rows(buff_rows);
    G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}

/* Warsmash shows a Blizzard shard wave first and resolves that wave's damage
 * 0.8 seconds later; Cast owns the delay before the first shard wave. */
TEST(wc3_ability_lifecycle, blizzard_shards_precede_damage_by_eight_tenths) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    T_NOT_NULL(thinker); T_FEQ(enemy->health.value, 1000, .001f);
    T_EQ(thinker->freetime, 2000);

    level.time = thinker->freetime; G_RunEntity(thinker);
    T_FEQ(enemy->health.value, 1000, .001f);
    T_EQ(thinker->freetime, 2800);
    level.time = 2799; G_RunEntity(thinker);
    T_FEQ(enemy->health.value, 1000, .001f);
    level.time = 2800; G_RunEntity(thinker);
    T_FEQ(enemy->health.value, 970, .001f);

    S_SpellCancelChannel(caster);
    if (thinker->inuse) G_RunEntity(thinker);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The shard/damage phase bit lives on the serialized thinker state, so a save
 * between presentation and impact must resume with damage rather than replaying
 * another shard phase. */
TEST(wc3_ability_lifecycle, blizzard_damage_phase_survives_save_load) {
    cstring_t path = "/tmp/openwarcraft3-blizzard-phase-save.bin";
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    level.time = thinker->freetime; G_RunEntity(thinker); /* shards */
    T_FEQ(enemy->health.value, 1000, .001f);
    T_ASSERT(WriteGame(path));
    caster->channel.code = 0; thinker->think = NULL; thinker->variation = 1;
    T_ASSERT(ReadGame(path));
    T_ASSERT(thinker->think == blizzard_think);
    level.time = thinker->freetime; G_RunEntity(thinker);
    T_FEQ(enemy->health.value, 970, .001f);
    S_SpellCancelChannel(caster);
    if (thinker->inuse) G_RunEntity(thinker);
    remove(path); G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Blizzard DataD scales structures after ordinary per-target wave damage. */
TEST(wc3_ability_lifecycle, blizzard_applies_authored_building_reduction) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100), building = review_unit(1, 120);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    building->targtype = TARG_STRUCTURE;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    level.time = thinker->freetime; G_RunEntity(thinker); /* shards */
    level.time = thinker->freetime; G_RunEntity(thinker); /* damage */
    T_FEQ(enemy->health.value, 970, .001f);
    T_FEQ(building->health.value, 985, .001f);
    S_SpellCancelChannel(caster);
    if (thinker && thinker->inuse) G_RunEntity(thinker);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Summon Water Elemental owns DataA as its summon count and marks each result
 * as an ability-created summon for dispel/JASS classification. */
TEST(wc3_ability_lifecycle, water_elemental_uses_dataa_count_and_marks_summons) {
    LPEDICT caster = review_setup();
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    uint32_t count = 0;
    VECTOR2 first = {0};
    bool separated = false;
    caster->s.angle = 0.35f;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AHwe")));
    FILTER_EDICTS(unit, unit->inuse && unit->owner == caster &&
                  unit->summon_ability == FS_SLKKey("AHwe")) {
        T_FEQ(unit->s.angle, caster->s.angle, .001f);
        T_EQ(G_UnitStatusLevel(unit, MAKEFOURCC('B','H','w','e')), 1);
        T_EQ(G_UnitStatusLevel(unit, MAKEFOURCC('B','T','L','F')), 1);
        if (!count) first = unit->s.origin2;
        else if (Vector2_distance(&first, &unit->s.origin2) > .001f) separated = true;
        count++;
    }
    T_EQ(count, 2); T_ASSERT(separated);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Mass Teleport is a real channel: DataB delays the relocation, the caster
 * counts against DataA, and only the caster player's mobile units are moved. */
TEST(wc3_ability_lifecycle, mass_teleport_delays_caps_and_excludes_allies_and_structures) {
    LPEDICT caster = review_setup(), own1 = review_unit(0, 100), ally = review_unit(1, 150);
    LPEDICT building = review_unit(0, 200), own2 = review_unit(0, 250), own3 = review_unit(0, 300);
    LPEDICT target = review_unit(0, 2000);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    uint32_t moved = 0;
    level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;
    building->targtype = TARG_STRUCTURE;

    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHmt"), target));
    LPEDICT thinker = review_thinker(caster);
    T_NOT_NULL(thinker); T_ASSERT(target->paused); T_FEQ(caster->s.origin2.x, 0, .001f);
    level.time = thinker->freetime - 1; G_RunEntity(thinker);
    T_FEQ(caster->s.origin2.x, 0, .001f);
    level.time = thinker->freetime; G_RunEntity(thinker);

    moved += own1->s.origin2.x > 1000;
    moved += own2->s.origin2.x > 1000;
    moved += own3->s.origin2.x > 1000;
    T_EQ(moved, 2);
    T_ASSERT(caster->s.origin2.x > 1000);
    T_ASSERT(ally->s.origin2.x < 1000);
    T_ASSERT(building->s.origin2.x < 1000);
    /* Relocation must also update the server broad phase: the caster vacated
     * x=0, so an unmoved unit can legally occupy that point after completion. */
    T_ASSERT(G_CanRepositionUnitAt(own3, &MAKE(VECTOR2, 0, 0)));
    T_ASSERT(!target->paused); T_EQ(caster->channel.code, 0); T_ASSERT(!thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Structure is a first-class target-mask token; cancelling the channel must
 * also undo Mass Teleport's temporary destination pause. */
TEST(wc3_ability_lifecycle, mass_teleport_accepts_structure_target_and_cleans_cancel) {
    LPEDICT caster = review_setup(), target = review_unit(0, 2000);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    target->targtype = TARG_STRUCTURE;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHmt"), target));
    LPEDICT thinker = review_thinker(caster);
    T_NOT_NULL(thinker); T_ASSERT(target->paused);
    S_SpellCancelChannel(caster);
    T_ASSERT(!target->paused); T_EQ(caster->channel.code, 0);
    if (thinker && thinker->inuse) G_RunEntity(thinker);
    T_ASSERT(!thinker || !thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The destination entity stays authoritative until completion; losing it
 * cancels the channel instead of teleporting to a stale cached coordinate. */
TEST(wc3_ability_lifecycle, mass_teleport_target_death_cancels) {
    LPEDICT caster = review_setup(), target = review_unit(0, 2000);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHmt"), target));
    LPEDICT thinker = review_thinker(caster);
    target->health.value = 0;
    G_RunEntity(thinker);
    T_FEQ(caster->s.origin2.x, 0, .001f);
    T_ASSERT(!target->paused); T_EQ(caster->channel.code, 0); T_ASSERT(!thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A live Mass Teleport thinker must retain its callback, target incarnation,
 * pause ownership and deadline across the production save/load path. */
TEST(wc3_ability_lifecycle, mass_teleport_continues_after_save_load) {
    cstring_t path = "/tmp/openwarcraft3-mass-teleport-save.bin";
    LPEDICT caster = review_setup(), target = review_unit(0, 2000);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHmt"), target));
    LPEDICT thinker = review_thinker(caster);
    T_NOT_NULL(thinker); T_ASSERT(target->paused); T_ASSERT(WriteGame(path));
    caster->channel.code = 0; thinker->think = NULL; target->paused = false;
    T_ASSERT(ReadGame(path));
    T_ASSERT(thinker->think == mass_teleport_think); T_ASSERT(target->paused);
    level.time = thinker->freetime; G_RunEntity(thinker);
    T_ASSERT(caster->s.origin2.x > 1000); T_ASSERT(!target->paused); T_ASSERT(!thinker->inuse);
    remove(path); G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Cancelling the caster's channel must retire its pending resource transfer too. */
TEST(wc3_ability_lifecycle, siphon_mana_stops_after_movement_cancel) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy);
    LPEDICT thinker = review_thinker(caster);
    caster->s.origin2.x += 10; spell_run_frame(caster);
    uint32_t channel = caster->channel.code;
    level.time += 1000;
    if (thinker) G_RunEntity(thinker);
    float mana = enemy->mana.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_EQ(channel, 0); T_FEQ(mana, 100, .001f);
}

/* ANdr's authored DataA drains life even when the target has no mana pool. */
TEST(wc3_ability_lifecycle, life_drain_transfers_health_from_manaless_target) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    caster->health.value = 500; enemy->mana.value = enemy->mana.max_value = 0;
    bool cast = S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), enemy);
    LPEDICT thinker = review_thinker(caster);
    level.time += 1000;
    if (thinker) G_RunEntity(thinker);
    float hp = enemy->health.value, healed = caster->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_FEQ(hp, 970, .001f); T_FEQ(healed, 530, .001f);
}

/* Tranquility is AB_CHANNEL even though it needs no target-selection click. */
TEST(wc3_ability_lifecycle, no_target_tranquility_establishes_channel) {
    LPEDICT caster = review_setup();
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastNoTargetSpell(caster, FS_SLKKey("AEtq"));
    uint32_t channel = caster->channel.code;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_EQ(channel, FS_SLKKey("AEtq"));
}

/* Cannibalize reserves the nearest eligible corpse for the whole channel and
 * consumes it when healing reaches full HP.  Mechanical corpses fail the authored
 * ground,dead,organic target mask rather than a Cannibalize-only type check. */
TEST(wc3_ability_lifecycle, cannibalize_reserves_nearest_organic_corpse_and_stops_at_full_health) {
    LPEDICT caster = review_setup(), mechanical = review_unit(1, 10), near = review_unit(1, 30), far = review_unit(1, 40);
    UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    mechanical->data.UnitData = near->data.UnitData = far->data.UnitData = &corpse_data;
    mechanical->health.value = near->health.value = far->health.value = 0;
    mechanical->svflags |= SVF_DEADMONSTER; near->svflags |= SVF_DEADMONSTER; far->svflags |= SVF_DEADMONSTER;
    mechanical->targtype = TARG_MECHANICAL; caster->health.value = 995;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("Acan")));
    LPEDICT thinker = review_thinker(caster);
    T_ASSERT(mechanical->inuse); T_ASSERT(near->inuse); T_ASSERT(far->inuse); T_NOT_NULL(thinker);
    T_ASSERT(near->aiflags & AI_CORPSE_RESERVED); T_ASSERT(!G_UnitIsRaisableCorpse(near));
    FOR_LOOP(i, 5) { level.time += FRAMETIME; G_RunEntities(); }
    T_FEQ(caster->health.value, 1000, .001f); T_EQ(caster->channel.code, 0); T_ASSERT(!near->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Cannibalize is a no-target command: it acquires the nearest corpse itself and
 * approaches it before starting the channel instead of targeting the corpse. */
TEST(wc3_ability_lifecycle, cannibalize_command_approaches_nearby_corpse) {
    LPEDICT caster = review_setup(), corpse = review_unit(1, 200);
    UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    LPEDICT clent = &g_edicts[0];
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Acan"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item, .client = clent);
    caster->health.value = 500;
    corpse->data.UnitData = &corpse_data; corpse->health.value = 0; corpse->svflags |= SVF_DEADMONSTER;
    clent->client = &game.clients[0]; clent->client->ps.number = 0; G_SelectEntity(clent->client, caster);
    T_ASSERT(S_AbilityMessage(caster, A_COMMAND, &call));
    T_ASSERT(caster->goalentity == corpse); T_STREQ(caster->currentmove->animation, "walk");
    T_NOT_NULL(review_thinker(caster));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Dur=33 and DataA=10 are authoritative in both ROC and TFT; healing is continuous
 * on the simulation cadence rather than quantized to one-second pulses. */
TEST(wc3_ability_lifecycle, cannibalize_heals_for_authored_duration_through_entity_scheduler) {
    LPEDICT caster = review_setup(), corpse = review_unit(1, 40);
    UnitBalance_t caster_balance = *caster->data.UnitBalance;
    UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    caster_balance.healthRegen = 0;
    caster->data.UnitBalance = &caster_balance;
    corpse->data.UnitData = &corpse_data;
    caster->health.value = 500; corpse->health.value = 0; corpse->svflags |= SVF_DEADMONSTER;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("Acan")));
    FOR_LOOP(i, 330) { level.time += FRAMETIME; G_RunEntities(); }
    T_ASSERT(caster->health.value >= 829.0f && caster->health.value <= 831.0f);
    T_EQ(caster->channel.code, 0); T_ASSERT(!corpse->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Live, mechanical, Hero-lifecycle and out-of-search-range units cannot fund a cast; movement interrupts an active feast. */
TEST(wc3_ability_lifecycle, cannibalize_rejects_invalid_corpses_and_stops_when_caster_moves) {
    LPEDICT caster = review_setup(), live = review_unit(1, 20), mechanical = review_unit(1, 30), far = review_unit(1, 801);
    UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    mechanical->data.UnitData = far->data.UnitData = &corpse_data;
    g_edicts[0].client = &game.clients[0]; game.clients[0].connected = true; game.clients[0].ps.number = 0;
    mechanical->health.value = far->health.value = 0;
    mechanical->svflags |= SVF_DEADMONSTER; far->svflags |= SVF_DEADMONSTER; mechanical->targtype = TARG_MECHANICAL;
    T_ASSERT(!S_CastNoTargetSpell(caster, FS_SLKKey("Acan")));
    T_ASSERT(live->inuse); T_ASSERT(mechanical->inuse); T_ASSERT(far->inuse);
    far->s.origin2.x = far->s.origin.x = 40; caster->health.value = 500;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("Acan")));
    caster->s.origin2.x += 10; caster->s.origin.x += 10; level.time += FRAMETIME; G_RunEntities();
    T_ASSERT(caster->health.value >= 500.0f && caster->health.value < 501.0f);
    T_EQ(caster->channel.code, 0); T_ASSERT(!far->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A launched bolt must not stun a target which becomes spell immune before impact. */
TEST(wc3_ability_lifecycle, avatar_blocks_storm_bolt_stun_at_impact) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    G_SetUnitColorOverride(caster, 6);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastUnitTargetSpell(caster, FS_SLKKey("AHtb"), enemy);
    LPEDICT missile = NULL;
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) { missile = ent; break; }
    T_NOT_NULL(missile);
    T_EQ((missile->s.effect_flags & EFX_TEAM_COLOR_MASK) >> EFX_TEAM_COLOR_SHIFT, 7);
    unit_addtimedstatus(enemy, "BHav", 1, 10);
    if (missile) missile->currentmove->endfunc(missile);
    uint32_t stun = G_UnitStatusLevel(enemy, FS_SLKKey("Bstu"));
    float hp = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_NOT_NULL(missile); T_FEQ(hp, 1000, .001f); T_EQ(stun, 0);
}

/* Resurrection's stock tooltip promises ordinary friendly corpses, not exclusively Heroes. */
TEST(wc3_ability_lifecycle, resurrection_revives_friendly_footman) {
    LPEDICT caster = review_setup(), ally = review_unit(0, 100);
    UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    ally->data.UnitData = &corpse_data;
    ally->health.value = 0; ally->svflags |= SVF_DEADMONSTER;
    bool cast = S_CastNoTargetSpell(caster, FS_SLKKey("AHre"));
    float hp = ally->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_ASSERT(hp > 0);
}

/* Frost Nova's range and target damage require a unit-target order. */
TEST(wc3_ability_lifecycle, frost_nova_accepts_enemy_unit_target) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 500);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastUnitTargetSpell(caster, FS_SLKKey("AUfn"), enemy);
    float hp = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_FEQ(hp, 850, .001f);
}

/* Serial ownership separates two casts of the same spell even when both thinkers still exist. */
TEST(wc3_ability_lifecycle, recast_retires_old_thinker_without_cancelling_new_channel) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    LPEDICT first = review_thinker(caster);
    uint32_t serial = caster->channel.serial;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    T_NE(caster->channel.serial, serial);
    level.time += 1000; G_RunEntity(first);
    T_ASSERT(!first->inuse); T_EQ(caster->channel.code, FS_SLKKey("AHdr")); T_FEQ(enemy->mana.value, 100, .001f);
    LPEDICT next = review_thinker(caster);
    T_NOT_NULL(next);
    if (next) G_RunEntity(next);
    T_FEQ(enemy->mana.value, 85, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stun can happen after cast commitment but before the caster's next own frame. */
TEST(wc3_ability_lifecycle, stun_interrupts_blizzard_before_next_wave) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    level.time = thinker->freetime; G_RunEntity(thinker); /* shards */
    level.time = thinker->freetime; G_RunEntity(thinker); /* first damage */
    unit_addtimedstatus(caster, "Bstu", 1, 5);
    level.time = thinker->freetime; G_RunEntity(thinker);
    T_FEQ(enemy->health.value, 970, .001f); T_ASSERT(!thinker->inuse); T_EQ(caster->channel.code, 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The retired caster slot must not donate its old drain to a newly spawned unit. */
TEST(wc3_ability_lifecycle, removed_caster_slot_cannot_own_old_drain) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    LPEDICT thinker = review_thinker(caster);
    G_FreeEdict(caster); level.time += 2000;
    LPEDICT fresh = review_unit(0, 0);
    T_ASSERT(fresh == caster);
    fresh->channel.code = FS_SLKKey("AHdr"); fresh->channel.serial = thinker->channel.serial;
    G_RunEntity(thinker);
    T_ASSERT(!thinker->inuse); T_FEQ(enemy->mana.value, 100, .001f);
    T_EQ(fresh->channel.code, FS_SLKKey("AHdr"));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The target's edict identity matters independently of the still-active caster and cast token. */
TEST(wc3_ability_lifecycle, removed_target_slot_cannot_receive_old_drain) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    LPEDICT thinker = review_thinker(caster);
    G_FreeEdict(enemy); level.time += 2000;
    LPEDICT fresh = review_unit(1, 100);
    T_ASSERT(fresh == enemy); G_RunEntity(thinker);
    T_ASSERT(!thinker->inuse); T_FEQ(fresh->mana.value, 100, .001f); T_EQ(caster->channel.code, 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* AHdr's authored friendly transfer rate and direction differ from enemy siphoning. */
TEST(wc3_ability_lifecycle, siphon_sends_mana_to_allies_and_expires) {
    LPEDICT caster = review_setup(), ally = review_unit(0, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    caster->mana.value = 500; ally->mana.value = 0;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), ally));
    LPEDICT thinker = review_thinker(caster);
    FOR_LOOP(i, 6) { level.time += 1000; G_RunEntity(thinker); }
    T_FEQ(caster->mana.value, 320, .001f); T_FEQ(ally->mana.value, 180, .001f);
    T_ASSERT(!thinker->inuse); T_EQ(caster->channel.code, 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Correcting the schema must preserve delayed startup, the reduced-damage phase, and final cleanup. */
TEST(wc3_ability_lifecycle, flame_strike_obeys_authored_phase_intervals_and_expiry) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    level.time = 2000; G_RunEntity(thinker); T_FEQ(enemy->health.value, 1000, .001f);
    FOR_LOOP(i, 9) { level.time = 2330 + i * 330; G_RunEntity(thinker); }
    T_FEQ(enemy->health.value, 865, .001f);
    level.time = 5999; G_RunEntity(thinker); T_FEQ(enemy->health.value, 865, .001f);
    level.time = 6000; G_RunEntity(thinker); T_FEQ(enemy->health.value, 861, .001f);
    level.time = 11331; G_RunEntity(thinker); T_ASSERT(!thinker->inuse);
    T_FEQ(enemy->health.value, 861, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A targeted nova damages around its victim; nearby units do not receive the direct-target bonus. */
TEST(wc3_ability_lifecycle, frost_nova_uses_victim_center_and_separate_direct_damage) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 500), near = review_unit(1, 550);
    LPEDICT far = review_unit(1, 50), ally = review_unit(0, 550);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUfn"), enemy));
    T_FEQ(enemy->health.value, 850, .001f); T_FEQ(near->health.value, 950, .001f);
    T_FEQ(far->health.value, 1000, .001f); T_FEQ(ally->health.value, 1000, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Ordinary corpses restore selection and the idle move instead of retaining their decay callback. */
TEST(wc3_ability_lifecycle, resurrection_retires_death_state_and_rejects_empty_cast) {
    LPEDICT caster = review_setup(), ally = review_unit(0, 100), enemy = review_unit(1, 100);
    UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    ally->data.UnitData = &corpse_data;
    T_ASSERT(!S_CastNoTargetSpell(caster, FS_SLKKey("AHre")));
    unit_die(ally, enemy); ally->aiflags |= AI_HOLD_FRAME;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AHre")));
    T_FEQ(ally->health.value, ally->health.max_value, .001f);
    T_ASSERT(!(ally->svflags & SVF_DEADMONSTER)); T_ASSERT(!(ally->s.flags & EF_NOT_SELECTABLE));
    T_ASSERT(!(ally->aiflags & AI_HOLD_FRAME));
    T_ASSERT(!S_CastNoTargetSpell(caster, FS_SLKKey("AHre")));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Saving a live drain preserves its owner/target pointers, callback, deadline and cast identity together. */
TEST(wc3_ability_lifecycle, live_drain_continues_once_after_save_load) {
    cstring_t path = "/tmp/openwarcraft3-skill-drain-save.bin";
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    LPEDICT thinker = review_thinker(caster);
    T_ASSERT(WriteGame(path)); caster->channel.code = 0; thinker->think = NULL;
    T_ASSERT(ReadGame(path));
    level.time += 1000; G_RunEntity(thinker);
    T_FEQ(enemy->mana.value, 85, .001f); T_EQ(caster->channel.code, FS_SLKKey("AHdr"));
    G_RunEntity(thinker); T_FEQ(enemy->mana.value, 85, .001f);
    remove(path); G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The ROC Data11/Data12 spell schema and TFT DataA1/DataB1 schema must produce the same stock six waves. */
TEST(wc3_ability_lifecycle, blizzard_roc_data_columns_keep_all_authored_waves) {
    char roc[sizeof(review_slk)];
    memcpy(roc, review_slk, sizeof(roc));
    FOR_LOOP(i, 5) {
        char field[] = "DataA1"; field[4] += i;
        char *at = strstr(roc, field);
        T_NOT_NULL(at);
        if (at) { at[4] = '1'; at[5] = '1' + i; }
    }
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(roc), *old = G_SetSLKRows("AbilityData", rows);
    T_FEQ(S_SpellData(FS_SLKKey("AHbz"), 1, 1), 6, .001f);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    FOR_LOOP(i, 12) {
        if (!thinker->inuse) break;
        level.time = thinker->freetime; G_RunEntity(thinker);
    }
    T_FEQ(enemy->health.value, 820, .001f); T_ASSERT(!thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* UnitBalance creep level is unrelated to Hero identity when selecting Storm Bolt's duration. */
TEST(wc3_ability_lifecycle, storm_bolt_uses_hero_identity_for_stun_duration) {
    LPEDICT caster = review_setup(), hero = review_unit(1, 100), creep = review_unit(1, 150);
    UnitBalance_t balance = { .level = 8 }, hero_row = { .level = 1, .strength = 20 };
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    hero->class_id = MAKEFOURCC('H','p','a','l'); hero->data.UnitBalance = &hero_row; creep->data.UnitBalance = &balance;
    T_ASSERT(G_UnitIsHero(hero)); T_ASSERT(!G_UnitIsHero(creep));
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHtb"), hero));
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) {
        T_FEQ(ent->wait, 3, .001f); ent->currentmove->endfunc(ent);
    }
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHtb"), creep));
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) T_FEQ(ent->wait, 5, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Fractional authored intervals accumulate on scheduled deadlines instead of rounding each tick up to FRAMETIME. */
TEST(wc3_ability_lifecycle, flame_strike_fractional_interval_keeps_cadence_on_server_frames) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    while (level.time < 5000) { level.time += FRAMETIME; G_RunEntity(thinker); }
    T_FEQ(enemy->health.value, 865, .001f); T_EQ(thinker->freetime, 6000);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Replacement orders interrupt at dispatch, even before a Move changes coordinates or an Attack swings. */
TEST(wc3_ability_lifecycle, stop_move_and_attack_retire_channel_before_motion) {
    cstring_t orders[] = { "stop", "move", "attack" };
    FOR_LOOP(i, 3) {
        LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
        slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
        caster->unitinfo.MoveSpeed = 300; caster->movetype = MOVETYPE_STEP;
        caster->attack1.type = ATK_NORMAL;
        caster->attack1.range = 600; caster->attack1.damageBase = 10; caster->attack1.cooldown = 1;
        caster->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
        T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
        LPEDICT thinker = review_thinker(caster);
        if (i == 0) T_ASSERT(unit_issueimmediateorder(caster, orders[i]));
        else if (i == 1) T_ASSERT(unit_issueorder(caster, orders[i], &MAKE(VECTOR2, .x = 300)));
        else T_ASSERT(unit_issuetargetorder(caster, orders[i], enemy));
        umove_t const *move = caster->currentmove;
        T_EQ(caster->channel.code, 0); T_FEQ(caster->s.origin2.x, 0, .001f);
        level.time += 1000; G_RunEntity(thinker);
        T_ASSERT(!thinker->inuse); T_FEQ(enemy->mana.value, 100, .001f); T_ASSERT(caster->currentmove == move);
        G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    }
}

/* Multiple authored pulses can fit in one server frame; the patch preserves them instead of clamping the interval. */
TEST(wc3_ability_lifecycle, flame_strike_custom_interval_can_tick_twice_per_frame) {
    char slk[sizeof(review_slk)];
    memcpy(slk, review_slk, sizeof(slk));
    char *interval = strstr(slk, "0.33");
    T_NOT_NULL(interval);
    if (interval) memcpy(interval, "0.05", 4);
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    level.time = 2300; G_RunEntity(thinker); T_FEQ(enemy->health.value, 1000, .001f);
    level.time = 2400; G_RunEntity(thinker); T_FEQ(enemy->health.value, 970, .001f);
    level.time = 2500; G_RunEntity(thinker); T_FEQ(enemy->health.value, 940, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}
#endif
