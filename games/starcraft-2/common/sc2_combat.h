#ifndef SC2_COMBAT_H
#define SC2_COMBAT_H

/* Bounded resolved CEffectSet -> CEffectDamage programs, owned by the game. */
#define SC2_DAMAGE_EFFECTS 8 // leaves per weapon; Liberty Tank uses two
#define SC2_DAMAGE_AREAS 8 // radial bands per damage effect; Liberty Tank uses three
#define SC2_DAMAGE_VALIDATORS 8 // radius predicates along a set/leaf path
enum { SC2_COMPARE_LT, SC2_COMPARE_LE, SC2_COMPARE_EQ, SC2_COMPARE_NE, SC2_COMPARE_GE, SC2_COMPARE_GT };
typedef struct { float radius, fraction; } sc2DamageArea_t;
typedef struct { float value; uint8_t compare; } sc2RadiusTest_t;
typedef struct {
    float amount;
    sc2DamageArea_t areas[SC2_DAMAGE_AREAS];
    sc2RadiusTest_t validators[SC2_DAMAGE_VALIDATORS];
    uint64_t required, excluded;
    uint8_t area_count, validator_count;
    bool exclude_target, offset_by_radius;
} sc2DamageEffect_t;

#endif
