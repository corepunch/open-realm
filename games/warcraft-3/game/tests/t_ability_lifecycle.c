#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t code, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

static UnitAbilities_t review_abilities = { .abilList = "AHfs,AHbz,AHdr,ANdr,AEtq,AHtb,AHre,AUfn,AHwe,Acan,AHmt,ANrf" };
static char blizzard_effect_model[MAX_PATHLEN];

static int review_capture_model(cstring_t model) {
    strlcpy(blizzard_effect_model, model ? model : "", sizeof(blizzard_effect_model));
    return 77;
}

static char flame_strike_models[8][MAX_PATHLEN];
static uint32_t flame_strike_model_count;
static int flame_strike_capture_model(cstring_t model) {
    if (flame_strike_model_count < sizeof(flame_strike_models) / sizeof(flame_strike_models[0]))
        strlcpy(flame_strike_models[flame_strike_model_count], model ? model : "", MAX_PATHLEN);
    flame_strike_model_count++;
    return 77;
}

static char blizzard_sound_path[MAX_PATHLEN];
static uint32_t blizzard_sound_calls;
static int blizzard_sound_index;
static edict_t *blizzard_sound_emitter;
static float blizzard_sound_volume;

static int review_capture_sound_index(cstring_t path) {
    strlcpy(blizzard_sound_path, path ? path : "", sizeof(blizzard_sound_path));
    return 91;
}
static int review_capture_sound_index_alias(cstring_t path, cstring_t alias) { (void)alias; return review_capture_sound_index(path); }


static void review_capture_sound_policy(vec3_t const *origin, edict_t *emitter, int channel, int sound, float volume, float attenuation, float timeofs, soundPolicy_t const *policy) {
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
    "ID;PWXL;N;EBB;Y12;X18\n"
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
    "C;Y1;X18;K\"DataI1\"\n"
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
    "C;Y2;X17;K\"XHfs\"\n"
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
    "C;Y4;X18;K\"3\"\n"
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
static edict_t *review_unit(uint32_t owner, float x) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, 0);
    ent->spawn_time = G_Time(); ent->svflags |= SVF_MONSTER; ent->s.player = owner; ent->targtype = TARG_GROUND;
    ent->health.value = ent->health.max_value = 1000;
    ent->mana.value = 100; ent->mana.max_value = 1000;
    ent->stand = unit_stand; unit_stand(ent);
    return ent;
}

static edict_t *review_setup(void) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    edict_t *caster = review_unit(0, 0);
    caster->data.UnitAbilities = &review_abilities;
    return caster;
}

static edict_t *review_thinker(edict_t *caster) {
    FILTER_EDICTS(ent, ent->owner == caster && ent->think) return ent;
    return NULL;
}

static UnitAbilities_t starfall_channel_abilities = { .abilList = "AEsf,AEtq" };
static char const starfall_channel_slk[] =
    "ID;PWXL;N;EBB;Y3;X9\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
    "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n"
    "C;Y2;X1;K\"AEsf\"\nC;Y2;X2;K\"AEsf\"\nC;Y2;X3;K1\n"
    "C;Y2;X4;K\"air,ground,structure,enemy,neutral\"\nC;Y2;X5;K5\nC;Y2;X6;K5\n"
    "C;Y2;X7;K150\nC;Y2;X8;K40\nC;Y2;X9;K1\n"
    "C;Y3;X1;K\"AEtq\"\nC;Y3;X2;K\"AEtq\"\nC;Y3;X3;K1\n"
    "C;Y3;X4;K\"air,ground,friend\"\nC;Y3;X5;K5\nC;Y3;X6;K5\n"
    "C;Y3;X7;K250\nC;Y3;X8;K20\nC;Y3;X9;K1\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *caster, *thinker;
    bool cast;
} starfallChannelFix_t;

static starfallChannelFix_t starfall_channel_setup(void) {
    starfallChannelFix_t fix = { 0 };
    uint32_t const code = FS_SLKKey("AEsf");
    fix.caster = review_setup();
    fix.rows = parse_slk_string(starfall_channel_slk);
    fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.caster->data.UnitAbilities = &starfall_channel_abilities;
    fix.caster->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);
    fix.caster->unit_state->ranks[1] = MAKE(heroability_t, .code = FS_SLKKey("AEtq"), .level = 1);
    fix.caster->unit_state->attack1.type = ATK_NORMAL;
    fix.caster->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    fix.caster->unit_state->attack1.damageBase = 10;
    fix.caster->unit_state->attack1.cooldown = 1.0f;
    fix.cast = S_CastNoTargetSpell(fix.caster, code);
    fix.thinker = review_thinker(fix.caster);
    return fix;
}

static void starfall_channel_done(starfallChannelFix_t fix) {
    S_SpellCancelChannel(fix.caster);
    if (fix.thinker && fix.thinker->inuse) G_RunEntity(fix.thinker);
    G_SetSLKRows("AbilityData", fix.old);
    free_slk_rows(fix.rows);
}

/* AHdr accepts enemy and friend targets. An enemy cast must still be blocked,
 * and the committed blocked cast must not leave its channel active. */
TEST(wc3_ability_lifecycle, spell_shield_blocks_hostile_mixed_target_channel) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    unit_addstatus(enemy, "BNss", 1);

    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    T_EQ(G_UnitStatusLevel(enemy, FS_SLKKey("BNss")), 0);
    T_ASSERT(!S_SpellIsChanneling(caster));
    T_NULL(review_thinker(caster));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stock AHfs starts burning promptly; DataA is damage per full-damage pulse, not a 15-second delay. */
TEST(wc3_ability_lifecycle, flame_strike_stock_data_burns_within_two_seconds) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2);
    edict_t *thinker = review_thinker(caster);
    level.time += 2000;
    if (thinker) G_RunEntity(thinker);
    float hp = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_NOT_NULL(thinker); T_ASSERT(hp < 1000);
}

TEST(wc3_ability_lifecycle, flame_strike_plays_warning_impact_field_and_damage_art) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    int (*old_model_index)(cstring_t) = gi.ModelIndex;
    edict_t *thinker, *field = NULL;
    uint32_t owned_fields = 0;
    edict_t *missile = NULL;

    flame_strike_model_count = 0;
    gi.ModelIndex = flame_strike_capture_model;
    caster->unit_state->attack1.projectile.model = 99;
    caster->unit_state->attack1.projectile.speed = 1000.0f;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    FILTER_EDICTS(candidate, candidate->owner == caster && candidate->movetype == MOVETYPE_FLYMISSILE)
        missile = candidate;
    T_NOT_NULL(missile);
    T_EQ(missile->s.model, 99);
    T_EQ(flame_strike_model_count, 0);
    FOR_LOOP(i, 20) {
        if (!missile->inuse) break;
        G_RunEntity(missile);
    }
    T_ASSERT(!missile->inuse);
    T_EQ(flame_strike_model_count, 1);
    T_STREQ(flame_strike_models[0], "TestUI\\Models\\flame-strike-warning.mdx");

    level.time = thinker->freetime;
    G_RunEntity(thinker);
    T_EQ(flame_strike_model_count, 4);
    T_STREQ(flame_strike_models[1], "TestUI\\Models\\flame-strike-impact-1.mdx");
    T_STREQ(flame_strike_models[2], "TestUI\\Models\\flame-strike-embers.mdx");
    T_STREQ(flame_strike_models[3], "TestUI\\Models\\flame-strike-damage.mdx");
    FILTER_EDICTS(effect, effect->owner == thinker && effect->s.model) {
        owned_fields++;
        field = effect;
    }
    T_EQ(owned_fields, 1);
    T_NOT_NULL(field);

    G_DestroyOwnedEffects(thinker);
    gi.ModelIndex = old_model_index;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Once a flame pulse runs, another server frame must not count as another full burn tick. */
TEST(wc3_ability_lifecycle, flame_strike_does_not_burn_every_server_frame) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2);
    edict_t *thinker = review_thinker(caster);
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
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2);
    edict_t *thinker = review_thinker(caster);
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
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    int (*old_model_index)(cstring_t) = gi.ModelIndex;
    blizzard_effect_model[0] = '\0';
    gi.ModelIndex = review_capture_model;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    edict_t *thinker = review_thinker(caster);
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
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
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
    edict_t *thinker = review_thinker(caster);
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

/* Area spells follow their level's EfctID when the ability row has no art/audio override. */
TEST(wc3_ability_lifecycle, area_spell_presentation_resolves_authored_effect_object) {
    static char const ability_slk[] =
        "ID;PWXL;N;E\n"
        "B;X8;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\n"
        "C;Y1;X6;K\"Area1\"\nC;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"EfctID1\"\n"
        "C;Y2;X1;K\"AOeq\"\nC;Y2;X2;K\"AOeq\"\n"
        "C;Y2;X3;K\"ground,structure\"\nC;Y2;X4;K\"3\"\n"
        "C;Y2;X5;K\"3\"\nC;Y2;X6;K\"128\"\n"
        "C;Y2;X7;K\"BOeq\"\nC;Y2;X8;K\"X001\"\nE\n";
    static char const effect_slk[] =
        "ID;PWXL;N;E\n"
        "B;X3;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"EffectArt\"\n"
        "C;Y1;X3;K\"Effectsoundlooped\"\nC;Y2;X1;K\"X001\"\n"
        "C;Y2;X2;K\"TestUI\\Models\\earthquake.mdx\"\n"
        "C;Y2;X3;K\"EarthquakeTestLoop\"\nE\n";
    static char const sound_slk[] =
        "ID;PWXL;N;E\n"
        "B;X4;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\nC;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\nC;Y1;X4;K\"Volume\"\n"
        "C;Y2;X1;K\"EarthquakeTestLoop\"\nC;Y2;X2;K\"quake-loop.wav\"\n"
        "C;Y2;X3;K\"TestUI\\Sounds\\\"\nC;Y2;X4;K\"127\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOeq" };
    slkTestData_t *ability_rows = parse_slk_string(ability_slk);
    slkTestData_t *effect_rows = parse_slk_string(effect_slk);
    slkTestData_t *sound_rows = parse_slk_string(sound_slk);
    slkTestData_t *old_ability = G_SetSLKRows("AbilityData", ability_rows);
    slkTestData_t *old_effect = G_SetSLKRows("AbilityBuffData", effect_rows);
    slkTestData_t *old_sound_rows = G_SetSLKRows("AbilitySounds", sound_rows);
    int (*old_model_index)(cstring_t) = gi.ModelIndex;
    int (*old_sound_index)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;
    edict_t *caster = review_setup();
    edict_t *thinker, *effect = NULL;

    caster->data.UnitAbilities = &abilities;
    blizzard_effect_model[0] = '\0'; blizzard_sound_path[0] = '\0';
    gi.ModelIndex = review_capture_model;
    gi.SoundIndex = review_capture_sound_index; gi.SoundIndexAlias = review_capture_sound_index_alias;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AOeq"), &caster->s.origin2));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    FILTER_EDICTS(candidate, candidate->owner == thinker && candidate->s.model) effect = candidate;
    T_NOT_NULL(effect);
    T_STREQ(blizzard_effect_model, "TestUI\\Models\\earthquake.mdx");
    T_ASSERT(effect->s.sound != 0);
    T_STREQ(blizzard_sound_path, "TestUI\\Sounds\\quake-loop.wav");

    gi.ModelIndex = old_model_index; gi.SoundIndex = old_sound_index; gi.SoundIndexAlias = old_sound_alias;
    S_SpellCancelChannel(caster);
    G_SetSLKRows("AbilitySounds", old_sound_rows); free_slk_rows(sound_rows);
    G_SetSLKRows("AbilityBuffData", old_effect); free_slk_rows(effect_rows);
    G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}

