#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_AVATAR MAKEFOURCC('A', 'H', 'a', 'v')
#define BZ_AVATAR_BUFF MAKEFOURCC('B', 'H', 'a', 'v')

LPEDICT alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

typedef struct { slkTestData_t *rows, *old; LPEDICT unit; } AVFIX;
static const char avatar_slk[] =
    "ID;PWXL;N;EBB;Y2;X15\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"Cost1\"\n"
    "C;Y1;X5;K\"Cool1\"\n"
    "C;Y1;X6;K\"Dur1\"\n"
    "C;Y1;X7;K\"DataA1\"\n"
    "C;Y1;X8;K\"DataB1\"\n"
    "C;Y1;X9;K\"DataC1\"\n"
    "C;Y1;X10;K\"Cost2\"\n"
    "C;Y1;X11;K\"Cool2\"\n"
    "C;Y1;X12;K\"Dur2\"\n"
    "C;Y1;X13;K\"DataA2\"\n"
    "C;Y1;X14;K\"DataB2\"\n"
    "C;Y1;X15;K\"DataC2\"\n"
    "C;Y2;X1;K\"AHav\"\n"
    "C;Y2;X2;K\"AHav\"\n"
    "C;Y2;X3;K\"2\"\n"
    "C;Y2;X4;K\"25\"\n"
    "C;Y2;X5;K\"90\"\n"
    "C;Y2;X6;K\"2\"\n"
    "C;Y2;X7;K\"5\"\n"
    "C;Y2;X8;K\"500\"\n"
    "C;Y2;X9;K\"20\"\n"
    "C;Y2;X10;K\"35\"\n"
    "C;Y2;X11;K\"90\"\n"
    "C;Y2;X12;K\"3\"\n"
    "C;Y2;X13;K\"7\"\n"
    "C;Y2;X14;K\"600\"\n"
    "C;Y2;X15;K\"31\"\n"
    "E\n";

/* Synthetic level-two data distinguishes row lookup from hardcoded retail constants. */
static AVFIX avatar_setup(uint32_t rank) {
    reset_entities(); setup_test_world(); level.time = 1000;
    AVFIX fix = { .rows = parse_slk_string(avatar_slk), .unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0) };
    fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.unit->heroabilities[0] = (heroability_t){ .code = BZ_AVATAR, .level = rank };
    fix.unit->hero.str = 22; fix.unit->hero.agi = 13; fix.unit->hero.intel = 17;
    fix.unit->health.value = 400; fix.unit->health.max_value = 650;
    fix.unit->mana.value = fix.unit->mana.max_value = 100;
    fix.unit->armor_value = 5; fix.unit->temporary_armor_bonus = 1.1f;
    fix.unit->attack1.temporaryDamageBonus = 3; fix.unit->attack2.temporaryDamageBonus = 4;
    fix.unit->svflags |= SVF_MONSTER; fix.unit->movetype = MOVETYPE_NONE;
    fix.unit->stand = unit_stand; fix.unit->die = unit_die;
    return fix;
}

