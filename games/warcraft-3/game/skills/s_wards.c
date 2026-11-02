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
	root_capable = G_UnitAbilityClassLevel(target, ID_AROO) ||
		G_UnitAbilityClassLevel(target, ID_ARO1) || G_UnitAbilityClassLevel(target, ID_ARO2);
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

	if (!mine || !mine->inuse || !code || !(level = G_UnitAbilityClassLevel(mine, code))) return false;
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
	(thinker->channel ? thinker->channel : (thinker->channel=G_AllocChannel()));
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
		if (!(ent && code && G_UnitAbilityClassLevel(ent, code)) && !land_mine_thinker(ent)) return false;
		land_mine_remove_thinker(ent);
		/* Death/explosion presentation must no longer be hidden by the trap's
		 * live-unit invisibility state. */
		G_SetEntityHidden(ent,false);
		return true;
	case A_DISABLE:
		/* G_ActorRemoveSkill removes the rawcode before dispatching A_DISABLE,
		 * so cleanup cannot rely on G_UnitAbilityClassLevel() still finding Amin. */
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
		if (!(ent && code && G_UnitAbilityClassLevel(ent, code)) && !land_mine_thinker(ent)) return false;
		land_mine_remove_thinker(ent);
		return true;
	default:
		return CAbilityPassive(ent, msg, call);
	}
}

static bool sentinel_owl_active(edict_t const *owl);

static edict_t *sentinel_tree_at_point(vec2_t const *point) {
	edict_t *nearest = NULL;
	float nearest_distance = 1.0f;
	if (!point) return NULL;
	FILTER_EDICTS(tree, tree->targtype == TARG_TREE && G_DestructableIsAttackable(tree)) {
		float const distance = Vector2_distance(&tree->s.origin2, point);
		if (distance <= nearest_distance) {
			nearest = tree;
			nearest_distance = distance;
		}
	}
	return nearest;
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
	static uint32_t const magic_sentry = MAKEFOURCC('A', 'd', 't', 's');
	static uint32_t const magic_sentry_research = MAKEFOURCC('R', 'h', 's', 'e');
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
		uint32_t level = G_UnitAbilityClassLevel(detector, abilities[i]);
        if (abilities[i] == magic_sentry &&
            G_GetPlayerTechResearchedLevel(G_GetPlayerClientByNumber(detector->s.player), magic_sentry_research) <= 0)
            continue;
		if (level) range = MAX(range, S_SpellRange(abilities[i], level));
	}
	return range;
}

/* Magic Sentry has no TargetArt in either the RoC or TFT AbilityData row.
 * Use its authored caster model as the persistent researched tower visual. */
static uint32_t const magic_sentry = MAKEFOURCC('A', 'd', 't', 's');
static uint32_t const magic_sentry_research = MAKEFOURCC('R', 'h', 's', 'e');
static cstring_t const magic_sentry_model = "Abilities\\Spells\\Human\\MagicSentry\\MagicSentryCaster.mdx";

static edict_t *find_magic_sentry_effect(edict_t *unit) {
	edict_t *effect = NULL;
	if (!unit || unit->s.number >= MAX_ENTITIES) return NULL;
	FILTER_EDICTS(candidate, candidate->owner == unit && candidate->summon_ability == magic_sentry &&
	              (candidate->s.flags & EF_NOT_SELECTABLE)) {
		effect = candidate;
		break;
	}
	return effect;
}

void S_RemoveMagicSentryPresentation(edict_t *unit) {
	edict_t *effect = find_magic_sentry_effect(unit);
	if (!effect) return;
	effect->owner = NULL;
	effect->summon_ability = 0;
	G_DestroyEffect(effect);
}

void S_UpdateMagicSentryPresentation(edict_t *unit) {
	edict_t *effect;
	gameClient_t *owner;
	bool active;

	if (!unit || !unit->inuse || unit->s.number >= MAX_ENTITIES ||
	    !G_UnitAbilityLevel(unit, magic_sentry)) return;
	effect = find_magic_sentry_effect(unit);
	owner = G_GetPlayerClientByNumber(unit->s.player);
	active = owner && G_GetPlayerTechResearchedLevel(owner, magic_sentry_research) > 0;
	if (active) {
		if (!effect) {
			effect = G_SpawnModelEffect(magic_sentry_model, NULL, unit, "overhead", false);
			if (effect) {
				effect->owner = unit;
				effect->summon_ability = magic_sentry;
			}
		}
	} else S_RemoveMagicSentryPresentation(unit);
}

static float permanent_invisibility_transition(edict_t const *unit) {
	uint32_t level = unit ? G_UnitAbilityClassLevel(unit, ID_APIV) : 0;
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
	if (G_UnitStatusClassCode(status) == ID_BINV || G_UnitStatusClassCode(status) == ID_BOWK) return true;
	if (!status->data) return false;
	item = S_AbilityItem(status->data);
	return item.ability && (item.ability->proc == CAbilityInvisibility ||
	                        item.ability->proc == CAbilityItemInvis);
}