/* RoC 1.0 authors Tranquility's rain loop on XEtq without an AEtq EfctID. */
TEST(wc3_ability_lifecycle, roc_tranquility_resolves_rain_effect_object) {
    static char const ability_slk[] =
        "ID;PWXL;N;E\nB;X8;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"Area1\"\n"
        "C;Y2;X1;K\"AEtq\"\nC;Y2;X2;K\"AEtq\"\nC;Y2;X3;K\"ground,friend,self\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"800\"\nE\n";
    static char const effect_slk[] =
        "ID;PWXL;N;E\nB;X3;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"EffectArt\"\nC;Y1;X3;K\"Effectsoundlooped\"\n"
        "C;Y2;X1;K\"XEtq\"\nC;Y2;X2;K\"TestUI\\Models\\tranquility-rain.mdx\"\n"
        "C;Y2;X3;K\"TranquilityTestLoop\"\nE\n";
    static char const sound_slk[] =
        "ID;PWXL;N;E\nB;X4;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\nC;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\nC;Y1;X4;K\"Volume\"\n"
        "C;Y2;X1;K\"TranquilityTestLoop\"\nC;Y2;X2;K\"tranquility-loop.wav\"\n"
        "C;Y2;X3;K\"TestUI\\Sounds\\\"\nC;Y2;X4;K\"127\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AEtq" };
    slkTestData_t *ability_rows = parse_slk_string(ability_slk);
    slkTestData_t *effect_rows = parse_slk_string(effect_slk);
    slkTestData_t *sound_rows = parse_slk_string(sound_slk);
    slkTestData_t *old_ability = G_SetSLKRows("AbilityData", ability_rows);
    slkTestData_t *old_effect = G_SetSLKRows("AbilityBuffData", effect_rows);
    slkTestData_t *old_sound_rows = G_SetSLKRows("AbilitySounds", sound_rows);
    int (*old_model_index)(cstring_t) = gi.ModelIndex;
    int (*old_sound_index)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;
    edict_t *caster = review_setup();
    edict_t *effect = NULL;

    caster->data.UnitAbilities = &abilities;
    blizzard_effect_model[0] = '\0'; blizzard_sound_path[0] = '\0';
    gi.ModelIndex = review_capture_model;
    gi.SoundIndex = review_capture_sound_index; gi.SoundIndexAlias = review_capture_sound_index_alias;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AEtq")));
    FILTER_EDICTS(candidate, candidate->inuse && candidate->owner && candidate->owner->inuse &&
                  candidate->owner->owner == caster && candidate->s.model)
        effect = candidate;
#if BZ_COMPAT_WORKAROUNDS
    T_NOT_NULL(effect);
    T_STREQ(blizzard_effect_model, "TestUI\\Models\\tranquility-rain.mdx");
    if (effect) T_ASSERT(effect->s.sound != 0);
    T_STREQ(blizzard_sound_path, "TestUI\\Sounds\\tranquility-loop.wav");

#else
    /* Strict mode deliberately does not infer a missing EfctID. */
    T_NULL(effect);
#endif
    gi.ModelIndex = old_model_index; gi.SoundIndex = old_sound_index; gi.SoundIndexAlias = old_sound_alias;
    S_SpellCancelChannel(caster);
    G_SetSLKRows("AbilitySounds", old_sound_rows); free_slk_rows(sound_rows);
    G_SetSLKRows("AbilityBuffData", old_effect); free_slk_rows(effect_rows);
    G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}

/* RoC 1.0 authors Starfall's persistent rain art on XEsf without AEsf EfctID. */
TEST(wc3_ability_lifecycle, roc_starfall_resolves_rain_effect_object) {
    static char const ability_slk[] =
        "ID;PWXL;N;E\nB;X9;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"Area1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AEsf\"\nC;Y2;X2;K\"AEsf\"\nC;Y2;X3;K\"air,ground,enemy\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"800\"\n"
        "C;Y2;X7;K\"30\"\nC;Y2;X8;K\"1\"\nC;Y2;X9;K\"AEsd\"\nE\n";
    static char const effect_slk[] =
        "ID;PWXL;N;E\nB;X2;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"EffectArt\"\n"
        "C;Y2;X1;K\"XEsf\"\nC;Y2;X2;K\"TestUI\\Models\\starfall-rain.mdx\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AEsf" };
    slkTestData_t *ability_rows = parse_slk_string(ability_slk);
    slkTestData_t *effect_rows = parse_slk_string(effect_slk);
    slkTestData_t *old_ability = G_SetSLKRows("AbilityData", ability_rows);
    slkTestData_t *old_effect = G_SetSLKRows("AbilityBuffData", effect_rows);
    int (*old_model_index)(cstring_t) = gi.ModelIndex;
    edict_t *caster = review_setup();
    edict_t *effect = NULL;

    caster->data.UnitAbilities = &abilities;
    blizzard_effect_model[0] = '\0';
    gi.ModelIndex = review_capture_model;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AEsf")));
    FILTER_EDICTS(candidate, candidate->owner && candidate->owner->owner == caster && candidate->s.model)
        effect = candidate;
#if BZ_COMPAT_WORKAROUNDS
    T_NOT_NULL(effect);
    T_STREQ(blizzard_effect_model, "TestUI\\Models\\starfall-rain.mdx");

#else
    /* Strict mode deliberately does not infer a missing EfctID. */
    T_NULL(effect);
#endif
    gi.ModelIndex = old_model_index;
    S_SpellCancelChannel(caster);
    G_SetSLKRows("AbilityBuffData", old_effect); free_slk_rows(effect_rows);
    G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}

/* Warsmash shows a Blizzard shard wave first and resolves that wave's damage
 * 0.8 seconds later; Cast owns the delay before the first shard wave. */
TEST(wc3_ability_lifecycle, blizzard_shards_precede_damage_by_eight_tenths) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    edict_t *thinker = review_thinker(caster);
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
    cstring_t path = Test_TempPath("blizzard-phase-save.bin");
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    edict_t *thinker = review_thinker(caster);
    level.time = thinker->freetime; G_RunEntity(thinker); /* shards */
    T_FEQ(enemy->health.value, 1000, .001f);
    T_ASSERT(WriteGame(path));
    G_AddChannel(caster);
    G_GetChannel(caster)->code = 0; thinker->think = NULL; thinker->variation = 1;
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
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100), *building = review_unit(1, 120);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    building->targtype = TARG_STRUCTURE;
    building->s.flags |= EF_BUILDING;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    edict_t *thinker = review_thinker(caster);
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
    edict_t *caster = review_setup();
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    uint32_t count = 0;
    vec2_t first = {0};
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

#if BZ_COMPAT_WORKAROUNDS
/* RoC 1.0 stores the Water Elemental unit rawcode in DataA's fourcc slot. */
TEST(wc3_ability_lifecycle, water_elemental_recovers_roc_dataa_unit_id) {
    edict_t *caster = review_setup();
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    uint32_t count = 0;
    abilityLevel_t const *row;

    row = (abilityLevel_t *)G_AbilityLevel(FS_SLKKey("AHwe"), 1);
    ((abilityLevel_t *)row)->data[0].id = MAKEFOURCC('h','f','o','o');
    ((abilityLevel_t *)row)->data[0].number = 0;
    ((abilityLevel_t *)row)->unitID = 0;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AHwe")));
    FILTER_EDICTS(unit, unit->inuse && unit->owner == caster &&
                  unit->summon_ability == FS_SLKKey("AHwe") &&
                  unit->class_id == MAKEFOURCC('h','f','o','o')) count++;
    T_EQ(count, 1);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_ability_lifecycle, priestess_scout_missing_unit_id_uses_roc_owl_fallback) {
    static UnitAbilities_t scout_abilities = { .abilList = "AEst" };
    static char const ability_slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Rng1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"UnitID1\"\n"
        "C;Y2;X1;K\"AEst\"\nC;Y2;X2;K\"AEst\"\nC;Y2;X3;K\"_\"\n"
        "C;Y2;X4;K50\nC;Y2;X5;K200\nC;Y2;X6;K60\n"
        "C;Y2;X7;K1\nE\n";
    static char const unit_ui_slk[] =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"unitUIID\"\nC;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"modelScale\"\nC;Y1;X4;K\"scale\"\n"
        "C;Y2;X1;K\"nowl\"\nC;Y2;X2;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\n"
        "C;Y2;X3;K1\nC;Y2;X4;K1\nE\n";
    static char const unit_data_slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"targType\"\nC;Y1;X3;K\"movetp\"\n"
        "C;Y1;X4;K\"collision\"\nC;Y1;X5;K\"propWin\"\n"
        "C;Y2;X1;K\"nowl\"\nC;Y2;X2;K\"air\"\nC;Y2;X3;K\"fly\"\n"
        "C;Y2;X4;K16\nC;Y2;X5;K60\nE\n";
    static char const unit_balance_slk[] =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"realHP\"\n"
        "C;Y2;X1;K\"nowl\"\nC;Y2;X2;K150\nE\n";
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui;
    slkTestData_t *data_rows, *old_data, *balance_rows, *old_balance;
    edict_t *caster;
    uint32_t owl_count = 0;

    reset_entities();
    setup_test_world();
    level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ability_rows = parse_slk_string(ability_slk); old_ability = G_SetSLKRows("AbilityData", ability_rows);
    ui_rows = parse_slk_string(unit_ui_slk); old_ui = G_SetSLKRows("UnitUI", ui_rows);
    data_rows = parse_slk_string(unit_data_slk); old_data = G_SetSLKRows("UnitData", data_rows);
    balance_rows = parse_slk_string(unit_balance_slk); old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    caster = review_unit(0, 0);
    caster->data.UnitAbilities = &scout_abilities;

    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AEst")));
    FILTER_EDICTS(unit, unit->inuse && unit->owner == caster &&
                  unit->summon_ability == FS_SLKKey("AEst") &&
                  unit->class_id == MAKEFOURCC('n','o','w','l')) {
        T_EQ(unit->s.player, caster->s.player);
        T_EQ(G_UnitStatusLevel(unit, MAKEFOURCC('B','T','L','F')), 1);
        T_ASSERT(Vector2_distance(&unit->s.origin2, &caster->s.origin2) <= 128.0f);
        owl_count++;
    }
#if BZ_COMPAT_WORKAROUNDS
    T_EQ(owl_count, 1);
#else
    T_EQ(owl_count, 0);
#endif

    reset_entities();
    G_SetSLKRows("UnitBalance", old_balance); free_slk_rows(balance_rows);
    G_SetSLKRows("UnitData", old_data); free_slk_rows(data_rows);
    G_SetSLKRows("UnitUI", old_ui); free_slk_rows(ui_rows);
    G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}
#endif

/* Mass Teleport is a real channel: DataB delays the relocation, the caster
 * counts against DataA, and only the caster player's mobile units are moved. */
