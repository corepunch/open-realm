#ifndef SC2_UNIT_STATE_H
#define SC2_UNIT_STATE_H

/* Mirrors WC3 edictStat_t: simulation owns current, maximum and regeneration.
 * Galaxy property numbers come from Core.SC2Mod/TriggerLibs/natives.galaxy. */
typedef struct { float value, max_value, regen; } sc2Vital_t;
typedef struct {
    sc2Vital_t vitals[3]; /* life, energy, shields */
    float normal[24], custom[64];
    float speed, height, radius, supplies_used, supplies_made, kills, resources;
    uint32_t states, map_id;
    char type[64];
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
#endif
