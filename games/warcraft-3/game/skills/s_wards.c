#include "../g_entity_set.h"
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
	if (G_UnitTargetType(target) == TARG_AIR || G_UnitIsStructure(target)) return false;
	return Vector2_distance(&target->s.origin2, &ward->s.origin2) <= radius;
}

static cstring_t stasis_buff(uint32_t code, uint32_t level) {
	cstring_t buff = S_SpellBuffId(code, level);
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
	edict_t *ward = S_SpellChannelOwner(thinker), *peers[16];
	uint32_t code = thinker->class_id, level = (uint32_t)thinker->wait, pn = 0;
	float detect, area, stun;
	cstring_t buff;
	bool trigger = false;

	/* Match Pocket Factory: only require the ward slot; fixture UnitBalance may leave HP at 0. */
	if (!ward) { G_FreeEdict(thinker); return; }
	if (G_Time() < thinker->freetime) return;
	detect = S_SpellData(code, level, 2);
	FILTER_EDICTS(t, stasis_land_enemy(ward, t, detect)) { trigger = true; break; }
	if (!trigger) return;
	area = S_SpellData(code, level, 3);
	buff = stasis_buff(code, level);
	FILTER_EDICTS(t, stasis_land_enemy(ward, t, area)) {
		if (!t->data.UnitBalance) continue;
		stun = G_UnitIsHero(t) ? S_SpellDuration(code, level, true) : S_SpellData(code, level, 4);
		(void)S_SpellApplyTimedStatus(t, buff, 1, stun);
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
	ward = S_SummonAbilityAt(caster, spell->code, unit_id, &st.point, life);
	if (!ward) return;
	ward->summon_ability = spell->code;
	G_SetEntityHidden(ward, true);
	thinker = S_SpellIdentityThinker(ward, spell->code, NULL);
	if (!thinker) { G_FreeEdict(ward); return; }
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
		mine = S_SummonAbilityAt(ent, code, unit_id, &call->target->point, life);
		if (!mine) return false;
		return true;
	}
	default:
		return CAbilitySimpleSpell(ent, msg, call);
	}
}

/* The owner pointer and thinker callback remain authoritative. Cache their
 * reverse lookup, rebuilding once after map/save replacement rather than
 * scanning every edict for every ordinary unit/effect removal. */
static edict_t *land_mine_thinkers[MAX_ENTITIES];
static entitySet_t land_mine_members;
static bool land_mine_multiple[MAX_ENTITIES];
static bool land_mine_index_valid;
#ifdef BZ_TESTS
static uint32_t land_mine_lookup_visits;
#endif

void S_ResetLandMineThinkers(void) {
    memset(land_mine_thinkers, 0, sizeof(land_mine_thinkers));
    land_mine_members = (entitySet_t){0};
    memset(land_mine_multiple, 0, sizeof(land_mine_multiple));
    land_mine_index_valid = false;
}

static uint32_t land_mine_owner_index(edict_t const *mine) {
    uintptr_t offset = (uintptr_t)mine - (uintptr_t)g_edicts;
    return offset < sizeof(*mine) * MAX_ENTITIES && offset % sizeof(*mine) == 0
        ? (uint32_t)(offset / sizeof(*mine)) : MAX_ENTITIES;
}

static void land_mine_index_thinker(edict_t *thinker) {
    uint32_t index = land_mine_owner_index(thinker->owner);
    if (index == MAX_ENTITIES) return;
    entity_set_put(&land_mine_members, (uint32_t)(thinker - g_edicts), true);
    edict_t *previous = land_mine_thinkers[index];
    if (previous && previous != thinker && previous->inuse && previous->owner == thinker->owner &&
        previous->think == land_mine_think) land_mine_multiple[index] = true;
    /* The old ascending scan returns the lowest matching thinker. Preserve
     * that choice if a restored world contains more than one for an owner. */
    if (!previous || !previous->inuse || previous->owner != thinker->owner ||
        previous->think != land_mine_think || thinker->s.number < previous->s.number)
        land_mine_thinkers[index] = thinker;
}

