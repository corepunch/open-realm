#include "s_skills.h"

/* Drain's Ndr1/Ndr2 select health/mana independently; allied transfer uses Ndr4/Ndr5 instead. */
static bool siphon_mana_validate(edict_t * caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t * target = st.entity;
    uint32_t rank = S_SpellLevel(caster, spell->code);
    if (!target || target == caster) return false;
    if (S_SpellIsFriend(caster, target))
        return (S_SpellData(spell->code, rank, 4) > 0 && caster->health.value > 1 && target->health.value < target->health.max_value) ||
            (S_SpellData(spell->code, rank, 5) > 0 && caster->mana.value > 0 && target->mana.value < target->mana.max_value);
    return S_SpellIsEnemy(caster, target) &&
        (S_SpellData(spell->code, rank, 1) > 0 || (S_SpellData(spell->code, rank, 2) > 0 && target->mana.value > 0));
}

/* Recheck both edict incarnations and the cast serial before every pulse; cancelled drains cannot revive on recast. */
void siphon_mana_think(edict_t * ent) {
    edict_t * caster = ent->owner, *target = ent->goalentity;
    uint32_t now = G_Time(), code = ent->class_id, rank = ent->resources;
    float amount, before;
    if (!S_SpellChannelActive(ent) || !S_SpellIsAliveTarget(target) ||
        target->spawn_time != ent->channel.target_spawn_time || !S_SpellAllowsTarget(code, caster, target) ||
        !S_SpellTargetInRange(caster, target, ent->collision)) { S_SpellEndChannel(ent); return; }
    if (now < ent->freetime) return;
    if (S_SpellIsFriend(caster, target)) {
        amount = MIN(MAX(0, caster->health.value - 1), S_SpellData(code, rank, 4) * ent->velocity);
        amount = MIN(amount, target->health.max_value - target->health.value);
        G_SetHealth(caster, caster->health.value - amount); S_SpellHeal(target, amount);
        amount = MIN(caster->mana.value, S_SpellData(code, rank, 5) * ent->velocity);
        amount = MIN(amount, target->mana.max_value - target->mana.value);
        caster->mana.value -= amount; target->mana.value += amount;
    } else {
        before = target->health.value;
        amount = MIN(before, S_SpellData(code, rank, 1) * ent->velocity);
        if (amount > 0 && S_SpellDamage(target, caster, (int)amount))
            S_SpellHeal(caster, MAX(0, before - target->health.value));
        amount = MIN(target->mana.value, S_SpellData(code, rank, 2) * ent->velocity);
        target->mana.value -= amount;
        caster->mana.value = MIN(caster->mana.max_value, caster->mana.value + amount);
    }
    if (now >= ent->spawn_time || !S_SpellIsAliveTarget(target)) { S_SpellEndChannel(ent); return; }
    ent->freetime = now + (uint32_t)MAX(FRAMETIME, ent->velocity * 1000.0f);
}

/* A shared thinker keeps the requested rawcode and rank, so Life Drain and Siphon Mana retain different data. */
static void siphon_mana_execute(edict_t * caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t rank = S_SpellLevel(caster, spell->code);
    edict_t * ent = S_SpellChannelThinker(caster, spell->code);
    ent->goalentity = st.entity; ent->channel.target_spawn_time = st.entity->spawn_time;
    ent->resources = rank; ent->velocity = S_SpellData(spell->code, rank, 3);
    ent->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, rank);
    ent->spawn_time = G_Time() + (uint32_t)(S_SpellDuration(spell->code, rank, G_UnitIsHero(st.entity)) * 1000.0f);
    ent->think = siphon_mana_think;
    ent->freetime = G_Time() + (uint32_t)MAX(FRAMETIME, ent->velocity * 1000.0f);
}

BZ_VALIDATED_SPELL_PROC(AbilityDrainNeutral, siphon_mana_validate, siphon_mana_execute)
BZ_VALIDATED_SPELL_PROC(AbilityDrain, siphon_mana_validate, siphon_mana_execute)
