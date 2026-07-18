#include "g_local.h"

//void unit_die(LPEDICT self);
//void unit_decay2(LPEDICT self);
void unit_decay1(LPEDICT self);
void unit_begin_decay(LPEDICT self);
void unit_decay_think(LPEDICT self);
void unit_cooldown(LPEDICT self);
void unit_stand(LPEDICT self);
static BOOL G_UnitIsHero(LPCEDICT ent);

/* WC3 corpse lifetime: DecayTime (flesh, 2s) + BoneDecayTime (bone, 88s) = 90s
 * after the death animation, then the corpse is removed (MiscData.txt). */
#define UNIT_DECAY_SECONDS 90.0f

void ai_birth2(LPEDICT self) {
    unit_runwait(self, unit_stand);
}

//static mmove_t unit_move_decay2 = { "Decay Bone", NULL, unit_die };
//static mmove_t unit_move_decay1 = { "Decay Flesh", NULL, unit_decay2 };
static umove_t unit_move_birth = { "birth", ai_birth, unit_stand };
static umove_t unit_move_stand = { "stand", ai_stand, unit_stand };
static umove_t unit_move_stand_ready = { "stand ready", ai_stand, unit_stand };
static umove_t unit_move_death = { "death", NULL, unit_begin_decay };
/* The corpse holds its final death frame (AI_HOLD_FRAME) while the decay timer
 * counts down; the model has no separate decay sequence we can rely on. */
static umove_t unit_move_decay = { "decay", unit_decay_think, NULL };

void unit_decay1(LPEDICT self) {
    self->aiflags |= AI_HOLD_FRAME;
}

/* Death animation finished: hold the corpse pose and start the removal timer. */
void unit_begin_decay(LPEDICT self) {
    unit_setmove(self, &unit_move_decay);
    self->aiflags |= AI_HOLD_FRAME;
    self->wait = UNIT_DECAY_SECONDS;
}

/* Count down the corpse decay timer; remove the corpse when it elapses.  Dead
 * heroes are NOT removed — they persist as a revivable body (ReviveHero), as in
 * the original where heroes wait at the altar rather than decaying away. */
void unit_decay_think(LPEDICT self) {
    if (G_UnitIsHero(self)) {
        return;
    }
    unit_runwait(self, G_FreeEdict);
}

void unit_entercombat(LPEDICT self, LPEDICT target) {
    if (!self || !target || target == self || M_IsDead(self) || M_IsDead(target)) {
        return;
    }
    self->combatentity = target;
}

void unit_leavecombat(LPEDICT self) {
    if (self) {
        self->combatentity = NULL;
    }
}

BOOL unit_affectingcombat(LPEDICT self) {
    if (!self || M_IsDead(self)) {
        return false;
    }
    if (!self->combatentity ||
        !self->combatentity->inuse ||
        M_IsDead(self->combatentity)) {
        self->combatentity = NULL;
        return false;
    }
    return true;
}

void unit_stand(LPEDICT self) {
    if (self->holding_position) {
        unit_setmove(self, unit_affectingcombat(self)
            ? &holdpos_move_stand_ready
            : &holdpos_move_stand);
    } else {
        unit_setmove(self, unit_affectingcombat(self)
            ? &unit_move_stand_ready
            : &unit_move_stand);
    }
    self->build = NULL;
    self->s.renderfx &= ~RF_NO_UBERSPLAT;
    self->s.ability = 255;
    self->move_last_distance = 0;
    self->move_blocked_frames = 0;
    
}

void unit_die(LPEDICT self, LPEDICT attacker) {
    unit_leavecombat(self);
    unit_setmove(self, &unit_move_death);
    /* EVENT_UNIT_DEATH matches widget-specific death triggers
     * (TriggerRegisterDeathEvent/UnitEvent); EVENT_PLAYER_UNIT_DEATH fires the
     * owner's player-unit-death triggers (TriggerRegisterPlayerUnitEvent), e.g.
     * the mission win check that counts the player's dying naga. */
    G_PublishEvent(self, EVENT_UNIT_DEATH);
    G_PublishEvent(self, EVENT_PLAYER_UNIT_DEATH);
    self->svflags |= SVF_DEADMONSTER;
    /* Award experience to the killer's nearby heroes (enemy kills only). */
    if (attacker && attacker != self && attacker->s.player != self->s.player) {
        G_GrantKillXP(self, attacker);
    }
}

