#include "s_skills.h"

/* Self Destruct blast is physical: armor applies, spell immunity does not block it. */
static BOOL self_destruct_allows(DWORD code, LPEDICT caster, LPEDICT target) {
	LPCSTR targets;
	if (!caster || !S_SpellIsAliveTarget(target) || target == caster || S_UnitIsCycloned(target))
		return false;
	targets = G_AbilityLevel(code, 1)->targs;
	if (!targets) return true;
	if ((strstr(targets, "air") || strstr(targets, "ground") || strstr(targets, "structure")) &&
		!(strstr(targets, "air") && target->targtype == TARG_AIR) &&
		!(strstr(targets, "ground") && target->targtype == TARG_GROUND) &&
		!(strstr(targets, "structure") && (target->targtype == TARG_STRUCTURE || G_UnitIsBuilding(target->class_id))))
		return false;
	if (strstr(targets, "enemy") && S_SpellIsEnemy(caster, target)) return true;
	if (strstr(targets, "neutral") && target->s.player < MAX_PLAYERS && level.mapinfo &&
		level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral) return true;
	return !strstr(targets, "friend") && !strstr(targets, "enemy") && !strstr(targets, "neutral");
}

static void self_destruct_explode(LPEDICT ent, DWORD code) {
	DWORD level = MAX(1u, G_UnitAbilityLevel(ent, code));
	FLOAT full_r = S_SpellData(code, level, 1), full_d = S_SpellData(code, level, 2);
	FLOAT part_r = S_SpellData(code, level, 3), part_d = S_SpellData(code, level, 4);
	FLOAT build = S_SpellData(code, level, 5);
	VECTOR2 origin = ent->s.origin2;
	if (full_d <= 0.0f && part_d <= 0.0f) return;
	if (part_r < full_r) part_r = full_r;
	FILTER_EDICTS(target, self_destruct_allows(code, ent, target)) {
		FLOAT dist = Vector2_distance(&target->s.origin2, &origin), damage;
		if (dist > part_r) continue;
		damage = dist <= full_r ? full_d : part_d;
		if (damage <= 0.0f) continue;
		if (build != 1.0f && G_UnitIsBuilding(target->class_id)) damage *= build;
		T_Damage(target, ent, (int)damage);
	}
}

static BOOL death_seen(DWORD *seen, DWORD *n, DWORD code) {
	FOR_LOOP(i, *n) if (seen[i] == code) return true;
	if (*n < 32) seen[(*n)++] = code;
	return false;
}

static void death_ability_one(LPEDICT ent, DWORD code, DWORD *seen, DWORD *n) {
	abilityitem_t item;
	abilityCall_t call;
	char name[5] = {0};
	if (!code || death_seen(seen, n, code)) return;
	memcpy(name, &code, 4);
	if (!G_ActorHasSkill(ent, name)) return;
	item = MAKE(abilityitem_t, .code = code, .ability = FindAbilityByClassname(name));
	if (!item.ability) item = S_AbilityItem(code);
	if (!item.ability) return;
	call = MAKE(abilityCall_t, .item = &item);
	S_AbilityMessage(ent, A_DEATH, &call);
}

/* Walk the dying unit's concrete abilities; do not use the global innate list. */
void S_UnitDeathAbilities(LPEDICT ent) {
	DWORD seen[32], n = 0;
	if (!ent) return;
	if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
		PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
			DWORD code = 0;
			if (strlen(token) != 4) continue;
			memcpy(&code, token, sizeof(code));
			death_ability_one(ent, code, seen, &n);
		}
	}
	FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added))
		if (ent->abilities.added[i]) death_ability_one(ent, ent->abilities.added[i], seen, &n);
	FOR_LOOP(i, MAX_HERO_ABILITIES)
		if (ent->heroabilities[i].level && ent->heroabilities[i].code)
			death_ability_one(ent, ent->heroabilities[i].code, seen, &n);
}

/* Name=Kaboom! / Self Destruct
 * DataA/B full radius/damage, DataC/D partial radius/damage, DataE building factor,
 * DataF explodes-on-death. Intentional Kaboom cast remains unimplemented.
 */
BZ_ABILITY_PROC(CAbilitySelfDestruct) {
	DWORD code = call && call->item ? call->item->code : 0;
	DWORD level;
	if (msg != A_DEATH || !code || !ent) return false;
	level = MAX(1u, G_UnitAbilityLevel(ent, code));
	if (S_SpellData(code, level, 6) <= 0.0f) return false;
	self_destruct_explode(ent, code);
	return true;
}
