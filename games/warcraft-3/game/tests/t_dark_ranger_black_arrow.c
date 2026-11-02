#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define DR_BLACK MAKEFOURCC('A','N','b','a')
#define DR_BLACK_CREEP MAKEFOURCC('A','C','b','k')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(cstring_t text);
void free_slk_rows(slkTestData_t *rows);

/* Nonstock mana, damage and projectile art distinguish the authored fields
 * from any fallback values; rank 2 is intentionally stronger than rank 1. */
static char const black_arrow_slk[] =
    "ID;PWXL;N;EBB;Y3;X16\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cost2\"\n"
    "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataA2\"\nC;Y1;X9;K\"Rng1\"\n"
    "C;Y1;X10;K\"UnitID1\"\nC;Y1;X11;K\"DataC1\"\n"
    "C;Y1;X12;K\"Dur1\"\nC;Y1;X13;K\"Dur2\"\n"
    "C;Y1;X14;K\"BuffID1\"\nC;Y1;X15;K\"UnitID2\"\n"
    "C;Y1;X16;K\"DataC2\"\n"
    "C;Y2;X1;K\"ANba\"\nC;Y2;X2;K\"ANba\"\nC;Y2;X3;K2\n"
    "C;Y2;X4;K\"enemy,organic,ground,air\"\nC;Y2;X5;K13\nC;Y2;X6;K17\n"
    "C;Y2;X7;K15\nC;Y2;X8;K24\nC;Y2;X9;K600\n"
    "C;Y2;X10;K\"hfoo\"\nC;Y2;X11;K12\n"
    "C;Y2;X12;K3\nC;Y2;X13;K1.5\nC;Y2;X14;K\"BNba\"\n"
    "C;Y2;X15;K\"hpea\"\nC;Y2;X16;K6\n"
    "C;Y3;X1;K\"ACbk\"\nC;Y3;X2;K\"ANba\"\nC;Y3;X3;K1\n"
    "C;Y3;X4;K\"enemy,organic,ground,air\"\nC;Y3;X5;K9\n"
    "C;Y3;X7;K4\nC;Y3;X9;K600\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *archer, *enemy, *ally, *mechanical;
} drBlackFix_t;

static void dr_black_setup(drBlackFix_t *fix) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(black_arrow_slk);
    fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->archer = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 0, 0);
    fix->enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    fix->ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);
    fix->mechanical = alloc_test_unit(MAKEFOURCC('h','m','t','t'), 140, 0);
    edict_t *all[] = { fix->archer, fix->enemy, fix->ally, fix->mechanical };
    uint32_t const all_count = (uint32_t)(sizeof(all) / sizeof(all[0]));
    FOR_LOOP(i, all_count) {
        all[i]->svflags |= SVF_MONSTER;
        all[i]->die = unit_die;
        all[i]->targtype = TARG_GROUND;
        all[i]->health.value = all[i]->health.max_value = 1000;
        all[i]->unit_state->armor_value = 0;
    }
    fix->archer->s.player = fix->ally->s.player = 0;
    fix->enemy->s.player = fix->mechanical->s.player = 1;
    fix->mechanical->targtype = TARG_MECHANICAL;
    fix->archer->unit_state->attack1.weapon = WPN_MISSILE;
    fix->archer->unit_state->attack1.type = ATK_NORMAL;
    fix->archer->unit_state->attack1.targetsAllowed =
        G_TargetFlagForType(TARG_GROUND) | G_TargetFlagForType(TARG_MECHANICAL);
    fix->archer->unit_state->ranks[0] = MAKE(heroability_t, .code = DR_BLACK, .level = 1);
    fix->archer->mana.value = fix->archer->mana.max_value = 100;
}

static void dr_black_done(drBlackFix_t *fix) {
    G_SetSLKRows("AbilityData", fix->old);
    free_slk_rows(fix->rows);
}

TEST(wc3_spell, dark_ranger_black_arrow_manual_attack_spends_once_and_retains_launch_rank) {
    drBlackFix_t fix;
    uint32_t code = 0, level = 0;
    dr_black_setup(&fix);
    T_EQ(S_AbilityItem(DR_BLACK).ability->proc, CAbilityBlackArrow);
    T_EQ(S_AbilityItem(DR_BLACK_CREEP).ability->proc, CAbilityBlackArrow);
    T_STREQ(G_AbilityEffectArt(DR_BLACK, WC3_EFFECT_MISSILE, 0),
             "TestUI\\Models\\anim_projectile.mdx");
    T_ASSERT(S_BlackArrowIssueOrder(fix.archer, fix.enemy, DR_BLACK));
    T_ASSERT(!G_UnitAutocastIsOn(fix.archer, DR_BLACK));
    T_FEQ(fix.archer->mana.value, 100, 0.001f);
    T_ASSERT(S_BlackArrowPrepareAttack(fix.archer, fix.enemy, &code, &level));
    T_EQ(code, DR_BLACK); T_EQ(level, 1u);
    T_FEQ(fix.archer->mana.value, 87, 0.001f);
    /* The already-launched shot retains rank 1 after ability learning and
     * disabling autocast; neither change may create a second mana charge. */
    fix.archer->unit_state->ranks[0].level = 2;
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 100, code, level);
    T_FEQ(fix.enemy->health.value, 885, 0.001f);
    T_FEQ(fix.archer->mana.value, 87, 0.001f);
    code = level = 0;
    T_ASSERT(!S_BlackArrowPrepareAttack(fix.archer, fix.enemy, &code, &level));
    T_EQ(code, 0u);
    dr_black_done(&fix);
}

