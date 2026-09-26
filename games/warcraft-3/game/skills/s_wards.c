#include "s_skills.h"

#define ID_STASIS_BUFF "Bsta"
#define ID_ADT1 MAKEFOURCC('A', 'd', 't', '1') // Detect (Sentry Ward); Rng is true-sight radius
#define ID_ASTA MAKEFOURCC('A', 's', 't', 'a') // Stasis Trap placement
#define ID_AEYE MAKEFOURCC('A', 'e', 'y', 'e') // Sentry Ward placement base code
#define ID_AMIN MAKEFOURCC('A', 'm', 'i', 'n') // Mine - exploding intrinsic behavior
#define ID_AROO MAKEFOURCC('A', 'r', 'o', 'o') // Root
#define ID_ARO1 MAKEFOURCC('A', 'r', 'o', '1') // Root (Ancients)
#define ID_ARO2 MAKEFOURCC('A', 'r', 'o', '2') // Root (Ancient Protector)

void stasis_trap_think(edict_t *thinker);

/* Land units only; air never arms or takes the stun. */
static bool stasis_land_enemy(edict_t *ward, edict_t *target, float radius) {
	if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(ward, target)) return false;
	if (target->targtype == TARG_AIR || target->targtype == TARG_STRUCTURE) return false;
	if (G_UnitIsBuilding(target->class_id)) return false;
	return Vector2_distance(&target->s.origin2, &ward->s.origin2) <= radius;
}

static cstring_t stasis_buff(uint32_t code, uint32_t level) {
	cstring_t buff = G_AbilityLevel(code, level)->buffID;
	return (buff && strlen(buff) >= 4) ? buff : ID_STASIS_BUFF;
}

static void stasis_kill_ward(edict_t *ward) {
	edict_t *th_list[8];
	uint32_t n = 0;
	if (!ward || !ward->inuse) return;
	FILTER_EDICTS(th, th->inuse && th->owner == ward && th->think == stasis_trap_think)
		if (n < 8) th_list[n++] = th;
	FOR_LOOP(i, n) G_FreeEdict(th_list[i]);
	G_FreeEdict(ward);
}

