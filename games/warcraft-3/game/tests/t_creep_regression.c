#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

LPEDICT alloc_test_unit(uint32_t, float, float);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *);
void free_slk_rows(slkTestData_t *);

/* Stock column meanings with deterministic proc chance; all cases drive production entry points. */
typedef struct { slkTestData_t *rows, *old; LPEDICT caster, target; UnitAbilities_t abilities; } CREEPFIX;

typedef struct { cstring_t id, parent, buffs, targs; float area, data[6]; bool roc; } CREEPDATA;

static void creep_setup(CREEPFIX *fix, CREEPDATA const *row) {
    char slk[4096];
    snprintf(slk, sizeof(slk),
        "ID;PWXL;N;EBB;Y2;X15\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"%s\"\nC;Y1;X6;K\"%s\"\n"
        "C;Y1;X7;K\"%s\"\nC;Y1;X8;K\"%s\"\nC;Y1;X9;K\"%s\"\n"
        "C;Y1;X10;K\"Dur1\"\nC;Y1;X11;K\"HeroDur1\"\nC;Y1;X12;K\"Rng1\"\n"
        "C;Y1;X13;K\"targs\"\nC;Y1;X14;K\"levels\"\nC;Y1;X15;K\"DataF1\"\n"
        "C;Y2;X1;K\"%s\"\nC;Y2;X2;K\"%s\"\nC;Y2;X3;K\"%s\"\n"
        "C;Y2;X4;K\"%g\"\nC;Y2;X5;K\"%g\"\nC;Y2;X6;K\"%g\"\n"
        "C;Y2;X7;K\"%g\"\nC;Y2;X8;K\"%g\"\nC;Y2;X9;K\"%g\"\n"
        "C;Y2;X10;K\"12\"\nC;Y2;X11;K\"12\"\nC;Y2;X12;K\"900\"\n"
        "C;Y2;X13;K\"%s\"\nC;Y2;X14;K\"1\"\nC;Y2;X15;K\"%g\"\nE\n",
        row->roc ? "Data11" : "DataA1", row->roc ? "Data12" : "DataB1", row->roc ? "Data13" : "DataC1",
        row->roc ? "Data14" : "DataD1", row->roc ? "Data15" : "DataE1",
        row->id, row->parent, row->buffs, row->area, row->data[0], row->data[1], row->data[2], row->data[3], row->data[4],
        row->targs ? row->targs : "air,ground,enemy,organic,neutral", row->data[5]);
    reset_entities(); setup_test_world(); level.time = 1000; level.framenum = 0;
    memset(level.alliances, 0, sizeof(level.alliances));
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    fix->rows = parse_slk_string(slk); fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->caster = alloc_test_unit(FS_SLKKey("ogru"), 400, 0);
    fix->target = alloc_test_unit(FS_SLKKey("hfoo"), 450, 0);
    fix->abilities = (UnitAbilities_t){ .abilList = row->id };
    fix->caster->data.UnitAbilities = &fix->abilities;
    fix->caster->s.player = 0; fix->target->s.player = 1;
    fix->caster->svflags |= SVF_MONSTER; fix->target->svflags |= SVF_MONSTER;
    fix->caster->targtype = fix->target->targtype = TARG_GROUND;
    fix->caster->health.value = fix->caster->health.max_value = 500;
    fix->target->health.value = fix->target->health.max_value = 500;
    fix->caster->mana.value = fix->caster->mana.max_value = 500;
    fix->caster->stand = fix->target->stand = unit_stand;
    fix->caster->think = fix->target->think = monster_think;
    fix->caster->die = fix->target->die = unit_die;
    unit_stand(fix->caster); unit_stand(fix->target);
}

static void creep_done(CREEPFIX *fix) { G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows); }

static LPEDICT creep_neighbor(float x) {
    LPEDICT unit = alloc_test_unit(FS_SLKKey("hfoo"), x, 0);
    unit->svflags |= SVF_MONSTER; unit->targtype = TARG_GROUND; unit->s.player = 1;
    unit->health.value = unit->health.max_value = 500; unit->die = unit_die;
    return unit;
}

