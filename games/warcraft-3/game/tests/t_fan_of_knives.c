#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define FAN_OF_KNIVES MAKEFOURCC('A','E','f','k')
#define CUSTOM_FAN MAKEFOURCC('A','0','f','k')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);
void SV_Physics_Toss(edict_t *ent);

/* Stock-shaped 1.36.2 row and an intentionally non-stock alias: the cap must
 * derive from each row's DataA/DataB rather than a fixed target count. */
static char const fok_slk[] =
    "ID;PWXL;N;EBB;Y3;X19\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
    "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n"
    "C;Y1;X10;K\"Cost2\"\nC;Y1;X11;K\"Cool2\"\nC;Y1;X12;K\"Area2\"\n"
    "C;Y1;X13;K\"DataA2\"\nC;Y1;X14;K\"DataB2\"\n"
    "C;Y1;X15;K\"Cost3\"\nC;Y1;X16;K\"Cool3\"\nC;Y1;X17;K\"Area3\"\n"
    "C;Y1;X18;K\"DataA3\"\nC;Y1;X19;K\"DataB3\"\n"
    "C;Y2;X1;K\"AEfk\"\nC;Y2;X2;K\"AEfk\"\nC;Y2;X3;K\"3\"\n"
    "C;Y2;X4;K\"air,ground,enemy,organic\"\nC;Y2;X5;K\"100\"\n"
    "C;Y2;X6;K\"9\"\nC;Y2;X7;K\"400\"\nC;Y2;X8;K\"70\"\nC;Y2;X9;K\"420\"\n"
    "C;Y2;X10;K\"100\"\nC;Y2;X11;K\"9\"\nC;Y2;X12;K\"450\"\n"
    "C;Y2;X13;K\"130\"\nC;Y2;X14;K\"780\"\n"
    "C;Y2;X15;K\"100\"\nC;Y2;X16;K\"9\"\nC;Y2;X17;K\"475\"\n"
    "C;Y2;X18;K\"200\"\nC;Y2;X19;K\"1200\"\n"
    "C;Y3;X1;K\"A0fk\"\nC;Y3;X2;K\"AEfk\"\nC;Y3;X3;K\"3\"\n"
    "C;Y3;X4;K\"air,ground,enemy,organic\"\nC;Y3;X5;K\"0\"\n"
    "C;Y3;X6;K\"0\"\nC;Y3;X7;K\"250\"\nC;Y3;X8;K\"51\"\nC;Y3;X9;K\"110\"\n"
    "C;Y3;X10;K\"0\"\nC;Y3;X11;K\"0\"\nC;Y3;X12;K\"350\"\n"
    "C;Y3;X13;K\"53\"\nC;Y3;X14;K\"110\"\n"
    "C;Y3;X15;K\"100\"\nC;Y3;X16;K\"9\"\nC;Y3;X17;K\"475\"\n"
    "C;Y3;X18;K\"55\"\nC;Y3;X19;K\"110\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *caster;
    edict_t *enemy[8];
    edict_t *ally;
    bool old_constants_loaded;
} fokFixture_t;

static fokFixture_t fok_setup(uint32_t code) {
    fokFixture_t f = { 0 };
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    f.rows = parse_slk_string(fok_slk);
    f.old = G_SetSLKRows("AbilityData", f.rows);
    f.old_constants_loaded = game.constants.combatConstantsLoaded;
    game.constants.combatConstantsLoaded = false;
    f.caster = alloc_test_unit(MAKEFOURCC('E','w','d','n'), 0, 0);
    f.caster->s.player = 0;
    f.caster->svflags |= SVF_MONSTER;
    f.caster->mana.value = f.caster->mana.max_value = 500.0f;
    /* Stock AEfk is in the Warden's hero ability list; add only the custom
     * alias through the runtime skill override before assigning its rank. */
    if (code == CUSTOM_FAN) T_ASSERT(G_ActorAddSkill(f.caster, code));
    f.caster->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);
    FOR_LOOP(i, 8) {
        f.enemy[i] = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20.0f + 20.0f * i, 0);
        f.enemy[i]->s.player = 1;
        f.enemy[i]->svflags |= SVF_MONSTER;
        f.enemy[i]->targtype = TARG_GROUND;
        f.enemy[i]->unit_state->defense_type = 4; /* normal */
        f.enemy[i]->health.value = f.enemy[i]->health.max_value = 500.0f;
    }
    f.ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 30, 0);
    f.ally->s.player = 0;
    f.ally->svflags |= SVF_MONSTER;
    f.ally->targtype = TARG_GROUND;
    f.ally->health.value = f.ally->health.max_value = 500.0f;
    return f;
}