static void avatar_done(AVFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

TEST(wc3_avatar, cast_expire_recast_keeps_other_bonuses) {
    AVFIX fix = avatar_setup(1);
    LPEDICT unit = fix.unit;
    T_ASSERT(unit_issueimmediateorder(unit, "avatar"));
    T_FEQ(unit->mana.value, 75, 0.001f);
    T_FEQ(unit->health.max_value, 1150, 0.001f);
    T_FEQ(unit->health.value, 900, 0.001f);
    T_FEQ(G_UnitArmorValue(unit), 10, 0.001f);
    T_FEQ(unit->attack1.temporaryDamageBonus, 23, 0.001f);
    T_FEQ(unit->attack2.temporaryDamageBonus, 24, 0.001f);
    T_ASSERT(S_UnitSpellImmune(unit));
    T_STREQ(unit->animation_props, "alternate");
    T_ASSERT(!unit_issueimmediateorder(unit, "avatar"));
    T_FEQ(unit->mana.value, 75, 0.001f);
    unit->health.value = 300;
    level.time += 2000; unit_updatestatuses(unit);
    T_FEQ(unit->health.value, 300, 0.001f);
    T_FEQ(unit->health.max_value, 650, 0.001f);
    T_FEQ(unit->armor_value, 5, 0.001f);
    T_FEQ(unit->attack1.temporaryDamageBonus, 3, 0.001f);
    T_FEQ(unit->attack2.temporaryDamageBonus, 4, 0.001f);
    T_ASSERT(!S_UnitSpellImmune(unit));
    T_STREQ(unit->animation_props, "");
    S_AvatarExpire(unit);
    T_FEQ(unit->health.max_value, 650, 0.001f);
    level.time += 90000; unit_updatestatuses(unit);
    T_ASSERT(unit_issueimmediateorder(unit, "avatar"));
    T_FEQ(unit->health.value, 800, 0.001f);
    level.time += 2000; unit_updatestatuses(unit);
    T_FEQ(unit->health.value, 650, 0.001f);
    avatar_done(fix);
}

TEST(wc3_avatar, runtime_health_bonus_publishes_life_limit_events) {
    AVFIX fix = avatar_setup(1);
    LPEDICT unit = fix.unit;
    LPEVENT gained = G_MakeEvent(EVENT_GAME_STATE_LIMIT);
    LPEVENT lost = G_MakeEvent(EVENT_GAME_STATE_LIMIT);
    bool saw_gain = false, saw_loss = false;

    G_SetEventSubject(gained, unit); G_SetEventSubject(lost, unit);
    gained->state = lost->state = UNIT_STATE_LIFE;
    gained->limitop = WC3_LIMITOP_GREATER_THAN_OR_EQUAL;
    gained->limitval = 800.0f;
    lost->limitop = WC3_LIMITOP_LESS_THAN_OR_EQUAL;
    lost->limitval = 700.0f;

    T_ASSERT(S_CastNoTargetSpell(unit, BZ_AVATAR));
    T_FEQ(unit->health.value, 900.0f, 0.001f);
    S_AvatarExpire(unit);
    T_FEQ(unit->health.value, 650.0f, 0.001f);

    FOR_LOOP(i, level.events.write) {
        GAMEEVENT const *event = &level.events.queue[i % MAX_EVENT_QUEUE];
        if (event->responseTo == gained) saw_gain = true;
        if (event->responseTo == lost) saw_loss = true;
    }
    T_ASSERT(saw_gain);
    T_ASSERT(saw_loss);
    avatar_done(fix);
}

TEST(wc3_avatar, level_change_death_and_removal_reverse_stored_values) {
    AVFIX fix = avatar_setup(2);
    LPEDICT unit = fix.unit;
    T_ASSERT(S_CastNoTargetSpell(unit, BZ_AVATAR));
    T_FEQ(unit->health.max_value, 1250, 0.001f);
    T_FEQ(unit->attack2.temporaryDamageBonus, 35, 0.001f);
    unit->hero.str++; G_RecomputeHeroStats(unit);
    T_FEQ(unit->health.max_value, 1275, 0.001f);
    T_FEQ(unit->armor_value, 12, 0.001f);
    unit->heroabilities[0].level = 1;
    unit_die(unit, unit);
    T_FEQ(unit->health.value, 0, 0.001f);
    T_FEQ(unit->health.max_value, 675, 0.001f);
    T_FEQ(unit->armor_value, 5, 0.001f);
    T_FEQ(unit->attack1.temporaryDamageBonus, 3, 0.001f);
    T_EQ(unit->avatar.level, 0);
    T_ASSERT(!S_UnitSpellImmune(unit));
    avatar_done(fix);
}

TEST(wc3_avatar, ability_removal_expires_active_avatar) {
    AVFIX fix = avatar_setup(1);
    LPEDICT unit = fix.unit;
    T_ASSERT(G_ActorAddSkill(unit, BZ_AVATAR));
    T_ASSERT(S_CastNoTargetSpell(unit, BZ_AVATAR));
    T_ASSERT(G_ActorRemoveSkill(unit, BZ_AVATAR));
    T_EQ(unit->avatar.level, 0); T_ASSERT(!S_UnitSpellImmune(unit));
    T_FEQ(unit->health.max_value, 650, 0.001f); T_FEQ(unit->attack1.temporaryDamageBonus, 3, 0.001f);
    avatar_done(fix);
}

TEST(wc3_avatar, immunity_rechecks_impact_and_leaves_physical_damage) {
    AVFIX fix = avatar_setup(1);
    LPEDICT unit = fix.unit, enemy = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 32, 0);
    enemy->health.value = 100; enemy->svflags |= SVF_MONSTER;
    T_ASSERT(S_CastNoTargetSpell(unit, BZ_AVATAR));
    T_ASSERT(!S_SpellAllowsTarget(MAKEFOURCC('A','H','t','b'), enemy, unit));
    T_ASSERT(!S_SpellDamage(unit, enemy, 100));
    T_FEQ(unit->health.value, 900, 0.001f);
    S_ResolveAttackHit(enemy, unit, 50);
    T_FEQ(unit->health.value, 850, 0.001f);
    level.time += 2000; unit_updatestatuses(unit);
    T_ASSERT(S_SpellDamage(unit, enemy, 100));
    T_FEQ(unit->health.value, 550, 0.001f);
    avatar_done(fix);
}

