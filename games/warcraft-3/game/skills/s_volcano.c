#include "s_skills.h"

#define ID_STUN_BUFF "Bstu"

/* Living ground/structure units in the blast, including spell-immune (DAMAGE_TYPE_NORMAL). */
static BOOL volcano_hits(LPEDICT caster, LPEDICT target, FLOAT radius, LPCVECTOR2 origin) {
    if (!target || target == caster || !S_SpellIsAliveTarget(target)) return false;
    if (Vector2_distance(&target->s.origin2, origin) > radius) return false;
    if (target->targtype == TARG_AIR) return false;
    return target->targtype == TARG_GROUND || target->targtype == TARG_STRUCTURE ||
           G_UnitIsBuilding(target->class_id);
}

/* Gate on targtype before G_IsDestructable: channel thinkers have class_id set but no
 * DestructableData pointer, and G_IsDestructable would NULL-deref them. */
static BOOL volcano_hits_destructable(LPEDICT skip, LPEDICT target, FLOAT radius, LPCVECTOR2 origin) {
    if (!target || target == skip || !target->inuse) return false;
    if (target->targtype != TARG_TREE && target->targtype != TARG_DEBRIS) return false;
    if (!G_IsDestructable(target) || target->destructable.dead) return false;
    return Vector2_distance(&target->s.origin2, origin) <= radius;
}

static FLOAT volcano_wave_damage(LPEDICT ent, FLOAT dist) {
    FLOAT dmg = (FLOAT)ent->damage;
    if (dist > ent->collision * 0.5f) dmg *= ent->health.value; /* DataF half-damage factor */
    return dmg;
}

static void volcano_finish(LPEDICT ent) {
    if (ent->goalentity && ent->goalentity->inuse) G_FreeEdict(ent->goalentity);
    ent->goalentity = NULL;
    S_SpellEndChannel(ent);
}

void volcano_think(LPEDICT ent) {
    DWORD now = G_Time(), code = ent->class_id, level;
    LPEDICT caster = ent->owner;
    FLOAT factor = ent->velocity;
    VECTOR2 origin = ent->s.origin2;

    if (!S_SpellChannelActive(ent)) { volcano_finish(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    level = S_SpellLevel(caster, code);
    FILTER_EDICTS(target, volcano_hits(caster, target, ent->collision, &origin)) {
        FLOAT dmg = volcano_wave_damage(ent, Vector2_distance(&target->s.origin2, &origin));
        if (G_UnitIsBuilding(target->class_id)) dmg *= factor;
        T_Damage(target, caster, (int)dmg);
        if (!M_IsDead(target))
            unit_addtimedstatus(target, ID_STUN_BUFF, 1, S_SpellDuration(code, level, G_UnitIsHero(target)));
    }
    FILTER_EDICTS(target, volcano_hits_destructable(ent->goalentity, target, ent->collision, &origin))
        G_DestructableApplyDamage(target, caster, volcano_wave_damage(ent, Vector2_distance(&target->s.origin2, &origin)));
    if (!--ent->resources) { volcano_finish(ent); return; }
    ent->freetime = now + (DWORD)(MAX(0.1f, ent->wait) * 1000.0f);
}

/* Name=Volcano
 * AbilityMetaData: DataA Rock Ring Count (presentation), DataB waves, DataC interval,
 * DataD building factor, DataE full damage inside Area/2, DataF half factor outside Area/2,
 * UnitID volcano doodad. Dur/HeroDur are stun only; channel length is DataB×DataC.
 */
BZ_SIMPLE_SPELL_PROC(AbilityVolcano) {
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD unit_id = S_SpellUnitId(spell->code, level);
    LPEDICT thinker = S_SpellChannelThinker(caster, spell->code);

    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->damage = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 5)); /* DataE full damage */
    thinker->resources = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 2)); /* DataB waves */
    thinker->wait = MAX(0.1f, S_SpellData(spell->code, level, 3)); /* DataC wave interval */
    thinker->velocity = S_SpellData(spell->code, level, 4); /* DataD building damage factor */
    thinker->health.value = S_SpellData(spell->code, level, 6); /* DataF half-damage factor */
    if (unit_id) {
        LPEDICT doodad = G_CreateDestructable(unit_id, st.point.x, st.point.y, 0, 0, 1, 0);
        if (doodad) thinker->goalentity = doodad;
        else fprintf(stderr, "WC3 Volcano: failed to spawn UnitID %.4s\n", (LPCSTR)&unit_id);
    }
    thinker->think = volcano_think;
    volcano_think(thinker); /* first wave immediately; stock 8*5s ends at 35s */
}
