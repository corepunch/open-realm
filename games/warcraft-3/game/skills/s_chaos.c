#include "s_skills.h"

/* Retail Chaos queues its type change after UnitAddAbility returns. The
 * retained authored ability supplies the pending work, including after load. */
BZ_ABILITY_PROC(CAbilityChaos) {
    abilityAliasRef_t ability;
    uint32_t target;

    if (msg != A_UPDATE || !ent || !ent->inuse || M_IsDead(ent))
        return CAbilityPassive(ent, msg, call);
    ability = S_ResolveAbilityAlias(ent, MAKEFOURCC('A','c','h','a'));
    if (!ability.alias || (!G_UnitAbilityResearchAvailable(ent, ability.alias) || !G_AbilityRequirementsSatisfied(ent, ability.alias))) return false;
    target = G_AbilityLevel(ability.alias, ability.level)->unitID;
    if (!target) {
        fprintf(stderr, "Chaos: missing UnitID for %.4s on unit %u\n", (cstring_t)&ability.alias, ent->s.number);
        return false;
    }
    if (ent->class_id == target) { G_ActorRemoveSkill(ent, ability.alias); return true; }
    if (!G_TransformUnitType(ent, target)) {
        fprintf(stderr, "Chaos: cannot transform unit %u from %.4s to %.4s for %.4s\n",
                ent->s.number, (cstring_t)&ent->class_id, (cstring_t)&target, (cstring_t)&ability.alias);
        return false;
    }
    G_ActorRemoveSkill(ent, ability.alias);
    return true;
}
