#include "s_skills.h"

/* DataA is visual rock-ring count. Rock missiles never damage independently:
 * the existing area wave owns all damage, stun and destructable interactions. */
static void volcano_rock_impact(edict_t *rock) {
    vec2_t const point = rock->s.origin2;
    G_SpawnAbilityEffectAtPoint(rock->class_id, WC3_EFFECT_SPECIAL, 0, &point, true);
    if (!G_StartProjectileDeathPresentation(rock)) G_FreeEdict(rock);
}
static umove_t volcano_rock_move = { "birth", NULL, volcano_rock_impact };

static uint32_t volcano_visual_random(uint32_t *state) {
    uint32_t x = *state ? *state : 0x6d2b79f5u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *state = x;
}

static void volcano_spawn_rock(edict_t *wave, edict_t *caster, uint32_t rank,
                               uint32_t ring, uint32_t rings) {
    uint32_t const effect_code = G_AbilityEffectCode(wave->class_id, rank);
    cstring_t art = G_AbilityEffectArt(wave->class_id, WC3_EFFECT_MISSILE, 0);
    cstring_t speed_text = S_SpellString(wave->class_id, "Missilespeed", 0);
    float speed;
    if ((!art || !*art) && effect_code) art = G_AbilityEffectArt(effect_code, WC3_EFFECT_MISSILE, 0);
    if ((!speed_text || !*speed_text) && effect_code)
        speed_text = S_SpellString(effect_code, "Missilespeed", 0);
    speed = speed_text ? (float)atof(speed_text) : 0.0f;
    float const a = (2.0f * (float)M_PI * (float)ring) / (float)rings +
        (float)volcano_visual_random(&wave->variation) / (float)UINT32_MAX;
    float const dist = wave->collision * (0.2f +
        0.8f * (float)volcano_visual_random(&wave->variation) / (float)UINT32_MAX);
    vec2_t const point = { wave->s.origin2.x + cosf(a) * dist,
                           wave->s.origin2.y + sinf(a) * dist };
    edict_t *rock;
    channel_t *flight;
    if (!caster || !art || !*art || speed <= 0.0f) return;
    rock = S_SpellIdentityThinker(caster, wave->class_id, NULL);
    if (!rock) return;
    rock->s.model = G_RegisterModel(art);
    if (!rock->s.model) { G_FreeEdict(rock); return; }
    rock->s.player = caster->s.player;
    rock->s.origin2 = wave->s.origin2;
    rock->s.origin = (vec3_t){ wave->s.origin2.x, wave->s.origin2.y,
        CM_GetHeightAtPoint(wave->s.origin2.x, wave->s.origin2.y) + 180.0f };
    rock->s.angle = a;
    G_InheritUnitTeamColor(rock, caster);
    rock->velocity = speed / 1000.0f;
    flight = G_GetChannel(rock);
    flight->origin = point;
    rock->aiflags |= AI_PROJECTILE_FIXED_TARGET;
    rock->movetype = MOVETYPE_FLYMISSILE;
    rock->currentmove = &volcano_rock_move;
    G_StartProjectilePresentation(rock);
}

/* Living ground/structure units in the blast, including spell-immune (DAMAGE_TYPE_NORMAL). */
static bool volcano_hits(edict_t *caster, edict_t *target, float radius, vec2_t const *origin) {
    if (!target || target == caster || !S_SpellIsAliveTarget(target)) return false;
    if (Vector2_distance(&target->s.origin2, origin) > radius) return false;
    if (G_UnitTargetType(target) == TARG_AIR) return false;
    return G_UnitTargetType(target) == TARG_GROUND || G_UnitTargetType(target) == TARG_STRUCTURE;
}

/* Gate on targtype before G_IsDestructable: channel thinkers have class_id set but no
 * DestructableData pointer, and G_IsDestructable would NULL-deref them. */