bool S_UnitHasTemporaryInvisibility(edict_t const *unit, heroabilitystatus_t const *except) {
	if (!unit) return false;
	FOR_LOOP(i, G_UnitStatusSlotCount(unit))
		if (unit->abilstatus + i != except &&
            S_UnitStatusIsTemporaryInvisibility(unit->abilstatus + i) &&
            (!unit->abilstatus[i].invisibility_owned || unit->permanent_invisibility_fade.published)) return true;
	return false;
}

bool S_UnitHasInvisibilityState(edict_t const *unit) {
	return S_UnitIsBurrowed(unit) || S_PermanentInvisibilityActive(unit) || S_GhostActive(unit) ||
	       S_ShadowMeldActive(unit) || S_UnitUsesInvisibilityRenderFlag(unit);
}

static bool invisibility_intrinsic_render_owner(edict_t const *unit) {
    uint32_t summon=G_AbilityCode(unit->summon_ability);
    return S_UnitIsSubmerged(unit) || G_UnitAbilityClassLevel(unit,ID_AMIN) ||
        summon==ID_AEYE || summon==ID_ASTA;
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
static edict_t *temporary_expiry_heap[MAX_ENTITIES];
static uint32_t temporary_expiry_positions[MAX_ENTITIES],temporary_expiry_count;

static heroabilitystatus_t *temporary_expiry_next(edict_t const *unit) {
    heroabilitystatus_t *best=NULL;
    FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t *s=unit->abilstatus+i;
        if(!s->level || !s->invisibility_owned || !s->class_timer.active)continue;
        abilityPrimaryTimer_t *a=&s->class_timer,*b=best ? &best->class_timer : NULL;
        if(!b || a->deadline.time<b->deadline.time ||
           (a->deadline.time==b->deadline.time && a->sequence<b->sequence))best=s;
    }
    return best;
}
static bool temporary_expiry_less(edict_t const *a,edict_t const *b) {
    abilityPrimaryTimer_t *x=&temporary_expiry_next(a)->class_timer,*y=&temporary_expiry_next(b)->class_timer;
    return x->deadline.time==y->deadline.time ? x->sequence<y->sequence : x->deadline.time<y->deadline.time;
}
static void temporary_expiry_put(uint32_t pos,edict_t *unit) {
    temporary_expiry_heap[pos]=unit;temporary_expiry_positions[unit-g_edicts]=pos+1;
}
static void temporary_expiry_forget(edict_t *unit) {
    uint32_t pos=temporary_expiry_positions[unit-g_edicts];
    if(!pos)return;
    temporary_expiry_positions[unit-g_edicts]=0;pos--;
    edict_t *last=temporary_expiry_heap[--temporary_expiry_count];
    if(pos==temporary_expiry_count)return;
    while(pos && temporary_expiry_less(last,temporary_expiry_heap[(pos-1)/2])) {
        temporary_expiry_put(pos,temporary_expiry_heap[(pos-1)/2]);pos=(pos-1)/2;
    }
    while(pos*2+1<temporary_expiry_count) {
        uint32_t child=pos*2+1;
        if(child+1<temporary_expiry_count && temporary_expiry_less(temporary_expiry_heap[child+1],temporary_expiry_heap[child]))child++;
        if(!temporary_expiry_less(temporary_expiry_heap[child],last))break;
        temporary_expiry_put(pos,temporary_expiry_heap[child]);pos=child;
    }
    temporary_expiry_put(pos,last);
}
static void temporary_expiry_insert(edict_t *unit) {
    if(!unit || !unit->inuse || G_IsDeferredFree(unit))return;
    if(!temporary_expiry_next(unit) || temporary_expiry_positions[unit-g_edicts])return;
    uint32_t pos=temporary_expiry_count++;
    while(pos && temporary_expiry_less(unit,temporary_expiry_heap[(pos-1)/2])) {
        temporary_expiry_put(pos,temporary_expiry_heap[(pos-1)/2]);pos=(pos-1)/2;
    }
    temporary_expiry_put(pos,unit);
}
static void temporary_expiry_schedule(heroabilitystatus_t *slot,wc3Clock_t now,float duration) {
    slot->class_timer=(abilityPrimaryTimer_t){.deadline=now,.sequence=++level.timer_sequence,.active=true};
    slot->class_timer.deadline.time=wc3_add(now.time,MAX(duration,G_ClockMinimumDelay()));
}

/*427610/43b0e0 change the private counter; only its first/last transitions
 * freeze/resume.531420 cancels the request and retains its remainder.532fa0
 * rearms that old remainder even if a refresh started another request while
 * paused. The unit's shared fade belongs to a different clock. */
static void temporary_invisibility_clock_adjust(heroabilitystatus_t *status,int32_t delta) {
    int32_t const before=status->class_clock_depth;
    status->class_clock_depth=(int32_t)((uint32_t)before+(uint32_t)delta);
    wc3Clock_t now=G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
    if(before<1 && status->class_clock_depth>0) {
        status->class_timer_remaining=status->class_timer.active ?
            MAX(0,wc3_elapsed(&status->class_timer.deadline,&now)) : 0;
        status->class_timer.active=false;
    } else if(before>0 && status->class_clock_depth<1) {
        if(status->class_timer_remaining>0)
            temporary_expiry_schedule(status,now,status->class_timer_remaining);
        status->class_timer_remaining=0;
    }
}
void S_AdjustTemporaryInvisibilityClocks(edict_t *unit,int32_t delta) {
    bool changed=false;
    FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t *status=unit->abilstatus+i;
        if(status->invisibility_owned && status->class_code==MAKEFOURCC('B','i','n','v') &&
           status->class_clock_depth>=0) {
            if(!changed)temporary_expiry_forget(unit);
            changed=true;
            temporary_invisibility_clock_adjust(status,delta);
        }
    }
    if(changed)temporary_expiry_insert(unit);
}

