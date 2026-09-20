#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_AUNS MAKEFOURCC('A', 'u', 'n', 's')
#define BZ_BUNS MAKEFOURCC('B', 'u', 'n', 's')

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock Cost/DataA/DataB prove the implementation reads authored data. */
#define UNS_SLK \
    "ID;PWXL;N;EBB;Y2;X9\n" \
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n" \
    "C;Y2;X1;K\"Auns\"\nC;Y2;X2;K\"Auns\"\nC;Y2;X3;K\"1\"\n" \
    "C;Y2;X4;K\"structure,player\"\n" \
    "C;Y2;X5;K\"15\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"0\"\n" \
    "C;Y2;X8;K\"0.25\"\nC;Y2;X9;K\"80\"\nE\n"

typedef struct {
    slkTestData_t *rows, *old;
    LPEDICT caster, building, enemy_bldg, unit;
    UnitBalance_t bldg_bal, unit_bal;
    LPGAMECLIENT client;
} UNSFIX;

static void uns_setup(UNSFIX *fix) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(UNS_SLK); fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->client = &game.clients[0];
    fix->bldg_bal = MAKE(UnitBalance_t, .maxHealth = 100, .isBuilding = true, .goldCost = 200, .lumberCost = 120);
    fix->unit_bal = MAKE(UnitBalance_t, .maxHealth = 100, .goldCost = 200, .lumberCost = 120);
    fix->caster = alloc_test_unit(MAKEFOURCC('u', 'a', 'c', 'o'), 0, 0);
    fix->building = alloc_test_unit(MAKEFOURCC('h', 'b', 'a', 'r'), 64, 0);
    fix->enemy_bldg = alloc_test_unit(MAKEFOURCC('h', 'b', 'a', 'r'), 96, 0);
    fix->unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 128, 0);
    fix->caster->s.player = fix->building->s.player = fix->unit->s.player = 0;
    fix->enemy_bldg->s.player = 1;
    fix->caster->svflags |= SVF_MONSTER; fix->building->svflags |= SVF_MONSTER;
    fix->enemy_bldg->svflags |= SVF_MONSTER; fix->unit->svflags |= SVF_MONSTER;
    fix->caster->targtype = fix->unit->targtype = TARG_GROUND;
    fix->building->targtype = fix->enemy_bldg->targtype = TARG_STRUCTURE;
    fix->building->data.UnitBalance = &fix->bldg_bal;
    fix->enemy_bldg->data.UnitBalance = &fix->bldg_bal;
    fix->unit->data.UnitBalance = &fix->unit_bal;
    fix->caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_AUNS, .level = 1);
    fix->caster->mana.value = fix->caster->mana.max_value = 100;
    fix->building->health.value = fix->building->health.max_value = 100;
    fix->enemy_bldg->health.value = fix->enemy_bldg->health.max_value = 100;
    fix->unit->health.value = fix->unit->health.max_value = 100;
    fix->building->die = unit_die; fix->enemy_bldg->die = unit_die; fix->unit->die = unit_die;
    fix->building->stand = unit_stand; fix->caster->stand = unit_stand;
    fix->client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 10;
    fix->client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 5;
}

static void uns_done(UNSFIX *fix) {
    G_SetSLKRows("AbilityData", fix->old);
    free_slk_rows(fix->rows);
}

static LPEDICT uns_thinker(LPEDICT caster) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == caster && ent->think == unsummon_think) return ent;
    return NULL;
}

static void uns_tick(LPEDICT caster, DWORD count) {
    FOR_LOOP(i, count) {
        LPEDICT thinker = uns_thinker(caster);
        if (!thinker) return;
        level.time += FRAMETIME;
        thinker->think(thinker);
    }
}

TEST(wc3_spell, unsummon_procedure_is_unit_target_channel) {
    abilityitem_t item = S_AbilityItem(BZ_AUNS);
    T_NOT_NULL(item.ability);
    T_EQ(item.ability->proc, CAbilityUnsummon);
    T_ASSERT(item.ability->flags & AB_CHANNEL);
    T_EQ(item.ability->target_type, SPELL_TARGET_UNIT);
}