static bool volcano_hits_destructable(edict_t *skip, edict_t *target, float radius, vec2_t const *origin) {
    if (!target || target == skip || !target->inuse) return false;
    if (target->targtype != TARG_TREE && target->targtype != TARG_DEBRIS) return false;
    if (!G_IsDestructable(target) || G_GetDestructable(target)->dead) return false;
    return Vector2_distance(&target->s.origin2, origin) <= radius;
}

static float volcano_wave_damage(edict_t *ent, float dist) {
    float dmg = (float)ent->damage;
    if (dist > ent->collision * 0.5f) dmg *= ent->health.value; /* DataF half-damage factor */
    return dmg;
}

static void volcano_finish(edict_t *ent) {
    S_SpellEndAreaPresentation(ent);
    if (ent->goalentity && ent->goalentity->inuse) G_FreeEdict(ent->goalentity);
    ent->goalentity = NULL;
    S_SpellEndChannel(ent);
}

void volcano_think(edict_t *ent) {
    uint32_t now = G_Time(), code = ent->class_id, level;
    edict_t *caster = ent->owner;
    float factor = ent->velocity;
    vec2_t origin = ent->s.origin2;

    if (!S_SpellChannelActive(ent)) { volcano_finish(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    level = S_SpellLevel(caster, code);
    { uint32_t const rings = (uint32_t)MAX(0.0f, S_SpellData(code, level, 1));
      FOR_LOOP(i, MIN(rings, 32u)) volcano_spawn_rock(ent, caster, level, i, rings);
      /* Visual missiles are deliberately independent of damage waves. */
    }
    FILTER_EDICTS(target, volcano_hits(caster, target, ent->collision, &origin)) {
        float dmg = volcano_wave_damage(ent, Vector2_distance(&target->s.origin2, &origin));
        if (G_UnitIsStructure(target)) dmg *= factor;
        T_Damage(target, caster, (int)dmg);
        if (!M_IsDead(target))
            S_SpellApplyStun(target, S_SpellResistantDuration(code, level, target));
    }
    FILTER_EDICTS(target, volcano_hits_destructable(ent->goalentity, target, ent->collision, &origin))
        G_DestructableApplyDamage(target, caster, volcano_wave_damage(ent, Vector2_distance(&target->s.origin2, &origin)));
    if (!--ent->resources) { volcano_finish(ent); return; }
    ent->freetime = now + (uint32_t)(MAX(0.1f, ent->wait) * 1000.0f);
}

/* Name=Volcano
 * AbilityMetaData: DataA Rock Ring Count (presentation), DataB waves, DataC interval,
 * DataD building factor, DataE full damage inside Area/2, DataF half factor outside Area/2,
 * UnitID volcano doodad. Dur/HeroDur are stun only; channel length is DataB×DataC.
 */
BZ_SIMPLE_SPELL_PROC(AbilityVolcano) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    uint32_t unit_id = S_SpellUnitId(spell->code, level);
    edict_t *thinker = S_SpellChannelThinker(caster, spell->code);

    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->damage = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 5)); /* DataE full damage */
    thinker->resources = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 2)); /* DataB waves */
    thinker->wait = MAX(0.1f, S_SpellData(spell->code, level, 3)); /* DataC wave interval */
    thinker->velocity = S_SpellData(spell->code, level, 4); /* DataD building damage factor */
    thinker->health.value = S_SpellData(spell->code, level, 6); /* DataF half-damage factor */
    if (unit_id) {
        edict_t *doodad = G_CreateDestructable(unit_id, st.point.x, st.point.y, 0, 0, 1, 0);
        if (doodad) thinker->goalentity = doodad;
        else fprintf(stderr, "WC3 Volcano: failed to spawn UnitID %.4s\n", (cstring_t)&unit_id);
    }
    thinker->variation = caster->spawn_time ^ (uint32_t)st.point.x ^
                         ((uint32_t)st.point.y << 16);
    (void)S_SpellBeginAreaPresentation(thinker, spell->code, level, &st.point);
    thinker->think = volcano_think;
    volcano_think(thinker); /* first wave immediately; stock 8*5s ends at 35s */
}