TEST(wc3_spell, dark_ranger_black_arrow_autocast_checks_mana_and_target_mask) {
    drBlackFix_t fix;
    uint32_t code = 0, level = 0;
    dr_black_setup(&fix);
    T_ASSERT(G_SetUnitAutocast(fix.archer, DR_BLACK, true));
    T_ASSERT(G_UnitAutocastIsOn(fix.archer, DR_BLACK));
    T_ASSERT(!S_BlackArrowPrepareAttack(fix.archer, fix.ally, &code, &level));
    T_ASSERT(!S_BlackArrowPrepareAttack(fix.archer, fix.mechanical, &code, &level));
    T_FEQ(fix.archer->mana.value, 100, 0.001f);
    T_ASSERT(S_BlackArrowPrepareAttack(fix.archer, fix.enemy, &code, &level));
    T_EQ(code, DR_BLACK); T_EQ(level, 1u);
    T_FEQ(fix.archer->mana.value, 87, 0.001f);
    T_ASSERT(G_SetUnitAutocast(fix.archer, DR_BLACK, false));
    T_ASSERT(!G_UnitAutocastIsOn(fix.archer, DR_BLACK));
    /* Impact still uses the launch-time selection despite later toggle-off. */
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 100, code, level);
    T_FEQ(fix.enemy->health.value, 885, 0.001f);
    fix.archer->mana.value = 4;
    T_ASSERT(G_SetUnitAutocast(fix.archer, DR_BLACK, true));
    code = level = 1;
    T_ASSERT(!S_BlackArrowPrepareAttack(fix.archer, fix.enemy, &code, &level));
    T_EQ(code, 0u); T_EQ(level, 0u);
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 100, code, level);
    T_FEQ(fix.enemy->health.value, 785, 0.001f);
    T_FEQ(fix.archer->mana.value, 4, 0.001f);
    dr_black_done(&fix);
}

/* Exercise the real attack damage-point callback and the moving projectile:
 * art, speed, mana and metadata are committed at launch, not impact. */
TEST(wc3_spell, dark_ranger_black_arrow_missile_preserves_launch_snapshot) {
    drBlackFix_t fix;
    edict_t *missile = NULL;
    uint32_t missile_index = 0;
    dr_black_setup(&fix);
    fix.archer->unit_state->attack1.damageBase = 100;
    fix.archer->unit_state->attack1.numberOfDice = 0;
    fix.archer->unit_state->attack1.range = 600.0f;
    fix.archer->unit_state->attack1.cooldown = 1.0f;
    fix.archer->unit_state->attack1.damagePoint = 0.1f;
    fix.archer->unit_state->attack1.projectile.speed = 600.0f;
    fix.archer->unit_state->attack1.projectile.model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
    T_ASSERT(G_SetUnitAutocast(fix.archer, DR_BLACK, true));
    fix.archer->goalentity = fix.enemy;
    attack_ranged(fix.archer);
    fix.archer->wait = 0.01f;
    T_NOT_NULL(fix.archer->currentmove);
    if (fix.archer->currentmove && fix.archer->currentmove->think)
        fix.archer->currentmove->think(fix.archer);
    FILTER_EDICTS(ent, ent->owner == fix.archer && ent->movetype == MOVETYPE_FLYMISSILE) {
        missile = ent; break;
    }
    T_NOT_NULL(missile);
    if (missile) {
        missile_index = missile->s.number;
        T_NOT_NULL(missile->missile);
        if (missile->missile) {
            T_EQ(missile->missile->arrow_ability, DR_BLACK);
            T_EQ(missile->missile->arrow_level, 1u);
        }
        T_EQ(missile->s.model, G_RegisterModel("TestUI\\Models\\anim_projectile.mdx"));
        T_FEQ(missile->velocity, 0.790f, 0.001f);
        T_FEQ(fix.archer->mana.value, 87.0f, 0.001f);
        T_ASSERT(G_SetUnitAutocast(fix.archer, DR_BLACK, false));
        fix.archer->unit_state->ranks[0].level = 2;
        FOR_LOOP(i, 100) {
            if (!g_edicts[missile_index].inuse) break;
            SV_Physics_Toss(&g_edicts[missile_index]);
        }
        T_ASSERT(!g_edicts[missile_index].inuse);
        T_FEQ(fix.archer->mana.value, 87.0f, 0.001f);
        T_ASSERT(fix.enemy->health.value < fix.enemy->health.max_value);
    }
    dr_black_done(&fix);
}

