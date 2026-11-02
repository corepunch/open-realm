#include "s_skills.h"

#define SIPHON_MANA_ABILITY_CODE MAKEFOURCC('A','H','d','r')

/* The special cap belongs to Siphon Mana, not Life Drain, even when both
 * share a channel thinker. Look up the concrete ability procedure so custom
 * aliases and creep Siphon Mana inherit the correct behavior. */
static bool siphon_mana_is_siphon(uint32_t code) {
    abilityitem_t item = S_AbilityItem(code);
    return item.ability && item.ability->proc == CAbilityDrain;
}

/* Life Drain is CAbilityDrainNeutral even though its AbilityData base is
 * AHdr (the same base as Siphon Mana).  Only the Life Drain presentation
 * belongs here: the Siphon Mana gameplay/channel contract remains unchanged. */
static bool siphon_mana_is_life_drain(uint32_t code) {
    abilityitem_t item = S_AbilityItem(code);
    return item.ability && item.ability->proc == CAbilityDrainNeutral;
}

/* The authored LightningEffect array contains DRAB (mixed drain), DRAL (life)
 * and DRAM (mana).  Select by the effective DataA/B/D/E transfer modes rather
 * than hardcoding ANdr, so custom ability aliases keep their own fields. */
static uint32_t siphon_mana_lightning_index(uint32_t code, uint32_t rank) {
    bool const life = S_SpellData(code, rank, 1) > 0.0f ||
                      S_SpellData(code, rank, 4) > 0.0f;
    bool const mana = S_SpellData(code, rank, 2) > 0.0f ||
                      S_SpellData(code, rank, 5) > 0.0f;
    return life && mana ? 0u : life ? 1u : 2u;
}

/* Life Drain can author both health and mana transfer. Stop a full-health
 * caster only when no authored mana transfer can still benefit them. */
static bool life_drain_recipient_full(edict_t const *caster, edict_t const *target,
                                      uint32_t code, uint32_t rank) {
    bool const health_full = caster->health.value >= caster->health.max_value;
    bool const mana_full = S_SpellData(code, rank, 2) <= 0.0f ||
                           caster->mana.value >= caster->mana.max_value ||
                           target->mana.value <= 0.0f;
    return health_full && mana_full;
}

/* The existing lightning slot is saved with its unique handle.  Reuse the
 * thinker's already-serialized variation field to remember just that handle,
 * without extending the edict/save/network contracts.  Never delete a beam
 * merely because it has the same caster and target as a newer channel. */
static void siphon_mana_end_presentation(edict_t *thinker) {
    uint32_t const handle = thinker ? thinker->variation : 0;
    if (!thinker) return;
    thinker->variation = 0;
    thinker->s.sound = 0;
    if (handle) {
        FOR_LOOP(i, MAX_LIGHTNING_EFFECTS) {
            gLightning_t *lightning = &level.lightning_effects[i];
            if (G_LightningValid(lightning) && lightning->state.handle == handle) {
                G_LightningRemove(lightning);
                break;
            }
        }
    }
    G_DestroyOwnedEffects(thinker);
}

static void siphon_mana_end(edict_t *thinker) {
    siphon_mana_end_presentation(thinker);
    S_SpellEndChannel(thinker);
}

/* The MPQ's drain buff art is attached to the two chests and stays visible
 * through the channel, not through a temporary status.  The configured
 * Life Drain buff classes are Bdcl (caster) and Bdtl (victim).  Missing
 * optional art/sound/lightning must not change gameplay or the channel. */
static void siphon_mana_begin_presentation(edict_t *thinker, edict_t *caster,
                                           edict_t *target, uint32_t code, uint32_t rank) {
    gLightning_t *lightning;
    if (!thinker || !caster || !target || !siphon_mana_is_life_drain(code)) return;
    (void)G_SpawnOwnedAbilityEffectTarget(thinker, MAKEFOURCC('B','d','c','l'),
                                          WC3_EFFECT_TARGET, 0, caster, "chest");
    (void)G_SpawnOwnedAbilityEffectTarget(thinker, MAKEFOURCC('B','d','t','l'),
                                          WC3_EFFECT_TARGET, 0, target, "chest");
    lightning = G_SpawnAbilityLightning(&(abilityLightningParams_t){
        .ability_id = code, .index = siphon_mana_lightning_index(code, rank),
        .source = caster, .target = target,
        /* A channel owns this beam; the 2-second LightningData duration is
         * the ribbon's animation data, not the scripted channel lifetime. */
        .duration_ms = 0,
    });
    if (lightning) thinker->variation = lightning->state.handle;
    thinker->s.origin = caster->s.origin;
    thinker->s.origin2 = caster->s.origin2;
    thinker->s.sound = (uint16_t)G_AbilityEffectSoundIndex(code, true);
}

/* General mana regeneration only asks for a decay rate when overcharged.
 * The ability owns the pause policy, including cancel/recast and aliases. */
