#include "s_skills.h"

/* Inclusive [DataB, DataC]; swap if inverted; zero max yields no exits. */
static DWORD dark_portal_count(DWORD code, DWORD level) {
    DWORD minc = (DWORD)MAX(0.0f, S_SpellData(code, level, 2));
    DWORD maxc = (DWORD)MAX(0.0f, S_SpellData(code, level, 3));
    if (maxc < minc) { DWORD tmp = minc; minc = maxc; maxc = tmp; }
    if (!maxc) return 0;
    if (minc == maxc) return minc;
    return minc + (DWORD)(rand() % (int)(maxc - minc + 1));
}

/* Permanent campaign troop: food/stand/summon events, but no owner/BTLF mark. */
static LPEDICT dark_portal_spawn(LPEDICT caster, DWORD unit_id, LPCVECTOR2 loc) {
    LPEDICT troop;
    VECTOR2 spot;
    FLOAT angle = 0.0f;
    if (!caster || !unit_id || !loc) return NULL;
    spot = *loc;
    SP_FindEmptySpaceAround(caster, unit_id, &spot, &angle);
    troop = SP_SpawnAtLocation(unit_id, caster->s.player, &spot);
    if (!troop) return NULL;
    G_ActivateUnitFood(troop);
    if (troop->stand) troop->stand(troop);
    G_PublishSummonEvents(caster, troop);
    return troop;
}

void dark_portal_think(LPEDICT ent) {
    DWORD now = G_Time(), code = ent->class_id;
    if (!ent->owner || !ent->owner->inuse || !ent->resources) { G_FreeEdict(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    if (!ent->damage) {
        fprintf(stderr, "WC3 Dark Portal: missing DataA unit for %.4s\n", (LPCSTR)&code);
        G_FreeEdict(ent); return;
    }
    dark_portal_spawn(ent->owner, ent->damage, &ent->s.origin2);
    if (!--ent->resources) { G_FreeEdict(ent); return; }
    /* Zero Dur cannot schedule the next exit; stop rather than spin every frame. */
    if (ent->velocity <= 0.0f) {
        fprintf(stderr, "WC3 Dark Portal: spawn interval became zero for %.4s\n", (LPCSTR)&code);
        G_FreeEdict(ent); return;
    }
    ent->freetime = now + (DWORD)(ent->velocity * 1000.0f);
}

/* Name=Dark Portal
 * Ubertip: opens a portal so demons step through at the target point.
 * DataA unit, DataB/DataC min/max count, Dur exit interval; not Mass Teleport.
 */
BZ_SIMPLE_SPELL_PROC(AbilityDarkPortal) {
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD unit = S_SpellDataId(spell->code, level, 1);
    DWORD count = dark_portal_count(spell->code, level);
    LPEDICT thinker;
    if (!caster || !count) return;
    if (!unit) {
        fprintf(stderr, "WC3 Dark Portal: missing DataA unit for %.4s\n", (LPCSTR)&spell->code);
        return;
    }
    thinker = G_Spawn();
    thinker->owner = caster; thinker->class_id = spell->code; thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x; thinker->s.origin.y = st.point.y;
    thinker->damage = unit; thinker->resources = count;
    thinker->velocity = MAX(0.0f, S_SpellDuration(spell->code, level, false));
    thinker->wait = (FLOAT)level; thinker->think = dark_portal_think;
    dark_portal_think(thinker);
}
