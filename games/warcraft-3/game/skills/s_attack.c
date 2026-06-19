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
        attacker->stand(attacker);
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

/* WC3 1.29 attack-type x defense-type damage multiplier table (verified from
 * MiscGame.txt). Rows = attack1.type (none,normal,pierce,siege,spells,chaos,
 * magic,hero); cols = defense_type (small,medium,large,fort,normal,hero,divine,
 * none). */
static const FLOAT g_damage_table[8][8] = {
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

/* Apply WC3 damage reduction: attack/defense type multiplier, then armor value
 * (0.06 coefficient). Used on the physical-attack path only; spells/triggers
 * call T_Damage directly with their own (already-typed) damage. */
static int G_AttackDamage(LPEDICT attacker, LPEDICT target, int base) {
    if (!attacker || !target || base <= 0) {
        return base;
    }
    DWORD at = attacker->attack1.type;
    DWORD dt = target->defense_type;
    if (at >= 8) at = 0;
    if (dt >= 8) dt = 7;
    FLOAT dmg = (FLOAT)base * g_damage_table[at][dt];
    FLOAT const armor = target->armor_value;
    if (armor >= 0.0f) {
        dmg /= (1.0f + armor * 0.06f);
    } else {
        dmg *= (2.0f - 1.0f / (1.0f + (-armor) * 0.06f));
    }
    int out = (int)(dmg + 0.5f);
    return out < 1 ? 1 : out;
}

/* Splash damage: full damage within areaFull of the impact, factorMedium within
 * areaMedium, factorSmall within areaSmall (WC3 area-of-effect attacks). The
 * type/armor multiplier is applied per target. Returns true if splash applied. */
static BOOL apply_splash(LPEDICT attacker, LPCVECTOR2 impact, int base) {
    FLOAT const rf = attacker->attack1.areaFull;
    FLOAT const rm = attacker->attack1.areaMedium;
    FLOAT const rs = attacker->attack1.areaSmall;
    FLOAT const fm = attacker->attack1.factorMedium;
    FLOAT const fs = attacker->attack1.factorSmall;
    FLOAT const rmax = MAX(rf, MAX(rm, rs));

    if (rmax <= 0.0f) {
        return false; /* not a splash attack */
    }
    FILTER_EDICTS(target, target->inuse && target != attacker) {
        if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(attacker, target)) {
            continue;
        }
        FLOAT const d = Vector2_distance(&target->s.origin2, impact);
        FLOAT factor;
        if (rf > 0.0f && d <= rf)      factor = 1.0f;
        else if (rm > 0.0f && d <= rm) factor = fm;
        else if (rs > 0.0f && d <= rs) factor = fs;
        else continue;
        int dmg = G_AttackDamage(attacker, target, (int)(base * factor + 0.5f));
        if (dmg > 0) {
            T_Damage(target, attacker, dmg);
        }
    }
    return true;
}

/* Bounce attack (e.g. Huntress moonglaive): hit the primary, then chain to the
 * nearest not-yet-hit enemies, losing damageLoss of the damage each bounce. */
#define BOUNCE_RANGE 300.0f
static BOOL apply_bounce(LPEDICT attacker, LPEDICT primary, int base) {
    if (attacker->attack1.weapon != WPN_MBOUNCE || attacker->attack1.maxTargets <= 1) {
        return false;
    }
    DWORD const max = attacker->attack1.maxTargets;
    FLOAT const loss = attacker->attack1.damageLoss;
    LPEDICT hit[16];
    DWORD nhit = 0;
    FLOAT factor = 1.0f;
    LPEDICT from = primary;

    T_Damage(primary, attacker, G_AttackDamage(attacker, primary, base));
    hit[nhit++] = primary;

    while (nhit < max && nhit < 16) {
        LPEDICT best = NULL;
        FLOAT best_d = BOUNCE_RANGE;
        FILTER_EDICTS(target, target->inuse && target != attacker) {
            if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(attacker, target)) {
                continue;
            }
            BOOL already = false;
            FOR_LOOP(i, nhit) if (hit[i] == target) { already = true; break; }
            if (already) continue;
            FLOAT const d = Vector2_distance(&target->s.origin2, &from->s.origin2);
            if (d < best_d) { best_d = d; best = target; }
        }
        if (!best) break;
        factor *= (1.0f - loss);
        int dmg = G_AttackDamage(attacker, best, (int)(base * factor + 0.5f));
        if (dmg > 0) T_Damage(best, attacker, dmg);
        hit[nhit++] = best;
        from = best;
    }
    return true;
}

static void damage_target(LPEDICT ent) {
    LPEDICT other = ent->goalentity;
    int base = ai_rolldamage1(ent, 1);
    if (apply_splash(ent, &other->s.origin2, base)) {
        return; /* splash already hit the primary target (at distance 0) */
    }
    if (apply_bounce(ent, other, base)) {
        return;
    }
    T_Damage(other, ent, G_AttackDamage(ent, other, base));
}

static void throw_missile(LPEDICT ent) {
    LPEDICT other = ent->goalentity;
    DWORD damage = G_AttackDamage(ent, other, ai_rolldamage1(ent, 1));
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

/* Post-swing recovery before the next attack.  WC3's "Cooldown Time" (ua1c) is
 * the total time *between* attacks, and the damage point is the windup *within*
 * that window — so the full cycle (windup + recovery) must equal the cooldown,
 * i.e. recovery = cooldown - damagePoint.  (Previously recovery was the full
 * cooldown, making the cycle cooldown+damagePoint and every unit attack too
 * slowly, the more so the larger its damage point.) */
/* Increased attack speed divisor: a hero's Agility speeds up its whole attack
 * (windup + recovery) by AgiAttackSpeedBonus per point (WC3 MiscGame, 0.02 =
 * +2% attack speed per Agility).  hero.agi is 0 on non-heroes -> divisor 1.0. */
#define AGI_ATTACKSPEED_BONUS 0.02f
static FLOAT attack_speed_factor(LPCEDICT self) {
    return 1.0f + (FLOAT)self->hero.agi * AGI_ATTACKSPEED_BONUS;
}

static FLOAT attack_recovery(LPCEDICT self) {
    FLOAT const recovery = self->attack1.cooldown - self->attack1.damagePoint;
    return (recovery > 0.0f ? recovery : 0.0f) / attack_speed_factor(self);
}

void attack_melee_cooldown(LPEDICT self) {
    unit_setmove(self, &attack_move_melee_cooldown);
    self->wait = attack_recovery(self);
}

void attack_melee(LPEDICT self) {
    unit_setmove(self, &attack_move_melee);
    self->wait = self->attack1.damagePoint / attack_speed_factor(self);
}

void attack_ranged_cooldown(LPEDICT self) {
    unit_setmove(self, &attack_move_ranged_cooldown);
    self->wait = attack_recovery(self);
}

void attack_ranged(LPEDICT self) {
    unit_setmove(self, &attack_move_ranged);
    self->wait = self->attack1.damagePoint / attack_speed_factor(self);
}

BOOL attack_menu_selecttarget(LPEDICT ent, LPEDICT target) {
    if (target->targtype == TARG_GROUND) {
        FOR_SELECTED_UNITS(ent->client, e) {
            order_attack(e, target);
        }
        return true;
    } else {
        return false;
    }
}

void attack_command(LPEDICT ent) {
    UI_AddCancelButton(ent);
    ent->client->menu.on_entity_selected = attack_menu_selecttarget;
}

ability_t a_attack = {
    .cmd = attack_command,
};
