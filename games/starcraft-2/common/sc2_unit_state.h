#ifndef SC2_UNIT_STATE_H
#define SC2_UNIT_STATE_H

/* Mirrors WC3 edictStat_t: simulation owns current, maximum and regeneration.
 * Galaxy property numbers come from Core.SC2Mod/TriggerLibs/natives.galaxy. */
typedef struct { float value, max_value, regen; } sc2Vital_t;

/* CUnit AbilArray/WeaponArray/BehaviorArray entries, resolved through catalog parents at spawn. */
#define SC2_LINK_LEN       48 // chars; longest stock Liberty CUnit array link is 41
#define SC2_UNIT_ABILS     20 // slots; stock Liberty CUnit maximum is 18
#define SC2_UNIT_WEAPONS    4 // slots; stock Liberty CUnit maximum is 2
#define SC2_UNIT_BEHAVIORS  8 // slots; stock catalog maximum is 4, the rest are script-added
#define SC2_UNIT_COOLDOWNS  8 // cooldown/charge links a script has touched on one unit
enum { SC2_LINK_ABIL, SC2_LINK_WEAPON, SC2_LINK_BEHAVIOR, SC2_LINK_KINDS };

typedef struct { char link[SC2_LINK_LEN]; uint8_t level; bool disabled, hidden; } sc2UnitAbil_t;
typedef struct { char link[SC2_LINK_LEN]; bool disabled; } sc2UnitWeapon_t;
/* duration is seconds left; 0 never expires. Stack counts share one slot per link. */
typedef struct { char link[SC2_LINK_LEN]; int32_t count; float duration; } sc2UnitBehavior_t;
/* Cooldown and charge links are global catalog ids, so the unit, ability, and behavior
 * variants of the natives address one table. charge_regen counts down to one charge back. */
typedef struct { char link[SC2_LINK_LEN]; float cooldown, charge_used, charge_regen; } sc2UnitCooldown_t;

typedef struct {
    sc2Vital_t vitals[3]; /* life, energy, shields */
    float normal[24], custom[64];
    float speed, height, radius, supplies_used, supplies_made, kills, resources;
    uint32_t states, map_id;
    uint64_t target_flags; /* CUnit attributes/flags/planes, ETargetFilter bits */
    uint32_t ai_options; /* c_unitAIOption* bits; stored for the AI, which does not read them yet */
    char type[64];
    sc2UnitAbil_t abils[SC2_UNIT_ABILS];
    sc2UnitWeapon_t weapons[SC2_UNIT_WEAPONS];
    sc2UnitBehavior_t behaviors[SC2_UNIT_BEHAVIORS];
    sc2UnitCooldown_t cooldowns[SC2_UNIT_COOLDOWNS];
    uint8_t abil_n, weapon_n, behavior_n, cooldown_n;
    bool initialized;
} sc2UnitState_t;

enum { SC2_UNIT_INVULNERABLE=8, SC2_UNIT_PAUSED=9, SC2_UNIT_HIDDEN=10,
       SC2_UNIT_SELECTABLE=17, SC2_UNIT_DEAD=22, SC2_UNIT_MOVE_SUPPRESSED=24, SC2_UNIT_TURN_SUPPRESSED=25 };
static inline bool SC2_UnitAlive(sc2UnitState_t const *u) { return u && u->vitals[0].value > 0; }
static inline float SC2_UnitProperty(sc2UnitState_t const *u, int prop) {
    if (prop < 0 || prop >= 24) return 0;
    if (prop < 12) {
        sc2Vital_t const *v = &u->vitals[prop/4];
        switch (prop%4) { case 0: return v->value; case 1: return v->max_value > 0 ? 100*v->value/v->max_value : 0;
        case 2: return v->max_value; default: return v->regen; }
    }
    switch (prop) {
    case 12: return u->supplies_used; case 13: return u->supplies_made; case 14: return u->kills;
    case 15: return u->vitals[0].value + u->vitals[2].value;
    case 16: { float max = u->vitals[0].max_value+u->vitals[2].max_value;
        return max > 0 ? 100*(u->vitals[0].value+u->vitals[2].value)/max : 0; }
    case 17: return u->vitals[0].max_value+u->vitals[2].max_value;
    case 19: return u->height; case 20: return u->speed; case 22: return u->resources; case 23: return u->radius;
    default: return u->normal[prop];
    }
}
static inline bool SC2_UnitSetProperty(sc2UnitState_t *u, int prop, float value) {
    if (!isfinite(value)) return false;
    if (prop >= 0 && prop < 12) {
        sc2Vital_t *v = &u->vitals[prop/4];
        switch (prop%4) {
        case 0: v->value = MAX(0,MIN(v->max_value,value)); break;
        case 1: v->value = v->max_value*MAX(0,MIN(100,value))/100; break;
        case 2: v->max_value = MAX(0,value); v->value = MIN(v->value,v->max_value); break;
        case 3: v->regen = value; break;
        }
        return true;
    }
    switch (prop) {
    case 14: u->kills = MAX(0,value); break;
    case 19: u->height = value; break;
    case 20: u->speed = MAX(0,value); break;
    case 22: u->resources = MAX(0,value); break;
    default: return false;
    }
    return true;
}
static inline void SC2_UnitRegenerate(sc2UnitState_t *u, float seconds) {
    if (!SC2_UnitAlive(u) || (u->states & (1u<<SC2_UNIT_PAUSED))) return;
    for (int i=0; i<3; i++) SC2_UnitSetProperty(u,i*4,u->vitals[i].value+seconds*u->vitals[i].regen);
}
/* Cooldowns, charge regeneration, and timed behaviors freeze with the unit, like vitals.
 * A charge regen reaching zero returns one used charge; without CAbil charge data it does not restart. */
static inline void SC2_UnitAdvanceTimers(sc2UnitState_t *u, float seconds) {
    if (!SC2_UnitAlive(u) || (u->states & (1u<<SC2_UNIT_PAUSED))) return;
    for (int i=0; i<u->cooldown_n; i++) {
        sc2UnitCooldown_t *c=&u->cooldowns[i];
        c->cooldown=MAX(0,c->cooldown-seconds);
        if (c->charge_regen>0 && (c->charge_regen-=seconds)<=0) { c->charge_regen=0; c->charge_used=MAX(0,c->charge_used-1); }
    }
    for (int i=0; i<u->behavior_n; ) {
        sc2UnitBehavior_t *b=&u->behaviors[i];
        if (b->duration>0 && (b->duration-=seconds)<=0) {
            memmove(b,b+1,(size_t)(--u->behavior_n-i)*sizeof(*b)); continue; /* keep UnitBehaviorGet order */
        }
        i++;
    }
}
#endif
