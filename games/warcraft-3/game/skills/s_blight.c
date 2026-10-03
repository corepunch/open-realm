#include "s_skills.h"

/* Resolve the concrete authored ability alias whose AbilityData behavior is Abli.
 * This keeps custom object-data derivatives data-driven instead of hard-coding
 * the stock rawcode. */
static uint32_t blight_growth_ability(edict_t *ent) {
    char alias_name[5] = { 0 };

    if (!ent) return 0;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            uint32_t alias = 0;
            abilityitem_t item;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&alias, token, sizeof(alias));
            item = S_AbilityItem(alias);
            if (item.ability && item.ability->proc == CAbilityBlightGrowth) return alias;
        }
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

    E_blight_growth(ent)->ability = code;
    E_blight_growth(ent)->radius = 0.0f;
    E_blight_growth(ent)->next_update = code ? now + (uint32_t)(MAX(0.0f, interval) * 1000.0f) : 0;
}

/* Warsmash CAbilityBlight semantics: DataA selects create/remove, DataB is the
 * amount grown each Duration interval, and Area is the maximum radius.  This is
 * a passive unit update; there is no command-card cursor or active cast.
 * A_UNIT_INIT/A_ENABLE cache the concrete alias so units without Blight Growth
 * do not parse their ability lists every simulation frame. */
BZ_ABILITY_PROC(CAbilityBlightGrowth) {
    uint32_t code, level, now;
    float interval, expansion, max_radius;

    if (!ent) return false;
    switch (msg) {
    case A_UNIT_INIT:
        code = blight_growth_ability(ent);
        blight_growth_reset(ent, code, G_Time());
        return code != 0;
    case A_ENABLE:
        code = call &E_item_get(call) ? call->item->code : 0;
        if (code) blight_growth_reset(ent, code, G_Time());
        return code != 0;
    case A_DISABLE:
        code = call &E_item_get(call) ? call->item->code : 0;
        if (!code || E_blight_growth_get(ent)->ability == code)
            G_PoolDrop_blight_growth(ent);
        return true;
    case A_UNIT_REMOVE:
        G_PoolDrop_blight_growth(ent);
        return true;
    case A_UPDATE:
        break;
    default:
        return false;
    }

    if (!ent->inuse || M_IsDead(ent) || !(code = E_blight_growth_get(ent)->ability)) return false;
    /* Construction owns the building's incomplete lifecycle.  Passive Abli
     * ticks resume only after the structure is complete. */
    if (E_construction_get(ent)->active) return false;
    if (!G_IsUnitAbilityAvailable(ent, code)) return false;
    level = G_UnitAbilityLevel(ent, code);
    if (!level) {
        G_PoolDrop_blight_growth(ent);
        return false;
    }
    interval = S_SpellDuration(code, level, false);
    expansion = S_SpellData(code, level, 2);
    max_radius = S_SpellNumber(code, ABILITY_NUMBER_AREA, level);
    now = G_Time();

    if (interval <= 0.0f || expansion <= 0.0f || max_radius <= 0.0f) return false;
    /* Data changes affect future growth, not already-painted world state. */
    E_blight_growth(ent)->radius = MIN(E_blight_growth_get(ent)->radius, max_radius);
    if (now < E_blight_growth_get(ent)->next_update) return false;

    if (E_blight_growth_get(ent)->radius < max_radius) {
        bool const creates = S_SpellData(code, level, 1) != 0.0f;
        E_blight_growth(ent)->radius = MIN(max_radius, E_blight_growth_get(ent)->radius + expansion);
        G_SetBlightRadius(&ent->s.origin2, E_blight_growth_get(ent)->radius, creates);
        BLIGHT_LOG("growth tick radius=%.3f max=%.3f\n", E_blight_growth_get(ent)->radius, max_radius);
    }
    E_blight_growth(ent)->next_update = now + (uint32_t)(interval * 1000.0f);
    return true;
}
