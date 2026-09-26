#include "s_skills.h"

void holylight_done(LPEDICT self);

static umove_t move_heal = { "stand channel", ai_idle, holylight_done, CAbilityHolyBolt };

void holylight_done(LPEDICT self) {
    self->stand(self);
}

/* Holy Light has unique target validation: friendlies are healed, undead
 * enemies take half-damage.  All other messages inherit CAbilitySimpleSpell. */
BZ_ABILITY_PROC(CAbilityHolyBolt) {
    switch (msg) {
    case A_VALIDATE: {
        spellTarget_t const *st = call ? call->target : NULL;
        LPEDICT target = st ? st->entity : NULL;

        if (!st || target == ent) return false;
        if (!S_SpellIsAliveTarget(target)) return false;
        if (S_SpellIsEnemy(ent, target)) {
            cstring_t race = target->data.UnitData->race;
            return race && !strcmp(race, STR_UNDEAD);
        }
        return S_SpellIsFriend(ent, target) && target->health.value < target->health.max_value;
    }
    case A_EXECUTE: {
        abilityitem_t const *spell = call ? call->item : NULL;
        spellTarget_t const *st = call ? call->target : NULL;
        LPEDICT target = st ? st->entity : NULL;
        uint32_t level;
        float amount;

        if (!spell || !st) return false;
        level = S_SpellLevel(ent, spell->code);
        amount = S_SpellData(spell->code, level, 1);
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, target, NULL, true);
        unit_setmove(ent, &move_heal);
        if (S_SpellIsFriend(ent, target))
            S_SpellHeal(target, amount);
        else
            S_SpellDamage(target, ent, (int)(amount * 0.5f));
        return true;
    }
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}
