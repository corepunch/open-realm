#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *slk_text);
void free_slk_rows(slkTestData_t *rows);

/* An object-data Heal alias must retain its authored amount and cost under autocast. */
TEST(wc3_ability_dispatch, autocast_keeps_authored_alias) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Rng1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y2;X1;K\"Ahea\"\nC;Y2;X2;K\"Ahea\"\nC;Y2;X3;K\"air,ground,friend\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"600\"\nC;Y2;X6;K\"25\"\n"
        "C;Y3;X1;K\"A001\"\nC;Y3;X2;K\"Ahea\"\nC;Y3;X3;K\"air,ground,friend\"\n"
        "C;Y3;X4;K\"13\"\nC;Y3;X5;K\"600\"\nC;Y3;X6;K\"37\"\n"
        "C;Y4;X1;K\"A002\"\nC;Y4;X2;K\"Ahea\"\nC;Y4;X3;K\"air,ground,friend\"\n"
        "C;Y4;X4;K\"23\"\nC;Y4;X5;K\"600\"\nC;Y4;X6;K\"89\"\nE\n";
    UnitAbilities_t list = { .abilList = "A001,A002" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    reset_entities();
    setup_test_world();
    LPEDICT caster = alloc_test_unit(MAKEFOURCC('h','p','r','i'), 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    caster->data.UnitAbilities = &list;
    caster->mana.value = caster->mana.max_value = 200;
    caster->s.player = target->s.player = 0;
    caster->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = 100; target->health.max_value = 1000;
    T_ASSERT(G_SetUnitAutocast(caster, FS_SLKKey("A001"), true));
    T_ASSERT(G_TryUnitAutocast(caster));
    T_FEQ(target->health.value, 137, 0.001f);
    T_FEQ(caster->mana.value, 187, 0.001f);
    T_ASSERT(G_UnitAutocastIsOn(caster, FS_SLKKey("A001")));
    T_ASSERT(!G_UnitAutocastIsOn(caster, FS_SLKKey("A002")));
    T_ASSERT(G_SetUnitAutocast(caster, FS_SLKKey("A001"), false));
    T_ASSERT(!G_TryUnitAutocast(caster));
    T_ASSERT(G_SetUnitAutocast(caster, FS_SLKKey("A002"), true));
    T_ASSERT(G_TryUnitAutocast(caster));
    T_FEQ(target->health.value, 226, 0.001f);
    T_FEQ(caster->mana.value, 164, 0.001f);
    T_ASSERT(G_ActorRemoveSkill(caster, FS_SLKKey("A002")));
    T_EQ(caster->autocast_code, 0);
    T_ASSERT(!G_TryUnitAutocast(caster));
    T_ASSERT(!G_SetUnitAutocast(caster, FS_SLKKey("A002"), true));
    T_ASSERT(G_ActorAddSkill(caster, FS_SLKKey("A002")));
    T_ASSERT(G_SetUnitAutocast(caster, FS_SLKKey("A002"), true));
    T_ASSERT(G_TryUnitAutocast(caster));
    T_FEQ(target->health.value, 315, 0.001f);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}
/* All Human boolean-message handlers and Repair must switch without leaking the old policy. */
TEST(wc3_ability_dispatch, autocast_boolean_messages_switch_and_remove) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y8;X2\nC;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"Ahea\"\nC;Y2;X2;K\"Ahea\"\n"
        "C;Y3;X1;K\"Ainf\"\nC;Y3;X2;K\"Ainf\"\n"
        "C;Y4;X1;K\"Aslo\"\nC;Y4;X2;K\"Aslo\"\n"
        "C;Y5;X1;K\"Asps\"\nC;Y5;X2;K\"Asps\"\n"
        "C;Y6;X1;K\"Arep\"\nC;Y6;X2;K\"Arep\"\n"
        "C;Y7;X1;K\"Aren\"\nC;Y7;X2;K\"Aren\"\n"
        "C;Y8;X1;K\"AEpa\"\nC;Y8;X2;K\"AEpa\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    DWORD const codes[] = { MAKEFOURCC('A','h','e','a'), MAKEFOURCC('A','i','n','f'),
        MAKEFOURCC('A','s','l','o'), MAKEFOURCC('A','s','p','s'),
        MAKEFOURCC('A','r','e','p'), MAKEFOURCC('A','r','e','n') };
    reset_entities(); setup_test_world();
    LPEDICT caster = alloc_test_unit(MAKEFOURCC('h','p','r','i'), 0, 0);
    UnitAbilities_t list = { .abilList = "" };
    caster->data.UnitAbilities = &list;
    FOR_LOOP(i, sizeof(codes) / sizeof(codes[0])) {
        T_ASSERT(G_ActorAddSkill(caster, codes[i]));
        T_ASSERT(G_SetUnitAutocast(caster, codes[i], true));
        T_ASSERT(G_UnitAutocastIsOn(caster, codes[i]));
        if (i) T_ASSERT(!G_UnitAutocastIsOn(caster, codes[i - 1]));
    }
    T_ASSERT(G_SetUnitAutocast(caster, codes[4], false));
    T_ASSERT(G_UnitAutocastIsOn(caster, codes[5]));
    T_ASSERT(G_ActorAddSkill(caster, FS_SLKKey("AEpa")));
    T_ASSERT(!G_SetUnitAutocast(caster, FS_SLKKey("AEpa"), true));
    T_ASSERT(G_UnitAutocastIsOn(caster, codes[5]));
    T_ASSERT(G_ActorRemoveSkill(caster, codes[5]));
    T_ASSERT(!(caster->aiflags & AI_AUTOCAST_ACTIVE));
    T_ASSERT(!(caster->aiflags & AI_AUTOCAST_REPAIR));
    T_EQ(caster->autocast_code, 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stock Shackles supplies separate caster/target buffs; cancellation must release only this cast's target. */
TEST(wc3_ability_dispatch, shackles_locks_target_until_its_channel_ends) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Rng1\"\n"
        "C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\nC;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Amls\"\nC;Y2;X2;K\"Amls\"\nC;Y2;X3;K\"air,enemy\"\nC;Y2;X4;K\"800\"\n"
        "C;Y2;X5;K\"10\"\nC;Y2;X6;K\"10\"\nC;Y2;X7;K\"30\"\nC;Y2;X8;K\"Bmlc,Bmlt\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    UnitAbilities_t list = { .abilList = "Amls" };
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    LPEDICT caster = alloc_test_unit(MAKEFOURCC('h','p','r','i'), 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0), first = NULL, second = NULL;
    caster->data.UnitAbilities = &list; caster->s.player = 0; target->s.player = 1;
    caster->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER; target->targtype = TARG_AIR;
    target->health.value = target->health.max_value = 1000;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("Amls"), target));
    FILTER_EDICTS(ent, ent->owner == caster && ent->think == human_ability_think) { first = ent; break; }
    T_NOT_NULL(first);
    T_ASSERT(!S_HumanCanAttack(target)); T_FEQ(S_HumanMoveFactor(target), 0, 0.001f);
    T_ASSERT(!G_UnitStatusLevel(target, FS_SLKKey("Bmlc")));
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("Amls"), target));
    FILTER_EDICTS(ent, ent != first && ent->owner == caster && ent->think == human_ability_think) {
        second = ent; break;
    }
    T_NOT_NULL(second);
    if (first) G_RunEntity(first);
    T_ASSERT(S_SpellIsChanneling(caster)); T_ASSERT(!S_HumanCanAttack(target));
    S_SpellCancelChannel(caster);
    if (second) G_RunEntity(second);
    T_ASSERT(S_HumanCanAttack(target)); T_FEQ(S_HumanMoveFactor(target), 1, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stock Cyclone makes an enemy organic unit unable to act or be targeted until its authored duration expires. */
TEST(wc3_ability_dispatch, cyclone_locks_actions_and_targeting_until_expiry) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Rng1\"\n"
        "C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\nC;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\n"
        "C;Y1;X9;K\"DataC1\"\nC;Y1;X10;K\"BuffID1\"\nC;Y1;X11;K\"Cost1\"\nC;Y1;X12;K\"Cool1\"\n"
        "C;Y2;X1;K\"Acyc\"\nC;Y2;X2;K\"Acyc\"\nC;Y2;X3;K\"air,ground,enemy,neutral,organic\"\n"
        "C;Y2;X4;K\"600\"\nC;Y2;X5;K\"20\"\nC;Y2;X6;K\"6\"\nC;Y2;X7;K\"1\"\n"
        "C;Y2;X8;K\"0\"\nC;Y2;X9;K\"0\"\nC;Y2;X10;K\"Bcyc,Bcy2\"\nC;Y2;X11;K\"150\"\nC;Y2;X12;K\"8\"\nE\n";
    UnitAbilities_t list = { .abilList = "Acyc" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    reset_entities(); setup_test_world(); InitAbilities(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    LPEDICT caster = alloc_test_unit(MAKEFOURCC('e','d','o','c'), 0, 0);
    LPEDICT victim = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    LPEDICT attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 150, 0);
    LPEDICT ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 200, 0);
    LPEDICT mechanical = alloc_test_unit(MAKEFOURCC('h','m','t','t'), 250, 0);
    LPEDICT waypoint = Waypoint_add(&MAKE(VECTOR2, .x = 400));
    LPEDICT units[] = { caster, victim, attacker, ally, mechanical };
    FOR_LOOP(i, sizeof(units) / sizeof(*units)) {
        units[i]->data.UnitAbilities = &list;
        units[i]->svflags |= SVF_MONSTER;
        units[i]->targtype = TARG_GROUND;
        units[i]->health.value = units[i]->health.max_value = 1000;
        units[i]->mana.value = units[i]->mana.max_value = 500;
        units[i]->attack1.type = ATK_NORMAL; units[i]->attack1.cooldown = 1; units[i]->attack1.damageBase = 10;
    }
    caster->s.player = attacker->s.player = ally->s.player = mechanical->s.player = 0;
    victim->s.player = 1; mechanical->s.player = 1; mechanical->targtype = TARG_MECHANICAL;

    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("Acyc"), ally));
    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("Acyc"), mechanical));
    T_FEQ(caster->mana.value, 500, 0.001f);
    order_move(victim, waypoint); T_ASSERT(move_is_active_order_walk(victim));
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("Acyc"), victim));
    T_EQ(G_UnitStatusLevel(victim, FS_SLKKey("Bcyc")), 1);
    T_ASSERT(!move_is_active_order_walk(victim));
    order_move(victim, waypoint); T_ASSERT(!move_is_active_order_walk(victim));
    T_ASSERT(!S_OrderAttack(victim, caster));
    T_ASSERT(!S_OrderAttack(attacker, victim));
    T_ASSERT(!S_CastUnitTargetSpell(victim, FS_SLKKey("Acyc"), caster));
    T_ASSERT(!S_CastUnitTargetSpell(attacker, FS_SLKKey("Acyc"), victim));
    S_ResolveAttackHit(attacker, victim, 25); T_FEQ(victim->health.value, 1000, 0.001f);

    level.started = true; level.scriptsStarted = true;
    level.time += 20000; globals.RunFrame();
    T_EQ(G_UnitStatusLevel(victim, FS_SLKKey("Bcyc")), 0);
    order_move(victim, waypoint); T_ASSERT(move_is_active_order_walk(victim));
    T_ASSERT(S_OrderAttack(victim, caster));
    S_ResolveAttackHit(attacker, victim, 25); T_ASSERT(victim->health.value < 1000);
    T_ASSERT(S_CastUnitTargetSpell(attacker, FS_SLKKey("Acyc"), victim));
    level.started = false;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}
#endif
