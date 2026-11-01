#include "s_skills.h"

#define ID_DISEASE_CLOUD MAKEFOURCC('A','a','p','l')
#define ID_ABOMINATION_DISEASE_CLOUD MAKEFOURCC('A','a','p','1')
#define ID_DISEASE_CLOUD_RESEARCH MAKEFOURCC('R','u','p','c')
#define DISEASE_TICK_MS 1000 // milliseconds; DataB is damage per second; infection pulse interval

static bool disease_cloud_researched(edict_t const *source, uint32_t ability) {
    gameClient_t *owner;
    if (ability != ID_ABOMINATION_DISEASE_CLOUD) return true;
    owner = G_GetPlayerClientByNumber(source ? source->s.player : MAX_PLAYERS);
    return owner && G_GetPlayerTechResearchedLevel(owner, ID_DISEASE_CLOUD_RESEARCH) > 0;
}

static void disease_cloud_update_abomination_art(edict_t *source, uint32_t ability) {
    edict_t *effect = NULL;
    if (!source || ability != ID_ABOMINATION_DISEASE_CLOUD) return;
    FILTER_EDICTS(existing, existing->inuse && existing->owner == source &&
                  existing->summon_ability == ability && existing->goalentity == source) {
        effect = existing;
        break;
    }
    if (!disease_cloud_researched(source, ability)) {
        if (effect) G_FreeEdict(effect);
        return;
    }
    if (effect) return;
    effect = G_SpawnAbilityEffectTarget(ability, WC3_EFFECT_TARGET, 0, source, NULL, false);
    if (!effect) return;
    effect->owner = source;
    effect->summon_ability = ability;
}

/* The map edict list is much larger than an individual cloud. Reject distant
 * entries before spell targeting (immunity/status/ability lookups). Keep the
 * existing exact sqrtf distance as the final range test: a square-only test
 * would incorrectly infect units near its corners. This index-free check
 * also follows moving sources/targets on every pulse. */
static bool disease_cloud_in_range(vec2_t const *center, vec2_t const *point, float radius) {
    float const dx = point->x - center->x;
    float const dy = point->y - center->y;
    if (!(dx >= -radius && dx <= radius && dy >= -radius && dy <= radius)) return false;
    return Vector2_distance(center, point) <= radius;
}

#ifdef BZ_TESTS
static uint32_t disease_cloud_target_validations;
void S_TestResetDiseaseCloudTargetValidations(void) { disease_cloud_target_validations = 0; }
uint32_t S_TestDiseaseCloudTargetValidations(void) { return disease_cloud_target_validations; }
#endif

static bool disease_cloud_allows_target(uint32_t ability, edict_t *source, edict_t *target) {
#ifdef BZ_TESTS
    disease_cloud_target_validations++;
#endif
    return S_SpellAllowsTarget(ability, source, target);
}

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
    if (msg == A_UNIT_TYPE_UPDATE && ent) return 0;
    if (msg == A_UNIT_TYPE_UPDATE) return S_UnitTypeHasAbilityCode(call->unit_type, ID_DISEASE_CLOUD) ? UNIT_UPDATE_RUN : UNIT_UPDATE_SKIP;
    abilityAliasRef_t ability;
    abilityLevel_t const *row;
    cstring_t buff;
    if (msg == A_STATUS_TICK && call && call->status.slot) { disease_tick(ent, call->status.slot); return true; }
    if (msg == A_STATUS_DEATH && call && call->status.slot) { unit_expirestatus(ent, call->status.slot); return true; }
    if (msg != A_UPDATE || !S_AuraUnitActive(ent)) return CAbilityPassive(ent, msg, call);
    ability = S_ResolveAbilityAlias(ent, ID_DISEASE_CLOUD);
    if (!ability.alias) return true;
    if (ability.alias == ID_ABOMINATION_DISEASE_CLOUD) {
        disease_cloud_update_abomination_art(ent, ability.alias);
        if (!disease_cloud_researched(ent, ability.alias)) return true;
    }
    row = G_AbilityLevel(ability.alias, ability.level);
    if (row->area <= 0.0f || row->data[0].number <= 0.0f || row->data[1].number <= 0.0f) return true;
    /* ROC omits BuffID; the authored TFT infection token is Bapl (Bplg is cloud art). */
    buff = row->buffID && strlen(row->buffID) >= 4 ? row->buffID : "Bapl";
    FILTER_EDICTS(target, target != ent && S_SpellIsAliveTarget(target) &&
                  disease_cloud_in_range(&ent->s.origin2, &target->s.origin2, row->area) &&
                  disease_cloud_allows_target(ability.alias, ent, target)) {
        /* Each infector owns its own infection: another source's Bapl must
         * neither suppress this application nor have its pulse phase reset. */
        heroabilitystatus_t *slot = unit_findstatussourceclass(target, FS_SLKKey(buff), ent);
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
                .buff_flags = FS_SLKKey(buff) == MAKEFOURCC('B','a','p','l') ?
                    WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_PHYSICAL : 0
            };
            slot = unit_applystatus(target, &app);
            if (!slot) continue; /* Full: do not evict another infection. */
            slot->next_tick = G_Time() + DISEASE_TICK_MS;
        }
        /* Also normalize a reused pre-classification save record. */
        if (slot->code == MAKEFOURCC('B','a','p','l'))
            slot->buff_flags = (slot->buff_flags & ~(WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_TRANSFERABLE)) |
                               WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_PHYSICAL;
        /* Disease Cloud's persistent target model is authored on Bapl. The
         * direct status application above intentionally owns independent
         * infections, so opt each slot into the regular status-art lifecycle
         * here (including same-source refreshes). */
        if (slot->code == MAKEFOURCC('B','a','p','l'))
            unit_status_enabletargetart(target, slot, NULL);
    }
    return true;
}
