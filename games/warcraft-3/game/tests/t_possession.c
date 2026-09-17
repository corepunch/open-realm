#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_APOS MAKEFOURCC('A', 'p', 'o', 's') // rawcode; Banshee Possession (instant)
#define BZ_ACPS MAKEFOURCC('A', 'C', 'p', 's') // rawcode; creep Possession alias of Apos
#define BZ_APS2 MAKEFOURCC('A', 'p', 's', '2') // rawcode; TFT channeling Possession
#define BZ_BPOS MAKEFOURCC('B', 'p', 'o', 's') // rawcode; target Possession stun buff
#define BZ_BPOC MAKEFOURCC('B', 'p', 'o', 'c') // rawcode; caster Possession damage-amp buff

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock Cost/DataA so tests cannot pass on retail 250/5. */
#define POS_APOS_SLK \
    "ID;PWXL;N;EBB;Y3;X9\n" \
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"Dur1\"\n" \
    "C;Y2;X1;K\"Apos\"\nC;Y2;X2;K\"Apos\"\nC;Y2;X3;K\"1\"\n" \
    "C;Y2;X4;K\"ground,nonhero,enemy,organic,neutral\"\n" \
    "C;Y2;X5;K\"40\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"500\"\nC;Y2;X8;K\"3\"\nC;Y2;X9;K\"0\"\n" \
    "C;Y3;X1;K\"ACps\"\nC;Y3;X2;K\"Apos\"\nC;Y3;X3;K\"1\"\n" \
    "C;Y3;X4;K\"ground,nonhero,enemy,organic,neutral\"\n" \
    "C;Y3;X5;K\"40\"\nC;Y3;X6;K\"0\"\nC;Y3;X7;K\"500\"\nC;Y3;X8;K\"3\"\nC;Y3;X9;K\"0\"\nE\n"

/* Non-stock Dur/DataB/C/D; DataB=2.5 proves attack amp is not hard-coded 1.66. */
#define POS_APS2_SLK \
    "ID;PWXL;N;EBB;Y2;X14\n" \
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n" \
    "C;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataB1\"\nC;Y1;X12;K\"DataC1\"\n" \
    "C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"BuffID1\"\n" \
    "C;Y2;X1;K\"Aps2\"\nC;Y2;X2;K\"Aps2\"\nC;Y2;X3;K\"1\"\n" \
    "C;Y2;X4;K\"ground,nonhero,enemy,organic,neutral\"\n" \
    "C;Y2;X5;K\"40\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"500\"\n" \
    "C;Y2;X8;K\"2\"\nC;Y2;X9;K\"2\"\nC;Y2;X10;K\"3\"\nC;Y2;X11;K\"2.5\"\n" \
    "C;Y2;X12;K\"1\"\nC;Y2;X13;K\"1\"\nC;Y2;X14;K\"Bpos,Bpoc\"\nE\n"

typedef struct {
    slkTestData_t *rows, *old;
    LPEDICT caster, enemy, ally, flyer, hero;
    UnitBalance_t enemy_bal, hero_bal, ally_bal;
} POSFIX;

static LPEDICT pos_thinker(LPEDICT caster) {
    FILTER_EDICTS(ent, ent->owner == caster && ent->think) return ent;
    return NULL;
}

/* Fill the caller's POSFIX. Returning a copy would dangle UnitBalance pointers
 * (&local.enemy_bal) after return; Linux then misreads G_UnitIsHero / DataA level. */
