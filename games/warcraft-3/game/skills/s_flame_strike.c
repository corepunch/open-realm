#include "s_skills.h"

/* Flame Strike (ANfs): delayed AoE point-target spell.  Creates a fire patch at
 * the target location that deals initial damage after a short delay, then
 * periodic burn damage to units in the area. */

#define ID_FLAME_STRIKE MAKEFOURCC('A', 'N', 'f', 's')

void flame_strike_tick(LPEDICT ent) {
    LPEDICT caster = ent->owner;
    FLOAT radius = ent->collision;
    DWORD now = G_Time();

    if (ent->freetime && now < ent->freetime)
        return;

#define FLAME_HITS(t) ((t)->inuse && S_SpellIsAliveTarget(t) &&             \
                       S_SpellIsEnemy(caster, t) &&                          \
                       Vector2_distance(&(t)->s.origin2, &ent->s.origin2) <= radius)

    /* Initial burst damage on first tick only. */
    if (ent->resources & 1) {/* resources bit 0 = initial burst pending */
        FILTER_EDICTS(target, FLAME_HITS(target))
            T_Damage(target, caster, ent->damage);
        ent->resources &= ~1;
    }
    /* Per-tick burn damage. */
    FILTER_EDICTS(target, FLAME_HITS(target))
        T_Damage(target, caster, ent->velocity); /* velocity = burn damage per tick */
#undef FLAME_HITS

    if (ent->spawn_time && now >= ent->spawn_time) {
        G_FreeEdict(ent);
        return;
    }
    /* Ticks every second after the initial delay. */
    if (!ent->freetime)
        ent->freetime = now + 1000;
}

static void flame_strike_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT delay = S_SpellData(spell->code, level, 1); /* DataA = Cast time / delay before flame (seconds) */
    DWORD initial_damage = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 2)); /* DataB = Initial Damage */
    DWORD burn_damage = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 3)); /* DataC = Damage Per Second */
    DWORD burn_ticks = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 4)); /* DataD = Full Damage Interval (ticks) */
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    LPEDICT thinker;

    thinker = G_Spawn();
    thinker->owner = caster;
    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->collision = area > 0 ? area : 200.0f;
    thinker->damage = initial_damage;
    thinker->velocity = (FLOAT)burn_damage; /* stash burn per tick */
    thinker->resources = 1; /* bit 0: initial burst pending */
    /* Freetime = G_Time() + delay_ms so the first tick fires after delay. */
    thinker->freetime = G_Time() + (DWORD)(MAX(0.1f, delay) * 1000.0f);
    thinker->spawn_time = thinker->freetime + burn_ticks * 1000;
    thinker->think = flame_strike_tick;
}

static spell_info_t spell_flame_strike = {
    .code = ID_FLAME_STRIKE,
    .name = "Flame Strike",
    .target_type = SPELL_TARGET_POINT,
    .execute = flame_strike_execute,
};

ability_t a_flame_strike = {
    .cmd = spell_cmd,
    .spell = &spell_flame_strike,
};