void unit_birth(LPEDICT self) {
    unit_setmove(self, &unit_move_birth);
    self->wait = UNIT_BUILD_TIME(self->class_id);
    self->s.renderfx |= RF_NO_UBERSPLAT;
}

static BOOL unit_smart_target_is_enemy(LPEDICT self, LPEDICT target) {
    if (!self || !target || target->s.player == self->s.player || target->s.player >= MAX_PLAYERS) {
        return false;
    }
    if (level.mapinfo) {
        playerType_t type = level.mapinfo->players[target->s.player].playerType;
        if (type == kPlayerTypeNone || type == kPlayerTypeNeutral) {
            return false;
        }
    }
    return true;
}

BOOL unit_issuetargetorder(LPEDICT self, LPCSTR order, LPEDICT target) {
    if (!self || !order || !target) {
        return false;
    }
    if (!strcmp(order, "smart")) {
        if (G_ActorHasSkill(self, "Ahar")) {
            if (G_ActorHasSkill(target, "Agld")) {
                harvest_gold_start(self, target);
                return true;
            }
            if (target->targtype == TARG_TREE) {
                harvest_start(self, target);
                return true;
            }
        }
        if (unit_smart_target_is_enemy(self, target)) {
            order_attack(self, target);
            return true;
        }
        return unit_issueorder(self, "move", &target->s.origin2);
    }
    if (!strcmp(order, "attack")) {
        order_attack(self, target);
        return true;
    }
    return false;
}

BOOL unit_issueorder(LPEDICT self, LPCSTR order, LPCVECTOR2 point) {
//    printf("%.4s %s\n", &self->class_id, order);
    if (!self || !order || !point) {
        return false;
    }
    if (!strcmp(order, "move") || !strcmp(order, "attack")) {
        VECTOR2 target = *point;
        CM_ClosestPathablePointForRadius(point, self->collision, &target);
        LPEDICT waypoint = Waypoint_add(&target);
        order_move(self, waypoint);
        return true;
    }
    return false;
}

BOOL unit_issueimmediateorder(LPEDICT self, LPCSTR order) {
//    printf("%.4s %s\n", &self->class_id, order);
    if (!self || !order) {
        return false;
    }
    if (!strcmp(order, "stop")) {
        order_stop(self);
        return true;
    }
    return false;
}

LPEDICT 
unit_createorfind(DWORD player,
                  DWORD unitid,
                  LPCVECTOR2 location,
                  FLOAT facing) 
{
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (ent->class_id == unitid &&
            Vector2_distance(location, &ent->s.origin2) < 10)
        {
            ent->s.player = player;
            ent->s.angle = facing * M_PI / 180;
            return ent;
        }
    }
    LPEDICT unit = SP_SpawnAtLocation(unitid, player, location);
//    printf("%.4s\n", &unit->class_id);
    if (!unit) {
        return NULL;
    }
    if (unit->stand) {
        unit->stand(unit);
    }
    unit->s.angle = facing * M_PI / 180;;
    return unit;
}

BOOL unit_additemtoslot(LPEDICT edict, LPEDICT item, DWORD i) {
    if (edict->inventory[i] == NULL) {
        edict->inventory[i] = item;
        return true;
    } else {
        return false;
    }
}

BOOL unit_additem(LPEDICT edict, LPEDICT item) {
    FOR_LOOP(i, MAX_INVENTORY) {
        if (unit_additemtoslot(edict, item, i)) {
            return true;
        }
    }
    return false;
}

static BOOL unit_status_stuns(DWORD code) {
    return code == MAKEFOURCC('B', 's', 't', 'u');
}

static BOOL unit_status_timedlife(DWORD code) {
    return code == MAKEFOURCC('B', 'T', 'L', 'F');
}

static void unit_refreshstatusflags(LPEDICT ent) {
    ent->stunned = false;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = ent->abilstatus + i;
        if (status->level && unit_status_stuns(status->code)) {
            ent->stunned = true;
        }
    }
}

void unit_updatestatuses(LPEDICT ent) {
    DWORD now = gi.GetTime();
    BOOL changed = false;
    BOOL kill = false;

    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = ent->abilstatus + i;
        if (!status->level || !status->timestamp) {
            continue;
        }
        if (now >= status->timestamp) {
            if (unit_status_timedlife(status->code)) {
                kill = true;
            }
            memset(status, 0, sizeof(*status));
            changed = true;
        }
    }
    if (changed) {
        unit_refreshstatusflags(ent);
    }
    if (kill && !M_IsDead(ent)) {
        ent->health.value = 0;
        if (ent->die) {
            ent->die(ent, ent->owner);
        }
    }
}