/* After DataA arm delay, DataB trigger, DataC stun + peer-ward destroy; DataD/HeroDur stun. */
void stasis_trap_think(edict_t *thinker) {
	edict_t *ward = thinker->owner, *peers[16];
	uint32_t code = thinker->class_id, level = (uint32_t)thinker->wait, pn = 0;
	float detect, area, stun;
	cstring_t buff;
	bool trigger = false;

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
	uint32_t level = S_SpellLevel(caster, spell->code);
	uint32_t unit_id = S_SpellUnitId(spell->code, level);
	float life = S_SpellDuration(spell->code, level, false);
	float arm = S_SpellData(spell->code, level, 1);
	edict_t *ward, *thinker;

	if (!unit_id) {
		fprintf(stderr, "WC3 Stasis Trap: missing UnitID for %.4s\n", (cstring_t)&spell->code);
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
	thinker->wait = (float)level;
	thinker->freetime = G_Time() + (uint32_t)(MAX(0.0f, arm) * 1000.0f);
	thinker->think = stasis_trap_think;
}

/* Item Place Goblin Land Mine is an ordinary authored point summon. Keep the
 * placed unit as a real player-owned unit so its Amin/Amnx abilities own the
 * trap and death-damage lifecycle. */
BZ_ABILITY_PROC(CAbilityPlaceMine) {
	uint32_t code = call && call->item ? call->item->code : 0;
	uint32_t level = S_SpellLevel(ent, code);
	uint32_t unit_id = code ? S_SpellUnitId(code, level) : 0;

	switch (msg) {
	case A_VALIDATE:
		return ent && call && call->target && call->target->type == SPELL_TARGET_POINT && unit_id;
	case A_EXECUTE: {
		edict_t *mine;
		float life;
		if (!ent || !call || !call->target || call->target->type != SPELL_TARGET_POINT || !unit_id) return false;
		life = S_SpellDuration(code, level, false);
		mine = S_SummonAt(ent, unit_id, &call->target->point, life);
		if (!mine) return false;
		mine->summon_ability = code;
		return true;
	}
	default:
		return CAbilitySimpleSpell(ent, msg, call);
	}
}

static edict_t *land_mine_thinker(edict_t const *mine) {
	if (!mine) return NULL;
	FILTER_EDICTS(th, th->inuse && th->owner == mine && th->think == land_mine_think) return th;
	return NULL;
}

static void land_mine_remove_thinker(edict_t const *mine) {
	edict_t *thinker = land_mine_thinker(mine);
	if (thinker) G_FreeEdict(thinker);
}

/* Patch 1.03 explicitly stopped rooted Ancients from triggering land mines.
 * Their mobile/uprooted form is the exception to the ordinary structure
 * exclusion, so key that distinction to the Root-family ability plus current
 * movement state instead of hard-coding Night Elf unit rawcodes. */
static bool land_mine_is_uprooted_ancient(edict_t const *target) {
	bool root_capable;

	if (!target || (target->targtype != TARG_STRUCTURE && !G_UnitIsBuilding(target->class_id))) return false;
	root_capable = G_UnitAbilityLevel(target, ID_AROO) ||
		G_UnitAbilityLevel(target, ID_ARO1) || G_UnitAbilityLevel(target, ID_ARO2);
	return root_capable && target->movetype != MOVETYPE_NONE;
}

/* Retail mines are walk-over traps: air and ordinary/rooted structures do not
 * trigger them, while a mobile uprooted Ancient behaves as a ground unit. */
static bool land_mine_trigger_target(edict_t *mine, edict_t *target, float radius) {
	bool structure;

	if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(mine, target)) return false;
	if (target->targtype == TARG_AIR) return false;
	structure = target->targtype == TARG_STRUCTURE || G_UnitIsBuilding(target->class_id);
	if (structure && !land_mine_is_uprooted_ancient(target)) return false;
	return Vector2_distance(&target->s.origin2, &mine->s.origin2) <= radius;
}

/* Amin DataA is activation delay, DataB is invisibility transition time, and
 * Cast Range is the proximity trigger radius. The mine kills itself through
 * the normal death path so Amnx and scripted death events still fire. */
void land_mine_think(edict_t *thinker) {
	edict_t *mine = thinker ? thinker->owner : NULL;
	uint32_t code, level;
	float radius;
	bool trigger = false;

	if (!thinker || !thinker->inuse) return;
	if (!mine || !mine->inuse || mine->spawn_time != thinker->channel.owner_spawn_time || M_IsDead(mine)) {
		G_FreeEdict(thinker);
		return;
	}
	code = thinker->class_id;
	level = MAX(1u, (uint32_t)thinker->wait);
	if (thinker->damage && G_Time() >= thinker->resources) {
		mine->s.renderfx |= RF_HIDDEN;
		thinker->damage = 0;
	}
	if (G_Time() < thinker->freetime) return;
	radius = S_SpellRange(code, level);
	if (radius <= 0.0f) return;
	FILTER_EDICTS(target, land_mine_trigger_target(mine, target, radius)) { trigger = true; break; }
	if (!trigger) return;

	/* Retire the thinker before death dispatch: CAbilityLandMine receives
	 * A_DEATH synchronously and must not free the currently executing edict. */
	G_FreeEdict(thinker);
	G_SetHealth(mine, 0.0f);
	if (mine->die) mine->die(mine, mine);
	else unit_die(mine, mine);

	/* The stock Goblin Land Mine keeps its detonation particles in the model's
	 * Death Spell sequence.  Keep generic unit_die() authoritative for death
	 * events, Amnx, corpse/decay setup, and ordinary damage destruction; only
	 * the Amin proximity-trigger path replaces the visual sequence afterward.
	 * If a custom model has no tagged Death Spell sequence, the normal animation
	 * selector falls back within the Death family. */
	if (mine->inuse && M_IsDead(mine)) {
		G_SetUnitAnimation(mine, "death spell");
		mine->animation_override = true;
		if (mine->animation) mine->s.frame = mine->animation->interval[0];
	}
}