/*531a80 refreshes the context before comparing the actual event-clock
 * remainder. A short/equal refresh keeps both deadline and FIFO sequence. */
void S_TemporaryInvisibilitySetDuration(edict_t *unit,heroabilitystatus_t *status,
                                       float duration,bool refresh) {
    if(!unit || !status)return;
    wc3Clock_t now=G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
    float remaining=status->class_timer.active ? wc3_elapsed(&status->class_timer.deadline,&now) : 0;
    if(refresh && duration>0 && !(duration>remaining)) {
        G_InvalidateUnitInfoPanel(unit);return; /* Context may have changed. */
    }
    temporary_expiry_forget(unit);
    status->timestamp=0; /* The primary request owns expiry, not frame polling. */
    status->duration_ms=duration>0 ? (uint32_t)(duration*1000.0f) : 0;
    if(duration>0)temporary_expiry_schedule(status,now,duration);
    else status->class_timer=(abilityPrimaryTimer_t){0};
    temporary_expiry_insert(unit);
    G_InvalidateUnitInfoPanel(unit);
}

static bool invisibility_live(edict_t const *unit,uint32_t birth) {
    return unit->inuse && !G_IsDeferredFree(unit) && unit->spawn_time==birth;
}
typedef struct invisibility_notice_s {
    edict_t *unit;
    uint32_t birth,instance;
    struct invisibility_notice_s *previous;
} invisibility_notice_t;
static invisibility_notice_t *invisibility_notices;
static uint32_t invisibility_publication_depth;
bool S_InvisibilityPublicationPending(void) { return invisibility_publication_depth!=0; }
static void invisibility_notify(edict_t *unit) {
    uint32_t birth=unit->spawn_time;
    invisibility_publication_depth++;
    /* Each status incarnation subscribes once. Reacquire storage after the
     * synchronous Attack recovery, which may grow or remove status slots. */
    FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t *status=unit->abilstatus+i;
        if(!status->level || !status->invisibility_owned || !status->invisibility_pending)continue;
        uint32_t instance=status->instance_id;
        bool nested=false;
        for(invisibility_notice_t *notice=invisibility_notices;notice;notice=notice->previous)
            if(notice->unit==unit && notice->birth==birth && notice->instance==instance)nested=true;
        if(nested)continue;
        invisibility_notice_t notice={unit,birth,instance,invisibility_notices};
        invisibility_notices=&notice;
        uint32_t generation=S_AttackInvisibilityAcquire(unit);
        invisibility_notices=notice.previous;
        if(!invisibility_live(unit,birth))goto done;
        if(!unit->abilstatus || i>=G_UnitStatusSlotCount(unit)) {
            if(generation)S_AttackInvisibilityRelease(unit,generation);
            goto done;
        }
        status=unit->abilstatus+i;
        if(status->instance_id!=instance || !status->level || !status->invisibility_owned) {
            if(generation)S_AttackInvisibilityRelease(unit,generation);
            if(!invisibility_live(unit,birth))goto done;
            continue;
        }
        status->invisibility_pending=false;
        status->invisibility_attack_generation=generation;
        status->invisibility_suppression_held=generation!=0 &&
            generation==unit->attack_invisibility_generation;
    }
