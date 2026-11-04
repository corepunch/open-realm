#include "s_skills.h"
#include <float.h>

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

/* Couple abilities are command-card actions: execute directly on the nearest
 * valid partner rather than entering target mode. */
bool S_CastNearestCouplePartner(edict_t *caster, uint32_t code) {
	edict_t *best = NULL;
	float best_distance = FLT_MAX;
	uint32_t level, partner;
	float range;
	uint32_t candidates = 0, rejected_target = 0;

	if (!caster || (code != MAKEFOURCC('A','c','o','h') && code != MAKEFOURCC('A','c','o','a'))) return false;
	if (!G_UnitAbilityLevel(caster, code) || !G_UnitAbilityResearchAvailable(caster, code)) {
		fprintf(stderr, "WC3_COUPLE command-reject unit=%u raw=%.4s reason=unavailable level=%u researched=%u\n",
		        (unsigned)caster->s.number, (cstring_t)&code, G_UnitAbilityLevel(caster, code),
		        (unsigned)G_UnitAbilityResearchAvailable(caster, code));
		return false;
	}
	level = S_SpellLevel(caster, code);
	partner = S_SpellDataId(code, level, 1);
	range = S_SpellRange(code, level);
	if (!partner || !range) {
		fprintf(stderr, "WC3_COUPLE command-reject unit=%u raw=%.4s reason=missing-data level=%u partner=%.4s range=%.1f\n",
		        (unsigned)caster->s.number, (cstring_t)&code, level,
		        partner ? (cstring_t)&partner : "----", range);
		return false;
	}
	FILTER_EDICTS(target, S_SpellIsAliveTarget(target) &&
	              target != caster && target->class_id == partner &&
	              S_SpellIsFriend(caster, target)) {
		float const distance = Vector2_distance(&caster->s.origin2, &target->s.origin2);
		bool const allowed = S_SpellAllowsTarget(code, caster, target);
		candidates++;
		if (distance > range || !allowed) {
			rejected_target++;
			fprintf(stderr, "WC3_COUPLE candidate-reject caster=%u raw=%.4s target=%u target_raw=%.4s distance=%.1f range=%.1f target_allowed=%u\n",
			        (unsigned)caster->s.number, (cstring_t)&code, (unsigned)target->s.number,
			        (cstring_t)&target->class_id, distance, range, (unsigned)allowed);
			continue;
		}
		if (distance >= best_distance) continue;
		best = target;
		best_distance = distance;
	}
	if (!best) {
		fprintf(stderr, "WC3_COUPLE no-partner caster=%u raw=%.4s partner=%.4s range=%.1f matching=%u rejected=%u\n",
		        (unsigned)caster->s.number, (cstring_t)&code, (cstring_t)&partner,
		        range, candidates, rejected_target);
		return false;
	}
	{
		uint32_t const caster_number = caster->s.number, target_number = best->s.number;
		bool const accepted = S_CastUnitTargetSpell(caster, code, best);
		fprintf(stderr, "WC3_COUPLE nearest-cast caster=%u raw=%.4s target=%u target_raw=%.4s distance=%.1f range=%.1f accepted=%u\n",
		        (unsigned)caster_number, (cstring_t)&code, (unsigned)target_number,
		        (cstring_t)&partner, best_distance, range, (unsigned)accepted);
		return accepted;
	}
}

/* Spawn the authored rider first, then remove both inputs (Warsmash CoupleInstant). */
static void couple_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
	edict_t *target = st.entity, *rider;
	uint32_t level, result;
	vec2_t origin;

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
	if (spell->ability && spell->ability->proc == CAbilityCoupleInstant)
		S_SpellCommitCast(caster, spell->code, level);
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

