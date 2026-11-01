/*
 * s_attack.c — Attack ability and projectile system.
 *
 * Implements the CAbilityAttack ability used by all combat units.  Handles both
 * melee and ranged (missile) attack styles, each with a damage phase and a
 * cooldown phase driven by the umove_t state machine.
 *
 * Ranged attacks spawn a projectile entity via fire_rocket().  The projectile
 * is a regular server entity with MOVETYPE_FLYMISSILE; each frame g_phys.c
 * advances it toward its target until it hits, at which point T_Damage() is
 * called and the entity is freed.
 *
 * T_Damage() is also the central damage resolution function: it reduces
 * health, triggers counter-attacks, and calls the die() callback when a unit
 * is killed.
 */
#include "s_skills.h"

void attack_walk(edict_t *ent);
void attack_melee(edict_t *ent);
void attack_melee_cooldown(edict_t *ent);
void attack_ranged(edict_t *ent);
void attack_ranged_cooldown(edict_t *ent);
void order_attack(edict_t *self, edict_t *target);
static void order_attack_internal(edict_t *self, edict_t *target, bool retaliation);

static void ai_melee_cooldown(edict_t *ent);
static void ai_ranged_cooldown(edict_t *ent);
static bool attack_target_out_of_range(edict_t *ent);
static bool attack_target_out_of_base_range(edict_t *ent);
static bool attack_target_too_close(edict_t *ent);
static bool attack_is_cooling_down(edict_t const *ent);
static bool attack_cooldown_elapsed(edict_t *ent);
static float attack_speed_divisor(edict_t *self);
static void attack_set_backswing_deadline(edict_t *ent);
static float attack_backswing_remaining(edict_t const *ent);
static umove_t attack_move_melee_cooldown;
static umove_t attack_move_ranged_cooldown;

#define ID_MEAT_WAGON MAKEFOURCC('u','m','t','w')
#define ID_MEAT_WAGON_IMPACT MAKEFOURCC('u','m','t','i')
#define ID_PLAGUE_TOSS MAKEFOURCC('A','p','t','s')
#define ID_DISEASE_CLOUD_RESEARCH MAKEFOURCC('R','u','p','c')
#define MEAT_WAGON_PLAGUE_CLOUD_MS 10000u

static void artillery_spawn_meat_wagon_impact(edict_t *attacker, vec2_t const *impact) {
    UnitProfile_t const *profile;
    edict_t *effect;
    animation_t const *death;
    if (!attacker || attacker->class_id != ID_MEAT_WAGON) return;
    profile = attacker->data.UnitProfile ? attacker->data.UnitProfile : G_UnitProfile(attacker->class_id);
    if (!profile || !profile->attack[0].art || !*profile->attack[0].art) return;
    effect = G_SpawnModelEffect(profile->attack[0].art, impact, NULL, NULL, true);
    if (!effect) return;
    effect->summon_ability = ID_MEAT_WAGON_IMPACT;
    death = G_GetAnimation(effect->s.model, "Death");
    if (death && death->interval[1] > death->interval[0]) {
        effect->animation = death;
        effect->s.frame = death->interval[0];
    }
}

static void artillery_spawn_meat_wagon_plague_cloud(edict_t *attacker, vec2_t const *impact) {
    gameClient_t *owner;
    edict_t *effect;
    UnitUI_t const *ward_ui;
    if (!attacker || attacker->class_id != ID_MEAT_WAGON ||
        !G_UnitAbilityLevel(attacker, ID_PLAGUE_TOSS)) return;
    owner = G_GetPlayerClientByNumber(attacker->s.player);
    if (!owner || G_GetPlayerTechResearchedLevel(owner, ID_DISEASE_CLOUD_RESEARCH) <= 0) return;
    effect = G_SpawnModelEffect("Units\\Undead\\PlagueCloud\\PlagueCloud.mdl",
                                impact, NULL, NULL, false);
    if (effect) {
        ward_ui = G_UnitUI(MAKEFOURCC('u','p','l','g'));
        effect->s.scale = ward_ui->modelScale > 0.0f ? ward_ui->modelScale : 1.0f;
        effect->s.radius = ward_ui->selectionScale * SEL_SCALE / 2.0f;
        effect->spawn_time = G_Time() + MEAT_WAGON_PLAGUE_CLOUD_MS;
        effect->summon_ability = ID_PLAGUE_TOSS;
    }
}

typedef struct {
    edict_t *target;
    vec2_t const *fixed_target;
    vec3_t start;
    vec3_t dir;
    uint32_t speed;
    uint32_t model;
    uint32_t damage;
    uint32_t attack_type;
    uint32_t arrow_ability;
    uint32_t arrow_level;
    unitAttack_t const *attack;
    uint32_t area_targets;
    /* Aline's researched follow-through is snapshotted only for normal
     * attacks. Explicit Attack Ground must never inherit it. */
    bool impaling_line;
    bool tree_attack_allowed;
    bool allow_destroyer_orb;
}  rocketDesc_t;

bool S_UnitAttackSlotEnabled(edict_t const *attacker, uint32_t slot) {
    uint32_t enabled;
    if (!attacker || !attacker->unit_state || slot >= 2) return false;
    enabled = attacker->data.UnitWeapons ? attacker->data.UnitWeapons->attacksEnabled : 0;
    /* War Club is a per-unit switch, never a mutation of UnitWeapons. The
     * alternate weapon is authored as Attack 2 even when the stock unit enables
     * only Attack 1 in its normal form. */
    if (attacker->unit_state->war_club_attacks_remaining &&
        attacker->unit_state->attack2.type != ATK_NONE)
        enabled = 2u;
    if (S_AncientHasRootAbility(attacker)) {
        /* Root/Unroot transitions must not retain an attack order from the
         * previous form. Stable forms use their authored AbilityData masks. */
        if (S_AncientIsMorphing(attacker)) return false;
        enabled = S_AncientAttackMask(attacker);
    }
    return (enabled & (1u << slot)) != 0;
}

/* Attack 1/2 remain the authored runtime copies. Select the compatible slot
 * from the target whenever attack behavior reads a profile. */
static unitAttack_t const *attack_profile(edict_t const *attacker, edict_t const *target) {
    uint32_t flag = target ? G_TargetFlagForType(G_UnitTargetType(target)) : 0;
    uint32_t retaliation = S_AncientIsRooted(attacker) ? S_AncientRetaliationAttackMask(attacker) : 0;
    if (attacker && target && G_GetDestructable(target) && target->targtype == TARG_TREE) {
        if (attacker->unit_state->attack1.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 0)) return &attacker->unit_state->attack1;
        if (attacker->unit_state->attack2.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 1)) return &attacker->unit_state->attack2;
    }
    /* Target eligibility and active weapon selection must agree for Ethereal
     * victims, including dual-weapon units and rooted Ancient retaliation. */
    if (attacker && target && flag && unit_hasstatusstate(target, WC3_STATUS_STATE_ETHEREAL)) {
        if (attacker->unit_state->attack1.type == ATK_MAGIC && (attacker->unit_state->attack1.targetsAllowed & flag) &&
            (S_UnitAttackSlotEnabled(attacker, 0) || (retaliation & 1u))) return &attacker->unit_state->attack1;
        if (attacker->unit_state->attack2.type == ATK_MAGIC && (attacker->unit_state->attack2.targetsAllowed & flag) &&
            (S_UnitAttackSlotEnabled(attacker, 1) || (retaliation & 2u))) return &attacker->unit_state->attack2;
        return &attacker->unit_state->attack1; /* Callers reject unqualified targets. */
    }
    if (attacker && flag && attacker->unit_state->attack1.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 0) &&
        (attacker->unit_state->attack1.targetsAllowed & flag)) return &attacker->unit_state->attack1;
    if (attacker && flag && attacker->unit_state->attack2.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 1) &&
        (attacker->unit_state->attack2.targetsAllowed & flag)) return &attacker->unit_state->attack2;
    /* Rooted Ancients may retaliate with their weapon profile even when Root's
     * authored mask disables player-issued attacks; explicit orders are still
     * validated by S_AttackCanTarget before this profile is selected. */
    if (attacker && flag && retaliation && attacker->unit_state->attack1.type != ATK_NONE && (retaliation & 1u) &&
        (attacker->unit_state->attack1.targetsAllowed & flag)) return &attacker->unit_state->attack1;
    if (attacker && flag && retaliation && attacker->unit_state->attack2.type != ATK_NONE && (retaliation & 2u) &&
        (attacker->unit_state->attack2.targetsAllowed & flag)) return &attacker->unit_state->attack2;
    return attacker ? &attacker->unit_state->attack1 : NULL;
}

bool S_AttackProfileIsMelee(unitAttack_t const *attack) {
    if (!attack || attack->type == ATK_NONE) return false;
    /* A short-range instant weapon is a melee hit; stock ranged instant attacks
     * such as Riflemen are distinguished by their authored weapon range. */
    return attack->weapon == WPN_NORMAL ||
           (attack->weapon == WPN_INSTANT && attack->range <= 128.0f);
}
#define ACTIVE_ATTACK(ent) attack_profile((ent), (ent)->goalentity)

/* Spawn a projectile entity aimed at desc->target.
 * The entity is given MOVETYPE_FLYMISSILE so that SV_Physics_Toss() in
 * g_phys.c will move it each frame until it reaches the target. */
