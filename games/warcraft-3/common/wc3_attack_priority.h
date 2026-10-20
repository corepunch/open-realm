#ifndef WC3_ATTACK_PRIORITY_H
#define WC3_ATTACK_PRIORITY_H

#include <stdbool.h>
#include <stdint.h>

/* Ordinary49d680 priorities, before the independent TownAI additions.
 * These are native packed ordering fields, not tuning weights. */
typedef enum {
    WC3_ATTACK_TARGET_IDLE,
    WC3_ATTACK_TARGET_SELF,
    WC3_ATTACK_TARGET_ALLY,
    WC3_ATTACK_TARGET_NEUTRAL,
    WC3_ATTACK_TARGET_ENEMY
} wc3AttackTargetRelation_t;

typedef struct {
    bool mobile, both_flying, fortified, armed, counterattack;
    bool low_level_neutral, in_range, retained;
    wc3AttackTargetRelation_t relation;
} wc3AttackPriority_t;

static inline uint64_t wc3_attack_priority(wc3AttackPriority_t const *target) {
    enum {
        MOBILE=0x20000000, BOTH_FLYING=0x00200000, UNFORTIFIED=0x40000000,
        DEFENDING_SELF=0x00800000, DEFENDING_ALLY=0x00400000,
        COUNTERATTACK=0x08000000, IN_RANGE=0x04000000, RETAINED_IN_RANGE=0x01000000,
        BASE=0x03800000, ARMED=0x00400000, NOT_ATTACKING_ENEMY=0x08000000,
        NOT_ATTACKING_NEUTRAL=0x00200000
    };
    uint32_t low=target->mobile ? MOBILE : 0, high=BASE;
    if(target->mobile && target->both_flying)low|=BOTH_FLYING;
    if(!target->fortified)low|=UNFORTIFIED;
    if(target->armed) {
        high|=ARMED;
        if(target->relation==WC3_ATTACK_TARGET_SELF)low|=DEFENDING_SELF|DEFENDING_ALLY;
        else if(target->relation==WC3_ATTACK_TARGET_ALLY)low|=DEFENDING_ALLY;
    }
    if(!target->armed || target->relation==WC3_ATTACK_TARGET_IDLE ||
       target->relation==WC3_ATTACK_TARGET_SELF || target->relation==WC3_ATTACK_TARGET_ALLY)
        high|=NOT_ATTACKING_ENEMY|NOT_ATTACKING_NEUTRAL;
    else if(target->relation==WC3_ATTACK_TARGET_NEUTRAL)high|=NOT_ATTACKING_ENEMY;
    else high|=NOT_ATTACKING_NEUTRAL;
    if(target->low_level_neutral)high&=~NOT_ATTACKING_NEUTRAL;
    if(target->counterattack)low|=COUNTERATTACK;
    if(target->in_range) {
        low|=IN_RANGE;
        if(target->retained)low|=RETAINED_IN_RANGE;
    }
    return ((uint64_t)high<<32)|low;
}
#endif
