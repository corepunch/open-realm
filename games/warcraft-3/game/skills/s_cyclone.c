#include "s_skills.h"

#define BZ_CYCLONE_BUFF MAKEFOURCC('B','c','y','c') // rawcode; primary Cyclone status; locks actions and targeting
#define BZ_CYCLONE_BUFF_EXTRA MAKEFOURCC('B','c','y','2') // rawcode; extra Cyclone status; locks actions and targeting
#define BZ_TIMED_LIFE_BUFF MAKEFOURCC('B','T','L','F') // lifecycle marker; dispel must not make a temporary unit permanent
#define BZ_CYCLONE_LIFT_HEIGHT 200.0f

static void cyclone_set_fly_height(edict_t *unit, float height) {
    if (!unit) return;
    unit->unitinfo.FlyHeight = MAX(0.0f, height);
    M_CheckGround(unit);
    if (unit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    gi.LinkEntity(unit);
}

static bool cyclone_is_lift_buff(uint32_t code) {
    return code == BZ_CYCLONE_BUFF || code == BZ_CYCLONE_BUFF_EXTRA;
}

static heroabilitystatus_t *cyclone_existing_lift_status(edict_t *unit) {
    heroabilitystatus_t *status = unit_findstatus(unit, BZ_CYCLONE_BUFF);
    return status ? status : unit_findstatus(unit, BZ_CYCLONE_BUFF_EXTRA);
}

static bool cyclone_has_other_lift_status(edict_t const *unit, heroabilitystatus_t const *removed) {
    FOR_LOOP(i, unit && unit->unit_state ? MAX_UNIT_STATUSES : 0) {
        heroabilitystatus_t const *status = unit->unit_state->statuses + i;
        if (status == removed || !status->level) continue;
        if (cyclone_is_lift_buff(status->code)) return true;
    }
    return false;
}

/* Both Acyc and Tornado's Atsp can own Bcyc. Removal callbacks run while
 * their slot is still live; restore the original height only on the final
 * Cyclone status, regardless of which ability created or removed it. */
bool S_CycloneRestoreLiftHeight(edict_t *unit, heroabilitystatus_t const *removed) {
    union { float height; uint32_t bits; } original_height;
    if (!unit || !removed || !cyclone_is_lift_buff(removed->code)) return false;
    if (!cyclone_has_other_lift_status(unit, removed)) {
        original_height.bits = removed->next_tick;
        cyclone_set_fly_height(unit, original_height.height);
    }
    return true;
}

bool S_UnitIsCycloned(edict_t const *unit) {
    return unit && (G_UnitStatusLevel(unit, BZ_CYCLONE_BUFF) || G_UnitStatusLevel(unit, BZ_CYCLONE_BUFF_EXTRA));
}

/* BTLF owns temporary-unit lifetime and is never dispellable. Cyclone additionally
 * uses DataA==0 on its applying rawcode (status.data) for an authored undispellable buff. */
bool S_StatusIsUndispellable(heroabilitystatus_t const *status) {
    abilityitem_t item;
    if (!status || !status->level) return false;
    if (status->code == BZ_TIMED_LIFE_BUFF || status->code == MAKEFOURCC('B','O','w','k')) return true;
    if (!status->data) return false;
    item = S_AbilityItem(status->data);
    if (!item.ability || item.ability->proc != CAbilityCyclone) return false;
    return S_SpellData(status->data, status->level, 1) == 0.0f;
}

/* Validate via authored targs; empty BuffID falls back to Bcyc like Aams → Bams. DataA is dispel-only. */
BZ_ABILITY_PROC(CAbilityCyclone) {
    spellTarget_t const *target;
    uint32_t level;
    uint32_t buff_code;
    cstring_t buff;
    heroabilitystatus_t *slot;
    union { float height; uint32_t bits; } original_height;

    if (msg == A_STATUS_REMOVE && call && call->status.slot &&
        S_CycloneRestoreLiftHeight(ent, call->status.slot)) return true;

    if ((msg != A_VALIDATE && msg != A_EXECUTE) || !call || !call->item || !call->target)
        return CAbilitySimpleSpell(ent, msg, call);
    target = call->target;
    if (msg == A_VALIDATE) {
        wc3_status_apply_check_t result;
        if (target->type != SPELL_TARGET_UNIT || !target->entity ||
            !S_SpellAllowsTarget(call->item->code, ent, target->entity)) return false;
        level = S_SpellLevel(ent, call->item->code);
        buff = S_SpellBuffId(call->item->code, level);
        if (!buff || strlen(buff) < 4) buff = "Bcyc";
        result = unit_status_checkapplication(target->entity, &(status_application_t){
            .buff = buff, .level = level
        });
        return result == WC3_STATUS_APPLY_FREE_SLOT || result == WC3_STATUS_APPLY_REUSE;
    }
    if (target->type != SPELL_TARGET_UNIT || !target->entity) return false;
    level = S_SpellLevel(ent, call->item->code);
    buff = S_SpellBuffId(call->item->code, level);
    if (!buff || strlen(buff) < 4) buff = "Bcyc";
    original_height.height = target->entity->unitinfo.FlyHeight;
    memcpy(&buff_code, buff, sizeof(buff_code));
    if (cyclone_is_lift_buff(buff_code)) {
        /* Replacements and overlapping Bcyc/Bcy2 statuses must retain the
         * pre-Cyclone height, not snapshot the already lifted 200-unit height. */
        heroabilitystatus_t *existing = cyclone_existing_lift_status(target->entity);
        if (existing) original_height.bits = existing->next_tick;
    }
    slot = S_SpellApplyTimedStatus(target->entity, buff, level,
                                    S_SpellResistantDuration(call->item->code, level, target->entity));
    /* Timed-status replacement clears payload; restore the applying rawcode. */
    if (slot) {
        slot->data = call->item->code;
        slot->source_ability = call->item->code;
        if (cyclone_is_lift_buff(slot->code)) {
            slot->next_tick = original_height.bits;
            cyclone_set_fly_height(target->entity, BZ_CYCLONE_LIFT_HEIGHT);
        }
        slot->buff_flags |= WC3_STATUS_BUFF_TARGET_ART;
        unit_status_enabletargetart(target->entity, slot, NULL);
    }
    if (!slot) return false; /* Do not interrupt movement on allocation failure at impact time. */
    target->entity->goalentity = NULL;
    target->entity->currentmove = &holdpos_move_stand;
    return true;
}
