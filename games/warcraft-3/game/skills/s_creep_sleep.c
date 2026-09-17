/*
 * s_creep_sleep.c — Warcraft III natural neutral-creep sleep.
 *
 * Natural creep sleep is separate from the Dreadlord Sleep spell (AUsl/BUsL).
 * UnitData.canSleep supplies the authored night-sleep capability; the runtime
 * flag may be changed by UnitAddSleep.  Only Neutral Hostile automatically
 * enters natural sleep at night.  Sleeping units use their authored Sleep MDX
 * sequence and wake at dawn, on positive damage, or through UnitWakeUp.
 */
#include "s_skills.h"

#define BZ_CREEP_SLEEP MAKEFOURCC('A','C','s','p') // rawcode; TFT natural sleep; used by JASS dispatch
#define BZ_CREEP_SLEEP_ART \
    "Abilities\\Spells\\Other\\CreepSleep\\CreepSleepTarget.mdx"

/* common.j declares PLAYER_STATE_NO_CREEP_SLEEP as playerstate 25.  The
 * WC3-local constant lives in g_local.h instead of widening common/shared.h. */
static void creep_sleep_think(LPEDICT self);
static umove_t creep_sleep_move = { .animation = "sleep", .think = creep_sleep_think, .proc = CAbilityCreepSleep };

/* Match only the overlay owned by this unit, leaving unrelated target effects untouched. */
static BOOL is_creep_sleep_overlay(LPCEDICT effect, LPCEDICT unit) {
    return effect && effect->inuse && effect->owner == unit && effect->goalentity == unit &&
           effect->summon_ability == BZ_CREEP_SLEEP;
}

/* Destroy every natural-sleep overlay before the unit leaves its sleep move. */
static void remove_creep_sleep_overlay(LPEDICT unit) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT effect = g_edicts + i;
        if (is_creep_sleep_overlay(effect, unit))
            G_DestroyEffect(effect);
    }
}

/* Spawn ACsp's target art at overhead. Some retail data sets do not expose
 * the hidden ability's TargetArt through the loaded Func metadata, so preserve
 * data/map overrides first and otherwise use Warcraft's canonical sleep art. */
static void add_creep_sleep_overlay(LPEDICT unit, DWORD code) {
    LPCSTR art = G_AbilityEffectArt(code, WC3_EFFECT_TARGET, 0);
    LPEDICT effect;

    if (art && *art) {
        effect = G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, unit, "overhead", false);
    } else {
        fprintf(stderr, "WC3 CreepSleep: %.4s TargetArt unavailable in map data; selected canonical sleep art for unit %u\n", (LPCSTR)&code, unit->s.number);
        effect = G_SpawnModelEffect(BZ_CREEP_SLEEP_ART, NULL, unit, "overhead", false);
    }
    if (!effect) {
        fprintf(stderr, "WC3 CreepSleep: failed to spawn %.4s overlay for unit %u\n", (LPCSTR)&code, unit->s.number);
        return;
    }
    effect->owner = unit;
    effect->summon_ability = BZ_CREEP_SLEEP;
}

/* Restrict automatic sleep to authored neutral-creep candidates. */
static BOOL unit_is_neutral_sleep_candidate(LPCEDICT unit) {
    return unit && unit->s.player >= PLAYER_NEUTRAL_AGGRESSIVE &&
           unit->s.player < MAX_PLAYERS;
}

/* Read the authoritative player-state switch that disables natural sleep. */
static BOOL neutral_hostile_sleep_disabled(void) {
    if (!game.clients || PLAYER_NEUTRAL_AGGRESSIVE >= game.max_clients)
        return false;
    return game.clients[PLAYER_NEUTRAL_AGGRESSIVE]
               .ps.stats[WC3_PLAYERSTATE_NO_CREEP_SLEEP] != 0;
}