TEST(wc3_spell, creep_regression_disease_uses_dps_not_duration) {
    CREEPFIX fix;
    creep_setup(&fix, &(CREEPDATA){ .id = "Aap1", .parent = "Aapl", .buffs = "Bapl", .area = 176, .data = {120, 1, 10, 0, 0} });
    G_RunEntities();
    T_ASSERT(fix.target->health.value >= 499);
    level.time += 1000; unit_updatestatuses(fix.target);
    T_FEQ(fix.target->health.value, 499, 0.1f);
    creep_done(&fix);
}

TEST(wc3_spell, creep_regression_pulverize_stock_columns) {
    CREEPFIX fix;
    LPEDICT nearby;
    creep_setup(&fix, &(CREEPDATA){ .id = "Awar", .parent = "Awar", .buffs = "", .area = 0, .data = {100, 60, 250, 350, 0} });
    nearby = alloc_test_unit(FS_SLKKey("hfoo"), 500, 0);
    nearby->svflags |= SVF_MONSTER; nearby->targtype = TARG_GROUND; nearby->s.player = 1;
    nearby->health.value = nearby->health.max_value = 500;
    S_ResolveAttackHit(fix.caster, fix.target, 10);
    T_FEQ(nearby->health.value, 440, 0.001f);
    creep_done(&fix);
}

TEST(wc3_spell, creep_regression_web_grounds_and_locks_flyer) {
    CREEPFIX fix;
    UnitData_t flight = { .moveTypeName = "fly", .moveHeight = 180 };
    creep_setup(&fix, &(CREEPDATA){ .id = "Aweb", .parent = "Aweb", .buffs = "Bwea,Bweb", .area = 0, .data = {0.6f, 200, 128, 0, 0} });
    fix.target->data.UnitData = &flight; fix.target->targtype = TARG_AIR;
    fix.target->aiflags |= AI_FLYING; fix.target->unitinfo.FlyHeight = 180;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, FS_SLKKey("Aweb"), fix.target));
    level.time += 1000; G_RunEntities();
    T_ASSERT(!S_UnitCanTranslate(fix.target));
    T_ASSERT(!(fix.target->aiflags & AI_FLYING));
    T_EQ(fix.target->targtype, TARG_GROUND);
    creep_done(&fix);
}

TEST(wc3_spell, creep_regression_monsoon_hits_requested_point) {
    CREEPFIX fix;
    creep_setup(&fix, &(CREEPDATA){ .id = "ANmo", .parent = "ANmo", .buffs = "ANmd", .targs = "air,ground,structure,enemy,neutral", .area = 64, .data = {20, 1.5f, 0.35f, 0, 0} });
    T_ASSERT(S_CastPointTargetSpell(fix.caster, FS_SLKKey("ANmo"), &fix.target->s.origin2));
    level.time += 1600; G_RunEntities();
    T_ASSERT(fix.target->health.value < 500);
    creep_done(&fix);
}

TEST(wc3_spell, creep_regression_incinerate_uses_authored_explosion_radius) {
    CREEPFIX fix;
    LPEDICT nearby;
    creep_setup(&fix, &(CREEPDATA){ .id = "ANic", .parent = "ANic", .buffs = "BNic", .area = 0, .data = {2, 30, 120, 15, 240} });
    nearby = alloc_test_unit(FS_SLKKey("hfoo"), 500, 0);
    nearby->svflags |= SVF_MONSTER; nearby->s.player = 1; nearby->targtype = TARG_GROUND;
    nearby->health.value = nearby->health.max_value = 500;
    fix.target->health.value = 11;
    S_ResolveAttackHit(fix.caster, fix.target, 10);
    T_ASSERT(M_IsDead(fix.target));
    T_FEQ(nearby->health.value, 470, 0.001f);
    creep_done(&fix);
}

