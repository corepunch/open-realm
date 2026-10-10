#include "s_skills.h"

#define ID_DISEASE_CLOUD MAKEFOURCC('A','a','p','l')
#define DISEASE_TICK_MS 1000 // milliseconds; DataB is damage per second; infection pulse interval

/* A recipient carries the infection after leaving the aura; rank and pulse deadline survive save/load. */
static void disease_tick(edict_t *target, heroabilitystatus_t *slot) {
    edict_t *source = S_SpellStatusSource(slot);
    if (!source) {
        unit_expirestatus(target, slot);
        return;
    }
    while (unit_status_take_due_tick(slot, DISEASE_TICK_MS, G_Time())) {
        uint32_t code = slot->data, rank = slot->rank;
        /* T_Damage updates statuses too: advance before damage to prevent recursive ticks. */
        /* Deadline already advanced by the shared scheduler helper. */
        S_SpellDamage(target, source, (int)S_SpellData(code, rank, 2));
        if (M_IsDead(target)) break;
    }
}

/* DataA is lifetime, DataB is DPS. Re-entry refreshes duration without adding an extra damage pulse. */
BZ_ABILITY_PROC(CAbilityDiseaseCloud) {
    abilityAliasRef_t ability;
    abilityLevel_t const *row;
    cstring_t buff;
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
        /* Each infector owns its own infection: another source's Bapl must
         * neither suppress this application nor have its pulse phase reset. */
        heroabilitystatus_t *slot = unit_findstatussource(target, FS_SLKKey(buff), ent);
        if (slot && slot->stack_policy == WC3_STATUS_STACK_INDEPENDENT &&
            slot->source_ability == ability.alias) {
            /* Same-source exposure refreshes lifetime, not the tick deadline.
             * Never replace this slot, which would dispatch inverse callbacks. */
            uint32_t duration_ms = (uint32_t)(row->data[0].number * 1000.0f);
            slot->timestamp = G_Time() + duration_ms;
            slot->duration_ms = duration_ms;
        } else {
            status_application_t app = {
                .buff = buff, .level = ability.level, .duration = row->data[0].number,
                .source_ability = ability.alias, .data = ability.alias,
                .source = ent, .rank = ability.level,
                .stack_policy = WC3_STATUS_STACK_INDEPENDENT,
                .buff_flags = WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGICAL
            };
            slot = unit_applystatus(target, &app);
            if (!slot) continue; /* Full: do not evict another infection. */
            slot->next_tick = G_Time() + DISEASE_TICK_MS;
        }
    }
    return true;
}