done:
    invisibility_publication_depth--;
}
static void invisibility_publish(edict_t *unit) {
    uint32_t birth=unit->spawn_time;
    invisibility_publication_depth++;
    unit->permanent_invisibility_fade.published=true;
    if(unit->permanent_invisibility_fade.permanent_owned)
        unit->runtime.flags|=UNIT_BALANCE_PERMANENT_INVISIBLE;
    if(unit->permanent_invisibility_fade.temporary_contributions)G_SetEntityHidden(unit,true);
    /* Visibility is published before synchronous TargetLost subscribers. */
    S_UnitTargetLost(unit);
    if(invisibility_live(unit,birth))invisibility_notify(unit);
    invisibility_publication_depth--;
}
static void invisibility_last_release(edict_t *unit) {
    invisibility_remove(unit);
    memset(&unit->permanent_invisibility_fade,0,sizeof(unit->permanent_invisibility_fade));
    unit->runtime.flags&=~UNIT_BALANCE_PERMANENT_INVISIBLE;
    S_UnitTargetAvailable(unit);
}
static void invisibility_clear(edict_t *unit) {

    unit->permanent_invisibility_fade.permanent_owned=false;
    unit->runtime.flags&=~UNIT_BALANCE_PERMANENT_INVISIBLE;
    if(!unit->permanent_invisibility_fade.temporary_contributions)invisibility_last_release(unit);
}
static void invisibility_begin(edict_t *unit, float transition) {
    bool invisible=S_UnitHasInvisibilityState(unit);
    unit->permanent_invisibility_fade.permanent_owned=true;
    if(unit->permanent_invisibility_fade.temporary_contributions) {
        if(unit->permanent_invisibility_fade.published)
            unit->runtime.flags|=UNIT_BALANCE_PERMANENT_INVISIBLE;
        invisibility_notify(unit);return;
    }
    unit->permanent_invisibility_fade.published=transition==0;
    invisibility_remove(unit);
    if(transition==0)unit->runtime.flags|=UNIT_BALANCE_PERMANENT_INVISIBLE;
    else unit->runtime.flags&=~UNIT_BALANCE_PERMANENT_INVISIBLE;
    unit->permanent_invisibility_fade.origin=G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
    unit->permanent_invisibility_fade.slope=transition ? wc3_recip(transition) : 0;
    if(transition>0)invisibility_schedule(unit,&unit->permanent_invisibility_fade.origin);
    else if(!invisible)S_UnitTargetLost(unit); /* Publish before synchronous subscribers. */
}
void S_TemporaryInvisibilityAcquire(edict_t *unit,heroabilitystatus_t *status,float transition) {
    if(!unit || !status || status->invisibility_owned)return;
    bool first=!unit->permanent_invisibility_fade.permanent_owned &&
        !unit->permanent_invisibility_fade.temporary_contributions;
    status->invisibility_owned=true;status->invisibility_pending=true;
    unit->permanent_invisibility_fade.temporary_contributions++;
    temporary_expiry_forget(unit);temporary_expiry_insert(unit);
    if(!first) {
        if(unit->permanent_invisibility_fade.published)G_SetEntityHidden(unit,true);
        invisibility_notify(unit);return;
    }
    unit->permanent_invisibility_fade.origin=G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
    unit->permanent_invisibility_fade.slope=transition ? wc3_recip(transition) : 0;
    if(transition>0)invisibility_schedule(unit,&unit->permanent_invisibility_fade.origin);
    else if(transition==0)invisibility_publish(unit);
    /* Negative/nonfinite authored transitions are not covered by299; no
     * invented immediate completion or permanent-passive suspend policy. */
}
void S_TemporaryInvisibilityRelease(edict_t *unit,heroabilitystatus_t *status) {
    if(!unit || !status || !status->invisibility_owned)return;
    uint32_t birth=unit->spawn_time;
    if(status->class_code==MAKEFOURCC('B','i','n','v'))
        S_DetachStatusAbilityAvailability(unit,MAKEFOURCC('B','i','n','v'),MAKEFOURCC('B','i','n','v'));
    /* Inspect presence while this contribution still explains RF_HIDDEN.
     * Loaded, mined, trained, reviving and world-retired units keep their
     * independent hiding owner when the temporary contribution leaves. */
    bool unhide=G_UnitHasWorldPresence(unit) && !unit->training && !G_HeroIsReviving(unit);
    bool held=status->invisibility_suppression_held;
    uint32_t generation=status->invisibility_attack_generation;
    temporary_expiry_forget(unit);status->class_timer.active=false;
    status->class_clock_depth=0;status->class_timer_remaining=0;
    status->invisibility_attack_generation=0;
    status->invisibility_owned=false;status->invisibility_pending=false;
    status->invisibility_suppression_held=false;
    if(unit->permanent_invisibility_fade.temporary_contributions)
        unit->permanent_invisibility_fade.temporary_contributions--;
    temporary_expiry_insert(unit);
    if(!unit->permanent_invisibility_fade.temporary_contributions && unhide &&
       !S_UnitHasTemporaryInvisibility(unit,status) && !invisibility_intrinsic_render_owner(unit))
        G_SetEntityHidden(unit,false);
    if(!unit->permanent_invisibility_fade.temporary_contributions &&
       !unit->permanent_invisibility_fade.permanent_owned)invisibility_last_release(unit);
    if(held && invisibility_live(unit,birth))S_AttackInvisibilityRelease(unit,generation);
}

/*62f050/62f280 find the concrete Binv class.532a80 may replace the
 * presented buff ID without replacing that object or its contribution. */