void fire_rocket(edict_t *ent, rocketDesc_t const *desc) {
    edict_t *rocket;
    vec2_t aim;

    if (!ent || !desc || (!desc->target && !desc->fixed_target)) return;
    rocket = G_Spawn();
    if (!rocket) return;
    aim = desc->fixed_target ? *desc->fixed_target : desc->target->s.origin2;
    rocket->s.origin = desc->start;
    rocket->s.angle = atan2f(aim.y - desc->start.y, aim.x - desc->start.x);
    rocket->s.model = desc->model;
    rocket->s.player = ent->s.player;
    G_InheritUnitTeamColor(rocket, ent);
    rocket->velocity = desc->speed / 1000.f;
    rocket->damage = desc->damage;
    G_InitMissile(rocket);
    rocket->missile->attack_type = desc->attack_type;
    rocket->missile->owner_spawn_time = ent->spawn_time;
    rocket->missile->arrow_ability = desc->arrow_ability;
    rocket->missile->arrow_level = desc->arrow_level;
    if (desc->fixed_target) {
        rocket->aiflags |= AI_PROJECTILE_FIXED_TARGET;
        channel_t *cast = G_AddChannel(rocket);
        cast->origin = *desc->fixed_target;
        /* ARTILLERY flies to the snapshotted point, but retaining the original
         * unit identity lets impact apply the ordinary primary-hit listeners
         * only when that same unit is still inside the splash bands. */
        rocket->goalentity = desc->target;
        cast->target_spawn_time = desc->target ? desc->target->spawn_time : 0;
        if (desc->attack) {
            artillery_t *splash = G_AddArtillery(rocket);
            splash->attack_type = desc->attack->type;
            splash->area_targets = desc->area_targets;
            splash->targets_allowed = desc->attack->targetsAllowed;
            splash->area_full = desc->attack->areaFull;
            splash->area_medium = desc->attack->areaMedium;
            splash->area_small = desc->attack->areaSmall;
            splash->factor_medium = desc->attack->factorMedium;
            splash->factor_small = desc->attack->factorSmall;
            splash->tree_attack_allowed = desc->tree_attack_allowed;
            if (desc->impaling_line && ent->data.UnitWeapons) {
                UnitWeapon_t const *weapon = desc->attack == &ent->unit_state->attack2 ?
                    &ent->data.UnitWeapons->attack2 : &ent->data.UnitWeapons->attack1;
                splash->spill_distance = MAX(0.0f, weapon->spillDistance);
                splash->spill_radius = MAX(0.0f, weapon->spillRadius);
                splash->line_origin = ent->s.origin2;
            }
        }
    } else {
        rocket->goalentity = desc->target;
    }
    /* Missile Splash follows a unit instead of the artillery impact point.
     * Both weapons snapshot their splash profile when the projectile launches,
     * so morphs, upgrades and target changes cannot change an in-flight hit. */
    /* Afak's authored splash belongs to empowered shots, not the Destroyer's
     * ordinary weapon. Stock Destroyers may author WPN_MSPLASH as the weapon
     * delivery type; do not apply its default splash when the Orb is off or
     * cannot be paid. Other splash weapons retain their ordinary profile. */
    bool const destroyer_orb_weapon = desc->allow_destroyer_orb &&
        desc->attack && desc->attack->weapon == WPN_MSPLASH &&
        G_UnitAbilityLevel(ent, MAKEFOURCC('A','f','a','k')) != 0;
    if (desc->attack && (desc->attack->weapon == WPN_ARTILLERY ||
                         (desc->attack->weapon == WPN_MSPLASH && !destroyer_orb_weapon))) {
        artillery_t *splash = G_AddArtillery(rocket);
        splash->attack_type = desc->attack->type;
        splash->area_targets = desc->area_targets;
        splash->targets_allowed = desc->attack->targetsAllowed;
        splash->area_full = desc->attack->areaFull;
        splash->area_medium = desc->attack->areaMedium;
        splash->area_small = desc->attack->areaSmall;
        splash->factor_medium = desc->attack->factorMedium;
        splash->factor_small = desc->attack->factorSmall;
    }
    /* Afak supplies its own splash rings even when the unit's weapon is a
     * plain missile. The generic Missile Splash route then applies attack
     * damage once to the primary and falloff to the other valid targets. */
    if (desc->allow_destroyer_orb && desc->target && !desc->fixed_target) {
        uint32_t code = 0, level = 0;
        int bonus = 0;
        if (S_DestroyerOrbCommitAttack(ent, desc->target, &code, &level, &bonus)) {
            artillery_t *splash = G_GetArtillery(rocket);
            cstring_t art = G_AbilityEffectArt(code, WC3_EFFECT_MISSILE, 0);
            cstring_t speed_text = S_SpellString(code, "Missilespeed", 0);
            float speed = speed_text ? (float)atof(speed_text) : 0.0f;
            if (!splash) splash = G_AddArtillery(rocket);
            if (splash) {
                splash->attack_type = desc->attack_type;
                splash->targets_allowed = desc->attack->targetsAllowed;
                splash->area_targets = desc->area_targets;
                splash->area_full = MAX(0.0f, S_SpellData(code, level, 4));
                splash->area_medium = MAX(0.0f, S_SpellData(code, level, 5));
                splash->area_small = MAX(0.0f, S_SpellNumber(code, ABILITY_NUMBER_AREA, level));
                splash->factor_medium = MAX(0.0f, S_SpellData(code, level, 2));
                splash->factor_small = MAX(0.0f, S_SpellData(code, level, 3));
            }
            rocket->damage = (uint32_t)MAX(0, (int)rocket->damage + bonus);
            if (art && *art) rocket->s.model = G_RegisterModel(art);
            if (isfinite(speed) && speed > 0.0f) rocket->velocity = speed / 1000.0f;
        }
    }
    /* Burning Oil is a unit upgrade, so make its projectile choice at the
     * final attack-projectile construction point. This keeps the authored
     * missile/model and speed stable after all other launch modifiers. */
    if (S_BurningOilEnabled(ent)) {
        cstring_t const art = S_BurningOilMissileArt(ent);
        cstring_t const speed_text = FindConfigValue("Abof", "Missilespeed");
        float const speed = speed_text ? (float)atof(speed_text) : 0.0f;
        if (art && *art) rocket->s.model = G_RegisterModel(art);
        if (isfinite(speed) && speed > 0.0f) rocket->velocity = speed / 1000.0f;
    }
    rocket->owner = ent;
    rocket->movetype = MOVETYPE_FLYMISSILE;
    G_StartProjectilePresentation(rocket);
//    rocket->clipmask = MASK_SHOT;
//    rocket->solid = SOLID_BBOX;
//    rocket->s.effects |= EF_ROCKET;
//    VectorClear (rocket->mins);
//    VectorClear (rocket->maxs);
//    rocket->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
//    rocket->owner = self;
//    rocket->touch = rocket_touch;
//    rocket->nextthink = level.time + 8000/speed;
//    rocket->think = G_FreeEdict;
//    rocket->dmg = damage;
//    rocket->radius_dmg = radius_damage;
//    rocket->dmg_radius = damage_radius;
//    rocket->s.sound = gi.soundindex ("weapons/rockfly.wav");
//    rocket->classname = "rocket";
//
//    if (self->client)
//        check_dodge (self, rocket->s.origin, dir, speed);
//
//    gi.linkentity (rocket);
}

static float ai_rolldamage1(edict_t *self, int weapon) {
    unitAttack_t const *atk = ACTIVE_ATTACK(self);
    float damageBase = atk->damageBase;
    (void)weapon;
    FOR_LOOP(i, atk->numberOfDice) {
        /* Warsmash treats a malformed zero-sided die as contributing +1
         * instead of taking modulo zero. Normal Warcraft data has S > 0. */
        damageBase += atk->sidesPerDie
                    ? (float)(rand() % atk->sidesPerDie + 1)
                    : 1.0f;
    }
    return damageBase + atk->temporaryDamageBonus;
}

void M_GetEntityMatrix(entityState_t const *entity, mat4_t *matrix) {
    Matrix4_identity(matrix);
    Matrix4_translate(matrix, &entity->origin);
    Matrix4_rotate(matrix, &(vec3_t){0, 0, entity->angle * 180 / M_PI}, ROTATE_XYZ);
    Matrix4_scale(matrix, &(vec3_t){entity->scale, entity->scale, entity->scale});
}

static uint32_t attack_order_mask(edict_t const *ent) {
    return (S_UnitAttackSlotEnabled(ent, 0) ? 1u : 0) | (S_UnitAttackSlotEnabled(ent, 1) ? 2u : 0);
}

/* Counterattacks use the same slots as orders, except that Root keeps a
 * rooted Ancient's weapons for retaliation while hiding its Attack order. */
static uint32_t attack_retaliation_mask(edict_t const *ent) {
    return S_AncientHasRootAbility(ent) ? S_AncientRetaliationAttackMask(ent) : attack_order_mask(ent);
}

static bool attack_mask_has_weapon(edict_t const *ent, uint32_t mask) {
    return ((mask & 1u) && ent->unit_state->attack1.type != ATK_NONE) || ((mask & 2u) && ent->unit_state->attack2.type != ATK_NONE);
}

static bool can_attack(edict_t const *ent) {
    if (S_LavaSpawnIsSplitting(ent) || S_UnitIsFreezingBreathFrozen(ent) || unit_hasstatusstate(ent, WC3_STATUS_STATE_ETHEREAL) ||
        S_UnitIsCycloned(ent) || S_UnitIsEntanglingRooted(ent) || G_BuildingIsUnsummoning(ent)) return false;
    if (!S_HumanCanAttack(ent) || !S_CargoAttacksEnabled(ent)) return false;
    if (!attack_mask_has_weapon(ent, attack_retaliation_mask(ent))) return false;
    return !ent->currentmove || ent->currentmove->proc != CAbilityAttack;
}

/* Weapon target masks are authoritative for ordinary unit targets as well as
 * destructables.  UnitData.targetType supplies the target category while
 * UnitWeapons.targs1/ua1g supplies the attacker's allowed categories. */
static bool attack_can_target_mask(edict_t const *attacker, edict_t const *target, uint32_t mask) {
    uint32_t flag;
    if (!attacker || S_LavaSpawnIsSplitting(attacker) || S_UnitIsFreezingBreathFrozen(attacker) || G_BuildingIsUnsummoning(attacker) || !target || !target->inuse || attacker == target ||
        !attack_mask_has_weapon(attacker, mask) || S_UnitIsCycloned(target) ||
        (S_ImpaleIsAirborne(target) && !S_ImpaleAirborneVulnerable(target)) ||
        unit_hasstatusstate(attacker, WC3_STATUS_STATE_ETHEREAL) ||
        (unit_hasstatusstate(target, WC3_STATUS_STATE_ETHEREAL) &&
         !(((mask & 1u) && attacker->unit_state->attack1.type == ATK_MAGIC &&
             (attacker->unit_state->attack1.targetsAllowed & G_TargetFlagForType(G_UnitTargetType(target)))) ||
           ((mask & 2u) && attacker->unit_state->attack2.type == ATK_MAGIC &&
             (attacker->unit_state->attack2.targetsAllowed & G_TargetFlagForType(G_UnitTargetType(target))))))) return false;
    if (S_UnitIsHiddenFromPlayer(target, attacker->s.player)) return false;
    if (G_GetDestructable(target)) return G_DestructableCanBeAttackedBy(attacker, target);
    if (M_IsDead((edict_t *)target)) return false;
    flag = G_TargetFlagForType(G_UnitTargetType(target));
    return flag && (((mask & 1u) && attacker->unit_state->attack1.type != ATK_NONE && (attacker->unit_state->attack1.targetsAllowed & flag)) ||
                    ((mask & 2u) && attacker->unit_state->attack2.type != ATK_NONE && (attacker->unit_state->attack2.targetsAllowed & flag)));
}

bool S_AttackCanTarget(edict_t const *attacker, edict_t const *target) {
    return attack_can_target_mask(attacker, target, attacker ? attack_order_mask(attacker) : 0);
}

/* Unlike an explicit Attack order, a counterattack never targets the
 * attacker's own units (e.g. after Charm changes the damage source owner). */
static bool attack_can_retaliate(edict_t const *attacker, edict_t const *target) {
    return attacker && target && attacker->s.player != target->s.player &&
        attack_can_target_mask(attacker, target, attack_retaliation_mask(attacker));
}

