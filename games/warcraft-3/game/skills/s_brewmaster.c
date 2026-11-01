#include "s_skills.h"

#define BREW_HAZE MAKEFOURCC('B','N','d','h')
#define BREW_FIRE MAKEFOURCC('B','N','b','f')
#define BREW_STORM MAKEFOURCC('n','p','n','2')
#define BREW_EARTH MAKEFOURCC('n','p','n','3')

/* Source ability is saved on the status, so campaign Acdh retains its separate
 * rank progression instead of silently reading the ordinary ANdh values. */
static uint32_t brewmaster_haze_ability(heroabilitystatus_t const *status) {
    return status && status->source_ability ? status->source_ability : MAKEFOURCC('A','N','d','h');
}

static umove_t brewmaster_breath_move = {
    .animation = "Stand",
    .endfunc = G_FreeEdict
};

/* Breath of Fire damage resolves over the cone immediately; this point missile
 * is its independent, authored flame presentation. */
static void brewmaster_breath_projectile(edict_t *caster, uint32_t code, vec2_t const *point) {
    cstring_t art = G_AbilityEffectArt(code, WC3_EFFECT_MISSILE, 0);
    cstring_t speed_text = S_SpellString(code, "Missilespeed", 0);
    uint32_t const base_code = G_AbilityCode(code);
    float speed = speed_text ? (float)atof(speed_text) : 0.0f;
    vec2_t offset, direction;
    float horizontal_distance, spatial_distance;
    vec3_t endpoint;
    edict_t *missile;
    channel_t *flight;

    if (!caster || !point || !art || !*art) return;
    if ((!isfinite(speed) || speed <= 0.0f) && base_code != code) {
        speed_text = S_SpellString(base_code, "Missilespeed", 0);
        speed = speed_text ? (float)atof(speed_text) : 0.0f;
    }
    if (!isfinite(speed) || speed <= 0.0f) return;
    offset = Vector2_sub(point, &caster->s.origin2);
    horizontal_distance = Vector2_len(&offset);
    if (horizontal_distance <= 0.001f) return;
    direction = Vector2_scale(&offset, 1.0f / horizontal_distance);
    missile = G_Spawn();
    if (!missile) return;
    missile->class_id = code;
    missile->owner = caster;
    missile->s.player = caster->s.player;
    missile->s.origin = caster->s.origin;
    missile->s.origin2 = caster->s.origin2;
    missile->s.angle = atan2f(direction.y, direction.x);
    missile->s.model = G_RegisterModel(art);
    if (!missile->s.model) { G_FreeEdict(missile); return; }
    G_InheritUnitTeamColor(missile, caster);
    flight = G_AddChannel(missile);
    if (!flight) { G_FreeEdict(missile); return; }
    flight->owner_spawn_time = caster->spawn_time;
    flight->origin = *point;
    endpoint = MAKE(vec3_t, point->x, point->y, CM_GetHeightAtPoint(point->x, point->y));
    spatial_distance = Vector3_distance(&missile->s.origin, &endpoint);
    missile->velocity = speed / 1000.0f * spatial_distance / horizontal_distance;
    missile->movetype = MOVETYPE_FLYMISSILE;
    missile->aiflags |= AI_PROJECTILE_FIXED_TARGET;
    missile->currentmove = &brewmaster_breath_move;
    G_StartProjectilePresentation(missile);
}

bool S_BrewmasterHazeMiss(edict_t const *victim) {
    heroabilitystatus_t const *status = unit_findstatus((edict_t *)victim, BREW_HAZE);
    uint32_t rank, code;
    if (!status || !status->level || (status->timestamp && status->timestamp <= G_Time())) return false;
    code = brewmaster_haze_ability(status);
    rank = status->rank ? status->rank : status->level;
    return (float)(rand() % 10000) / 10000.0f <
           MIN(1.0f, MAX(0.0f, S_SpellData(code, rank, 2)));
}

/* BNbf is the authoritative dispellable owner of the ongoing ignition. Damage
 * thinkers remember its instance identity so a subsequent application can
 * replace the old one without ticking twice or reviving removed effects. */
void brewmaster_burn_think(edict_t *burn) {
    edict_t *victim = S_SpellChannelTarget(burn);
    edict_t *source = S_SpellChannelOwner(burn);
    heroabilitystatus_t const *status = victim ? unit_findstatus(victim, BREW_FIRE) : NULL;
    uint32_t now = G_Time();
    if (!victim || !S_SpellIsAliveTarget(victim) || !status ||
        status->instance_id != burn->variation ||
        (status->timestamp && status->timestamp <= now) || now >= burn->damage) {
        G_FreeEdict(burn);
        return;
    }
    if (now < burn->freetime) return;
    if (source) S_SpellDamage(victim, source, (int)MAX(0.0f, burn->velocity));
    burn->freetime = now + 1000;
}

