#include "s_skills.h"

#define ID_CHARM MAKEFOURCC('A', 'N', 'c', 'h')
#define ID_MOON_WELL MAKEFOURCC('A', 'm', 'b', 't')

/* ---- Charm (ANch): transfer target ownership to caster -------------------- */

static bool charm_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    uint32_t max_level = (uint32_t)S_SpellData(spell->code, level, 1);

    if (!S_SpellIsEnemy(caster, target)) return false;
    if (max_level && target->data.UnitBalance->level > max_level) return false;
    return true;
}

static void charm_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    (void)spell;

    G_SetUnitPlayer(target, caster->s.player);
    target->owner = caster;
    target->combatentity = NULL;
    if (target->stand)
        target->stand(target);
}

/* ---- Eat Tree (Aeat): consume a tree for healing ------------------------- */

static bool eat_tree_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    edict_t *target = st.entity;
    if (!target || target->targtype != TARG_TREE) return false;
    return true;
}

static void eat_tree_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    float heal = S_SpellData(spell->code, level, 3);

    S_SpellHeal(caster, heal);
    G_FreeEdict(target);
}

/* ---- Moon Well (Ambt): spend the well's mana on friendly life, then mana -- */

static bool moon_well_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    float mana_ratio = S_SpellData(spell->code, level, 1); /* DataA: well mana / target mana */
    float life_ratio = S_SpellData(spell->code, level, 2); /* DataB: well mana / target HP */

    if (!caster || !target || !S_SpellIsFriend(caster, target) || caster->mana.value <= 0.0f) return false;
    if (life_ratio > 0.0f && target->health.value < target->health.max_value) return true;
    return mana_ratio > 0.0f && target->mana.value < target->mana.max_value;
}

static void moon_well_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    float mana_ratio = S_SpellData(spell->code, level, 1);
    float life_ratio = S_SpellData(spell->code, level, 2);
    float available = MAX(0.0f, caster->mana.value);

    /* Warsmash/Warcraft spends the same Moon Well pool sequentially: restore
     * missing life first with DataB, then spend what remains on missing mana
     * with DataA. These values are ratios, not per-cast restoration caps. */
    if (life_ratio > 0.0f && available > 0.0f) {
        float wanted = MAX(0.0f, target->health.max_value - target->health.value);
        float gained = MIN(wanted, available / life_ratio);
        if (gained > 0.0f) {
            S_SpellHeal(target, gained);
            available -= gained * life_ratio;
        }
    }
    if (mana_ratio > 0.0f && available > 0.0f) {
        float wanted = MAX(0.0f, target->mana.max_value - target->mana.value);
        float gained = MIN(wanted, available / mana_ratio);
        if (gained > 0.0f) {
            target->mana.value = MIN(target->mana.max_value, target->mana.value + gained);
            available -= gained * mana_ratio;
        }
    }
    caster->mana.value = MAX(0.0f, available);
}

/* ---- Registration -------------------------------------------------------- */

BZ_VALIDATED_SPELL_PROC(AbilityCharm, charm_validate, charm_execute)

BZ_VALIDATED_SPELL_PROC(AbilityEatTree, eat_tree_validate, eat_tree_execute)

BZ_VALIDATED_SPELL_PROC(AbilityManaBattery, moon_well_validate, moon_well_execute)

/* ---- Root (Aroo): toggle rooted state ------------------------------------ */

BZ_COMMAND_PROC(AbilityRoot) {
    edict_t *caster = G_GetMainSelectedUnit(clent->client);
    if (!caster) return;
    caster->no_pathing = !caster->no_pathing;
    caster->movetype = caster->no_pathing ? MOVETYPE_NONE : MOVETYPE_STEP;
    if (caster->stand)
        caster->stand(caster);
}