TEST(wc3_ability_lifecycle, mass_teleport_delays_caps_and_excludes_allies_and_structures) {
    edict_t *caster = review_setup(), *own1 = review_unit(0, 100), *ally = review_unit(1, 150);
    edict_t *building = review_unit(0, 200), *own2 = review_unit(0, 250), *own3 = review_unit(0, 300);
    edict_t *target = review_unit(0, 2000);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    uint32_t moved = 0;
    level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;
    building->targtype = TARG_STRUCTURE;
    building->s.flags |= EF_BUILDING;

    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHmt"), target));
    edict_t *thinker = review_thinker(caster);
    T_NOT_NULL(thinker); T_ASSERT(G_UnitHasFlag(target, UNIT_FLAG_PAUSED)); T_FEQ(caster->s.origin2.x, 0, .001f);
    level.time = thinker->freetime - 1; G_RunEntity(thinker);
    T_FEQ(caster->s.origin2.x, 0, .001f);
    level.time = thinker->freetime; G_RunEntity(thinker);

    moved += own1->s.origin2.x > 1000;
    moved += own2->s.origin2.x > 1000;
    moved += own3->s.origin2.x > 1000;
    T_EQ(moved, 2);
    T_ASSERT(caster->s.origin2.x > 1000);
    T_FEQ(caster->s.origin.x, caster->s.origin2.x, 0.001f);
    T_FEQ(caster->s.origin.y, caster->s.origin2.y, 0.001f);
    T_ASSERT(ally->s.origin2.x < 1000);
    T_ASSERT(building->s.origin2.x < 1000);
    /* Relocation must also update the server broad phase: the caster vacated
     * x=0, so an unmoved unit can legally occupy that point after completion. */
    T_ASSERT(G_CanRepositionUnitAt(own3, &MAKE(vec2_t, 0, 0)));
    T_ASSERT(!G_UnitHasFlag(target, UNIT_FLAG_PAUSED)); T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0); T_ASSERT(!thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Structure is a first-class target-mask token; cancelling the channel must
 * also undo Mass Teleport's temporary destination pause. */
TEST(wc3_ability_lifecycle, mass_teleport_accepts_structure_target_and_cleans_cancel) {
    edict_t *caster = review_setup(), *target = review_unit(0, 2000);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    target->targtype = TARG_STRUCTURE;
    target->s.flags |= EF_BUILDING;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHmt"), target));
    edict_t *thinker = review_thinker(caster);
    T_NOT_NULL(thinker); T_ASSERT(G_UnitHasFlag(target, UNIT_FLAG_PAUSED));
    S_SpellCancelChannel(caster);
    T_ASSERT(!G_UnitHasFlag(target, UNIT_FLAG_PAUSED)); T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0);
    if (thinker && thinker->inuse) G_RunEntity(thinker);
    T_ASSERT(!thinker || !thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Cancellation cleans up immediately, but the old thinker may remain until
 * its scheduled tick. That late cleanup must not release a recast's pause. */
TEST(wc3_ability_lifecycle, mass_teleport_old_thinker_preserves_recast_destination_pause) {
    edict_t *caster = review_setup(), *target = review_unit(0, 2000);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *retired, *replacement;
    uint32_t code = FS_SLKKey("AHmt");
    T_ASSERT(S_CastUnitTargetSpell(caster, code, target));
    retired = review_thinker(caster);
    T_NOT_NULL(retired);
    T_ASSERT(G_UnitHasFlag(target, UNIT_FLAG_PAUSED));
    S_SpellCancelChannel(caster);
    T_ASSERT(!G_UnitHasFlag(target, UNIT_FLAG_PAUSED));
    /* Model an immediately restarted channel without bypassing the real
     * channel thinker identity capture or running an unrelated cooldown. */
    G_GetChannel(caster)->serial++;
    G_GetChannel(caster)->code = code;
    replacement = S_SpellChannelTargetThinker(caster, code, target);
    T_NOT_NULL(replacement);
    replacement->think = mass_teleport_think;
    replacement->wait = 0.0f;
    replacement->freetime = G_Time() + 5000;
    G_SetUnitFlag(target, UNIT_FLAG_PAUSED, true);
    if (retired->inuse) G_RunEntity(retired);
    T_ASSERT(G_UnitHasFlag(target, UNIT_FLAG_PAUSED));
    S_SpellCancelChannel(caster);
    T_ASSERT(!G_UnitHasFlag(target, UNIT_FLAG_PAUSED));
    if (replacement->inuse) G_RunEntity(replacement);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The destination entity stays authoritative until completion; losing it
 * cancels the channel instead of teleporting to a stale cached coordinate. */
TEST(wc3_ability_lifecycle, mass_teleport_target_death_cancels) {
    edict_t *caster = review_setup(), *target = review_unit(0, 2000);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHmt"), target));
    edict_t *thinker = review_thinker(caster);
    target->health.value = 0;
    G_RunEntity(thinker);
    T_FEQ(caster->s.origin2.x, 0, .001f);
    T_ASSERT(!G_UnitHasFlag(target, UNIT_FLAG_PAUSED)); T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0); T_ASSERT(!thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A live Mass Teleport thinker must retain its callback, target incarnation,
 * pause ownership and deadline across the production save/load path. */
TEST(wc3_ability_lifecycle, mass_teleport_continues_after_save_load) {
    cstring_t path = Test_TempPath("mass-teleport-save.bin");
    edict_t *caster = review_setup(), *target = review_unit(0, 2000);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHmt"), target));
    edict_t *thinker = review_thinker(caster);
    T_NOT_NULL(thinker); T_ASSERT(G_UnitHasFlag(target, UNIT_FLAG_PAUSED)); T_ASSERT(WriteGame(path));
    G_AddChannel(caster);
    G_GetChannel(caster)->code = 0; thinker->think = NULL; G_SetUnitFlag(target, UNIT_FLAG_PAUSED, false);
    T_ASSERT(ReadGame(path));
    T_ASSERT(thinker->think == mass_teleport_think); T_ASSERT(G_UnitHasFlag(target, UNIT_FLAG_PAUSED));
    level.time = thinker->freetime; G_RunEntity(thinker);
    T_ASSERT(caster->s.origin2.x > 1000); T_ASSERT(!G_UnitHasFlag(target, UNIT_FLAG_PAUSED)); T_ASSERT(!thinker->inuse);
    remove(path); G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Cancelling the caster's channel must retire its pending resource transfer too. */
TEST(wc3_ability_lifecycle, siphon_mana_stops_after_movement_cancel) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy);
    edict_t *thinker = review_thinker(caster);
    caster->s.origin2.x += 10; spell_run_frame(caster);
    uint32_t channel = G_GetChannel(caster)->code;
    level.time += 1000;
    if (thinker) G_RunEntity(thinker);
    float mana = enemy->mana.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_EQ(channel, 0); T_FEQ(mana, 100, .001f);
}

/* Casting through the real channel thinker must transfer over the ordinary
 * max, rather than silently destroying enemy mana at the normal cap. */
TEST(wc3_ability_lifecycle, siphon_mana_overcharges_from_enemy) {
    char slk[sizeof(review_slk)];
    memcpy(slk, review_slk, sizeof(slk));
    char *cell = strstr(slk, "C;Y4;X10;K\"15\"\n");
    T_NOT_NULL(cell);
    if (cell) cell[strlen("C;Y4;X10;K\"") + 1] = '7'; /* Non-stock DataB = 17 mana/sec. */
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    caster->mana.value = caster->mana.max_value = 100;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    edict_t *thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    level.time += 1000; G_RunEntity(thinker);
    T_FEQ(caster->mana.max_value, 100.0f, .001f);
    T_FEQ(caster->mana.value, 117.0f, .001f);
    T_FEQ(enemy->mana.value, 83.0f, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A channel at the 200% limit cannot remove mana that the caster cannot hold. */
TEST(wc3_ability_lifecycle, siphon_mana_caps_at_200_percent_without_wasting_target_mana) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    caster->mana.max_value = 100; caster->mana.value = 195;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    edict_t *thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    level.time += 1000; G_RunEntity(thinker);
    T_FEQ(caster->mana.value, 200.0f, .001f);
    T_FEQ(enemy->mana.value, 95.0f, .001f);
    level.time += 1000; G_RunEntity(thinker);
    T_FEQ(caster->mana.value, 200.0f, .001f);
    T_FEQ(enemy->mana.value, 95.0f, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Excess mana does not leak while actively channeling; a real Stop order
 * releases the lock and starts decay, which ends exactly at normal max. */
TEST(wc3_ability_lifecycle, siphon_mana_overcharge_decays_after_stop) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    caster->mana.max_value = 100; caster->mana.value = 101;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    T_ASSERT(S_SpellIsChanneling(caster));
    G_RunEntity(caster);
    T_FEQ(caster->mana.value, 101.0f, .001f);
    T_ASSERT(unit_issueimmediateorder(caster, "stop"));
    T_ASSERT(!S_SpellIsChanneling(caster));
    G_RunEntity(caster);
    T_FEQ(caster->mana.value, 100.7f, .001f);
    FOR_LOOP(i, 3) G_RunEntity(caster);
    T_FEQ(caster->mana.value, 100.0f, .001f);
    T_EQ(caster->s.stats[ENT_MANA], 255);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_ability_lifecycle, siphon_mana_overcharge_decay_uses_authored_data) {
    char slk[sizeof(review_slk)];
    memcpy(slk, review_slk, sizeof(slk));
    char *cell = strstr(slk, "C;Y4;X18;K\"3\"\n");
    T_NOT_NULL(cell);
    if (cell) cell[strlen("C;Y4;X18;K\"")] = '5';
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = review_setup();
    caster->mana.max_value = 100; caster->mana.value = 101;
    T_FEQ(S_ManaExcessDecayRate(caster), 5.0f, .001f);
    G_RunEntity(caster);
    T_FEQ(caster->mana.value, 100.5f, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Other channelled spells, including Life Drain, must not suspend decay. */
TEST(wc3_ability_lifecycle, life_drain_channel_does_not_freeze_siphon_overcharge) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    caster->mana.max_value = 100; caster->mana.value = 105;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), enemy));
    T_ASSERT(S_SpellIsChanneling(caster));
    G_RunEntity(caster);
    T_FEQ(caster->mana.value, 104.7f, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Cap modifiers preserve the overcharge fraction across equip/unequip rather
 * than clamping the 150/100 state to 100 on the first stat recalculation. */
TEST(wc3_ability_lifecycle, siphon_overcharge_survives_temporary_mana_cap_change) {
    edict_t *caster = review_setup();
    caster->mana.max_value = 100; caster->mana.value = 150;
    G_ApplyTemporaryMaxManaBonus(caster, 50);
    T_FEQ(caster->mana.max_value, 150, .001f);
    T_FEQ(caster->mana.value, 225, .001f);
    G_ApplyTemporaryMaxManaBonus(caster, -50);
    T_FEQ(caster->mana.max_value, 100, .001f);
    T_FEQ(caster->mana.value, 150, .001f);
}

/* ANdr's authored DataA drains life even when the target has no mana pool. */
TEST(wc3_ability_lifecycle, life_drain_transfers_health_from_manaless_target) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    caster->health.value = 500; enemy->mana.value = enemy->mana.max_value = 0;
    bool cast = S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), enemy);
    edict_t *thinker = review_thinker(caster);
    level.time += 1000;
    if (thinker) G_RunEntity(thinker);
    float hp = enemy->health.value, healed = caster->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_FEQ(hp, 970, .001f); T_FEQ(healed, 530, .001f);
}

/* Life Drain ends when its recipient is full, transfers only the remaining
 * health needed, and leaves ordinary movement available after completion. */
TEST(wc3_ability_lifecycle, life_drain_ends_at_full_health_and_allows_movement) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    vec2_t destination = { 300.0f, 0.0f };
    edict_t *thinker;
    caster->health.value = 990.0f;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), enemy));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    T_ASSERT(S_SpellIsChanneling(caster));
    level.time += 1000;
    if (thinker) G_RunEntity(thinker);
    T_FEQ(caster->health.value, 1000.0f, .001f);
    T_FEQ(enemy->health.value, 990.0f, .001f);
    T_ASSERT(!S_SpellIsChanneling(caster));
    T_ASSERT(!thinker || !thinker->inuse);
    order_move(caster, Waypoint_add(&destination));
    T_FEQ(caster->goalentity->s.origin2.x, destination.x, .001f);
    T_ASSERT(caster->currentmove && caster->currentmove->think != ai_stand);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A player Move order interrupts the live Life Drain channel immediately and
 * retires its beam thinker on the next simulation update. */
TEST(wc3_ability_lifecycle, life_drain_movement_order_cancels_channel) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    vec2_t destination = { 300.0f, 0.0f };
    edict_t *thinker, *waypoint;
    caster->health.value = 900.0f;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), enemy));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    T_ASSERT(S_SpellIsChanneling(caster));
    waypoint = Waypoint_add(&destination);
    T_NOT_NULL(waypoint);
    order_move(caster, waypoint);
    T_ASSERT(!S_SpellIsChanneling(caster));
    T_ASSERT(caster->goalentity == waypoint);
    T_ASSERT(caster->currentmove && caster->currentmove->think != ai_stand);
    if (thinker && thinker->inuse) G_RunEntity(thinker);
    T_ASSERT(!thinker || !thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Natural duration completion must release the channel lock just like the
 * full-health condition, so the next player Move order can take effect. */
TEST(wc3_ability_lifecycle, life_drain_duration_completion_allows_movement) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    vec2_t destination = { 300.0f, 0.0f };
    edict_t *thinker;
    caster->health.value = 900.0f;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), enemy));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    T_ASSERT(S_SpellIsChanneling(caster));
    level.time += 8000;
    if (thinker) G_RunEntity(thinker);
    T_ASSERT(!S_SpellIsChanneling(caster));
    T_ASSERT(!thinker || !thinker->inuse);
    order_move(caster, Waypoint_add(&destination));
    T_FEQ(caster->goalentity->s.origin2.x, destination.x, .001f);
    T_ASSERT(caster->currentmove && caster->currentmove->think != ai_stand);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The channel's next pulse must not drain either resource after a stun. */
TEST(wc3_ability_lifecycle, siphon_mana_stun_before_tick_preserves_resources) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    edict_t *thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    float target_mana = enemy->mana.value, caster_mana = caster->mana.value;
    T_ASSERT(unit_addtimedstatus(caster, "Bstu", 1, 2.0f));
    level.time += 1000;
    if (thinker->inuse) G_RunEntity(thinker);
    T_ASSERT(!thinker->inuse);
    T_FEQ(enemy->mana.value, target_mana, .001f);
    T_FEQ(caster->mana.value, caster_mana, .001f);
    T_ASSERT(!G_GetChannel(caster) || !G_GetChannel(caster)->code);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Keep the standard neutral-drain fixture unchanged. These tests explicitly
 * enable Life Drain DataB so a missing mid-pulse guard is observable. */
static slkTestData_t *review_life_drain_with_mana_data(void) {
    /* parse_slk_string retains its input until free_slk_rows(), so this
     * modified SLK fixture must outlive the helper call. */
    static char fixture[sizeof(review_slk)];
    char *cell;
    memcpy(fixture, review_slk, sizeof(fixture));
    cell = strstr(fixture, "C;Y5;X10;K\"0\"\n");
    T_NOT_NULL(cell);
    if (!cell) return NULL;
    /* Replace one digit in the Life Drain DataB1 cell (0 -> 7). */
    cell[strlen("C;Y5;X10;K\"")] = '7';
    return parse_slk_string(fixture);
}