TEST(wc3_spell, dark_ranger_black_arrow_manual_rejects_invalid_target_without_mana) {
    drBlackFix_t fix;
    dr_black_setup(&fix);
    T_ASSERT(!S_BlackArrowIssueOrder(fix.archer, fix.ally, DR_BLACK));
    T_ASSERT(!S_BlackArrowIssueOrder(fix.archer, fix.mechanical, DR_BLACK));
    T_FEQ(fix.archer->mana.value, 100, 0.001f);
    dr_black_done(&fix);
}

/* The synthetic data uses a Footman/Peasant as Dark Minion stand-ins so a
 * headless test archive need not contain retail ndr1/ndr2 unit tables.
 * DeathType is supplied explicitly because the small test SLK omits that
 * column; it must match retail corpse-capable units (both Raise and Decay). */
static void dr_black_corpses(drBlackFix_t *fix, bool enabled) {
    static UnitData_t corpse_data;
    UnitData_t const *original = fix->enemy->data.UnitData;
    corpse_data = original ? *original : (UnitData_t){ 0 };
    corpse_data.deathType = enabled ? 3u : 0u;
    fix->enemy->data.UnitData = &corpse_data;
}

static uint32_t dr_black_minion_count(edict_t *caster, uint32_t unit_id) {
    uint32_t count = 0;
    FILTER_EDICTS(ent, ent->summon_ability == DR_BLACK && ent->owner == caster &&
                  (!unit_id || ent->class_id == unit_id)) count++;
    return count;
}

TEST(wc3_spell, dark_ranger_black_arrow_marks_survivor_then_other_attack_summons) {
    drBlackFix_t fix;
    heroabilitystatus_t *mark;
    dr_black_setup(&fix);
    dr_black_corpses(&fix, true);
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 40, DR_BLACK, 1);
    mark = unit_findstatus(fix.enemy, MAKEFOURCC('B','N','b','a'));
    T_NOT_NULL(mark);
    if (mark) {
        T_EQ(mark->source_ability, DR_BLACK);
        T_EQ(mark->rank, 1u);
        T_ASSERT(mark->source == fix.archer);
        T_EQ(mark->timestamp, G_Time() + 3000u);
        T_ASSERT(mark->buff_flags & WC3_STATUS_BUFF_NEGATIVE);
    }
    T_EQ(dr_black_minion_count(fix.archer, 0), 0u);
    /* The original Archer does not need to provide the killing blow. */
    T_Damage(fix.enemy, fix.ally, 2000);
    T_ASSERT(M_IsDead(fix.enemy));
    T_EQ(dr_black_minion_count(fix.archer, MAKEFOURCC('h','f','o','o')), 1u);
    T_ASSERT(fix.enemy->aiflags & AI_CORPSE_UNRAISABLE);
    T_ASSERT(fix.enemy->aiflags & AI_CORPSE_NO_DECAY);
    T_ASSERT(!G_UnitIsRaisableCorpse(fix.enemy));
    T_NULL(unit_findstatus(fix.enemy, MAKEFOURCC('B','N','b','a')));
    /* Repeated death notifications must never duplicate a summon. */
    unit_die(fix.enemy, fix.ally);
    T_EQ(dr_black_minion_count(fix.archer, 0), 1u);
    dr_black_done(&fix);
}

TEST(wc3_spell, dark_ranger_black_arrow_lethal_hit_summons_once_and_uses_launch_rank) {
    drBlackFix_t fix;
    dr_black_setup(&fix);
    dr_black_corpses(&fix, true);
    fix.enemy->health.value = 30;
    /* A level-up between shot launch and impact cannot choose a new minion. */
    fix.archer->unit_state->ranks[0].level = 2;
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 100, DR_BLACK, 1);
    T_EQ(dr_black_minion_count(fix.archer, MAKEFOURCC('h','f','o','o')), 1u);
    T_EQ(dr_black_minion_count(fix.archer, MAKEFOURCC('h','p','e','a')), 0u);
    dr_black_done(&fix);
}

