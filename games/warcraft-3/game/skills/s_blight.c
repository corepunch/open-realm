#include "s_skills.h"

/* Resolve the concrete authored ability alias whose AbilityData behavior is Abli.
 * This keeps custom object-data derivatives data-driven instead of hard-coding
 * the stock rawcode. */
static DWORD blight_growth_ability(LPEDICT ent) {
    char alias_name[5] = { 0 };

    if (!ent) return 0;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            DWORD alias = 0;
            abilityitem_t item;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&alias, token, sizeof(alias));
            item = S_AbilityItem(alias);
            if (item.ability && item.ability->proc == CAbilityBlightGrowth) return alias;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        DWORD const alias = ent->abilities.added[i];
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

static void blight_growth_reset(LPEDICT ent, DWORD code, DWORD now) {
    DWORD const level = code ? MAX(1u, G_UnitAbilityLevel(ent, code)) : 0;
    FLOAT const interval = level ? S_SpellDuration(code, level, false) : 0.0f;

    ent->blight_growth.ability = code;
    ent->blight_growth.radius = 0.0f;
    ent->blight_growth.next_update = code ? now + (DWORD)(MAX(0.0f, interval) * 1000.0f) : 0;
#ifdef WC3_DEBUG_BLIGHT
    fprintf(stderr, "WC3_BLIGHT growth init ent=%ld unit=%.4s code=%.4s level=%u interval=%.3f next=%u origin=(%.1f,%.1f)\n",
            (long)(ent - g_edicts), (LPCSTR)&ent->class_id, (LPCSTR)&code,
            (unsigned)level, interval, (unsigned)ent->blight_growth.next_update,
            ent->s.origin2.x, ent->s.origin2.y);
#endif
}

/* Warsmash CAbilityBlight semantics: DataA selects create/remove, DataB is the
 * amount grown each Duration interval, and Area is the maximum radius.  This is
 * a passive unit update; there is no command-card cursor or active cast.
 * A_UNIT_INIT/A_ENABLE cache the concrete alias so units without Blight Growth
 * do not parse their ability lists every simulation frame. */
BZ_ABILITY_PROC(CAbilityBlightGrowth) {
    DWORD code, level, now;
    FLOAT interval, expansion, max_radius;
    LPGAMECLIENT owner;

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
        if (!code || ent->blight_growth.ability == code)
            memset(&ent->blight_growth, 0, sizeof(ent->blight_growth));
        return true;
    case A_UNIT_REMOVE:
        memset(&ent->blight_growth, 0, sizeof(ent->blight_growth));
        return true;
    case A_UPDATE:
        break;
    default:
        return false;
    }

    if (!ent->inuse || M_IsDead(ent) || !(code = ent->blight_growth.ability)) return false;
    owner = G_GetPlayerClientByNumber(ent->s.player);
    if (owner && !G_IsPlayerAbilityAvailable(owner, code)) {
#ifdef WC3_DEBUG_BLIGHT
        if (G_Time() >= ent->blight_growth.next_update)
            fprintf(stderr, "WC3_BLIGHT growth blocked ent=%ld code=%.4s reason=unavailable player=%u now=%u\n",
                    (long)(ent - g_edicts), (LPCSTR)&code, (unsigned)ent->s.player, (unsigned)G_Time());
#endif
        return false;
    }
    level = G_UnitAbilityLevel(ent, code);
    if (!level) {
        memset(&ent->blight_growth, 0, sizeof(ent->blight_growth));
        return false;
    }
    interval = S_SpellDuration(code, level, false);
    expansion = S_SpellData(code, level, 2);
    max_radius = S_SpellNumber(code, ABILITY_NUMBER_AREA, level);
    now = G_Time();

    if (interval <= 0.0f || expansion <= 0.0f || max_radius <= 0.0f) {
#ifdef WC3_DEBUG_BLIGHT
        if (G_Time() == ent->blight_growth.next_update)
            fprintf(stderr, "WC3_BLIGHT growth blocked ent=%ld code=%.4s reason=invalid-data interval=%.3f expansion=%.3f max_radius=%.3f dataA=%.3f\n",
                    (long)(ent - g_edicts), (LPCSTR)&code, interval, expansion, max_radius,
                    S_SpellData(code, level, 1));
#endif
        return false;
    }
    if (now < ent->blight_growth.next_update) return false;

    if (ent->blight_growth.radius < max_radius) {
        BOOL const creates = S_SpellData(code, level, 1) != 0.0f;
#ifdef WC3_DEBUG_BLIGHT
        FLOAT const old_radius = ent->blight_growth.radius;
#endif
        ent->blight_growth.radius = MIN(max_radius, ent->blight_growth.radius + expansion);
        G_SetBlightRadius(&ent->s.origin2, ent->blight_growth.radius, creates);
#ifdef WC3_DEBUG_BLIGHT
        fprintf(stderr, "WC3_BLIGHT growth tick ent=%ld code=%.4s now=%u radius=%.3f->%.3f expansion=%.3f max=%.3f creates=%u next=%u point=(%.1f,%.1f) blighted=%u\n",
                (long)(ent - g_edicts), (LPCSTR)&code, (unsigned)now,
                old_radius, ent->blight_growth.radius, expansion, max_radius,
                (unsigned)creates, (unsigned)(now + (DWORD)(interval * 1000.0f)),
                ent->s.origin2.x, ent->s.origin2.y,
                (unsigned)G_IsPointBlighted(&ent->s.origin2));
#endif
    }
    ent->blight_growth.next_update = now + (DWORD)(interval * 1000.0f);
    return true;
}