/* A live Life Drain pulse must use its nonzero mana component. This makes
 * the fatal-pulse guard test meaningful rather than vacuously passing. */
TEST(wc3_ability_lifecycle, life_drain_nonfatal_pulse_transfers_authored_mana) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = review_life_drain_with_mana_data(), *old = G_SetSLKRows("AbilityData", rows);
    float before_target, before_caster;
    caster->mana.value = 20;
    enemy->mana.value = 90;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), enemy));
    edict_t *thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    before_target = enemy->mana.value;
    before_caster = caster->mana.value;
    level.time += 1000;
    if (thinker->inuse) G_RunEntity(thinker);
    T_ASSERT(!M_IsDead(enemy));
    T_ASSERT(enemy->mana.value < before_target);
    T_ASSERT(caster->mana.value > before_caster);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A fatal Life Drain health pulse must not also transfer mana afterwards. */
TEST(wc3_ability_lifecycle, life_drain_lethal_pulse_does_not_drain_mana) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = review_life_drain_with_mana_data(), *old = G_SetSLKRows("AbilityData", rows);
    enemy->health.value = 10;
    caster->mana.value = 20;
    enemy->mana.value = 90;
    enemy->die = unit_die;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), enemy));
    edict_t *thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    if (!thinker) {
        G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
        return;
    }
    float before_caster = caster->mana.value;
    level.time += 1000;
    if (thinker->inuse) G_RunEntity(thinker);
    T_ASSERT(M_IsDead(enemy));
    T_FEQ(enemy->mana.value, 90, .001f);
    T_FEQ(caster->mana.value, before_caster, .001f);
    T_ASSERT(!thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The real Life Drain cast owns a single DRAL ribbon, two persistent
 * chest attachments, and the authored looping sound.  No game installation
 * is needed: the test archive supplies spell/buff art and a sound row. */
TEST(wc3_ability_lifecycle, life_drain_attaches_art_beam_and_looped_sound) {
    edict_t *caster = review_setup(), *victim = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    int (*old_alias)(cstring_t, cstring_t) = gi.SoundIndexAlias;
    gLightning_t *beam = NULL;
    edict_t *thinker;
    uint32_t visuals = 0;
    uint32_t const model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");

    gi.SoundIndexAlias = review_capture_sound_index_alias;
    memset(level.lightning_effects, 0, sizeof(level.lightning_effects));
    T_EQ(G_AbilityLightningId(FS_SLKKey("ANdr"), 1), MAKEFOURCC('D','R','A','L'));
    T_ASSERT(model != 0);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), victim));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    if (thinker) {
        T_NE(thinker->variation, 0u);
        T_EQ(thinker->s.sound, 91);
        FILTER_EDICTS(effect, effect->owner == thinker && effect->s.model == model) {
            T_ASSERT(effect->goalentity == caster || effect->goalentity == victim);
            T_ASSERT(effect->s.flags & EF_NAMED_ATTACHMENT);
            T_EQ(effect->movetype, MOVETYPE_LINK);
            visuals++;
        }
        T_EQ(visuals, 2u);
        FOR_LOOP(i, MAX_LIGHTNING_EFFECTS) {
            gLightning_t *candidate = &level.lightning_effects[i];
            if (candidate->inuse && candidate->state.handle == thinker->variation) beam = candidate;
        }
        T_NOT_NULL(beam);
        if (beam) {
            T_EQ(beam->state.effect_id, MAKEFOURCC('D','R','A','L'));
            T_EQ(beam->state.end_time, 0u);
            T_ASSERT(beam->source_entity == caster && beam->target_entity == victim);
            victim->s.origin.x += 55.0f;
            G_LightningUpdateAttached(beam);
            T_FEQ(beam->state.target.x, victim->s.origin.x, 0.001f);
        }
        T_STREQ(blizzard_sound_path, "TestUI\\Sounds\\LifeDrain.wav");
        T_ASSERT(unit_issueimmediateorder(caster, "stop"));
        G_RunEntity(thinker);
        T_ASSERT(!thinker->inuse);
        if (beam) T_ASSERT(!beam->inuse);
        FILTER_EDICTS(effect, effect->s.model == model &&
                      (effect->goalentity == caster || effect->goalentity == victim)) {
            T_ASSERT(effect->owner != thinker);
        }
    }
    gi.SoundIndexAlias = old_alias;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Beam retirement follows the old thinker's unique handle, not the endpoints.
 * Recasting between cancellation and cleanup must keep the new beam alive. */
TEST(wc3_ability_lifecycle, life_drain_recast_retains_only_new_beam) {
    edict_t *caster = review_setup(), *victim = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *first, *second;
    uint32_t first_id, second_id;
    uint32_t count = 0;

    memset(level.lightning_effects, 0, sizeof(level.lightning_effects));
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), victim));
    first = review_thinker(caster);
    T_NOT_NULL(first);
    if (!first) goto done;
    first_id = first->variation;
    T_NE(first_id, 0u);
    T_ASSERT(unit_issueimmediateorder(caster, "stop"));
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), victim));
    second = NULL;
    FILTER_EDICTS(candidate, candidate != first && candidate->owner == caster &&
                  candidate->think == siphon_mana_think) { second = candidate; break; }
    T_NOT_NULL(second);
    if (!second) goto done;
    second_id = second->variation;
    T_NE(second_id, first_id);
    G_RunEntity(first);
    T_ASSERT(!first->inuse);
    T_ASSERT(second->inuse);
    FOR_LOOP(i, MAX_LIGHTNING_EFFECTS) {
        gLightning_t *beam = &level.lightning_effects[i];
        if (!beam->inuse) continue;
        T_NE(beam->state.handle, first_id);
        T_EQ(beam->state.handle, second_id);
        count++;
    }
    T_EQ(count, 1u);
    T_ASSERT(unit_issueimmediateorder(caster, "stop"));
    G_RunEntity(second);
    T_ASSERT(!second->inuse);
    FOR_LOOP(i, MAX_LIGHTNING_EFFECTS) T_ASSERT(!level.lightning_effects[i].inuse);

done:
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Losing the victim or stunning the caster must retire the same persistent
 * presentation; otherwise a dead target could leave a permanent beam. */
TEST(wc3_ability_lifecycle, life_drain_victim_death_and_stun_retire_beam) {
    FOR_LOOP(reason, 2) {
        edict_t *caster = review_setup(), *victim = review_unit(1, 100);
        slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
        edict_t *thinker;
        uint32_t beam_handle;
        bool beam_left = false;
        memset(level.lightning_effects, 0, sizeof(level.lightning_effects));
        T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), victim));
        thinker = review_thinker(caster);
        T_NOT_NULL(thinker);
        if (!thinker) {
            G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
            continue;
        }
        beam_handle = thinker->variation;
        T_NE(beam_handle, 0u);
        if (!reason) victim->health.value = 0;
        else T_ASSERT(unit_addtimedstatus(caster, "Bstu", 1, 2.0f));
        G_RunEntity(thinker);
        T_ASSERT(!thinker->inuse);
        FOR_LOOP(i, MAX_LIGHTNING_EFFECTS)
            if (level.lightning_effects[i].inuse &&
                level.lightning_effects[i].state.handle == beam_handle) beam_left = true;
        T_ASSERT(!beam_left);
        G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    }
}

/* The thinker's variation field and lightning registry were already saved;
 * a channel loaded mid-effect can clean up by the original handle. */
TEST(wc3_ability_lifecycle, life_drain_visual_identity_survives_save_and_cleanup) {
    cstring_t filename = Test_TempPath("wc3-life-drain-beam.bin");
    edict_t *caster = review_setup(), *victim = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *thinker;
    uint32_t beam_handle;
    bool found = false;

    memset(level.lightning_effects, 0, sizeof(level.lightning_effects));
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), victim));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    if (!thinker) goto done;
    beam_handle = thinker->variation;
    T_NE(beam_handle, 0u);
    T_ASSERT(WriteGame(filename));
    thinker->variation = 0;
    memset(level.lightning_effects, 0, sizeof(level.lightning_effects));
    T_ASSERT(ReadGame(filename));
    T_EQ(thinker->variation, beam_handle);
    FOR_LOOP(i, MAX_LIGHTNING_EFFECTS)
        if (level.lightning_effects[i].inuse &&
            level.lightning_effects[i].state.handle == beam_handle) found = true;
    T_ASSERT(found);
    T_ASSERT(unit_issueimmediateorder(caster, "stop"));
    G_RunEntity(thinker);
    T_ASSERT(!thinker->inuse);
    FOR_LOOP(i, MAX_LIGHTNING_EFFECTS) T_ASSERT(!level.lightning_effects[i].inuse);

done:
    remove(filename);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Tranquility is AB_CHANNEL even though it needs no target-selection click. */
TEST(wc3_ability_lifecycle, no_target_tranquility_establishes_channel) {
    edict_t *caster = review_setup();
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastNoTargetSpell(caster, FS_SLKKey("AEtq"));
    uint32_t channel = G_GetChannel(caster)->code;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_EQ(channel, FS_SLKKey("AEtq"));
}

/* Starfall damages every enemy in its authored radius on repeated channel pulses. */
TEST(wc3_ability_lifecycle, priestess_starfall_repeatedly_damages_enemies_in_area) {
    static UnitAbilities_t const starfall_abilities = { .abilList = "AEsf" };
    static char const starfall_slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n"
        "C;Y2;X1;K\"AEsf\"\nC;Y2;X2;K\"AEsf\"\nC;Y2;X3;K1\n"
        "C;Y2;X4;K\"air,ground,enemy\"\nC;Y2;X5;K5\nC;Y2;X6;K5\n"
        "C;Y2;X7;K150\nC;Y2;X8;K40\nC;Y2;X9;K1\nE\n";
    edict_t *caster = review_setup();
    edict_t *near_ground = review_unit(1, 100);
    edict_t *near_air = review_unit(1, 120);
    edict_t *far_enemy = review_unit(1, 200);
    edict_t *ally = review_unit(0, 80);
    slkTestData_t *rows = parse_slk_string(starfall_slk), *old = G_SetSLKRows("AbilityData", rows);
    uint32_t const code = FS_SLKKey("AEsf");
    edict_t *thinker;

    caster->data.UnitAbilities = (UnitAbilities_t *)&starfall_abilities;
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);
    near_air->targtype = TARG_AIR;
    far_enemy->s.origin2.x = 151;
    far_enemy->s.origin.x = 151;

    T_ASSERT(S_CastNoTargetSpell(caster, code));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    T_FEQ(near_ground->health.value, 960, .001f);
    T_FEQ(near_air->health.value, 960, .001f);
    T_FEQ(far_enemy->health.value, 1000, .001f);
    T_FEQ(ally->health.value, 1000, .001f);

    level.time = 1999;
    G_RunEntity(thinker);
    T_FEQ(near_ground->health.value, 960, .001f);
    level.time = 2000;
    G_RunEntity(thinker);
    T_FEQ(near_ground->health.value, 920, .001f);
    T_FEQ(near_air->health.value, 920, .001f);
    T_FEQ(far_enemy->health.value, 1000, .001f);
    T_FEQ(ally->health.value, 1000, .001f);

    caster->s.origin2.x += 10;
    caster->s.origin.x += 10;
    level.time = 3000;
    G_RunEntity(thinker);
    T_ASSERT(!thinker->inuse);
    T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0);
    T_FEQ(near_ground->health.value, 920, .001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_ability_lifecycle, starfall_stops_on_stun_sleep_silence_or_death) {
    static cstring_t const interruptions[] = { "Bstu", "BUsl", "BNsi", NULL };
    FOR_LOOP(i, sizeof(interruptions) / sizeof(interruptions[0])) {
        starfallChannelFix_t fix = starfall_channel_setup();
        T_ASSERT(fix.cast); T_NOT_NULL(fix.thinker);
        if (interruptions[i]) unit_addtimedstatus(fix.caster, interruptions[i], 1, 30.0f);
        else fix.caster->health.value = 0;
        G_RunEntity(fix.caster);
        T_ASSERT(!S_SpellIsChanneling(fix.caster));
        if (fix.thinker && fix.thinker->inuse) G_RunEntity(fix.thinker);
        T_ASSERT(!fix.thinker || !fix.thinker->inuse);
        starfall_channel_done(fix);
    }
}