void unit_addtimedstatus(LPEDICT ent, LPCSTR skill, DWORD level, FLOAT duration) {
    DWORD code;
    DWORD now;
    heroabilitystatus_t *slot = NULL;

    if (!ent || !skill || !*skill || level == 0) {
        return;
    }

    code = *((DWORD const *)skill);
    now = gi.GetTime();
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = ent->abilstatus + i;
        if (status->level && status->code == code) {
            slot = status;
            break;
        }
        if (!status->level && !slot) {
            slot = status;
        }
    }
    if (!slot) {
        return;
    }

    slot->code = code;
    slot->level = level;
    slot->timestamp = duration > 0 ? now + (DWORD)(duration * 1000.0f) : 0;
    unit_refreshstatusflags(ent);
}

void unit_addstatus(LPEDICT ent, LPCSTR skill, DWORD level) {
    unit_addtimedstatus(ent, skill, level, 0);
}

void unit_learnability(LPEDICT ent, DWORD abilcode) {
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t *ha = ent->heroabilities+i;
        if (ha->level == 0) {
            ha->level = 1;
            ha->code = abilcode;
            return;
        } else if (ha->code == abilcode) {
            ha->level++;
            return;
        }
    }
}

/* WC3 hero attribute -> derived-stat bonuses.  Per-point constants are taken
 * exactly from UnitBalance.slk (consistent across every hero): +25 max HP per
 * Strength, +15 max mana per Intelligence, +0.3 armor per Agility.  The unit's
 * realHP/realM/realdef columns are precomputed at the hero's BASE attributes,
 * so we add the delta for the hero's current attributes.  Current HP/mana move
 * with the max (gaining Strength heals by the HP gained; losing attributes
 * cannot drop a living hero below 1 HP).  Non-heroes (no attributes) are a
 * no-op.  Call whenever a hero's str/agi/intel change. */
void G_RecomputeHeroStats(LPEDICT ent) {
    DWORD const cls = ent->class_id;
    LONG const baseStr = UNIT_STRENGTH(cls);
    LONG const baseAgi = UNIT_AGILITY(cls);
    LONG const baseInt = UNIT_INTELLIGENCE(cls);
    if (baseStr <= 0 && baseAgi <= 0 && baseInt <= 0) {
        return;
    }
    FLOAT const newMaxHP   = UNIT_HP(cls)           + ((LONG)ent->hero.str   - baseStr) * 25.0f;
    FLOAT const newMaxMana = UNIT_MANA_MAXIMUM(cls) + ((LONG)ent->hero.intel - baseInt) * 15.0f;
    FLOAT const newArmor   = UNIT_ARMOR_VALUE(cls)  + ((LONG)ent->hero.agi   - baseAgi) * 0.3f;

    BOOL const alive = ent->health.value > 0.0f;
    FLOAT const dHP = newMaxHP - ent->health.max_value;
    ent->health.max_value = MAX(1.0f, newMaxHP);
    ent->health.value = MIN(ent->health.max_value, ent->health.value + dHP);
    if (alive && ent->health.value < 1.0f) {
        ent->health.value = 1.0f;
    }

    FLOAT const dMana = newMaxMana - ent->mana.max_value;
    ent->mana.max_value = MAX(0.0f, newMaxMana);
    ent->mana.value = MAX(0.0f, MIN(ent->mana.max_value, ent->mana.value + dMana));

    ent->armor_value = newArmor;

    /* Primary attribute adds +1 attack damage per point (WC3 "green" bonus
     * damage = the hero's current primary-attribute value).  Primary is the
     * UnitBalance "Primary" column: STR/AGI/INT. */
    {
        LPCSTR const prim = UNIT_PRIMARY_ATTRIBUTE(cls);
        DWORD primVal = ent->hero.str;
        if (prim) {
            if (!strcmp(prim, "AGI")) primVal = ent->hero.agi;
            else if (!strcmp(prim, "INT")) primVal = ent->hero.intel;
        }
        ent->attack1.damageBase = UNIT_ATTACK1_DAMAGE_BASE(cls) + (FLOAT)primVal;
    }
}