static edict_t *land_mine_thinker(edict_t const *mine) {
    uint32_t index = land_mine_owner_index(mine);
    if (index == MAX_ENTITIES) return NULL;
    if (!land_mine_index_valid) {
        FOR_LOOP(i, globals.num_edicts) {
#ifdef BZ_TESTS
            land_mine_lookup_visits++;
#endif
            edict_t *thinker = g_edicts + i;
            if (thinker->inuse && thinker->owner && thinker->think == land_mine_think)
                land_mine_index_thinker(thinker);
        }
        land_mine_index_valid = true;
    }
    edict_t *thinker = land_mine_thinkers[index];
    if (thinker && thinker->inuse && thinker->owner == mine && thinker->think == land_mine_think) return thinker;
    if (land_mine_multiple[index]) {
        /* A retired first thinker must expose the next original match. Only
         * thinker candidates participate, and their live ownership is checked. */
        for (uint32_t i = entity_set_next(&land_mine_members, 0); i < globals.num_edicts;
             i = entity_set_next(&land_mine_members, i + 1)) {
            thinker = g_edicts + i;
#ifdef BZ_TESTS
            land_mine_lookup_visits++;
#endif
            if (thinker->inuse && thinker->owner == mine && thinker->think == land_mine_think)
                return land_mine_thinkers[index] = thinker;
        }
    }
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
	edict_t *mine = S_SpellChannelOwner(thinker);
	uint32_t code, level;
	float radius;
	bool trigger = false;

	if (!thinker || !thinker->inuse) return;
	if (!mine || M_IsDead(mine)) {
		G_FreeEdict(thinker);
		return;
	}
	code = thinker->class_id;
	level = MAX(1u, (uint32_t)thinker->wait);
	if (thinker->damage && G_Time() >= thinker->resources) {
		G_SetEntityHidden(mine,true);
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
	G_SetEntityHidden(mine,false);
	if (invis == 0.0f) G_SetEntityHidden(mine,true);
	thinker->owner = mine;
	if (!thinker->channel) thinker->channel = G_AllocChannel();
	assert(thinker->channel);
	thinker->channel->owner_spawn_time = mine->spawn_time;
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
    land_mine_index_thinker(thinker);
	return true;
}

BZ_ABILITY_PROC(CAbilityLandMine) {
	uint32_t code = call && call->item && call->item->code ? call->item->code : ID_AMIN;

	switch (msg) {
    case A_UNIT_TYPE_INIT:
        if (ent || !call) return UNIT_INIT_UNKNOWN;
        return G_UnitHasAuthoredAbility(call->unit_type, code) ? UNIT_INIT_RUN : UNIT_INIT_SKIP_FALSE;
	case A_UNIT_EVENT_MASK:
		return UNIT_MESSAGE_SUBSCRIPTIONS(A_UNIT_INIT, A_ENABLE, A_LEVEL_CHANGED, A_DEATH, A_DISABLE, A_UNIT_REMOVE);
	case A_UNIT_INIT:
	case A_ENABLE:
	case A_LEVEL_CHANGED:
		return land_mine_initialize(ent, code);
	case A_DEATH:
		if (!(ent && code && G_UnitAbilityLevel(ent, code)) && !land_mine_thinker(ent)) return false;
		land_mine_remove_thinker(ent);
		/* Death/explosion presentation must no longer be hidden by the trap's
		 * live-unit invisibility state. */
		G_SetEntityHidden(ent,false);
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
		G_SetEntityHidden(ent,false);
		return true;
	case A_UNIT_REMOVE:
		if (!(ent && code && G_UnitAbilityLevel(ent, code)) && !land_mine_thinker(ent)) return false;
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

bool S_GhostActive(edict_t const *unit) {
	return unit && unit->inuse && (unit->runtime.flags & UNIT_BALANCE_GHOST_INVISIBLE);
}

bool S_PermanentInvisibilityActive(edict_t const *unit) {
	return unit && unit->inuse && (unit->runtime.flags & UNIT_BALANCE_PERMANENT_INVISIBLE);
}

bool S_UnitStatusIsTemporaryInvisibility(heroabilitystatus_t const *status) {
	abilityitem_t item;
	if (!status || !status->level) return false;
	if (status->code == ID_BINV || status->code == ID_BOWK) return true;
	if (!status->data) return false;
	item = S_AbilityItem(status->data);
	return item.ability && (item.ability->proc == CAbilityInvisibility ||
	                        item.ability->proc == CAbilityItemInvis);
}

bool S_UnitHasTemporaryInvisibility(edict_t const *unit, heroabilitystatus_t const *except) {
	if (!unit) return false;
	FOR_LOOP(i, G_UnitStatusSlotCount(unit))
		if (unit->abilstatus + i != except &&
		    S_UnitStatusIsTemporaryInvisibility(unit->abilstatus + i)) return true;
	return false;
}

bool S_UnitHasInvisibilityState(edict_t const *unit) {
	return S_PermanentInvisibilityActive(unit) || S_GhostActive(unit) ||
	       S_ShadowMeldActive(unit) || S_UnitUsesInvisibilityRenderFlag(unit);
}

/* Derived membership contains only pending fades. Ordinary lookup is O(1),
 * admission/cancellation O(log N); unrelated units never enter a timer scan. */
static edict_t *invisibility_heap[MAX_ENTITIES];
static uint32_t invisibility_positions[MAX_ENTITIES], invisibility_count;

static uint32_t invisibility_index(edict_t const *unit) {
    uintptr_t offset=(uintptr_t)unit-(uintptr_t)g_edicts;
    return g_edicts && offset%sizeof(*unit)==0 && offset/sizeof(*unit)<MAX_ENTITIES ?
        offset/sizeof(*unit) : MAX_ENTITIES;
}

static bool invisibility_less(edict_t const *a, edict_t const *b) {
    abilityPrimaryTimer_t const *x=&a->permanent_invisibility_fade.request;
    abilityPrimaryTimer_t const *y=&b->permanent_invisibility_fade.request;
    return x->deadline.time==y->deadline.time ? x->sequence<y->sequence : x->deadline.time<y->deadline.time;
}
static void invisibility_put(uint32_t position, edict_t *unit) {
    invisibility_heap[position]=unit;invisibility_positions[unit-g_edicts]=position+1;
}
static void invisibility_remove(edict_t *unit) {
    unit->permanent_invisibility_fade.request.active=false;
    uint32_t index=invisibility_index(unit);
    if(index==MAX_ENTITIES)return;
    uint32_t position=invisibility_positions[index];
    if(!position)return;
    invisibility_positions[index]=0;position--;
    edict_t *last=invisibility_heap[--invisibility_count];
    if(position==invisibility_count)return;
    while(position && invisibility_less(last,invisibility_heap[(position-1)/2])) {
        invisibility_put(position,invisibility_heap[(position-1)/2]);position=(position-1)/2;
    }
    while(position*2+1<invisibility_count) {
        uint32_t child=position*2+1;
        if(child+1<invisibility_count && invisibility_less(invisibility_heap[child+1],invisibility_heap[child]))child++;
        if(!invisibility_less(invisibility_heap[child],last))break;
        invisibility_put(position,invisibility_heap[child]);position=child;
    }
    invisibility_put(position,last);
}
static void invisibility_insert(edict_t *unit) {
    if(invisibility_index(unit)==MAX_ENTITIES)gi.error("Invisibility: invalid timer owner");
    uint32_t position=invisibility_count++;
    while(position && invisibility_less(unit,invisibility_heap[(position-1)/2])) {
        invisibility_put(position,invisibility_heap[(position-1)/2]);position=(position-1)/2;
    }
    invisibility_put(position,unit);
}
/* 6696e0 sets the near-zero scalar before installing reciprocal fade slope.
 * 003d00 initializes listener tolerance from the original decimal "0.005";
 * 001cb0 initializes its maximum request delay to4. */
static float invisibility_value(edict_t const *unit, wc3Clock_t const *clock) {
    float elapsed=wc3_elapsed(clock,&unit->permanent_invisibility_fade.origin);
    return MAX(0,MIN(1,wc3_add(wc3_float(0x3a83126f),
        wc3_mul(unit->permanent_invisibility_fade.slope,elapsed))));
}
static void invisibility_schedule(edict_t *unit, wc3Clock_t const *clock) {
    /* 161c30 does not create requests below the scalar slope deadzone. */
    if(unit->permanent_invisibility_fade.slope<wc3_float(0x3556bf95))return;
    float delay=wc3_div(wc3_sub(1,invisibility_value(unit,clock)),unit->permanent_invisibility_fade.slope);
    abilityPrimaryTimer_t *request=&unit->permanent_invisibility_fade.request;
    request->deadline=*clock;
    request->deadline.time=wc3_add(clock->time,MAX(wc3_float(0x38d1b717),MIN(4,delay)));
    request->sequence=++level.timer_sequence;request->active=true;
    invisibility_insert(unit);
}
static void invisibility_clear(edict_t *unit) {
    invisibility_remove(unit);
    memset(&unit->permanent_invisibility_fade,0,sizeof(unit->permanent_invisibility_fade));
    unit->runtime.flags&=~UNIT_BALANCE_PERMANENT_INVISIBLE;
}
static void invisibility_begin(edict_t *unit, float transition) {
    bool invisible=S_UnitHasInvisibilityState(unit);
    invisibility_remove(unit);
    if(transition==0)unit->runtime.flags|=UNIT_BALANCE_PERMANENT_INVISIBLE;
    else unit->runtime.flags&=~UNIT_BALANCE_PERMANENT_INVISIBLE;
    unit->permanent_invisibility_fade.origin=G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
    unit->permanent_invisibility_fade.slope=transition ? wc3_recip(transition) : 0;
    if(transition>0)invisibility_schedule(unit,&unit->permanent_invisibility_fade.origin);
    else if(!invisible)S_UnitTargetLost(unit); /* Publish before synchronous subscribers. */
}
void S_PermanentInvisibilityInitialize(edict_t *unit) {
    if(!unit)return;
    float transition=permanent_invisibility_transition(unit);
    if(transition<0) {invisibility_clear(unit);return;}
    invisibility_begin(unit,transition);
}

/* Own Permanent Invisibility's publication and exact scalar listener queue. */
BZ_ABILITY_PROC(CAbilityPermanentInvisibility) {
    switch(msg) {
    case A_UNIT_TYPE_INIT:
        if(ent || !call)return UNIT_INIT_UNKNOWN;
        return G_UnitHasAuthoredAbility(call->unit_type,ID_APIV) ? UNIT_INIT_RUN : UNIT_INIT_SKIP_TRUE;
    case A_UNIT_EVENT_MASK:
        return UNIT_MESSAGE_SUBSCRIPTIONS(A_UNIT_INIT,A_ENABLE,A_LEVEL_CHANGED,A_DISABLE,A_UNIT_REMOVING,A_UNIT_REMOVE);
    case A_UNIT_INIT:case A_ENABLE:case A_LEVEL_CHANGED:
        S_PermanentInvisibilityInitialize(ent);return true;
    case A_DISABLE:case A_UNIT_REMOVING:case A_UNIT_REMOVE:
        if(ent)invisibility_clear(ent);
        return true;
    case A_TIMERS_RESET:
        if(ent)return false;
        invisibility_count=0;memset(invisibility_positions,0,sizeof(invisibility_positions));return true;
    case A_TIMERS_REBUILD:
        if(ent)return false;
        invisibility_count=0;memset(invisibility_positions,0,sizeof(invisibility_positions));
        FOR_LOOP(i,globals.num_edicts) {
            edict_t *unit=g_edicts+i;
            if(unit->inuse && unit->permanent_invisibility_fade.request.active)invisibility_insert(unit);
        }
        return true;
    case A_PRIMARY_TIMER_NEXT:
        if(ent || !call || !call->primary_timer || !invisibility_count)return false;
        call->primary_timer->deadline=invisibility_heap[0]->permanent_invisibility_fade.request.deadline;
        call->primary_timer->sequence=invisibility_heap[0]->permanent_invisibility_fade.request.sequence;
        return true;
    case A_PRIMARY_TIMER_FIRE: {
        if(ent || !invisibility_count)return false;
        edict_t *unit=invisibility_heap[0];
        wc3Clock_t clock=unit->permanent_invisibility_fade.request.deadline;
        invisibility_remove(unit);
        float value=invisibility_value(unit,&clock);
        /* 162250 accepts strict proximity, not equality or integer duration. */
        if(fabsf(wc3_sub(value,1))<wc3_float(0x3ba3d70a)) {
            unit->runtime.flags|=UNIT_BALANCE_PERMANENT_INVISIBLE;S_UnitTargetLost(unit);
        } else invisibility_schedule(unit,&clock);
        return true;
    }
    case A_PRIMARY_TIMER_REBASE:
        if(ent || !call)return false;
        FOR_LOOP(i,invisibility_count) {
            abilityPrimaryTimer_t *request=&invisibility_heap[i]->permanent_invisibility_fade.request;
            request->deadline.time=wc3_sub(request->deadline.time,call->clock_span);request->deadline.epoch++;
        }
        return true;
    default:return CAbilityPassive(ent,msg,call);
    }
}
void S_PermanentInvisibilityReveal(edict_t *unit) {
    if(!unit || (!(unit->runtime.flags&UNIT_BALANCE_PERMANENT_INVISIBLE) && !unit->permanent_invisibility_fade.request.active))return;
    float transition=permanent_invisibility_transition(unit);
    if(transition<0) {invisibility_clear(unit);return;}
    invisibility_begin(unit,transition);
}

/* RF_HIDDEN is also used for cargo, mines, training and revival.  This narrow
 * predicate identifies only states that true sight is allowed to reveal in a
 * per-client snapshot. */
bool S_UnitUsesInvisibilityRenderFlag(edict_t const *unit) {
	uint32_t summon;
	if (!unit || !unit->inuse || !(unit->s.renderfx & RF_HIDDEN)) return false;
	if (S_UnitHasTemporaryInvisibility(unit, NULL)) return true;
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
	if (!S_PermanentInvisibilityActive(unit) && !S_GhostActive(unit) && !S_ShadowMeldActive(unit) && !S_UnitUsesInvisibilityRenderFlag(unit)) return false;
	return !S_UnitIsDetectedByPlayer(unit, player);
}

/* Script-hidden units are absent from ordinary targeting. RF_HIDDEN also backs
 * player-local invisibility, so retain its detector-aware target policy. */
bool S_UnitIsHiddenFromPlayer(edict_t const *unit, uint32_t player) {
	if (!unit || !unit->inuse) return true;
	if ((unit->s.renderfx & RF_HIDDEN) && !S_UnitUsesInvisibilityRenderFlag(unit)) return true;
	return S_UnitIsInvisibleToPlayer(unit, player);
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
	ward = S_SummonAbilityAt(caster, spell->code, unit_id, &st.point, life);
	if (!ward) return;
	ward->summon_ability = spell->code;
	G_SetEntityHidden(ward,true);
	ward->wait = S_SpellRange(ID_ADT1, 1);
	if (ward->wait <= 0.0f)
		fprintf(stderr, "WC3 Sentry Ward: Adt1 Rng missing for detect on %.4s\n", (cstring_t)&spell->code);
}