static edict_t *couple_instant_nearest(edict_t *caster, abilityitem_t const *spell) {
	edict_t *best = NULL;
	float best_distance = FLT_MAX;
	uint32_t level, partner;
	float radius;

	if (!caster || !spell) return NULL;
	level = S_SpellLevel(caster, spell->code);
	partner = S_SpellDataId(spell->code, level, 1);
	radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
	if (!partner || radius <= 0.0f) {
		fprintf(stderr, "WC3_COUPLE instant-reject caster=%u ability=%.4s reason=missing-data level=%u partner=%.4s area=%.1f\n",
		        (unsigned)caster->s.number, (cstring_t)&spell->code, level,
		        partner ? (cstring_t)&partner : "----", radius);
		return NULL;
	}
	FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && target != caster &&
	              target->class_id == partner && S_SpellIsFriend(caster, target)) {
		float const distance = Vector2_distance(&caster->s.origin2, &target->s.origin2);
		if (distance > radius || distance >= best_distance ||
		    !S_SpellAllowsTarget(spell->code, caster, target)) continue;
		best = target;
		best_distance = distance;
	}
	if (!best) {
		fprintf(stderr, "WC3_COUPLE instant-no-partner caster=%u ability=%.4s partner=%.4s area=%.1f\n",
		        (unsigned)caster->s.number, (cstring_t)&spell->code,
		        (cstring_t)&partner, radius);
	} else {
		fprintf(stderr, "WC3_COUPLE instant-partner caster=%u ability=%.4s target=%u target_raw=%.4s distance=%.1f area=%.1f\n",
		        (unsigned)caster->s.number, (cstring_t)&spell->code,
		        (unsigned)best->s.number, (cstring_t)&partner, best_distance, radius);
	}
	return best;
}

/* Couple Instant's Area is an acquisition radius, not the distance at which
 * the two units can combine. Keep the normal collision-edge interaction
 * distance small so the mount walks up to its partner before merging. */
static float couple_instant_interaction_range(edict_t const *caster, edict_t const *target) {
	return MAX(32.0f, caster ? caster->collision : 0.0f) +
	       MAX(0.0f, target ? target->collision : 0.0f);
}

static bool couple_instant_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
	if (!caster || !spell || st.type != SPELL_TARGET_NONE ||
	    (spell->code != MAKEFOURCC('A','c','o','2') && spell->code != MAKEFOURCC('A','c','o','3'))) return false;
	if (!G_UnitAbilityResearchAvailable(caster, spell->code)) {
		fprintf(stderr, "WC3_COUPLE instant-reject caster=%u ability=%.4s reason=requires-training level=%u\n",
		        (unsigned)caster->s.number, (cstring_t)&spell->code,
		        G_UnitAbilityLevel(caster, spell->code));
		return false;
	}
	return couple_instant_nearest(caster, spell) != NULL;
}

static void couple_instant_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
	edict_t *target;
	spellTarget_t target_call;
	float distance;
	if (!couple_instant_validate(caster, st, spell)) return;
	target = couple_instant_nearest(caster, spell);
	if (!target) return;
	distance = Vector2_distance(&caster->s.origin2, &target->s.origin2);
	if (distance > couple_instant_interaction_range(caster, target)) {
		edict_t *thinker = G_Spawn();
		if (!thinker) return;
		/* Replacing an order cancels any older couple approach first. */
		FILTER_EDICTS(other, other->inuse && other->think == S_CoupleInstantApproachThink &&
		              other->owner == caster)
			G_FreeEdict(other);
		order_move(caster, target);
		if (caster->goalentity != target || !move_is_active_order_walk(caster)) {
			G_FreeEdict(thinker);
			return;
		}
		thinker->owner = caster;
		thinker->goalentity = target;
		thinker->class_id = spell->code;
		channel_t *cast = G_AddChannel(thinker);
		cast->owner_spawn_time = caster->spawn_time;
		cast->target_spawn_time = target->spawn_time;
		thinker->think = S_CoupleInstantApproachThink;
		fprintf(stderr, "WC3_COUPLE instant-approach caster=%u ability=%.4s target=%u distance=%.1f interaction=%.1f\n",
		        (unsigned)caster->s.number, (cstring_t)&spell->code,
		        (unsigned)target->s.number, distance, couple_instant_interaction_range(caster, target));
		return;
	}
	fprintf(stderr, "WC3_COUPLE instant-execute caster=%u ability=%.4s target=%u distance=%.1f\n",
	        (unsigned)caster->s.number, (cstring_t)&spell->code, (unsigned)target->s.number, distance);
	target_call = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target);
	couple_execute(caster, target_call, spell);
}