TEST(wc3_ability_lifecycle, starfall_stops_when_another_spell_is_cast) {
    starfallChannelFix_t fix = starfall_channel_setup();
    T_ASSERT(fix.cast); T_NOT_NULL(fix.thinker);
    T_ASSERT(S_CastNoTargetSpell(fix.caster, FS_SLKKey("AEtq")));
    T_EQ(G_GetChannel(fix.caster)->code, FS_SLKKey("AEtq"));
    G_RunEntity(fix.thinker);
    T_ASSERT(!fix.thinker->inuse);
    T_EQ(G_GetChannel(fix.caster)->code, FS_SLKKey("AEtq"));
    starfall_channel_done(fix);
}

TEST(wc3_ability_lifecycle, starfall_stops_when_its_duration_expires) {
    starfallChannelFix_t fix = starfall_channel_setup();
    T_ASSERT(fix.cast); T_NOT_NULL(fix.thinker);
    level.time = fix.thinker->spawn_time;
    G_RunEntity(fix.thinker);
    T_ASSERT(!fix.thinker->inuse);
    T_ASSERT(!S_SpellIsChanneling(fix.caster));
    starfall_channel_done(fix);
}

TEST(wc3_ability_lifecycle, starfall_manual_move_attack_and_stop_orders_interrupt_channel) {
    starfallChannelFix_t fix = starfall_channel_setup();
    vec2_t point = { 300, 0 };
    T_ASSERT(fix.cast);
    T_ASSERT(unit_issueorder(fix.caster, "move", &point));
    T_ASSERT(!S_SpellIsChanneling(fix.caster));
    starfall_channel_done(fix);

    fix = starfall_channel_setup();
    edict_t *enemy = review_unit(1, 50);
    T_ASSERT(fix.cast);
    T_ASSERT(unit_issuetargetorder(fix.caster, "attack", enemy));
    T_ASSERT(!S_SpellIsChanneling(fix.caster));
    starfall_channel_done(fix);

    fix = starfall_channel_setup();
    T_ASSERT(fix.cast);
    T_ASSERT(unit_issueimmediateorder(fix.caster, "stop"));
    T_ASSERT(!S_SpellIsChanneling(fix.caster));
    starfall_channel_done(fix);
}

TEST(wc3_ability_lifecycle, starfall_does_not_auto_acquire_until_manual_right_click) {
    starfallChannelFix_t fix = starfall_channel_setup();
    edict_t *enemy = review_unit(1, 50);
    T_ASSERT(fix.cast);
    level.time = 300 - (uint32_t)(fix.caster - g_edicts) % 300;
    T_ASSERT(!G_ShouldAcquireThisFrame(fix.caster));
    G_RunEntity(fix.caster);
    T_ASSERT(fix.caster->goalentity != enemy);
    T_EQ(G_GetChannel(fix.caster)->code, FS_SLKKey("AEsf"));

    T_ASSERT(unit_issuetargetorder(fix.caster, "smart", enemy));
    T_ASSERT(!S_SpellIsChanneling(fix.caster));
    T_ASSERT(fix.caster->goalentity == enemy);
    starfall_channel_done(fix);
}

/* Cannibalize reserves the nearest eligible corpse for the whole channel and
 * consumes it when healing reaches full HP.  Mechanical corpses fail the authored
 * ground,dead,organic target mask rather than a Cannibalize-only type check. */
