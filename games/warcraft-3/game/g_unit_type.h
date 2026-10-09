#ifndef G_UNIT_TYPE_H
#define G_UNIT_TYPE_H

/* Immutable row identity and parsed defaults live for the level. Resource
 * bindings are derived and may be rebuilt without changing instance state. */
typedef struct {
    UnitUI_t const *row;
    uint32_t class_id, metadata, model, splat;
    uint64_t revision;
#ifndef USE_SHADOWMAPS
    uint32_t shadow, shadow_rect;
#endif
    bool valid, building, shadow_set;
} unitVisualResources_t;
typedef struct {
    uint32_t class_id, metadata, abilities, flags, runtime;
    UnitBalance_t const *balance;
    UnitData_t const *data;
    UnitUI_t const *ui;
    float collision;
    TARGTYPE target;
    bool valid;
} unitSpawnTraits_t;
/* Text classification is authored per row, not per unit. Resolve at the
 * original combat phase, after initialization callbacks have run. */
typedef struct {
    UnitBalance_t const *balance;
    UnitWeapons_t const *weapons;
    cstring_t names[5];
    uint32_t generation, defense, attack[2], weapon[2];
    unitAttack_t const *defaults[2];
    bool valid;
} unitCombatTypes_t;

typedef struct {
    uint32_t class_id, metadata, slots;
    uint64_t revision;
    vec3_t origin;
    unitAttack_t const *base_profiles[2], *complete_profiles[2];
    struct { uint32_t model; float arc, speed; } projectile[2];
    bool valid;
} unitProjectileResources_t;
typedef struct {
    UnitUI_t const *ui;
    UnitWeapons_t const *weapons;
    uint32_t class_id, metadata, catalog;
    uint64_t revision;
    unitSoundProfile_t const *profile;
    bool valid;
} unitSoundResources_t;

typedef struct {
    UnitProfile_t const *row;
    cstring_t authored;
    unitAnimationText_t const *properties;
    uint32_t metadata;
    bool valid;
} unitAnimationDefaults_t;

typedef struct {
    unitAnimationDefaults_t animation;
    unitSpawnTraits_t traits;
    unitCombatTypes_t combat;
    unitVisualResources_t visuals;
    unitProjectileResources_t projectiles;
    unitSoundResources_t sounds;
} unitTypeBindings_t;

typedef struct unitRuntimeType_s {
    uint32_t rawcode, version, ability_version;
    edictData_s data;
    unitTypeBindings_t bindings;
    struct unitEventPlan_s const *initialization;
    uint32_t initialization_epoch;
    struct unitRuntimeType_s *hash_next, *allocated_next;
} unitRuntimeType_t;

unitRuntimeType_t *G_UnitRuntimeType(uint32_t rawcode);
void G_ClearUnitRuntimeTypes(void);
void G_ResetUnitTypeBindings(void);
#ifdef BZ_TESTS
uint32_t G_TestUnitTypeResolutions(bool reset);
#endif
#endif