TEST(wc3_spell, creep_regression_incinerate_explodes_on_normal_lethal_hit) {
    CREEPFIX fix;
    LPEDICT nearby;
    /* Nonzero Area isolates the death hook from the independent radius bug. */
    creep_setup(&fix, &(CREEPDATA){ .id = "ANic", .parent = "ANic", .buffs = "BNic", .area = 120, .data = {2, 30, 120, 15, 240} });
    nearby = alloc_test_unit(FS_SLKKey("hfoo"), 500, 0);
    nearby->svflags |= SVF_MONSTER; nearby->s.player = 1; nearby->targtype = TARG_GROUND;
    nearby->health.value = nearby->health.max_value = 500;
    S_ResolveAttackHit(fix.caster, fix.target, 10);
    T_EQ(G_UnitStatusLevel(fix.target, FS_SLKKey("BNic")), 1);
    fix.target->health.value = 5;
    S_ResolveAttackHit(fix.caster, fix.target, 10);
    T_ASSERT(M_IsDead(fix.target));
    T_FEQ(nearby->health.value, 470, 0.001f);
    creep_done(&fix);
}

TEST(wc3_spell, creep_regression_disease_roc_lingers_expires_and_dispels) {
    CREEPFIX fix;
    creep_setup(&fix, &(CREEPDATA){ .id = "Zdis", .parent = "Aapl", .buffs = "", .area = 176, .data = {3, 7}, .roc = true });
    S_RunAbilityUpdates(fix.caster);
    T_ASSERT(S_UnitHasStatus(fix.target, FS_SLKKey("Bapl")));
    fix.caster->s.origin.x = 900;
    level.time += 1000; unit_updatestatuses(fix.target); unit_updatestatuses(fix.target);
    T_FEQ(fix.target->health.value, 493, 0.001f);
    level.time += 1000; unit_updatestatuses(fix.target);
    T_FEQ(fix.target->health.value, 486, 0.001f);
    level.time += 1000; unit_updatestatuses(fix.target);
    T_ASSERT(!S_UnitHasStatus(fix.target, FS_SLKKey("Bapl")));
    T_FEQ(fix.target->health.value, 486, 0.001f);
    fix.caster->s.origin.x = 400; S_RunAbilityUpdates(fix.caster);
    unit_expirestatus(fix.target, unit_findstatus(fix.target, FS_SLKKey("Bapl")));
    level.time += 1000; unit_updatestatuses(fix.target);
    T_FEQ(fix.target->health.value, 486, 0.001f);
    creep_done(&fix);
}

TEST(wc3_spell, creep_regression_pulverize_roc_outer_ring_and_target_filter) {
    CREEPFIX fix;
    LPEDICT full, half, outside, air, friend;
    creep_setup(&fix, &(CREEPDATA){ .id = "Zpul", .parent = "Awar", .buffs = "", .data = {100, 40, 100, 200}, .roc = true });
    full = creep_neighbor(500); half = creep_neighbor(600); outside = creep_neighbor(700);
    air = creep_neighbor(500); air->targtype = TARG_AIR;
    friend = creep_neighbor(500); friend->s.player = 0;
    S_ResolveAttackHit(fix.caster, fix.target, 10);
    T_FEQ(full->health.value, 460, 0.001f); T_FEQ(half->health.value, 480, 0.001f);
    T_FEQ(outside->health.value, 500, 0.001f); T_FEQ(air->health.value, 500, 0.001f);
    T_FEQ(friend->health.value, 500, 0.001f);
    creep_done(&fix);
}