void S_CoupleInstantApproachThink(edict_t *thinker) {
	edict_t *caster = S_SpellChannelOwner(thinker);
	edict_t *target = S_SpellChannelTarget(thinker);
	uint32_t code = thinker ? thinker->class_id : 0;
	abilityitem_t spell = S_AbilityItem(code);
	spellTarget_t target_call;
	float distance;

	if (!caster || !target || !spell.ability || spell.ability->proc != CAbilityCoupleInstant ||
	    !G_UnitAbilityResearchAvailable(caster, code) ||
	    !S_SpellIsAliveTarget(target) || !S_SpellIsFriend(caster, target) ||
	    target->class_id != S_SpellDataId(code, S_SpellLevel(caster, code), 1) ||
	    !S_SpellAllowsTarget(code, caster, target)) {
		if (caster && caster->goalentity == target) {
			caster->goalentity = NULL;
			unit_stand(caster);
		}
		G_FreeEdict(thinker);
		return;
	}
	if (!G_IsUnitAbilityAvailable(caster, code)) {
		if (caster->goalentity == target) {
			caster->goalentity = NULL;
			unit_stand(caster);
		}
		G_FreeEdict(thinker);
		return;
	}
	if (caster->goalentity != target) {
		G_FreeEdict(thinker);
		return;
	}
	distance = Vector2_distance(&caster->s.origin2, &target->s.origin2);
		if (distance > couple_instant_interaction_range(caster, target)) {
		if (caster->movement && (caster->movement->flow_unreachable || caster->movement->flow_goal_reached)) {
			caster->goalentity = NULL;
			unit_stand(caster);
			G_FreeEdict(thinker);
			return;
		}
		if (!move_is_active_order_walk(caster)) {
			G_FreeEdict(thinker);
			return;
		}
		return;
	}
	caster->goalentity = NULL;
	unit_stand(caster);
	target_call = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target);
	if (S_SpellCanActivate(caster, code, S_SpellLevel(caster, code), false)) {
		couple_execute(caster, target_call, &spell);
	}
	G_FreeEdict(thinker);
}

static edict_t *decouple_spawn(edict_t *rider, uint32_t unit_id, bool offset_from_partner) {
	edict_t *ent;

	if (!rider || !unit_id) return NULL;
	ent = SP_SpawnAtLocationNoBirth(unit_id, rider->s.player, &rider->s.origin2);
	if (!ent) {
		fprintf(stderr, "WC3_COUPLE: failed to spawn companion %.4s on dismount\n", (cstring_t)&unit_id);
		return NULL;
	}
	ent->s.angle = rider->s.angle;
	if (offset_from_partner) {
		vec2_t const requested = { rider->s.origin2.x + 64.0f, rider->s.origin2.y };
		vec2_t position;
		/* Prefer the nearest collision-safe spot. If static pathing data is
		 * unavailable, still place the Archer beside the mount instead of
		 * leaving both companions stacked at the rider's origin. */
		G_FindUnitUnstuckPosition(ent, &requested, &position);
		ent->s.origin2 = position;
		ent->s.origin.x = position.x;
		ent->s.origin.y = position.y;
		gi.LinkEntity(ent);
	}
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
	/* Place the mount first at the rider's position, then find a free nearby
	 * ground position for its Archer companion. */
	second = decouple_spawn(caster, b, false);
	first = decouple_spawn(caster, a, true);
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
BZ_VALIDATED_SPELL_PROC(AbilityCoupleInstant, couple_instant_validate, couple_instant_execute)
