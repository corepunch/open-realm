#include "s_skills.h"

#define BZ_AEXH MAKEFOURCC('A', 'e', 'x', 'h') // rawcode; Exhume Corpses TFT Meat Wagon research

/* Count the authored corpse type in this wagon's real cargo slots. */
static uint32_t exhume_count(edict_t * wagon, uint32_t unit_id) {
	uint32_t n = 0;
	if (!wagon || !unit_id || !S_CargoIsCorpseHolder(wagon)) return 0;
	FOR_LOOP(i, wagon->cargo.count) {
		edict_t * corpse = S_CargoUnitAt(wagon, i);
		if (corpse && S_CorpseCargoIsStored(corpse) && corpse->class_id == unit_id) n++;
	}
	return n;
}

static edict_t * exhume_find_thinker(edict_t * wagon) {
	FILTER_EDICTS(ent, ent->inuse && ent->owner == wagon && ent->think == exhume_think && !ent->class_id)
		return ent;
	return NULL;
}

static void exhume_spawn(edict_t * wagon, uint32_t unit_id) {
	edict_t * corpse = SP_SpawnAtLocationNoBirth(unit_id, wagon->s.player, &wagon->s.origin2);
	if (!corpse) {
		fprintf(stderr, "WC3 Exhume: failed to spawn corpse %.4s for wagon %u\n",
			(cstring_t)&unit_id, wagon->s.number);
		return;
	}
	corpse->owner = wagon;
	corpse->health.value = 0;
	corpse->svflags |= SVF_DEADMONSTER;
	unit_begin_decay(corpse);
	if (!S_CorpseCargoTryLoad(wagon, corpse)) G_FreeEdict(corpse);
}

/* Classless producer owned by the wagon; wagon removal or missing Aexh ends it. */
void exhume_think(edict_t * thinker) {
	edict_t * wagon = thinker->owner;
	uint32_t code = BZ_AEXH, level, unit_id, cap, interval_ms;
	float interval;
	if (!wagon || !wagon->inuse || M_IsDead(wagon) || !G_UnitAbilityLevel(wagon, code)) {
		G_FreeEdict(thinker);
		return;
	}
	if (G_Time() < thinker->freetime) return;
	level = G_UnitAbilityLevel(wagon, code);
	interval = S_SpellDuration(code, level, false);
	interval_ms = interval > 0.0f ? (uint32_t)(interval * 1000.0f) : 0;
	if (!interval_ms) { G_FreeEdict(thinker); return; }
	unit_id = S_SpellUnitId(code, level);
	cap = (uint32_t)MAX(0.0f, S_SpellData(code, level, 1));
	if (unit_id && exhume_count(wagon, unit_id) < cap) exhume_spawn(wagon, unit_id);
	thinker->freetime = G_Time() + interval_ms;
}

/* Arm the thinker on the first update after the unit gains Aexh. */
static void exhume_ensure(edict_t * wagon) {
	uint32_t level = G_UnitAbilityLevel(wagon, BZ_AEXH);
	float interval;
	edict_t * thinker;
	if (!wagon || !level || M_IsDead(wagon) || exhume_find_thinker(wagon)) return;
	interval = S_SpellDuration(BZ_AEXH, level, false);
	if (interval <= 0.0f) return;
	thinker = G_Spawn();
	if (!thinker) return;
	thinker->owner = wagon;
	thinker->think = exhume_think;
	thinker->freetime = G_Time() + (uint32_t)(interval * 1000.0f);
}

/* Name=Exhume Corpses
 * Dur = spawn interval, DataA = maximum authored corpse count, UnitID = corpse type.
 */
BZ_ABILITY_PROC(CAbilityExhumeCorpses) {
	switch (msg) {
	case A_UPDATE: exhume_ensure(ent); return true;
	default: return false;
	}
}
