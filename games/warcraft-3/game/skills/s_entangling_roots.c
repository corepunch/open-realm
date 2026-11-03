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

        if (!spell || !target) {
            fprintf(stderr, "WC3 Entangling Roots execute-rejected reason=missing-spell-or-target spell=%u target=%u\n",
                    (unsigned)(spell != NULL), (unsigned)(target != NULL));
            return true;
        }
        level = S_SpellLevel(ent, spell->code);
        buff = S_SpellBuffId(spell->code, level);
#if BZ_COMPAT_WORKAROUNDS
        /* RoC 1.0 omits AEer.BuffID; the stock root status is BEer. */
        if ((!buff || strlen(buff) < 4) &&
            (spell->code == MAKEFOURCC('A','E','e','r') ||
             spell->code == MAKEFOURCC('A','e','n','r') ||
             spell->code == MAKEFOURCC('A','e','n','w'))) buff = "BEer";
#endif
        if (!buff || strlen(buff) < 4) {
            fprintf(stderr, "WC3 Entangling Roots execute-rejected reason=invalid-buff caster=%u target=%u code=%.4s level=%u buff=%s\n",
                    ent ? (unsigned)ent->s.number : UINT32_MAX,
                    (unsigned)target->s.number, (cstring_t)&spell->code, level,
                    buff ? buff : "(missing)");
            return true;
        }
        duration = S_SpellResistantDuration(spell->code, level, target);
        slot = S_SpellApplyTimedStatus(target, buff, level, duration);
        if (!slot) {
            fprintf(stderr, "WC3 Entangling Roots execute-rejected reason=status-application-failed caster=%u target=%u code=%.4s level=%u buff=%s duration=%.2f\n",
                    ent ? (unsigned)ent->s.number : UINT32_MAX,
                    (unsigned)target->s.number, (cstring_t)&spell->code, level, buff, duration);
            return true;
        }
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
        fprintf(stderr, "WC3 Entangling Roots applied caster=%u target=%u code=%.4s level=%u buff=%s duration=%.2f target-owner=%u caster-owner=%u\n",
                ent ? (unsigned)ent->s.number : UINT32_MAX, (unsigned)target->s.number,
                (cstring_t)&spell->code, level, buff, duration,
                (unsigned)target->s.player, ent ? (unsigned)ent->s.player : UINT32_MAX);
        return true;
    }
    return CAbilitySimpleSpell(ent, msg, call);
}
