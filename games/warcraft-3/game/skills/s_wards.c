#include "s_skills.h"

#define ID_STASIS_BUFF "Bsta"
#define ID_ADT1 MAKEFOURCC('A', 'd', 't', '1') // Detect (Sentry Ward); Rng is true-sight radius
#define ID_ASTA MAKEFOURCC('A', 's', 't', 'a') // Stasis Trap placement
#define ID_AEYE MAKEFOURCC('A', 'e', 'y', 'e') // Sentry Ward placement base code

void stasis_trap_think(LPEDICT thinker);

/* Land units only; air never arms or takes the stun. */
static BOOL stasis_land_enemy(LPEDICT ward, LPEDICT target, FLOAT radius) {
	if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(ward, target)) return false;
	if (target->targtype == TARG_AIR || target->targtype == TARG_STRUCTURE) return false;
	if (G_UnitIsBuilding(target->class_id)) return false;
	return Vector2_distance(&target->s.origin2, &ward->s.origin2) <= radius;
}

static LPCSTR stasis_buff(DWORD code, DWORD level) {
	LPCSTR buff = G_AbilityLevel(code, level)->buffID;
	return (buff && strlen(buff) >= 4) ? buff : ID_STASIS_BUFF;
}

static void stasis_kill_ward(LPEDICT ward) {
	LPEDICT th_list[8];
	DWORD n = 0;
	if (!ward || !ward->inuse) return;
	FILTER_EDICTS(th, th->inuse && th->owner == ward && th->think == stasis_trap_think)
		if (n < 8) th_list[n++] = th;
	FOR_LOOP(i, n) G_FreeEdict(th_list[i]);
	G_FreeEdict(ward);
}

/* After DataA arm delay, DataB trigger, DataC stun + peer-ward destroy; DataD/HeroDur stun. */
void stasis_trap_think(LPEDICT thinker) {
	LPEDICT ward = thinker->owner, peers[16];
	DWORD code = thinker->class_id, level = (DWORD)thinker->wait, pn = 0;
	FLOAT detect, area, stun;
	LPCSTR buff;
	BOOL trigger = false;

	/* Match Pocket Factory: only require the ward slot; fixture UnitBalance may leave HP at 0. */
	if (!ward || !ward->inuse) { G_FreeEdict(thinker); return; }
	if (G_Time() < thinker->freetime) return;
	detect = S_SpellData(code, level, 2);
	FILTER_EDICTS(t, stasis_land_enemy(ward, t, detect)) { trigger = true; break; }
	if (!trigger) return;
	area = S_SpellData(code, level, 3);
	buff = stasis_buff(code, level);
	FILTER_EDICTS(t, stasis_land_enemy(ward, t, area)) {
		if (!t->data.UnitBalance) continue;
		stun = G_UnitIsHero(t) ? S_SpellDuration(code, level, true) : S_SpellData(code, level, 4);
		unit_addtimedstatus(t, buff, 1, stun);
	}
	FILTER_EDICTS(peer, peer != ward && peer->inuse && peer->summon_ability == ID_ASTA &&
	              Vector2_distance(&peer->s.origin2, &ward->s.origin2) <= area)
		if (pn < 16) peers[pn++] = peer;
	G_FreeEdict(thinker);
	FOR_LOOP(i, pn) stasis_kill_ward(peers[i]);
	stasis_kill_ward(ward);
}

/* Name=Stasis Trap; invisible ward arms after DataA then stuns with DataD/HeroDur. */
BZ_SIMPLE_SPELL_PROC(AbilityStasisTrap) {
	DWORD level = S_SpellLevel(caster, spell->code);
	DWORD unit_id = S_SpellUnitId(spell->code, level);
	FLOAT life = S_SpellDuration(spell->code, level, false);
	FLOAT arm = S_SpellData(spell->code, level, 1);
	LPEDICT ward, thinker;

	if (!unit_id) {
		fprintf(stderr, "WC3 Stasis Trap: missing UnitID for %.4s\n", (LPCSTR)&spell->code);
		return;
	}
	ward = S_SummonAt(caster, unit_id, &st.point, life);
	if (!ward) return;
	ward->summon_ability = spell->code;
	ward->s.renderfx |= RF_HIDDEN;
	thinker = G_Spawn();
	if (!thinker) { G_FreeEdict(ward); return; }
	thinker->owner = ward;
	thinker->class_id = spell->code;
	thinker->wait = (FLOAT)level;
	thinker->freetime = G_Time() + (DWORD)(MAX(0.0f, arm) * 1000.0f);
	thinker->think = stasis_trap_think;
}

static BOOL ward_is_sentry(LPCEDICT ward) {
	return ward && ward->inuse && G_AbilityCode(ward->summon_ability) == ID_AEYE;
}

/* True when an RF_HIDDEN unit sits inside an opposing living sentry ward's Adt1 Rng. */
BOOL S_UnitIsDetected(LPCEDICT unit) {
	FLOAT range;
	if (!unit || !unit->inuse || !(unit->s.renderfx & RF_HIDDEN)) return false;
	FILTER_EDICTS(ward, ward_is_sentry(ward) && S_SpellIsEnemy(ward, (LPEDICT)unit)) {
		range = ward->wait;
		if (range <= 0.0f) continue;
		if (Vector2_distance(&ward->s.origin2, &unit->s.origin2) <= range) return true;
	}
	return false;
}

/* Name=Sentry Ward; UnitID + Dur summon; detect radius from Adt1 Rng stored on ward.wait. */
BZ_SIMPLE_SPELL_PROC(AbilityEvilEye) {
	DWORD level = S_SpellLevel(caster, spell->code);
	DWORD unit_id = S_SpellUnitId(spell->code, level);
	FLOAT life = S_SpellDuration(spell->code, level, false);
	LPEDICT ward;

	if (!unit_id) {
		fprintf(stderr, "WC3 Sentry Ward: missing UnitID for %.4s\n", (LPCSTR)&spell->code);
		return;
	}
	ward = S_SummonAt(caster, unit_id, &st.point, life);
	if (!ward) return;
	ward->summon_ability = spell->code;
	ward->s.renderfx |= RF_HIDDEN;
	ward->wait = S_SpellRange(ID_ADT1, 1);
	if (ward->wait <= 0.0f)
		fprintf(stderr, "WC3 Sentry Ward: Adt1 Rng missing for detect on %.4s\n", (LPCSTR)&spell->code);
}