TEST(wc3_spell, creep_regression_web_roc_validation_and_last_bind_release) {
    CREEPFIX fix;
    UnitData_t flight = { .moveTypeName = "fly", .moveHeight = 180 };
    creep_setup(&fix, &(CREEPDATA){ .id = "Zweb", .parent = "Aweb", .buffs = "", .targs = "air,enemy,neutral", .data = {0.6f, 200, 128}, .roc = true });
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, FS_SLKKey("Zweb"), fix.target));
    fix.target->data.UnitData = &flight; fix.target->targtype = TARG_AIR;
    fix.target->aiflags |= AI_FLYING; fix.target->unitinfo.FlyHeight = 180;
    fix.target->s.player = 0;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, FS_SLKKey("Zweb"), fix.target));
    fix.target->s.player = 1;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, FS_SLKKey("Zweb"), fix.target));
    level.time += 1000; S_RunAbilityUpdates(fix.target);
    T_FEQ(fix.target->unitinfo.FlyHeight, 0, 0.001f);
    /* Two independently dispellable bind slots share the one landing state. */
    unit_addtimedstatus(fix.target, "Bens", 1, 12);
    unit_findstatus(fix.target, FS_SLKKey("Bens"))->data = FS_SLKKey("Zweb");
    unit_expirestatus(fix.target, unit_findstatus(fix.target, FS_SLKKey("Bwea")));
    unit_refreshstatusflags(fix.target);
    T_ASSERT(S_UnitIsEnsnared(fix.target)); T_ASSERT(!(fix.target->aiflags & AI_FLYING));
    T_FEQ(fix.target->unitinfo.FlyHeight, 0, 0.001f);
    unit_expirestatus(fix.target, unit_findstatus(fix.target, FS_SLKKey("Bens")));
    unit_refreshstatusflags(fix.target);
    level.time += 1000; S_RunAbilityUpdates(fix.target);
    T_ASSERT(!S_UnitIsEnsnared(fix.target)); T_ASSERT(fix.target->aiflags & AI_FLYING);
    T_EQ(fix.target->targtype, TARG_AIR); T_FEQ(fix.target->unitinfo.FlyHeight, 180, 0.001f);
    creep_done(&fix);
}

TEST(wc3_spell, creep_regression_monsoon_interval_buildings_and_cancellation) {
    CREEPFIX fix;
    LPEDICT building, outside, thinker = NULL;
    creep_setup(&fix, &(CREEPDATA){ .id = "Zmon", .parent = "ANmo", .buffs = "ANmd", .targs = "air,ground,structure,enemy,neutral", .area = 64, .data = {20, 1.5f, 0.35f} });
    building = creep_neighbor(460); building->targtype = TARG_STRUCTURE;
    outside = creep_neighbor(600);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, FS_SLKKey("Zmon"), &fix.target->s.origin2));
    FILTER_EDICTS(ent, ent->think == monsoon_think) { thinker = ent; break; }
    T_ASSERT(thinker != NULL);
    if (thinker) {
        T_FEQ(fix.target->health.value, 480, 0.001f); T_FEQ(building->health.value, 493, 0.001f);
        level.time += 1499; monsoon_think(thinker);
        T_FEQ(fix.target->health.value, 480, 0.001f);
        level.time++; monsoon_think(thinker);
        T_FEQ(fix.target->health.value, 460, 0.001f); T_FEQ(building->health.value, 486, 0.001f);
        T_FEQ(outside->health.value, 500, 0.001f);
        fix.caster->s.origin.x += 20; level.time += 1500; monsoon_think(thinker);
        T_ASSERT(!thinker->inuse); T_FEQ(fix.target->health.value, 460, 0.001f);
    }
    creep_done(&fix);
}

TEST(wc3_spell, creep_regression_incinerate_delayed_third_party_death_and_rings) {
    CREEPFIX fix;
    LPEDICT full, half, outside, blast = NULL;
    creep_setup(&fix, &(CREEPDATA){ .id = "Zinc", .parent = "ANic", .buffs = "BNic", .targs = "enemy,neutral,organic,nonancient", .data = {2, 30, 120, 15, 240, 0.2f} });
    full = creep_neighbor(500); half = creep_neighbor(650); outside = creep_neighbor(750);
    S_ResolveAttackHit(fix.caster, fix.target, 10);
    T_Damage(fix.target, full, 1000);
    T_ASSERT(M_IsDead(fix.target)); T_ASSERT(fix.target->aiflags & AI_CORPSE_UNRAISABLE);
    FILTER_EDICTS(ent, ent->think == incinerate_explode_think) { blast = ent; break; }
    T_ASSERT(blast != NULL);
    if (blast) {
        T_FEQ(full->health.value, 500, 0.001f);
        level.time += 199; incinerate_explode_think(blast);
        T_FEQ(full->health.value, 500, 0.001f);
        level.time++; incinerate_explode_think(blast);
        T_FEQ(full->health.value, 470, 0.001f); T_FEQ(half->health.value, 485, 0.001f);
        T_FEQ(outside->health.value, 500, 0.001f); T_ASSERT(!blast->inuse);
        unit_statusdeath(fix.target); T_FEQ(full->health.value, 470, 0.001f);
    }
    creep_done(&fix);
}

