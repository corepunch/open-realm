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

#define ID_APIV MAKEFOURCC('A', 'p', 'i', 'v')
#define ID_BINV MAKEFOURCC('B', 'i', 'n', 'v')
#define ID_BOWK MAKEFOURCC('B', 'O', 'w', 'k')
#define ID_ASTA MAKEFOURCC('A', 's', 't', 'a')

/* Passive detectors all expose their authored true-sight radius through Rng.
 * Keep this list data-driven rather than keying detection to unit rawcodes. */
static FLOAT unit_detector_range(LPCEDICT detector) {
	static DWORD const abilities[] = {
		MAKEFOURCC('A', 'd', 'e', 't'), /* Detector */
		MAKEFOURCC('A', 'g', 'y', 'v'), /* True Sight */
		MAKEFOURCC('A', 'd', 't', 's'), /* Magic Sentry */
		MAKEFOURCC('A', 'b', 'd', 't'), /* Burrow Detection */
		ID_ADT1,                         /* Detect (Sentry Ward) */
	};
	FLOAT range = 0.0f;
	FOR_LOOP(i, sizeof(abilities) / sizeof(*abilities)) {
		DWORD level = G_UnitAbilityLevel(detector, abilities[i]);
		if (level) range = MAX(range, S_SpellRange(abilities[i], level));
	}
	return range;
}

static FLOAT permanent_invisibility_transition(LPCEDICT unit) {
	DWORD level = unit ? G_UnitAbilityLevel(unit, ID_APIV) : 0;
	return level ? S_SpellDuration(ID_APIV, level, false) : -1.0f;
}

BOOL S_PermanentInvisibilityActive(LPCEDICT unit) {
	return unit && unit->inuse && (unit->runtime.flags & UNIT_BALANCE_PERMANENT_INVISIBLE) &&
		G_Time() >= unit->permanent_invisibility_reveal_until;
}

void S_PermanentInvisibilityInitialize(LPEDICT unit) {
    FLOAT transition;
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
        G_Time() + (DWORD)(MAX(0.0f, transition) * 1000.0f);
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

void S_PermanentInvisibilityReveal(LPEDICT unit) {
	FLOAT transition;
	if (!unit || !(unit->runtime.flags & UNIT_BALANCE_PERMANENT_INVISIBLE)) return;
	transition = permanent_invisibility_transition(unit);
	if (transition < 0.0f) {
		unit->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
		unit->permanent_invisibility_reveal_until = 0;
		return;
	}
	unit->permanent_invisibility_reveal_until =
		G_Time() + (DWORD)(MAX(0.0f, transition) * 1000.0f);
}

/* RF_HIDDEN is also used for cargo, mines, training and revival.  This narrow
 * predicate identifies only states that true sight is allowed to reveal in a
 * per-client snapshot. */
BOOL S_UnitUsesInvisibilityRenderFlag(LPCEDICT unit) {
	DWORD summon;
	if (!unit || !unit->inuse || !(unit->s.renderfx & RF_HIDDEN)) return false;
	if (G_UnitStatusLevel(unit, ID_BINV) || G_UnitStatusLevel(unit, ID_BOWK)) return true;
	summon = G_AbilityCode(unit->summon_ability);
	return summon == ID_AEYE || summon == ID_ASTA;
}

static BOOL detector_shared_with_player(LPCEDICT detector, DWORD player) {
	return detector && detector->s.player < MAX_PLAYERS &&
		G_FowPlayersShareVision(player, detector->s.player);
}

/* Player-local true sight.  Detector ownership/shared vision determines who
 * receives the reveal; the hidden entity itself is never globally unhidden. */
BOOL S_UnitIsDetectedByPlayer(LPCEDICT unit, DWORD player) {
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
	              S_SpellIsEnemy(detector, (LPEDICT)unit)) {
		FLOAT range = ward_is_sentry(detector) ? detector->wait : unit_detector_range(detector);
		if (range > 0.0f && Vector2_distance(&detector->s.origin2, &unit->s.origin2) <= range) return true;
	}
	return false;
}

/* Gameplay invisibility is viewer-specific. Owners and players receiving the
 * owner's shared vision keep access to their invisible units; hostile viewers
 * need a detector covering the target. Non-invisibility RF_HIDDEN states are
 * deliberately outside this predicate. */
BOOL S_UnitIsInvisibleToPlayer(LPCEDICT unit, DWORD player) {
	if (!unit || !unit->inuse || player >= MAX_PLAYERS) return false;
	if (unit->s.player < MAX_PLAYERS && G_FowPlayersShareVision(player, unit->s.player)) return false;
	if (!S_PermanentInvisibilityActive(unit) && !S_UnitUsesInvisibilityRenderFlag(unit)) return false;
	return !S_UnitIsDetectedByPlayer(unit, player);
}

/* Legacy aggregate query retained for ability/tests that only need to know
 * whether any player's detector currently covers the unit. */
BOOL S_UnitIsDetected(LPCEDICT unit) {
	if (!unit || !unit->inuse) return false;
	FOR_LOOP(player, MAX_PLAYERS)
		if (S_UnitIsDetectedByPlayer(unit, player)) return true;
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