/* Delayed damage can outlive its attack order; only that order may complete or resume its parent behavior. */
static void attack_finish_after_combat(edict_t *attacker, edict_t const *target, cstring_t reason) {
    if (!attacker || M_IsDead(attacker) || !attacker->currentmove ||
        attacker->currentmove->proc != CAbilityAttack || attacker->goalentity != target) return;
    G_BotTraceAssaultUnit(attacker, "assault_attack_ended",
        "reason=%s target=%u target_inuse=%d target_alive=%d target_type=%c%c%c%c target_spawn_matches=%d distance=%.1f retained_attackmove=%d patrol=%d follow=%d",
        reason ? reason : "unspecified", target ? target->s.number : UINT32_MAX,
        target && target->inuse, target && target->inuse && !M_IsDead((edict_t *)target),
        target ? (char)(target->class_id & 255) : '-',
        target ? (char)((target->class_id >> 8) & 255) : '-',
        target ? (char)((target->class_id >> 16) & 255) : '-',
        target ? (char)((target->class_id >> 24) & 255) : '-',
        target && attacker->unit_state->attack_target_spawn_time == target->spawn_time,
        target ? Vector2_distance(&attacker->s.origin2, &target->s.origin2) : 0.0f,
        attacker->orders->attackmove_waypoint != NULL,
        attacker->orders->patrol_a != NULL, attacker->orders->follow_target != NULL);
    unit_leavecombat(attacker);
    attacker->goalentity = NULL;
    attacker->unit_state->attack_target_spawn_time = 0;
    attacker->orders->explicit_allied_attack = false;
    if (G_BuildingIsUnsummoning(attacker)) {
        attacker->currentmove = NULL;
        attacker->animation = NULL;
        attacker->wait = 0;
        return;
    }
    if (attacker->orders->patrol_a) {
        order_patrol_resume(attacker);
    } else if (attacker->orders->attackmove_waypoint) {
        order_attackmove(attacker, attacker->orders->attackmove_waypoint);
    } else if (attacker->orders->follow_target) {
        order_follow_resume(attacker);
    } else if (G_CreepGuardCombatEnd(attacker)) {
        return;
    } else if (S_UnitAbilityEvent(attacker, A_AUTO_COMBAT_END)) {
        return;
    } else if (attacker->stand) {
        attacker->stand(attacker);
    }
}

static bool attack_stop_if_target_invalid(edict_t *attacker) {
    edict_t const *target = attacker ? attacker->goalentity : NULL;
    bool allied = attacker && target && attacker->s.player < MAX_PLAYERS &&
        target->s.player < MAX_PLAYERS && attacker->s.player != target->s.player &&
        G_PlayerTreatsPlayerAsAlly(attacker->s.player, target->s.player) &&
        !attacker->orders->explicit_allied_attack;
    /* Existing attack orders are combat orders, unlike the explicit Attack
     * command which may deliberately target an allied unit.  Alliance changes
     * must therefore end an automatic/cinematic attack before its next hit. */
    if ((S_AttackCanTarget(attacker, target) || attack_can_retaliate(attacker, target)) && !allied &&
        attacker->unit_state->attack_target_spawn_time == target->spawn_time) {
        return false;
    }
    if (attacker) attack_finish_after_combat(attacker, attacker->goalentity, "target_invalid");
    return true;
}

/* Stock fallback for attack-type × defense-type values. Production games load
 * the active table from MiscGame/war3mapMisc into game.constants; these values
 * keep unit-level tests and early bootstrap callers deterministic. */
static float const g_default_damage_table[8][8] = {
    /* BZ_HARDCODED_DATA_FALLBACK: WC3 1.29 / Warsmash defaults. */
    /* small  medium large  fort   normal hero   divine none  */
    { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f }, /* none   */
    { 1.00f, 1.50f, 1.00f, 0.70f, 1.00f, 1.00f, 0.05f, 1.00f }, /* normal */
    { 2.00f, 0.75f, 1.00f, 0.35f, 1.00f, 0.50f, 0.05f, 1.50f }, /* pierce */
    { 1.00f, 0.50f, 1.00f, 1.50f, 1.00f, 0.50f, 0.05f, 1.50f }, /* siege  */
    { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.70f, 0.05f, 1.00f }, /* spells */
    { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f }, /* chaos  */
    { 1.25f, 0.75f, 2.00f, 0.35f, 1.00f, 0.50f, 0.05f, 1.00f }, /* magic  */
    { 1.00f, 1.00f, 1.00f, 0.50f, 1.00f, 1.00f, 0.05f, 1.00f }, /* hero   */
};

/* Shared attack-type/defense-type multiplier, independently of numeric armor.
 * Enhanced-physical spells use ATK_SPELLS but deliberately skip armor. */
float G_AttackTypeDamageMultiplier(uint32_t atk, uint32_t def) {
    if (atk >= 8) atk = ATK_NONE;
    if (def >= 8) def = 7;
    return game.constants.combatConstantsLoaded
        ? game.constants.damageBonus[atk][def]
        : g_default_damage_table[atk][def];
}

/* The tooltip may use retail/localized prose only while the actual map's
 * multipliers agree with the same stock table used for fallback combat. */
bool G_AttackTypeDamageMultiplierIsDefault(uint32_t atk, uint32_t def) {
    if (atk >= 8 || def >= 8) return false;
    return fabsf(G_AttackTypeDamageMultiplier(atk, def) - g_default_damage_table[atk][def]) < 0.0001f;
}

/* Apply the Warsmash/WC3 damage formula: active attack×defense multiplier,
 * then numeric armor. Positive armor is 1/(1+K*A); negative armor uses the
 * Warcraft exponential curve 2-(1-K)^(-A). Result remains minimum 1 for the
 * existing OpenRealm physical-attack contract. */
static int attack_damage_type(edict_t *attacker, edict_t *target, int base, uint32_t atk);
int G_AttackDamage(edict_t *attacker, edict_t *target, int base) {
    return attack_damage_type(attacker, target, base,
                              attacker && attacker->unit_state && target ? attack_profile(attacker, target)->type : 0);
}

int G_AttackDamageWithType(edict_t *attacker, edict_t *target, int base, uint32_t type) {
    return attack_damage_type(attacker, target, base, type);
}

static int attack_damage_type(edict_t *attacker, edict_t *target, int base, uint32_t atk) {
    if (!attacker || !target || base <= 0) return base;
    uint32_t def = target->unit_state ? target->unit_state->defense_type : 0;
    float const mult = G_AttackTypeDamageMultiplier(atk, def);
    float const armor_coefficient = game.constants.combatConstantsLoaded
                                  ? game.constants.defenseArmor
                                  : 0.06f;
    float dmg = (float)base * S_TinkerDemolishMultiplier(attacker, target) * mult;
    /* Ethereal receivers reject physical attack damage, and amplify magic attacks.
     * Source: Blizzard Warcraft III spell basics (66% bonus). */
    if (unit_hasstatusstate(target, WC3_STATUS_STATE_ETHEREAL)) {
        if (atk != ATK_MAGIC && atk != ATK_SPELLS) return 0;
        dmg *= 1.66f;
    }
    float armor = G_UnitArmorValue(target);
    if (armor >= 0.0f)
        dmg = dmg / (1.0f + armor * armor_coefficient);
    else
        dmg = dmg * (2.0f - powf(1.0f - armor_coefficient, -armor));
    int result = (int)dmg;
    return result < 1 ? 1 : result;
}

/* Apply damage to target from attacker.
 * If the hit is lethal, the target's die() callback is invoked and the
 * attacker returns to its stand (idle) state.  Otherwise, if the target is
 * able to attack back it issues an automatic counter-attack order. */
static void T_DamageInternal(edict_t *target, edict_t *attacker, int damage, bool primary_attack,
                             uint32_t arrow_code, uint32_t arrow_level) {
    bool instant_kill;

    if (!G_UnitIsWorldActive(target) || G_UnitHasFlag(target, UNIT_FLAG_INVULNERABLE) ||
        unit_hasstatusstate(target, WC3_STATUS_STATE_INVULNERABLE) ||
        S_UnitIsCycloned(target) || M_IsDead(target)) {
        return;
    }
    /* Instant-kill follows the same combat path for units and attackable destructables; the old
     * SVF_MONSTER target gate accidentally excluded gates, trees, and crates from the cheat. */
    instant_kill = attacker && (attacker->svflags & SVF_MONSTER) &&
                   ((target->svflags & SVF_MONSTER) ||
                    (G_IsDestructable(target) && G_DestructableCanBeAttackedBy(attacker, target))) &&
                   G_PlayerInstantKill(attacker->s.player);
    damage = S_ManaShieldDamage(target, damage);
    if (instant_kill) damage = MAX(damage, (int)ceilf(target->health.value));
    if (damage <= 0) return;
    if (G_IsDestructable(target)) {
        if (G_DestructableApplyDamage(target, attacker, (float)damage)) {
            attack_finish_after_combat(attacker, target, "destructable_destroyed");
        }
        return;
    }
    damage = S_SpiritLinkRedirect(target, attacker, damage);
    if (damage <= 0) return;
    damage = MAX(1, (int)ceilf((float)damage *
        (1.0f + unit_status_modifier_total(target, WC3_STATUS_MOD_DAMAGE_TAKEN_PERCENT))));
    /* Primary attack only: shields, Spirit Link, damage immunities and overkill
     * have already been resolved. Award before death can free this edict. */
    if (primary_attack && attacker)
        S_PillageOnAttackDamage(attacker, target, MIN(target->health.value, (float)damage));
    /* GetEventDamage / GetEventDamageSource read value/source from these events. */
    G_PublishEventWithValue(target, EVENT_UNIT_DAMAGED, attacker, damage);
    G_PublishEventWithValue(target, EVENT_PLAYER_UNIT_DAMAGED, attacker, damage);
    /* Only real post-mitigation unit damage should refresh the owning Hero shortcut's transient attack warning. */
    G_AlertHeroShortcutDamage(target);
    FOR_LOOP(i, target->unit_state ? MAX_UNIT_STATUSES : 0)
        if (target->unit_state->statuses[i].level && target->unit_state->statuses[i].code == MAKEFOURCC('B','U','s','l'))
            unit_removestatus(target, target->unit_state->statuses + i, STATUS_REMOVE_SCRIPT);
    unit_updatestatuses(target);
    /* A real, positive, post-shield/Spirit-Link Black Arrow hit marks the
     * victim before lethal damage dispatches A_STATUS_DEATH. Projectile
     * misses and secondary/spell damage cannot apply the marker. */
    if (primary_attack && arrow_code && arrow_level)
        S_BlackArrowMarkHit(attacker, target, arrow_code, arrow_level);
    unit_entercombat(attacker, target);
    unit_entercombat(target, attacker);

    /* Notify the camp while the victim is still alive: killing blows must
     * alert surviving allies too. This is a single non-recursive fan-out. */
    G_CreepGuardCallForHelp(target, attacker);
    if (target->health.value <= damage) {
        G_SetHealth(target, 0);
        unit_leavecombat(target);
        target->die(target, attacker);
        attack_finish_after_combat(attacker, target, "target_killed");
        return;
    }
    G_AddHealth(target, -damage);
    G_CreepGuardDamaged(target);
    /* Only a survivor reacts to the hit. A killing blow goes straight to die();
     * dispatching first would let Awan start a flee Move that death tears down. */
    {
        abilityCall_t call = MAKE(abilityCall_t, .entity = attacker);
        S_UnitAbilityEventWithCall(target, A_DAMAGED, &call);
    }
    if (can_attack(target) && !unit_is_walking(target) &&
        target->orders->creep_guard_phase != CREEP_GUARD_RETURNING &&
        S_SpellIsEnemy(target, attacker)) {
        if (!S_UnitAbilityEvent(target, A_NO_RETALIATE)) {
            S_UnitAbilityEvent(target, A_AUTO_COMBAT_START);
            order_attack_internal(target, attacker, true);
            if (target->goalentity == attacker && target->currentmove &&
                target->currentmove->proc == CAbilityAttack)
                G_CreepGuardAutoCombat(target);
        }
    } else if (target->pain) {
        target->pain(target);
    }
}