TEST(wc3_spell, unsummon_uses_datab_dps_progressive_refund_and_temporary_magic_immunity) {
    UNSFIX fix;
    uns_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AUNS, fix.building));
    T_FEQ(fix.caster->mana.value, 85, 0.001f);
    T_NOT_NULL(uns_thinker(fix.caster));
    T_ASSERT(G_UnitStatusLevel(fix.building, BZ_BUNS));
    T_ASSERT(S_UnitSpellImmune(fix.building));

    /* DataB=80 DPS: five 100 ms frames remove 40/100 HP.  DataA=.25 then
     * refunds 40% of the total 50g/30l recovery: 20g/12l. */
    uns_tick(fix.caster, 5);
    T_FEQ(fix.building->health.value, 60.0f, 0.001f);
    T_EQ(fix.client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 30);
    T_EQ(fix.client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 17);
    T_ASSERT(!M_IsDead(fix.building));

    uns_tick(fix.caster, 8);
    T_ASSERT(M_IsDead(fix.building));
    T_EQ(fix.client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 60);
    T_EQ(fix.client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 35);
    T_EQ(G_UnitStatusLevel(fix.building, BZ_BUNS), 0);
    T_NULL(uns_thinker(fix.caster));
    uns_done(&fix);
}

TEST(wc3_spell, unsummon_enemy_damage_reduces_recovered_resources) {
    UNSFIX fix;
    uns_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AUNS, fix.building));
    uns_tick(fix.caster, 5); /* Unsummon removes 40 HP. */
    G_AddHealth(fix.building, -20.0f); /* Enemy damage is not refundable. */
    uns_tick(fix.caster, 5); /* Unsummon removes the remaining 40 HP. */
    T_ASSERT(M_IsDead(fix.building));
    T_EQ(fix.client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 50); /* 10 + 80% of 50 */
    T_EQ(fix.client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 29); /* 5 + 80% of 30 */
    uns_done(&fix);
}

TEST(wc3_spell, unsummon_cancel_keeps_partial_damage_and_refund_but_removes_immunity) {
    UNSFIX fix;
    uns_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AUNS, fix.building));
    uns_tick(fix.caster, 2);
    T_FEQ(fix.building->health.value, 84.0f, 0.001f);
    S_SpellCancelChannel(fix.caster);
    T_EQ(G_UnitStatusLevel(fix.building, BZ_BUNS), 0);
    T_ASSERT(!S_UnitSpellImmune(fix.building));
    T_ASSERT(!M_IsDead(fix.building));
    uns_tick(fix.caster, 1); /* retired thinker self-cleans */
    T_NULL(uns_thinker(fix.caster));
    T_FEQ(fix.building->health.value, 84.0f, 0.001f);
    uns_done(&fix);
}

TEST(wc3_spell, unsummon_rejects_invalid_targets_without_mana_spend) {
    UNSFIX fix;
    FLOAT mana;
    uns_setup(&fix);
    mana = fix.caster->mana.value;

    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_AUNS, fix.enemy_bldg));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_AUNS, fix.unit));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    fix.building->health.value = 0;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_AUNS, fix.building));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    uns_done(&fix);
}

TEST(wc3_save, unsummon_live_channel_thinker_round_trips) {
    LPCSTR filename = "/tmp/openwarcraft3-unsummon-live.bin";
    UNSFIX fix;
    LPEDICT thinker;
    uns_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AUNS, fix.building));
    thinker = uns_thinker(fix.caster);
    T_NOT_NULL(thinker);
    T_ASSERT(WriteGame(filename));
    thinker->think = NULL; thinker->goalentity = NULL;
    T_ASSERT(ReadGame(filename));
    thinker = uns_thinker(fix.caster);
    T_NOT_NULL(thinker);
    T_ASSERT(thinker->goalentity == fix.building);
    T_EQ(thinker->channel.target_spawn_time, fix.building->spawn_time);
    remove(filename);
    uns_done(&fix);
}

#endif