static void fok_done(fokFixture_t f) {
    game.constants.combatConstantsLoaded = f.old_constants_loaded;
    G_SetSLKRows("AbilityData", f.old);
    free_slk_rows(f.rows);
}

TEST(wc3_spell, fan_of_knives_stock_models_are_presented_from_archive_paths) {
    setup_test_world();
    /* The test archive deliberately omits stock AEfk art. The resolver uses
     * the two documented model names only when the data has no art entry. */
    T_STREQ(G_AbilityEffectArt(FAN_OF_KNIVES, WC3_EFFECT_CASTER, 0),
            "Abilities\\Spells\\NightElf\\FanOfKnives\\FanOfKnivesCaster.mdx");
    T_STREQ(G_AbilityEffectArt(FAN_OF_KNIVES, WC3_EFFECT_MISSILE, 0),
            "Abilities\\Spells\\NightElf\\FanOfKnives\\FanOfKnivesMissile.mdx");
}

TEST(wc3_spell, fan_of_knives_custom_art_spawns_radial_missiles) {
    fokFixture_t f = fok_setup(CUSTOM_FAN);
    uint32_t caster_model = G_RegisterModel("TestUI\\Models\\anim_oneshot.mdx");
    uint32_t knife_model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
    int caster_count = 0, knife_count = 0;
    edict_t *first_knife = NULL;
    T_STREQ(G_AbilityEffectArt(CUSTOM_FAN, WC3_EFFECT_CASTER, 0),
            "TestUI\\Models\\anim_oneshot.mdx");
    T_STREQ(G_AbilityEffectArt(CUSTOM_FAN, WC3_EFFECT_MISSILE, 0),
            "TestUI\\Models\\anim_pulse.mdx");
    T_ASSERT(S_CastNoTargetSpell(f.caster, CUSTOM_FAN));
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = globals.edicts + i;
        if (!ent->inuse || !(ent->s.flags & EF_NOT_SELECTABLE)) continue;
        if (ent->s.model == caster_model) caster_count++;
        if (ent->s.model == knife_model) knife_count++;
        if (ent->s.model == caster_model || ent->s.model == knife_model) {
            T_NULL(ent->goalentity); /* no enemy-tracking visuals */
            T_FEQ(ent->s.origin2.x, f.caster->s.origin2.x, 0.001f);
            T_FEQ(ent->s.origin2.y, f.caster->s.origin2.y, 0.001f);
            T_NOT_NULL(ent->animation);
            if (ent->s.model == knife_model) {
                channel_t const *flight = G_GetChannel(ent);
                if (!first_knife) first_knife = ent;
                T_EQ(ent->movetype, MOVETYPE_FLYMISSILE);
                T_ASSERT(ent->aiflags & AI_PROJECTILE_FIXED_TARGET);
                T_EQ(ent->owner, f.caster);
                T_ASSERT(ent->velocity > 0.0f);
                T_NOT_NULL(flight);
                T_FEQ(Vector2_distance(&flight->origin, &f.caster->s.origin2),
                       250.0f, 0.01f); /* custom Area1, not DataB cap */
            }
        }
    }
    T_EQ(caster_count, 1);
    T_EQ(knife_count, 8);
    T_NOT_NULL(first_knife);
    {
        float const initial_health = f.enemy[0]->health.value;
        float const initial_x = first_knife->s.origin.x;
        float const initial_y = first_knife->s.origin.y;
        SV_Physics_Toss(first_knife);
        T_ASSERT(first_knife->inuse);
        T_ASSERT(fabsf(first_knife->s.origin.x - initial_x) > 0.01f ||
                 fabsf(first_knife->s.origin.y - initial_y) > 0.01f);
        /* Moving and arriving at the visual endpoint must not reapply damage. */
        for (int tick = 0; tick < 30 && first_knife->inuse; tick++)
            SV_Physics_Toss(first_knife);
        T_ASSERT(!first_knife->inuse);
        T_FEQ(f.enemy[0]->health.value, initial_health, .001f);
    }
    fok_done(f);
}


