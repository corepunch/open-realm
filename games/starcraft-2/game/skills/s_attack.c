/* Attack owns effect execution; the catalog supplies data, never client-side rules. */
#include "../g_sc2_local.h"
#include "../hud/hud.h"

static bool sc2_damage_radius_matches(sc2DamageEffect_t const *effect, float radius) {
    for (int i=0;i<effect->validator_count;i++) {
        sc2RadiusTest_t const *v=&effect->validators[i];
        bool match=false;
        switch (v->compare) {
        case SC2_COMPARE_LT: match=radius<v->value; break;
        case SC2_COMPARE_LE: match=radius<=v->value; break;
        case SC2_COMPARE_EQ: match=radius==v->value; break;
        case SC2_COMPARE_NE: match=radius!=v->value; break;
        case SC2_COMPARE_GE: match=radius>=v->value; break;
        case SC2_COMPARE_GT: match=radius>v->value; break;
        }
        if (!match) return false;
    }
    return true;
}
static bool sc2_damage_filter_matches(edict_t const *source, edict_t const *victim, sc2DamageEffect_t const *effect) {
    uint64_t flags=victim->unit.target_flags;
    if (source==victim) flags|=1ull;
    uint32_t player=source->s.player,other=victim->s.player;
    if (player==other) flags|=(1ull<<1)|(1ull<<2);
    else if (!other) flags|=1ull<<3;
    else if (player<32 && other<32 && (sc2_players[player].alliances[other]&1u)) flags|=1ull<<2;
    else flags|=1ull<<4;
    flags&=~((1ull<<5)|(1ull<<6)); flags|=1ull<<(victim->move.flying ? 5 : 6);
    if (victim->unit.states&(1u<<6)) flags|=1ull<<31;
    if (!SC2_UnitAlive(&victim->unit)) flags|=1ull<<33;
    if (SC2_UnitHidden(&victim->unit)) flags|=1ull<<35;
    if (victim->unit.states&(1u<<SC2_UNIT_INVULNERABLE)) flags|=1ull<<37;
    return (flags&effect->required)==effect->required && !(flags&effect->excluded);
}
static void sc2_damage_unit(edict_t *source, edict_t *victim, float amount) {
    if (!victim->inuse || !victim->unit.initialized || !SC2_UnitAlive(&victim->unit) || amount<=0 ||
        (victim->unit.states&(1u<<SC2_UNIT_INVULNERABLE))) return;
    sc2UnitPresentation_t presentation;
    float armor=SC2_MapUnitPresentation(victim->unit.type,&presentation) ? presentation.armor : 0;
    float old=victim->unit.vitals[0].value,damage=MAX(0.5f,amount-armor);
    SC2_UnitSetProperty(&victim->unit,0,old-damage);
    bool died=!SC2_UnitAlive(&victim->unit);
    SC2_UpdateUnit(victim);
    if (died) source->unit.kills++;
    galaxy_combat_damage(sc2_level.vm,source,victim,MIN(old,damage),died);
}
void SC2_WeaponDamage(edict_t *unit, edict_t *target, sc2WeaponPresentation_t const *weapon) {
    /* Capture impact geometry before death/property events can change the target. */
    vec2_t center=target->s.origin2;
    float radius=target->unit.radius;
    for (int i=0;i<weapon->effect_count;i++) {
        sc2DamageEffect_t const *effect=&weapon->effects[i];
        if (!sc2_damage_radius_matches(effect,radius)) continue;
        sc2_damage_unit(unit,target,effect->amount);
        if (!effect->area_count) continue;
        for (uint32_t j=globals.max_clients;j<globals.num_edicts;j++) {
            edict_t *victim=&globals.edicts[j];
            /* The explicit impact already damages the primary target once. */
            if (victim==target || !victim->inuse || !victim->unit.initialized || !SC2_UnitAlive(&victim->unit) ||
                !sc2_damage_filter_matches(unit,victim,effect)) continue;
            float distance=Vector2_distance(&center,&victim->s.origin2);
            if (effect->offset_by_radius) distance=MAX(0,distance-victim->unit.radius);
            float fraction=0,band_radius=INFINITY;
            for (int a=0;a<effect->area_count;a++) if (distance<=effect->areas[a].radius && effect->areas[a].radius<band_radius) {
                band_radius=effect->areas[a].radius; fraction=effect->areas[a].fraction;
            }
            sc2_damage_unit(unit,victim,effect->amount*fraction);
        }
    }
}
