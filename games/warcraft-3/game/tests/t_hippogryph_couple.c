#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ACOA MAKEFOURCC('A', 'c', 'o', 'a') // rawcode; Archer Mount Hippogryph
#define BZ_ACOH MAKEFOURCC('A', 'c', 'o', 'h') // rawcode; Pick up Archer
#define BZ_ADEC MAKEFOURCC('A', 'd', 'e', 'c') // rawcode; Dismount / Decouple
#define BZ_HFOO MAKEFOURCC('h', 'f', 'o', 'o') // unit; fixture partner (non-stock ehip)
#define BZ_HPEA MAKEFOURCC('h', 'p', 'e', 'a') // unit; fixture archer/companion (non-stock earc)
#define BZ_OGRU MAKEFOURCC('o', 'g', 'r', 'u') // unit; fixture rider (non-stock ehpr)

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock DataA/UnitID/Cost so mount cannot pass on retail ehip/ehpr/0. */
#define COUPLE_MOUNT_SLK \
	"ID;PWXL;N;EBB;Y3;X9\n" \
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
	"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
	"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"UnitID1\"\n" \
	"C;Y2;X1;K\"Acoa\"\nC;Y2;X2;K\"Acoa\"\nC;Y2;X3;K\"1\"\n" \
	"C;Y2;X4;K\"_\"\nC;Y2;X5;K\"17\"\nC;Y2;X6;K\"0\"\n" \
	"C;Y2;X7;K\"500\"\nC;Y2;X8;K\"hfoo\"\nC;Y2;X9;K\"ogru\"\n" \
	"C;Y3;X1;K\"Acoh\"\nC;Y3;X2;K\"Acoh\"\nC;Y3;X3;K\"1\"\n" \
	"C;Y3;X4;K\"_\"\nC;Y3;X5;K\"17\"\nC;Y3;X6;K\"0\"\n" \
	"C;Y3;X7;K\"500\"\nC;Y3;X8;K\"hpea\"\nC;Y3;X9;K\"ogru\"\nE\n"

/* Non-stock DataA/DataB/Cost; companions are not retail earc/ehip. */
#define COUPLE_DISMOUNT_SLK \
	"ID;PWXL;N;EBB;Y2;X9\n" \
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
	"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
	"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n" \
	"C;Y2;X1;K\"Adec\"\nC;Y2;X2;K\"Adec\"\nC;Y2;X3;K\"1\"\n" \
	"C;Y2;X4;K\"_\"\nC;Y2;X5;K\"11\"\nC;Y2;X6;K\"0\"\n" \
	"C;Y2;X7;K\"0\"\nC;Y2;X8;K\"hpea\"\nC;Y2;X9;K\"hfoo\"\nE\n"

typedef struct {
	slkTestData_t *rows, *old;
	LPEDICT caster, partner, enemy, wrong;
} COUPLEFIX;

static void couple_setup(COUPLEFIX *fix, LPCSTR slk, DWORD code, DWORD caster_id, DWORD partner_id) {
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	fix->rows = parse_slk_string(slk); fix->old = G_SetSLKRows("AbilityData", fix->rows);
	fix->caster = alloc_test_unit(caster_id, 0, 0);
	fix->partner = alloc_test_unit(partner_id, 64, 0);
	fix->enemy = alloc_test_unit(partner_id, 96, 0);
	fix->wrong = alloc_test_unit(BZ_OGRU, 128, 0);
	fix->caster->s.player = fix->partner->s.player = fix->wrong->s.player = 0;
	fix->enemy->s.player = 1;
	fix->caster->svflags |= SVF_MONSTER; fix->partner->svflags |= SVF_MONSTER;
	fix->enemy->svflags |= SVF_MONSTER; fix->wrong->svflags |= SVF_MONSTER;
	fix->caster->targtype = fix->partner->targtype = fix->enemy->targtype = fix->wrong->targtype = TARG_GROUND;
	fix->caster->heroabilities[0] = MAKE(heroability_t, .code = code, .level = 1);
	fix->caster->mana.value = fix->caster->mana.max_value = 100;
	fix->caster->health.value = fix->caster->health.max_value = 200;
	fix->partner->health.value = fix->partner->health.max_value = 300;
	fix->enemy->health.value = fix->enemy->health.max_value = 300;
	fix->wrong->health.value = fix->wrong->health.max_value = 400;
}

static void couple_done(COUPLEFIX *fix) {
	G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows);
}

static DWORD couple_count(DWORD class_id, DWORD player) {
	DWORD n = 0;
	FILTER_EDICTS(ent, ent->inuse && ent->class_id == class_id && ent->s.player == player)
		n++;
	return n;
}