TEST(wc3_spell, fan_of_knives_level_two_increases_visual_density) {
    fokFixture_t f = fok_setup(CUSTOM_FAN);
    uint32_t const knife_model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
    int knife_count = 0;
    FOR_LOOP(i, 8) G_FreeEdict(f.enemy[i]);
    G_FreeEdict(f.ally);
    f.caster->unit_state->ranks[0].level = 2;
    T_ASSERT(S_CastNoTargetSpell(f.caster, CUSTOM_FAN));
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = globals.edicts + i;
        if (!ent->inuse || ent->s.model != knife_model ||
            !(ent->s.flags & EF_NOT_SELECTABLE)) continue;
        channel_t const *flight = G_GetChannel(ent);
        knife_count++;
        T_NOT_NULL(flight);
        T_FEQ(Vector2_distance(&flight->origin, &f.caster->s.origin2),
               350.0f, 0.01f);
    }
    T_EQ(knife_count, 12);
    fok_done(f);
}

TEST(wc3_spell, fan_of_knives_level_three_visuals_do_not_require_enemies) {
    fokFixture_t f = fok_setup(CUSTOM_FAN);
    uint32_t const knife_model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
    uint32_t const caster_model = G_RegisterModel("TestUI\\Models\\anim_oneshot.mdx");
    int knife_count = 0, caster_count = 0;
    FOR_LOOP(i, 8) G_FreeEdict(f.enemy[i]);
    G_FreeEdict(f.ally);
    f.caster->unit_state->ranks[0].level = 3;
    T_ASSERT(S_CastNoTargetSpell(f.caster, CUSTOM_FAN));
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = globals.edicts + i;
        if (!ent->inuse || !(ent->s.flags & EF_NOT_SELECTABLE)) continue;
        if (ent->s.model == caster_model) caster_count++;
        if (ent->s.model == knife_model) {
            channel_t const *flight = G_GetChannel(ent);
            knife_count++;
            T_EQ(ent->movetype, MOVETYPE_FLYMISSILE);
            T_NOT_NULL(flight);
            T_FEQ(Vector2_distance(&flight->origin, &f.caster->s.origin2),
                   475.0f, 0.01f); /* custom third-level Area3 */
        }
    }
    T_EQ(caster_count, 1);
    T_EQ(knife_count, 16); /* visual density, independent of number of victims */
    T_FEQ(f.caster->mana.value, 400.0f, .001f);
    fok_done(f);
}

TEST(wc3_spell, fan_of_knives_limits_victims_instead_of_distributing_damage) {
    fokFixture_t f = fok_setup(FAN_OF_KNIVES);
    int hit = 0;
    T_EQ(S_AbilityItem(FAN_OF_KNIVES).ability->proc, CAbilityFanOfKnives);
    T_ASSERT(S_CastNoTargetSpell(f.caster, FAN_OF_KNIVES));
    FOR_LOOP(i, 8) {
        if (f.enemy[i]->health.value < 500.0f) {
            T_FEQ(f.enemy[i]->health.value, 430.0f, 0.001f);
            hit++;
        }
    }
    T_EQ(hit, 6);
    T_FEQ(f.ally->health.value, 500.0f, 0.001f);
    T_FEQ(f.caster->mana.value, 400.0f, 0.001f);
    T_ASSERT(!S_CastNoTargetSpell(f.caster, FAN_OF_KNIVES));
    fok_done(f);
}

TEST(wc3_spell, fan_of_knives_custom_alias_uses_authored_cap_and_area) {
    fokFixture_t f = fok_setup(CUSTOM_FAN);
    int hit = 0;
    T_EQ(S_AbilityItem(CUSTOM_FAN).ability->proc, CAbilityFanOfKnives);
    f.enemy[7]->s.origin2.x = 300.0f; /* outside custom Area1=250 */
    T_ASSERT(S_CastNoTargetSpell(f.caster, CUSTOM_FAN));
    FOR_LOOP(i, 8) {
        if (f.enemy[i]->health.value < 500.0f) {
            T_FEQ(f.enemy[i]->health.value, 449.0f, 0.001f);
            hit++;
        }
    }
    T_EQ(hit, 2); /* floor(DataB=110 / DataA=51) */
    T_FEQ(f.enemy[7]->health.value, 500.0f, 0.001f);
    fok_done(f);
}

