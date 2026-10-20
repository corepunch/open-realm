#include "s_skills.h"

unitAttack_t const unit_attack_empty[2];

typedef struct attackProfileRecord_s {
    unitAttack_t value;
    unsigned slot;
    struct attackProfileRecord_s *next;
} attackProfileRecord_t;
static attackProfileRecord_t *attack_profiles[1024];

unitAttack_t *S_AttackProfileWrite(edict_t *ent, unsigned slot) {
    assert(ent && slot < 2);
    if (!ent->attack_overrides[slot]) {
        unitAttack_t const *source = S_AttackProfileRead(ent, slot);
        unitAttack_t *owned = slot ? G_AllocAttackTwo() : G_AllocAttackOne();
        *owned = *source;
        ent->attack_overrides[slot] = owned;
    }
    return ent->attack_overrides[slot];
}

/* Only preparation and restore intern profiles. Ordinary readers and gameplay
 * writers never hash profiles or modify another unit's configuration. */
unitAttack_t const *S_InternAttackProfile(unitAttack_t const *value, unsigned slot) {
    assert(slot < 2);
    _Static_assert(sizeof(unitAttack_t) == 104, "Profile interning requires scalar fields without padding");
    if (!memcmp(value, unit_attack_empty + slot, sizeof(*value))) return unit_attack_empty + slot;
    uint32_t hash = 2166136261u ^ slot;
    uint8_t const *bytes = (uint8_t const *)value;
    FOR_LOOP(i, sizeof(*value)) hash = (hash ^ bytes[i]) * 16777619u;
    attackProfileRecord_t **head = attack_profiles + (hash & 1023);
    for (attackProfileRecord_t *record = *head; record; record = record->next)
        if (record->slot == slot && !memcmp(&record->value, value, sizeof(*value))) return &record->value;
    attackProfileRecord_t *record = gi.MemAlloc(sizeof(*record));
    record->value = *value;
    record->slot = slot;
    record->next = *head;
    *head = record;
    return &record->value;
}

unitAttack_t const *S_CompileAttackProfile(UnitWeapon_t const *row, unsigned slot, uint32_t type, uint32_t weapon) {
    unitAttack_t value = {
        .type = type, .weapon = weapon, .damageBase = row->damageBase,
        .numberOfDice = row->damageDice, .sidesPerDie = row->damageSides,
        .cooldown = row->cooldown, .damagePoint = row->damagePoint,
        .backswingPoint = row->backswingPoint, .range = row->range,
        .rangeBuffer = row->rangeBuffer, .targetsAllowed = (uint32_t)row->targetsAllowed,
        .areaFull = row->areaFull, .areaMedium = row->areaMedium, .areaSmall = row->areaSmall,
        .factorMedium = row->factorMedium, .factorSmall = row->factorSmall,
        .maxTargets = row->maxTargets, .damageLoss = row->damageLossFactor
    };
    return S_InternAttackProfile(&value, slot);
}

void S_AttackApplyDefaults(edict_t *ent, unsigned slot, unitAttack_t const *defaults) {
    if (!ent->attack_profiles[slot] && !ent->attack_overrides[slot]) {
        ent->attack_profiles[slot] = defaults;
        return;
    }
    unitAttack_t const *old = S_AttackProfileRead(ent, slot);
    unitAttack_t value = *defaults;
    /* Initialization assigns authored configuration but retains pre-existing
     * runtime bonuses and projectile state, including callback mutations. */
    value.origin = old->origin;
    value.permanentDamageBonus = old->permanentDamageBonus;
    value.temporaryDamageBonus = old->temporaryDamageBonus;
    value.projectile = old->projectile;
    if (ent->attack_overrides[slot]) *ent->attack_overrides[slot] = value;
    else ent->attack_profiles[slot] = !memcmp(&value, defaults, sizeof(value)) ? defaults : S_InternAttackProfile(&value, slot);
}

void S_ClearAttackProfiles(void) {
    FOR_LOOP(i, sizeof(attack_profiles) / sizeof(*attack_profiles)) {
        while (attack_profiles[i]) {
            attackProfileRecord_t *record = attack_profiles[i];
            attack_profiles[i] = record->next;
            gi.MemFree(record);
        }
    }
}