void T_Damage(edict_t *target, edict_t *attacker, int damage) {
    T_DamageInternal(target, attacker, damage, false, 0, 0);
}

/* Secondary hits carry the primary weapon's type, not a different weapon selected for the victim.
 * Previously only the primary hit rejected physical damage against Ethereal. */
bool S_AttackHitAllowed(edict_t const *attacker, edict_t const *primary, edict_t const *target) {
    return !unit_hasstatusstate(attacker, WC3_STATUS_STATE_ETHEREAL) &&
           (!unit_hasstatusstate(target, WC3_STATUS_STATE_ETHEREAL) || attack_profile(attacker, primary)->type == ATK_MAGIC);
}

void S_ResolveAttackHitWithArrow(edict_t *attacker, edict_t *target, int damage,
                                 uint32_t arrow_code, uint32_t arrow_level) {
    float const initial_health = target ? target->health.value : 0.0f;
    /* A missile can reach a corpse after another hit has killed its target.
     * Such a hit must not produce a second Dark Minion. */
    if (!attacker || !target || M_IsDead(target) ||
        !S_AttackHitAllowed(attacker, target, target)) return;
    if (S_EvasionRoll(target) || S_BrewmasterHazeMiss(attacker)) return;
    { float const miss = S_CurseMissChance(attacker); if (miss > 0.0f && (float)(rand() % 100) < miss * 100.0f) return; }
    S_HumanBreakInvisibility(attacker);
    S_PermanentInvisibilityReveal(attacker);
    damage = S_CriticalStrikeDamage(attacker, damage);
    damage = S_BlackArrowDamageForShot(arrow_code, arrow_level, damage);
    /* Only one arrow modifier owns an empowered attack. A Black Arrow
     * snapshot must not pick up a new Searing/Frost autocast mid-flight. */
    if (!arrow_code)
        damage = S_ColdArrowsDamage(attacker, target, S_SearingArrowDamage(attacker, damage));
    /* ANak is independent of the mana-paid Afak missile modifier: only the
     * former belongs in this generic on-hit path. */
    damage = S_OrbAnnihilationDamage(attacker, damage);
    damage = (int)((float)damage * (1.0f + S_TrueshotAttackBonus(attacker) + S_CommandAuraAttackBonus(attacker) +
                                         S_WarDrumsAttackBonus(attacker) + S_RoarDamageBonus(attacker)
                                         + unit_status_modifier_total(attacker, WC3_STATUS_MOD_ATTACK_DAMAGE_PERCENT)
                                         - S_SoulBurnDamageReduction(attacker)));
    damage = S_HumanAttackDamage(attacker, target, damage);
    if (damage <= 0) return;
    { abilityAliasRef_t bash = S_ResolveAbilityAlias(attacker, MAKEFOURCC('A', 'H', 'b', 'h'));
    if (bash.alias && bash.level && (float)(rand() % 100) < S_SpellData(bash.alias, bash.level, 1)) {
        damage += (int)S_SpellData(bash.alias, bash.level, 3);
        S_SpellApplyStun(target, S_SpellDuration(bash.alias, bash.level, false));
    } }
    damage += (int)S_UnitStatusAbilityEvent(attacker, A_ATTACK_DAMAGE_BONUS, NULL);
    S_UnitStatusAbilityEvent(attacker, A_ATTACK_LANDED, NULL);
    damage = S_PossessionDamageTaken(target, damage);
    damage = S_HardenedSkinDamage(target, damage);
    if (damage <= 0) return;
    G_WC3_AttackAlert(target, attacker);
    G_PlayCombatImpactSound(attacker, target);
    S_IncinerateOnHit(attacker, target);
    T_DamageInternal(target, attacker, damage, true, arrow_code, arrow_level);
    /* Lava Spawn counts confirmed landed attacks, not projectile launches,
     * misses, evades or an attack that was prevented before damage. */
    if (attacker->inuse && !M_IsDead(attacker)) S_LavaSpawnAttackHit(attacker);
    if (target->inuse && G_UnitIsStructure(target) && S_SpellIsEnemy(attacker, target) &&
        target->health.value < initial_health) {
        static uint32_t const ability = MAKEFOURCC('A','f','r','z');
        static uint32_t const research = MAKEFOURCC('R','u','f','b');
        uint32_t const level = G_UnitAbilityLevel(attacker, ability);
        gameClient_t *owner = G_GetPlayerClientByNumber(attacker->s.player);
        /* Afrz exists before TFT research; only researched Rufb grants its building-hit effect. */
        if (level && owner && G_GetPlayerTechResearchedLevel(owner, research) > 0) {
            cstring_t buff = S_SpellBuffId(ability, level);
            float const duration = S_SpellDuration(ability, level, false);
            if (!buff || strlen(buff) < 4) buff = "Bfrz";
            if (duration > 0.0f)
                S_SpellApplyTimedTargetStatus(target, ability, level, buff, duration);
        }
    }
    S_CreepAttackOnHit(attacker, target);
    S_PulverizeAttack(attacker, target);
    S_HumanAttackSplash(attacker, target, damage);
    { uint32_t cleave_code = G_UnitAbilityLevel(attacker, MAKEFOURCC('A','N','c','a')) ?
            MAKEFOURCC('A','N','c','a') : MAKEFOURCC('A','C','c','e');
    uint32_t cleave_level = G_UnitAbilityLevel(attacker, cleave_code);
    /* Cleave is a melee physical attack modifier. Its authored target mask
     * (ground/enemy/neutral) also applies to secondary splash victims. */
    if (cleave_level && S_AttackProfileIsMelee(attack_profile(attacker, target))) {
        float radius = S_SpellNumber(cleave_code, ABILITY_NUMBER_AREA, cleave_level);
        float fraction = S_SpellData(cleave_code, cleave_level, 1);
        char name[5];
        cstring_t attachment;
        uint32_t base = G_AbilityCode(cleave_code);
        memcpy(name, &cleave_code, 4); name[4] = '\0';
        attachment = FindConfigValue(name, "Specialattach");
        if ((!attachment || !*attachment) && base != cleave_code) {
            memcpy(name, &base, 4); name[4] = '\0';
            attachment = FindConfigValue(name, "Specialattach");
        }
        /* Stock ANca/ACce use chest; custom rows can override this socket. */
        if (!attachment || !*attachment) attachment = "chest";
        FILTER_EDICTS(other, other != target && other != attacker && S_SpellIsAliveTarget(other) &&
                      S_AttackHitAllowed(attacker, target, other) &&
                      S_SpellAllowsAreaTarget(cleave_code, attacker, other) &&
                      Vector2_distance(&other->s.origin2, &target->s.origin2) <= radius) {
            G_SpawnAbilityEffectTarget(cleave_code, WC3_EFFECT_SPECIAL, 0, other, attachment, true);
            T_Damage(other, attacker, (int)MAX(1.0f, damage * fraction));
        }
    } }
    S_MoonGlaiveAttack(attacker, target, damage);
    S_SlowPoisonOnHit(attacker, target);
    S_OrbOnHit(attacker, target);
    S_PoisonOnHit(attacker, target);
    {
        unitAttack_t const *profile = attack_profile(attacker, target);
        if (S_AttackProfileIsMelee(profile))
            G_AddHealth(attacker, damage * S_VampiricLifeSteal(attacker));
    }
    if (target->inuse && attacker->inuse && !M_IsDead(attacker) &&
        S_SpellIsEnemy(target, attacker)) {
        /* Damage returned by a normal melee attack is a secondary damage
         * event, never another attack hit. Ranged attacks and spell impacts
         * cannot trigger it even when they occur at point-blank range. */
        unitAttack_t const *profile = attack_profile(attacker, target);
        if (profile && (profile->weapon == WPN_NORMAL || profile->weapon == WPN_INSTANT)) {
            float const thorns = S_ThornsMeleeDamageReturn(target, damage);
            float const carapace = S_SpikedCarapaceDamageReturn(target, damage);
            float const barricades = S_SpikedBarricadesDamageReturn(target, damage);
            if (thorns > 0.0f) T_Damage(attacker, target, (int)thorns);
            if (carapace > 0.0f && attacker->inuse && !M_IsDead(attacker))
                T_Damage(attacker, target, (int)carapace);
            /* Spikes are magic retaliation: go through spell immunity and
             * anti-magic absorption, not the plain physical-damage path. */
            if (barricades > 0.0f && attacker->inuse && !M_IsDead(attacker))
                S_SpellDamage(attacker, target, (int)barricades);
        }
    }
}

void S_ResolveAttackHit(edict_t *attacker, edict_t *target, int damage) {
    S_ResolveAttackHitWithArrow(attacker, target, damage, 0, 0);
}

/* Target-compatible weapon data is shared by the damage roll and swarm
 * steering, including custom locust units with only Attack 2 enabled. */
unitAttack_t const *S_LocustAttackWeapon(edict_t const *locust, edict_t const *target) {
    return attack_profile(locust, target);
}

/* Locust Swarm attacks use the same weapon selection, damage roll, armor,
 * evasion, shields and primary-hit callbacks as an ordinary weapon hit.
 * HP delta (capped by the victim's pre-hit life) is the healing source, not
 * the nominal roll. This also counts no damage when a hit misses or is
 * negated; a reallocated target slot can never award another entity's HP. */
