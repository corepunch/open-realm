#include "s_skills.h"

#define ID_STUN_BUFF "Bstu"

/* Volcano waves hit living ground/structure units in the blast, including spell-immune
 * targets (DAMAGE_TYPE_NORMAL). Tree/debris destruction remains unresolved. */
static BOOL volcano_hits(LPEDICT caster, LPEDICT target, FLOAT radius, LPCVECTOR2 origin) {
    if (!target || target == caster || !S_SpellIsAliveTarget(target)) return false;
    if (Vector2_distance(&target->s.origin2, origin) > radius) return false;
    if (target->targtype == TARG_AIR) return false;
    return target->targtype == TARG_GROUND || target->targtype == TARG_STRUCTURE ||
           G_UnitIsBuilding(target->class_id);
}

void volcano_think(LPEDICT ent) {
    DWORD now = G_Time(), code = ent->class_id, level;
    LPEDICT caster = ent->owner;
    FLOAT factor = ent->velocity;

    if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    level = S_SpellLevel(caster, code);
    FILTER_EDICTS(target, volcano_hits(caster, target, ent->collision, &ent->s.origin2)) {
        FLOAT dmg = (FLOAT)ent->damage;
        if (G_UnitIsBuilding(target->class_id)) dmg *= factor;
        T_Damage(target, caster, (int)dmg);
        if (!M_IsDead(target))
            unit_addtimedstatus(target, ID_STUN_BUFF, 1, S_SpellDuration(code, level, G_UnitIsHero(target)));
    }
    if (!--ent->resources) { S_SpellEndChannel(ent); return; }
    ent->freetime = now + (DWORD)(MAX(0.1f, ent->wait) * 1000.0f);
}

/* Name=Volcano
 * Ubertip: every DataC seconds, DataB waves of DataE damage (buildings * DataD), stun Dur/HeroDur.
 * Channel length is DataB waves, not Dur; Dur/HeroDur are stun lengths only.
 */
BZ_SIMPLE_SPELL_PROC(AbilityVolcano) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT thinker = S_SpellChannelThinker(caster, spell->code);

    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->damage = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 5)); /* DataE full damage */
    thinker->resources = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 2)); /* DataB waves */
    thinker->wait = MAX(0.1f, S_SpellData(spell->code, level, 3)); /* DataC wave interval */
    thinker->velocity = S_SpellData(spell->code, level, 4); /* DataD building damage factor */
    thinker->think = volcano_think;
    volcano_think(thinker); /* first wave immediately; stock 8*5s ends at 35s */
}
