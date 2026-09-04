#include "s_skills.h"

#define ID_BLIZZARD MAKEFOURCC('A', 'H', 'b', 'z')
#define ID_CARRION_SWARM MAKEFOURCC('A', 'U', 'c', 's')

/* Deal ent->damage to every enemy within ent->collision of ent.  maxtotal > 0
 * caps the combined damage of this burst (WC3 "Max Damage" / "Maximum Damage per
 * Wave"): when damage*targets would exceed it, the per-target damage is scaled
 * down so the total lands on the cap. */
static void area_spell_damage(LPEDICT ent, FLOAT maxtotal) {
    LPEDICT caster = ent->owner;
    FLOAT radius = ent->collision;
    FLOAT damage = (FLOAT)ent->damage;
    DWORD ntargets = 0;

#define AREA_HITS(t) ((t)->inuse && (t) != caster && S_SpellIsAliveTarget(t) && \
                      S_SpellIsEnemy(caster, t) &&                              \
                      Vector2_distance(&(t)->s.origin2, &ent->s.origin2) <= radius)

    if (maxtotal > 0.0f) {
        FILTER_EDICTS(target, AREA_HITS(target)) {
            ntargets++;
        }
        if (ntargets > 0 && damage * (FLOAT)ntargets > maxtotal) {
            damage = MAX(1.0f, maxtotal / (FLOAT)ntargets);
        }
    }
    FILTER_EDICTS(target, AREA_HITS(target)) {
        T_Damage(target, caster, (DWORD)damage);
    }
#undef AREA_HITS
}

void blizzard_think(LPEDICT ent) {
    DWORD now = G_Time();

    if (ent->freetime && now < ent->freetime)
        return;
    area_spell_damage(ent, ent->velocity); /* velocity reused: max damage per wave */
    if (ent->resources > 0)
        ent->resources--;
    if (ent->resources == 0 || (ent->spawn_time && now >= ent->spawn_time)) {
        LPEDICT caster = ent->owner;
        G_FreeEdict(ent);
        if (caster && caster->channel.code == ID_BLIZZARD)
            S_SpellCancelChannel(caster);
        return;
    }
    ent->freetime = now + 1000;
}

/* Blizzard: channeled point-target AoE.  SPELL_CHANNEL flag causes the unified
 * pipeline to lock the caster via channel_code/cast_origin; spell_run_frame()
 * enforces movement-cancel.  The thinker entity runs the per-wave damage. */
static void blizzard_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD waves = (DWORD)S_SpellData(spell->code, level, 1);
    DWORD damage = (DWORD)S_SpellData(spell->code, level, 2);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    LPEDICT thinker;

    thinker = G_Spawn();
    thinker->owner = caster;
    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->collision = area > 0 ? area : 200.0f;
    thinker->damage = damage ? damage : 1;
    thinker->resources = waves ? waves : 1;
    thinker->velocity = S_SpellData(spell->code, level, 6); /* DataF = Max Damage per Wave */
    thinker->spawn_time = G_Time() + (DWORD)(MAX(1.0f, S_SpellDuration(spell->code, level, false)) * 1000.0f);
    thinker->think = blizzard_think;
    blizzard_think(thinker); /* first wave immediately */
}

static spell_info_t spell_blizzard = {
    .code = ID_BLIZZARD,
    .name = "Blizzard",
    .target_type = SPELL_TARGET_POINT,
    .flags = SPELL_CHANNEL,
    .execute = blizzard_execute,
};

/* Carrion Swarm: instant point-target AoE blast. */
static void carrion_swarm_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT blast;

    blast = G_Spawn();
    blast->owner = caster;
    blast->s.origin2 = st.point;
    blast->collision = MAX(96.0f, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level));
    blast->damage = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 1));
    area_spell_damage(blast, S_SpellData(spell->code, level, 2)); /* DataB = Max Damage */
    G_FreeEdict(blast);
}

static spell_info_t spell_carrion_swarm = {
    .code = ID_CARRION_SWARM,
    .name = "Carrion Swarm",
    .target_type = SPELL_TARGET_POINT,
    .execute = carrion_swarm_execute,
};

ability_t a_blizzard = {
    .cmd = spell_cmd,
    .spell = &spell_blizzard,
};

ability_t a_carrion_swarm = {
    .cmd = spell_cmd,
    .spell = &spell_carrion_swarm,
};

static void channel_test_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    S_SpellCursorSplat(clent, 200.0f);
}

/* a_channel_test remains a non-spell ability for ad-hoc testing. */
ability_t a_channel_test = {
    .cmd = channel_test_command,
};
