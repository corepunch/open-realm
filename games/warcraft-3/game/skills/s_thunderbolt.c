#include "s_skills.h"

#define ID_THUNDER_BOLT MAKEFOURCC('A', 'H', 't', 'b')
#define ID_FIRE_BOLT MAKEFOURCC('A', 'N', 'f', 'b')
#define ID_STUN_BUFF "Bstu"

static LPCSTR thunderbolt_missile_art;
static LPCSTR firebolt_missile_art;
static FLOAT thunderbolt_missile_speed;
static FLOAT firebolt_missile_speed;

static void thunderbolt_projectile_hit(LPEDICT missile);

static umove_t thunderbolt_projectile_move = { "stand", NULL, thunderbolt_projectile_hit, &a_thunderbolt };
static umove_t firebolt_projectile_move = { "stand", NULL, thunderbolt_projectile_hit, &a_firebolt };
static umove_t spell_cast_move = { "spell", ai_idle, NULL, &a_thunderbolt };

static LPCSTR bolt_missile_art(DWORD code) {
    return code == ID_FIRE_BOLT ? firebolt_missile_art : thunderbolt_missile_art;
}

static FLOAT bolt_missile_speed(DWORD code) {
    FLOAT speed = code == ID_FIRE_BOLT ? firebolt_missile_speed : thunderbolt_missile_speed;
    return speed > 0 ? speed : 1000;
}

static void thunderbolt_projectile_hit(LPEDICT missile) {
    LPEDICT target = missile->goalentity;
    LPEDICT caster = missile->owner;

    if (S_SpellIsAliveTarget(target)) {
        T_Damage(target, caster, missile->damage);
        if (!M_IsDead(target)) {
            unit_addtimedstatus(target, ID_STUN_BUFF, 1, missile->wait);
        }
    }
    G_FreeEdict(missile);
}

static void thunderbolt_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    LPEDICT target = st.entity;
    DWORD code = spell->code;
    DWORD level = S_SpellLevel(caster, code);
    LPCSTR art = bolt_missile_art(code);
    FLOAT speed = bolt_missile_speed(code);
    FLOAT duration = S_SpellDuration(code, level, UNIT_LEVEL(target->class_id) >= 5);
    LPEDICT missile;

    unit_setmove(caster, &spell_cast_move);
    missile = G_Spawn();
    missile->s.origin = caster->s.origin;
    missile->s.angle = caster->s.angle;
    missile->s.model = art ? G_RegisterModel(art) : 0;
    missile->goalentity = target;
    missile->owner = caster;
    missile->velocity = speed / 1000.0f;
    missile->damage = (DWORD)S_SpellData(code, level, 1);
    missile->wait = duration;
    missile->movetype = MOVETYPE_FLYMISSILE;
    missile->currentmove = code == ID_FIRE_BOLT ? &firebolt_projectile_move : &thunderbolt_projectile_move;
}

static void SP_ability_thunderbolt(LPCSTR classname, ability_t *self) {
    (void)self;
    thunderbolt_missile_art = FindConfigValue(classname, "Missileart");
    thunderbolt_missile_speed = AB_Number(classname, "Missilespeed");
}

static void SP_ability_firebolt(LPCSTR classname, ability_t *self) {
    (void)self;
    firebolt_missile_art = FindConfigValue(classname, "Missileart");
    firebolt_missile_speed = AB_Number(classname, "Missilespeed");
}

static spell_info_t spell_thunderbolt = {
    .code = ID_THUNDER_BOLT,
    .name = "Thunder Bolt",
    .target_type = SPELL_TARGET_UNIT,
    .execute = thunderbolt_execute,
};

static spell_info_t spell_firebolt = {
    .code = ID_FIRE_BOLT,
    .name = "Fire Bolt",
    .target_type = SPELL_TARGET_UNIT,
    .execute = thunderbolt_execute,
};

ability_t a_thunderbolt = {
    .init = SP_ability_thunderbolt,
    .cmd = spell_cmd,
    .spell = &spell_thunderbolt,
};

ability_t a_firebolt = {
    .init = SP_ability_firebolt,
    .cmd = spell_cmd,
    .spell = &spell_firebolt,
};