bool S_ApplyTemporaryInvisibility(edict_t *unit,edict_t *source,uint32_t code,
        uint32_t rank,cstring_t buff,float duration,float transition) {
    uint32_t const concrete=MAKEFOURCC('B','i','n','v');
    if(!unit || !unit->inuse || !isfinite(duration))return false;
    bool const authored=buff && strlen(buff)>=4;
    heroabilitystatus_t *status=unit_findstatusclass(unit,concrete);
    if(status && status->invisibility_owned) {
        if(authored && status->code!=*((uint32_t const *)buff)) {
            bool other_art=false;
            FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
                heroabilitystatus_t const *other=unit->abilstatus+i;
                if(other!=status && other->level && other->code==status->code &&
                   (other->buff_flags&WC3_STATUS_BUFF_TARGET_ART))other_art=true;
            }
            if((status->buff_flags&WC3_STATUS_BUFF_TARGET_ART) && !other_art)
                G_DestroyStatusEffectTarget(status->code,unit);
            status->code=*((uint32_t const *)buff);
            unit_status_enabletargetart(unit,status,"origin");
        }
        status->data=code;status->source_ability=code;status->rank=rank;
        status->source=source;status->source_spawn_time=source ? source->spawn_time : 0;
        S_TemporaryInvisibilitySetDuration(unit,status,duration,true);
        return true;
    }
    status_application_t app={.buff=authored ? buff : "Binv",.class_code=concrete,
        .level=rank,.data=code,.source_ability=code,.source=source,.rank=rank};
    status=unit_applystatus(unit,&app);
    if(!status)return false;
    S_TemporaryInvisibilitySetDuration(unit,status,duration,false);
    unit_status_enabletargetart(unit,status,"origin");
    uint32_t const birth=unit->spawn_time,instance=status->instance_id;
    /*63ce40 starts the fade before48b4c0 admits the owner to the clock walk.
     * Reentrant publication cannot release a clock it has not inherited yet. */
    status->class_clock_depth=-1;
    S_TemporaryInvisibilityAcquire(unit,status,transition);
    if(invisibility_live(unit,birth)) {
        status=unit_findstatusclass(unit,concrete);
        if(status && status->instance_id==instance && status->invisibility_owned) {
            /*48b4c0 admits availability before inheriting private clocks.
             * The ordered owner uses class identity, not displayed BuffID. */
            S_AttachStatusAbilityAvailability(unit,concrete,concrete);
            temporary_expiry_forget(unit);
            status->class_clock_depth=0;
            temporary_invisibility_clock_adjust(status,unit->ability_clock_depth);
            temporary_expiry_insert(unit);
        }
    }
    return true; /* Acquisition can synchronously retire the unit/status. */
}

static void temporary_expiry_fire(void) {
    edict_t *unit=temporary_expiry_heap[0];
    uint32_t birth=unit->spawn_time;
    heroabilitystatus_t *status=temporary_expiry_next(unit);
    temporary_expiry_forget(unit);status->class_timer.active=false;
    unit_removestatus(unit,status,STATUS_REMOVE_EXPIRE);
    if(invisibility_live(unit,birth))temporary_expiry_insert(unit);
}

/* The attached temporary buff has its own expiration request, independent of
 * the common fade and of script/order suspension. Heap membership is derived. */
BZ_ABILITY_PROC(CBuffInvisibility) {
    switch(msg) {
    case A_UNIT_EVENT_MASK:return UNIT_MESSAGE_SUBSCRIPTIONS(A_UNIT_REMOVING,A_UNIT_REMOVE,A_UNIT_ACTION);
    case A_UNIT_ACTION: {
        /*643fb0 removes notified Binv owners. Native flag100 is the pending
         * publication guard, cleared only after suppression/recovery returns. */
        heroabilitystatus_t *status=ent ? unit_findstatusclass(ent,MAKEFOURCC('B','i','n','v')) : NULL;
        if(status && status->invisibility_owned && !status->invisibility_pending)unit_expirestatus(ent,status);
        return true;
    }
    case A_UNIT_REMOVING:case A_UNIT_REMOVE:
        if(ent)temporary_expiry_forget(ent);
        return true;
    case A_TIMERS_RESET:
        temporary_expiry_count=0;memset(temporary_expiry_positions,0,sizeof(temporary_expiry_positions));return true;
    case A_TIMERS_REBUILD:
        temporary_expiry_count=0;memset(temporary_expiry_positions,0,sizeof(temporary_expiry_positions));
        FOR_LOOP(i,globals.num_edicts)temporary_expiry_insert(g_edicts+i);
        return true;
    case A_PRIMARY_TIMER_NEXT:
        if(!temporary_expiry_count || !call || !call->primary_timer)return false;
        *call->primary_timer=(abilityTimerRequest_t){temporary_expiry_next(temporary_expiry_heap[0])->class_timer.deadline,
            temporary_expiry_next(temporary_expiry_heap[0])->class_timer.sequence,CBuffInvisibility};return true;
    case A_PRIMARY_TIMER_FIRE:
        if(temporary_expiry_count)temporary_expiry_fire();
        return true;
    case A_PRIMARY_TIMER_REBASE:
        FOR_LOOP(i,temporary_expiry_count)FOR_LOOP(j,G_UnitStatusSlotCount(temporary_expiry_heap[i])) {
            heroabilitystatus_t *status=temporary_expiry_heap[i]->abilstatus+j;
            if(status->invisibility_owned && status->class_timer.active) {
                status->class_timer.deadline.time=wc3_sub(status->class_timer.deadline.time,call->clock_span);
                status->class_timer.deadline.epoch++;
            }
        }
        return true;
    case A_PRIMARY_TIMER:
        if(!level.scheduled_frame)while(temporary_expiry_count) {
            wc3Clock_t now=G_TimerQueryClock(NULL);
            if(temporary_expiry_next(temporary_expiry_heap[0])->class_timer.deadline.time>now.time)break;
            temporary_expiry_fire();
        }
        return true;
    default:return false;
    }
}

