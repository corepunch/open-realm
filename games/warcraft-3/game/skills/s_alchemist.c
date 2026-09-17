#include "s_skills.h"

/* ---- Healing Spray (ANhs): channeled point AoE heal waves ---------------- */

static BOOL healing_spray_hits(LPEDICT caster, DWORD code, LPEDICT target, FLOAT radius, LPCVECTOR2 origin) {
    if (!target || !S_SpellIsAliveTarget(target)) return false;
    if (Vector2_distance(&target->s.origin2, origin) > radius) return false;
    return S_SpellAllowsTarget(code, caster, target);
}

void healing_spray_think(LPEDICT ent) {
    DWORD now = G_Time(), ntargets = 0;
    LPEDICT caster = ent->owner;
    FLOAT heal = (FLOAT)ent->damage, maxheal = ent->velocity;

    if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    if (maxheal > 0.0f) {
        FILTER_EDICTS(target, healing_spray_hits(caster, ent->class_id, target, ent->collision, &ent->s.origin2))
            ntargets++;
        if (ntargets && heal * (FLOAT)ntargets > maxheal)
            heal = maxheal / (FLOAT)ntargets;
    }
    FILTER_EDICTS(target, healing_spray_hits(caster, ent->class_id, target, ent->collision, &ent->s.origin2))
        S_SpellHeal(target, heal);
    if (!--ent->resources) { S_SpellEndChannel(ent); return; }
    ent->freetime = now + (DWORD)(MAX(0.1f, ent->wait) * 1000.0f);
}

/* Name=Healing Spray
 * Ubertip: DataF waves; each heals DataA to friendlies in Area. DataB interval,
 * DataD per-wave max heal budget. DataC missiles / DataE build factor unused.
 */
BZ_SIMPLE_SPELL_PROC(AbilityHealingSpray) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT thinker = S_SpellChannelThinker(caster, spell->code);

    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->damage = (DWORD)MAX(0.0f, S_SpellData(spell->code, level, 1)); /* DataA gainedHP */
    thinker->wait = MAX(0.1f, S_SpellData(spell->code, level, 2)); /* DataB frequency */
    thinker->velocity = S_SpellData(spell->code, level, 4); /* DataD maxGainedHP */
    thinker->resources = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 6)); /* DataF waves */
    thinker->think = healing_spray_think;
    healing_spray_think(thinker);
}

/* ---- Transmute (ANtm): kill target, credit goldCost * DataA -------------- */

static BOOL transmute_is_neutral(LPCEDICT target) {
    return target && target->s.player < MAX_PLAYERS && level.mapinfo &&
        level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral;
}

static BOOL transmute_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT target = st.entity;
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD max_level = (DWORD)S_SpellData(spell->code, level, 3); /* DataC maxCreepLv */
    UnitBalance_t const *bal;

    if (!target || !target->data.UnitBalance) return false;
    bal = target->data.UnitBalance;
    if (G_UnitIsHero(target)) return false;
    if (!S_SpellIsEnemy(caster, target) && !transmute_is_neutral(target)) return false;
    if (max_level && (DWORD)bal->level > max_level) return false;
    return true;
}

static void transmute_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT target = st.entity;
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT gold_factor = S_SpellData(spell->code, level, 1); /* DataA */
    FLOAT lumber_factor = S_SpellData(spell->code, level, 2); /* DataB */
    UnitBalance_t const *bal;
    LPGAMECLIENT client;
    LONG gold, lumber;

    if (!target || !target->data.UnitBalance) return;
    bal = target->data.UnitBalance;
    client = G_GetPlayerClientByNumber(caster->s.player);
    if (client) {
        gold = (LONG)(MAX(0, bal->goldCost) * gold_factor);
        lumber = (LONG)(MAX(0, bal->lumberCost) * lumber_factor);
        if (gold > 0) G_CreditResourceIncome(&client->ps, target, PLAYERSTATE_RESOURCE_GOLD, gold);
        if (lumber > 0) G_CreditResourceIncome(&client->ps, target, PLAYERSTATE_RESOURCE_LUMBER, lumber);
    }
    G_SetHealth(target, 0);
    if (target->die) target->die(target, caster);
    else unit_die(target, caster);
}

BZ_VALIDATED_SPELL_PROC(AbilityTransmute, transmute_validate, transmute_execute)