float S_LocustAttackHit(edict_t *locust, edict_t *target) {
    uint32_t spawn_time;
    float health_before, health_after;
    int damage;
    if (!locust || !target || !locust->inuse || !target->inuse ||
        !locust->unit_state || !S_AttackCanTarget(locust, target) ||
        !S_SpellIsEnemy(locust, target) ||
        G_UnitHasFlag(target, UNIT_FLAG_INVULNERABLE) ||
        unit_hasstatusstate(target, WC3_STATUS_STATE_INVULNERABLE)) return 0.0f;
    spawn_time = target->spawn_time;
    health_before = MAX(0.0f, target->health.value);
    damage = G_AttackDamage(locust, target, (int)ai_rolldamage1(locust, 1));
    if (damage <= 0) return 0.0f;
    S_ResolveAttackHit(locust, target, damage);
    if (!target->inuse || target->spawn_time != spawn_time) return 0.0f;
    health_after = MAX(0.0f, target->health.value);
    return MAX(0.0f, MIN(health_before, health_before - health_after));
}

static bool artillery_splash_target_allowed(edict_t *attacker, edict_t *target, uint32_t mask, uint32_t targets_allowed) {
    uint32_t flag;

    if (!attacker || !target || !target->inuse || target == attacker || M_IsDead(target)) return false;
    if (!mask) mask = targets_allowed;
    flag = G_TargetFlagForType(G_UnitTargetType(target));
    return flag && (mask & flag) != 0;
}

/* Artillery snapshots an impact point, while Missile Splash resolves around a
 * homing unit on arrival. Both use the authored rings and only the primary
 * victim receives attack-hit listeners. */
static void resolve_splash_point_hit(edict_t *attacker, edict_t *primary, vec2_t const *impact, int raw_damage,
                                     struct artillery_s const *profile, bool missile_splash,
                                     uint32_t arrow_code, uint32_t arrow_level) {
    float max_radius;
    bool primary_hit = false;

    if (!attacker || !impact || raw_damage <= 0 || !profile) return;
    if (!missile_splash) {
        artillery_spawn_meat_wagon_impact(attacker, impact);
        artillery_spawn_meat_wagon_plague_cloud(attacker, impact);
        S_BurningOilImpact(attacker, impact);
    }
    max_radius = MAX(profile->area_full, MAX(profile->area_medium, profile->area_small));
    if (max_radius < 0.0f) return;

    FILTER_EDICTS(other, artillery_splash_target_allowed(attacker, other, profile->area_targets, profile->targets_allowed) &&
                  (!missile_splash || other == primary || S_SpellIsEnemy(attacker, other))) {
        float const distance = MAX(0.0f, Vector2_distance(&other->s.origin2, impact) - MAX(0.0f, other->collision));
        float factor;
        int damage;

        if (distance <= profile->area_full) factor = 1.0f;
        else if (distance <= profile->area_medium) factor = profile->factor_medium;
        else if (distance <= profile->area_small) factor = profile->factor_small;
        else continue;
        if (factor <= 0.0f) continue;
        damage = attack_damage_type(attacker, other, (int)MAX(1.0f, (float)raw_damage * factor), profile->attack_type);
        if (other == primary) {
            primary_hit = true;
            S_ResolveAttackHitWithArrow(attacker, other, damage, arrow_code, arrow_level);
        } else {
            T_Damage(other, attacker, damage);
        }
    }
    if (missile_splash && !primary_hit && primary && primary->inuse && primary != attacker && !M_IsDead(primary)) {
        int const damage = attack_damage_type(attacker, primary, raw_damage, profile->attack_type);
        S_ResolveAttackHitWithArrow(attacker, primary, damage, arrow_code, arrow_level);
    }
    /* A direct, researched tree attack is legal even if the weapon's splash
     * target mask omits TREE. Follow-through never damages trees. */
    if (!primary_hit && primary && G_DestructableIsAttackable(primary) &&
        primary->targtype == TARG_TREE && profile->tree_attack_allowed) {
        S_ResolveAttackHitWithArrow(attacker, primary,
            attack_damage_type(attacker, primary, raw_damage, profile->attack_type), arrow_code, arrow_level);
    }
    /* Impaling Bolt is a separate band extending *behind* the original unit
     * target, along the projectile's captured launch direction. Do not apply
     * splash and penetration twice to the same victim. */
    if (profile->spill_distance > 0.0f && profile->spill_radius > 0.0f &&
        primary && !G_IsDestructable(primary)) {
        float const dx = impact->x - profile->line_origin.x;
        float const dy = impact->y - profile->line_origin.y;
        float const length = sqrtf(dx * dx + dy * dy);
        if (length > 0.001f) {
            float const nx = dx / length, ny = dy / length;
            float const splash = MAX(profile->area_full, MAX(profile->area_medium, profile->area_small));
            FILTER_EDICTS(other, artillery_splash_target_allowed(attacker, other, profile->area_targets, profile->targets_allowed) &&
                          other != primary && !G_IsDestructable(other)) {
                float const ox = other->s.origin2.x - impact->x;
                float const oy = other->s.origin2.y - impact->y;
                float const along = ox * nx + oy * ny;
                float const lateral = fabsf(ox * ny - oy * nx);
                float const collision = MAX(0.0f, other->collision);
                if (along <= 0.0f || along > profile->spill_distance + collision ||
                    lateral > profile->spill_radius + collision ||
                    Vector2_distance(&other->s.origin2, impact) - collision <= splash) continue;
                T_Damage(other, attacker,
                    attack_damage_type(attacker, other, raw_damage, profile->attack_type));
            }
        }
    }
}

void S_ResolveArtilleryPointHit(edict_t *attacker, edict_t *primary, vec2_t const *impact, int raw_damage,
                                struct artillery_s const *profile) {
    resolve_splash_point_hit(attacker, primary, impact, raw_damage, profile, false, 0, 0);
}

void S_ResolveArtilleryPointHitWithArrow(edict_t *attacker, edict_t *primary, vec2_t const *impact,
                                        int raw_damage, struct artillery_s const *profile,
                                        uint32_t arrow_code, uint32_t arrow_level) {
    resolve_splash_point_hit(attacker, primary, impact, raw_damage, profile, false, arrow_code, arrow_level);
}

void S_ResolveMissileSplashHit(edict_t *attacker, edict_t *primary, vec2_t const *impact, int raw_damage,
                               struct artillery_s const *profile) {
    resolve_splash_point_hit(attacker, primary, impact, raw_damage, profile, true, 0, 0);
}

void S_ResolveMissileSplashHitWithArrow(edict_t *attacker, edict_t *primary, vec2_t const *impact,
                                       int raw_damage, struct artillery_s const *profile,
                                       uint32_t arrow_code, uint32_t arrow_level) {
    resolve_splash_point_hit(attacker, primary, impact, raw_damage, profile, true, arrow_code, arrow_level);
}

void S_ResolveArtilleryHit(edict_t *attacker, edict_t *target, int raw_damage) {
    vec2_t impact;
    unitAttack_t const *atk;
    struct artillery_s profile = { 0 };
    if (!target) return;
    atk = attack_profile(attacker, target);
    if (!atk) return;
    profile.attack_type = atk->type;
    profile.targets_allowed = atk->targetsAllowed;
    profile.area_full = atk->areaFull; profile.area_medium = atk->areaMedium; profile.area_small = atk->areaSmall;
    profile.factor_medium = atk->factorMedium; profile.factor_small = atk->factorSmall;
    if (attacker->data.UnitWeapons)
        profile.area_targets = atk == &attacker->unit_state->attack2 ? attacker->data.UnitWeapons->attack2.areaTargets
                                                       : attacker->data.UnitWeapons->attack1.areaTargets;
    impact = target->s.origin2;
    S_ResolveArtilleryPointHit(attacker, target, &impact, raw_damage, &profile);
}

static bool attack_animation_can_finish(edict_t const *ent) {
    return ent && ent->animation && ent->animation->interval[1] > ent->animation->interval[0];
}

static void attack_set_backswing_deadline(edict_t *ent) {
    uint32_t duration = (uint32_t)ceilf(MAX(0.0f, ACTIVE_ATTACK(ent)->backswingPoint /
                                                      attack_speed_divisor(ent)) * 1000.0f);
    ent->unit_state->attack_backswing_end_time = G_Time() + duration;
}

static float attack_backswing_remaining(edict_t const *ent) {
    int32_t remaining = ent ? (int32_t)(ent->unit_state->attack_backswing_end_time - G_Time()) : 0;
    return remaining > 0 ? remaining / 1000.0f : 0.0f;
}

static void damage_target(edict_t *ent) {
    if (S_UnitIsEntanglingRooted(ent)) return;
    if (attack_stop_if_target_invalid(ent)) return;
    umove_t const *move = ent->currentmove;
    edict_t *target = ent->goalentity;
    attack_set_backswing_deadline(ent);
    uint32_t arrow_code = 0, arrow_level = 0;
    S_BlackArrowPrepareAttack(ent, target, &arrow_code, &arrow_level);
    S_ResolveAttackHitWithArrow(ent, target,
        G_AttackDamage(ent, target, ai_rolldamage1(ent, 1)), arrow_code, arrow_level);
    /* Count committed weapon swings, not issued orders or approach frames.
     * Damage (including the final Siege strike) has been resolved before the
     * club expires and changes the effective profile. */
    S_WarClubConsumeAttack(ent);
    /* Normal units enter recovery from the attack animation's end callback.
     * Some building models (notably Orc Burrows in the current asset path) do
     * not resolve a usable attack sequence. Their damage-point timer still
     * fires the first hit, but M_MoveFrame() can never reach the move endfunc,
     * leaving the attack state parked at wait==0 forever. Treat the completed
     * hit as the end of the windup when there is no finite animation to drive
     * that transition. A lethal hit may already have resumed Follow or the next
     * queued order, so only the unchanged attack may enter this recovery. */
    if (ent->currentmove == move && ent->goalentity == target &&
        S_AttackCanTarget(ent, target) && !attack_animation_can_finish(ent))
        attack_melee_cooldown(ent);
}