static bool land_mine_initialize(edict_t *mine, uint32_t code) {
	uint32_t level;
	float arm, invis, collision;
	edict_t *thinker;

	if (!mine || !mine->inuse || !code || !(level = G_UnitAbilityLevel(mine, code))) return false;
	thinker = land_mine_thinker(mine);
	collision = thinker ? thinker->collision : mine->collision;
	arm = MAX(0.0f, S_SpellData(code, level, 1));
	invis = S_SpellData(code, level, 2);
	if (!thinker) thinker = G_Spawn();
	if (!thinker) { fprintf(stderr, "WC3 land mine: failed to allocate thinker for unit %u\n", mine->s.number); return false; }
	/* Apply gameplay/presentation state only after the thinker owns the lifecycle. */
	mine->collision = 0.0f;
	mine->s.renderfx &= ~RF_HIDDEN;
	if (invis == 0.0f) mine->s.renderfx |= RF_HIDDEN;
	thinker->owner = mine;
	thinker->channel.owner_spawn_time = mine->spawn_time;
	thinker->class_id = code;
	thinker->wait = (float)level;
	thinker->collision = collision;
	thinker->damage = 0;
	thinker->resources = 0;
	thinker->freetime = G_Time() + (uint32_t)(arm * 1000.0f);
	if (invis > 0.0f) {
		thinker->damage = 1;
		thinker->resources = G_Time() + (uint32_t)(invis * 1000.0f);
	}
	thinker->think = land_mine_think;
	return true;
}

BZ_ABILITY_PROC(CAbilityLandMine) {
	uint32_t code = call && call->item && call->item->code ? call->item->code : ID_AMIN;
	bool owns_mine_ability = ent && code && G_UnitAbilityLevel(ent, code);

	switch (msg) {
	case A_UNIT_INIT:
	case A_ENABLE:
	case A_LEVEL_CHANGED:
		return land_mine_initialize(ent, code);
	case A_DEATH:
		if (!owns_mine_ability && !land_mine_thinker(ent)) return false;
		land_mine_remove_thinker(ent);
		/* Death/explosion presentation must no longer be hidden by the trap's
		 * live-unit invisibility state. */
		ent->s.renderfx &= ~RF_HIDDEN;
		return true;
	case A_DISABLE:
		/* G_ActorRemoveSkill removes the rawcode before dispatching A_DISABLE,
		 * so cleanup cannot rely on G_UnitAbilityLevel() still finding Amin. */
		if (!ent) return false;
		{
			edict_t *thinker = land_mine_thinker(ent);
			if (thinker) {
				ent->collision = ent->s.collision = thinker->collision;
				G_FreeEdict(thinker);
			}
		}
		ent->s.renderfx &= ~RF_HIDDEN;
		return true;
	case A_UNIT_REMOVE:
		if (!owns_mine_ability && !land_mine_thinker(ent)) return false;
		land_mine_remove_thinker(ent);
		return true;
	default:
		return CAbilityPassive(ent, msg, call);
	}
}

static bool ward_is_sentry(edict_t const *ward) {
	return ward && ward->inuse && G_AbilityCode(ward->summon_ability) == ID_AEYE;
}

#define ID_APIV MAKEFOURCC('A', 'p', 'i', 'v')
#define ID_BINV MAKEFOURCC('B', 'i', 'n', 'v')
#define ID_BOWK MAKEFOURCC('B', 'O', 'w', 'k')
#define ID_ASTA MAKEFOURCC('A', 's', 't', 'a')

/* Passive detectors all expose their authored true-sight radius through Rng.
 * Keep this list data-driven rather than keying detection to unit rawcodes. */
