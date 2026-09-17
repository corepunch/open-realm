#include "s_skills.h"

/* Splash/secondary hits: air enemy or neutral, not the primary, not the caster. */
static BOOL unstable_concoction_splash_allows(DWORD code, LPEDICT caster, LPEDICT primary, LPEDICT target) {
	LPCSTR targets;
	if (!caster || !S_SpellIsAliveTarget(target) || target == caster || target == primary ||
		S_UnitIsCycloned(target) || target->targtype != TARG_AIR)
		return false;
	targets = G_AbilityLevel(code, 1)->targs;
	if (!targets) return true;
	if (strstr(targets, "enemy") && S_SpellIsEnemy(caster, target)) return true;
	if (strstr(targets, "neutral") && target->s.player < MAX_PLAYERS && level.mapinfo &&
		level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral)
		return true;
	return false;
}

static void unstable_concoction_explode(LPEDICT caster, LPEDICT primary, DWORD code) {
	DWORD level = MAX(1u, G_UnitAbilityLevel(caster, code));
	FLOAT full_r = S_SpellData(code, level, 1), full_d = S_SpellData(code, level, 2);
	FLOAT part_r = S_SpellData(code, level, 3), part_d = S_SpellData(code, level, 4);
	FLOAT max_d = S_SpellData(code, level, 5), splash_spent = 0.0f;
	VECTOR2 origin;

	if (!primary || (full_d <= 0.0f && part_d <= 0.0f)) return;
	origin = primary->s.origin2;
	if (part_r < full_r) part_r = full_r;
	T_Damage(primary, caster, (int)full_d);
	FILTER_EDICTS(target, unstable_concoction_splash_allows(code, caster, primary, target)) {
		FLOAT dist = Vector2_distance(&target->s.origin2, &origin), damage;
		if (dist > part_r) continue;
		damage = (full_r > 0.0f && dist <= full_r) ? full_d : part_d;
		if (damage <= 0.0f) continue;
		if (max_d > 0.0f) {
			if (splash_spent >= max_d) break;
			if (splash_spent + damage > max_d) damage = max_d - splash_spent;
		}
		T_Damage(target, caster, (int)damage);
		splash_spent += damage;
	}
}

static void unstable_concoction_kill_caster(LPEDICT caster) {
	G_SetHealth(caster, 0);
	if (caster->die) caster->die(caster, caster);
	else unit_die(caster, caster);
}

/* Name=Unstable Concoction — TFT Auco. DataB primary, DataC/D air splash, caster dies.
 * DataA full secondary ring; DataE splash cap; DataF charge movespeed not applied on instant execute.
 */
BZ_SIMPLE_SPELL_PROC(AbilityUnstableConcoction) {
	DWORD code;
	if (!caster || !spell || !st.entity) return;
	code = spell->code;
	unstable_concoction_explode(caster, st.entity, code);
	unstable_concoction_kill_caster(caster);
}