static void brewmaster_ignite(edict_t *caster, edict_t *victim, uint32_t code, uint32_t rank) {
    cstring_t buff = S_SpellBuffId(code, rank);
    float duration = S_SpellHeroDuration(code, rank, victim);
    heroabilitystatus_t *status;
    edict_t *burn;
    channel_t *identity;
    if (!buff || strlen(buff) < 4 || duration <= 0.0f) return;
    status = S_SpellApplyTimedTargetStatus(victim, code, rank, buff, duration);
    if (!status) return;
    burn = S_SpellIdentityThinker(caster, code, victim);
    if (!burn) return;
    identity = G_GetChannel(burn);
    if (!identity) { G_FreeEdict(burn); return; }
    burn->resources = BREW_FIRE;
    burn->variation = status->instance_id;
    burn->velocity = S_SpellData(code, rank, 5); /* DataE: burn DPS */
    burn->freetime = G_Time() + 1000;
    burn->damage = G_Time() + (uint32_t)(duration * 1000.0f);
    burn->svflags |= SVF_NOCLIENT;
    burn->think = brewmaster_burn_think;
}

/* DataC is the cone length; DataD its terminal width. DataB caps the sum
 * of initial damage from one cast, while DataA is damage per eligible unit.
 * The hit corridor starts at the caster, not at the clicked endpoint. */
void S_BrewmasterBreathOfFire(edict_t *caster, vec2_t const *point, uint32_t code) {
    uint32_t rank = S_SpellLevel(caster, code);
    vec2_t direction, start;
    float length = MAX(0.0f, S_SpellData(code, rank, 3));
    float width = MAX(0.0f, S_SpellData(code, rank, 4));
    float damage = MAX(0.0f, S_SpellData(code, rank, 1));
    float budget = MAX(0.0f, S_SpellData(code, rank, 2));
    float remaining = budget > 0.0f ? budget : FLT_MAX;
    if (!caster || !point || length <= 0.0f) return;
    start = caster->s.origin2;
    direction = Vector2_sub(point, &start);
    float distance = Vector2_distance(point, &start);
    if (distance <= 0.001f) return;
    direction = Vector2_scale(&direction, 1.0f / distance);
    brewmaster_breath_projectile(caster, code, point);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_CASTER, 0, caster, NULL, true);
    FILTER_EDICTS(victim, remaining >= 1.0f && victim != caster &&
                  S_SpellIsAliveTarget(victim) && S_SpellIsEnemy(caster, victim) &&
                  S_SpellAllowsAreaTarget(code, caster, victim)) {
        vec2_t relative = Vector2_sub(&victim->s.origin2, &start);
        float along = relative.x * direction.x + relative.y * direction.y;
        float across = fabsf(relative.x * direction.y - relative.y * direction.x);
        if (along < 0.0f || along > length + victim->collision ||
            across > width * 0.5f * MIN(1.0f, MAX(0.0f, along / length)) + victim->collision) continue;
        int dealt = (int)MIN(damage, remaining);
        if (dealt <= 0 || !S_SpellDamage(victim, caster, dealt)) continue;
        remaining -= (float)dealt;
        G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, victim, NULL, true);
        if (!M_IsDead(victim)) {
            heroabilitystatus_t const *haze = unit_findstatus(victim, BREW_HAZE);
            if (haze && (!haze->timestamp || haze->timestamp > G_Time()))
                brewmaster_ignite(caster, victim, code, rank);
        }
    }
}

/* The Strong Drink bottle is homing. Its impact, not the click, samples the
 * authored area and starts the miss/slow statuses. */
static void brewmaster_haze_hit(edict_t *missile) {
    edict_t *caster = S_SpellChannelOwner(missile);
    edict_t *primary = S_SpellChannelTarget(missile);
    uint32_t code = missile->class_id, rank = missile->resources;
    float radius = S_SpellNumber(code, ABILITY_NUMBER_AREA, rank);
    cstring_t buff = S_SpellBuffId(code, rank);
    if (caster && primary && !M_IsDead(primary) && buff) {
        vec2_t center = primary->s.origin2;
        G_SpawnAbilityEffectAtPoint(code, WC3_EFFECT_SPECIAL, 0, &center, true);
        FILTER_EDICTS(victim, S_SpellIsAliveTarget(victim) && S_SpellIsEnemy(caster, victim) &&
                      S_SpellAllowsAreaTarget(code, caster, victim) &&
                      Vector2_distance(&victim->s.origin2, &center) <= radius) {
            S_SpellApplyTimedTargetStatus(victim, code, rank, buff,
                                          S_SpellHeroDuration(code, rank, victim));
        }
    }
    G_FreeEdict(missile);
}
static umove_t brewmaster_haze_move = { "stand", NULL, brewmaster_haze_hit };