static float unit_detector_range(edict_t const *detector) {
	static uint32_t const abilities[] = {
		MAKEFOURCC('A', 'd', 'e', 't'), /* Detector */
		MAKEFOURCC('A', 'g', 'y', 'v'), /* True Sight (Flying Machine) */
		MAKEFOURCC('A', 't', 'r', 'u'), /* True Sight (Undead Shade) */
		MAKEFOURCC('A', 'd', 't', 's'), /* Magic Sentry */
		MAKEFOURCC('A', 'b', 'd', 't'), /* Burrow Detection */
		ID_ADT1,                         /* Detect (Sentry Ward) */
	};
	float range = 0.0f;
	FOR_LOOP(i, sizeof(abilities) / sizeof(*abilities)) {
		uint32_t level = G_UnitAbilityLevel(detector, abilities[i]);
		if (level) range = MAX(range, S_SpellRange(abilities[i], level));
	}
	return range;
}

static float permanent_invisibility_transition(edict_t const *unit) {
	uint32_t level = unit ? G_UnitAbilityLevel(unit, ID_APIV) : 0;
	return level ? S_SpellDuration(ID_APIV, level, false) : -1.0f;
}

bool S_PermanentInvisibilityActive(edict_t const *unit) {
	return unit && unit->inuse && (unit->runtime.flags & UNIT_BALANCE_PERMANENT_INVISIBLE) &&
		G_Time() >= unit->permanent_invisibility_reveal_until;
}

void S_PermanentInvisibilityInitialize(edict_t *unit) {
    float transition;
    if (!unit || !G_UnitAbilityLevel(unit, ID_APIV)) {
        if (unit) {
            unit->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
            unit->permanent_invisibility_reveal_until = 0;
        }
        return;
	}
	transition = permanent_invisibility_transition(unit);
	/* Negative transition is the authored opt-out: this unit never enters invisibility. */
	if (transition < 0.0f) {
		unit->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
		unit->permanent_invisibility_reveal_until = 0;
		return;
	}
	unit->runtime.flags |= UNIT_BALANCE_PERMANENT_INVISIBLE;
    unit->permanent_invisibility_reveal_until =
        G_Time() + (uint32_t)(MAX(0.0f, transition) * 1000.0f);
}

/* Own Permanent Invisibility's spawn, add, remove, and level-change lifecycle. */
BZ_ABILITY_PROC(CAbilityPermanentInvisibility) {
    switch (msg) {
    case A_UNIT_INIT:
    case A_ENABLE:
    case A_LEVEL_CHANGED:
        S_PermanentInvisibilityInitialize(ent); return true;
    case A_DISABLE:
    case A_UNIT_REMOVE:
        if (ent) {
            ent->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
            ent->permanent_invisibility_reveal_until = 0;
        }
        return true;
    default: return CAbilityPassive(ent, msg, call);
    }
}

void S_PermanentInvisibilityReveal(edict_t *unit) {
	float transition;
	if (!unit || !(unit->runtime.flags & UNIT_BALANCE_PERMANENT_INVISIBLE)) return;
	transition = permanent_invisibility_transition(unit);
	if (transition < 0.0f) {
		unit->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
		unit->permanent_invisibility_reveal_until = 0;
		return;
	}
	unit->permanent_invisibility_reveal_until =
		G_Time() + (uint32_t)(MAX(0.0f, transition) * 1000.0f);
}

/* RF_HIDDEN is also used for cargo, mines, training and revival.  This narrow
 * predicate identifies only states that true sight is allowed to reveal in a
 * per-client snapshot. */
bool S_UnitUsesInvisibilityRenderFlag(edict_t const *unit) {
	uint32_t summon;
	if (!unit || !unit->inuse || !(unit->s.renderfx & RF_HIDDEN)) return false;
	if (G_UnitStatusLevel(unit, ID_BINV) || G_UnitStatusLevel(unit, ID_BOWK)) return true;
	if (G_UnitAbilityLevel(unit, ID_AMIN)) return true;
	summon = G_AbilityCode(unit->summon_ability);
	return summon == ID_AEYE || summon == ID_ASTA;
}

