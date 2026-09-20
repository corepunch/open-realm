#include "s_skills.h"

/* Devotion Aura is a learned passive.  The shared Hero-aura cache in
 * s_hero_passives.c reads its authored Area/DataA and G_UnitArmorValue()
 * consumes the strongest in-range contribution. */
BZ_ABILITY_PROC(CAbilityAuraDevotion) {
    return CAbilityPassive(ent, msg, call);
}