bool S_ValidateInvisibilityOwners(void) {
    if(S_InvisibilityPublicationPending())return false;
    FOR_LOOP(n,globals.num_edicts) {
        edict_t const *unit=g_edicts+n;
        if(!unit->inuse)continue;
        if(unit->ability_clock_depth<0 || unit->ability_clock_depth!=unit->ability_pause_depth)return false;
        uint32_t contributions=0,held=0,binv_owners=0,binv_nodes=0;
        for(unitAbilityDisable_t const *node=unit->ability_disables;node;node=node->next)
            if(node->class_code==MAKEFOURCC('B','i','n','v')) {
                if(node->code!=node->class_code || !node->linked || node->morph_owned ||
                   node->exclusive_owned || node->exclusive_inventory || node->ethereal_initialized ||
                   node->ethereal_owned || node->burrow_initialized || node->burrow_owned)return false;
                binv_nodes++;
            }
        FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
            if(!unit->abilstatus)return false;
            heroabilitystatus_t const *status=unit->abilstatus+i;
            if(*(uint8_t const *)&status->invisibility_owned>1 ||
               *(uint8_t const *)&status->invisibility_pending>1 ||
               *(uint8_t const *)&status->invisibility_suppression_held>1)return false;
            if(status->invisibility_owned) {
                if(status->class_code==MAKEFOURCC('B','i','n','v'))binv_owners++;
                if(!status->level || !status->instance_id ||
                   !S_UnitStatusIsTemporaryInvisibility(status) ||
                   (unit->permanent_invisibility_fade.published && status->invisibility_pending))return false;
                abilityPrimaryTimer_t const *expiry=&status->class_timer;
                if((status->class_code && status->class_code!=MAKEFOURCC('B','i','n','v')) ||
                   status->class_clock_depth<0 || !isfinite(status->class_timer_remaining) ||
                   status->class_timer_remaining<0 ||
                   (status->class_code==MAKEFOURCC('B','i','n','v') &&
                    status->class_clock_depth!=unit->ability_clock_depth) ||
                   (!status->class_clock_depth && status->class_timer_remaining) ||
                   *(uint8_t const *)&expiry->active>1)return false;
                if(expiry->active && (status->timestamp || !isfinite(expiry->deadline.time) ||
                   !isfinite(expiry->deadline.span) || expiry->deadline.span<=0 ||
                   !expiry->sequence || expiry->sequence>level.timer_sequence))return false;
                if(!expiry->active && status->duration_ms && !status->timestamp &&
                   !status->class_clock_depth)return false;
                contributions++;
            } else if(status->invisibility_pending || status->invisibility_suppression_held ||
                      status->invisibility_attack_generation)return false;
            if(status->invisibility_suppression_held) {
                if(status->invisibility_pending || !status->invisibility_attack_generation)return false;
                if(status->invisibility_attack_generation==unit->attack_invisibility_generation)held++;
            }
        }
        if(binv_nodes!=binv_owners || contributions!=unit->permanent_invisibility_fade.temporary_contributions ||
           held!=unit->attack_invisibility_suppression)return false;
        if(*(uint8_t const *)&unit->permanent_invisibility_fade.permanent_owned>1 ||
           *(uint8_t const *)&unit->permanent_invisibility_fade.published>1 ||
           *(uint8_t const *)&unit->permanent_invisibility_fade.request.active>1)return false;
        bool owned=contributions || unit->permanent_invisibility_fade.permanent_owned;
        abilityPrimaryTimer_t const *request=&unit->permanent_invisibility_fade.request;
        if((request->active || unit->permanent_invisibility_fade.published) && !owned)return false;
        if(owned && (!isfinite(unit->permanent_invisibility_fade.slope) ||
           !isfinite(unit->permanent_invisibility_fade.origin.time) ||
           !isfinite(unit->permanent_invisibility_fade.origin.span) ||
           unit->permanent_invisibility_fade.origin.span<=0))return false;
        if(request->active && (!isfinite(request->deadline.time) ||
           !isfinite(request->deadline.span) || request->deadline.span<=0 ||
           !request->sequence || request->sequence>level.timer_sequence ||
           unit->permanent_invisibility_fade.slope<=0 ||
           unit->permanent_invisibility_fade.published))return false;
    }
    return true;
}

void S_PermanentInvisibilityInitialize(edict_t *unit) {
    if(!unit)return;
    float transition=permanent_invisibility_transition(unit);
    if(transition<0 || S_AbilityExecutionDisabled(unit,ID_APIV)) {invisibility_clear(unit);return;}
    invisibility_begin(unit,transition);
}

/*670950 invokes68b780 after rebind, before new ability attachment. A
 * retained enabled contribution completes its fade; a destination-only passive
 * must keep the authored fade begun by its actual initialization. */
void S_RefreshRetainedPermanentInvisibility(edict_t *unit,bool retained) {
    if(retained && unit && G_ActorHasAbilityCode(unit,ID_APIV) && !S_AbilityExecutionDisabled(unit,ID_APIV))
        invisibility_begin(unit,0);
}