static bool detector_shared_with_player(edict_t const *detector, uint32_t player) {
	return detector && detector->s.player < MAX_PLAYERS &&
		G_FowPlayersShareVision(player, detector->s.player);
}

/* Player-local true sight.  Detector ownership/shared vision determines who
 * receives the reveal; the hidden entity itself is never globally unhidden. */
bool S_UnitIsDetectedByPlayer(edict_t const *unit, uint32_t player) {
	if (!unit || !unit->inuse || player >= MAX_PLAYERS) return false;

	/* Far Sight owns an independent timed thinker after the caster is gone. */
	FILTER_EDICTS(sight, sight->inuse && sight->think == far_sight_think &&
	              sight->s.player < MAX_PLAYERS && G_Time() < sight->spawn_time &&
	              sight->collision > 0.0f && detector_shared_with_player(sight, player)) {
		if (Vector2_distance(&sight->s.origin2, &unit->s.origin2) <= sight->collision) return true;
	}

	/* Sentry wards are active hidden detectors whose fixture/unit data may not
	 * carry authored monster health; their summon lifecycle is authoritative. */
	FILTER_EDICTS(detector, detector != unit &&
	              (ward_is_sentry(detector) || S_SpellIsAliveTarget(detector)) &&
	              detector_shared_with_player(detector, player) &&
	              S_SpellIsEnemy(detector, (edict_t *)unit)) {
		float range = ward_is_sentry(detector) ? detector->wait : unit_detector_range(detector);
		if (range > 0.0f && Vector2_distance(&detector->s.origin2, &unit->s.origin2) <= range) return true;
	}
	return false;
}

/* Gameplay invisibility is viewer-specific. Owners and players receiving the
 * owner's shared vision keep access to their invisible units; hostile viewers
 * need a detector covering the target. Non-invisibility RF_HIDDEN states are
 * deliberately outside this predicate. */
bool S_UnitIsInvisibleToPlayer(edict_t const *unit, uint32_t player) {
	if (!unit || !unit->inuse || player >= MAX_PLAYERS) return false;
	if (unit->s.player < MAX_PLAYERS && G_FowPlayersShareVision(player, unit->s.player)) return false;
	if (!S_PermanentInvisibilityActive(unit) && !S_UnitUsesInvisibilityRenderFlag(unit)) return false;
	return !S_UnitIsDetectedByPlayer(unit, player);
}

/* Legacy aggregate query retained for ability/tests that only need to know
 * whether any player's detector currently covers the unit. */
bool S_UnitIsDetected(edict_t const *unit) {
	if (!unit || !unit->inuse) return false;
	FOR_LOOP(player, MAX_PLAYERS)
		if (S_UnitIsDetectedByPlayer(unit, player)) return true;
	return false;
}

/* Name=Sentry Ward; UnitID + Dur summon; detect radius from Adt1 Rng stored on ward.wait. */
BZ_SIMPLE_SPELL_PROC(AbilityEvilEye) {
	uint32_t level = S_SpellLevel(caster, spell->code);
	uint32_t unit_id = S_SpellUnitId(spell->code, level);
	float life = S_SpellDuration(spell->code, level, false);
	edict_t *ward;

	if (!unit_id) {
		fprintf(stderr, "WC3 Sentry Ward: missing UnitID for %.4s\n", (cstring_t)&spell->code);
		return;
	}
	ward = S_SummonAt(caster, unit_id, &st.point, life);
	if (!ward) return;
	ward->summon_ability = spell->code;
	ward->s.renderfx |= RF_HIDDEN;
	ward->wait = S_SpellRange(ID_ADT1, 1);
	if (ward->wait <= 0.0f)
		fprintf(stderr, "WC3 Sentry Ward: Adt1 Rng missing for detect on %.4s\n", (cstring_t)&spell->code);
}
