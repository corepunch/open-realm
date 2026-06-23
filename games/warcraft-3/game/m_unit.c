#include "g_local.h"

//void unit_die(LPEDICT self);
//void unit_decay2(LPEDICT self);
void unit_decay1(LPEDICT self);
void unit_cooldown(LPEDICT self);
void unit_stand(LPEDICT self);

void ai_birth2(LPEDICT self) {
    unit_runwait(self, unit_stand);
}

//static mmove_t unit_move_decay2 = { "Decay Bone", NULL, unit_die };
//static mmove_t unit_move_decay1 = { "Decay Flesh", NULL, unit_decay2 };
static umove_t unit_move_birth = { "birth", ai_birth, unit_stand };
static umove_t unit_move_stand = { "stand", ai_stand, unit_stand };
static umove_t unit_move_stand_ready = { "stand ready", ai_stand, unit_stand };
static umove_t unit_move_death = { "death", NULL, unit_decay1 };

//void unit_die(LPEDICT self) {
//}

//void unit_decay2(LPEDICT self) {
//    self->monsterinfo.currentmove = &unit_move_decay2;
//}

void unit_decay1(LPEDICT self) {
//    self->monsterinfo.currentmove = &unit_move_decay1;
    self->aiflags |= AI_HOLD_FRAME;
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
    unit_setmove(self, unit_affectingcombat(self)
        ? &unit_move_stand_ready
        : &unit_move_stand);
    self->build = NULL;
    self->s.renderfx &= ~RF_NO_UBERSPLAT;
    self->s.ability = 0;
    self->move_last_distance = 0;
    self->move_blocked_frames = 0;
    
}

void unit_die(LPEDICT self, LPEDICT attacker) {
    unit_leavecombat(self);
    unit_setmove(self, &unit_move_death);
    G_PublishEvent(self, EVENT_UNIT_DEATH);
    self->svflags |= SVF_DEADMONSTER;
    if (self->sound_death) { self->s.event = EV_DEATH; self->s.sound = self->sound_death; }
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

void SP_monster_unit(LPEDICT self) {
    self->movetype = unit_movedistance(self) > 0 ? MOVETYPE_STEP : MOVETYPE_NONE;
    self->die = unit_die;
    self->stand = unit_stand;
    self->birth = unit_birth;
    
    unit_setmove(self, &unit_move_stand);
    
    monster_start(self);
}