static void throw_missile(edict_t *ent) {
    if (S_UnitIsEntanglingRooted(ent)) return;
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    edict_t *other = ent->goalentity;
    /* Roll at launch, but defer target armor/type mitigation until impact so
     * armor or defense changes while the projectile is in flight are honored. */
    int damage = (int)ai_rolldamage1(ent, 1);
    mat4_t matrix;
    unitAttack_t const *atk = ACTIVE_ATTACK(ent);
    M_GetEntityMatrix(&ent->s, &matrix);
    vec3_t origin = Matrix4_multiply_vector3(&matrix, &atk->origin);
    vec2_t impact = other->s.origin2;
    uint32_t arrow_code = 0, arrow_level = 0;
    cstring_t missile_art;
    float missile_speed = atk->projectile.speed;
    S_BlackArrowPrepareAttack(ent, other, &arrow_code, &arrow_level);
    missile_art = arrow_code ? G_AbilityEffectArt(arrow_code, WC3_EFFECT_MISSILE, 0) :
                               S_SearingArrowsMissileArt(ent);
    if (arrow_code) {
        /* The authored Black Arrow ability overrides the normal ranged
         * projectile speed when configured, without mutating weapon data. */
        cstring_t const speed_text = S_SpellString(arrow_code, "Missilespeed", 0);
        float const configured = speed_text ? (float)atof(speed_text) : 0.0f;
        if (isfinite(configured) && configured > 0.0f) missile_speed = configured;
    }
    attack_set_backswing_deadline(ent);
    fire_rocket(ent, &(rocketDesc_t) {
        .start = origin,
        .target = other,
        .fixed_target = (atk->weapon == WPN_ARTILLERY || atk->weapon == WPN_ALINE) ? &impact : NULL,
        .speed = missile_speed,
        .model = missile_art ? G_RegisterModel(missile_art) : atk->projectile.model,
        .arrow_ability = arrow_code,
        .arrow_level = arrow_level,
        .allow_destroyer_orb = !arrow_code,
        .damage = damage,
        .attack_type = atk->type,
        .attack = atk,
        .impaling_line = atk->weapon == WPN_ALINE && G_ImpalingBoltResearchLevel(ent) > 0 &&
                         !G_IsDestructable(other),
        .tree_attack_allowed = atk->weapon == WPN_ALINE &&
                               G_ImpalingBoltResearchLevel(ent) > 0 && G_IsDestructable(other),
        .area_targets = ent->data.UnitWeapons ? (atk == &ent->unit_state->attack2 ? ent->data.UnitWeapons->attack2.areaTargets : ent->data.UnitWeapons->attack1.areaTargets) : 0,
    });
    S_WarClubConsumeAttack(ent);
    /* A ranged attack's next-shot timing comes from weapon cooldown and
     * backswing, not from the length of its selected animation sequence.
     * Enter recovery at release for every model: waiting for a long finite
     * clip's endfunc can otherwise stretch a 1-second weapon cadence to the
     * full animation length. The recovery move may interrupt the attack clip,
     * then starts the next attack when both authored timing gates have elapsed. */
    if (S_AttackCanTarget(ent, ent->goalentity))
        attack_ranged_cooldown(ent);
//    gi.WriteByte (svc_temp_entity);
//    gi.WriteByte(TE_MISSILE);
//    gi.WritePosition(&origin);
//    gi.WriteShort(ent->unit_state->attack1.projectile.model);
//    gi.WriteShort(ent->unit_state->attack1.projectile.speed);
//    gi.WriteShort(Vector2_len(&dir) * 1000 / ent->unit_state->attack1.projectile.speed);
//    gi.WriteAngle(atan2(dir.y, dir.x));
//    gi.multicast(&ent->s.origin, MULTICAST_PHS);
}

static void ai_melee(edict_t *ent) {
    if (S_UnitIsEntanglingRooted(ent)) return;
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    /* The target must still satisfy the true weapon range at damage point.
     * RngBuff is cooldown hysteresis only; it must never preserve a pending
     * melee hit after the target has escaped the real attack envelope. */
    if (attack_target_out_of_base_range(ent) || attack_target_too_close(ent)) {
        ent->wait = 0.0f;
        attack_walk(ent);
        return;
    }
    unit_facegoal(ent);
    unit_runwait(ent, damage_target);
}

static void ai_ranged(edict_t *ent) {
    if (S_UnitIsEntanglingRooted(ent)) return;
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    /* A projectile is committed only at damage point.  Until then, leaving
     * true range cancels the windup and returns to chase just like melee. */
    if (attack_target_out_of_base_range(ent) || attack_target_too_close(ent)) {
        ent->wait = 0.0f;
        attack_walk(ent);
        return;
    }
    unit_facegoal(ent);
    unit_runwait(ent, throw_missile);
}

static float attack_minimum_range(edict_t const *ent) {
    return ent && ent->data.UnitWeapons ? MAX(0.0f, ent->data.UnitWeapons->minimumAttackRange) : 0.0f;
}

/* Distance from the attacker's collision edge to the target's collision edge
 * or authored pathing footprint. */
static float attack_target_distance(edict_t const *ent, edict_t const *target) {
    float footprint;
    if (!ent || !target) return FLT_MAX;
    if ((G_UnitIsStructure(target) || G_IsDestructable(target)) && target->pathtex) {
        footprint = CM_DistanceToPathingFootprint(target, &ent->s.origin2);
        if (footprint < FLT_MAX)
            return MAX(0.0f, footprint - MAX(0.0f, ent->collision));
    }
    return MAX(0.0f, Vector2_distance(&target->s.origin2, &ent->s.origin2) -
                      MAX(0.0f, ent->collision) - MAX(0.0f, target->collision));
}

static bool attack_target_too_close_for(edict_t const *ent, edict_t const *target) {
    float const minimum = attack_minimum_range(ent);
    if (!ent || !target || minimum <= 0.0f) return false;
    return attack_target_distance(ent, target) < minimum;
}

static bool attack_target_too_close(edict_t *ent) {
    return ent && attack_target_too_close_for(ent, ent->goalentity);
}

static void attack_retreat_from_target(edict_t *ent) {
    vec2_t dir;
    float len;

    if (!ent || !ent->goalentity) return;
    dir = Vector2_sub(&ent->s.origin2, &ent->goalentity->s.origin2);
    len = Vector2_len(&dir);
    if (len <= 0.001f) dir = MAKE(vec2_t, cosf(ent->s.angle + (float)M_PI), sinf(ent->s.angle + (float)M_PI));
    else { dir.x /= len; dir.y /= len; }
    ent->s.angle = atan2f(dir.y, dir.x);
    G_BeginMovement(ent);
    ent->movement->heading = ent->s.angle;
    ent->movement->flow_direct = true;
    ent->movement->flow_generation = 0;
    unit_moveindirection(ent);
}

/* RngBuff applies until the simulation-time deadline, even while another
 * ability owns the unit's think callback. */
static bool attack_is_cooling_down(edict_t const *ent) {
    return ent && ent->unit_state->attack_cooldown_end_time &&
        (int32_t)(ent->unit_state->attack_cooldown_end_time - G_Time()) > 0;
}

/* Recovery animation waits use seconds; derive them only when needed from simulation milliseconds. */
static float attack_cooldown_remaining(edict_t const *ent) {
    return attack_is_cooling_down(ent) ? (ent->unit_state->attack_cooldown_end_time - G_Time()) / 1000.0f : 0.0f;
}

static void attack_set_cooldown(edict_t *ent, float seconds) {
    uint32_t duration = (uint32_t)ceilf(MAX(0.0f, seconds) * 1000.0f);
    ent->unit_state->attack_cooldown_end_time = G_Time() + duration;
}

/* Centralize attack-range geometry so buffered cooldown range and true firing
 * range share the same footprint/minimum-range-independent distance contract. */
static bool attack_target_out_of_range_for_mode(edict_t const *ent, edict_t const *target,
                                                 bool allow_cooldown_buffer) {
    unitAttack_t const *attack;
    float range, ensnare_range;

    if (!ent || !target) return true;

    /* Ensnare DataC forces the bound unit's own attacks to melee range. */
    ensnare_range = S_EnsnareMeleeRange(ent);
    attack = attack_profile(ent, target);
    range = ensnare_range > 0.0f ? ensnare_range : attack->range;
    if (ensnare_range <= 0.0f && allow_cooldown_buffer && attack_is_cooling_down(ent))
        range += MAX(0.0f, attack->rangeBuffer);
    return attack_target_distance(ent, target) > range;
}

static bool attack_target_out_of_range_for(edict_t const *ent, edict_t const *target) {
    return attack_target_out_of_range_for_mode(ent, target, true);
}

static bool attack_target_out_of_range(edict_t *ent) {
    return !ent || attack_target_out_of_range_for(ent, ent->goalentity);
}

static bool attack_target_out_of_base_range(edict_t *ent) {
    return !ent || attack_target_out_of_range_for_mode(ent, ent->goalentity, false);
}

/* Movement-disabled attackers (ordinary towers/buildings) must not auto-acquire
 * something they can see but can never approach.  Mobile units still acquire
 * throughout uacq and chase normally; Hold Position keeps its separate
 * disable-chase lifecycle. */
bool S_AttackCanAutoAcquire(edict_t const *attacker, edict_t const *target) {
    if (S_UnitIsEntanglingRooted(attacker) || !S_AttackCanTarget(attacker, target)) return false;
    if ((attacker->aiflags & AI_IMMOBILE) &&
        (attack_target_out_of_range_for(attacker, target) || attack_target_too_close_for(attacker, target)))
        return false;
    return true;
}

/* Weapon cooldown advances independently of the owning think callback; recovery remains move-local. */
static bool attack_cooldown_elapsed(edict_t *ent) {
    float const step = FRAMETIME / 1000.0f;
    ent->wait = MAX(0.0f, ent->wait - step);
    return !attack_is_cooling_down(ent) && ent->wait <= 0.0f;
}

static void ai_melee_cooldown(edict_t *ent) {
    bool ready;
    if (S_UnitIsEntanglingRooted(ent)) return;
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    ready = attack_cooldown_elapsed(ent);
    if (attack_target_out_of_range(ent) || attack_target_too_close(ent)) {
        attack_walk(ent);
    } else if (ready) {
        /* RngBuff disappears exactly when the weapon becomes ready.  Recheck
         * true range before starting the next swing so the buffer cannot turn
         * into extra firing range. */
        if (attack_target_out_of_base_range(ent) || attack_target_too_close(ent))
            attack_walk(ent);
        else
            attack_melee(ent);
    }
}

static void ai_ranged_cooldown(edict_t *ent) {
    bool ready;
    if (S_UnitIsEntanglingRooted(ent)) return;
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    ready = attack_cooldown_elapsed(ent);
    if (attack_target_out_of_range(ent) || attack_target_too_close(ent)) {
        attack_walk(ent);
    } else if (ready) {
        if (attack_target_out_of_base_range(ent) || attack_target_too_close(ent))
            attack_walk(ent);
        else
            attack_ranged(ent);
    }
}