TEST(wc3_spell, creep_regression_incinerate_expiry_dispel_and_source_reuse) {
    FOR_LOOP(reason, 3) {
        CREEPFIX fix;
        LPEDICT nearby;
        creep_setup(&fix, &(CREEPDATA){ .id = "ANic", .parent = "ANic", .buffs = "BNic", .data = {2, 30, 120, 15, 240} });
        nearby = creep_neighbor(500);
        S_ResolveAttackHit(fix.caster, fix.target, 10);
        if (reason == 0) { level.time += 12000; unit_updatestatuses(fix.target); }
        if (reason == 1) unit_expirestatus(fix.target, unit_findstatus(fix.target, FS_SLKKey("BNic")));
        if (reason == 2) fix.caster->spawn_time++; /* Same address, different incarnation. */
        T_Damage(fix.target, nearby, 1000);
        T_ASSERT(M_IsDead(fix.target)); T_FEQ(nearby->health.value, 500, 0.001f);
        creep_done(&fix);
    }
}

TEST(wc3_save, creep_disease_status_round_trip_and_source_reuse) {
    cstring_t filename = "/tmp/openwarcraft3-creep-disease.bin";
    CREEPFIX fix;
    heroabilitystatus_t *slot;
    creep_setup(&fix, &(CREEPDATA){ .id = "Aap1", .parent = "Aapl", .buffs = "Bapl", .area = 176, .data = {120, 1} });
    S_RunAbilityUpdates(fix.caster);
    T_ASSERT(WriteGame(filename)); T_ASSERT(ReadGame(filename));
    slot = unit_findstatus(fix.target, FS_SLKKey("Bapl")); T_ASSERT(slot != NULL);
    if (slot) {
        T_ASSERT(slot->source == fix.caster); T_EQ(slot->data, FS_SLKKey("Aap1"));
        T_EQ(slot->rank, 1); T_EQ(slot->next_tick, 2000); T_EQ(slot->timestamp, 121000);
        level.time += 1000; unit_updatestatuses(fix.target);
        T_FEQ(fix.target->health.value, 499, 0.001f);
        fix.caster->spawn_time++; level.time += 1000; unit_updatestatuses(fix.target);
        T_FEQ(fix.target->health.value, 499, 0.001f); T_EQ(slot->level, 0);
    }
    remove(filename); creep_done(&fix);
}

TEST(wc3_save, creep_incinerate_mark_and_delayed_explosion_round_trip) {
    cstring_t filename = "/tmp/openwarcraft3-creep-incinerate.bin";
    CREEPFIX fix;
    LPEDICT nearby, blast = NULL;
    creep_setup(&fix, &(CREEPDATA){ .id = "ANic", .parent = "ANic", .buffs = "BNic", .data = {2, 30, 120, 15, 240, 0.2f} });
    nearby = creep_neighbor(500);
    S_ResolveAttackHit(fix.caster, fix.target, 10);
    T_ASSERT(WriteGame(filename)); T_ASSERT(ReadGame(filename));
    T_Damage(fix.target, nearby, 1000);
    FILTER_EDICTS(ent, ent->think == incinerate_explode_think) { blast = ent; break; }
    T_ASSERT(blast != NULL);
    T_ASSERT(WriteGame(filename)); T_ASSERT(ReadGame(filename));
    if (blast) {
        T_ASSERT(blast->owner == fix.caster); T_ASSERT(blast->think == incinerate_explode_think);
        level.time += 200; blast->think(blast);
        T_FEQ(nearby->health.value, 470, 0.001f); T_ASSERT(!blast->inuse);
    }
    remove(filename); creep_done(&fix);
}

