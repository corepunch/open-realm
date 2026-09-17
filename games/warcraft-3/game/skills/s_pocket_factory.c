#include "s_skills.h"

#define ID_POCKET_FACTORY MAKEFOURCC('A','N','s','y')

/* Pocket Factory's classless thinker follows the factory slot, so removing or reusing the factory cannot keep production alive. */
static void pocket_factory_think(LPEDICT thinker) {
    LPEDICT factory = thinker->owner;
    if (!factory || !factory->inuse || G_Time() >= thinker->spawn_time) { G_FreeEdict(thinker); return; }
    if (G_Time() < thinker->freetime) return;
    S_SummonAt(factory, thinker->damage, &factory->s.origin2, thinker->wait);
    thinker->freetime = G_Time() + thinker->resources;
}

/* Name=Pocket Factory
 * UnitID is the timed factory, DataA is its production interval, DataB is the Clockwerk rawcode,
 * and DataC is each Clockwerk's timed life. The factory's Dur bounds both the summon and its scheduler.
 */
BZ_SIMPLE_SPELL_PROC(AbilityPocketFactory) {
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD interval = (DWORD)(S_SpellData(spell->code, level, 1) * 1000.0f);
    DWORD clockwerk = S_SpellDataId(spell->code, level, 2);
    FLOAT duration = S_SpellDuration(spell->code, level, false);
    LPEDICT factory, thinker;
    if (!interval || !clockwerk || duration <= 0.0f) return;
    factory = S_SummonAt(caster, S_SpellUnitId(spell->code, level), &st.point, duration);
    if (!factory) return;
    thinker = G_Spawn();
    if (!thinker) { G_FreeEdict(factory); return; }
    thinker->owner = factory; thinker->damage = clockwerk; thinker->resources = interval;
    thinker->wait = S_SpellData(spell->code, level, 3);
    thinker->freetime = G_Time() + interval; thinker->spawn_time = G_Time() + (DWORD)(duration * 1000.0f);
    thinker->think = pocket_factory_think;
}