static void ai_attack_walk(edict_t *ent) {
    bool cooldown_ready = false;
    if (S_UnitIsEntanglingRooted(ent)) return;
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    cooldown_ready = attack_cooldown_elapsed(ent);
    if (attack_target_out_of_range(ent)) {
        /* Hold Position and movement-disabled structures cannot chase an
         * out-of-range target.  Finish the attack behavior instead of leaving
         * an immobile tower stuck forever in attack_walk. */
        if (ent->orders->holding_position || (ent->aiflags & AI_IMMOBILE)) {
            attack_finish_after_combat(ent, ent->goalentity, "immobile_target_out_of_range");
            return;
        }
        if (!S_UnitCanTranslate(ent)) return;
        if (ent->goalentity->targtype == TARG_WALL)
            unit_changeangle(ent);
        else
            unit_changeangle_for_radius(ent, ent->collision);
        if (ent->movement->flow_unreachable) return;
        unit_moveindirection(ent);
    } else if (attack_target_too_close(ent)) {
        /* Artillery minimum range is a real dead zone. Mobile siege units back
         * away until they can fire; Hold Position/immobile attackers cannot. */
        if (ent->orders->holding_position || (ent->aiflags & AI_IMMOBILE)) {
            attack_finish_after_combat(ent, ent->goalentity, "immobile_target_too_close");
            return;
        }
        if (!S_UnitCanTranslate(ent)) return;
        attack_retreat_from_target(ent);
    } else {
        if (!cooldown_ready) {
            ent->wait = MAX(ent->wait, attack_cooldown_remaining(ent));
            if (ACTIVE_ATTACK(ent)->weapon == WPN_MISSILE || ACTIVE_ATTACK(ent)->weapon == WPN_ARTILLERY ||
                ACTIVE_ATTACK(ent)->weapon == WPN_MSPLASH || ACTIVE_ATTACK(ent)->weapon == WPN_MBOUNCE ||
                ACTIVE_ATTACK(ent)->weapon == WPN_ALINE)
                unit_setmove(ent, &attack_move_ranged_cooldown);
            else
                unit_setmove(ent, &attack_move_melee_cooldown);
            return;
        }
        if (ACTIVE_ATTACK(ent)->weapon == WPN_MISSILE || ACTIVE_ATTACK(ent)->weapon == WPN_ARTILLERY ||
            ACTIVE_ATTACK(ent)->weapon == WPN_MSPLASH || ACTIVE_ATTACK(ent)->weapon == WPN_MBOUNCE ||
            ACTIVE_ATTACK(ent)->weapon == WPN_ALINE)
            attack_ranged(ent);
        else
            attack_melee(ent);
    }
}

static umove_t attack_move_walk = { "walk", ai_attack_walk, NULL, CAbilityAttack, .travel = true };
static umove_t attack_move_melee_cooldown = { "stand ready", ai_melee_cooldown, NULL, CAbilityAttack };
static umove_t attack_move_melee = { "attack", ai_melee, attack_melee_cooldown, CAbilityAttack };
static umove_t attack_move_ranged_cooldown = { "stand ready", ai_ranged_cooldown, NULL, CAbilityAttack };
static umove_t attack_move_ranged = { "attack range", ai_ranged, attack_ranged_cooldown, CAbilityAttack };

void attack_walk(edict_t *self) {
    unit_setmove(self, &attack_move_walk);
}

/* Set the attack target and start walking toward attack range. */
static void order_attack_internal(edict_t *self, edict_t *target, bool retaliation) {
    if (!self || S_UnitIsCycloned(self) || S_GoldMineWorkerIsInside(self) ||
        !(retaliation ? attack_can_retaliate(self, target) : S_AttackCanTarget(self, target))) {
        return;
    }
    /* Beginning an attack is incompatible with Shadow Meld. This path is used
     * by explicit attacks, ordinary acquisition, and the automatic retaliation
     * issued by T_Damage(), so clear both invisibility and the explicit Hide
     * hold-fire state before installing the attack behavior. */
    S_ShadowMeldBreak(self);
    self->orders->explicit_allied_attack = false;
    unit_entercombat(self, target);
    self->goalentity = target;
    self->unit_state->attack_target_spawn_time = target->spawn_time;
    /* Birth and other non-attack moves can leave a long wait on the unit.
     * Preserve waits only while an authored weapon cooldown is still active. */
    if (!attack_is_cooling_down(self)) self->wait = 0.0f;
    attack_walk(self);
}

void order_attack(edict_t *self, edict_t *target) {
    order_attack_internal(self, target, false);
}

/* Player orders replace retained movement; automatic acquisition keeps it so combat can resume Follow/Patrol. */
bool S_OrderAttack(edict_t *self, edict_t *target) {
    if (!self || M_IsDead(self) || S_UnitIsCycloned(self) || S_GoldMineWorkerIsInside(self) ||
        !S_AttackCanTarget(self, target))
        return false;
    S_SpellCancelChannel(self);
    self->orders->attackmove_waypoint = NULL;
    self->orders->patrol_a = self->orders->patrol_b = self->orders->patrol_target = NULL;
    self->orders->follow_target = NULL;
    self->orders->holding_position = false;
    order_attack(self, target);
    self->orders->explicit_allied_attack = G_PlayerTreatsPlayerAsAlly(self->s.player, target->s.player);
    return true;
}

/* A manually targeted attack modifier arms exactly one normal Attack order;
 * mana is charged later when its ranged weapon reaches the damage point. */
bool S_SearingArrowsIssueOrder(edict_t *attacker, edict_t *target, uint32_t ability) {
    status_application_t app;
    heroabilitystatus_t *status;
    uint32_t level;
    if (!attacker || !target ||
        (G_AbilityCode(ability) != MAKEFOURCC('A','H','f','a') &&
         G_AbilityCode(ability) != MAKEFOURCC('A','f','a','k')) ||
        !G_UnitAbilityLevel(attacker, ability) || !S_AttackCanTarget(attacker, target) ||
        !attacker->unit_state ||
        (attacker->unit_state->attack1.weapon != WPN_MISSILE &&
         attacker->unit_state->attack1.weapon != WPN_MSPLASH)) return false;
    level = G_UnitAbilityLevel(attacker, ability);
    app = (status_application_t){
        .buff = GetClassName(ability), .level = level, .source_ability = ability,
        .rank = level, .buff_flags = WC3_STATUS_BUFF_ONE_SHOT_ATTACK
    };
    status = unit_applystatus(attacker, &app);
    if (!status) return false;
    status->buff_flags |= WC3_STATUS_BUFF_ONE_SHOT_ATTACK;
    if (S_OrderAttack(attacker, target)) return true;
    unit_removestatus(attacker, status, STATUS_REMOVE_SCRIPT);
    return false;
}

static float attack_speed_divisor(edict_t *self) {
    float const agi_bonus = game.constants.combatConstantsLoaded
                          ? game.constants.agiAttackSpeedBonus
                          : 0.02f;
    float total_bonus = (float)self->unit_state->hero.agi * agi_bonus +
                      unit_status_modifier_total(self, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT)
                      - S_DefendAttackReduction(self) - S_CreepAttackSpeedReduction(self) - S_SlowAuraAttackReduction(self);
    total_bonus += S_EnduranceAttackBonus(self);
    /* Warsmash clamps total attack-speed bonus to [-90%, +400%]. OpenRealm
     * combines authored buffs/debuffs with Agility before applying the same
     * timing bounds. */
    total_bonus = MAX(-0.9f, MIN(4.0f, total_bonus));
    return 1.0f + total_bonus;
}

void attack_melee_cooldown(edict_t *self) {
    float divisor = attack_speed_divisor(self);
    /* A normal animated swing starts its cooldown at attack_melee(). The
     * animation end callback can run well after damage point, so retain that
     * deadline instead of restarting cooldown - damagePoint from here. The
     * fallback covers callers that enter recovery without an active swing
     * deadline (for example zero-length animation paths). */
    if (self->currentmove != &attack_move_melee) {
        attack_set_cooldown(self, (ACTIVE_ATTACK(self)->cooldown - ACTIVE_ATTACK(self)->damagePoint) / divisor);
        /* Direct calls without a preceding swing model the damage point at
         * recovery entry; normal attack moves set this at the committed hit. */
        attack_set_backswing_deadline(self);
    }
    unit_setmove(self, &attack_move_melee_cooldown);
    self->wait = MAX(attack_cooldown_remaining(self), attack_backswing_remaining(self));
    /* The next swing must satisfy both authored gates: weapon cooldown from
     * swing start and backswing after damage point.  If both have already
     * elapsed, transition explicitly instead of parking on wait==0. */
    if (self->wait <= 0.0f) attack_melee(self);
}

void attack_melee(edict_t *self) {
    float divisor = attack_speed_divisor(self);
    S_PermanentInvisibilityReveal(self);
    attack_set_cooldown(self, ACTIVE_ATTACK(self)->cooldown / divisor);
    unit_setmove(self, &attack_move_melee);
    /* unit_setmove selects the new animation but M_MoveFrame would otherwise
     * spend its first simulation tick snapping a stale frame from the prior
     * sequence back to this one. Start the visible swing at its authored first
     * frame immediately so its motion stays aligned with damagePoint. */
    if (self->animation && self->animation->interval[1] > self->animation->interval[0])
        self->s.frame = self->animation->interval[0];
    self->wait = ACTIVE_ATTACK(self)->damagePoint / divisor;
}

void attack_ranged_cooldown(edict_t *self) {
    float divisor = attack_speed_divisor(self);
    if (self->currentmove != &attack_move_ranged) {
        attack_set_cooldown(self, (ACTIVE_ATTACK(self)->cooldown - ACTIVE_ATTACK(self)->damagePoint) / divisor);
        attack_set_backswing_deadline(self);
    }
    unit_setmove(self, &attack_move_ranged_cooldown);
    self->wait = MAX(attack_cooldown_remaining(self), attack_backswing_remaining(self));
    if (self->wait <= 0.0f) attack_ranged(self);
}

void attack_ranged(edict_t *self) {
    float divisor = attack_speed_divisor(self);
    S_PermanentInvisibilityReveal(self);
    attack_set_cooldown(self, ACTIVE_ATTACK(self)->cooldown / divisor);
    unit_setmove(self, &attack_move_ranged);
    self->wait = ACTIVE_ATTACK(self)->damagePoint / divisor;
}

/* ---- Attack Ground --------------------------------------------------------
 * Warsmash exposes Attack Ground for ARTILLERY weapons. The order keeps the
 * clicked point authoritative, walks a mobile siege unit into its normal
 * min/max range band, and snapshots that same point into each projectile at
 * the damage point. */
static bool attack_ground_valid(edict_t const *ent) {
    return ent && ent->inuse && !M_IsDead((edict_t *)ent) &&
           S_UnitAttackSlotEnabled(ent, 0) && ent->unit_state->attack1.type != ATK_NONE &&
           (ent->unit_state->attack1.weapon == WPN_ARTILLERY || ent->unit_state->attack1.weapon == WPN_ALINE) && !S_UnitIsCycloned(ent) &&
           S_HumanCanAttack(ent) && S_CargoAttacksEnabled(ent);
}

static float attack_ground_distance(edict_t const *ent) {
    return ent ? Vector2_distance(&ent->s.origin2, &G_GetChannel(ent)->origin) : FLT_MAX;
}

static bool attack_ground_out_of_range(edict_t const *ent) {
    return !ent || attack_ground_distance(ent) > ent->unit_state->attack1.range;
}

static bool attack_ground_too_close(edict_t const *ent) {
    float const minimum = attack_minimum_range(ent);
    return ent && minimum > 0.0f && attack_ground_distance(ent) < minimum;
}

static void attack_ground_walk(edict_t *ent);
static void attack_ground_ranged(edict_t *ent);
static void attack_ground_cooldown(edict_t *ent);

