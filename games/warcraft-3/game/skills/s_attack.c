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

void attack_walk(LPEDICT ent);
void attack_melee(LPEDICT ent);
void attack_melee_cooldown(LPEDICT ent);
void attack_ranged(LPEDICT ent);
void attack_ranged_cooldown(LPEDICT ent);
void order_attack(LPEDICT self, LPEDICT target);

typedef struct {
    LPEDICT target;
    LPCVECTOR2 fixed_target;
    VECTOR3 start;
    VECTOR3 dir;
    DWORD speed;
    DWORD model;
    DWORD damage;
    DWORD attack_type;
    unitAttack_t const *attack;
    DWORD area_targets;
}  rocketDesc_t;

BOOL S_UnitAttackSlotEnabled(LPCEDICT attacker, DWORD slot) {
    return attacker && slot < 2 && attacker->data.UnitWeapons &&
        (attacker->data.UnitWeapons->attacksEnabled & (1 << slot)) != 0;
}

/* Attack 1/2 remain the authored runtime copies. Select the compatible slot
 * from the target whenever attack behavior reads a profile. */
static unitAttack_t const *attack_profile(LPCEDICT attacker, LPCEDICT target) {
    DWORD flag = target ? G_TargetFlagForType(target->targtype) : 0;
    if (attacker && target && target->destructable.initialized && target->targtype == TARG_TREE) {
        if (attacker->attack1.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 0)) return &attacker->attack1;
        if (attacker->attack2.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 1)) return &attacker->attack2;
    }
    if (attacker && flag && attacker->attack1.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 0) &&
        (attacker->attack1.targetsAllowed & flag)) return &attacker->attack1;
    if (attacker && flag && attacker->attack2.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 1) &&
        (attacker->attack2.targetsAllowed & flag)) return &attacker->attack2;
    return attacker ? &attacker->attack1 : NULL;
}
#define ACTIVE_ATTACK(ent) attack_profile((ent), (ent)->goalentity)

/* Spawn a projectile entity aimed at desc->target.
 * The entity is given MOVETYPE_FLYMISSILE so that SV_Physics_Toss() in
 * g_phys.c will move it each frame until it reaches the target. */
void fire_rocket(LPEDICT ent, rocketDesc_t const *desc) {
    LPEDICT rocket;
    VECTOR2 aim;

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
    rocket->projectile_attack_type = desc->attack_type;
    if (desc->fixed_target) {
        rocket->aiflags |= AI_PROJECTILE_FIXED_TARGET;
        rocket->channel.origin = *desc->fixed_target;
        /* ARTILLERY flies to the snapshotted point, but retaining the original
         * unit identity lets impact apply the ordinary primary-hit listeners
         * only when that same unit is still inside the splash bands. */
        rocket->goalentity = desc->target;
        rocket->channel.target_spawn_time = desc->target ? desc->target->spawn_time : 0;
        if (desc->attack) {
            rocket->artillery.attack_type = desc->attack->type;
            rocket->artillery.area_targets = desc->area_targets;
            rocket->artillery.targets_allowed = desc->attack->targetsAllowed;
            rocket->artillery.area_full = desc->attack->areaFull;
            rocket->artillery.area_medium = desc->attack->areaMedium;
            rocket->artillery.area_small = desc->attack->areaSmall;
            rocket->artillery.factor_medium = desc->attack->factorMedium;
            rocket->artillery.factor_small = desc->attack->factorSmall;
        }
    } else {
        rocket->goalentity = desc->target;
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

static FLOAT ai_rolldamage1(LPEDICT self, int weapon) {
    unitAttack_t const *atk = ACTIVE_ATTACK(self);
    FLOAT damageBase = atk->damageBase;
    (void)weapon;
    FOR_LOOP(i, atk->numberOfDice) {
        /* Warsmash treats a malformed zero-sided die as contributing +1
         * instead of taking modulo zero. Normal Warcraft data has S > 0. */
        damageBase += atk->sidesPerDie
                    ? (FLOAT)(rand() % atk->sidesPerDie + 1)
                    : 1.0f;
    }
    return damageBase + atk->temporaryDamageBonus;
}

void M_GetEntityMatrix(LPCENTITYSTATE entity, LPMATRIX4 matrix) {
    Matrix4_identity(matrix);
    Matrix4_translate(matrix, &entity->origin);
    Matrix4_rotate(matrix, &(VECTOR3){0, 0, entity->angle * 180 / M_PI}, ROTATE_XYZ);
    Matrix4_scale(matrix, &(VECTOR3){entity->scale, entity->scale, entity->scale});
}

static BOOL can_attack(LPCEDICT ent) {
    if (S_UnitIsCycloned(ent) || G_BuildingIsUnsummoning(ent)) return false;
    if (!S_HumanCanAttack(ent)) return false;
    if (!S_CargoAttacksEnabled(ent)) return false;
    if ((!S_UnitAttackSlotEnabled(ent, 0) || ent->attack1.type == ATK_NONE) &&
        (!S_UnitAttackSlotEnabled(ent, 1) || ent->attack2.type == ATK_NONE))
        return false;
    if (!ent->currentmove || ent->currentmove->proc != CAbilityAttack)
        return true;
    return false;
}

/* Weapon target masks are authoritative for ordinary unit targets as well as
 * destructables.  UnitData.targetType supplies the target category while
 * UnitWeapons.targs1/ua1g supplies the attacker's allowed categories. */
BOOL S_AttackCanTarget(LPCEDICT attacker, LPCEDICT target) {
    DWORD flag;

    if (!attacker || G_BuildingIsUnsummoning(attacker) || !target || !target->inuse || attacker == target ||
        ((!S_UnitAttackSlotEnabled(attacker, 0) || attacker->attack1.type == ATK_NONE) &&
         (!S_UnitAttackSlotEnabled(attacker, 1) || attacker->attack2.type == ATK_NONE)) || S_UnitIsCycloned(target)) {
        return false;
    }
    if (attacker->s.player < MAX_PLAYERS && S_UnitIsInvisibleToPlayer(target, attacker->s.player)) return false;
    if (target->destructable.initialized) {
        return G_DestructableCanBeAttackedBy(attacker, target);
    }
    if (M_IsDead((LPEDICT)target)) return false;

    flag = G_TargetFlagForType(target->targtype);
    return flag && ((attacker->attack1.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 0) && (attacker->attack1.targetsAllowed & flag)) ||
                    (attacker->attack2.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 1) && (attacker->attack2.targetsAllowed & flag)));
}

