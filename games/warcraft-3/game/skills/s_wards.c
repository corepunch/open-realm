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
	ward->s.renderfx |= RF_HIDDEN;
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
		G_SetUnitFlag(mine, UNIT_FLAG_ANIMATION_OVERRIDE, true);
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
	G_AddChannel(thinker);
	G_GetChannel(thinker)->owner_spawn_time = mine->spawn_time;
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
		uint32_t level = G_UnitAbilityLevel(detector, abilities[i]);
		if (abilities[i] == magic_sentry) {
			gameClient_t *owner = G_GetPlayerClientByNumber(detector->s.player);
			/* Towers carry Adts in their unit rows before research; Rhse is
			 * what activates its true-sight radius. The owning player's allied
			 * vision recipients then share the detector through the usual path. */
			if (!owner || G_GetPlayerTechResearchedLevel(owner, magic_sentry_research) <= 0) continue;
		}
		if (level) range = MAX(range, S_SpellRange(abilities[i], level));
	}
	if (G_InventoryCanUseItems(detector)) {
		FOR_LOOP(slot, G_InventoryCapacity(detector)) {
			edict_t *item = G_InventoryItem(detector, slot);
			cstring_t carried = item ? G_ItemAbilityList(item) : NULL;
			if (!carried) continue;
			PARSE_LIST(carried, name, parse_segment) {
				uint32_t const alias = strlen(name) == 4 ? FS_SLKKey(name) : 0;
				bool detector_ability = false;
				if (!alias) continue;
				FOR_LOOP(i, sizeof(abilities) / sizeof(*abilities))
					if (alias == abilities[i] || G_AbilityCode(alias) == abilities[i]) {
						detector_ability = true;
						break;
					}
				if (detector_ability) range = MAX(range, S_SpellRange(alias, 1));
			}
		}
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
	uint32_t level = unit ? G_UnitAbilityLevel(unit, ID_APIV) : 0;
	return level ? S_SpellDuration(ID_APIV, level, false) : -1.0f;
}

bool S_GhostActive(edict_t const *unit) {
	return unit && unit->inuse && (unit->runtime.flags & UNIT_BALANCE_GHOST_INVISIBLE);
}

bool S_PermanentInvisibilityActive(edict_t const *unit) {
	return unit && unit->inuse && (unit->runtime.flags & UNIT_BALANCE_PERMANENT_INVISIBLE) &&
		(!unit->vision || G_Time() >= unit->vision->reveal_until);
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
	FOR_LOOP(i, unit->unit_state ? MAX_UNIT_STATUSES : 0)
		if (unit->unit_state->statuses + i != except &&
		    S_UnitStatusIsTemporaryInvisibility(unit->unit_state->statuses + i)) return true;
	return false;
}

bool S_UnitHasInvisibilityState(edict_t const *unit) {
	return S_PermanentInvisibilityActive(unit) || S_GhostActive(unit) ||
	       S_ShadowMeldActive(unit) || S_UnitUsesInvisibilityRenderFlag(unit);
}

void S_PermanentInvisibilityInitialize(edict_t *unit) {
    float transition;
    if (!unit || !G_UnitAbilityLevel(unit, ID_APIV)) {
        if (unit) {
            unit->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
            if (unit->vision) unit->vision->reveal_until = 0;
        }
        return;
	}
	transition = permanent_invisibility_transition(unit);
	/* Negative transition is the authored opt-out: this unit never enters invisibility. */
	if (transition < 0.0f) {
		unit->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
		if (unit->vision) unit->vision->reveal_until = 0;
		return;
	}
	unit->runtime.flags |= UNIT_BALANCE_PERMANENT_INVISIBLE;
    G_InitVisibility(unit);
    unit->vision->reveal_until =
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
            if (ent->vision) ent->vision->reveal_until = 0;
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
		if (unit->vision) unit->vision->reveal_until = 0;
		return;
	}
	G_InitVisibility(unit);
	unit->vision->reveal_until =
		G_Time() + (uint32_t)(MAX(0.0f, transition) * 1000.0f);
}

/* RF_HIDDEN is also used for cargo, mines, training and revival.  This narrow
 * predicate identifies only states that true sight is allowed to reveal in a
 * per-client snapshot. */
bool S_UnitUsesInvisibilityRenderFlag(edict_t const *unit) {
	uint32_t summon;
	if (!unit || !unit->inuse || !(unit->s.renderfx & RF_HIDDEN)) return false;
	if (S_UnitHasTemporaryInvisibility(unit, NULL)) return true;
	if (S_UnitIsSubmerged(unit)) return true;
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
	/* Dust of Appearance's Bdet is attached to each affected unit. Its payload
	 * records the player who used the item; shared-vision allies inherit it. */
	FOR_LOOP(i, unit->unit_state ? MAX_UNIT_STATUSES : 0) {
		heroabilitystatus_t const *status = unit->unit_state->statuses + i;
		uint32_t const buff = MAKEFOURCC('B', 'd', 'e', 't');
		if (status->level && status->code == buff && status->data > 0 &&
		    status->data <= MAX_PLAYERS &&
		    (!status->timestamp || G_Time() < status->timestamp) &&
		    G_FowPlayersShareVision(player, status->data - 1)) return true;
	}

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
           !G_GetDestructable(tree)->dead && tree->health.value > 0.0f &&
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
	ward->s.renderfx |= RF_HIDDEN;
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
