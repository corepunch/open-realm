#include "s_skills.h"

/* DataE leash: factory-owned Clockwerks beyond range are ordered back to the factory. */
static void pocket_factory_leash(edict_t *factory, uint32_t clockwerk, float leash) {
    if (!factory || leash <= 0.0f || !clockwerk) return;
    FILTER_EDICTS(goblin, goblin->inuse && goblin->owner == factory && goblin->class_id == clockwerk && !M_IsDead(goblin)) {
        edict_t *goal;
        if (Vector2_distance(&goblin->s.origin2, &factory->s.origin2) <= leash) continue;
        goal = goblin->goalentity;
        if (goal && goal->inuse && Vector2_distance(&goal->s.origin2, &factory->s.origin2) < 1.0f) continue;
        order_move(goblin, Waypoint_add(&factory->s.origin2));
    }
}

/* Pocket Factory's classless thinker follows the factory slot, so removing or reusing the factory cannot keep production alive. */
void pocket_factory_think(edict_t *thinker) {
    edict_t *factory = thinker->owner;
    vec2_t loc;
    if (!factory || !factory->inuse || G_Time() >= thinker->spawn_time) { G_FreeEdict(thinker); return; }
    pocket_factory_leash(factory, thinker->damage, thinker->velocity);
    if (G_Time() < thinker->freetime) return;
    loc = factory->s.origin2;
    if (thinker->collision > 0.0f) {
        loc.x += cosf(factory->s.angle) * thinker->collision;
        loc.y += sinf(factory->s.angle) * thinker->collision;
    }
    S_SummonAt(factory, thinker->damage, &loc, thinker->wait);
    thinker->freetime = G_Time() + thinker->resources;
}

/* Name=Pocket Factory
 * UnitID is the timed factory, DataA is its production interval, DataB is the Clockwerk rawcode,
 * DataC is each Clockwerk's timed life, DataD is the spawn offset from the factory origin,
 * and DataE is the leash range that pulls Clockwerks back toward the factory.
 * The factory's Dur bounds both the summon and its scheduler.
 */
BZ_SIMPLE_SPELL_PROC(AbilityPocketFactory) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    uint32_t interval = (uint32_t)(S_SpellData(spell->code, level, 1) * 1000.0f);
    uint32_t clockwerk = S_SpellDataId(spell->code, level, 2);
    float duration = S_SpellDuration(spell->code, level, false);
    edict_t *factory, *thinker;
    if (!interval || !clockwerk || duration <= 0.0f) return;
    factory = S_SummonAt(caster, S_SpellUnitId(spell->code, level), &st.point, duration);
    if (!factory) return;
    thinker = G_Spawn();
    if (!thinker) { G_FreeEdict(factory); return; }
    thinker->owner = factory; thinker->damage = clockwerk; thinker->resources = interval;
    thinker->wait = S_SpellData(spell->code, level, 3);
    thinker->collision = S_SpellData(spell->code, level, 4);
    thinker->velocity = S_SpellData(spell->code, level, 5); /* DataE leash range */
    thinker->freetime = G_Time() + interval; thinker->spawn_time = G_Time() + (uint32_t)(duration * 1000.0f);
    thinker->think = pocket_factory_think;
}