void S_BrewmasterDrunkenHaze(edict_t *caster, edict_t *target, uint32_t code) {
    edict_t *missile;
    channel_t *flight;
    uint32_t rank;
    cstring_t art, speed_data;
    float speed;
    if (!caster || !target || !S_SpellIsAliveTarget(target)) return;
    rank = S_SpellLevel(caster, code);
    art = G_AbilityEffectArt(code, WC3_EFFECT_MISSILE, 0);
    speed_data = S_SpellString(code, "Missilespeed", 0);
    speed = speed_data ? (float)atof(speed_data) : 700.0f;
    if (!art || !*art || speed <= 0.0f) {
        /* Artless custom spell data still performs gameplay. */
        missile = S_SpellIdentityThinker(caster, code, target);
        if (missile) { missile->resources = rank; brewmaster_haze_hit(missile); }
        return;
    }
    missile = S_SpellIdentityThinker(caster, code, target);
    if (!missile) return;
    flight = G_GetChannel(missile);
    missile->resources = rank;
    missile->s.player = caster->s.player;
    missile->s.model = G_RegisterModel(art);
    missile->s.origin = caster->s.origin;
    missile->s.origin2 = caster->s.origin2;
    missile->s.angle = atan2f(target->s.origin2.y - caster->s.origin2.y,
                              target->s.origin2.x - caster->s.origin2.x);
    G_InheritUnitTeamColor(missile, caster);
    flight->origin = target->s.origin2;
    missile->velocity = speed / 1000.0f;
    missile->movetype = MOVETYPE_FLYMISSILE;
    missile->currentmove = &brewmaster_haze_move;
    G_StartProjectilePresentation(missile);
}

/* Each split has a single active controller. Elementals retain the real hero
 * as owner; the controller never substitutes their JASS handles for the hero's. */
static bool brewmaster_elemental(edict_t const *unit, edict_t const *hero, uint32_t code) {
    return unit && unit->inuse && unit->owner == hero && unit->summon_ability == code &&
        (unit->class_id == BREW_STORM || unit->class_id == BREW_EARTH ||
         unit->class_id == MAKEFOURCC('n','p','n','1') || unit->class_id == MAKEFOURCC('n','p','n','4'));
}