/* ---- Hero experience / leveling (verified against WC3 1.29 binary) ----------
 * - Max level: Misc/MaxHeroLevel gameplay constant (default 10).
 * - XP to REACH level L: the "NeedHeroXP" table; the default WC3 values are
 *   100*(L*(L+1)/2 - 1) = 50*L*(L+1) - 100 (L1=0, L2=200, L3=500, L10=5400).
 * - Attributes are derived live from level, not stored per level-up: each
 *   primary attribute = base + trunc((level-1) * perLevelGain).  The product is
 *   TRUNCATED toward zero (the binary's attribute getter converts the float via
 *   a bare float->int, no rounding) — a (LONG) cast matches that exactly.
 * - SetHeroLevel works by granting enough XP to reach the level; XP is the
 *   source of truth and level only ever increases. */
DWORD G_MaxHeroLevel(void) {
    LPCSTR const v = FS_FindSheetCell(game.config.misc, "Misc", "MaxHeroLevel");
    DWORD const m = v ? (DWORD)atoi(v) : 0;
    return m > 0 ? m : 10;
}

DWORD G_HeroXPForLevel(DWORD level) {
    if (level <= 1) {
        return 0;
    }
    return 50 * level * (level + 1) - 100;
}

DWORD G_HeroLevelForXP(DWORD xp) {
    DWORD const maxLevel = G_MaxHeroLevel();
    DWORD level = 1;
    while (level < maxLevel && xp >= G_HeroXPForLevel(level + 1)) {
        level++;
    }
    return level;
}

/* Set a hero's level and derive its attributes + HP/mana/armor for that level. */
void G_HeroApplyLevel(LPEDICT ent, DWORD level) {
    DWORD const cls = ent->class_id;
    LONG const baseStr = UNIT_STRENGTH(cls);
    LONG const baseAgi = UNIT_AGILITY(cls);
    LONG const baseInt = UNIT_INTELLIGENCE(cls);
    if (baseStr <= 0 && baseAgi <= 0 && baseInt <= 0) {
        return; /* not a hero */
    }
    if (level < 1) level = 1;
    if (level > G_MaxHeroLevel()) level = G_MaxHeroLevel();

    FLOAT const steps = (FLOAT)(level - 1);
    ent->hero.level = level;
    ent->hero.str   = (DWORD)MAX(0, baseStr + (LONG)(steps * UNIT_STRENGTH_PER_LEVEL(cls)));
    ent->hero.agi   = (DWORD)MAX(0, baseAgi + (LONG)(steps * UNIT_AGILITY_PER_LEVEL(cls)));
    ent->hero.intel = (DWORD)MAX(0, baseInt + (LONG)(steps * UNIT_INTELLIGENCE_PER_LEVEL(cls)));
    G_RecomputeHeroStats(ent);
}

/* Update a hero's accumulated XP, leveling it up if a threshold was crossed. */
void G_HeroSetXP(LPEDICT ent, DWORD xp) {
    DWORD const oldLevel = ent->hero.level;
    ent->hero.xp = xp;
    DWORD const newLevel = G_HeroLevelForXP(xp);
    if (newLevel > oldLevel) {
        G_HeroApplyLevel(ent, newLevel);
        /* WC3 fires the hero level-up event once per level gained; campaign
         * triggers (TriggerRegisterPlayerUnitEvent, EVENT_PLAYER_HERO_LEVEL)
         * react to it and GetLevelingUnit() resolves to this hero. */
        for (DWORD lv = oldLevel + 1; lv <= newLevel; lv++) {
            G_PublishEvent(ent, EVENT_PLAYER_HERO_LEVEL);
        }
    }
}

/* --- XP-on-kill (data-driven from Units\MiscGame.txt) ------------------------
 * Constants read live from config.misc so map overrides stay 1:1; fallbacks are
 * the WC3 1.29 defaults: HeroExpRange=1200 (XP-share radius), GrantNormalXP=25 +
 * GrantNormalXPFormulaB=5/level (base XP by victim level), GrantHeroXP list
 * 100,120,160,220,300 (heroes), HeroFactorXP=80,70,60,50,0 (diminishing % when
 * the hero outlevels the victim by N), BuildingKillsGiveExp=0. */
static FLOAT G_MiscNum(LPCSTR key, FLOAT fallback) {
    LPCSTR const v = FS_FindSheetCell(game.config.misc, "Misc", key);
    return (v && *v) ? (FLOAT)atof(v) : fallback;
}