static void pos_setup(POSFIX *fix, LPCSTR slk, DWORD code) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(slk); fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->enemy_bal = MAKE(UnitBalance_t, .maxHealth = 500, .level = 2);
    fix->ally_bal = MAKE(UnitBalance_t, .maxHealth = 500, .level = 2);
    fix->hero_bal = MAKE(UnitBalance_t, .maxHealth = 500, .strength = 20, .level = 1);
    fix->caster = alloc_test_unit(MAKEFOURCC('u', 'n', 'e', 'c'), 0, 0);
    fix->enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    fix->ally = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 96, 0);
    fix->flyer = alloc_test_unit(MAKEFOURCC('h', 'g', 'r', 'y'), 128, 0);
    fix->hero = alloc_test_unit(MAKEFOURCC('H', 'p', 'a', 'l'), 160, 0);
    fix->caster->s.player = fix->ally->s.player = 0;
    fix->enemy->s.player = fix->flyer->s.player = fix->hero->s.player = 1;
    fix->caster->svflags |= SVF_MONSTER; fix->enemy->svflags |= SVF_MONSTER;
    fix->ally->svflags |= SVF_MONSTER; fix->flyer->svflags |= SVF_MONSTER; fix->hero->svflags |= SVF_MONSTER;
    fix->caster->targtype = fix->enemy->targtype = fix->ally->targtype = fix->hero->targtype = TARG_GROUND;
    fix->flyer->targtype = TARG_AIR;
    fix->enemy->data.UnitBalance = &fix->enemy_bal;
    fix->ally->data.UnitBalance = &fix->ally_bal;
    fix->hero->data.UnitBalance = &fix->hero_bal;
    fix->caster->heroabilities[0] = MAKE(heroability_t, .code = code, .level = 1);
    fix->caster->mana.value = fix->caster->mana.max_value = 100;
    fix->caster->health.value = fix->caster->health.max_value = 300;
    fix->enemy->health.value = fix->enemy->health.max_value = 500;
    fix->ally->health.value = fix->ally->health.max_value = 500;
    fix->flyer->health.value = fix->flyer->health.max_value = 500;
    fix->hero->health.value = fix->hero->health.max_value = 500;
    fix->caster->die = unit_die; fix->enemy->die = unit_die;
    fix->caster->stand = unit_stand; fix->enemy->stand = unit_stand;
}

static void pos_done(POSFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

TEST(wc3_spell, possession_aliases_share_procedures) {
    T_EQ(S_AbilityItem(BZ_APOS).ability->proc, CAbilityPossession);
    T_EQ(S_AbilityItem(BZ_ACPS).ability->proc, CAbilityPossession);
    T_EQ(S_AbilityItem(BZ_APS2).ability->proc, CAbilityPossessionTwo);
    T_ASSERT(S_AbilityItem(BZ_APS2).ability->flags & AB_CHANNEL);
    T_ASSERT(!(S_AbilityItem(BZ_APOS).ability->flags & AB_CHANNEL));
}

/* Instant Apos transfers ownership and kills the caster; mana is spent. */
TEST(wc3_spell, possession_apos_takes_over_and_consumes_caster) {
    POSFIX fix; pos_setup(&fix, POS_APOS_SLK, BZ_APOS);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_APOS, fix.enemy));
    T_FEQ(fix.caster->mana.value, 60, 0.001f);
    T_EQ(fix.enemy->s.player, 0);
    T_ASSERT(M_IsDead(fix.caster));
    T_ASSERT(!M_IsDead(fix.enemy));
    pos_done(fix);
}

/* ACps shares CAbilityPossession and reads its own AbilityData row. */
TEST(wc3_spell, possession_acps_alias_takes_over) {
    POSFIX fix; pos_setup(&fix, POS_APOS_SLK, BZ_ACPS);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACPS, fix.enemy));
    T_EQ(fix.enemy->s.player, 0);
    T_ASSERT(M_IsDead(fix.caster));
    pos_done(fix);
}