TEST(wc3_spell, fan_of_knives_enhanced_physical_ignores_armor_not_hero_type) {
    fokFixture_t f = fok_setup(FAN_OF_KNIVES);
    /* Only two targets remain. Both have massive numeric armor, but the hero
     * still receives ATK_SPELLS x Hero-defense 0.70 damage. */
    FOR_LOOP(i, 8) if (i >= 2) G_FreeEdict(f.enemy[i]);
    G_ApplyTemporaryArmorBonus(f.enemy[0], 100.0f);
    G_ApplyTemporaryArmorBonus(f.enemy[1], 100.0f);
    f.enemy[1]->unit_state->defense_type = 5; /* hero */
    T_ASSERT(S_CastNoTargetSpell(f.caster, FAN_OF_KNIVES));
    T_FEQ(f.enemy[0]->health.value, 430.0f, 0.001f);
    T_FEQ(f.enemy[1]->health.value, 451.0f, 0.001f);
    fok_done(f);
}

TEST(wc3_spell, fan_of_knives_rejects_ethereal_and_magic_immune_before_cap) {
    fokFixture_t f = fok_setup(CUSTOM_FAN);
    T_NOT_NULL(unit_applystatus(f.enemy[0], &(status_application_t){
        .buff = "Bban", .level = 1, .duration = 30.0f,
        .state_mask = WC3_STATUS_STATE_ETHEREAL
    }));
    T_NOT_NULL(unit_applystatus(f.enemy[1], &(status_application_t){
        .buff = "Biyy", .level = 1, .duration = 30.0f,
        .state_mask = WC3_STATUS_STATE_MAGIC_IMMUNE
    }));
    T_ASSERT(S_CastNoTargetSpell(f.caster, CUSTOM_FAN));
    T_FEQ(f.enemy[0]->health.value, 500.0f, 0.001f);
    T_FEQ(f.enemy[1]->health.value, 500.0f, 0.001f);
    T_FEQ(f.enemy[2]->health.value, 449.0f, 0.001f);
    T_FEQ(f.enemy[3]->health.value, 449.0f, 0.001f);
    T_FEQ(f.enemy[4]->health.value, 500.0f, 0.001f);
    fok_done(f);
}

TEST(wc3_spell, fan_of_knives_physical_damage_bypasses_magic_absorb_pool) {
    fokFixture_t f = fok_setup(FAN_OF_KNIVES);
    heroabilitystatus_t *shield;
    FOR_LOOP(i, 8) if (i >= 1) G_FreeEdict(f.enemy[i]);
    shield = unit_applystatus(f.enemy[0], &(status_application_t){
        .buff = "Bam2", .level = 1, .duration = 30.0f
    });
    T_NOT_NULL(shield);
    shield->data = 300; /* Bam2 absorbs S_SpellDamage, not physical damage */
    T_ASSERT(S_CastNoTargetSpell(f.caster, FAN_OF_KNIVES));
    T_FEQ(f.enemy[0]->health.value, 430.0f, 0.001f);
    T_EQ(shield->data, 300);
    fok_done(f);
}

TEST(wc3_spell, fan_of_knives_obeys_loaded_spell_attack_multiplier) {
    fokFixture_t f = fok_setup(FAN_OF_KNIVES);
    float const old_value = game.constants.damageBonus[ATK_SPELLS][4];
    FOR_LOOP(i, 8) if (i >= 1) G_FreeEdict(f.enemy[i]);
    game.constants.combatConstantsLoaded = true;
    game.constants.damageBonus[ATK_SPELLS][4] = 0.5f;
    T_ASSERT(S_CastNoTargetSpell(f.caster, FAN_OF_KNIVES));
    T_FEQ(f.enemy[0]->health.value, 465.0f, 0.001f);
    game.constants.damageBonus[ATK_SPELLS][4] = old_value;
    fok_done(f);
}
#endif
