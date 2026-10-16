#include "s_skills.h"

/* Resolve the concrete authored ability alias whose AbilityData behavior is Abli.
 * This keeps custom object-data derivatives data-driven instead of hard-coding
 * the stock rawcode. */
static uint32_t blight_growth_ability(edict_t *ent) {
    char alias_name[5] = { 0 };

    if (!ent) return 0;
    uint32_t count;
    uint32_t const *codes = G_UnitAbilityCodes(ent->data.UnitAbilities, &count);
    FOR_LOOP(i, count) {
        abilityitem_t item = S_AbilityItem(codes[i]);
        if (item.ability && item.ability->proc == CAbilityBlightGrowth &&
            G_ActorHasAbilityCode(ent, codes[i])) return codes[i];
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        uint32_t const alias = ent->abilities.added[i];
        abilityitem_t item;
        if (!alias) continue;
        memcpy(alias_name, &alias, 4);
        item = S_AbilityItem(alias);
        if (G_ActorHasSkill(ent, alias_name) && item.ability && item.ability->proc == CAbilityBlightGrowth)
            return alias;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t const *hero = ent->heroabilities + i;
        abilityitem_t item;
        if (!hero->level) continue;
        item = S_AbilityItem(hero->code);
        if (item.ability && item.ability->proc == CAbilityBlightGrowth) return hero->code;
    }
    return 0;
}

static void blight_growth_reset(edict_t *ent, uint32_t code, uint32_t now) {
    uint32_t const level = code ? MAX(1u, G_UnitAbilityLevel(ent, code)) : 0;
    float const interval = level ? S_SpellDuration(code, level, false) : 0.0f;

    if (!code) { G_FreeBlightGrowth(ent); return; }
    if (!ent->blight_growth) ent->blight_growth = G_AllocBlightGrowth();
    assert(ent->blight_growth);
    ent->blight_growth->ability = code;
    ent->blight_growth->radius = 0.0f;
    ent->blight_growth->next_update = code ? now + (uint32_t)(MAX(0.0f, interval) * 1000.0f) : 0;
}

/* Warsmash CAbilityBlight semantics: DataA selects create/remove, DataB is the
 * amount grown each Duration interval, and Area is the maximum radius.  This is
 * a passive unit update; there is no command-card cursor or active cast.
 * A_UNIT_INIT/A_ENABLE cache the concrete alias so units without Blight Growth
 * do not parse their ability lists every simulation frame. */
BZ_ABILITY_PROC(CAbilityBlightGrowth) {
    if (msg == A_UNIT_TYPE_UPDATE && ent) return 0;
    if (msg == A_UNIT_TYPE_UPDATE) return UNIT_UPDATE_POINTER(blight_growth);
    uint32_t code, level, now;
    float interval, expansion, max_radius;

    if (msg == A_UNIT_TYPE_INIT) {
        if (ent || !call) return UNIT_INIT_UNKNOWN;
        return S_UnitTypeHasAbilityProc(call->unit_type, CAbilityBlightGrowth) ? UNIT_INIT_RUN : UNIT_INIT_SKIP_FALSE;
    }
    if (msg == A_UNIT_EVENT_MASK)
        return UNIT_MESSAGE_SUBSCRIPTIONS(A_UNIT_INIT, A_ENABLE, A_DISABLE, A_UNIT_REMOVE, A_UPDATE);
    if (!ent) return false;
    switch (msg) {
    case A_UNIT_INIT:
        code = blight_growth_ability(ent);
        blight_growth_reset(ent, code, G_Time());
        return code != 0;
    case A_ENABLE:
        code = call && call->item ? call->item->code : 0;
        if (code) blight_growth_reset(ent, code, G_Time());
        return code != 0;
    case A_DISABLE:
        code = call && call->item ? call->item->code : 0;
        if (ent->blight_growth && (!code || ent->blight_growth->ability == code))
            G_FreeBlightGrowth(ent);
        return true;
    case A_UNIT_REMOVE:
        G_FreeBlightGrowth(ent);
        return true;
    case A_UPDATE:
        break;
    default:
        return false;
    }

    if (!ent->inuse || !ent->blight_growth || M_IsDead(ent) || !(code = ent->blight_growth->ability)) return false;
    /* Construction owns the building's incomplete lifecycle.  Passive Abli
     * ticks resume only after the structure is complete. */
    if (ent->construction) return false;
    if (!G_IsUnitAbilityAvailable(ent, code)) return false;
    level = G_UnitAbilityLevel(ent, code);
    if (!level) {
        G_FreeBlightGrowth(ent);
        return false;
    }
    interval = S_SpellDuration(code, level, false);
    expansion = S_SpellData(code, level, 2);
    max_radius = S_SpellNumber(code, ABILITY_NUMBER_AREA, level);
    now = G_Time();

    if (interval <= 0.0f || expansion <= 0.0f || max_radius <= 0.0f) return false;
    /* Data changes affect future growth, not already-painted world state. */
    ent->blight_growth->radius = MIN(ent->blight_growth->radius, max_radius);
    if (now < ent->blight_growth->next_update) return false;

    if (ent->blight_growth->radius < max_radius) {
        bool const creates = S_SpellData(code, level, 1) != 0.0f;
        ent->blight_growth->radius = MIN(max_radius, ent->blight_growth->radius + expansion);
        G_SetBlightRadius(&ent->s.origin2, ent->blight_growth->radius, creates);
        BLIGHT_LOG("growth tick radius=%.3f max=%.3f\n", ent->blight_growth->radius, max_radius);
    }
    ent->blight_growth->next_update = now + (uint32_t)(interval * 1000.0f);
    return true;
}