/* Rejects ally/dead/hero/flyer/over-level/magic-immune without spending mana. */
TEST(wc3_spell, possession_rejects_invalid_targets_without_mana_spend) {
    POSFIX fix; pos_setup(&fix, POS_APOS_SLK, BZ_APOS);
    FLOAT mana = fix.caster->mana.value;

    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_APOS, fix.ally));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);

    fix.enemy->health.value = 0;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_APOS, fix.enemy));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    fix.enemy->health.value = 500;

    T_ASSERT(G_UnitIsHero(fix.hero));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_APOS, fix.hero));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);

    T_ASSERT(!S_SpellAllowsTarget(BZ_APOS, fix.caster, fix.flyer));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_APOS, fix.flyer));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);

    fix.enemy_bal.level = 4;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_APOS, fix.enemy));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    fix.enemy_bal.level = 2;

    unit_addtimedstatus(fix.enemy, "Bams", 1, 30);
    T_ASSERT(S_UnitSpellImmune(fix.enemy));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_APOS, fix.enemy));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    pos_done(fix);
}

/* After a successful take-over the dead caster cannot recast. */
TEST(wc3_spell, possession_apos_invalid_after_caster_consumed) {
    POSFIX fix; pos_setup(&fix, POS_APOS_SLK, BZ_APOS);
    LPEDICT other = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 200, 0);
    UnitBalance_t bal = MAKE(UnitBalance_t, .maxHealth = 500, .level = 2);
    other->s.player = 1; other->svflags |= SVF_MONSTER; other->targtype = TARG_GROUND;
    other->data.UnitBalance = &bal; other->health.value = other->health.max_value = 500;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_APOS, fix.enemy));
    T_ASSERT(M_IsDead(fix.caster));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_APOS, other));
    pos_done(fix);
}

/* Aps2 locks both sides, then transfers ownership when the channel expires. */
TEST(wc3_spell, possession_aps2_channel_completes_takeover) {
    POSFIX fix; pos_setup(&fix, POS_APS2_SLK, BZ_APS2);
    LPEDICT thinker;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_APS2, fix.enemy));
    T_EQ(fix.caster->channel.code, BZ_APS2);
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BPOS), 1);
    T_EQ(G_UnitStatusLevel(fix.caster, BZ_BPOC), 1);
    T_ASSERT(fix.enemy->stunned);
    T_ASSERT(fix.enemy->invulnerable);
    T_EQ(fix.enemy->s.player, 1);
    T_ASSERT(!M_IsDead(fix.caster));
    thinker = pos_thinker(fix.caster);
    T_NOT_NULL(thinker);
    level.time = thinker->spawn_time; G_RunEntities();
    T_EQ(fix.enemy->s.player, 0);
    T_ASSERT(M_IsDead(fix.caster));
    T_EQ(fix.caster->channel.code, 0);
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BPOS), 0);
    T_ASSERT(!fix.enemy->invulnerable);
    pos_done(fix);
}

/* Cancel mid-channel restores the target and does not transfer ownership. */
TEST(wc3_spell, possession_aps2_abort_keeps_owner) {
    POSFIX fix; pos_setup(&fix, POS_APS2_SLK, BZ_APS2);
    LPEDICT thinker;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_APS2, fix.enemy));
    thinker = pos_thinker(fix.caster);
    T_NOT_NULL(thinker);
    T_ASSERT(fix.enemy->invulnerable);
    S_SpellCancelChannel(fix.caster);
    level.time += FRAMETIME; G_RunEntities();
    T_EQ(fix.enemy->s.player, 1);
    T_ASSERT(!M_IsDead(fix.caster));
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BPOS), 0);
    T_EQ(G_UnitStatusLevel(fix.caster, BZ_BPOC), 0);
    T_ASSERT(!fix.enemy->invulnerable);
    T_ASSERT(!thinker->inuse);
    pos_done(fix);
}

/* DataB multiplies attack damage taken by the channeling caster. */
TEST(wc3_spell, possession_aps2_datab_amplifies_attack_damage) {
    POSFIX fix; pos_setup(&fix, POS_APS2_SLK, BZ_APS2);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_APS2, fix.enemy));
    S_ResolveAttackHit(fix.enemy, fix.caster, 40);
    T_FEQ(fix.caster->health.value, 200, 0.001f); /* 300 - 40*2.5 */
    pos_done(fix);
}

#endif
