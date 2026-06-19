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

static void blizzard_think(LPEDICT ent) {
    DWORD now = gi.GetTime();

    if (ent->freetime && now < ent->freetime) {
        return;
    }
    area_spell_damage(ent, ent->velocity); /* velocity reused: max damage per wave */
    if (ent->resources > 0) {
        ent->resources--;
    }
    if (ent->resources == 0 || (ent->spawn_time && now >= ent->spawn_time)) {
        G_FreeEdict(ent);
        return;
    }
    ent->freetime = now + 1000;
}

static BOOL blizzard_selectlocation(LPEDICT clent, LPCVECTOR2 point) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_BLIZZARD);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT range = S_SpellRange(code, level);
    FLOAT area = S_SpellNumber(code, "Area", level);
    DWORD waves = (DWORD)S_SpellData(code, level, 1);
    DWORD damage = (DWORD)S_SpellData(code, level, 2);
    LPEDICT thinker;

    if (!caster || !point) {
        return false;
    }
    if (range > 0 && Vector2_distance(&caster->s.origin2, point) > range) {
        return false;
    }
    if (!S_SpellCooldownReady(caster, code) || !S_SpellSpendMana(caster, code, level)) {
        return false;
    }
    S_SpellStartCooldown(caster, code, level);

    thinker = G_Spawn();
    thinker->owner = caster;
    thinker->s.origin2 = *point;
    thinker->s.origin.x = point->x;
    thinker->s.origin.y = point->y;
    thinker->collision = area > 0 ? area : 200.0f;
    thinker->damage = damage ? damage : 1;
    thinker->resources = waves ? waves : 1;
    /* DataF (HBZ6) = Maximum Damage per Wave; stash on the stationary thinker's
     * (otherwise unused) velocity field so each wave can apply the cap. */
    thinker->velocity = S_SpellData(code, level, 6);
    thinker->spawn_time = gi.GetTime() + (DWORD)(MAX(1.0f, S_SpellDuration(code, level, false)) * 1000.0f);
    thinker->think = blizzard_think;
    blizzard_think(thinker);
    S_SpellCursorSplat(clent, 0.0f);
    return true;
}

static void blizzard_command(LPEDICT clent) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_BLIZZARD);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT area = S_SpellNumber(code, "Area", level);

    UI_AddCancelButton(clent);
    S_SpellCursorSplat(clent, area > 0.0f ? area : 200.0f);
    clent->client->menu.on_location_selected = blizzard_selectlocation;
}

static BOOL carrion_swarm_selectlocation(LPEDICT clent, LPCVECTOR2 point) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_CARRION_SWARM);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT range = S_SpellRange(code, level);
    LPEDICT blast;

    if (!caster || !point) {
        return false;
    }
    if (range > 0 && Vector2_distance(&caster->s.origin2, point) > range) {
        return false;
    }
    if (!S_SpellCooldownReady(caster, code) || !S_SpellSpendMana(caster, code, level)) {
        return false;
    }
    S_SpellStartCooldown(caster, code, level);

    blast = G_Spawn();
    blast->owner = caster;
    blast->s.origin2 = *point;
    blast->collision = MAX(96.0f, S_SpellNumber(code, "Area", level));
    blast->damage = (DWORD)MAX(1.0f, S_SpellData(code, level, 1));
    area_spell_damage(blast, S_SpellData(code, level, 2)); /* DataB (UCS2) = Max Damage */
    G_FreeEdict(blast);
    S_SpellCursorSplat(clent, 0.0f);
    return true;
}

static void carrion_swarm_command(LPEDICT clent) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_CARRION_SWARM);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT area = S_SpellNumber(code, "Area", level);

    UI_AddCancelButton(clent);
    S_SpellCursorSplat(clent, area > 0.0f ? area : 96.0f);
    clent->client->menu.on_location_selected = carrion_swarm_selectlocation;
}

static void channel_test_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    S_SpellCursorSplat(clent, 200.0f);
}

ability_t a_blizzard = {
    .cmd = blizzard_command,
};

ability_t a_carrion_swarm = {
    .cmd = carrion_swarm_command,
};

ability_t a_channel_test = {
    .cmd = channel_test_command,
};
