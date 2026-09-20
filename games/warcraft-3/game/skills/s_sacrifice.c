#include "s_skills.h"

#define ID_SACRIFICE_PIT MAKEFOURCC('A','s','a','c')
#define ID_SACRIFICE_ACOLYTE MAKEFOURCC('A','l','a','m')
#define ID_SHADE MAKEFOURCC('u','s','h','d')

/* Retail exposes the command from both ends: Asac on the Sacrificial Pit
 * targets an Acolyte, while Alam on an Acolyte targets a Sacrificial Pit.
 * Neither ability has object-data fields describing the result type; the
 * stock mechanic always creates a Shade. */
static BOOL sacrifice_pair(LPEDICT caster, LPEDICT target, DWORD ability,
                           LPEDICT *pit, LPEDICT *worker) {
    DWORD const code = G_AbilityCode(ability);

    if (!caster || !target || !S_SpellIsAliveTarget(caster) || !S_SpellIsAliveTarget(target) ||
        caster->s.player != target->s.player) return false;
    if (code == ID_SACRIFICE_PIT) {
        *pit = caster;
        *worker = target;
    } else if (code == ID_SACRIFICE_ACOLYTE) {
        *pit = target;
        *worker = caster;
    } else {
        return false;
    }
    if (!G_UnitIsBuilding((*pit)->class_id) || G_UnitIsBuilding((*worker)->class_id) ||
        G_UnitIsHero(*worker) || ((*worker)->s.renderfx & RF_HIDDEN)) return false;
    /* Counterpart abilities identify the two stock endpoints without coupling
     * the behavior to uaco/usap unit rawcodes. */
    if (!G_UnitAbilityLevel(*pit, ID_SACRIFICE_PIT) ||
        !G_UnitAbilityLevel(*worker, ID_SACRIFICE_ACOLYTE)) return false;
    return true;
}

static BOOL sacrifice_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT pit = NULL, worker = NULL;
    if (!spell || st.type != SPELL_TARGET_UNIT || !st.entity ||
        !sacrifice_pair(caster, st.entity, spell->code, &pit, &worker)) return false;
    /* Warsmash's Sacrifice is a producer queue item and a pit can own only one
     * sacrifice worker.  Do not displace ordinary training/research state. */
    if (pit->build || pit->construction.active || G_BuildingUpgradeActive(pit)) return false;
    return G_UnitBalance(ID_SHADE) != NULL;
}

static void sacrifice_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT pit = NULL, worker = NULL;
    if (!sacrifice_validate(caster, st, spell) ||
        !sacrifice_pair(caster, st.entity, spell->code, &pit, &worker)) return;
    G_QueueSacrifice(pit, worker, ID_SHADE);
}

BZ_VALIDATED_SPELL_PROC(AbilitySacrifice, sacrifice_validate, sacrifice_execute)
