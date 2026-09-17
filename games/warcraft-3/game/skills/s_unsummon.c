#include "s_skills.h"

/* Owned living structure only; S_SpellAllowsTarget ignores structure/player tokens. */
static BOOL unsummon_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT building = st.entity;
    (void)spell;
    if (!caster || !building || !S_SpellIsAliveTarget(building)) return false;
    if (building->s.player != caster->s.player) return false;
    return G_UnitIsBuilding(building->class_id);
}

/* Refund DataA of UnitBalance gold/lumber (same cost source as cancel-build payment), then kill. */
static void unsummon_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT building = st.entity;
    UnitBalance_t const *bal;
    LPGAMECLIENT client;
    DWORD level;
    FLOAT rate;
    LONG gold, lumber;

    if (!unsummon_validate(caster, st, spell)) return;
    bal = building->data.UnitBalance;
    if (!bal) bal = G_UnitBalance(building->class_id);
    if (!bal) return;
    level = S_SpellLevel(caster, spell->code);
    rate = S_SpellData(spell->code, level, 1);
    if (rate < 0.0f) rate = 0.0f;
    gold = (LONG)(MAX(0, bal->goldCost) * rate);
    lumber = (LONG)(MAX(0, bal->lumberCost) * rate);
    client = G_GetPlayerClientByNumber(building->s.player);
    if (client && client->ps.number == building->s.player) {
        client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] += gold;
        client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] += lumber;
        G_RefreshResourceBar(G_GetPlayerEntityByNumber(building->s.player));
    }
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, building, NULL, true);
    unit_die(building, caster);
}

BZ_VALIDATED_SPELL_PROC(AbilityUnsummon, unsummon_validate, unsummon_execute)
