#include "g_local.h"

//void unit_die(LPEDICT self);
//void unit_decay2(LPEDICT self);
void unit_decay1(LPEDICT self);
void unit_begin_decay(LPEDICT self);
void unit_decay_think(LPEDICT self);
void unit_cooldown(LPEDICT self);
void unit_stand(LPEDICT self);
BOOL G_UnitIsHero(LPCEDICT ent);

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

static void hero_become_revivable(LPEDICT self) {
    LPGAMECLIENT owner;

    if (!self || !self->inuse || !G_UnitIsHero(self) ||
        !(self->svflags & SVF_DEADMONSTER)) return;
    self->revival.awaiting = true;
    self->revival.reviving = false;
    self->s.renderfx |= RF_HIDDEN;
    G_PublishEvent(self, EVENT_PLAYER_HERO_REVIVABLE);
    G_PublishEvent(self, EVENT_UNIT_HERO_REVIVABLE);
    owner = G_GetPlayerClientByNumber(self->s.player);
    if (owner && owner->ps.number == self->s.player) G_InvalidateCommands(owner);
}

/* Death animation finished: hold the corpse pose and start the removal timer. */
void unit_begin_decay(LPEDICT self) {
    unit_setmove(self, &unit_move_decay);
    self->aiflags |= AI_HOLD_FRAME;
    self->wait = G_UnitIsHero(self) && game.constants.dissipateTime > 0.0f
        ? game.constants.dissipateTime
        : UNIT_DECAY_SECONDS;
}

/* Ordinary corpses are removed. Heroes instead finish their dissipation timer,
 * become hidden/awaiting-revive, and keep the same authoritative edict. */
