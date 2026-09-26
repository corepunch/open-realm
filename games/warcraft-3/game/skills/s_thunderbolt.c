#include "s_skills.h"

#define ID_FIRE_BOLT MAKEFOURCC('A', 'N', 'f', 'b')
#define ID_STUN_BUFF "Bstu"

static float thunderbolt_missile_speed;
static float firebolt_missile_speed;

static float ConfigNumber(cstring_t classname, cstring_t field) { cstring_t value = FindConfigValue(classname, field); return value ? atof(value) : 0; }

static void thunderbolt_projectile_hit(edict_t * missile);

static umove_t thunderbolt_projectile_move = { "stand", NULL, thunderbolt_projectile_hit, CAbilityThunderBolt };
static umove_t firebolt_projectile_move = { "stand", NULL, thunderbolt_projectile_hit, CAbilityFireBolt };
static umove_t spell_cast_move = { "spell", ai_idle, NULL, CAbilityThunderBolt };

static float bolt_missile_speed(uint32_t code) {
    float speed = code == ID_FIRE_BOLT ? firebolt_missile_speed : thunderbolt_missile_speed;
    return speed > 0 ? speed : 1000;
}

static void thunderbolt_projectile_hit(edict_t * missile) {
    edict_t * target = missile->goalentity;
    edict_t * caster = missile->owner;

    if (S_SpellIsAliveTarget(target)) {
        if (S_SpellDamage(target, caster, missile->damage) && !M_IsDead(target)) {
            unit_addtimedstatus(target, ID_STUN_BUFF, 1, missile->wait);
        }
    }
    G_FreeEdict(missile);
}

static void thunderbolt_execute(edict_t * caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t * target = st.entity;
    uint32_t code = spell->code;
    uint32_t level = S_SpellLevel(caster, code);
    cstring_t art = G_AbilityEffectArt(code, WC3_EFFECT_MISSILE, 0);
    float speed = bolt_missile_speed(code);
    float duration = S_SpellDuration(code, level, S_UnitIsResistant(target));
    edict_t * missile;

    unit_setmove(caster, &spell_cast_move);
    missile = G_Spawn();
    missile->s.origin = caster->s.origin;
    missile->s.angle = caster->s.angle;
    missile->s.model = art ? G_RegisterModel(art) : 0;
    missile->s.player = caster->s.player;
    G_InheritUnitTeamColor(missile, caster);
    missile->goalentity = target;
    missile->owner = caster;
    missile->velocity = speed / 1000.0f;
    missile->damage = (uint32_t)S_SpellData(code, level, 1);
    missile->wait = duration;
    missile->movetype = MOVETYPE_FLYMISSILE;
    G_StartProjectilePresentation(missile);
    missile->currentmove = code == ID_FIRE_BOLT ? &firebolt_projectile_move : &thunderbolt_projectile_move;
}

#define BZ_BOLT_PROC(NAME, SPEED) \
    BZ_ABILITY_PROC(C##NAME) { \
        switch (msg) { \
        case A_INIT: if (call && call->classname) SPEED = ConfigNumber(call->classname, "Missilespeed"); return true; \
        case A_EXECUTE: \
            if (!call || !call->item || !call->target) return false; \
            thunderbolt_execute(ent, *call->target, call->item); return true; \
        default: return CAbilitySimpleSpell(ent, msg, call); \
        } \
    }

BZ_BOLT_PROC(AbilityThunderBolt, thunderbolt_missile_speed)
BZ_BOLT_PROC(AbilityFireBolt, firebolt_missile_speed)
