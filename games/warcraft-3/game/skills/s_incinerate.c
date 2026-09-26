#include "s_skills.h"

#define ID_INCINERATE MAKEFOURCC('A','N','i','c')
#define ID_INCINERATE_ARROW MAKEFOURCC('A','N','i','a')

/* TFT stores full/outer damage in DataB/D and radii in DataC/E; Area is deliberately zero. */
void incinerate_explode_think(edict_t * ent) {
    uint32_t code = ent->class_id, rank = ent->resources;
    edict_t * source = ent->owner;
    float full = S_SpellData(code, rank, 3), outer = S_SpellData(code, rank, 5);
    if (G_Time() < ent->freetime) return;
    if (source && source->inuse && source->spawn_time == ent->channel.owner_spawn_time) {
        FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellAllowsTarget(code, source, target)) {
            float dist = Vector2_distance(&target->s.origin2, &ent->s.origin2);
            float damage = dist <= full ? S_SpellData(code, rank, 2) :
                           dist <= outer ? S_SpellData(code, rank, 4) : 0.0f;
            if (damage > 0.0f) S_SpellDamage(target, source, (int)damage);
        }
    }
    G_FreeEdict(ent);
}

/* Attach the mark before either component of the attack can kill; the victim's death owns the explosion. */
void S_IncinerateOnHit(edict_t * attacker, edict_t * target) {
    abilityAliasRef_t ability = S_ResolveAbilityAlias(attacker, ID_INCINERATE_ARROW);
    heroabilitystatus_t *slot;
    cstring_t buff;
    uint32_t stacks, code, rank;
    if (!ability.alias) ability = S_ResolveAbilityAlias(attacker, ID_INCINERATE);
    code = ability.alias; rank = ability.level;
    if (!code || !rank || !target || !S_SpellAllowsTarget(code, attacker, target)) return;
    buff = G_AbilityLevel(code, rank)->buffID;
    /* Map overrides may omit BuffID; both Incinerate variants use BNic in the TFT profiles. */
    if (!buff || strlen(buff) < 4) buff = "BNic";
    stacks = G_UnitStatusLevel(target, FS_SLKKey(buff)) + 1;
    unit_addtimedstatus(target, buff, stacks, S_SpellDuration(code, rank, false));
    slot = unit_findstatus(target, FS_SLKKey(buff));
    if (!slot) return;
    slot->data = code; slot->rank = rank;
    slot->source = attacker; slot->source_spawn_time = attacker->spawn_time;
    S_SpellDamage(target, attacker, (int)(MAX(0.0f, S_SpellData(code, rank, 1)) * stacks));
}

/* Consume the mark before spawning/damaging: nested death events must not explode it twice. */
BZ_ABILITY_PROC(CAbilityIncinerate) {
    heroabilitystatus_t *slot = call ? call->status.slot : NULL;
    edict_t * blast;
    if (msg != A_STATUS_DEATH || !slot) return CAbilityPassive(ent, msg, call);
    if (!slot->source || !slot->source->inuse || slot->source->spawn_time != slot->source_spawn_time) {
        memset(slot, 0, sizeof(*slot));
        return true;
    }
    blast = G_Spawn(); blast->owner = slot->source;
    blast->channel.owner_spawn_time = slot->source_spawn_time;
    blast->class_id = slot->data; blast->resources = slot->rank; blast->s.origin2 = ent->s.origin2;
    blast->freetime = G_Time() + (uint32_t)(MAX(0.0f, S_SpellData(slot->data, slot->rank, 6)) * 1000.0f);
    blast->think = incinerate_explode_think;
    memset(slot, 0, sizeof(*slot));
    ent->aiflags |= AI_CORPSE_UNRAISABLE;
    incinerate_explode_think(blast);
    return true;
}