TEST(wc3_save, creep_monsoon_channel_round_trip) {
    cstring_t filename = "/tmp/openwarcraft3-creep-monsoon.bin";
    CREEPFIX fix;
    LPEDICT thinker = NULL;
    creep_setup(&fix, &(CREEPDATA){ .id = "ANmo", .parent = "ANmo", .buffs = "ANmd", .targs = "air,ground,structure,enemy,neutral", .area = 64, .data = {20, 1.5f, 0.35f} });
    T_ASSERT(S_CastPointTargetSpell(fix.caster, FS_SLKKey("ANmo"), &fix.target->s.origin2));
    FILTER_EDICTS(ent, ent->think == monsoon_think) { thinker = ent; break; }
    T_ASSERT(thinker != NULL);
    T_ASSERT(WriteGame(filename)); T_ASSERT(ReadGame(filename));
    if (thinker) {
        T_ASSERT(thinker->think == monsoon_think); T_ASSERT(thinker->owner == fix.caster);
        T_FEQ(thinker->s.origin.x, 450, 0.001f);
        level.time += 1500; thinker->think(thinker);
        T_FEQ(fix.target->health.value, 460, 0.001f);
        S_SpellCancelChannel(fix.caster); thinker->think(thinker);
        T_ASSERT(!thinker->inuse);
    }
    remove(filename); creep_done(&fix);
}

TEST(wc3_save, creep_web_autocast_landing_and_expiry_round_trip) {
    cstring_t filename = "/tmp/openwarcraft3-creep-web.bin";
    CREEPFIX fix;
    slkTestData_t *units, *old_units;
    abilityitem_t item;
    abilityCall_t call;
    creep_setup(&fix, &(CREEPDATA){ .id = "ACwb", .parent = "Aweb", .buffs = "Bwea,Bweb", .targs = "air,enemy,neutral", .data = {2, 160, 90} });
    units = parse_slk_string("ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"movetp\"\nC;Y1;X3;K\"moveHeight\"\n"
        "C;Y2;X1;K\"hgry\"\nC;Y2;X2;K\"fly\"\nC;Y2;X3;K\"180\"\nE\n");
    old_units = G_SetSLKRows("UnitData", units);
    fix.target->class_id = FS_SLKKey("hgry"); G_BindEntityData(fix.target);
    fix.target->targtype = TARG_AIR; fix.target->aiflags |= AI_FLYING; fix.target->unitinfo.FlyHeight = 180;
    item = S_AbilityItem(FS_SLKKey("ACwb")); call = MAKE(abilityCall_t, .item = &item);
    T_ASSERT(G_SetUnitAutocast(fix.caster, item.code, true));
    T_ASSERT(G_UnitAutocastIsOn(fix.caster, item.code));
    T_ASSERT(S_AbilityMessage(fix.caster, A_AUTOCAST_ACQUIRE, &call));
    T_ASSERT(!S_AbilityMessage(fix.caster, A_AUTOCAST_ACQUIRE, &call));
    level.time += 1000; S_RunAbilityUpdates(fix.target);
    T_FEQ(fix.target->unitinfo.FlyHeight, 80, 0.001f);
    T_ASSERT(WriteGame(filename)); T_ASSERT(ReadGame(filename));
    T_ASSERT(S_UnitIsEnsnared(fix.target)); T_ASSERT(!S_UnitCanTranslate(fix.target));
    level.time += 1000; S_RunAbilityUpdates(fix.target);
    T_FEQ(fix.target->unitinfo.FlyHeight, 0, 0.001f);
    level.time += 10000; unit_updatestatuses(fix.target);
    T_ASSERT(!S_UnitIsEnsnared(fix.target)); T_ASSERT(fix.target->aiflags & AI_FLYING);
    T_EQ(fix.target->targtype, TARG_AIR);
    level.time += 2000; S_RunAbilityUpdates(fix.target);
    T_FEQ(fix.target->unitinfo.FlyHeight, 180, 0.001f);
    G_SetSLKRows("UnitData", old_units); free_slk_rows(units);
    remove(filename); creep_done(&fix);
}
#endif
