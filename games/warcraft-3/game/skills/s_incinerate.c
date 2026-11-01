#include "s_skills.h"

#define ID_INCINERATE MAKEFOURCC('A','N','i','c')
#define ID_INCINERATE_ARROW MAKEFOURCC('A','N','i','a')

/* TFT stores full/outer damage in DataB/D and radii in DataC/E; Area is deliberately zero. */
void incinerate_explode_think(edict_t *ent) {
    uint32_t code = ent->class_id, rank = ent->resources;
    edict_t *source = S_SpellChannelOwner(ent);
    float full = S_SpellData(code, rank, 3), outer = S_SpellData(code, rank, 5);
    if (G_Time() < ent->freetime) return;
    if (source) {
        FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellAllowsTarget(code, source, target)) {
            float dist = Vector2_distance(&target->s.origin2, &ent->s.origin2);
            float damage = dist <= full ? S_SpellData(code, rank, 2) :
                           dist <= outer ? S_SpellData(code, rank, 4) : 0.0f;
            if (damage > 0.0f) S_SpellDamage(target, source, (int)damage);
        }
    }
    G_SpawnAbilityEffectAtPoint(code, WC3_EFFECT_SPECIAL, 0, &ent->s.origin2, true);
    G_FreeEdict(ent);
}

/* Attach the mark before either component of the attack can kill; the victim's death owns the explosion. */
void S_IncinerateOnHit(edict_t *attacker, edict_t *target) {
    abilityAliasRef_t ability = S_ResolveAbilityAlias(attacker, ID_INCINERATE_ARROW);
    heroabilitystatus_t *slot;
    cstring_t buff;
    uint32_t stacks, code, rank;
    /* The mana-paid arrow is optional. If owned but disabled/without mana,
     * attacks remain ordinary: never silently fall back to passive ANic. */
    if (ability.alias && ability.level) {
        if (!G_UnitAutocastIsOn(attacker, ability.alias) ||
            !target || !S_SpellAllowsTarget(ability.alias, attacker, target) ||
            !S_SpellSpendMana(attacker, ability.alias, ability.level)) return;
    } else ability = S_ResolveAbilityAlias(attacker, ID_INCINERATE);
    code = ability.alias; rank = ability.level;
    if (!code || !rank || !target || !S_SpellAllowsTarget(code, attacker, target)) return;
    buff = S_SpellBuffId(code, rank);
    /* Map overrides may omit BuffID; both Incinerate variants use BNic in the TFT profiles. */
    if (!buff || strlen(buff) < 4) buff = "BNic";
    stacks = G_UnitStatusLevel(target, FS_SLKKey(buff)) + 1;
    slot = S_SpellApplyTimedStatus(target, buff, stacks, S_SpellDuration(code, rank, false));
    if (!slot) return;
    slot->data = code; slot->source_ability = code; slot->rank = rank;
    slot->source = attacker; slot->source_spawn_time = attacker->spawn_time;
    if (stacks == 1) unit_status_enabletargetart(target, slot, "chest");
    S_SpellDamage(target, attacker, (int)(MAX(0.0f, S_SpellData(code, rank, 1)) * stacks));
}

/* Consume the mark before spawning/damaging: nested death events must not explode it twice. */
BZ_ABILITY_PROC(CAbilityIncinerate) {
    heroabilitystatus_t *slot = call ? call->status.slot : NULL;
    edict_t *blast, *source;
    uint32_t const code = call && call->item ? call->item->code : 0;
    if (msg == A_TOGGLE_ON || msg == A_AUTOCAST_ON)
        return ent && ent->unit_state && ent->unit_state->autocast == code;
    if (msg == A_AUTOCAST_SET) return true;
    /* Attack modifiers are consumed by confirmed hits, not periodic target acquisition. */
    if (msg == A_AUTOCAST_ACQUIRE) return false;
    if (msg != A_STATUS_DEATH || !slot) return CAbilitySimpleSpell(ent, msg, call);
    source = S_SpellStatusSource(slot);
    if (!source) {
        /* An invalidated source cannot explode; retire the mark normally. */
        unit_removestatus_deferred(ent, slot, STATUS_REMOVE_DEATH);
        return true;
    }
    blast = G_Spawn(); blast->owner = source;
    G_AddChannel(blast);
    G_GetChannel(blast)->owner_spawn_time = slot->source_spawn_time;
    blast->class_id = slot->data; blast->resources = slot->rank; blast->s.origin2 = ent->s.origin2;
    blast->freetime = G_Time() + (uint32_t)(MAX(0.0f, S_SpellData(slot->data, slot->rank, 6)) * 1000.0f);
    blast->think = incinerate_explode_think;
    /* Retire before any immediate explosion damage. The removal callback only
     * handles A_STATUS_DEATH, so this cannot schedule a second explosion.
     * Do not reconcile a dying unit during its death dispatch. */
    unit_removestatus_deferred(ent, slot, STATUS_REMOVE_DEATH);
    ent->aiflags |= AI_CORPSE_UNRAISABLE;
    incinerate_explode_think(blast);
    return true;
}