void unit_decay_think(LPEDICT self) {
    if (G_UnitIsHero(self)) {
        if (!self->revival.awaiting) unit_runwait(self, hero_become_revivable);
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
    if (self->movement.holding_position) {
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
    self->movement.last_distance = 0;
    self->movement.blocked_frames = 0;
    
}

void unit_die(LPEDICT self, LPEDICT attacker) {
    LPGAMECLIENT owner;

    if (self->training) G_ClearTrainingQueueFood(self);
    else { G_CancelHeroRevives(self); G_CancelTrainingQueue(self, true); }
    G_ClearUnitFood(self);
    if (G_UnitIsHero(self)) {
        self->revival.awaiting = false;
        self->revival.reviving = false;
        self->revival.producer = NULL;
        self->revival.queue_next = NULL;
        self->revival.player = 0;
        self->revival.gold = self->revival.lumber = 0;
        self->revival.progress = 0.0f;
    }
    unit_leavecombat(self);
    unit_setmove(self, &unit_move_death);
    if (self->sound.death) {
        self->sound.world_pending = self->sound.death;
        self->sound.world_pending_event = EV_DEATH;
    }
    /* Destroying a transport ejects its passengers at the wreck. */
    if (self->cargo.count > 0) {
        cargo_drop_all(self);
    }
    /* EVENT_UNIT_DEATH matches widget-specific death triggers
     * (TriggerRegisterDeathEvent/UnitEvent); EVENT_PLAYER_UNIT_DEATH fires the
     * owner's player-unit-death triggers (TriggerRegisterPlayerUnitEvent), e.g.
     * the mission win check that counts the player's dying naga. */
    G_PublishEventWithSource(self, EVENT_UNIT_DEATH, attacker);
    G_PublishEventWithSource(self, EVENT_PLAYER_UNIT_DEATH, attacker);
    self->svflags |= SVF_DEADMONSTER;
    /* A dead producer cannot retain ownership of a revival.  This clears each
     * Hero's reviving flag and refunds what this Altar charged. */
    G_CancelHeroRevives(self);
    if (self->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    /* Award experience to the killer's nearby heroes (enemy kills only). */
    if (attacker && attacker != self && attacker->s.player != self->s.player) {
        G_GrantKillXP(self, attacker);
    }
    owner = G_GetPlayerClientByNumber(self->s.player);
    if (owner && owner->ps.number == self->s.player) G_InvalidateCommands(owner);
}

void unit_birth(LPEDICT self) {
    unit_setmove(self, &unit_move_birth);
    self->wait = self->UnitBalance->buildTime;
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
    if (S_GoldMineWorkerIsInside(self))
        return false;
    if (!strcmp(order, "smart")) {
        if (G_IsItem(target)) {
            return G_OrderPickupItem(self, target);
        }
        if (G_ActorHasSkill(self, "Ahar")) {
            if (S_GoldMineIsMine(target)) {
                return harvest_gold_order(self, target);
            }
            if (target->targtype == TARG_TREE) {
                harvest_start(self, target);
                return true;
            }
            if (self->harvested_lumber > 0 && harvest_lumber_return_to(self, target))
                return true;
            if (self->harvested_gold > 0 && harvest_gold_return_to(self, target))
                return true;
        }
        /* Neutral crates are not enemy units, but they are valid normal-attack
         * targets.  Trees keep the harvest behavior above for workers. */
        if (G_IsDestructable(target)) {
            if (!G_DestructableIsAttackable(target)) {
                return false;
            }
            order_attack(self, target);
            return true;
        }
        if (unit_smart_target_is_enemy(self, target)) {
            order_attack(self, target);
            return true;
        }
        if (S_RepairSmart(self, target)) {
            return true;
        }
        return unit_issueorder(self, "move", &target->s.origin2);
    }
    if (!strcmp(order, "attack")) {
        if (G_IsDestructable(target) && !G_DestructableIsAttackable(target)) {
            return false;
        }
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
    if (S_GoldMineWorkerIsInside(self))
        return false;
    if (self->aiflags & AI_IMMOBILE)
        return false;
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
    if (S_GoldMineWorkerIsInside(self))
        return false;
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
            G_SetUnitPlayer(ent, player);
            ent->s.angle = facing * M_PI / 180;
            G_ActivateUnitFood(ent);
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
    G_ActivateUnitFood(unit);
    return unit;
}

BOOL unit_additemtoslot(LPEDICT edict, LPEDICT item, DWORD i) {
    return G_AddItemToSlot(edict, item, i);
}

BOOL unit_additem(LPEDICT edict, LPEDICT item) {
    return G_PickupItem(edict, item);
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
    LPCSTR stacktype;

    if (!ent || !skill || !*skill || level == 0) {
        return;
    }

    code = *((DWORD const *)skill);
    now = gi.GetTime();
    stacktype = S_SpellString(code, "BuffStackType", 0);

    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = ent->abilstatus + i;
        if (status->level && status->code == code) {
            /* Existing buff of same code found — apply stacking rule. */
            if (stacktype && !strcmp(stacktype, "Stack")) {
                status->level += level;
                if (duration > 0) {
                    status->timestamp = now + (DWORD)(duration * 1000.0f);
                }
            } else if (stacktype && !strcmp(stacktype, "Refresh")) {
                if (duration > 0) {
                    status->timestamp = now + (DWORD)(duration * 1000.0f);
                }
            } else {
                /* "Replace" (default): overwrite level and timestamp. */
                status->level = level;
                if (duration > 0) {
                    status->timestamp = now + (DWORD)(duration * 1000.0f);
                } else {
                    status->timestamp = 0;
                }
            }
            unit_refreshstatusflags(ent);
            return;
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

static heroability_t *G_FindRuntimeAbility(LPEDICT ent, DWORD abilcode) {
    if (!ent || !abilcode) {
        return NULL;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t *ha = ent->heroabilities + i;
        if (ha->level && ha->code == abilcode) {
            return ha;
        }
    }
    return NULL;
}

static BOOL G_FourCCListContains(LPCSTR list, DWORD code) {
    LPCSTR cursor = list;

    if (!list || !code) {
        return false;
    }
    while (*cursor) {
        LPCSTR start;
        LPCSTR end;
        DWORD item = 0;

        while (*cursor == ',' || isspace((unsigned char)*cursor)) cursor++;
        if (!*cursor) break;
        start = cursor;
        while (*cursor && *cursor != ',') cursor++;
        end = cursor;
        while (end > start && isspace((unsigned char)end[-1])) end--;
        if ((size_t)(end - start) == sizeof(item)) {
            memcpy(&item, start, sizeof(item));
            if (item == code) return true;
        }
        if (*cursor == ',') cursor++;
    }
    return false;
}

static DWORD G_HeroSkillLevel(LPCEDICT ent, DWORD abilcode) {
    if (!ent || !abilcode) {
        return 0;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t const *ha = ent->heroabilities + i;
        if (ha->level && ha->code == abilcode) {
            return ha->level;
        }
    }
    return 0;
}

void G_HeroInitializeProgression(LPEDICT ent) {
    DWORD spent_points = 0;

    if (!ent) {
        return;
    }
    if (ent->hero.level == 0) {
        ent->hero.level = 1;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        spent_points += ent->heroabilities[i].level;
    }
    if (!ent->hero.skillpoints && ent->hero.level > spent_points) {
        ent->hero.skillpoints = ent->hero.level - spent_points;
    }
}

DWORD G_UnitAbilityLevel(LPCEDICT ent, DWORD abilcode) {
    DWORD const hero_level = G_HeroSkillLevel(ent, abilcode);
    if (hero_level) {
        return hero_level;
    }
    if (ent && ent->UnitAbilities && G_FourCCListContains(ent->UnitAbilities->abilList, abilcode)) {
        return 1;
    }
    return 0;
}

void unit_learnability(LPEDICT ent, DWORD abilcode) {
    heroability_t *existing = G_FindRuntimeAbility(ent, abilcode);
    if (existing) {
        existing->level++;
        return;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t *ha = ent->heroabilities + i;
        if (ha->level == 0) {
            ha->level = 1;
            ha->code = abilcode;
            return;
        }
    }
}

static DWORD G_HeroAbilityLevelSkip(void) {
    LPCSTR const value = Stb_IniCacheFind(&game.config.misc, "Misc", "HeroAbilityLevelSkip");
    DWORD const skip = value ? (DWORD)atoi(value) : 0;
    return skip > 0 ? skip : 2;
}

BOOL G_HeroHasCandidateSkill(LPCEDICT ent, DWORD abilcode) {
    if (!ent || !G_UnitIsHero(ent) || !ent->UnitAbilities || !abilcode) {
        return false;
    }
    return G_FourCCListContains(ent->UnitAbilities->heroAbilList, abilcode);
}

DWORD G_HeroSkillRequiredLevel(LPEDICT ent, DWORD abilcode) {
    AbilityData_t const *ability = G_AbilityData(abilcode);
    DWORD const current = G_HeroSkillLevel(ent, abilcode);
    DWORD const base = ability->reqLevel > 0 ? (DWORD)ability->reqLevel : 1;
    DWORD const skip = ability->levelSkip > 0 ? (DWORD)ability->levelSkip : G_HeroAbilityLevelSkip();
    return base + current * skip;
}

heroSkillState_t G_HeroSkillState(LPEDICT ent, DWORD abilcode, DWORD *next_level, DWORD *required_level) {
    AbilityData_t const *ability;
    DWORD current;
    DWORD required;

    if (next_level) *next_level = 0;
    if (required_level) *required_level = 0;
    if (!G_HeroHasCandidateSkill(ent, abilcode)) {
        return HERO_SKILL_ABSENT;
    }

    ability = G_AbilityData(abilcode);
    if (!ability->id || ability->levels <= 0) {
        return HERO_SKILL_ABSENT;
    }
    current = G_HeroSkillLevel(ent, abilcode);
    if (next_level) *next_level = current + 1;
    if (current >= (DWORD)ability->levels) {
        return HERO_SKILL_MAXED;
    }
    if (!ent->hero.skillpoints) {
        return HERO_SKILL_NO_POINTS;
    }

    required = G_HeroSkillRequiredLevel(ent, abilcode);
    if (required_level) *required_level = required;
    if (ent->hero.level < required) {
        return HERO_SKILL_LEVEL_LOCKED;
    }
    return HERO_SKILL_AVAILABLE;
}

BOOL G_HeroLearnSkill(LPEDICT ent, DWORD abilcode) {
    DWORD const old_level = G_HeroSkillLevel(ent, abilcode);

    if (G_HeroSkillState(ent, abilcode, NULL, NULL) != HERO_SKILL_AVAILABLE) {
        return false;
    }
    unit_learnability(ent, abilcode);
    if (G_HeroSkillLevel(ent, abilcode) != old_level + 1) {
        return false;
    }
    ent->hero.skillpoints--;
    return true;
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
    UnitBalance_t const *balance = ent->UnitBalance;
    LONG const baseStr = balance->strength;
    LONG const baseAgi = balance->agility;
    LONG const baseInt = balance->intelligence;
    if (baseStr <= 0 && baseAgi <= 0 && baseInt <= 0) {
        return;
    }
    FLOAT const newMaxHP = balance->maxHealth + ((LONG)ent->hero.str - baseStr) * 25.0f;
    FLOAT const newMaxMana = balance->maxMana + ((LONG)ent->hero.intel - baseInt) * 15.0f;
    FLOAT const newArmor = balance->armor + ((LONG)ent->hero.agi - baseAgi) * 0.3f;

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
        LPCSTR const prim = balance->primaryAttribute;
        DWORD primVal = ent->hero.str;
        if (prim) {
            if (!strcmp(prim, "AGI")) primVal = ent->hero.agi;
            else if (!strcmp(prim, "INT")) primVal = ent->hero.intel;
        }
        ent->attack1.damageBase = ent->UnitWeapons->attack1.damageBase + (FLOAT)primVal;
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
    LPCSTR const v = Stb_IniCacheFind(&game.config.misc, "Misc", "MaxHeroLevel");
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
    UnitBalance_t const *balance = ent->UnitBalance;
    LONG const baseStr = balance->strength;
    LONG const baseAgi = balance->agility;
    LONG const baseInt = balance->intelligence;
    if (baseStr <= 0 && baseAgi <= 0 && baseInt <= 0) {
        return; /* not a hero */
    }
    if (level < 1) level = 1;
    if (level > G_MaxHeroLevel()) level = G_MaxHeroLevel();

    FLOAT const steps = (FLOAT)(level - 1);
    ent->hero.level = level;
    ent->hero.str = (DWORD)MAX(0, baseStr + (LONG)(steps * balance->strengthPerLevel));
    ent->hero.agi = (DWORD)MAX(0, baseAgi + (LONG)(steps * balance->agilityPerLevel));
    ent->hero.intel = (DWORD)MAX(0, baseInt + (LONG)(steps * balance->intelligencePerLevel));
    G_RecomputeHeroStats(ent);
}

/* Update a hero's accumulated XP, leveling it up if a threshold was crossed. */
void G_HeroSetXP(LPEDICT ent, DWORD xp) {
    DWORD const oldLevel = ent->hero.level;
    ent->hero.xp = xp;
    DWORD const newLevel = G_HeroLevelForXP(xp);
    if (newLevel > oldLevel) {
        /* WC3 fires the hero level-up event once per level gained; campaign
         * triggers (TriggerRegisterPlayerUnitEvent, EVENT_PLAYER_HERO_LEVEL)
         * react to it and GetLevelingUnit() resolves to this hero. */
        for (DWORD lv = oldLevel + 1; lv <= newLevel; lv++) {
            G_HeroApplyLevel(ent, lv);
            ent->hero.skillpoints++;
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
    LPCSTR const v = Stb_IniCacheFind(&game.config.misc, "Misc", key);
    return (v && *v) ? (FLOAT)atof(v) : fallback;
}

/* n-th (0-based) comma-separated entry of a Misc list, clamped to the last. */
static FLOAT G_MiscListNum(LPCSTR key, DWORD n, FLOAT fallback) {
    LPCSTR v = Stb_IniCacheFind(&game.config.misc, "Misc", key);
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

BOOL G_UnitIsHero(LPCEDICT ent) {
    return ent->UnitBalance->strength > 0 || ent->UnitBalance->agility > 0 || ent->UnitBalance->intelligence > 0;
}

/* Award experience for killing `victim` to the killer's heroes within range,
 * applying the per-victim base XP and the level-difference diminishing returns. */
void G_GrantKillXP(LPEDICT victim, LPEDICT killer) {
    DWORD const vcls = victim->class_id;
    if (G_UnitIsBuilding(vcls) && G_MiscNum("BuildingKillsGiveExp", 0.0f) == 0.0f) {
        return;
    }
    BOOL const victimHero = G_UnitIsHero(victim);
    DWORD const victimLevel = victimHero ? (DWORD)MAX(1, (LONG)victim->hero.level)
                                         : (DWORD)MAX(1, victim->UnitBalance->level);
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
    FLOAT mana;

    if (!ent) {
        return;
    }
    if (ent->revival.reviving) G_CancelHeroRevive(ent->revival.producer, ent);
    FLOAT const lifeFactor = G_MiscNum("HeroReviveLifeFactor", 1.0f);
    FLOAT const manaFactor = G_MiscNum("HeroReviveManaFactor", 0.0f);
    FLOAT const manaStart = G_MiscNum("HeroReviveManaStart", 0.0f);
    ent->svflags &= ~SVF_DEADMONSTER;
    ent->aiflags &= ~AI_HOLD_FRAME;
    ent->combatentity = NULL;
    ent->revival.awaiting = false;
    ent->revival.reviving = false;
    ent->revival.producer = NULL;
    ent->revival.queue_next = NULL;
    ent->revival.player = 0;
    ent->revival.gold = ent->revival.lumber = 0;
    ent->revival.progress = 0.0f;
    ent->s.renderfx &= ~RF_HIDDEN;
    ent->health.value = MIN(ent->health.max_value, MAX(1.0f, ent->health.max_value * lifeFactor));
    mana = ent->mana.max_value * manaFactor;
    if (ent->UnitBalance) mana += ent->UnitBalance->initialMana * manaStart;
    ent->mana.value = MAX(0.0f, MIN(ent->mana.max_value, mana));
    ent->s.origin2.x = x;
    ent->s.origin2.y = y;
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    G_ActivateUnitFood(ent);
    unit_stand(ent); /* back to a living idle state */
    gi.LinkEntity(ent);
}

void SP_monster_unit(LPEDICT self) {
    self->movetype = unit_movedistance(self) > 0 ? MOVETYPE_STEP : MOVETYPE_NONE;
    self->die = unit_die;
    self->stand = unit_stand;
    self->birth = unit_birth;
    
    unit_setmove(self, &unit_move_stand);
    S_GoldMineInitUnit(self);
    monster_start(self);
}