/* n-th (0-based) comma-separated entry of a Misc list, clamped to the last. */
static FLOAT G_MiscListNum(LPCSTR key, DWORD n, FLOAT fallback) {
    LPCSTR v = FS_FindSheetCell(game.config.misc, "Misc", key);
    if (!v || !*v) {
        return fallback;
    }
    FLOAT val = fallback;
    for (DWORD i = 0; ; i++) {
        val = (FLOAT)atof(v);
        LPCSTR const comma = strchr(v, ',');
        if (i >= n || !comma) {
            break;
        }
        v = comma + 1;
    }
    return val;
}

static BOOL G_UnitIsHero(LPCEDICT ent) {
    DWORD const cls = ent->class_id;
    return UNIT_STRENGTH(cls) > 0 || UNIT_AGILITY(cls) > 0 || UNIT_INTELLIGENCE(cls) > 0;
}

/* Award experience for killing `victim` to the killer's heroes within range,
 * applying the per-victim base XP and the level-difference diminishing returns. */
void G_GrantKillXP(LPEDICT victim, LPEDICT killer) {
    DWORD const vcls = victim->class_id;
    if (UNIT_IS_BUILDING(vcls) && G_MiscNum("BuildingKillsGiveExp", 0.0f) == 0.0f) {
        return;
    }
    BOOL const victimHero = G_UnitIsHero(victim);
    DWORD const victimLevel = victimHero ? (DWORD)MAX(1, (LONG)victim->hero.level)
                                         : (DWORD)MAX(1, UNIT_LEVEL(vcls));
    DWORD baseXP;
    if (victimHero) {
        baseXP = (DWORD)G_MiscListNum("GrantHeroXP", victimLevel - 1, 100.0f);
    } else {
        FLOAT const g0 = G_MiscNum("GrantNormalXP", 25.0f);
        FLOAT const gb = G_MiscNum("GrantNormalXPFormulaB", 5.0f);
        baseXP = (DWORD)(g0 + gb * (FLOAT)(victimLevel - 1));
    }
    FLOAT const range = G_MiscNum("HeroExpRange", 1200.0f);

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT h = &globals.edicts[i];
        if (!h->inuse || h->s.player != killer->s.player || h->health.value <= 0) {
            continue;
        }
        if (!G_UnitIsHero(h) ||
            Vector2_distance(&h->s.origin2, &victim->s.origin2) > range) {
            continue;
        }
        /* Diminishing returns: hero N levels above the victim earns
         * HeroFactorXP[N-1] percent (full XP when at or below the victim). */
        LONG const diff = (LONG)h->hero.level - (LONG)victimLevel;
        FLOAT factor = 1.0f;
        if (diff > 0) {
            factor = G_MiscListNum("HeroFactorXP", (DWORD)(diff - 1), 0.0f) / 100.0f;
        }
        DWORD const award = (DWORD)(baseXP * factor + 0.5f);
        if (award > 0) {
            G_HeroSetXP(h, h->hero.xp + award);
        }
    }
}

/* Scripted hero revival (ReviveHero native): bring a dead hero back to life at
 * (x,y) with HP/mana set from the MiscGame revive factors (defaults: full life,
 * no mana).  Dead heroes persist (unit_decay_think) so the edict is still valid. */
void G_ReviveHero(LPEDICT ent, FLOAT x, FLOAT y) {
    if (!ent) {
        return;
    }
    FLOAT const lifeFactor = G_MiscNum("HeroReviveLifeFactor", 1.0f);
    FLOAT const manaFactor = G_MiscNum("HeroReviveManaFactor", 0.0f);
    ent->svflags &= ~SVF_DEADMONSTER;
    ent->aiflags &= ~AI_HOLD_FRAME;
    ent->combatentity = NULL;
    ent->health.value = MAX(1.0f, ent->health.max_value * lifeFactor);
    ent->mana.value   = MAX(0.0f, ent->mana.max_value * manaFactor);
    ent->s.origin2.x = x;
    ent->s.origin2.y = y;
    unit_stand(ent); /* back to a living idle state */
}

void SP_monster_unit(LPEDICT self) {
    self->movetype = unit_movedistance(self) > 0 ? MOVETYPE_STEP : MOVETYPE_NONE;
    self->die = unit_die;
    self->stand = unit_stand;
    self->birth = unit_birth;
    
    unit_setmove(self, &unit_move_stand);
    
    monster_start(self);
}
