#include "s_skills.h"

/* Monsoon stays at the selected point; DataB sets the pulse interval, DataC scales building damage. */
void monsoon_think(edict_t *ent) {
    uint32_t code = ent->class_id, rank = ent->resources;
    uint32_t step = (uint32_t)(S_SpellData(code, rank, 2) * 1000.0f);
    if (!S_SpellChannelActive(ent) || G_Time() >= ent->spawn_time) {
        G_DestroyOwnedEffects(ent); S_SpellEndChannel(ent); return;
    }
    if (!step) {
        fprintf(stderr, "WC3 Monsoon: invalid pulse interval for %.4s\n", (cstring_t)&code);
        G_DestroyOwnedEffects(ent); S_SpellEndChannel(ent); return;
    }
    while (ent->freetime <= G_Time()) {
        ent->freetime += step;
        FILTER_EDICTS(target, S_SpellAllowsTarget(code, ent->owner, target) &&
                      Vector2_distance(&target->s.origin2, &ent->s.origin2) <= ent->collision) {
            float damage = S_SpellData(code, rank, 1);
            if (target->targtype == TARG_STRUCTURE || G_UnitIsBuilding(target->class_id))
                damage *= S_SpellData(code, rank, 3);
            if (damage > 0.0f) S_SpellDamage(target, ent->owner, (int)damage);
        }
    }
}

/* The generic channel thinker has no position: initialize it from the cast before the first pulse. */
BZ_ABILITY_PROC(CAbilityMonsoon) {
    uint32_t code = call && call->item ? call->item->code : 0, rank;
    edict_t *thinker;
    if (msg != A_VALIDATE && msg != A_EXECUTE) return CAbilitySimpleSpell(ent, msg, call);
    rank = S_SpellLevel(ent, code);
    if (msg == A_VALIDATE) return call && call->target && call->target->type == SPELL_TARGET_POINT &&
        S_SpellData(code, rank, 2) >= 0.001f;
    if (!call || !call->target) return false;
    thinker = S_SpellChannelThinker(ent, code);
    thinker->resources = rank; thinker->s.origin2 = call->target->point;
    thinker->collision = S_SpellNumber(code, ABILITY_NUMBER_AREA, rank);
    thinker->spawn_time = G_Time() + (uint32_t)(S_SpellDuration(code, rank, false) * 1000.0f);
    thinker->freetime = G_Time(); thinker->think = monsoon_think;
    G_SpawnOwnedAbilityEffectAtPoint(thinker, code, WC3_EFFECT_EFFECT, 0, &thinker->s.origin2);
    monsoon_think(thinker);
    return true;
}
