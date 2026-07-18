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
    FOR_LOOP(i, self->attack1.numberOfDice) {
        damageBase += rand() % self->attack1.sidesPerDie + 1;
    }
    return damageBase;
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

/* WC3 1.29 attack-type × defense-type damage multiplier table (verified from
 * MiscGame.txt). Rows = attack1.type (none,normal,pierce,siege,spells,chaos,
 * magic,hero); cols = defense_type (small,medium,large,fort,normal,hero,divine,
 * none). */
static FLOAT const g_damage_table[8][8] = {
    /* small  medium large  fort   normal hero   divine none  */
    { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f }, /* none   */
    { 1.00f, 1.50f, 1.00f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f }, /* normal */
    { 2.00f, 0.75f, 1.00f, 0.35f, 1.00f, 0.50f, 1.00f, 1.50f }, /* pierce */
    { 1.00f, 0.50f, 1.00f, 1.50f, 1.00f, 0.50f, 1.00f, 1.50f }, /* siege  */
    { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.70f, 1.00f, 1.00f }, /* spells */
    { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f }, /* chaos  */
    { 1.25f, 0.75f, 2.00f, 0.35f, 1.00f, 0.50f, 1.00f, 1.00f }, /* magic  */
    { 1.00f, 1.00f, 1.00f, 0.50f, 1.00f, 1.00f, 1.00f, 1.00f }, /* hero   */
};

/* Apply the WC3 damage formula: attack×defense type multiplier, then armor
 * reduction (0.06 coefficient).  Chaos attack bypasses the type table.
 * Result is clamped to a minimum of 1. */
int G_AttackDamage(LPEDICT attacker, LPEDICT target, int base) {
    if (!attacker || !target || base <= 0)
        return base;
    DWORD atk = attacker->attack1.type;
    DWORD def = target->defense_type;
    if (atk >= 8) atk = 0;
    if (def >= 8) def = 7;
    FLOAT mult = (atk == ATK_CHAOS) ? 1.0f : g_damage_table[atk][def];
    FLOAT dmg = (FLOAT)base * mult;
    FLOAT armor = target->armor_value;
    if (armor >= 0.0f)
        dmg = dmg / (1.0f + armor * 0.06f);
    else
        dmg = dmg * (2.0f - 1.0f / (1.0f - armor * 0.06f));
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
    unit_entercombat(attacker, target);
    unit_entercombat(target, attacker);

    if (target->health.value <= damage) {
        target->health.value = 0;
        unit_leavecombat(target);
        unit_leavecombat(attacker);
        target->die(target, attacker);
        if (attacker->attackmove_waypoint) {
            order_attackmove(attacker, attacker->attackmove_waypoint);
        } else {
            attacker->stand(attacker);
        }
        return;
    } else {
        target->health.value -= damage;
    }
    if (can_attack(target)) {
        order_attack(target, attacker);
    } else if (target->pain) {
        target->pain(target);
    }
}

static void damage_target(LPEDICT ent) {
    LPEDICT other = ent->goalentity;
    int damage = G_AttackDamage(ent, other, ai_rolldamage1(ent, 1));
    T_Damage(other, ent, damage);
}

static void throw_missile(LPEDICT ent) {
    LPEDICT other = ent->goalentity;
    int damage = G_AttackDamage(ent, other, ai_rolldamage1(ent, 1));
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
    unit_changeangle(ent);
    unit_runwait(ent, damage_target);
}

static void ai_ranged(LPEDICT ent) {
    unit_changeangle(ent);
    unit_runwait(ent, throw_missile);
}

static void ai_melee_cooldown(LPEDICT ent) {
    if (M_DistanceToGoal(ent) > ent->attack1.range) {
        attack_walk(ent);
    } else {
        unit_runwait(ent, attack_melee);
    }
}

static void ai_ranged_cooldown(LPEDICT ent) {
    if (M_DistanceToGoal(ent) > ent->attack1.range) {
        attack_walk(ent);
    } else {
        unit_runwait(ent, attack_ranged);
    }
}

static void ai_attack_walk(LPEDICT ent) {
    if (M_DistanceToGoal(ent) > ent->attack1.range) {
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
    unit_entercombat(self, target);
    self->goalentity = target;
    attack_walk(self);
}

static FLOAT attack_speed_divisor(LPEDICT self) {
    if (self->hero.agi > 0)
        return 1.0f + (FLOAT)self->hero.agi * 0.02f;
    return 1.0f;
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
    if (self->sound_attack) { self->s.event = EV_ATTACK; self->s.sound = self->sound_attack; }
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
    if (self->sound_attack) { self->s.event = EV_ATTACK; self->s.sound = self->sound_attack; }
}

BOOL attack_menu_selecttarget(LPEDICT ent, LPEDICT target) {
    if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(ent, target)) {
        return false;
    }
    FOR_SELECTED_UNITS(ent->client, e) {
        e->attackmove_waypoint = NULL;
        order_attack(e, target);
    }
    return true;
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
        ent->attackmove_waypoint = NULL;
        ent->stand(ent);
    } else if (move_is_blocked(ent, distance, move_distance)) {
        ent->attackmove_waypoint = NULL;
        ent->stand(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static umove_t attackmove_move_walk = { "walk", ai_attackmove_walk, NULL, &a_attack };

/* Begin (or resume, after a kill) attack-moving toward a waypoint. */
void order_attackmove(LPEDICT self, LPEDICT waypoint) {
    self->attackmove_waypoint = waypoint;
    self->goalentity = waypoint;
    move_reset_progress(self);
    unit_setmove(self, &attackmove_move_walk);
}

static BOOL attackmove_selectlocation(LPEDICT clent, LPCVECTOR2 location) {
    LPEDICT waypoint;
    BOOL any = false;

    FOR_SELECTED_UNITS(clent->client, ent) {
        if (UNIT_IS_BUILDING(ent->class_id) || UNIT_SPEED(ent->class_id) <= 0) {
            continue;
        }
        if (!any) {
            waypoint = Waypoint_add(location);
            any = true;
        }
        order_attackmove(ent, waypoint);
    }
    return any;
}

void attack_command(LPEDICT ent) {
    UI_AddCancelButton(ent);
    ent->client->menu.on_entity_selected = attack_menu_selecttarget;
    ent->client->menu.on_location_selected = attackmove_selectlocation;
}

ability_t a_attack = {
    .cmd = attack_command,
};
