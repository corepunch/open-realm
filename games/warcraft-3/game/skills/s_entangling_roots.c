#include "s_skills.h"

/* Entangling Roots is a dispellable timed status. Movement and attack paths
 * consume BEer directly; the status callback owns the authored Eer1 DPS.
 * Eer1 is damage-per-second, so use the status scheduler's deterministic
 * one-second pulse convention (the retail internal pulse granularity is not
 * treated as recovered data). */
#define ENTANGLING_ROOTS_TICK_MS 1000

static void entangling_roots_tick(edict_t *target, heroabilitystatus_t *slot) {
    edict_t *source;
    float damage;

    if (!target || !slot || !slot->level || !slot->data || !slot->rank) return;
    source = S_SpellStatusSource(slot);
    if (!source) return;
    damage = MAX(0.0f, S_SpellData(slot->data, slot->rank, 1));
    while (unit_status_take_due_tick(slot, ENTANGLING_ROOTS_TICK_MS, G_Time())) {
        /* Damage can recurse through unit_updatestatuses(); advance first. */
        /* Deadline already advanced by the shared scheduler helper. */
        if (damage > 0.0f) S_SpellDamage(target, source, (int)damage);
        if (M_IsDead(target)) break;
    }
}

/* Name=Entangling Roots
 * Ubertip="Roots a target enemy unit in place, preventing movement for <AEer,Dur1> seconds."
 */
BZ_ABILITY_PROC(CAbilityEntanglingRoots) {
    if (msg == A_STATUS_TICK && call && call->status.slot) {
        entangling_roots_tick(ent, call->status.slot);
        return true;
    }
    if (msg == A_STATUS_DEATH && call && call->status.slot) {
        unit_expirestatus(ent, call->status.slot);
        return true;
    }
    if (msg == A_EXECUTE) {
        abilityitem_t const *spell = call ? call->item : NULL;
        spellTarget_t st = call && call->target ? *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
        edict_t *target = st.entity;
        uint32_t level;
        cstring_t buff;
        float duration;
        heroabilitystatus_t *slot;

        if (!spell || !target) return true;
        level = S_SpellLevel(ent, spell->code);
        buff = S_SpellBuffId(spell->code, level);
        if (!buff || strlen(buff) < 4) return true;
        duration = S_SpellHeroDuration(spell->code, level, target);
        slot = S_SpellApplyTimedStatus(target, buff, level, duration);
        if (!slot) return true;
        slot->data = spell->code;
        slot->source_ability = spell->code;
        slot->rank = level;
        slot->source = ent;
        slot->source_spawn_time = ent->spawn_time;
        slot->next_tick = G_Time() + ENTANGLING_ROOTS_TICK_MS;
        /* Roots interrupt active channels but are not a stun/silence: the
         * victim may still issue otherwise legal spell casts afterwards. */
        S_SpellCancelChannel(target);
        unit_status_enabletargetart(target, slot, "origin");
        return true;
    }
    return CAbilitySimpleSpell(ent, msg, call);
}