TEST(wc3_avatar, no_mana_or_capacity_does_not_commit) {
    AVFIX fix = avatar_setup(1);
    LPEDICT unit = fix.unit;
    unit->mana.value = 24;
    T_ASSERT(!S_CastNoTargetSpell(unit, BZ_AVATAR));
    T_EQ(unit->avatar.level, 0);
    unit->mana.value = 100;
    FOR_LOOP(i, MAX_UNIT_STATUSES) unit->abilstatus[i] = (heroabilitystatus_t){ .code = i + 1, .level = 1 };
    T_ASSERT(!S_CastNoTargetSpell(unit, BZ_AVATAR));
    T_FEQ(unit->mana.value, 100, 0.001f);
    T_FEQ(unit->health.max_value, 650, 0.001f);
    avatar_done(fix);
}
TEST(wc3_avatar, jass_added_ability_casts_by_order_and_publishes_spell_effect) {
    AVFIX fix = avatar_setup(1);
    LPEDICT unit = fix.unit;

    memset(unit->heroabilities, 0, sizeof(unit->heroabilities));
    T_EQ(G_UnitAbilityLevel(unit, BZ_AVATAR), 0);
    T_ASSERT(G_ActorAddSkill(unit, BZ_AVATAR));
    T_EQ(G_UnitAbilityLevel(unit, BZ_AVATAR), 1);

    level.events.read = level.events.write = 0;
    memset(level.events.queue, 0, sizeof(level.events.queue));
    T_ASSERT(unit_issueimmediateorder(unit, "avatar"));
    T_EQ(level.events.write, 4);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_SPELL_EFFECT);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_SPELL_EFFECT);
    T_EQ(level.events.queue[2].type, EVENT_PLAYER_UNIT_ISSUED_ORDER);
    T_EQ(level.events.queue[3].type, EVENT_UNIT_ISSUED_ORDER);
    T_ASSERT(level.events.queue[0].edict == unit && level.events.queue[1].edict == unit);
    T_EQ((uint32_t)level.events.queue[0].value, BZ_AVATAR);
    T_EQ((uint32_t)level.events.queue[1].value, BZ_AVATAR);
    T_ASSERT(level.events.queue[2].edict == unit && level.events.queue[3].edict == unit);
    T_EQ(G_GetIssuedOrderId(unit), G_OrderId("avatar"));

    T_ASSERT(G_ActorRemoveSkill(unit, BZ_AVATAR));
    T_EQ(G_UnitAbilityLevel(unit, BZ_AVATAR), 0);
    avatar_done(fix);
}

#endif