TEST(wc3_ability_lifecycle, cannibalize_reserves_nearest_organic_corpse_and_stops_at_full_health) {
    edict_t *caster = review_setup(), *mechanical = review_unit(1, 10), *near = review_unit(1, 30), *far = review_unit(1, 40);
    UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    mechanical->data.UnitData = near->data.UnitData = far->data.UnitData = &corpse_data;
    mechanical->health.value = near->health.value = far->health.value = 0;
    mechanical->svflags |= SVF_DEADMONSTER; near->svflags |= SVF_DEADMONSTER; far->svflags |= SVF_DEADMONSTER;
    mechanical->targtype = TARG_MECHANICAL; caster->health.value = 995;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("Acan")));
    edict_t *thinker = review_thinker(caster);
    T_ASSERT(mechanical->inuse); T_ASSERT(near->inuse); T_ASSERT(far->inuse); T_NOT_NULL(thinker);
    T_ASSERT(near->aiflags & AI_CORPSE_RESERVED); T_ASSERT(!G_UnitIsRaisableCorpse(near));
    FOR_LOOP(i, 5) { level.time += FRAMETIME; G_RunEntities(); }
    T_FEQ(caster->health.value, 1000, .001f); T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0); T_ASSERT(!near->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Cannibalize is a no-target command: it acquires the nearest corpse itself and
 * approaches it before starting the channel instead of targeting the corpse. */
TEST(wc3_ability_lifecycle, cannibalize_command_approaches_nearby_corpse) {
    edict_t *caster = review_setup(), *corpse = review_unit(1, 200);
    UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *clent = &g_edicts[0];
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Acan"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item, .entity = clent);
    caster->health.value = 500;
    corpse->data.UnitData = &corpse_data; corpse->health.value = 0; corpse->svflags |= SVF_DEADMONSTER;
    clent->client = &game.clients[0]; clent->client->ps.number = 0; G_SelectEntity(clent->client, caster);
    G_SetPlayerAbilityAvailable(clent->client, FS_SLKKey("Acan"), false);
    T_ASSERT(!S_AbilityMessage(caster, A_COMMAND, &call));
    T_NULL(caster->goalentity);
    G_SetPlayerAbilityAvailable(clent->client, FS_SLKKey("Acan"), true);
    T_ASSERT(S_AbilityMessage(caster, A_COMMAND, &call));
    T_ASSERT(caster->goalentity == corpse); T_STREQ(caster->currentmove->animation, "walk");
    T_NOT_NULL(review_thinker(caster));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Dur=33 and DataA=10 are authoritative in both ROC and TFT; healing is continuous
 * on the simulation cadence rather than quantized to one-second pulses. */
TEST(wc3_ability_lifecycle, cannibalize_heals_for_authored_duration_through_entity_scheduler) {
    edict_t *caster = review_setup(), *corpse = review_unit(1, 40);
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
    T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0); T_ASSERT(!corpse->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Live, mechanical, Hero-lifecycle and out-of-search-range units cannot fund a cast; movement interrupts an active feast. */
TEST(wc3_ability_lifecycle, cannibalize_rejects_invalid_corpses_and_stops_when_caster_moves) {
    edict_t *caster = review_setup(), *live = review_unit(1, 20), *mechanical = review_unit(1, 30), *far = review_unit(1, 801);
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
    T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0); T_ASSERT(!far->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A launched bolt must not stun a target which becomes spell immune before impact. */
TEST(wc3_ability_lifecycle, avatar_blocks_storm_bolt_stun_at_impact) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    G_SetUnitColorOverride(caster, 6);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastUnitTargetSpell(caster, FS_SLKKey("AHtb"), enemy);
    edict_t *missile = NULL;
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
    edict_t *caster = review_setup(), *ally = review_unit(0, 100);
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
    edict_t *caster = review_setup(), *enemy = review_unit(1, 500);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    bool cast = S_CastUnitTargetSpell(caster, FS_SLKKey("AUfn"), enemy);
    float hp = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_FEQ(hp, 850, .001f);
}

/* Serial ownership separates two casts of the same spell even when both thinkers still exist. */
TEST(wc3_ability_lifecycle, recast_retires_old_thinker_without_cancelling_new_channel) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    edict_t *first = review_thinker(caster);
    uint32_t serial = G_GetChannel(caster)->serial;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    T_NE(G_GetChannel(caster)->serial, serial);
    level.time += 1000; G_RunEntity(first);
    T_ASSERT(!first->inuse); T_EQ(G_GetChannel(caster)->code, FS_SLKKey("AHdr")); T_FEQ(enemy->mana.value, 100, .001f);
    edict_t *next = review_thinker(caster);
    T_NOT_NULL(next);
    if (next) G_RunEntity(next);
    T_FEQ(enemy->mana.value, 85, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stun can happen after cast commitment but before the caster's next own frame. */
TEST(wc3_ability_lifecycle, stun_interrupts_blizzard_before_next_wave) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    edict_t *thinker = review_thinker(caster);
    level.time = thinker->freetime; G_RunEntity(thinker); /* shards */
    level.time = thinker->freetime; G_RunEntity(thinker); /* first damage */
    unit_addtimedstatus(caster, "Bstu", 1, 5);
    level.time = thinker->freetime; G_RunEntity(thinker);
    T_FEQ(enemy->health.value, 970, .001f); T_ASSERT(!thinker->inuse); T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The retired caster slot must not donate its old drain to a newly spawned unit. */
TEST(wc3_ability_lifecycle, removed_caster_slot_cannot_own_old_drain) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    edict_t *thinker = review_thinker(caster);
    G_FreeEdict(caster); level.time += 2000;
    edict_t *fresh = review_unit(0, 0);
    T_ASSERT(fresh == caster);
    G_AddChannel(fresh);
    G_GetChannel(fresh)->code = FS_SLKKey("AHdr"); G_GetChannel(fresh)->serial = G_GetChannel(thinker)->serial;
    G_RunEntity(thinker);
    T_ASSERT(!thinker->inuse); T_FEQ(enemy->mana.value, 100, .001f);
    T_EQ(G_GetChannel(fresh)->code, FS_SLKKey("AHdr"));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The target's edict identity matters independently of the still-active caster and cast token. */
TEST(wc3_ability_lifecycle, removed_target_slot_cannot_receive_old_drain) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    edict_t *thinker = review_thinker(caster);
    G_FreeEdict(enemy); level.time += 2000;
    edict_t *fresh = review_unit(1, 100);
    T_ASSERT(fresh == enemy); G_RunEntity(thinker);
    T_ASSERT(!thinker->inuse); T_FEQ(fresh->mana.value, 100, .001f); T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* AHdr's authored friendly transfer rate and direction differ from enemy siphoning. */
TEST(wc3_ability_lifecycle, siphon_sends_mana_to_allies_and_expires) {
    edict_t *caster = review_setup(), *ally = review_unit(0, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    caster->mana.value = 500; ally->mana.value = 0;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), ally));
    edict_t *thinker = review_thinker(caster);
    FOR_LOOP(i, 6) { level.time += 1000; G_RunEntity(thinker); }
    T_FEQ(caster->mana.value, 320, .001f); T_FEQ(ally->mana.value, 180, .001f);
    T_ASSERT(!thinker->inuse); T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Correcting the schema must preserve delayed startup, the reduced-damage phase, and final cleanup. */
TEST(wc3_ability_lifecycle, flame_strike_obeys_authored_phase_intervals_and_expiry) {
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2));
    edict_t *thinker = review_thinker(caster);
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
    edict_t *caster = review_setup(), *enemy = review_unit(1, 500), *near = review_unit(1, 550);
    edict_t *far = review_unit(1, 50), *ally = review_unit(0, 550);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUfn"), enemy));
    T_FEQ(enemy->health.value, 850, .001f); T_FEQ(near->health.value, 950, .001f);
    T_FEQ(far->health.value, 1000, .001f); T_FEQ(ally->health.value, 1000, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Ordinary corpses restore selection and the idle move instead of retaining their decay callback. */
TEST(wc3_ability_lifecycle, resurrection_retires_death_state_and_rejects_empty_cast) {
    edict_t *caster = review_setup(), *ally = review_unit(0, 100), *enemy = review_unit(1, 100);
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
    cstring_t path = Test_TempPath("skill-drain-save.bin");
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    edict_t *thinker = review_thinker(caster);
    G_AddChannel(caster);
    T_ASSERT(WriteGame(path)); G_GetChannel(caster)->code = 0; thinker->think = NULL;
    T_ASSERT(ReadGame(path));
    level.time += 1000; G_RunEntity(thinker);
    T_FEQ(enemy->mana.value, 85, .001f); T_EQ(G_GetChannel(caster)->code, FS_SLKKey("AHdr"));
    G_RunEntity(thinker); T_FEQ(enemy->mana.value, 85, .001f);
    remove(path); G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Overcharge uses the existing serialized current mana, without a new
 * field or thinker; loading a saved surplus must allow decay to resume. */
TEST(wc3_ability_lifecycle, siphon_overcharge_survives_save_load_and_decays) {
    cstring_t path = Test_TempPath("skill-siphon-overcharge-save.bin");
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = review_setup();
    caster->mana.max_value = 100; caster->mana.value = 145;
    T_ASSERT(WriteGame(path));
    caster->mana.value = 0;
    T_ASSERT(ReadGame(path));
    T_FEQ(caster->mana.max_value, 100, .001f);
    T_FEQ(caster->mana.value, 145, .001f);
    G_RunEntity(caster);
    T_FEQ(caster->mana.value, 144.7f, .001f);
    remove(path);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
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
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(roc), *old = G_SetSLKRows("AbilityData", rows);
    T_FEQ(S_SpellData(FS_SLKKey("AHbz"), 1, 1), 6, .001f);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    edict_t *thinker = review_thinker(caster);
    FOR_LOOP(i, 12) {
        if (!thinker->inuse) break;
        level.time = thinker->freetime; G_RunEntity(thinker);
    }
    T_FEQ(enemy->health.value, 820, .001f); T_ASSERT(!thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* UnitBalance creep level is unrelated to Hero identity when selecting Storm Bolt's duration. */
TEST(wc3_ability_lifecycle, storm_bolt_uses_hero_identity_for_stun_duration) {
    edict_t *caster = review_setup(), *hero = review_unit(1, 100), *creep = review_unit(1, 150);
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
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2));
    edict_t *thinker = review_thinker(caster);
    while (level.time < 5000) { level.time += FRAMETIME; G_RunEntity(thinker); }
    T_FEQ(enemy->health.value, 865, .001f); T_EQ(thinker->freetime, 6000);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Replacement orders interrupt at dispatch, even before a Move changes coordinates or an Attack swings. */
TEST(wc3_ability_lifecycle, stop_move_and_attack_retire_channel_before_motion) {
    cstring_t orders[] = { "stop", "move", "attack" };
    FOR_LOOP(i, 3) {
        edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
        slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
        caster->unitinfo.MoveSpeed = 300; caster->movetype = MOVETYPE_STEP;
        caster->unit_state->attack1.type = ATK_NORMAL;
        caster->unit_state->attack1.range = 600; caster->unit_state->attack1.damageBase = 10; caster->unit_state->attack1.cooldown = 1;
        caster->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
        T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
        edict_t *thinker = review_thinker(caster);
        if (i == 0) T_ASSERT(unit_issueimmediateorder(caster, orders[i]));
        else if (i == 1) T_ASSERT(unit_issueorder(caster, orders[i], &MAKE(vec2_t, .x = 300)));
        else T_ASSERT(unit_issuetargetorder(caster, orders[i], enemy));
        umove_t const *move = caster->currentmove;
        T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0); T_FEQ(caster->s.origin2.x, 0, .001f);
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
    edict_t *caster = review_setup(), *enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2));
    edict_t *thinker = review_thinker(caster);
    level.time = 2300; G_RunEntity(thinker); T_FEQ(enemy->health.value, 1000, .001f);
    level.time = 2400; G_RunEntity(thinker); T_FEQ(enemy->health.value, 970, .001f);
    level.time = 2500; G_RunEntity(thinker); T_FEQ(enemy->health.value, 940, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}
/* Phase 9: zero flags deliberately preserve old timed-buff dispel behavior,
 * while explicit classification gates transfer separately from removal. */
TEST(wc3_ability_lifecycle, classified_status_dispel_and_transfer_policy) {
    edict_t *unit = review_setup();
    status_application_t app = { .buff = "Bixx", .level = 1, .duration = 5.0f };
    heroabilitystatus_t *slot = unit_applystatus(unit, &app);
    T_NOT_NULL(slot);
    T_ASSERT(unit_status_can_dispel(slot));
    T_ASSERT(!unit_status_can_steal(slot));
    slot->buff_flags = WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL |
                       WC3_STATUS_BUFF_TRANSFERABLE;
    T_ASSERT(unit_status_can_dispel(slot));
    T_ASSERT(unit_status_can_steal(slot));
    slot->buff_flags |= WC3_STATUS_BUFF_UNDISPELLABLE;
    T_ASSERT(!unit_status_can_dispel(slot));
    T_ASSERT(!unit_status_can_steal(slot));
    slot->buff_flags = WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_TRANSFERABLE;
    T_ASSERT(unit_status_can_steal(slot));
    unit_removestatus(unit, slot, STATUS_REMOVE_SCRIPT);
    T_ASSERT(!unit_status_can_dispel(slot));
}

/* Magical dispels must not strip explicit physical-only statuses; legacy
 * unclassified records retain their previous behavior. */
TEST(wc3_ability_lifecycle, magical_dispel_respects_physical_classification) {
    edict_t *unit = review_setup();
    status_application_t app = { .buff = "Bixx", .level = 1, .duration = 5.0f };
    heroabilitystatus_t *slot = unit_applystatus(unit, &app);
    T_NOT_NULL(slot);
    T_ASSERT(unit_status_can_magic_dispel(slot)); /* legacy compatibility */
    slot->buff_flags = WC3_STATUS_BUFF_PHYSICAL;
    T_ASSERT(unit_status_can_dispel(slot));
    T_ASSERT(!unit_status_can_magic_dispel(slot));
    slot->buff_flags = WC3_STATUS_BUFF_MAGICAL;
    T_ASSERT(unit_status_can_magic_dispel(slot));
    slot->buff_flags = WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_PHYSICAL;
    T_ASSERT(!unit_status_can_magic_dispel(slot));
    slot->buff_flags = WC3_STATUS_BUFF_UNDISPELLABLE | WC3_STATUS_BUFF_MAGICAL;
    T_ASSERT(!unit_status_can_magic_dispel(slot));
    unit_removestatus(unit, slot, STATUS_REMOVE_SCRIPT);
    T_ASSERT(!unit_status_can_magic_dispel(slot));
}

TEST(wc3_ability_lifecycle, classified_status_application_replace_resets_flags) {
    edict_t *unit = review_setup();
    status_application_t app = {
        .buff = "Bixx", .level = 1, .duration = 5.0f,
        .buff_flags = WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_TRANSFERABLE
    };
    heroabilitystatus_t *slot = unit_applystatus(unit, &app);
    T_NOT_NULL(slot);
    T_ASSERT(unit_status_can_steal(slot));
    app.buff_flags = WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGICAL;
    slot = unit_applystatus(unit, &app);
    T_NOT_NULL(slot);
    T_EQ(slot->buff_flags, app.buff_flags);
    T_ASSERT(unit_status_can_dispel(slot));
    T_ASSERT(!unit_status_can_steal(slot));
}

#endif

#ifdef BZ_TESTS
TEST(wc3_ability_lifecycle, status_capacity_preflight_is_deterministic) {
    edict_t *unit = review_setup();
    static cstring_t const ids[MAX_UNIT_STATUSES + 1] = {
        "Bq01", "Bq02", "Bq03", "Bq04", "Bq05", "Bq06", "Bq07", "Bq08",
        "Bq09", "Bq10", "Bq11", "Bq12", "Bq13", "Bq14", "Bq15", "Bq16", "Bq17"
    };
    status_application_t app = { .level = 1, .duration = 10.0f };
    T_EQ(unit_status_checkapplication(unit, &app), WC3_STATUS_APPLY_INVALID);
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        app.buff = ids[i];
        T_EQ(unit_status_checkapplication(unit, &app), WC3_STATUS_APPLY_FREE_SLOT);
        T_NOT_NULL(unit_applystatus(unit, &app));
    }
    app.buff = ids[MAX_UNIT_STATUSES];
    T_EQ(unit_status_checkapplication(unit, &app), WC3_STATUS_APPLY_FULL);
    T_ASSERT(unit_applystatus(unit, &app) == NULL);
    T_ASSERT(!unit_addtimedstatus(unit, ids[MAX_UNIT_STATUSES], 1, 10.0f));
    T_EQ(G_UnitStatusLevel(unit, *((uint32_t const *)ids[0])), 1);
    app.buff = ids[0];
    T_EQ(unit_status_checkapplication(unit, &app), WC3_STATUS_APPLY_REUSE);
    T_NOT_NULL(unit_applystatus(unit, &app));
    T_EQ(G_UnitStatusLevel(unit, *((uint32_t const *)ids[0])), 1);
}

TEST(wc3_ability_lifecycle, independent_status_instances_keep_source_identity) {
    edict_t *target = review_setup(), *source_a = review_unit(0, 100), *source_b = review_unit(0, 100);
    uint32_t old_time = level.time;
    status_application_t app = {
        .buff = "Bqpo", .level = 1, .duration = 10.0f,
        .stack_policy = WC3_STATUS_STACK_INDEPENDENT, .source = source_a
    };
    level.time = 1000;
    heroabilitystatus_t *a = unit_applystatus(target, &app), *b;
    T_NOT_NULL(a);
    app.duration = 20.0f;
    app.source = source_b;
    b = unit_applystatus(target, &app);
    T_NOT_NULL(b);
    T_NE(a, b);
    T_NE(a->instance_id, b->instance_id);
    T_ASSERT(b->timestamp > a->timestamp);
    T_EQ(unit_findstatussource(target, a->code, source_a), a);
    T_EQ(unit_findstatussource(target, b->code, source_b), b);
    level.time = a->timestamp;
    unit_updatestatuses(target);
    T_NULL(unit_findstatussource(target, b->code, source_a));
    T_EQ(unit_findstatussource(target, b->code, source_b), b);
    level.time = b->timestamp;
    unit_updatestatuses(target);
    T_NULL(unit_findstatussource(target, b->code, source_b));
    level.time = old_time;
}

TEST(wc3_ability_lifecycle, status_application_separates_owner_from_payload) {
    edict_t *target = review_setup(), *source = review_unit(0, 100);
    status_application_t app = {
        .buff = "Bixx", .level = 1, .duration = 6.0f,
        .source_ability = MAKEFOURCC('A','H','f','s'),
        .data = 125, .source = source, .rank = 2
    };
    heroabilitystatus_t *slot = unit_applystatus(target, &app);
    T_NOT_NULL(slot);
    T_EQ(slot->source_ability, app.source_ability);
    T_EQ(slot->data, 125);
    T_EQ(slot->source, source);
    T_EQ(slot->source_spawn_time, source->spawn_time);
    T_EQ(slot->rank, 2);
    T_ASSERT(slot->timestamp > G_Time());
    app.data = 250;
    app.source_ability = 0;
    app.source = NULL;
    slot = unit_applystatus(target, &app); /* Replace defaults in stock data. */
    T_NOT_NULL(slot);
    T_EQ(slot->data, 250);
    T_EQ(slot->source_ability, 0);
    T_EQ(slot->source, NULL);
}

/* Phase 3: status contributors are independent of buff rawcode and do not
 * reinterpret legacy numeric status payloads. */
TEST(wc3_ability_lifecycle, status_state_contributors_compose_and_expire) {
    edict_t *target = review_setup();
    status_application_t first = {
        .buff = "Bixx", .level = 1, .duration = 8.0f,
        .state_mask = WC3_STATUS_STATE_INVULNERABLE | WC3_STATUS_STATE_SILENCED,
        .data = 125
    };
    status_application_t second = {
        .buff = "Biyy", .level = 1, .duration = 8.0f,
        .state_mask = WC3_STATUS_STATE_INVULNERABLE
    };
    heroabilitystatus_t *a = unit_applystatus(target, &first);
    heroabilitystatus_t *b;
    T_NOT_NULL(a);
    b = unit_applystatus(target, &second);
    T_NOT_NULL(b);
    T_ASSERT(unit_hasstatusstate(target, WC3_STATUS_STATE_INVULNERABLE));
    T_ASSERT(unit_hasstatusstate(target, WC3_STATUS_STATE_SILENCED));
    T_ASSERT(!unit_hasstatusstate(target, WC3_STATUS_STATE_ETHEREAL));
    T_EQ(a->data, 125);
    unit_removestatus(target, a, STATUS_REMOVE_SCRIPT);
    T_ASSERT(unit_hasstatusstate(target, WC3_STATUS_STATE_INVULNERABLE));
    T_ASSERT(!unit_hasstatusstate(target, WC3_STATUS_STATE_SILENCED));
    unit_removestatus(target, b, STATUS_REMOVE_SCRIPT);
    T_ASSERT(!unit_hasstatusstate(target, WC3_STATUS_STATE_INVULNERABLE));
}

TEST(wc3_ability_lifecycle, status_state_replace_and_refresh) {
    edict_t *target = review_setup();
    status_application_t app = {
        .buff = "Bixx", .level = 1, .duration = 6.0f,
        .state_mask = WC3_STATUS_STATE_ROOTED
    };
    heroabilitystatus_t *slot = unit_applystatus(target, &app);
    T_NOT_NULL(slot);
    T_ASSERT(unit_hasstatusstate(target, WC3_STATUS_STATE_ROOTED));
    app.state_mask = WC3_STATUS_STATE_ETHEREAL;
    slot = unit_applystatus(target, &app); /* Replace (default policy). */
    T_NOT_NULL(slot);
    T_ASSERT(!unit_hasstatusstate(target, WC3_STATUS_STATE_ROOTED));
    T_ASSERT(unit_hasstatusstate(target, WC3_STATUS_STATE_ETHEREAL));
    slot->timestamp = G_Time(); /* Simulate elapsed lifetime before update. */
    T_ASSERT(!unit_hasstatusstate(target, WC3_STATUS_STATE_ETHEREAL));
}

/* Stock buffs must contribute to the same state query as status_application_t.
 * Legacy insertion cannot be dropped until every ability is migrated. */
TEST(wc3_ability_lifecycle, stock_buff_states_are_live_and_composable) {
    edict_t *unit = review_setup();
    unit_addtimedstatus(unit, "BNsi", 1, 10.0f);
    T_ASSERT(unit_hasstatusstate(unit, WC3_STATUS_STATE_SILENCED));
    unit_addtimedstatus(unit, "BNso", 1, 10.0f);
    T_ASSERT(S_UnitIsSilenced(unit));
    unit_removestatus(unit, unit_findstatus(unit, MAKEFOURCC('B','N','s','i')), STATUS_REMOVE_DISPEL);
    T_ASSERT(S_UnitIsSilenced(unit));
    unit_removestatus(unit, unit_findstatus(unit, MAKEFOURCC('B','N','s','o')), STATUS_REMOVE_DISPEL);
    T_ASSERT(!S_UnitIsSilenced(unit));

    unit_addtimedstatus(unit, "BEer", 1, 10.0f);
    T_ASSERT(unit_hasstatusstate(unit, WC3_STATUS_STATE_ROOTED));
    T_ASSERT(S_UnitIsEntanglingRooted(unit));
    T_ASSERT(!S_UnitIsEnsnared(unit));
    unit_addtimedstatus(unit, "Bens", 1, 10.0f);
    T_ASSERT(S_UnitIsEnsnared(unit));
    unit_removestatus(unit, unit_findstatus(unit, MAKEFOURCC('B','E','e','r')), STATUS_REMOVE_DISPEL);
    T_ASSERT(unit_hasstatusstate(unit, WC3_STATUS_STATE_ROOTED));
    T_ASSERT(!S_UnitIsEntanglingRooted(unit));
    T_ASSERT(S_UnitIsEnsnared(unit));
}

TEST(wc3_ability_lifecycle, stock_magic_immunity_composes_with_explicit_contribution) {
    edict_t *unit = review_setup();
    status_application_t app = {
        .buff = "Biyy", .level = 1, .duration = 10.0f,
        .state_mask = WC3_STATUS_STATE_MAGIC_IMMUNE
    };
    unit_addtimedstatus(unit, "Bams", 1, 10.0f);
    T_ASSERT(S_UnitSpellImmune(unit));
    T_NOT_NULL(unit_applystatus(unit, &app));
    unit_removestatus(unit, unit_findstatus(unit, MAKEFOURCC('B','a','m','s')), STATUS_REMOVE_DISPEL);
    T_ASSERT(S_UnitSpellImmune(unit));
    unit_removestatus(unit, unit_findstatus(unit, MAKEFOURCC('B','i','y','y')), STATUS_REMOVE_SCRIPT);
    T_ASSERT(!S_UnitSpellImmune(unit));
}
TEST(wc3_ability_lifecycle, default_status_does_not_replace_independent_source) {
    edict_t *target = review_setup(), *source = review_unit(0, 100);
    status_application_t app = { .buff = "Bqpo", .level = 1, .duration = 10.0f,
        .stack_policy = WC3_STATUS_STACK_INDEPENDENT, .source = source };
    heroabilitystatus_t *independent = unit_applystatus(target, &app);
    heroabilitystatus_t *legacy;
    T_NOT_NULL(independent);
    app.stack_policy = WC3_STATUS_STACK_DEFAULT;
    app.source = NULL;
    T_EQ(unit_status_checkapplication(target, &app), WC3_STATUS_APPLY_FREE_SLOT);
    legacy = unit_applystatus(target, &app);
    T_NOT_NULL(legacy);
    T_NE(legacy, independent);
    T_EQ(independent->stack_policy, WC3_STATUS_STACK_INDEPENDENT);
    T_EQ(independent->source, source);
    T_EQ(unit_status_checkapplication(target, &app), WC3_STATUS_APPLY_REUSE);
}

TEST(wc3_ability_lifecycle, unrelated_status_application_keeps_custom_modifiers) {
    edict_t *target = review_setup();
    status_application_t app = { .buff = "Bqpo", .level = 1, .duration = 10.0f };
    heroabilitystatus_t *custom = unit_applystatus(target, &app);
    wc3_status_modifier_t mod = { .type = WC3_STATUS_MOD_ARMOR_FLAT,
        .policy = WC3_STATUS_MOD_ADD, .value = 3.0f };
    T_NOT_NULL(custom);
    T_ASSERT(unit_status_setmodifier(custom, 0, &mod));
    T_NOT_NULL(S_SpellApplyTimedStatus(target, "Bq01", 1, 3.0f));
    T_EQ(custom->modifier_count, 1);
    T_FEQ(unit_status_modifier_total(target, WC3_STATUS_MOD_ARMOR_FLAT), 3.0f, 0.001f);
}


TEST(wc3_ability_lifecycle, scripted_status_removal_updates_derived_stun_immediately) {
    edict_t *unit = review_setup();
    heroabilitystatus_t *slot = unit_applystatus(unit, &(status_application_t){
        .buff = "Bstu", .level = 1, .duration = 8.0f
    });
    T_NOT_NULL(slot);
    T_ASSERT(unit_hasstatusstate(unit, WC3_STATUS_STATE_STUNNED));
    unit_removestatus(unit, slot, STATUS_REMOVE_SCRIPT);
    T_ASSERT(!unit_hasstatusstate(unit, WC3_STATUS_STATE_STUNNED));
}

TEST(wc3_ability_lifecycle, status_source_lookup_does_not_return_expired_record) {
    edict_t *unit = review_setup(), *source = review_unit(0, 100);
    heroabilitystatus_t *slot = unit_applystatus(unit, &(status_application_t){
        .buff = "Bqpo", .level = 1, .duration = 8.0f,
        .stack_policy = WC3_STATUS_STACK_INDEPENDENT, .source = source
    });
    T_NOT_NULL(slot);
    T_EQ(unit_findstatussource(unit, slot->code, source), slot);
    slot->timestamp = G_Time();
    T_NULL(unit_findstatussource(unit, slot->code, source));
}


TEST(wc3_ability_lifecycle, status_deferred_removal_reconciles_after_batch) {
    edict_t *unit = review_setup();
    heroabilitystatus_t *first, *second;
    T_ASSERT(unit_addtimedstatus(unit, "Bstu", 1, 10.0f));
    T_ASSERT(unit_addtimedstatus(unit, "Bsta", 1, 10.0f));
    first = unit_findstatus(unit, MAKEFOURCC('B','s','t','u'));
    second = unit_findstatus(unit, MAKEFOURCC('B','s','t','a'));
    T_NOT_NULL(first); T_NOT_NULL(second);
    T_ASSERT(unit_hasstatusstate(unit, WC3_STATUS_STATE_STUNNED));
    unit_removestatus_deferred(unit, first, STATUS_REMOVE_DISPEL);
    unit_removestatus_deferred(unit, second, STATUS_REMOVE_DISPEL);
    unit_refreshstatusflags(unit);
    T_ASSERT(!unit_hasstatusstate(unit, WC3_STATUS_STATE_STUNNED));
    T_NULL(unit_findstatus(unit, MAKEFOURCC('B','s','t','u')));
    T_NULL(unit_findstatus(unit, MAKEFOURCC('B','s','t','a')));
}

TEST(wc3_ability_lifecycle, explicit_root_state_blocks_translation_without_disarming) {
    edict_t *unit = review_setup();
    heroabilitystatus_t *slot = unit_applystatus(unit, &(status_application_t){
        .buff = "Bqro", .level = 1, .duration = 8.0f,
        .state_mask = WC3_STATUS_STATE_ROOTED
    });
    T_NOT_NULL(slot);
    T_ASSERT(!S_UnitCanTranslate(unit));
    unit_removestatus(unit, slot, STATUS_REMOVE_SCRIPT);
    T_ASSERT(S_UnitCanTranslate(unit));
}


TEST(wc3_ability_lifecycle, physical_statuses_resist_magical_dispels) {
    edict_t *unit = review_setup();
    static cstring_t const physical[] = { "Bens", "Bena", "Bwea", "Bweb", "Bpoi", "Bpsd", "Bapl" };
    FOR_LOOP(i, sizeof(physical) / sizeof(physical[0])) {
        heroabilitystatus_t *slot = S_SpellApplyTimedStatus(unit, physical[i], 1, 10.0f);
        T_NOT_NULL(slot);
        slot->buff_flags = WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_PHYSICAL;
        T_ASSERT(unit_status_can_dispel(slot));
        T_ASSERT(!unit_status_can_magic_dispel(slot));
        T_ASSERT(!unit_status_can_steal(slot));
    }
    {
        heroabilitystatus_t *legacy = S_SpellApplyTimedStatus(unit, "Bq01", 1, 10.0f);
        T_NOT_NULL(legacy);
        T_ASSERT(unit_status_can_magic_dispel(legacy));
    }
}


/* JASS removal must respect explicit polarity/resistance and retain
 * unclassified and lifetime-owning statuses. */
TEST(wc3_ability_lifecycle, jass_removebuffs_filters_classified_statuses) {
    edict_t *unit = review_setup();
    heroabilitystatus_t *positive = unit_applystatus(unit, &(status_application_t){
        .buff = "Bixx", .level = 1, .duration = 5.0f,
        .buff_flags = WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL
    });
    heroabilitystatus_t *negative = unit_applystatus(unit, &(status_application_t){
        .buff = "Biyy", .level = 1, .duration = 5.0f,
        .buff_flags = WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_PHYSICAL
    });
    heroabilitystatus_t *unknown = unit_applystatus(unit, &(status_application_t){
        .buff = "Bizz", .level = 1, .duration = 5.0f
    });
    T_NOT_NULL(positive); T_NOT_NULL(negative); T_NOT_NULL(unknown);
    if (!positive || !negative || !unknown) return;
    unit_removebuffs(unit, true, true, true, false, false, false, false);
    T_EQ(positive->level, 0);
    T_ASSERT(negative->level != 0);
    T_ASSERT(unknown->level != 0);
    unit_removebuffs(unit, false, true, false, true, false, false, false);
    T_EQ(negative->level, 0);
    T_ASSERT(unknown->level != 0);
}

TEST(wc3_ability_lifecycle, jass_removebuffs_preserves_undispellable_and_mixed_records) {
    edict_t *unit = review_setup();
    heroabilitystatus_t *protected = unit_applystatus(unit, &(status_application_t){
        .buff = "Bi01", .level = 1, .duration = 7.0f,
        .buff_flags = WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_UNDISPELLABLE
    });
    heroabilitystatus_t *mixed = unit_applystatus(unit, &(status_application_t){
        .buff = "Bi02", .level = 1, .duration = 7.0f,
        .buff_flags = WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGICAL
    });
    T_NOT_NULL(protected); T_NOT_NULL(mixed);
    if (!protected || !mixed) return;
    unit_removebuffs(unit, true, true, true, true, false, false, false);
    T_ASSERT(protected->level != 0);
    T_ASSERT(mixed->level != 0);
}

#endif


#ifdef BZ_TESTS
/* The real Pit Lord's three-second channel must contain multiple waves, not
 * one wave every three seconds. Use deliberately nonstock DataA/B/E/F so
 * hardcoded stock values cannot satisfy the regression. */
static char const rain_of_fire_slk[] =
    "ID;PWXL;N;EBB;Y2;X14\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"Area1\"\n"
    "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\n"
    "C;Y1;X9;K\"DataC1\"\nC;Y1;X10;K\"DataD1\"\n"
    "C;Y1;X11;K\"DataE1\"\nC;Y1;X12;K\"DataF1\"\nC;Y1;X13;K\"Cost1\"\n"
    "C;Y1;X14;K\"EfctID1\"\n"
    "C;Y2;X1;K\"ANrf\"\nC;Y2;X2;K\"ANrf\"\nC;Y2;X3;K1\n"
    "C;Y2;X4;K\"air,ground,structure,enemy\"\n"
    "C;Y2;X5;K2\nC;Y2;X6;K130\nC;Y2;X7;K4\n"
    "C;Y2;X8;K25\nC;Y2;X9;K3\nC;Y2;X10;K0.5\n"
    "C;Y2;X11;K7\nC;Y2;X12;K50\nC;Y2;X13;K0\n"
    "C;Y2;X14;K\"XErf\"\nE\n";

static edict_t *rain_of_fire_test_setup(slkTestData_t **rows, slkTestData_t **old) {
    edict_t *caster = review_setup();
    *rows = parse_slk_string(rain_of_fire_slk);
    *old = G_SetSLKRows("AbilityData", *rows);
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = FS_SLKKey("ANrf"), .level = 1);
    return caster;
}
static void rain_of_fire_test_done(slkTestData_t *rows, slkTestData_t *old) {
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

static int rain_fire_capture_sound_index(cstring_t path) {
    strlcpy(blizzard_sound_path, path ? path : "", sizeof(blizzard_sound_path));
    return path && strstr(path, "loop.wav") ? 92 : 91;
}
static int rain_fire_capture_sound_index_alias(cstring_t path, cstring_t alias) {
    (void)alias;
    return rain_fire_capture_sound_index(path);
}

/* Effect data is supplied by the synthetic SLK fixture, not an installed
 * Warcraft archive. Wave impact models and sounds are selected by XErf; their
 * lifetime is separate from the caster-owned channel loop sound. */
TEST(wc3_ability_lifecycle, pit_lord_rain_of_fire_uses_xerf_shards_wave_audio_and_channel_loop) {
    static char const effect_slk[] =
        "ID;PWXL;N;E\nB;X4;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"EffectArt\"\n"
        "C;Y1;X3;K\"Effectsound\"\nC;Y1;X4;K\"Effectsoundlooped\"\n"
        "C;Y2;X1;K\"XErf\"\n"
        "C;Y2;X2;K\"TestUI\\Models\\quad_sprite.mdx\"\n"
        "C;Y2;X3;K\"RainWaveTest\"\nC;Y2;X4;K\"RainLoopTest\"\nE\n";
    static char const sound_slk[] =
        "ID;PWXL;N;E\nB;X5;Y3;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\nC;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\nC;Y1;X4;K\"Volume\"\n"
        "C;Y1;X5;K\"Flags\"\n"
        "C;Y2;X1;K\"RainWaveTest\"\nC;Y2;X2;K\"wave.wav\"\n"
        "C;Y2;X3;K\"TestUI\\Sounds\\\"\nC;Y2;X4;K127\n"
        "C;Y2;X5;K\"IGNOREUSERNAME\"\n"
        "C;Y3;X1;K\"RainLoopTest\"\nC;Y3;X2;K\"loop.wav\"\n"
        "C;Y3;X3;K\"TestUI\\Sounds\\\"\nC;Y3;X4;K127\n"
        "C;Y3;X5;K\"IGNOREUSERNAME\"\nE\n";
    slkTestData_t *rows, *old;
    edict_t *caster = rain_of_fire_test_setup(&rows, &old);
    edict_t *enemy = review_unit(1, 300), *thinker;
    slkTestData_t *effects = parse_slk_string(effect_slk);
    slkTestData_t *sounds = parse_slk_string(sound_slk);
    slkTestData_t *old_effects = G_SetSLKRows("AbilityBuffData", effects);
    slkTestData_t *old_sounds = G_SetSLKRows("AbilitySounds", sounds);
    int (*old_model_index)(cstring_t) = gi.ModelIndex;
    int (*old_sound_index)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;
    __typeof__(gi.SoundPolicy) old_sound_policy = gi.SoundPolicy;
    vec2_t point = { 300, 0 };

    flame_strike_model_count = 0;
    blizzard_sound_calls = 0;
    blizzard_sound_path[0] = '\0';
    gi.ModelIndex = flame_strike_capture_model;
    gi.SoundIndex = rain_fire_capture_sound_index;
    gi.SoundIndexAlias = rain_fire_capture_sound_index_alias;
    gi.SoundPolicy = review_capture_sound_policy;
    T_EQ(G_AbilityEffectCode(FS_SLKKey("ANrf"), 1), FS_SLKKey("XErf"));
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("ANrf"), &point));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    if (thinker) {
        /* Three authored shards from DataC1=3, not DataA1=4 waves. */
        T_EQ(flame_strike_model_count, 3u);
        FOR_LOOP(i, 3)
            T_STREQ(flame_strike_models[i], "TestUI\\Models\\quad_sprite.mdx");
        T_EQ(blizzard_sound_calls, 1u);
        T_EQ(blizzard_sound_index, 91);
        T_EQ(thinker->s.sound, 92u); /* loop clip, not one-shot wave clip */
        T_STREQ(blizzard_sound_path, "TestUI\\Sounds\\wave.wav");
        level.time = 1500;
        G_RunEntity(thinker);
        T_EQ(flame_strike_model_count, 6u);
        T_EQ(blizzard_sound_calls, 2u); /* one wave sound, not one per shard */
        T_FEQ(enemy->health.value, 950, .001f);
        S_SpellCancelChannel(caster);
        G_RunEntity(thinker);
        T_ASSERT(!thinker->inuse); /* freeing the carrier also stops looped audio */
        T_EQ(flame_strike_model_count, 6u); /* no new impacts on cancellation */
    }
    gi.ModelIndex = old_model_index;
    gi.SoundIndex = old_sound_index;
    gi.SoundIndexAlias = old_sound_alias;
    gi.SoundPolicy = old_sound_policy;
    G_SetSLKRows("AbilityBuffData", old_effects); free_slk_rows(effects);
    G_SetSLKRows("AbilitySounds", old_sounds); free_slk_rows(sounds);
    rain_of_fire_test_done(rows, old);
}

TEST(wc3_ability_lifecycle, pit_lord_rain_of_fire_still_deals_damage_without_effect_art) {
    char slk[sizeof(rain_of_fire_slk)];
    slkTestData_t *rows, *old;
    edict_t *caster = rain_of_fire_test_setup(&rows, &old);
    edict_t *enemy = review_unit(1, 300);
    edict_t *thinker;
    vec2_t point = { 300, 0 };
    slkTestData_t *without_art, *previous;
    int (*old_model_index)(cstring_t) = gi.ModelIndex;
    memcpy(slk, rain_of_fire_slk, sizeof(slk));
    char *effect = strstr(slk, "XErf");
    T_NOT_NULL(effect);
    if (!effect) { rain_of_fire_test_done(rows, old); return; }
    memcpy(effect, "XZzz", 4);
    without_art = parse_slk_string(slk);
    previous = G_SetSLKRows("AbilityData", without_art);
    flame_strike_model_count = 0;
    gi.ModelIndex = flame_strike_capture_model;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("ANrf"), &point));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    T_EQ(flame_strike_model_count, 0u);
    T_FEQ(enemy->health.value, 975, .001f);
    if (thinker) { S_SpellCancelChannel(caster); G_RunEntity(thinker); }
    gi.ModelIndex = old_model_index;
    G_SetSLKRows("AbilityData", previous); free_slk_rows(without_art);
    rain_of_fire_test_done(rows, old);
}

