/*
 * s_attack.c — Attack ability and projectile system.
 *
 * Implements the a_attack ability used by all combat units.  Handles both
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

void attack_walk(LPEDICT ent);
void attack_melee(LPEDICT ent);
void attack_melee_cooldown(LPEDICT ent);
void attack_ranged(LPEDICT ent);
void attack_ranged_cooldown(LPEDICT ent);
void order_attack(LPEDICT self, LPEDICT target);

typedef struct {
    LPEDICT target;
    VECTOR3 start;
    VECTOR3 dir;
    DWORD speed;
    DWORD model;
    DWORD damage;
}  rocketDesc_t;

/* Spawn a projectile entity aimed at desc->target.
 * The entity is given MOVETYPE_FLYMISSILE so that SV_Physics_Toss() in
 * g_phys.c will move it each frame until it reaches the target. */
void fire_rocket(LPEDICT ent, rocketDesc_t const *desc) {
    VECTOR3 dir = Vector3_sub(&desc->target->s.origin, &ent->s.origin);
    Vector3_normalize(&dir);
    LPEDICT rocket = G_Spawn();
    rocket->s.origin = desc->start;
    rocket->s.angle = atan2f(dir.y, dir.x);
    rocket->s.model = desc->model;
    rocket->velocity = desc->speed / 1000.f;
    rocket->damage = desc->damage;
    rocket->goalentity = desc->target;
    rocket->owner = ent;
    rocket->movetype = MOVETYPE_FLYMISSILE;
    rocket->s.renderfx |= 64;
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

static FLOAT ai_rolldamage1(LPEDICT self, int weapon) {
    FLOAT damageBase = self->attack1.damageBase;
    (void)weapon;
    FOR_LOOP(i, self->attack1.numberOfDice) {
        /* Warsmash treats a malformed zero-sided die as contributing +1
         * instead of taking modulo zero. Normal Warcraft data has S > 0. */
        damageBase += self->attack1.sidesPerDie
                    ? (FLOAT)(rand() % self->attack1.sidesPerDie + 1)
                    : 1.0f;
    }
    return damageBase + self->attack1.temporaryDamageBonus;
}

void M_GetEntityMatrix(LPCENTITYSTATE entity, LPMATRIX4 matrix) {
    Matrix4_identity(matrix);
    Matrix4_translate(matrix, &entity->origin);
    Matrix4_rotate(matrix, &(VECTOR3){0, 0, entity->angle * 180 / M_PI}, ROTATE_XYZ);
    Matrix4_scale(matrix, &(VECTOR3){entity->scale, entity->scale, entity->scale});
}

static BOOL can_attack(LPCEDICT ent) {
    if (ent->attack1.type == ATK_NONE)
        return false;
    if (!ent->currentmove || ent->currentmove->ability != &a_attack)
        return true;
    return false;
}

static BOOL attack_target_is_valid(LPCEDICT target) {
    if (!target || !target->inuse) {
        return false;
    }
    if (target->destructable.initialized) {
        return G_DestructableIsAttackable(target);
    }
    return !M_IsDead((LPEDICT)target);
}

static void attack_finish_after_combat(LPEDICT attacker) {
    if (!attacker) {
        return;
    }
    if (attacker->movement.patrol_a) {
        order_patrol_resume(attacker);
    } else if (attacker->movement.attackmove_waypoint) {
        order_attackmove(attacker, attacker->movement.attackmove_waypoint);
    } else if (attacker->movement.follow_target) {
        order_follow_resume(attacker);
    } else if (attacker->stand) {
        attacker->stand(attacker);
    }
}

static BOOL attack_stop_if_target_invalid(LPEDICT attacker) {
    if (attack_target_is_valid(attacker ? attacker->goalentity : NULL)) {
        return false;
    }
    if (attacker) {
        unit_leavecombat(attacker);
        attacker->goalentity = NULL;
        attack_finish_after_combat(attacker);
    }
    return true;
}

/* Stock fallback for attack-type × defense-type values. Production games load
 * the active table from MiscGame/war3mapMisc into game.constants; these values
 * keep unit-level tests and early bootstrap callers deterministic. */
static FLOAT const g_default_damage_table[8][8] = {
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

/* Apply the Warsmash/WC3 damage formula: active attack×defense multiplier,
 * then numeric armor. Positive armor is 1/(1+K*A); negative armor uses the
 * Warcraft exponential curve 2-(1-K)^(-A). Result remains minimum 1 for the
 * existing OpenRealm physical-attack contract. */
int G_AttackDamage(LPEDICT attacker, LPEDICT target, int base) {
    if (!attacker || !target || base <= 0)
        return base;
    DWORD atk = attacker->attack1.type;
    DWORD def = target->defense_type;
    if (atk >= 8) atk = 0;
    if (def >= 8) def = 7;

    FLOAT const mult = game.constants.combatConstantsLoaded
                     ? game.constants.damageBonus[atk][def]
                     : g_default_damage_table[atk][def];
    FLOAT const armor_coefficient = game.constants.combatConstantsLoaded
                                  ? game.constants.defenseArmor
                                  : 0.06f;
    FLOAT dmg = (FLOAT)base * mult;
    FLOAT armor = G_UnitArmorValue(target);
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
void T_Damage(LPEDICT target, LPEDICT attacker, int damage) {
    if (!target || target->invulnerable) {
        return;
    }
    if (G_IsDestructable(target)) {
        if (G_DestructableApplyDamage(target, attacker, (FLOAT)damage)) {
            attack_finish_after_combat(attacker);
        }
        return;
    }
    unit_entercombat(attacker, target);
    unit_entercombat(target, attacker);

    if (target->health.value <= damage) {
        target->health.value = 0;
        unit_leavecombat(target);
        unit_leavecombat(attacker);
        target->die(target, attacker);
        attack_finish_after_combat(attacker);
        return;
    } else {
        target->health.value -= damage;
    }
    if (can_attack(target) && !unit_is_walking(target) &&
        S_SpellIsEnemy(target, attacker)) {
        order_attack(target, attacker);
    } else if (target->pain) {
        target->pain(target);
    }
}

static void damage_target(LPEDICT ent) {
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    LPEDICT other = ent->goalentity;
    int damage = G_AttackDamage(ent, other, ai_rolldamage1(ent, 1));
    T_Damage(other, ent, damage);
}

static void throw_missile(LPEDICT ent) {
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    LPEDICT other = ent->goalentity;
    /* Roll at launch, but defer target armor/type mitigation until impact so
     * armor or defense changes while the projectile is in flight are honored. */
    int damage = (int)ai_rolldamage1(ent, 1);
    MATRIX4 matrix;
    M_GetEntityMatrix(&ent->s, &matrix);
    VECTOR3 origin = Matrix4_multiply_vector3(&matrix, &ent->attack1.origin);
    fire_rocket(ent, &(rocketDesc_t) {
        .start = origin,
        .target = other,
        .speed = ent->attack1.projectile.speed,
        .model = ent->attack1.projectile.model,
        .damage = damage,
    });
//    gi.WriteByte (svc_temp_entity);
//    gi.WriteByte(TE_MISSILE);
//    gi.WritePosition(&origin);
//    gi.WriteShort(ent->attack1.projectile.model);
//    gi.WriteShort(ent->attack1.projectile.speed);
//    gi.WriteShort(Vector2_len(&dir) * 1000 / ent->attack1.projectile.speed);
//    gi.WriteAngle(atan2(dir.y, dir.x));
//    gi.multicast(&ent->s.origin, MULTICAST_PHS);
}


static void ai_melee(LPEDICT ent) {
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    unit_changeangle(ent);
    unit_runwait(ent, damage_target);
}

static void ai_ranged(LPEDICT ent) {
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    unit_changeangle(ent);
    unit_runwait(ent, throw_missile);
}

static BOOL attack_target_out_of_range(LPEDICT ent) {
    LPEDICT target;
    FLOAT footprint;

    if (!ent || !(target = ent->goalentity)) {
        return true;
    }
    if (G_UnitIsBuilding(target->class_id)) {
        footprint = CM_DistanceToPathingFootprint(target, &ent->s.origin2);
        if (footprint < FLT_MAX) {
            return footprint > ent->collision + ent->attack1.range;
        }
    }
    return M_DistanceToGoal(ent) > ent->attack1.range;
}

static void ai_melee_cooldown(LPEDICT ent) {
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    if (attack_target_out_of_range(ent)) {
        attack_walk(ent);
    } else {
        unit_runwait(ent, attack_melee);
    }
}

static void ai_ranged_cooldown(LPEDICT ent) {
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    if (attack_target_out_of_range(ent)) {
        attack_walk(ent);
    } else {
        unit_runwait(ent, attack_ranged);
    }
}

static void ai_attack_walk(LPEDICT ent) {
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    if (attack_target_out_of_range(ent)) {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    } else if (ent->attack1.weapon == WPN_MISSILE) {
        attack_ranged(ent);
    } else {
        attack_melee(ent);
    }
}

static umove_t attack_move_walk = { "walk", ai_attack_walk, NULL, &a_attack };
static umove_t attack_move_melee_cooldown = { "stand ready", ai_melee_cooldown, NULL, &a_attack };
static umove_t attack_move_melee = { "attack", ai_melee, attack_melee_cooldown, &a_attack };
static umove_t attack_move_ranged_cooldown = { "stand ready", ai_ranged_cooldown, NULL, &a_attack };
static umove_t attack_move_ranged = { "attack range", ai_ranged, attack_ranged_cooldown, &a_attack };

void attack_walk(LPEDICT self) {
    unit_setmove(self, &attack_move_walk);
}

/* Set the attack target and start walking toward attack range. */
void order_attack(LPEDICT self, LPEDICT target) {
    if (!self || S_GoldMineWorkerIsInside(self) || !attack_target_is_valid(target)) {
        return;
    }
    unit_entercombat(self, target);
    self->goalentity = target;
    attack_walk(self);
}

static FLOAT attack_speed_divisor(LPEDICT self) {
    FLOAT const agi_bonus = game.constants.combatConstantsLoaded
                          ? game.constants.agiAttackSpeedBonus
                          : 0.02f;
    FLOAT total_bonus = (FLOAT)self->hero.agi * agi_bonus;
    /* Warsmash clamps total attack-speed bonus to [-90%, +400%]. OpenRealm
     * currently has only the Agility contribution, but keeping the clamp here
     * makes extreme/custom hero data follow the same timing bounds. */
    total_bonus = MAX(-0.9f, MIN(4.0f, total_bonus));
    return 1.0f + total_bonus;
}

void attack_melee_cooldown(LPEDICT self) {
    FLOAT divisor = attack_speed_divisor(self);
    unit_setmove(self, &attack_move_melee_cooldown);
    self->wait = MAX(0.0f, (self->attack1.cooldown - self->attack1.damagePoint) / divisor);
}

void attack_melee(LPEDICT self) {
    FLOAT divisor = attack_speed_divisor(self);
    unit_setmove(self, &attack_move_melee);
    self->wait = self->attack1.damagePoint / divisor;
    if (self->sound.attack) gi.Sound(self, CHAN_WEAPON, self->sound.attack, 1.0f, 1.0f, 0.0f);
}

void attack_ranged_cooldown(LPEDICT self) {
    FLOAT divisor = attack_speed_divisor(self);
    unit_setmove(self, &attack_move_ranged_cooldown);
    self->wait = MAX(0.0f, (self->attack1.cooldown - self->attack1.damagePoint) / divisor);
}

void attack_ranged(LPEDICT self) {
    FLOAT divisor = attack_speed_divisor(self);
    unit_setmove(self, &attack_move_ranged);
    self->wait = self->attack1.damagePoint / divisor;
    if (self->sound.attack) gi.Sound(self, CHAN_WEAPON, self->sound.attack, 1.0f, 1.0f, 0.0f);
}

BOOL attack_menu_selecttarget(LPEDICT ent, LPEDICT target) {
    BOOL destructable = G_DestructableIsAttackable(target);
    BOOL issued = false;

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
    return issued;
}

/* Attack-move: walk toward the goal, but each tick prefer engaging the
 * nearest enemy within acquisition range over continuing to walk. */
static void ai_attackmove_walk(LPEDICT ent) {
    if (G_ShouldAcquireThisFrame(ent)) {
        LPEDICT enemy = G_FindNearestEnemy(ent, G_AcquisitionRange(ent));
        if (enemy) {
            order_attack(ent, enemy);
            return;
        }
    }

    FLOAT distance = M_DistanceToGoal(ent);
    FLOAT move_distance = unit_movedistance(ent);

    if (move_should_arrive(ent, move_distance)) {
        if (M_MoveIsValid(ent, &ent->goalentity->s.origin2)) {
            ent->s.origin2 = ent->goalentity->s.origin2;
            gi.LinkEntity(ent);
        }
        ent->movement.attackmove_waypoint = NULL;
        ent->stand(ent);
    } else if (move_is_blocked(ent, distance, move_distance)) {
        ent->movement.attackmove_waypoint = NULL;
        ent->stand(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static umove_t attackmove_move_walk = { "walk", ai_attackmove_walk, NULL, &a_attack };

/* Begin (or resume, after a kill) attack-moving toward a waypoint. */
void order_attackmove(LPEDICT self, LPEDICT waypoint) {
    if (S_GoldMineWorkerIsInside(self))
        return;
    self->movement.attackmove_waypoint = waypoint;
    self->movement.patrol_a = NULL;
    self->movement.patrol_b = NULL;
    self->movement.patrol_target = NULL;
    self->movement.follow_target = NULL;
    self->movement.holding_position = false;
    self->goalentity = waypoint;
    move_reset_progress(self);
    unit_setmove(self, &attackmove_move_walk);
}

static BOOL attackmove_selectlocation(LPEDICT clent, LPCVECTOR2 location) {
    BOOL any = false;

    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        VECTOR2 target = *location;
        if ((ent->aiflags & AI_IMMOBILE) || ent->data.UnitBalance->speed <= 0) {
            continue;
        }
        CM_ClosestPathablePointForRadius(location, ent->collision, &target);
        if (G_IssueUnitPointOrder(ent, "attack", &target,
                                  clent->client->menu.order_queued,
                                  clent->client->ps.number, 0.0f)) {
            any = true;
        }
    }
    if (any) G_SendPointConfirmation(clent, location, true);
    return any;
}

void attack_command(LPEDICT ent) {
    UI_AddCancelButton(ent);
    ent->client->menu.on_entity_selected = attack_menu_selecttarget;
    ent->client->menu.on_location_selected = attackmove_selectlocation;
    ent->client->menu.supports_order_queue = true;
}

ability_t a_attack = {
    .cmd = attack_command,
};