/* Own Permanent Invisibility's publication and exact scalar listener queue. */
BZ_ABILITY_PROC(CAbilityPermanentInvisibility) {
    switch(msg) {
    case A_UNIT_TYPE_INIT:
        if(ent || !call)return UNIT_INIT_UNKNOWN;
        return G_UnitHasAuthoredAbility(call->unit_type,ID_APIV) ? UNIT_INIT_RUN : UNIT_INIT_SKIP_TRUE;
    case A_UNIT_EVENT_MASK:
        return UNIT_MESSAGE_SUBSCRIPTIONS(A_UNIT_INIT,A_ENABLE,A_LEVEL_CHANGED,A_DISABLE,A_UNIT_REMOVING,A_UNIT_REMOVE,A_UNIT_SUSPEND,A_UNIT_RESUME,A_UNIT_ACTION);
    case A_UNIT_ACTION:
        /*643f60 uses the execution count, not the unit's physical pause. */
        if(ent && call && call->item && !S_AbilityExecutionDisabled(ent,call->item->code))
            S_PermanentInvisibilityReveal(ent);
        return true;
    case A_UNIT_INIT:case A_ENABLE:case A_LEVEL_CHANGED:case A_UNIT_RESUME:
        S_PermanentInvisibilityInitialize(ent);return true;
    case A_DISABLE:case A_UNIT_REMOVING:case A_UNIT_REMOVE:case A_UNIT_SUSPEND:
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
            invisibility_publish(unit);
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
    if(!unit || !unit->permanent_invisibility_fade.permanent_owned)return;
    uint32_t const birth=unit->spawn_time;
    /*643f60 releases this contribution before reacquiring authored Dur.
     * Another contribution retains the shared fade across this inverse. */
    invisibility_clear(unit);
    if(!invisibility_live(unit,birth))return;
    float transition=permanent_invisibility_transition(unit);
    if(transition>=0)invisibility_begin(unit,transition);
}

/* RF_HIDDEN is also used for cargo, mines, training and revival.  This narrow
 * predicate identifies only states that true sight is allowed to reveal in a
 * per-client snapshot. */
bool S_UnitUsesInvisibilityRenderFlag(edict_t const *unit) {
	if (!unit || !unit->inuse || !(unit->s.renderfx & RF_HIDDEN)) return false;
	if (S_UnitHasTemporaryInvisibility(unit, NULL)) return true;
	return invisibility_intrinsic_render_owner(unit);
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

	/* Sentry wards and Sentinel owls are independent detector entities, not
	 * combat monsters; their respective lifecycles are authoritative. */
	FILTER_EDICTS(detector, detector != unit &&
	              (ward_is_sentry(detector) || sentinel_owl_active(detector) || S_SpellIsAliveTarget(detector)) &&
	              detector_shared_with_player(detector, player) &&
	              S_SpellIsEnemy(detector, (edict_t *)unit)) {
		float range = (ward_is_sentry(detector) || detector->think == sentinel_owl_think) ?
			detector->wait : unit_detector_range(detector);
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
	if (G_UnitIsForcedVisibleToPlayer(unit, player)) return false;
	if (unit->s.player < MAX_PLAYERS && G_FowPlayersShareVision(player, unit->s.player)) return false;
	if (!S_PermanentInvisibilityActive(unit) && !S_GhostActive(unit) && !S_ShadowMeldActive(unit) && !S_UnitUsesInvisibilityRenderFlag(unit)) return false;
	return !S_UnitIsDetectedByPlayer(unit, player);
}

/* Script-hidden units are absent from ordinary targeting. RF_HIDDEN also backs
 * player-local invisibility, so retain its detector-aware target policy. */
bool S_UnitIsHiddenFromPlayer(edict_t const *unit, uint32_t player) {
	if (!unit || !unit->inuse || unit->script_world_hidden) return true;
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

#define ID_AESN MAKEFOURCC('A', 'e', 's', 'n') /* Sentinel (Huntress) */
#define SENTINEL_OWL_MODEL "Units\\NightElf\\Owl\\Owl.mdx"

/* Do not keep the caster as an owner: an owl belongs to its player and remains
 * stationed even if the Huntress dies.  The target incarnation and health
 * recorded at placement are the tree's attachment/removal contract. */
static bool sentinel_owl_active(edict_t const *owl) {
    edict_t *tree;
    if (!owl || !owl->inuse || owl->summon_ability != ID_AESN ||
        owl->think != sentinel_owl_think ||
        (owl->freetime && G_Time() >= owl->freetime)) return false;
    tree = owl->goalentity;
    return tree && tree->inuse && tree->spawn_time == owl->damage &&
           tree->targtype == TARG_TREE && G_IsDestructable(tree) &&
           !tree->destructable->dead && tree->health.value > 0.0f &&
           tree->health.value >= owl->mana.value;
}

/* The thinker is saved as an edict callback.  Its runtime sight is consumed by
 * g_fow.c's normal owner/shared-vision pass, not by a private fog modifier. */
void sentinel_owl_think(edict_t *owl) {
    animation_t const *anim;
    if (!sentinel_owl_active(owl)) {
        G_FreeEdict(owl);
        return;
    }
    /* An owl is a lightweight entity without a unit currentmove.  Advance
     * its stand sequence directly, not through M_MoveFrame (which expects an
     * allocated unit move and invokes its callbacks). */
    anim = owl->animation;
    if (anim && anim->interval[1] > anim->interval[0]) {
        if (owl->s.frame < anim->interval[0] || owl->s.frame + 1 >= anim->interval[1])
            owl->s.frame = anim->interval[0];
        else
            owl->s.frame++;
    }
}

/* Dispel is an independent removal path; a dispelled owl cannot restore the
 * spent Sentinel charge to its Huntress. */
uint32_t S_SentinelDispelAt(vec2_t const *point, float radius) {
    uint32_t removed = 0;
    if (!point || radius < 0.0f) return 0;
    FILTER_EDICTS(owl, owl->inuse && owl->summon_ability == ID_AESN &&
                  owl->think == sentinel_owl_think &&
                  Vector2_distance(&owl->s.origin2, point) <= radius) {
        G_FreeEdict(owl);
        removed++;
    }
    return removed;
}

/* The stock Sentinel owl uses AbilityData Area for the revealed/detected
 * radius and Dur for lifetime.  The caster must target a living tree, and
 * the one-shot command is removed only after the owl is created. */
BZ_ABILITY_PROC(CAbilitySentinel) {
	spellTarget_t const *target;
	edict_t *tree;
    uint32_t code = call && call->item ? call->item->code : ID_AESN;
    uint32_t level;
    float radius, duration;
    edict_t *owl;

    switch (msg) {
	case A_SPELL_TARGET_MODE:
		if (call) ((abilityCall_t *)call)->spell_entity_target_mode = true;
		return true;
	case A_SPELL_APPROACH_RANGE:
		/* This query carries the range in the call payload, not a target. */
		return true;
	default:
		break;
	}
	target = call ? call->target : NULL;
	tree = target && target->type == SPELL_TARGET_UNIT ? target->entity :
		target && target->type == SPELL_TARGET_POINT ? sentinel_tree_at_point(&target->point) : NULL;
	switch (msg) {
    case A_VALIDATE:
        return ent && tree && G_UnitAbilityLevel(ent, code) &&
               G_UnitAbilityResearchAvailable(ent, code) &&
               tree->targtype == TARG_TREE && G_DestructableIsAttackable(tree) &&
               S_SpellAllowsTarget(code, ent, tree);
    case A_EXECUTE:
        if (!ent || !tree || !G_UnitAbilityLevel(ent, code) ||
            !G_UnitAbilityResearchAvailable(ent, code) ||
            tree->targtype != TARG_TREE || !G_DestructableIsAttackable(tree)) return false;
        level = S_SpellLevel(ent, code);
        /* Aesn's object-data fields are ability-specific: DataB is the
         * hovering owl's sight radius and DataC is its hovering height.
         * Area and Dur are zero in stock Warcraft data. */
        radius = S_SpellData(code, level, 2);
        float hover_height = S_SpellData(code, level, 3);
        if (hover_height <= 0.0f) hover_height = 64.0f;
        duration = S_SpellDuration(code, level, false);
        if (radius <= 0.0f) return false;
        owl = G_Spawn();
        if (!owl) return false;
        owl->summon_ability = ID_AESN;
        owl->s.player = ent->s.player;
        owl->s.flags |= EF_NOT_SELECTABLE;
        owl->s.origin = tree->s.origin;
        owl->s.origin2 = tree->s.origin2;
        owl->s.origin.z += hover_height;
        owl->s.model = G_RegisterModel(SENTINEL_OWL_MODEL);
        owl->s.scale = 1.0f;
        owl->health.value = owl->health.max_value = 1.0f;
        owl->mana.value = tree->health.value; /* original tree HP */
        owl->damage = tree->spawn_time; /* target incarnation */
        owl->goalentity = tree;
        owl->wait = radius; /* true-sight radius */
        owl->runtime.sight_radius.day = radius;
        owl->runtime.sight_radius.night = radius;
        owl->freetime = duration > 0.0f ?
            G_Time() + (uint32_t)(duration * 1000.0f) : 0;
        owl->think = sentinel_owl_think;
        G_SetUnitAnimation(owl, "stand");
        /* Publish the persistent model at the tree's fixed world position. */
        gi.LinkEntity(owl);
        if (!G_ActorRemoveSkill(ent, code)) {
            G_FreeEdict(owl);
            return false;
        }
        return true;
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
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

/* Rune of the Watcher is a power-up form of the Sentry Ward ability.  Unlike
 * Aeye/AIsw, the created Watcher Ward has no timed life.  Use the rune's own
 * UnitID (rather than the ordinary Sentry Ward's) and retain its ability alias
 * for player-relative true sight and save/load. */
BZ_ABILITY_PROC(CAbilityItemWatcherWard) {
    uint32_t code = call && call->item ? call->item->code : 0;
    uint32_t unit_id = code ? S_SpellUnitId(code, 1) : 0;
    edict_t *ward;
    if (msg != A_ITEM_USE || !ent || !unit_id) return false;
    ward = S_SummonAbilityAt(ent, code, unit_id, &ent->s.origin2, 0.0f);
    if (!ward) return false;
    ward->s.renderfx |= RF_HIDDEN;
    ward->wait = S_SpellRange(ID_ADT1, 1);
    G_SpawnAbilityEffectAtPoint(code, WC3_EFFECT_EFFECT, 0, &ent->s.origin2, true);
    return true;
}