static void attack_ground_stop(edict_t *ent) {
    if (!ent) return;
    ent->goalentity = NULL;
    if (ent->stand) ent->stand(ent);
    else ent->currentmove = NULL;
}

static void throw_artillery_ground(edict_t *ent) {
    if (S_UnitIsEntanglingRooted(ent)) return;
    int damage;
    mat4_t matrix;
    vec3_t origin;
    vec2_t impact;

    if (!attack_ground_valid(ent)) { attack_ground_stop(ent); return; }
    impact = G_GetChannel(ent)->origin;
    damage = (int)ai_rolldamage1(ent, 1);
    M_GetEntityMatrix(&ent->s, &matrix);
    origin = Matrix4_multiply_vector3(&matrix, &ent->unit_state->attack1.origin);
    fire_rocket(ent, &(rocketDesc_t) {
        .start = origin,
        .fixed_target = &impact,
        .speed = ent->unit_state->attack1.projectile.speed,
        .model = ent->unit_state->attack1.projectile.model,
        .damage = damage,
        .attack = &ent->unit_state->attack1,
        .area_targets = ent->data.UnitWeapons ? ent->data.UnitWeapons->attack1.areaTargets : 0,
    });
    if (ent->currentmove && ent->currentmove->proc == CAbilityAttackGround &&
        !attack_animation_can_finish(ent))
        attack_ground_cooldown(ent);
}

static void ai_attack_ground_ranged(edict_t *ent) {
    if (S_UnitIsEntanglingRooted(ent)) return;
    if (!attack_ground_valid(ent)) { attack_ground_stop(ent); return; }
    unit_facegoal(ent);
    unit_runwait(ent, throw_artillery_ground);
}

static void ai_attack_ground_cooldown(edict_t *ent) {
    if (S_UnitIsEntanglingRooted(ent)) return;
    if (!attack_ground_valid(ent)) { attack_ground_stop(ent); return; }
    if (attack_ground_out_of_range(ent) || attack_ground_too_close(ent)) attack_ground_walk(ent);
    else unit_runwait(ent, attack_ground_ranged);
}

static void ai_attack_ground_walk(edict_t *ent) {
    if (S_UnitIsEntanglingRooted(ent)) return;
    if (!attack_ground_valid(ent)) { attack_ground_stop(ent); return; }
    if (attack_ground_out_of_range(ent)) {
        if (ent->aiflags & AI_IMMOBILE) { attack_ground_stop(ent); return; }
        if (!S_UnitCanTranslate(ent)) return;
        unit_changeangle(ent);
        unit_moveindirection(ent);
    } else if (attack_ground_too_close(ent)) {
        if (ent->aiflags & AI_IMMOBILE) { attack_ground_stop(ent); return; }
        if (!S_UnitCanTranslate(ent)) return;
        attack_retreat_from_target(ent);
    } else {
        attack_ground_ranged(ent);
    }
}

static umove_t attack_ground_move_walk = { "walk", ai_attack_ground_walk, NULL, CAbilityAttackGround, .travel = true };
static umove_t attack_ground_move_cooldown = { "stand ready", ai_attack_ground_cooldown, NULL, CAbilityAttackGround };
static umove_t attack_ground_move_ranged = { "attack range", ai_attack_ground_ranged, attack_ground_cooldown, CAbilityAttackGround };

static void attack_ground_walk(edict_t *ent) {
    unit_setmove(ent, &attack_ground_move_walk);
}

static void attack_ground_cooldown(edict_t *ent) {
    float divisor = attack_speed_divisor(ent);
    unit_setmove(ent, &attack_ground_move_cooldown);
    ent->wait = MAX(0.0f, MAX(ent->unit_state->attack1.cooldown - ent->unit_state->attack1.damagePoint,
                               ent->unit_state->attack1.backswingPoint) / divisor);
    if (ent->wait <= 0.0f) attack_ground_ranged(ent);
}

static void attack_ground_ranged(edict_t *ent) {
    float divisor = attack_speed_divisor(ent);
    S_PermanentInvisibilityReveal(ent);
    unit_setmove(ent, &attack_ground_move_ranged);
    ent->wait = ent->unit_state->attack1.damagePoint / divisor;
}

bool S_OrderAttackGround(edict_t *unit, vec2_t const *point) {
    edict_t *waypoint;

    if (!unit || !point || S_UnitIsEntanglingRooted(unit) || !attack_ground_valid(unit) || S_GoldMineWorkerIsInside(unit) ||
        S_UnitPolymorphed(unit)) return false;
    waypoint = Waypoint_add(point);
    if (!waypoint) return false;
    unit->orders->attackmove_waypoint = NULL;
    unit->orders->patrol_a = unit->orders->patrol_b = unit->orders->patrol_target = NULL;
    unit->orders->follow_target = NULL;
    unit->orders->holding_position = false;
    if (unit->movement) unit->movement->group_speed = 0.0f;
    S_SpellCancelChannel(unit);
    unit->goalentity = waypoint;
    unit->unit_state->attack_target_spawn_time = 0;
    attack_ground_walk(unit);
    G_AddChannel(unit);
    G_GetChannel(unit)->origin = *point;
    return true;
}

bool attack_menu_selecttarget(edict_t *ent, edict_t *target) {
    bool destructable = G_DestructableIsAttackable(target);
    bool issued = false;

    /* Explicit Attack may force-fire on friendly units and buildings.  Smart
     * right-click attack selection remains enemy-only. */
    if (!destructable && (!S_SpellIsAliveTarget(target) ||
        (!S_SpellIsEnemy(ent, target) && !S_SpellIsFriend(ent, target)))) {
        return false;
    }
    FOR_CONTROLLABLE_SELECTED_UNITS(ent->client, e) {
        if (e == target) continue;
        if (G_IssueUnitTargetOrder(e, "attack", target,
                                   ent->client->menu.order_queued,
                                   ent->client->ps.number)) {
            issued = true;
        }
    }
    if (issued) G_QueueAttackOrderSound(G_GetMainControllableUnit(ent->client));
    return issued;
}

/* Attack-move: walk toward the goal, but each tick prefer engaging the
 * nearest enemy within acquisition range over continuing to walk. */
static void ai_attackmove_walk(edict_t *ent) {
    if (G_ShouldAcquireThisFrame(ent)) {
        edict_t *enemy = G_FindNearestEnemy(ent, G_AcquisitionRange(ent));
        if (enemy) {
            order_attack(ent, enemy);
            return;
        }
    }

    float distance = M_DistanceToGoal(ent);
    float move_distance = unit_movedistance(ent);

    if (!S_UnitCanTranslate(ent)) return;
    if (move_should_arrive(ent, move_distance)) {
        /* A shared attack-move waypoint may be across blocked terrain. Don't
         * snap onto it based only on Euclidean proximity: each mover must be
         * able to reach the goal in its own collision-sized flow field. */
        uint32_t const heatmap = M_RefreshHeatmapForMover(ent, ent->goalentity, ent->collision);
        if (heatmap && CM_FlowCanReach(heatmap, ent->s.origin.x, ent->s.origin.y) &&
            M_MoveIsValid(ent, &ent->goalentity->s.origin2)) {
            ent->s.origin2 = ent->goalentity->s.origin2;
            gi.LinkEntity(ent);
        }
        ent->orders->attackmove_waypoint = NULL;
        ent->stand(ent);
    } else if (move_is_blocked(ent, distance, move_distance)) {
        ent->orders->attackmove_waypoint = NULL;
        ent->stand(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static umove_t attackmove_move_walk = { "walk", ai_attackmove_walk, NULL, CAbilityAttack, .travel = true };

/* Begin (or resume, after a kill) attack-moving toward a waypoint. */
void order_attackmove(edict_t *self, edict_t *waypoint) {
    if (S_GoldMineWorkerIsInside(self))
        return;
    S_SpellCancelChannel(self);
    self->orders->attackmove_waypoint = waypoint;
    self->orders->patrol_a = NULL;
    self->orders->patrol_b = NULL;
    self->orders->patrol_target = NULL;
    self->orders->follow_target = NULL;
    self->orders->holding_position = false;
    self->goalentity = waypoint;
    self->unit_state->attack_target_spawn_time = 0;
    move_reset_progress(self);
    unit_setmove(self, &attackmove_move_walk);
}

static bool attackmove_selectlocation(edict_t *clent, vec2_t const *location) {
    bool any = false;

    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        vec2_t target = *location;
        if ((ent->aiflags & AI_IMMOBILE) || ent->data.UnitBalance->speed <= 0) {
            continue;
        }
        CM_ClosestPathablePointForRadiusFlags(location, ent->collision, M_UnitStaticPathingFlags(ent), &target);
        if (G_IssueUnitPointOrder(ent, "attack", &target,
                                  clent->client->menu.order_queued,
                                  clent->client->ps.number, 0.0f)) {
            any = true;
        }
    }
    if (any) {
        G_QueueAttackOrderSound(G_GetMainControllableUnit(clent->client));
        G_SendPointConfirmation(clent, location, true);
    }
    return any;
}

BZ_COMMAND_PROC(AbilityAttack) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = attack_menu_selecttarget;
    clent->client->menu.on_location_selected = attackmove_selectlocation;
    clent->client->menu.supports_order_queue = true;
}

static bool attack_ground_selectlocation(edict_t *clent, vec2_t const *location) {
    bool any = false;

    if (!clent || !clent->client || !location) return false;
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        if (ent->unit_state->attack1.weapon != WPN_ARTILLERY && ent->unit_state->attack1.weapon != WPN_ALINE) continue;
        if (G_IssueUnitPointOrder(ent, "attackground", location,
                                  clent->client->menu.order_queued,
                                  clent->client->ps.number, 0.0f)) any = true;
    }
    if (any) {
        S_SpellCursorSplat(clent, 0.0f);
        G_SendPointConfirmation(clent, location, true);
    }
    return any;
}

static float attack_ground_cursor_radius(gameClient_t *client) {
    float radius = 0.0f;
    if (!client) return 0.0f;
    FOR_CONTROLLABLE_SELECTED_UNITS(client, ent) {
        unitAttack_t const *attacks[] = { &ent->unit_state->attack1, &ent->unit_state->attack2 };
        FOR_LOOP(i, 2) {
            unitAttack_t const *attack = attacks[i];
            if (!S_UnitAttackSlotEnabled(ent, i) ||
                (attack->weapon != WPN_ARTILLERY && attack->weapon != WPN_ALINE)) continue;
            radius = MAX(radius, MAX(attack->areaFull, MAX(attack->areaMedium, attack->areaSmall)));
        }
    }
    /* Use the authored splash bands; do not invent a visual area for
     * custom artillery weapons with no area data. */
    return radius;
}

BZ_COMMAND_PROC(AbilityAttackGround) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_location_selected = attack_ground_selectlocation;
    clent->client->menu.supports_order_queue = true;
    S_SpellCursorSplat(clent, attack_ground_cursor_radius(clent->client));
}
