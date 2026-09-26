#include "s_skills.h"

/* Hippogryph couple: Acoa/Acoh merge two living same-owner units into UnitID;
 * Adec splits the rider into DataA+DataB. Not Defend, not cargo, not Raven Form. */

static bool couple_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
	edict_t *target = st.entity;
	uint32_t level, partner, rider;

	if (!caster || !spell || !target || target == caster) return false;
	if (!S_SpellIsAliveTarget(target) || !S_SpellIsFriend(caster, target)) return false;
	level = S_SpellLevel(caster, spell->code);
	partner = S_SpellDataId(spell->code, level, 1);
	rider = S_SpellUnitId(spell->code, level);
	if (!partner || !rider || target->class_id != partner) return false;
	return true;
}

/* Spawn the authored rider first, then remove both inputs (Warsmash CoupleInstant). */
static void couple_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
	edict_t *target = st.entity, *rider;
	uint32_t level, result;
	vector2_t origin;

	if (!caster || !spell || !target || !couple_validate(caster, st, spell)) return;
	level = S_SpellLevel(caster, spell->code);
	result = S_SpellUnitId(spell->code, level);
	origin = caster->s.origin2;
	rider = SP_SpawnAtLocationNoBirth(result, caster->s.player, &origin);
	if (!rider) {
		fprintf(stderr, "WC3_COUPLE: failed to spawn rider %.4s from %.4s\n",
		        (cstring_t)&result, (cstring_t)&spell->code);
		return;
	}
	rider->s.angle = caster->s.angle;
	G_ActivateUnitFood(rider);
	if (rider->stand) rider->stand(rider);
	G_FreeEdict(target);
	G_FreeEdict(caster);
}

static bool decouple_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
	uint32_t level, a, b;

	(void)st;
	if (!caster || !spell || !S_SpellIsAliveTarget(caster)) return false;
	level = S_SpellLevel(caster, spell->code);
	a = S_SpellDataId(spell->code, level, 1);
	b = S_SpellDataId(spell->code, level, 2);
	return a && b;
}

static edict_t *decouple_spawn(edict_t *rider, uint32_t unit_id) {
	edict_t *ent;

	if (!rider || !unit_id) return NULL;
	ent = SP_SpawnAtLocationNoBirth(unit_id, rider->s.player, &rider->s.origin2);
	if (!ent) {
		fprintf(stderr, "WC3_COUPLE: failed to spawn companion %.4s on dismount\n", (cstring_t)&unit_id);
		return NULL;
	}
	ent->s.angle = rider->s.angle;
	G_ActivateUnitFood(ent);
	if (ent->stand) ent->stand(ent);
	return ent;
}

static void decouple_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
	uint32_t level, a, b;
	edict_t *first, *second;

	(void)st;
	if (!caster || !spell || !decouple_validate(caster, st, spell)) return;
	level = S_SpellLevel(caster, spell->code);
	a = S_SpellDataId(spell->code, level, 1);
	b = S_SpellDataId(spell->code, level, 2);
	first = decouple_spawn(caster, a);
	second = decouple_spawn(caster, b);
	if (!first || !second) {
		if (first) G_FreeEdict(first);
		if (second) G_FreeEdict(second);
		return;
	}
	G_FreeEdict(caster);
}

BZ_VALIDATED_SPELL_PROC(AbilityCoupleArcher, couple_validate, couple_execute)
BZ_VALIDATED_SPELL_PROC(AbilityCoupleHippogryph, couple_validate, couple_execute)
BZ_VALIDATED_SPELL_PROC(AbilityDecouple, decouple_validate, decouple_execute)