/* Report whether a unit retains the runtime permission to enter natural sleep. */
BOOL G_UnitCanSleep(LPCEDICT unit) {
    return unit_is_neutral_sleep_candidate(unit) && unit->sleep.can_sleep;
}

/* Distinguish the authored creep sleep move from other animation moves. */
BOOL G_UnitIsSleeping(LPCEDICT unit) {
    return unit && unit->sleep.sleeping && unit->currentmove == &creep_sleep_move;
}

/* Clear natural sleep and remove its persistent target presentation effect. */
static void creep_sleep_leave(LPEDICT unit) {
    if (!unit || !unit->sleep.sleeping)
        return;
    unit->sleep.sleeping = false;
    remove_creep_sleep_overlay(unit);
}

/* Leave natural sleep and restore the normal stand move when the unit is alive. */
static void creep_sleep_wake(LPEDICT unit) {
    if (!G_UnitIsSleeping(unit))
        return;
    creep_sleep_leave(unit);
    if (!M_IsDead(unit))
        unit_stand(unit);
}

/* Enter natural sleep only for idle Neutral Hostile units during nighttime. */
static BOOL creep_sleep_enter(LPEDICT unit, DWORD code) {
    if (!unit || unit->s.player != PLAYER_NEUTRAL_AGGRESSIVE ||
        !G_UnitCanSleep(unit) || G_UnitIsSleeping(unit) || M_IsDead(unit) ||
        unit_affectingcombat(unit) || !G_IsNight() || neutral_hostile_sleep_disabled()) {
        return false;
    }

    unit_setmove(unit, &creep_sleep_move);
    unit->sleep.sleeping = true;
    add_creep_sleep_overlay(unit, code);
    return true;
}

/* Wake sleeping creeps as soon as the simulation reaches daytime. */
static void creep_sleep_think(LPEDICT self) {
    if (!G_IsNight())
        creep_sleep_wake(self);
}

/* The TFT parent owns shared sleep exit paths. Asla's independent activation policies
 * (Sleep Once/Allow On Any Player Slot) remain unresolved and are not registered. */
BZ_ABILITY_PROC(CAbilitySleepAlways) {
    switch (msg) {
    case A_MOVE_LEAVE:
    case A_UNIT_REMOVE: creep_sleep_leave(ent); return true;
    case A_DAMAGED:
    case A_CANCEL: creep_sleep_wake(ent); return true;
    case A_NO_ACQUIRE: return G_UnitIsSleeping(ent);
    default: return false;
    }
}

/* ACsp is implicit in UnitData.canSleep; it consumes lifecycle messages even when
 * the unit has no explicit ACsp command-card entry. The clock remains game-owned. */
BZ_ABILITY_PROC(CAbilityCreepSleep) {
    switch (msg) {
    case A_UNIT_INIT:
        ent->sleep.can_sleep = ent->data.UnitData->canSleep;
        if (!ent->sleep.can_sleep || !G_UnitIsSleeping(ent)) creep_sleep_leave(ent);
        return true;
    case A_IDLE: return call && call->item && creep_sleep_enter(ent, call->item->code);
    case A_ENABLE:
    case A_DISABLE:
        if (!unit_is_neutral_sleep_candidate(ent)) return false;
        ent->sleep.can_sleep = msg == A_ENABLE;
        if (!ent->sleep.can_sleep) creep_sleep_wake(ent);
        return true;
    default: return CAbilitySleepAlways(ent, msg, call);
    }
}

/* Script natives use the same concrete registration as unit lifecycle dispatch. */
static void creep_sleep_message(LPEDICT unit, abilityMsg_t msg) {
    abilityitem_t item = S_AbilityItem(BZ_CREEP_SLEEP);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    S_AbilityMessage(unit, msg, &call);
}

void G_UnitSetCanSleep(LPEDICT unit, BOOL enabled) { creep_sleep_message(unit, enabled ? A_ENABLE : A_DISABLE); }
void G_UnitWakeUp(LPEDICT unit) { creep_sleep_message(unit, A_CANCEL); }
