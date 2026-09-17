#include "s_skills.h"

#define BZ_AEXH MAKEFOURCC('A', 'e', 'x', 'h') // rawcode; Exhume Corpses TFT Meat Wagon research

/* Count dead UnitID edicts this wagon still owns (ground stand-in for cargo). */
static DWORD exhume_count(LPEDICT wagon, DWORD unit_id) {
	DWORD n = 0;
	if (!wagon || !unit_id) return 0;
	FILTER_EDICTS(ent, ent->inuse && ent->owner == wagon && ent->class_id == unit_id && M_IsDead(ent)) n++;
	return n;
}

static LPEDICT exhume_find_thinker(LPEDICT wagon) {
	FILTER_EDICTS(ent, ent->inuse && ent->owner == wagon && ent->think == exhume_think && !ent->class_id)
		return ent;
	return NULL;
}

/* Cargo Amel/Amed is out of scope: spawn a dead UnitID edict at the wagon origin. */
static void exhume_spawn(LPEDICT wagon, DWORD unit_id) {
	LPEDICT corpse = SP_SpawnAtLocationNoBirth(unit_id, wagon->s.player, &wagon->s.origin2);
	if (!corpse) {
		fprintf(stderr, "WC3 Exhume: failed to spawn corpse %.4s for wagon %u\n",
			(LPCSTR)&unit_id, wagon->s.number);
		return;
	}
	corpse->owner = wagon;
	corpse->health.value = 0;
	corpse->svflags |= SVF_DEADMONSTER;
}

/* Classless producer owned by the wagon; wagon removal or missing Aexh ends it. */
void exhume_think(LPEDICT thinker) {
	LPEDICT wagon = thinker->owner;
	DWORD code = BZ_AEXH, level, unit_id, cap, interval_ms;
	FLOAT interval;
	if (!wagon || !wagon->inuse || M_IsDead(wagon) || !G_UnitAbilityLevel(wagon, code)) {
		G_FreeEdict(thinker);
		return;
	}
	if (G_Time() < thinker->freetime) return;
	level = G_UnitAbilityLevel(wagon, code);
	interval = S_SpellDuration(code, level, false);
	interval_ms = interval > 0.0f ? (DWORD)(interval * 1000.0f) : 0;
	if (!interval_ms) { G_FreeEdict(thinker); return; }
	unit_id = S_SpellUnitId(code, level);
	cap = (DWORD)MAX(0.0f, S_SpellData(code, level, 1));
	if (unit_id && exhume_count(wagon, unit_id) < cap) exhume_spawn(wagon, unit_id);
	thinker->freetime = G_Time() + interval_ms;
}

/* Arm the thinker on the first update after the unit gains Aexh. */
static void exhume_ensure(LPEDICT wagon) {
	DWORD level = G_UnitAbilityLevel(wagon, BZ_AEXH);
	FLOAT interval;
	LPEDICT thinker;
	if (!wagon || !level || M_IsDead(wagon) || exhume_find_thinker(wagon)) return;
	interval = S_SpellDuration(BZ_AEXH, level, false);
	if (interval <= 0.0f) return;
	thinker = G_Spawn();
	if (!thinker) return;
	thinker->owner = wagon;
	thinker->think = exhume_think;
	thinker->freetime = G_Time() + (DWORD)(interval * 1000.0f);
}

/* Name=Exhume Corpses
 * Dur = spawn interval, DataA = max corpses, UnitID = corpse type.
 * Cargo hold integration is intentionally deferred with Amel/Amed.
 */
BZ_ABILITY_PROC(CAbilityExhumeCorpses) {
	switch (msg) {
	case A_UPDATE: exhume_ensure(ent); return true;
	default: return false;
	}
}