static void brewmaster_unhide(edict_t *hero, vec2_t const *point) {
    vec2_t location;
    if (point) {
        if (!G_FindUnitUnstuckPosition(hero, point, &location)) location = *point;
        hero->s.origin2 = location;
        hero->s.origin.x = location.x;
        hero->s.origin.y = location.y;
        hero->s.origin.z = CM_GetHeightAtPoint(location.x, location.y);
    }
    hero->aiflags &= ~AI_SOUL_TRAPPED;
    G_SetUnitFlag(hero, UNIT_FLAG_PAUSED, false);
    hero->s.renderfx &= ~RF_HIDDEN;
    hero->svflags &= ~SVF_NOCLIENT;
    hero->s.flags &= ~EF_NOT_SELECTABLE;
    gi.LinkEntity(hero);
    if (hero->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    G_InvalidateUnitShortcutsForUnit(hero);
    G_InvalidateCommands(G_GetPlayerClientByNumber(hero->s.player));
}

void brewmaster_split_think(edict_t *controller) {
    edict_t *hero = S_SpellChannelOwner(controller);
    edict_t *survivor = NULL;
    uint32_t alive = 0;
    if (!hero || M_IsDead(hero)) {
        uint32_t code = controller->class_id;
        G_FreeEdict(controller);
        if (hero) FILTER_EDICTS(unit, brewmaster_elemental(unit, hero, code))
            G_FreeEdict(unit);
        return;
    }
    FILTER_EDICTS(unit, brewmaster_elemental(unit, hero, controller->class_id) &&
                  !M_IsDead(unit) && G_UnitIsWorldActive(unit)) {
        survivor = unit;
        alive++;
    }
    if (alive && G_Time() < controller->freetime) return;
    vec2_t return_point = survivor ? survivor->s.origin2 : controller->s.origin2;
    /* Retire the controller first: simultaneous deaths and save restoration
     * must not trigger two independent hero returns. */
    uint32_t code = controller->class_id;
    G_FreeEdict(controller);
    FILTER_EDICTS(unit, brewmaster_elemental(unit, hero, code)) {
        G_FreeEdict(unit); /* elementals disappear, not normal corpse deaths */
    }
    brewmaster_unhide(hero, &return_point);
    if (!alive) {
        G_SetHealth(hero, 0.0f);
        unit_die(hero, NULL);
    } else {
        unit_stand(hero);
        G_SpawnAbilityEffectTarget(code, WC3_EFFECT_CASTER, 0, hero, NULL, true);
    }
}

/* G_FreeEdict may remove a hidden hero from a script without running the
 * active split thinker again. Retire its controller and summons while the
 * original owner incarnation is still valid. */
void S_BrewmasterOnUnitRemoved(edict_t *hero) {
    uint32_t code = 0;
    if (!hero || !hero->unit_state || !(hero->aiflags & AI_SOUL_TRAPPED)) return;
    FILTER_EDICTS(controller, controller->think == brewmaster_split_think &&
                  S_SpellChannelOwner(controller) == hero) {
        code = controller->class_id;
        G_FreeEdict(controller);
    }
    if (!code) return;
    FILTER_EDICTS(unit, brewmaster_elemental(unit, hero, code)) G_FreeEdict(unit);
}

static bool brewmaster_split_active(edict_t const *hero) {
    if (!hero) return false;
    FILTER_EDICTS(marker, marker->think == brewmaster_split_think &&
                  S_SpellChannelOwner(marker) == hero) return true;
    return false;
}

static bool brewmaster_split_valid(edict_t *hero, uint32_t code) {
    uint32_t rank, fire;
    if (!hero || !hero->unit_state || M_IsDead(hero) ||
        !G_UnitIsWorldActive(hero) || brewmaster_split_active(hero)) return false;
    rank = S_SpellLevel(hero, code);
    fire = S_SpellDataId(code, rank, 1);
    return fire && G_UnitBalance(fire) && G_UnitBalance(BREW_STORM) &&
           G_UnitBalance(BREW_EARTH);
}

static bool brewmaster_split_start(edict_t *hero, uint32_t code) {
    uint32_t rank = S_SpellLevel(hero, code), fire = S_SpellDataId(code, rank, 1);
    uint32_t ids[3] = { BREW_STORM, BREW_EARTH, fire };
    edict_t *spawned[3] = { NULL, NULL, NULL };
    edict_t *controller;
    float duration = S_SpellDuration(code, rank, false);
    vec2_t origin = hero->s.origin2;
    if (!brewmaster_split_valid(hero, code) || duration <= 0.0f) return false;
    controller = S_SpellIdentityThinker(hero, code, NULL);
    if (!controller) return false;
    FOR_LOOP(i, 3) {
        float angle = 2.0f * (float)M_PI * (float)i / 3.0f;
        vec2_t point = { origin.x + cosf(angle) * 96.0f,
                         origin.y + sinf(angle) * 96.0f };
        SP_FindEmptySpaceAround(hero, ids[i], &point, &angle);
        spawned[i] = S_SummonAbilityAt(hero, code, ids[i], &point, duration);
        if (!spawned[i]) {
            FOR_LOOP(j, i) if (spawned[j] && spawned[j]->inuse) G_FreeEdict(spawned[j]);
            G_FreeEdict(controller);
            return false;
        }
    }
    controller->s.origin2 = origin;
    controller->freetime = G_Time() + (uint32_t)(duration * 1000.0f);
    controller->svflags |= SVF_NOCLIENT;
    controller->think = brewmaster_split_think;
    G_SpawnAbilityEffectAtPoint(code, WC3_EFFECT_SPECIAL, 0, &origin, true);
    S_SpellCancelChannel(hero);
    G_ClearActiveUnitOrder(hero);
    G_ClearUnitOrderQueue(hero);
    hero->goalentity = hero->combatentity = NULL;
    G_SetUnitFlag(hero, UNIT_FLAG_PAUSED, true);
    hero->aiflags |= AI_SOUL_TRAPPED;
    hero->s.renderfx |= RF_HIDDEN;
    hero->svflags |= SVF_NOCLIENT;
    hero->s.flags |= EF_NOT_SELECTABLE;
    gi.UnlinkEntity(hero);
    if (hero->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *client = game.clients + i;
        if (!client->connected || !(hero->selected & (1u << client->ps.number))) continue;
        G_DeselectEntity(client, hero);
        FOR_LOOP(j, 3) G_SelectEntity(client, spawned[j]);
        G_SyncClientSelection(client);
    }
    hero->selected = 0;
    G_InvalidateUnitShortcutsForUnit(hero);
    G_InvalidateCommands(G_GetPlayerClientByNumber(hero->s.player));
    return true;
}

BZ_ABILITY_PROC(CAbilityStormEarthFire) {
    if (!call || !call->item) return false;
    if (msg == A_VALIDATE) return brewmaster_split_valid(ent, call->item->code);
    if (msg == A_EXECUTE) return brewmaster_split_start(ent, call->item->code);
    return CAbilitySimpleSpell(ent, msg, call);
}
