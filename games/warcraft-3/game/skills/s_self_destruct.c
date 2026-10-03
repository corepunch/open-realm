#include "s_skills.h"

/* Self Destruct blast is physical: armor applies, spell immunity does not block it. */
static bool self_destruct_allows(uint32_t code, edict_t *caster, edict_t *target) {
	cstring_t targets;
	if (!caster || !S_SpellIsAliveTarget(target) || target == caster || S_UnitIsCycloned(target))
		return false;
	targets = G_AbilityLevel(code, 1)->targs;
	if (!targets) return true;
	if ((strstr(targets, "air") || strstr(targets, "ground") || strstr(targets, "structure")) &&
		!(strstr(targets, "air") && G_UnitTargetType(target) == TARG_AIR) &&
		!(strstr(targets, "ground") && G_UnitTargetType(target) == TARG_GROUND) &&
		!(strstr(targets, "structure") && G_UnitIsStructure(target)))
		return false;
	if (strstr(targets, "enemy") && S_SpellIsEnemy(caster, target)) return true;
	if (strstr(targets, "neutral") && target->s.player < MAX_PLAYERS && level.mapinfo &&
		level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral) return true;
	return !strstr(targets, "friend") && !strstr(targets, "enemy") && !strstr(targets, "neutral");
}

static void self_destruct_explode(edict_t *ent, uint32_t code) {
	uint32_t level = MAX(1u, G_UnitAbilityLevel(ent, code));
	float full_r = S_SpellData(code, level, 1), full_d = S_SpellData(code, level, 2);
	float part_r = S_SpellData(code, level, 3), part_d = S_SpellData(code, level, 4);
	float build = S_SpellData(code, level, 5);
	vec2_t origin = ent->s.origin2;
	if (full_d <= 0.0f && part_d <= 0.0f) return;
	if (part_r < full_r) part_r = full_r;
	FILTER_EDICTS(target, self_destruct_allows(code, ent, target)) {
		float dist = Vector2_distance(&target->s.origin2, &origin), damage;
		if (dist > part_r) continue;
		damage = dist <= full_r ? full_d : part_d;
		if (damage <= 0.0f) continue;
		if (build != 1.0f && G_UnitIsStructure(target)) damage *= build;
		T_Damage(target, ent, (int)damage);
	}
}

/* Guards A_DEATH while intentional cast kills the caster (DataF aliases would double-blast). */
static bool kaboom_cast;

/* Intentional Kaboom always blasts, then kills the caster; DataF does not gate this path. */
static void self_destruct_kaboom(edict_t *ent, uint32_t code) {
	if (!ent || !code || M_IsDead(ent)) return;
	self_destruct_explode(ent, code);
	kaboom_cast = true;
	G_SetHealth(ent, 0);
	if (ent->die) ent->die(ent, ent);
	else unit_die(ent, ent);
	kaboom_cast = false;
}

/* Autocast: detonate in place when a valid target is already inside DataA. */
static bool self_destruct_autocast_acquire(edict_t *caster, uint32_t code) {
	uint32_t level = MAX(1u, G_UnitAbilityLevel(caster, code));
	float full_r = S_SpellData(code, level, 1);
	vec2_t point;
	if (full_r <= 0.0f) full_r = 100.0f;
	FILTER_EDICTS(target, self_destruct_allows(code, caster, target)) {
		if (Vector2_distance(&target->s.origin2, &caster->s.origin2) > full_r) continue;
		point = caster->s.origin2;
		return S_CastPointTargetSpell(caster, code, &point);
	}
	return false;
}

/* Name=Kaboom! / Self Destruct
 * DataA/B full radius/damage, DataC/D partial radius/damage, DataE building factor,
 * DataF explodes-on-death. Point-target click always blasts; death path needs DataF.
 */
BZ_ABILITY_PROC(CAbilitySelfDestruct) {
	uint32_t code = call &E_item_get(call) ? call->item->code : 0;
	uint32_t level;
	switch (msg) {
	case A_VALIDATE:
		return call && call->target && call->target->type == SPELL_TARGET_POINT;
	case A_EXECUTE:
		if (!ent || !code) return false;
		self_destruct_kaboom(ent, code);
		return true;
	case A_DEATH:
		if (!code || !ent || kaboom_cast) return false;
		level = MAX(1u, G_UnitAbilityLevel(ent, code));
		if (S_SpellData(code, level, 6) <= 0.0f) return false;
		self_destruct_explode(ent, code);
		return true;
	case A_AUTOCAST_ON:
		return ent && code && ent->autocast_code == code;
	case A_AUTOCAST_SET:
		return true;
	case A_AUTOCAST_ACQUIRE:
		return code && self_destruct_autocast_acquire(ent, code);
	default:
		return CAbilitySimpleSpell(ent, msg, call);
	}
}
