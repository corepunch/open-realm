#include "s_skills.h"

/* Night Elf Warden (Maiev) hero abilities: Blink, Fan of Knives, Shadow Strike. */

#define ID_BLINK        MAKEFOURCC('A', 'E', 'b', 'l')
#define ID_FAN_OF_KNIVES MAKEFOURCC('A', 'E', 'f', 'k')
#define ID_SHADOW_STRIKE MAKEFOURCC('A', 'E', 's', 'h')

/* ---- Blink (AEbl): instant teleport to a target point within range -------- */

static BOOL blink_validate(LPEDICT caster, spellTarget_t st) {
    DWORD level = S_SpellLevel(caster, ID_BLINK);
    FLOAT maxrange = S_SpellData(ID_BLINK, level, 1);
    FLOAT minrange = S_SpellData(ID_BLINK, level, 2);
    FLOAT dist = Vector2_distance(&caster->s.origin2, &st.point);

    if (maxrange > 0 && dist > maxrange) return false;
    if (minrange > 0 && dist < minrange) return false;
    return true;
}

static void blink_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);

    S_SpellSpawnTargetArt(caster, S_SpellString(spell->code, "Specialart", level));
    VECTOR2 dest = st.point;
    CM_ClosestPathablePointForRadius(&st.point, caster->collision, &dest);
    caster->s.origin2 = dest;
    caster->s.origin.x = dest.x;
    caster->s.origin.y = dest.y;
    gi.LinkEntity(caster);
    S_SpellSpawnTargetArt(caster, S_SpellString(spell->code, "Areaeffectart", level));
}

static spell_info_t spell_blink = {
    .code = ID_BLINK,
    .name = "Blink",
    .target_type = SPELL_TARGET_POINT,
    .validate = blink_validate,
    .execute = blink_execute,
};

/* ---- Fan of Knives (AEfk): instant area damage centred on the caster ------ */

static void fanofknives_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT radius = S_SpellNumber(spell->code, "Area", level);
    FLOAT damage = MAX(1.0f, S_SpellData(spell->code, level, 1));
    FLOAT maxtotal = S_SpellData(spell->code, level, 2);
    DWORD ntargets = 0;

    if (!caster) return;
    if (radius <= 0.0f) radius = 400.0f;

#define FOK_HITS(t) ((t)->inuse && (t) != caster && S_SpellIsEnemy(caster, t) && \
                     S_SpellAllowsTarget(spell->code, caster, t) &&               \
                     Vector2_distance(&(t)->s.origin2, &caster->s.origin2) <= radius)

    FILTER_EDICTS(target, FOK_HITS(target))
        ntargets++;
    if (maxtotal > 0.0f && ntargets > 0 && damage * (FLOAT)ntargets > maxtotal)
        damage = MAX(1.0f, maxtotal / (FLOAT)ntargets);
    FILTER_EDICTS(target, FOK_HITS(target))
        T_Damage(target, caster, (DWORD)damage);
#undef FOK_HITS
}

static spell_info_t spell_fan_of_knives = {
    .code = ID_FAN_OF_KNIVES,
    .name = "Fan of Knives",
    .target_type = SPELL_TARGET_NONE,
    .execute = fanofknives_execute,
};

/* ---- Shadow Strike (AEsh): single-target nuke ----------------------------- */

static void shadowstrike_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    LPEDICT target = st.entity;
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD damage = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 5)); /* DataE = Initial Damage */

    T_Damage(target, caster, damage);
    /* TODO(1:1): Shadow Strike also applies a movement slow and a decaying
     * poison DoT via the BEsh buff.  The status system needs a movement-speed
     * modifier and a periodic-damage tick first. */
}

static spell_info_t spell_shadow_strike = {
    .code = ID_SHADOW_STRIKE,
    .name = "Shadow Strike",
    .target_type = SPELL_TARGET_UNIT,
    .execute = shadowstrike_execute,
};

/* ---- Registration -------------------------------------------------------- */

static void SP_ability_noop(LPCSTR classname, ability_t *self) {
    (void)classname;
    (void)self;
}

ability_t a_blink = {
    .init = SP_ability_noop,
    .cmd = spell_cmd,
    .spell = &spell_blink,
};

ability_t a_fan_of_knives = {
    .init = SP_ability_noop,
    .cmd = spell_cmd,
    .spell = &spell_fan_of_knives,
};

ability_t a_shadow_strike = {
    .init = SP_ability_noop,
    .cmd = spell_cmd,
    .spell = &spell_shadow_strike,
};
