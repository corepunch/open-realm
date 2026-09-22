#include "s_skills.h"

#define ID_DISEASE_CLOUD MAKEFOURCC('A','a','p','l')
#define DISEASE_TICK_MS 1000 // milliseconds; DataB is damage per second; infection pulse interval

/* A recipient carries the infection after leaving the aura; rank and pulse deadline survive save/load. */
static void disease_tick(LPEDICT target, heroabilitystatus_t *slot) {
    LPEDICT source = slot->source;
    if (!source || !source->inuse || source->spawn_time != slot->source_spawn_time) {
        unit_expirestatus(target, slot);
        return;
    }
    while (slot->level && slot->next_tick <= G_Time() && slot->next_tick < slot->timestamp) {
        DWORD code = slot->data, rank = slot->rank;
        /* T_Damage updates statuses too: advance before damage to prevent recursive ticks. */
        slot->next_tick += DISEASE_TICK_MS;
        S_SpellDamage(target, source, (int)S_SpellData(code, rank, 2));
        if (M_IsDead(target)) break;
    }
}

/* DataA is lifetime, DataB is DPS. Re-entry refreshes duration without adding an extra damage pulse. */
BZ_ABILITY_PROC(CAbilityDiseaseCloud) {
    abilityAliasRef_t ability;
    abilityLevel_t const *row;
    LPCSTR buff;
    if (msg == A_STATUS_TICK && call && call->status.slot) { disease_tick(ent, call->status.slot); return true; }
    if (msg == A_STATUS_DEATH && call && call->status.slot) { unit_expirestatus(ent, call->status.slot); return true; }
    if (msg != A_UPDATE || !S_AuraUnitActive(ent)) return CAbilityPassive(ent, msg, call);
    ability = S_ResolveAbilityAlias(ent, ID_DISEASE_CLOUD);
    if (!ability.alias) return true;
    row = G_AbilityLevel(ability.alias, ability.level);
    if (row->area <= 0.0f || row->data[0].number <= 0.0f || row->data[1].number <= 0.0f) return true;
    /* ROC omits BuffID; the authored TFT infection token is Bapl (Bplg is cloud art). */
    buff = row->buffID && strlen(row->buffID) >= 4 ? row->buffID : "Bapl";
    FILTER_EDICTS(target, target != ent && S_SpellAllowsTarget(ability.alias, ent, target) &&
                  Vector2_distance(&ent->s.origin2, &target->s.origin2) <= row->area) {
        heroabilitystatus_t *slot = unit_findstatus(target, FS_SLKKey(buff));
        if (!slot || !S_UnitHasStatus(target, slot->code)) {
            unit_addtimedstatus(target, buff, ability.level, row->data[0].number);
            slot = unit_findstatus(target, FS_SLKKey(buff));
            if (!slot) continue;
            slot->data = ability.alias; slot->rank = ability.level;
            slot->source = ent; slot->source_spawn_time = ent->spawn_time;
            slot->next_tick = G_Time() + DISEASE_TICK_MS;
        } else if (slot->source == ent && slot->source_spawn_time == ent->spawn_time) {
            slot->timestamp = G_Time() + (DWORD)(row->data[0].number * 1000.0f);
        }
    }
    return true;
}