TEST(wc3_ability_lifecycle, pit_lord_rain_of_fire_uses_wave_interval_cap_friendly_fire_and_building_multiplier) {
    slkTestData_t *rows, *old;
    edict_t *caster = rain_of_fire_test_setup(&rows, &old);
    edict_t *enemy = review_unit(1, 300);
    edict_t *ally = review_unit(0, 310);
    edict_t *building = review_unit(1, 320);
    edict_t *outside = review_unit(1, 700);
    edict_t *thinker;
    vec2_t point = { 300, 0 };
    building->s.flags |= EF_BUILDING;
    building->targtype = TARG_STRUCTURE;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("ANrf"), &point));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    if (!thinker) { rain_of_fire_test_done(rows, old); return; }
    T_EQ(thinker->resources, 3u); /* first of four waves fires immediately */
    T_EQ(thinker->freetime, 1500u);
    T_FEQ(enemy->health.value, 984, .001f);
    T_FEQ(ally->health.value, 984, .001f);
    T_FEQ(building->health.value, 992, .001f);
    T_FEQ(outside->health.value, 1000, .001f);
    level.time = 1499; G_RunEntity(thinker);
    T_FEQ(enemy->health.value, 984, .001f);
    level.time = 1500; G_RunEntity(thinker);
    T_EQ(thinker->resources, 2u);
    T_FEQ(enemy->health.value, 968, .001f);
    level.time = 2000; G_RunEntity(thinker);
    T_EQ(thinker->resources, 1u);
    level.time = 2500; G_RunEntity(thinker);
    T_EQ(thinker->resources, 0u);
    level.time = 3000; G_RunEntity(thinker);
    T_ASSERT(!thinker->inuse);
    T_ASSERT(!S_SpellIsChanneling(caster));
    T_FEQ(enemy->health.value, 936, .001f);
    T_FEQ(ally->health.value, 936, .001f);
    T_FEQ(building->health.value, 968, .001f);
    rain_of_fire_test_done(rows, old);
}