/* Delayed damage can outlive its attack order; only that order may complete or resume its parent behavior. */
static void attack_finish_after_combat(LPEDICT attacker, LPCEDICT target) {
    if (!attacker || M_IsDead(attacker) || !attacker->currentmove ||
        attacker->currentmove->proc != CAbilityAttack || attacker->goalentity != target) return;
    unit_leavecombat(attacker);
    attacker->goalentity = NULL;
    if (G_BuildingIsUnsummoning(attacker)) {
        attacker->currentmove = NULL;
        attacker->animation = NULL;
        attacker->wait = 0;
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
    if (S_AttackCanTarget(attacker, attacker ? attacker->goalentity : NULL)) {
        return false;
    }
    if (attacker) attack_finish_after_combat(attacker, attacker->goalentity);
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
static int attack_damage_type(LPEDICT attacker, LPEDICT target, int base, DWORD atk);
int G_AttackDamage(LPEDICT attacker, LPEDICT target, int base) {
    return attack_damage_type(attacker, target, base,
                              attacker && target ? attack_profile(attacker, target)->type : 0);
}

int G_AttackDamageWithType(LPEDICT attacker, LPEDICT target, int base, DWORD type) {
    return attack_damage_type(attacker, target, base, type);
}

static int attack_damage_type(LPEDICT attacker, LPEDICT target, int base, DWORD atk) {
    if (!attacker || !target || base <= 0) return base;
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
    BOOL instant_kill;

    if (!target || target->invulnerable || S_UnitIsCycloned(target) || M_IsDead(target)) {
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
        if (G_DestructableApplyDamage(target, attacker, (FLOAT)damage)) {
            attack_finish_after_combat(attacker, target);
        }
        return;
    }
    damage = S_SpiritLinkRedirect(target, attacker, damage);
    if (damage <= 0) return;
    S_UnitAbilityEvent(target, A_DAMAGED);
    /* GetEventDamage / GetEventDamageSource read value/source from these events. */
    G_PublishEventWithValue(target, EVENT_UNIT_DAMAGED, attacker, damage);
    G_PublishEventWithValue(target, EVENT_PLAYER_UNIT_DAMAGED, attacker, damage);
    /* Only real post-mitigation unit damage should refresh the owning Hero shortcut's transient attack warning. */
    G_AlertHeroShortcutDamage(target);
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (target->abilstatus[i].level && target->abilstatus[i].code == MAKEFOURCC('B','U','s','l'))
            memset(target->abilstatus + i, 0, sizeof(target->abilstatus[i]));
    unit_updatestatuses(target);
    unit_entercombat(attacker, target);
    unit_entercombat(target, attacker);

    if (target->health.value <= damage) {
        G_SetHealth(target, 0);
        unit_leavecombat(target);
        target->die(target, attacker);
        attack_finish_after_combat(attacker, target);
        return;
    } else {
        G_AddHealth(target, -damage);
    }
    if (can_attack(target) && !unit_is_walking(target) &&
        S_SpellIsEnemy(target, attacker)) {
        order_attack(target, attacker);
    } else if (target->pain) {
        target->pain(target);
    }
}

void S_ResolveAttackHit(LPEDICT attacker, LPEDICT target, int damage) {
    if (S_EvasionRoll(target)) return;
    { FLOAT const miss = S_CurseMissChance(attacker); if (miss > 0.0f && (FLOAT)(rand() % 100) < miss * 100.0f) return; }
    S_HumanBreakInvisibility(attacker);
    S_PermanentInvisibilityReveal(attacker);
    damage = S_OrbAnnihilationDamage(attacker,
        S_SearingArrowDamage(attacker, S_BlackArrowDamage(attacker, S_CriticalStrikeDamage(attacker, damage))));
    damage = (int)((FLOAT)damage * (1.0f + S_TrueshotAttackBonus(attacker) + S_CommandAuraAttackBonus(attacker) +
                                         S_WarDrumsAttackBonus(attacker) + S_RoarDamageBonus(attacker)
                                         - S_CrippleDamageReduction(attacker) - S_SoulBurnDamageReduction(attacker)));
    damage = S_HumanAttackDamage(attacker, target, damage);
    if (damage <= 0) return;
    { abilityAliasRef_t bash = S_ResolveAbilityAlias(attacker, MAKEFOURCC('A', 'H', 'b', 'h'));
    if (bash.alias && bash.level && (FLOAT)(rand() % 100) < S_SpellData(bash.alias, bash.level, 1)) {
        damage += (int)S_SpellData(bash.alias, bash.level, 3);
        unit_addtimedstatus(target, "Bstu", 1, S_SpellDuration(bash.alias, bash.level, false));
    } }
    DWORD wind_level = G_UnitStatusLevel(attacker, MAKEFOURCC('B', 'O', 'w', 'k'));
    if (wind_level) {
        damage += (int)S_SpellData(MAKEFOURCC('A', 'O', 'w', 'k'), wind_level, 3);
        attacker->s.renderfx &= ~RF_HIDDEN;
        FOR_LOOP(i, MAX_UNIT_STATUSES)
            if (attacker->abilstatus[i].code == MAKEFOURCC('B', 'O', 'w', 'k')) memset(attacker->abilstatus + i, 0, sizeof(attacker->abilstatus[i]));
    }
    damage = S_PossessionDamageTaken(target, damage);
    damage = S_HardenedSkinDamage(target, damage);
    if (damage <= 0) return;
    G_PlayCombatImpactSound(attacker, target);
    S_IncinerateOnHit(attacker, target);
    T_Damage(target, attacker, damage);
    S_CreepAttackOnHit(attacker, target);
    S_PulverizeAttack(attacker, target);
    S_HumanAttackSplash(attacker, target, damage);
    { DWORD cleave_code = G_UnitAbilityLevel(attacker, MAKEFOURCC('A','N','c','a')) ?
            MAKEFOURCC('A','N','c','a') : MAKEFOURCC('A','C','c','e');
    DWORD cleave_level = G_UnitAbilityLevel(attacker, cleave_code);
    if (cleave_level) {
        FLOAT radius = S_SpellNumber(cleave_code, ABILITY_NUMBER_AREA, cleave_level);
        FLOAT fraction = S_SpellData(cleave_code, cleave_level, 1);
        FILTER_EDICTS(other, other != target && S_SpellIsAliveTarget(other) &&
                      S_SpellIsEnemy(attacker, other) &&
                      Vector2_distance(&other->s.origin2, &target->s.origin2) <= radius)
            T_Damage(other, attacker, (int)MAX(1.0f, damage * fraction));
    } }
    S_BlackArrowDeath(attacker, target);
    S_MoonGlaiveAttack(attacker, target, damage);
    S_SlowPoisonOnHit(attacker, target);
    S_OrbOnHit(attacker, target);
    S_PoisonOnHit(attacker, target);
    G_AddHealth(attacker, damage * S_VampiricLifeSteal(attacker));
    if (target->inuse) {
        FLOAT thorns = S_ThornsDamageReturn(target, attacker, damage);
        FLOAT spiked = S_SpikedDamageReturn(target, damage);
        if (thorns + spiked > 0.0f) T_Damage(attacker, target, (int)(thorns + spiked));
    }
}


static BOOL artillery_splash_target_allowed(LPEDICT attacker, LPEDICT target, DWORD mask, DWORD targets_allowed) {
    DWORD flag;

    if (!attacker || !target || !target->inuse || target == attacker || M_IsDead(target)) return false;
    if (!mask) mask = targets_allowed;
    flag = G_TargetFlagForType(target->targtype);
    return flag && (mask & flag) != 0;
}

/* Artillery weapons damage around the fixed impact point using the authored
 * full/medium/small radii. Warsmash converts an ARTILLERY unit target to an
 * AbilityPointTarget at the damage point, so the original unit is not a
 * guaranteed direct hit if it moves before impact. Preserve OpenRealm's
 * established hit contract: only that original target receives primary-hit
 * listeners (if it is still in the blast); secondary/Attack-Ground victims
 * receive physical splash damage without multiplying orb/lifesteal/cleave. */
void S_ResolveArtilleryPointHit(LPEDICT attacker, LPEDICT primary, LPCVECTOR2 impact, int raw_damage,
                                struct edictArtillery_s const *profile) {
    FLOAT max_radius;

    if (!attacker || !impact || raw_damage <= 0) return;
    if (!profile) return;
    max_radius = MAX(profile->area_full, MAX(profile->area_medium, profile->area_small));
    if (max_radius < 0.0f) return;

    FILTER_EDICTS(other, artillery_splash_target_allowed(attacker, other, profile->area_targets, profile->targets_allowed)) {
        FLOAT const distance = MAX(0.0f, Vector2_distance(&other->s.origin2, impact) - MAX(0.0f, other->collision));
        FLOAT factor;
        int damage;

        if (distance <= profile->area_full) factor = 1.0f;
        else if (distance <= profile->area_medium) factor = profile->factor_medium;
        else if (distance <= profile->area_small) factor = profile->factor_small;
        else continue;
        if (factor <= 0.0f) continue;
        damage = attack_damage_type(attacker, other, (int)MAX(1.0f, (FLOAT)raw_damage * factor), profile->attack_type);
        if (other == primary) S_ResolveAttackHit(attacker, other, damage);
        else T_Damage(other, attacker, damage);
    }
}

void S_ResolveArtilleryHit(LPEDICT attacker, LPEDICT target, int raw_damage) {
    VECTOR2 impact;
    unitAttack_t const *atk;
    struct edictArtillery_s profile = { 0 };
    if (!target) return;
    atk = attack_profile(attacker, target);
    if (!atk) return;
    profile.attack_type = atk->type;
    profile.targets_allowed = atk->targetsAllowed;
    profile.area_full = atk->areaFull; profile.area_medium = atk->areaMedium; profile.area_small = atk->areaSmall;
    profile.factor_medium = atk->factorMedium; profile.factor_small = atk->factorSmall;
    if (attacker->data.UnitWeapons)
        profile.area_targets = atk == &attacker->attack2 ? attacker->data.UnitWeapons->attack2.areaTargets
                                                       : attacker->data.UnitWeapons->attack1.areaTargets;
    impact = target->s.origin2;
    S_ResolveArtilleryPointHit(attacker, target, &impact, raw_damage, &profile);
}

static BOOL attack_animation_can_finish(LPCEDICT ent) {
    return ent && ent->animation && ent->animation->interval[1] > ent->animation->interval[0];
}

static void damage_target(LPEDICT ent) {
    if (attack_stop_if_target_invalid(ent)) return;
    umove_t const *move = ent->currentmove;
    LPEDICT target = ent->goalentity;
    S_ResolveAttackHit(ent, ent->goalentity, G_AttackDamage(ent, ent->goalentity, ai_rolldamage1(ent, 1)));
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
    unitAttack_t const *atk = ACTIVE_ATTACK(ent);
    VECTOR3 origin = Matrix4_multiply_vector3(&matrix, &atk->origin);
    VECTOR2 impact = other->s.origin2;
    fire_rocket(ent, &(rocketDesc_t) {
        .start = origin,
        .target = other,
        .fixed_target = atk->weapon == WPN_ARTILLERY ? &impact : NULL,
        .speed = atk->projectile.speed,
        .model = atk->projectile.model,
        .damage = damage,
        .attack_type = atk->type,
        .attack = atk,
        .area_targets = ent->data.UnitWeapons ? (atk == &ent->attack2 ? ent->data.UnitWeapons->attack2.areaTargets : ent->data.UnitWeapons->attack1.areaTargets) : 0,
    });
    /* See damage_target(): if the model has no finite attack sequence there
     * will be no animation-end callback to start recovery, so do it at the
     * projectile launch point instead. */
    if (S_AttackCanTarget(ent, ent->goalentity) && !attack_animation_can_finish(ent))
        attack_ranged_cooldown(ent);
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

static FLOAT attack_minimum_range(LPCEDICT ent) {
    return ent && ent->data.UnitWeapons ? MAX(0.0f, ent->data.UnitWeapons->minimumAttackRange) : 0.0f;
}

static BOOL attack_target_too_close_for(LPCEDICT ent, LPCEDICT target) {
    FLOAT const minimum = attack_minimum_range(ent);
    if (!ent || !target || minimum <= 0.0f) return false;
    return Vector2_distance(&target->s.origin2, &ent->s.origin2) < minimum;
}

static BOOL attack_target_too_close(LPEDICT ent) {
    return ent && attack_target_too_close_for(ent, ent->goalentity);
}

static void attack_retreat_from_target(LPEDICT ent) {
    VECTOR2 dir;
    FLOAT len;

    if (!ent || !ent->goalentity) return;
    dir = Vector2_sub(&ent->s.origin2, &ent->goalentity->s.origin2);
    len = Vector2_len(&dir);
    if (len <= 0.001f) dir = MAKE(VECTOR2, cosf(ent->s.angle + (FLOAT)M_PI), sinf(ent->s.angle + (FLOAT)M_PI));
    else { dir.x /= len; dir.y /= len; }
    ent->s.angle = atan2f(dir.y, dir.x);
    ent->movement.heading = ent->s.angle;
    ent->movement.flow_direct = true;
    ent->movement.flow_generation = 0;
    unit_moveindirection(ent);
}

static BOOL attack_target_out_of_range_for(LPCEDICT ent, LPCEDICT target) {
    FLOAT footprint, range, ensnare_range;

    if (!ent || !target) return true;

    /* Ensnare DataC forces the bound unit's own attacks to melee range. */
    ensnare_range = S_EnsnareMeleeRange(ent);
    range = ensnare_range > 0.0f ? ensnare_range : attack_profile(ent, target)->range;
    if ((G_UnitIsBuilding(target->class_id) || G_IsDestructable(target)) && target->pathtex) {
        footprint = CM_DistanceToPathingFootprint(target, &ent->s.origin2);
        if (footprint < FLT_MAX) {
            return footprint > ent->collision + range;
        }
    }
    return Vector2_distance(&target->s.origin2, &ent->s.origin2) > range;
}

static BOOL attack_target_out_of_range(LPEDICT ent) {
    return !ent || attack_target_out_of_range_for(ent, ent->goalentity);
}

/* Movement-disabled attackers (ordinary towers/buildings) must not auto-acquire
 * something they can see but can never approach.  Mobile units still acquire
 * throughout uacq and chase normally; Hold Position keeps its separate
 * disable-chase lifecycle. */
BOOL S_AttackCanAutoAcquire(LPCEDICT attacker, LPCEDICT target) {
    if (!S_AttackCanTarget(attacker, target)) return false;
    if ((attacker->aiflags & AI_IMMOBILE) &&
        (attack_target_out_of_range_for(attacker, target) || attack_target_too_close_for(attacker, target)))
        return false;
    return true;
}

static void ai_melee_cooldown(LPEDICT ent) {
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    if (attack_target_out_of_range(ent) || attack_target_too_close(ent)) {
        attack_walk(ent);
    } else {
        unit_runwait(ent, attack_melee);
    }
}

static void ai_ranged_cooldown(LPEDICT ent) {
    if (attack_stop_if_target_invalid(ent)) {
        return;
    }
    if (attack_target_out_of_range(ent) || attack_target_too_close(ent)) {
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
        /* Hold Position and movement-disabled structures cannot chase an
         * out-of-range target.  Finish the attack behavior instead of leaving
         * an immobile tower stuck forever in attack_walk. */
        if (ent->movement.holding_position || (ent->aiflags & AI_IMMOBILE)) {
            attack_finish_after_combat(ent, ent->goalentity);
            return;
        }
        if (!S_UnitCanTranslate(ent)) return;
        unit_changeangle(ent);
        unit_moveindirection(ent);
    } else if (attack_target_too_close(ent)) {
        /* Artillery minimum range is a real dead zone. Mobile siege units back
         * away until they can fire; Hold Position/immobile attackers cannot. */
        if (ent->movement.holding_position || (ent->aiflags & AI_IMMOBILE)) {
            attack_finish_after_combat(ent, ent->goalentity);
            return;
        }
        if (!S_UnitCanTranslate(ent)) return;
        attack_retreat_from_target(ent);
    } else if (ACTIVE_ATTACK(ent)->weapon == WPN_MISSILE || ACTIVE_ATTACK(ent)->weapon == WPN_ARTILLERY) {
        attack_ranged(ent);
    } else {
        attack_melee(ent);
    }
}

static umove_t attack_move_walk = { "walk", ai_attack_walk, NULL, CAbilityAttack };
static umove_t attack_move_melee_cooldown = { "stand ready", ai_melee_cooldown, NULL, CAbilityAttack };
static umove_t attack_move_melee = { "attack", ai_melee, attack_melee_cooldown, CAbilityAttack };
static umove_t attack_move_ranged_cooldown = { "stand ready", ai_ranged_cooldown, NULL, CAbilityAttack };
static umove_t attack_move_ranged = { "attack range", ai_ranged, attack_ranged_cooldown, CAbilityAttack };

void attack_walk(LPEDICT self) {
    unit_setmove(self, &attack_move_walk);
}

/* Set the attack target and start walking toward attack range. */
void order_attack(LPEDICT self, LPEDICT target) {
    if (!self || S_UnitIsCycloned(self) || S_GoldMineWorkerIsInside(self) ||
        !S_AttackCanTarget(self, target)) {
        return;
    }
    unit_entercombat(self, target);
    self->goalentity = target;
    attack_walk(self);
}

/* Player orders replace retained movement; automatic acquisition keeps it so combat can resume Follow/Patrol. */
BOOL S_OrderAttack(LPEDICT self, LPEDICT target) {
    if (!self || M_IsDead(self) || S_UnitIsCycloned(self) || S_GoldMineWorkerIsInside(self) ||
        !S_AttackCanTarget(self, target))
        return false;
    self->movement.attackmove_waypoint = NULL;
    self->movement.patrol_a = self->movement.patrol_b = self->movement.patrol_target = NULL;
    self->movement.follow_target = NULL;
    self->movement.holding_position = false;
    order_attack(self, target);
    return true;
}

static FLOAT attack_speed_divisor(LPEDICT self) {
    FLOAT const agi_bonus = game.constants.combatConstantsLoaded
                          ? game.constants.agiAttackSpeedBonus
                          : 0.02f;
    FLOAT total_bonus = (FLOAT)self->hero.agi * agi_bonus + S_BloodlustAttackBonus(self)
                      + S_FrenzyAttackBonus(self) + S_UnholyFrenzyAttackBonus(self)
                      - S_CrippleAttackReduction(self) - S_SlowPoisonAttackReduction(self)
                      - S_DefendAttackReduction(self) - S_CreepAttackSpeedReduction(self) - S_SlowAuraAttackReduction(self);
    if (S_AuraUnitActive(self)) {
        FOR_LOOP(i, globals.num_edicts) {
            LPEDICT aura = g_edicts + i;
            DWORD level = G_UnitAbilityLevel(aura, MAKEFOURCC('A', 'O', 'a', 'e'));
            abilityLevel_t const *ability_level;
            if (!S_AuraUnitActive(aura) || !level || !S_SpellIsFriend(aura, self)) continue;
            ability_level = G_AbilityLevel(MAKEFOURCC('A', 'O', 'a', 'e'), level);
            if (Vector2_distance(&aura->s.origin2, &self->s.origin2) <= ability_level->area)
                total_bonus += ability_level->data[1].number * 0.01f;
        }
    }
    /* Warsmash clamps total attack-speed bonus to [-90%, +400%]. OpenRealm
     * combines authored buffs/debuffs with Agility before applying the same
     * timing bounds. */
    total_bonus = MAX(-0.9f, MIN(4.0f, total_bonus));
    return 1.0f + total_bonus;
}

void attack_melee_cooldown(LPEDICT self) {
    FLOAT divisor = attack_speed_divisor(self);
    unit_setmove(self, &attack_move_melee_cooldown);
    self->wait = MAX(0.0f, (ACTIVE_ATTACK(self)->cooldown - ACTIVE_ATTACK(self)->damagePoint) / divisor);
    /* Burrow cargo can reduce the authored cooldown below damagePoint.  A zero
     * recovery means the next swing starts immediately; unit_runwait() treats
     * wait==0 as inactive, so transition explicitly instead of stalling after
     * one attack. */
    if (self->wait <= 0.0f) attack_melee(self);
}

void attack_melee(LPEDICT self) {
    FLOAT divisor = attack_speed_divisor(self);
    S_PermanentInvisibilityReveal(self);
    unit_setmove(self, &attack_move_melee);
    self->wait = ACTIVE_ATTACK(self)->damagePoint / divisor;
}

void attack_ranged_cooldown(LPEDICT self) {
    FLOAT divisor = attack_speed_divisor(self);
    unit_setmove(self, &attack_move_ranged_cooldown);
    self->wait = MAX(0.0f, (ACTIVE_ATTACK(self)->cooldown - ACTIVE_ATTACK(self)->damagePoint) / divisor);
    if (self->wait <= 0.0f) attack_ranged(self);
}

void attack_ranged(LPEDICT self) {
    FLOAT divisor = attack_speed_divisor(self);
    S_PermanentInvisibilityReveal(self);
    unit_setmove(self, &attack_move_ranged);
    self->wait = ACTIVE_ATTACK(self)->damagePoint / divisor;
}

/* ---- Attack Ground --------------------------------------------------------
 * Warsmash exposes Attack Ground for ARTILLERY weapons. The order keeps the
 * clicked point authoritative, walks a mobile siege unit into its normal
 * min/max range band, and snapshots that same point into each projectile at
 * the damage point. */
static BOOL attack_ground_valid(LPCEDICT ent) {
    return ent && ent->inuse && !M_IsDead((LPEDICT)ent) && S_UnitAttackSlotEnabled(ent, 0) && ent->attack1.type != ATK_NONE &&
           ent->attack1.weapon == WPN_ARTILLERY && !S_UnitIsCycloned(ent) &&
           S_HumanCanAttack(ent) && S_CargoAttacksEnabled(ent);
}

static FLOAT attack_ground_distance(LPCEDICT ent) {
    return ent ? Vector2_distance(&ent->s.origin2, &ent->channel.origin) : FLT_MAX;
}

static BOOL attack_ground_out_of_range(LPCEDICT ent) {
    return !ent || attack_ground_distance(ent) > ent->attack1.range;
}

static BOOL attack_ground_too_close(LPCEDICT ent) {
    FLOAT const minimum = attack_minimum_range(ent);
    return ent && minimum > 0.0f && attack_ground_distance(ent) < minimum;
}

static void attack_ground_walk(LPEDICT ent);
static void attack_ground_ranged(LPEDICT ent);
static void attack_ground_cooldown(LPEDICT ent);

static void attack_ground_stop(LPEDICT ent) {
    if (!ent) return;
    ent->goalentity = NULL;
    if (ent->stand) ent->stand(ent);
    else ent->currentmove = NULL;
}

static void throw_artillery_ground(LPEDICT ent) {
    int damage;
    MATRIX4 matrix;
    VECTOR3 origin;
    VECTOR2 impact;

    if (!attack_ground_valid(ent)) { attack_ground_stop(ent); return; }
    impact = ent->channel.origin;
    damage = (int)ai_rolldamage1(ent, 1);
    M_GetEntityMatrix(&ent->s, &matrix);
    origin = Matrix4_multiply_vector3(&matrix, &ent->attack1.origin);
    fire_rocket(ent, &(rocketDesc_t) {
        .start = origin,
        .fixed_target = &impact,
        .speed = ent->attack1.projectile.speed,
        .model = ent->attack1.projectile.model,
        .damage = damage,
        .attack = &ent->attack1,
        .area_targets = ent->data.UnitWeapons ? ent->data.UnitWeapons->attack1.areaTargets : 0,
    });
    if (ent->currentmove && ent->currentmove->proc == CAbilityAttackGround &&
        !attack_animation_can_finish(ent))
        attack_ground_cooldown(ent);
}

static void ai_attack_ground_ranged(LPEDICT ent) {
    if (!attack_ground_valid(ent)) { attack_ground_stop(ent); return; }
    unit_changeangle(ent);
    unit_runwait(ent, throw_artillery_ground);
}

static void ai_attack_ground_cooldown(LPEDICT ent) {
    if (!attack_ground_valid(ent)) { attack_ground_stop(ent); return; }
    if (attack_ground_out_of_range(ent) || attack_ground_too_close(ent)) attack_ground_walk(ent);
    else unit_runwait(ent, attack_ground_ranged);
}

static void ai_attack_ground_walk(LPEDICT ent) {
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

static umove_t attack_ground_move_walk = { "walk", ai_attack_ground_walk, NULL, CAbilityAttackGround };
static umove_t attack_ground_move_cooldown = { "stand ready", ai_attack_ground_cooldown, NULL, CAbilityAttackGround };
static umove_t attack_ground_move_ranged = { "attack range", ai_attack_ground_ranged, attack_ground_cooldown, CAbilityAttackGround };

static void attack_ground_walk(LPEDICT ent) {
    unit_setmove(ent, &attack_ground_move_walk);
}

static void attack_ground_cooldown(LPEDICT ent) {
    FLOAT divisor = attack_speed_divisor(ent);
    unit_setmove(ent, &attack_ground_move_cooldown);
    ent->wait = MAX(0.0f, (ent->attack1.cooldown - ent->attack1.damagePoint) / divisor);
    if (ent->wait <= 0.0f) attack_ground_ranged(ent);
}

static void attack_ground_ranged(LPEDICT ent) {
    FLOAT divisor = attack_speed_divisor(ent);
    S_PermanentInvisibilityReveal(ent);
    unit_setmove(ent, &attack_ground_move_ranged);
    ent->wait = ent->attack1.damagePoint / divisor;
    if (ent->sound.attack) G_PlaySound(NULL, ent, CHAN_WEAPON, ent->sound.attack, 1.0f, 1.0f, 0.0f);
}

BOOL S_OrderAttackGround(LPEDICT unit, LPCVECTOR2 point) {
    LPEDICT waypoint;

    if (!unit || !point || !attack_ground_valid(unit) || S_GoldMineWorkerIsInside(unit) ||
        S_UnitPolymorphed(unit)) return false;
    waypoint = Waypoint_add(point);
    if (!waypoint) return false;
    unit->movement.attackmove_waypoint = NULL;
    unit->movement.patrol_a = unit->movement.patrol_b = unit->movement.patrol_target = NULL;
    unit->movement.follow_target = NULL;
    unit->movement.holding_position = false;
    unit->movement.group_speed = 0.0f;
    S_SpellCancelChannel(unit);
    unit->goalentity = waypoint;
    attack_ground_walk(unit);
    unit->channel.origin = *point;
    return true;
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
    if (issued) G_QueueAttackOrderSound(G_GetMainControllableUnit(ent->client));
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

    if (!S_UnitCanTranslate(ent)) return;
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

static umove_t attackmove_move_walk = { "walk", ai_attackmove_walk, NULL, CAbilityAttack };

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

static BOOL attack_ground_selectlocation(LPEDICT clent, LPCVECTOR2 location) {
    BOOL any = false;

    if (!clent || !clent->client || !location) return false;
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        if (ent->attack1.weapon != WPN_ARTILLERY) continue;
        if (G_IssueUnitPointOrder(ent, "attackground", location,
                                  clent->client->menu.order_queued,
                                  clent->client->ps.number, 0.0f)) any = true;
    }
    if (any) G_SendPointConfirmation(clent, location, true);
    return any;
}

BZ_COMMAND_PROC(AbilityAttackGround) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_location_selected = attack_ground_selectlocation;
    clent->client->menu.supports_order_queue = true;
}