TEST(wc3_spell, dark_ranger_black_arrow_expiry_and_no_corpse_prevent_summon) {
    drBlackFix_t fix;
    dr_black_setup(&fix);
    dr_black_corpses(&fix, true);
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 20, DR_BLACK, 1);
    T_NOT_NULL(unit_findstatus(fix.enemy, MAKEFOURCC('B','N','b','a')));
    level.time += 3001;
    T_Damage(fix.enemy, fix.ally, 2000);
    T_EQ(dr_black_minion_count(fix.archer, 0), 0u);
    dr_black_done(&fix);

    dr_black_setup(&fix);
    dr_black_corpses(&fix, false);
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 20, DR_BLACK, 1);
    T_NULL(unit_findstatus(fix.enemy, MAKEFOURCC('B','N','b','a')));
    T_Damage(fix.enemy, fix.ally, 2000);
    T_EQ(dr_black_minion_count(fix.archer, 0), 0u);
    dr_black_done(&fix);
}

TEST(wc3_spell, dark_ranger_black_arrow_refresh_replaces_owner_and_rank) {
    drBlackFix_t fix;
    heroabilitystatus_t *mark;
    dr_black_setup(&fix);
    dr_black_corpses(&fix, true);
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 20, DR_BLACK, 1);
    mark = unit_findstatus(fix.enemy, MAKEFOURCC('B','N','b','a'));
    T_NOT_NULL(mark);
    level.time += 1000;
    /* Another Black Arrow rank refreshes the same debuff; the most recent
     * attack determines which level of Dark Minion is produced. */
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 20, DR_BLACK, 2);
    T_ASSERT(unit_findstatus(fix.enemy, MAKEFOURCC('B','N','b','a')) == mark);
    if (mark) {
        T_EQ(mark->rank, 2u);
        T_EQ(mark->timestamp, G_Time() + 1500u);
    }
    T_Damage(fix.enemy, fix.ally, 2000);
    T_EQ(dr_black_minion_count(fix.archer, MAKEFOURCC('h','p','e','a')), 1u);
    T_EQ(dr_black_minion_count(fix.archer, MAKEFOURCC('h','f','o','o')), 0u);
    dr_black_done(&fix);
}

TEST(wc3_spell, dark_ranger_black_arrow_invulnerable_and_stale_caster_do_not_summon) {
    drBlackFix_t fix;
    dr_black_setup(&fix);
    dr_black_corpses(&fix, true);
    G_SetUnitFlag(fix.enemy, UNIT_FLAG_INVULNERABLE, true);
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 20, DR_BLACK, 1);
    T_NULL(unit_findstatus(fix.enemy, MAKEFOURCC('B','N','b','a')));
    G_SetUnitFlag(fix.enemy, UNIT_FLAG_INVULNERABLE, false);
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 20, DR_BLACK, 1);
    heroabilitystatus_t *mark = unit_findstatus(fix.enemy, MAKEFOURCC('B','N','b','a'));
    T_NOT_NULL(mark);
    if (mark) mark->source_spawn_time++;
    T_Damage(fix.enemy, fix.ally, 2000);
    T_EQ(dr_black_minion_count(fix.archer, 0), 0u);
    dr_black_done(&fix);
}

TEST(wc3_save, dark_ranger_black_arrow_mark_survives_save_and_source_identity) {
    drBlackFix_t fix;
    char const *filename = Test_TempPath("wc3-black-arrow-mark.bin");
    heroabilitystatus_t *mark;
    dr_black_setup(&fix);
    dr_black_corpses(&fix, true);
    S_ResolveAttackHitWithArrow(fix.archer, fix.enemy, 20, DR_BLACK, 1);
    mark = unit_findstatus(fix.enemy, MAKEFOURCC('B','N','b','a'));
    T_NOT_NULL(mark);
    T_ASSERT(WriteGame(filename));
    if (mark) {
        mark->source = NULL;
        mark->source_spawn_time = mark->source_ability = mark->rank = mark->timestamp = 0;
    }
    T_ASSERT(ReadGame(filename));
    mark = unit_findstatus(fix.enemy, MAKEFOURCC('B','N','b','a'));
    T_NOT_NULL(mark);
    if (mark) {
        T_EQ(mark->source_ability, DR_BLACK);
        T_EQ(mark->rank, 1u);
        T_ASSERT(S_SpellStatusSource(mark) == fix.archer);
        T_EQ(mark->timestamp, 4000u);
    }
    /* Completing the lifecycle after restoring the source pointer proves
     * that a saved mark remains an actual death trigger, not inert metadata. */
    dr_black_corpses(&fix, true);
    T_Damage(fix.enemy, fix.ally, 2000);
    T_EQ(dr_black_minion_count(fix.archer, MAKEFOURCC('h','f','o','o')), 1u);
    remove(filename);
    dr_black_done(&fix);
}
#endif