TEST(wc3_ability_lifecycle, pit_lord_rain_of_fire_burns_survive_channel_interrupt_and_stack_per_wave) {
    slkTestData_t *rows, *old;
    edict_t *caster = rain_of_fire_test_setup(&rows, &old);
    edict_t *enemy = review_unit(1, 300);
    edict_t *thinker, *first = NULL, *second = NULL;
    vec2_t point = { 300, 0 };
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("ANrf"), &point));
    thinker = review_thinker(caster);
    T_NOT_NULL(thinker);
    if (!thinker) { rain_of_fire_test_done(rows, old); return; }
    T_FEQ(enemy->health.value, 975, .001f);
    FILTER_EDICTS(b, b->think == rain_of_fire_burn_think && b->goalentity == enemy) first = b;
    T_NOT_NULL(first);
    level.time = 1500; G_RunEntity(thinker);
    FILTER_EDICTS(b, b->think == rain_of_fire_burn_think && b->goalentity == enemy && b != first) second = b;
    T_NOT_NULL(second);
    T_FEQ(enemy->health.value, 950, .001f);
    S_SpellCancelChannel(caster);
    G_RunEntity(thinker);
    T_ASSERT(!thinker->inuse);
    T_ASSERT(!S_SpellIsChanneling(caster));
    level.time = 2000;
    if (first) G_RunEntity(first);
    if (second) G_RunEntity(second);
    T_FEQ(enemy->health.value, 943, .001f); /* first landed wave burns */
    level.time = 2500;
    if (first) G_RunEntity(first);
    if (second) G_RunEntity(second);
    T_FEQ(enemy->health.value, 936, .001f); /* second wave burns separately */
    level.time = 3000;
    if (first && first->inuse) G_RunEntity(first);
    if (second && second->inuse) G_RunEntity(second);
    T_ASSERT(first && first->inuse);
    T_FEQ(enemy->health.value, 929, .001f);
    level.time = 3500;
    if (second && second->inuse) G_RunEntity(second);
    T_ASSERT(second && second->inuse);
    T_FEQ(enemy->health.value, 922, .001f);
    level.time = 4000;
    if (first && first->inuse) G_RunEntity(first);
    T_ASSERT(!first || !first->inuse);
    T_FEQ(enemy->health.value, 915, .001f);
    level.time = 4500;
    if (second && second->inuse) G_RunEntity(second);
    T_ASSERT(!second || !second->inuse);
    T_FEQ(enemy->health.value, 908, .001f);
    rain_of_fire_test_done(rows, old);
}

TEST(wc3_ability_lifecycle, pit_lord_rain_of_fire_burn_survives_save_load_without_recasting) {
    cstring_t filename = Test_TempPath("rain-of-fire-burn-save.bin");
    slkTestData_t *rows, *old;
    edict_t *caster = rain_of_fire_test_setup(&rows, &old);
    edict_t *enemy = review_unit(1, 300), *burn = NULL, *thinker;
    vec2_t point = { 300, 0 };
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("ANrf"), &point));
    thinker = review_thinker(caster);
    FILTER_EDICTS(b, b->think == rain_of_fire_burn_think && b->goalentity == enemy) burn = b;
    T_NOT_NULL(burn);
    if (!burn) { rain_of_fire_test_done(rows, old); return; }
    S_SpellCancelChannel(caster);
    if (thinker) G_RunEntity(thinker);
    T_ASSERT(WriteGame(filename));
    burn->think = NULL; burn->resources = 0; burn->goalentity = NULL;
    T_ASSERT(ReadGame(filename));
    T_ASSERT(burn->think == rain_of_fire_burn_think);
    T_EQ(burn->resources, 3u);
    T_ASSERT(S_SpellChannelTarget(burn) == enemy);
    level.time = burn->freetime;
    G_RunEntity(burn);
    T_FEQ(enemy->health.value, 968.0f, .001f);
    T_EQ(burn->resources, 2u);
    remove(filename);
    rain_of_fire_test_done(rows, old);
}
#endif /* BZ_TESTS */