TEST(wc3_spell, hippogryph_couple_procedures_registered) {
	T_EQ(S_AbilityItem(BZ_ACOA).ability->proc, CAbilityCoupleArcher);
	T_EQ(S_AbilityItem(BZ_ACOH).ability->proc, CAbilityCoupleHippogryph);
	T_EQ(S_AbilityItem(BZ_ADEC).ability->proc, CAbilityDecouple);
	T_EQ(S_AbilityItem(BZ_ACOA).ability->target_type, SPELL_TARGET_UNIT);
	T_EQ(S_AbilityItem(BZ_ACOH).ability->target_type, SPELL_TARGET_UNIT);
	T_EQ(S_AbilityItem(BZ_ADEC).ability->target_type, SPELL_TARGET_NONE);
}

TEST(wc3_spell, hippogryph_couple_reads_authored_partner_and_rider) {
	COUPLEFIX fix;
	couple_setup(&fix, COUPLE_MOUNT_SLK, BZ_ACOA, BZ_HPEA, BZ_HFOO);
	T_EQ((int)S_SpellDataId(BZ_ACOA, 1, 1), (int)BZ_HFOO);
	T_EQ((int)S_SpellUnitId(BZ_ACOA, 1), (int)BZ_OGRU);
	T_EQ((int)S_SpellDataId(BZ_ACOH, 1, 1), (int)BZ_HPEA);
	T_EQ((int)S_SpellUnitId(BZ_ACOH, 1), (int)BZ_OGRU);
	couple_done(&fix);
}

/* Acoa consumes caster+partner and spawns authored UnitID rider. */
TEST(wc3_spell, hippogryph_couple_acoa_mounts_into_rider) {
	COUPLEFIX fix;
	LPEDICT rider;
	couple_setup(&fix, COUPLE_MOUNT_SLK, BZ_ACOA, BZ_HPEA, BZ_HFOO);
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACOA, fix.partner));
	/* Execute frees both inputs; spent Cost is observed only on reject paths. */
	T_ASSERT(!fix.caster->inuse);
	T_ASSERT(!fix.partner->inuse);
	T_EQ(couple_count(BZ_OGRU, 0), 2); /* fixture wrong-type unit + new rider */
	rider = NULL;
	FILTER_EDICTS(ent, ent->inuse && ent->class_id == BZ_OGRU && ent->s.player == 0 && ent != fix.wrong)
		rider = ent;
	T_ASSERT(rider);
	couple_done(&fix);
}

/* Acoh is the inverse target (hippo casts on archer) with the same merge. */
TEST(wc3_spell, hippogryph_couple_acoh_picks_up_into_rider) {
	COUPLEFIX fix;
	LPEDICT rider = NULL;
	couple_setup(&fix, COUPLE_MOUNT_SLK, BZ_ACOH, BZ_HFOO, BZ_HPEA);
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACOH, fix.partner));
	T_ASSERT(!fix.caster->inuse);
	T_ASSERT(!fix.partner->inuse);
	T_EQ(couple_count(BZ_OGRU, 0), 2);
	FILTER_EDICTS(ent, ent->inuse && ent->class_id == BZ_OGRU && ent->s.player == 0 && ent != fix.wrong)
		rider = ent;
	T_ASSERT(rider);
	couple_done(&fix);
}

TEST(wc3_spell, hippogryph_couple_rejects_invalid_targets_without_mana_spend) {
	COUPLEFIX fix;
	FLOAT mana;
	couple_setup(&fix, COUPLE_MOUNT_SLK, BZ_ACOA, BZ_HPEA, BZ_HFOO);
	mana = fix.caster->mana.value;
	T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ACOA, fix.enemy));
	T_FEQ(fix.caster->mana.value, mana, 0.001f);
	T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ACOA, fix.wrong));
	T_FEQ(fix.caster->mana.value, mana, 0.001f);
	fix.partner->health.value = 0;
	T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ACOA, fix.partner));
	T_FEQ(fix.caster->mana.value, mana, 0.001f);
	T_ASSERT(fix.caster->inuse);
	couple_done(&fix);
}

/* Adec splits the rider into authored DataA/DataB companions. */
TEST(wc3_spell, hippogryph_couple_adec_dismounts_into_companions) {
	COUPLEFIX fix;
	DWORD before_a, before_b;
	couple_setup(&fix, COUPLE_DISMOUNT_SLK, BZ_ADEC, BZ_OGRU, BZ_HFOO);
	T_EQ((int)S_SpellDataId(BZ_ADEC, 1, 1), (int)BZ_HPEA);
	T_EQ((int)S_SpellDataId(BZ_ADEC, 1, 2), (int)BZ_HFOO);
	before_a = couple_count(BZ_HPEA, 0);
	before_b = couple_count(BZ_HFOO, 0);
	T_ASSERT(S_CastNoTargetSpell(fix.caster, BZ_ADEC));
	T_ASSERT(!fix.caster->inuse);
	T_EQ(couple_count(BZ_HPEA, 0), before_a + 1);
	T_EQ(couple_count(BZ_HFOO, 0), before_b + 1);
	couple_done(&fix);
}

#endif