float S_ManaExcessDecayRate(edict_t *ent) {
    channel_t const *cast = G_GetChannel(ent);
    return cast && cast->code && siphon_mana_is_siphon(cast->code) ? 0.0f :
        MAX(0.0f, S_SpellData(SIPHON_MANA_ABILITY_CODE, 1, 9));
}

/* Drain's Ndr1/Ndr2 select health/mana independently; allied transfer uses Ndr4/Ndr5 instead. */
static bool siphon_mana_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    uint32_t rank = S_SpellLevel(caster, spell->code);
    if (!target || target == caster) return false;
    if (S_SpellIsFriend(caster, target))
        return (S_SpellData(spell->code, rank, 4) > 0 && caster->health.value > 1 && target->health.value < target->health.max_value) ||
            (S_SpellData(spell->code, rank, 5) > 0 && caster->mana.value > 0 && target->mana.value < target->mana.max_value);
    return S_SpellIsEnemy(caster, target) &&
        (S_SpellData(spell->code, rank, 1) > 0 || (S_SpellData(spell->code, rank, 2) > 0 && target->mana.value > 0));
}

/* Recheck both edict incarnations and the cast serial before every pulse; cancelled drains cannot revive on recast. */
void siphon_mana_think(edict_t *ent) {
    edict_t *caster = ent->owner, *target = S_SpellChannelTarget(ent);
    uint32_t now = G_Time(), code = ent->class_id, rank = ent->resources;
    float amount, before;
    if (!S_SpellChannelActive(ent) || !S_SpellIsAliveTarget(target) || !S_SpellAllowsTarget(code, caster, target) ||
        !S_SpellTargetInRange(caster, target, ent->collision)) { siphon_mana_end(ent); return; }
    /* Life Drain has nothing left to transfer once its caster is fully healed.
     * End before damaging the victim, so the target is not drained after the
     * recipient reaches its authored health cap. */
    if (siphon_mana_is_life_drain(code) && !S_SpellIsFriend(caster, target) &&
        life_drain_recipient_full(caster, target, code, rank)) {
        siphon_mana_end(ent);
        return;
    }
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
        if (siphon_mana_is_life_drain(code) && S_SpellData(code, rank, 2) <= 0.0f)
            amount = MIN(amount, MAX(0.0f, caster->health.max_value - caster->health.value));
        if (amount > 0 && S_SpellDamage(target, caster, (int)amount))
            S_SpellHeal(caster, MAX(0, before - target->health.value));
        /* Damage may kill the target or dispatch a trigger that interrupts this
         * channel. Do not continue draining mana from a dead/stale target or
         * let a retired thinker affect a replacement cast in the same pulse. */
        if (!S_SpellChannelActive(ent) || !S_SpellIsAliveTarget(target) ||
            S_SpellChannelTarget(ent) != target) {
            siphon_mana_end(ent);
            return;
        }
        if (siphon_mana_is_life_drain(code) &&
            life_drain_recipient_full(caster, target, code, rank)) {
            siphon_mana_end(ent);
            return;
        }
        /* Siphon Mana transfers to a temporary 200% cap. Only transfer what
         * the receiver can keep; the old max-mana clamp discarded the excess
         * while still removing the full amount from the target. */
        float const cap = caster->mana.max_value * (siphon_mana_is_siphon(code) ? WC3_MANA_OVERCHARGE_LIMIT : 1.0f);
        amount = MIN(MAX(0.0f, target->mana.value), S_SpellData(code, rank, 2) * ent->velocity);
        amount = MAX(0.0f, MIN(amount, cap - caster->mana.value));
        target->mana.value -= amount;
        caster->mana.value += amount;
    }
    if (now >= ent->spawn_time || !S_SpellIsAliveTarget(target)) { siphon_mana_end(ent); return; }
    ent->freetime = now + (uint32_t)MAX(FRAMETIME, ent->velocity * 1000.0f);
}

/* A shared thinker keeps the requested rawcode and rank, so Life Drain and Siphon Mana retain different data. */
static void siphon_mana_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t rank = S_SpellLevel(caster, spell->code);
    edict_t *ent = S_SpellChannelTargetThinker(caster, spell->code, st.entity);
    ent->resources = rank; ent->velocity = S_SpellData(spell->code, rank, 3);
    ent->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, rank);
    ent->spawn_time = G_Time() + (uint32_t)(S_SpellHeroDuration(spell->code, rank, st.entity) * 1000.0f);
    ent->think = siphon_mana_think;
    ent->freetime = G_Time() + (uint32_t)MAX(FRAMETIME, ent->velocity * 1000.0f);
    siphon_mana_begin_presentation(ent, caster, st.entity, spell->code, rank);
}

BZ_VALIDATED_SPELL_PROC(AbilityDrainNeutral, siphon_mana_validate, siphon_mana_execute)
BZ_VALIDATED_SPELL_PROC(AbilityDrain, siphon_mana_validate, siphon_mana_execute)
