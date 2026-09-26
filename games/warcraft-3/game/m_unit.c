#include "g_local.h"

//void unit_die(LPEDICT self);
//void unit_decay2(LPEDICT self);
void unit_decay1(LPEDICT self);
void unit_begin_decay(LPEDICT self);
void unit_decay_think(LPEDICT self);
void unit_cooldown(LPEDICT self);
void unit_stand(LPEDICT self);
bool G_UnitIsHero(LPCEDICT ent);

/* WC3 corpse lifetime: DecayTime (flesh, 2s) + BoneDecayTime (bone, 88s) = 90s
 * after the death animation, then the corpse is removed (MiscData.txt). */
#define UNIT_DEATH_TYPE_RAISE (1 << 0)
#define UNIT_DEATH_TYPE_DECAY (1 << 1)

void ai_birth2(LPEDICT self) {
    unit_runwait(self, unit_stand);
}

//static mmove_t unit_move_decay2 = { "Decay Bone", NULL, unit_die };
//static mmove_t unit_move_decay1 = { "Decay Flesh", NULL, unit_decay2 };
static umove_t unit_move_birth = { "birth", ai_birth, unit_stand };
static umove_t unit_move_stand = { "stand", ai_stand, unit_stand };
static umove_t unit_move_stand_ready = { "stand ready", ai_stand, unit_stand };
static void unit_decay_flesh_think(LPEDICT self);
static void unit_begin_bone_decay(LPEDICT self);
static float unit_decay_flesh_duration(LPCEDICT self);
static float unit_decay_bone_duration(LPCEDICT self);
static float unit_dissipate_duration(LPCEDICT self);
static umove_t unit_move_death = { "death", NULL, unit_begin_decay };
/* Simulation owns corpse lifetime. Presentation stretches the selected Decay
 * sequence across the matching gameplay phase, just as Warsmash does. */
static umove_t unit_move_decay_flesh = { "decay flesh", unit_decay_flesh_think, NULL, NULL, unit_decay_flesh_duration };
static umove_t unit_move_decay_bones = { "decay bone", unit_decay_think, NULL, NULL, unit_decay_bone_duration };
static umove_t unit_move_dissipate = { "dissipate", unit_decay_think, NULL, NULL, unit_dissipate_duration };
static umove_t unit_move_decay_remove = { "death", unit_decay_think, NULL };

void unit_decay1(LPEDICT self) {
    self->aiflags |= AI_HOLD_FRAME;
}

static void hero_become_revivable(LPEDICT self) {
    LPGAMECLIENT owner;

    if (!self || !self->inuse || !G_UnitIsHero(self) ||
        (self->aiflags & AI_ILLUSION) || !(self->svflags & SVF_DEADMONSTER)) return;
    self->revival.awaiting = true;
    self->revival.reviving = false;
    self->s.renderfx |= RF_HIDDEN;
    G_PublishEvent(self, EVENT_PLAYER_HERO_REVIVABLE);
    G_PublishEvent(self, EVENT_UNIT_HERO_REVIVABLE);
    owner = G_GetPlayerClientByNumber(self->s.player);
    if (owner && owner->ps.number == self->s.player) G_InvalidateCommands(owner);
}

static float unit_decay_wait(float seconds) {
    /* unit_runwait() deliberately ignores zero, so a zero-authored phase must
     * still advance on the next simulation frame rather than becoming immortal. */
    return MAX(seconds, FRAMETIME / 1000.0f);
}

static float unit_decay_flesh_duration(LPCEDICT self) {
    (void)self;
    return unit_decay_wait(game.constants.decayTime);
}

static float unit_decay_bone_duration(LPCEDICT self) {
    if (self && G_UnitIsBuilding(self->class_id))
        return unit_decay_wait(game.constants.structureDecayTime);
    return unit_decay_wait(game.constants.boneDecayTime);
}

static float unit_dissipate_duration(LPCEDICT self) {
    (void)self;
    return unit_decay_wait(game.constants.dissipateTime);
}

static void unit_set_decay_move(LPEDICT self, umove_t *move) {
    unit_setmove(self, move);
    /* A missing exact secondary sequence may fall back to another sequence in
     * the same primary family. If the model has no usable family at all, keep
     * the final Death frame while the authoritative timer continues. */
    if (self->animation) self->aiflags &= ~AI_HOLD_FRAME;
    else self->aiflags |= AI_HOLD_FRAME;
}

static void unit_begin_bone_decay(LPEDICT self) {
    unit_set_decay_move(self, &unit_move_decay_bones);
    self->wait = unit_decay_wait(game.constants.boneDecayTime);
}

/* Retail cargo preserves corpses indefinitely while stored, but dropping a
 * corpse restarts the bone-decay countdown rather than resuming the old
 * remainder.  Keep this phase-specific: the exact flesh-phase cargo behavior
 * is not established by the available retail evidence. */
void G_RestartCorpseBoneDecayAfterCargo(LPEDICT corpse) {
    if (!corpse || !corpse->inuse || corpse->currentmove != &unit_move_decay_bones ||
        G_UnitIsHero(corpse) || G_UnitIsBuilding(corpse->class_id)) return;
    corpse->wait = unit_decay_wait(game.constants.boneDecayTime);
}

static void unit_decay_flesh_think(LPEDICT self) {
    /* An active corpse consumer owns the remains.  Freeze ordinary decay until
     * that reservation is released or, for Cannibalize, the corpse is consumed. */
    if (self->aiflags & (AI_CORPSE_RESERVED | AI_CORPSE_IN_CARGO)) return;
    unit_runwait(self, unit_begin_bone_decay);
}

/* Death animation finished.  UnitData.deathType is authoritative: bit 1 says
 * the remains decay at all.  Ordinary corpses then use the map's separate
 * flesh and bone constants; structures use StructureDecayTime; Heroes retain
 * their distinct dissipation/revival lifecycle. */
void unit_begin_decay(LPEDICT self) {
    UnitData_t const *data = self && self->data.UnitData ? self->data.UnitData :
        (self ? G_UnitData(self->class_id) : NULL);
    bool const hero = G_UnitIsHero(self) && !(self->aiflags & AI_ILLUSION);
    bool const no_decay = !hero &&
        ((self->aiflags & AI_CORPSE_NO_DECAY) || !data || !(data->deathType & UNIT_DEATH_TYPE_DECAY));

    if (hero) {
        unit_set_decay_move(self, &unit_move_dissipate);
        self->wait = unit_decay_wait(game.constants.dissipateTime);
        return;
    }
    if (no_decay) {
        unit_setmove(self, &unit_move_decay_remove);
        self->aiflags |= AI_HOLD_FRAME;
        self->wait = FRAMETIME / 1000.0f;
        return;
    }
    if (G_UnitIsBuilding(self->class_id)) {
        unit_set_decay_move(self, &unit_move_decay_bones);
        self->wait = unit_decay_wait(game.constants.structureDecayTime);
        return;
    }
    unit_set_decay_move(self, &unit_move_decay_flesh);
    self->wait = unit_decay_wait(game.constants.decayTime);
}

/* Ordinary corpses are removed. Heroes instead finish their dissipation timer,
 * become hidden/awaiting-revive, and keep the same authoritative edict. */
void unit_decay_think(LPEDICT self) {
    if (self->aiflags & (AI_CORPSE_RESERVED | AI_CORPSE_IN_CARGO)) return;
    if (G_UnitIsHero(self) && !(self->aiflags & AI_ILLUSION)) {
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

bool unit_affectingcombat(LPEDICT self) {
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
    /* Reaching stand is the common completion edge for Move, direct Attack,
     * Repair, Harvest, and several cast behaviors. Retire transient state first,
     * then let a pending Shift order become authoritative before installing the
     * idle/default stand behavior. */
    self->build = NULL;
    self->s.renderfx &= ~RF_NO_UBERSPLAT;
    self->s.ability = 255;
    self->movement.last_distance = 0;
    self->movement.blocked_frames = 0;
    if (G_UnitStartNextQueuedOrder(self)) {
        return;
    }
    if (self->movement.holding_position) {
        unit_setmove(self, unit_affectingcombat(self)
            ? &holdpos_move_stand_ready
            : &holdpos_move_stand);
    } else {
        unit_setmove(self, unit_affectingcombat(self)
            ? &unit_move_stand_ready
            : &unit_move_stand);
    }
}

/* All runtime unit-health changes pass here so intrinsic ability levels transition exactly once. */
void G_SetHealth(LPEDICT ent, float value) {
    uint8_t const old = compress_stat(&ent->health);
    float const old_value = ent->health.value;
    uint8_t next;
    ent->health.value = value;
    next = compress_stat(&ent->health);
    if (old_value != value) FOR_EACH_EVENT(evt) {
        if (evt->type == EVENT_GAME_STATE_LIMIT && G_EventSubjectIsCurrent(evt) && evt->subject == ent && evt->state == WC3_UNIT_STATE_LIFE &&
            !G_LimitMatches(evt->limitop, old_value, evt->limitval) &&
             G_LimitMatches(evt->limitop, value, evt->limitval))
            G_PublishEventResponse(ent, EVENT_GAME_STATE_LIMIT, evt);
    }
    if ((ent->s.flags & EF_BUILDING) && (old != next || value <= 0.0f))
        S_RefreshAbilityLevel(ent, FindAbilityByClassname("Afih"));
}

void G_AddHealth(LPEDICT ent, float value) { G_SetHealth(ent, MIN(ent->health.max_value, ent->health.value + value)); }

static void unit_apply_health_cap_delta(LPEDICT ent, float amount, float *ledger) {
    float old_max, fraction;
    if (!ent || amount == 0.0f) return;
    old_max = MAX(1.0f, ent->health.max_value);
    fraction = ent->health.value / old_max;
    *ledger += amount;
    ent->health.max_value = MAX(1.0f, ent->health.max_value + amount);
    G_SetHealth(ent, ent->health.max_value * fraction);
    G_InvalidateUnitInfoPanel(ent);
}

void G_ApplyPermanentMaxHealthBonus(LPEDICT ent, float amount) {
    if (ent) unit_apply_health_cap_delta(ent, amount, &ent->permanent_health_bonus);
}

void G_ApplyTemporaryMaxHealthBonus(LPEDICT ent, float amount) {
    if (ent) unit_apply_health_cap_delta(ent, amount, &ent->temporary_health_bonus);
}

void G_ApplyTemporaryMaxManaBonus(LPEDICT ent, float amount) {
    float old_max, fraction;
    if (!ent || amount == 0.0f) return;
    old_max = MAX(1.0f, ent->mana.max_value);
    fraction = ent->mana.value / old_max;
    ent->temporary_mana_bonus += amount;
    ent->mana.max_value = MAX(0.0f, ent->mana.max_value + amount);
    ent->mana.value = ent->mana.max_value * fraction;
    G_InvalidateUnitInfoPanel(ent);
}

static void unit_apply_armor_delta(LPEDICT ent, float amount, float *ledger) {
    if (!ent || amount == 0.0f) return;
    *ledger += amount;
    ent->armor_value += amount;
    G_InvalidateUnitInfoPanel(ent);
}

void G_ApplyPermanentArmorBonus(LPEDICT ent, float amount) {
    if (ent) unit_apply_armor_delta(ent, amount, &ent->permanent_armor_bonus);
}

void G_ApplyTemporaryArmorBonus(LPEDICT ent, float amount) {
    if (ent) unit_apply_armor_delta(ent, amount, &ent->temporary_armor_bonus);
}

void G_ApplyPermanentAttackDamageBonus(LPEDICT ent, float amount) {
    if (!ent || amount == 0.0f) return;
    if (ent->attack1.numberOfDice) {
        ent->attack1.permanentDamageBonus += amount;
        ent->attack1.damageBase = (uint32_t)MAX(0, (int32_t)ent->attack1.damageBase + (int32_t)amount);
    }
    if (ent->attack2.numberOfDice) {
        ent->attack2.permanentDamageBonus += amount;
        ent->attack2.damageBase = (uint32_t)MAX(0, (int32_t)ent->attack2.damageBase + (int32_t)amount);
    }
    G_InvalidateUnitInfoPanel(ent);
}

void G_ApplyTemporaryAttackDamageBonus(LPEDICT ent, float amount) {
    if (!ent || amount == 0.0f) return;
    ent->attack1.temporaryDamageBonus += amount;
    ent->attack2.temporaryDamageBonus += amount;
    G_InvalidateUnitInfoPanel(ent);
}

static bool unit_is_raisable_corpse(LPCEDICT ent, bool stored) {
    UnitData_t const *data;
    if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER) ||
        !(ent->svflags & SVF_DEADMONSTER) || !M_IsDead(ent) ||
        (ent->aiflags & (AI_CORPSE_UNRAISABLE | AI_CORPSE_RESERVED))) return false;
    if (!!(ent->aiflags & AI_CORPSE_IN_CARGO) != stored) return false;
    data = ent->data.UnitData ? ent->data.UnitData : G_UnitData(ent->class_id);
    return data && (data->deathType & UNIT_DEATH_TYPE_RAISE) != 0;
}

bool G_UnitIsRaisableCorpse(LPCEDICT ent) { return unit_is_raisable_corpse(ent, false); }
bool G_UnitIsRaisableStoredCorpse(LPCEDICT ent) { return unit_is_raisable_corpse(ent, true); }

/* Ordinary corpse revival keeps handle identity while retiring every death-state owner before returning to idle. */
void G_ReviveCorpse(LPEDICT ent, float life_fraction) {
    ent->svflags &= ~SVF_DEADMONSTER; ent->s.flags &= ~EF_NOT_SELECTABLE;
    ent->aiflags &= ~AI_HOLD_FRAME; ent->s.renderfx &= ~RF_HIDDEN;
    ent->combatentity = ent->goalentity = ent->secondarygoal = NULL;
    ent->wait = 0; G_ClearUnitOrderQueue(ent);
    ent->aiflags &= ~(AI_CORPSE_UNRAISABLE | AI_CORPSE_NO_DECAY | AI_CORPSE_RESERVED | AI_CORPSE_IN_CARGO);
    G_SetHealth(ent, ent->health.max_value * MAX(0.0f, MIN(1.0f, life_fraction)));
    G_ActivateUnitFood(ent); unit_stand(ent); gi.LinkEntity(ent);
}

void unit_die(LPEDICT self, LPEDICT attacker) {
    LPGAMECLIENT owner;
    uint32_t selected_mask;

    if (!self || (self->svflags & SVF_DEADMONSTER)) return;
    selected_mask = self->selected;

    S_AvatarExpire(self);
    /* A dead polymorphed unit must not later restore as a living unit when its
     * timed buff expires.  Keep the death presentation chosen at the time of
     * death, but retire the reversible morph contract immediately. */
    if (self->polymorph.active) self->polymorph.active = false;
    G_ClearUnitOrderQueue(self);
    G_InvalidateUnitShortcutsForUnit(self);
    G_SetHealth(self, 0.0f);
    /* Marks belong to their applying abilities, even when another unit lands the killing blow. */
    unit_statusdeath(self);
    /* Construction owns Repair workers and a self-linked HUD queue marker.
     * Tear that state down before generic production/revival death cleanup.
     * A structure upgrade is the same edict rather than a queued child; death
     * abandons it without the player-cancel refund. */
    if (G_BuildingUpgradeActive(self)) G_StopBuildingUpgrade(self, false);
    if (self->construction.active) G_StopConstruction(self);
    else if (self->build == self) {
        G_SetConstructionLoopSound(self, false);
        /* Legacy construction uses a self-link as a marker, not a production
         * queue. Clear it before generic death cleanup walks build links. */
        self->build = NULL;
    }
    if (self->mineoverlay.parent || self->think == blight_mine_think) S_MineOverlayRelease(self);
    if (S_AcolyteHarvestIsActive(self)) S_AcolyteHarvestRelease(self);
    S_CargoReleaseUnit(self);
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
    self->selected = 0;
    self->s.flags |= EF_NOT_SELECTABLE;
    self->aiflags &= ~AI_HOLD_FRAME;
    unit_setmove(self, &unit_move_death);
    /* Warsmash advances the render Death animation after KillUnit even when
     * the simulation unit is paused by a cinematic. Keep the explicit frame
     * override active until the death move reaches its decay transition. */
    self->animation_override = true;
    if (self->animation) self->s.frame = self->animation->interval[0];
    if (self->sound.death) {
        self->sound.world_pending = self->sound.death;
        self->sound.world_pending_event = EV_DEATH;
    }
    /* Destroying a transport ejects its passengers at the wreck. */
    if (self->cargo.count > 0) {
        cargo_drop_all(self);
    }
    /* Inventory abilities own their death policy. Non-Hero Backpack carriers
     * use inv2/Drop Items On Death; Hero inventory normally leaves this off. */
    G_DropInventoryOnDeath(self);
    /* EVENT_UNIT_DEATH matches widget-specific death triggers
     * (TriggerRegisterDeathEvent/UnitEvent); EVENT_PLAYER_UNIT_DEATH fires the
     * owner's player-unit-death triggers (TriggerRegisterPlayerUnitEvent), e.g.
     * the mission win check that counts the player's dying naga. */
    G_PublishEventWithSource(self, EVENT_UNIT_DEATH, attacker);
    G_PublishEventWithSource(self, EVENT_PLAYER_UNIT_DEATH, attacker);
    self->svflags |= SVF_DEADMONSTER;
    S_UnitDeathAbilities(self);
    S_ReincarnationOnDeath(self);
    /* Static building footprints are baked into pathmap.original. Rebuild after
     * the death flag becomes authoritative so destroyed/cancelled structures
     * stop blocking routes immediately. */
    if (G_UnitIsBuilding(self->class_id)) CM_BakeStaticObstacles();
    G_InvalidateRallyTarget(self);
    /* A dead producer cannot retain ownership of a revival.  This clears each
     * Hero's reviving flag and refunds what this Altar charged. */
    G_CancelHeroRevives(self);
    if (self->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    /* Award experience to the killer's nearby heroes (enemy kills only). */
    if (attacker && attacker != self && attacker->s.player != self->s.player) {
        G_GrantKillXP(self, attacker);
    }
    /* Corpses stop being selection members immediately. Mirror that server
     * mutation to each connected client that had this unit selected; otherwise
     * the packed multiselect layer and local current-selection cache can retain
     * a dead icon until another explicit selection occurs. */
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        if (client->connected && client->ps.number < MAX_PLAYERS &&
            (selected_mask & (1u << client->ps.number)))
            G_SyncClientSelection(client);
    }

    owner = G_GetPlayerClientByNumber(self->s.player);
    if (owner && owner->ps.number == self->s.player &&
        (!owner->connected || owner->ps.number >= MAX_PLAYERS ||
         !(selected_mask & (1u << owner->ps.number))))
        G_InvalidateCommands(owner);
}

void unit_birth(LPEDICT self) {
    unit_setmove(self, &unit_move_birth);
    self->wait = self->data.UnitBalance->buildTime;
    self->s.renderfx |= RF_NO_UBERSPLAT;
}

static bool unit_smart_target_is_enemy(LPEDICT self, LPEDICT target) {
    uint32_t owner;

    if (!self || !target || self->s.player >= MAX_PLAYERS || target->s.player >= MAX_PLAYERS) {
        return false;
    }
    owner = target->s.player;
    if (owner == self->s.player) {
        return false;
    }
    if (owner < PLAYER_NEUTRAL_AGGRESSIVE && level.mapinfo &&
        level.mapinfo->players[owner].playerType == kPlayerTypeNone) {
        return false;
    }
    return !G_PlayerTreatsPlayerAsAlly(self->s.player, owner);
}

static bool unit_smart_target_is_followable(LPEDICT self, LPEDICT target) {
    uint32_t owner;

    if (!self || !target || self->s.player >= MAX_PLAYERS || target->s.player >= MAX_PLAYERS) {
        return false;
    }
    owner = target->s.player;
    if (owner == self->s.player) {
        return true;
    }
    if (owner < PLAYER_NEUTRAL_AGGRESSIVE && level.mapinfo &&
        level.mapinfo->players[owner].playerType == kPlayerTypeNone) {
        return false;
    }
    return G_PlayerTreatsPlayerAsAlly(self->s.player, owner);
}

static bool unit_order_name_valid(cstring_t order) {
    return order && *order && strlen(order) < UNIT_ORDER_NAME_SIZE;
}

typedef struct {
    cstring_t name;
    uint32_t id;
    uint32_t ability;
} unitOrderDef_t;

/* Warcraft order ids are not FourCCs. Keep this table restricted to stock
 * orders whose ids are established by Warcraft/Warsmash and to spell orders
 * that OpenRealm already implements through ability_t. Duplicate names are
 * intentional when more than one stock ability shares a base order. */
static unitOrderDef_t const unit_order_defs[] = {
    { "smart", 851971, 0 },
    { "stop", 851972, 0 },
    { "attack", 851983, 0 },
    { "attackground", 851984, 0 },
    { "move", 851986, 0 },
    { "holdposition", 851993, 0 },
    { "repair", 852024, 0 },
    { "repairon", 852025, 0 },
    { "repairoff", 852026, 0 },

    { "avatar", 852086, MAKEFOURCC('A','H','a','v') },
    { "blizzard", 852089, MAKEFOURCC('A','H','b','z') },
    { "divineshield", 852090, MAKEFOURCC('A','H','d','s') },
    { "holybolt", 852092, MAKEFOURCC('A','H','h','b') },
    { "massteleport", 852093, MAKEFOURCC('A','H','m','t') },
    { "resurrection", 852094, MAKEFOURCC('A','H','r','e') },
    { "thunderbolt", 852095, MAKEFOURCC('A','H','t','b') },
    { "thunderclap", 852096, MAKEFOURCC('A','H','t','c') },
    { "waterelemental", 852097, MAKEFOURCC('A','H','w','e') },
    { "chainlightning", 852119, MAKEFOURCC('A','O','c','l') },
    { "earthquake", 852121, MAKEFOURCC('A','O','e','q') },
    { "farsight", 852122, MAKEFOURCC('A','O','f','s') },
    { "mirrorimage", 852123, MAKEFOURCC('A','O','m','i') },
    { "shockwave", 852125, MAKEFOURCC('A','O','s','h') },
    { "spiritwolf", 852126, MAKEFOURCC('A','O','s','f') },
    { "stomp", 852127, MAKEFOURCC('A','O','w','s') },
    { "whirlwind", 852128, MAKEFOURCC('A','O','w','w') },
    { "windwalk", 852129, MAKEFOURCC('A','O','w','k') },
    { "eattree", 852146, MAKEFOURCC('A','e','a','t') },
    { "barkskin", 852135, MAKEFOURCC('A','b','a','r') },
    { "entanglingroots", 852171, MAKEFOURCC('A','E','e','r') },
    { "forceofnature", 852176, MAKEFOURCC('A','E','f','n') },
    { "manaburn", 852179, MAKEFOURCC('A','E','m','b') },
    { "metamorphosis", 852180, MAKEFOURCC('A','E','m','e') },
    { "starfall", 852183, MAKEFOURCC('A','E','s','f') },
    { "tranquility", 852184, MAKEFOURCC('A','E','t','q') },
    { "animatedead", 852217, MAKEFOURCC('A','U','a','n') },
    { "carrionswarm", 852218, MAKEFOURCC('A','U','c','s') },
    { "darkritual", 852219, MAKEFOURCC('A','U','d','r') },
    { "darkconversion", 852228, MAKEFOURCC('S','N','d','c') },
    { "darkconversion", 852228, MAKEFOURCC('A','N','d','c') },
    { "deathanddecay", 852221, MAKEFOURCC('A','U','d','d') },
    { "deathcoil", 852222, MAKEFOURCC('A','U','d','c') },
    { "deathpact", 852223, MAKEFOURCC('A','U','d','p') },
    { "frostarmor", 852225, MAKEFOURCC('A','U','f','a') },
    { "frostarmor", 852225, MAKEFOURCC('A','U','f','u') },
    { "frostnova", 852226, MAKEFOURCC('A','U','f','n') },
    { "sleep", 852227, MAKEFOURCC('A','U','s','l') },
    { "polymorph", WC3_ORDER_ID_POLYMORPH, MAKEFOURCC('A','p','l','y') },
    { "firebolt", 852231, MAKEFOURCC('A','N','f','b') },
    { "inferno", 852232, MAKEFOURCC('A','U','i','n') },
    { "rainoffire", 852238, MAKEFOURCC('A','N','r','f') },
    { "drain", 852487, MAKEFOURCC('A','N','d','r') },
    { "flamestrike", 852488, MAKEFOURCC('A','H','f','s') },
    { "flamestrike", 852488, MAKEFOURCC('A','N','f','s') },
    { "healingwave", 852501, MAKEFOURCC('A','O','h','w') },
    { "hex", 852502, MAKEFOURCC('A','O','h','x') },
    { "vengeance", 852521, MAKEFOURCC('A','E','s','v') },
    { "blink", 852525, MAKEFOURCC('A','E','b','l') },
    { "fanofknives", 852526, MAKEFOURCC('A','E','f','k') },
    { "shadowstrike", 852527, MAKEFOURCC('A','E','s','h') },
    { "spiritofvengeance", 852528, MAKEFOURCC('A','E','s','v') },
    { "impale", 852555, MAKEFOURCC('A','U','i','m') },
    { "locustswarm", 852556, MAKEFOURCC('A','U','l','s') },
    { "breathoffire", 852580, MAKEFOURCC('A','N','b','f') },
    { "charm", 852581, MAKEFOURCC('A','N','c','h') },
    { "drunkenhaze", 852585, MAKEFOURCC('A','N','d','h') },
    { "forkedlightning", 852587, MAKEFOURCC('A','N','f','l') },
    { "manashieldon", 852589, 0 },
    { "manashieldoff", 852590, 0 },
    { "silence", 852592, MAKEFOURCC('A','N','s','i') },
    { "stampede", 852593, MAKEFOURCC('A','N','s','t') },
    { "summongrizzly", 852594, MAKEFOURCC('A','N','s','g') },
    { "summonquillbeast", 852595, MAKEFOURCC('A','N','s','q') },
    { "summonwareagle", 852596, MAKEFOURCC('A','N','s','w') },
    { "tornado", 852597, MAKEFOURCC('A','N','t','o') },
};

uint32_t G_OrderId(cstring_t order) {
    uint32_t id = 0;
    if (!order) return 0;
    FOR_LOOP(i, sizeof(unit_order_defs) / sizeof(unit_order_defs[0])) {
        if (!strcmp(order, unit_order_defs[i].name)) return unit_order_defs[i].id;
    }
    /* Preserve the old custom-order fallback for maps that intentionally used
     * a four-character order string. */
    memcpy(&id, order, MIN(sizeof(id), strlen(order)));
    return id;
}

cstring_t G_OrderId2String(uint32_t id) {
    FOR_LOOP(i, sizeof(unit_order_defs) / sizeof(unit_order_defs[0])) {
        if (id == unit_order_defs[i].id) return unit_order_defs[i].name;
    }
    return GetClassName(id);
}

static uint32_t unit_spell_code_for_order(LPCEDICT unit, cstring_t order) {
    if (!unit || !order) return 0;
    ability_t const *ordered = FindAbilityByOrder(order);
    if (ordered && (ordered->flags & AB_SPELL) && ordered->classname) {
        uint32_t code = 0;
        memcpy(&code, ordered->classname, MIN(sizeof(code), strlen(ordered->classname)));
        if (G_UnitAbilityLevel(unit, code) && S_SpellAbilityForCode(code)) return code;
    }
    FOR_LOOP(i, sizeof(unit_order_defs) / sizeof(unit_order_defs[0])) {
        uint32_t const code = unit_order_defs[i].ability;
        uint32_t const level = code ? G_UnitAbilityLevel(unit, code) : 0;
        ability_t const *spell = code ? S_SpellAbilityForCode(code) : NULL;
        if (code && !strcmp(order, unit_order_defs[i].name) && level && spell) {
            return code;
        }
    }
    return 0;
}

static uint32_t issued_order_ids[MAX_ENTITIES];
static VECTOR2 issued_order_points[MAX_ENTITIES];
static bool issued_order_point_valid[MAX_ENTITIES];

static uint32_t unit_order_event_id(cstring_t order) {
    return G_OrderId(order);
}

uint32_t G_GetIssuedOrderId(LPCEDICT self) {
    if (!self || self->s.number >= MAX_ENTITIES) return 0;
    return issued_order_ids[self->s.number];
}

bool G_GetIssuedOrderPoint(LPCEDICT self, LPVECTOR2 point) {
    if (point) *point = (VECTOR2){ 0.0f, 0.0f };
    if (!self || self->s.number >= MAX_ENTITIES || !point ||
        !issued_order_point_valid[self->s.number]) return false;
    *point = issued_order_points[self->s.number];
    return true;
}

void G_PublishIssuedPointOrder(LPEDICT self, uint32_t order_id, LPCVECTOR2 point,
                               uint32_t issuer_player, cstring_t debug_order) {
    if (!self || self->s.number >= MAX_ENTITIES || !point) return;
    issued_order_ids[self->s.number] = order_id;
    issued_order_points[self->s.number] = *point;
    issued_order_point_valid[self->s.number] = true;
    if (WC3_TUTORIAL_DEBUG_ENABLED()) {
        fprintf(stderr,
                "WC3_QUEST_ORDER publish event=POINT player=%u unit=%u id=%.4s order=\"%s\" order_id=%u point=(%.1f,%.1f)\n",
                (unsigned)issuer_player, (unsigned)self->s.number,
                (cstring_t)&self->class_id, debug_order ? debug_order : "",
                (unsigned)order_id, point->x, point->y);
    }
    G_PublishEvent(self, EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER);
    G_PublishEvent(self, EVENT_UNIT_ISSUED_POINT_ORDER);
}

void G_PublishIssuedImmediateOrder(LPEDICT self, uint32_t order_id,
                                   uint32_t issuer_player, cstring_t debug_order) {
    if (!self || self->s.number >= MAX_ENTITIES) return;
    issued_order_ids[self->s.number] = order_id;
    issued_order_point_valid[self->s.number] = false;
    G_PublishEvent(self, EVENT_PLAYER_UNIT_ISSUED_ORDER);
    G_PublishEvent(self, EVENT_UNIT_ISSUED_ORDER);
}

static void unit_publish_target_order(LPEDICT self, cstring_t order,
                                      LPEDICT target, uint32_t issuer_player) {
    uint32_t const order_id = unit_order_event_id(order);

    if (!self || self->s.number >= MAX_ENTITIES) return;
    issued_order_ids[self->s.number] = order_id;
    issued_order_point_valid[self->s.number] = false;
    if (WC3_TUTORIAL_DEBUG_ENABLED()) {
        fprintf(stderr,
                "WC3_QUEST_ORDER publish event=TARGET player=%u unit=%u id=%.4s order=\"%s\" order_id=%u target=%u target_id=%.4s\n",
                (unsigned)issuer_player, (unsigned)self->s.number,
                (cstring_t)&self->class_id, order ? order : "",
                (unsigned)order_id, target ? (unsigned)target->s.number : 0u,
                target ? (cstring_t)&target->class_id : "----");
    }
    G_PublishEventWithSource(self, EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER, target);
    G_PublishEventWithSource(self, EVENT_UNIT_ISSUED_TARGET_ORDER, target);
}

bool G_UnitHasActiveOrder(LPCEDICT self) {
    return self && self->currentmove && self->currentmove->proc != NULL &&
           !move_is_terminal_hold(self);
}

bool G_QueueUnitOrder(LPEDICT self, cstring_t order, unitOrderTargetType_t target_type,
                      LPCVECTOR2 point, LPEDICT target, uint32_t issuer_player,
                      float group_speed, uint32_t order_id) {
    unitOrderQueue_t *queue;
    unitOrder_t *queued;
    uint32_t slot;

    if (!self || !unit_order_name_valid(order)) return false;
    queue = &self->order_queue;
    if (queue->count >= MAX_UNIT_ORDER_QUEUE) return false;
    slot = (queue->head + queue->count) % MAX_UNIT_ORDER_QUEUE;
    queued = &queue->entries[slot];
    memset(queued, 0, sizeof(*queued));
    snprintf(queued->order, sizeof(queued->order), "%s", order);
    queued->target_type = target_type;
    queued->issuer_player = issuer_player;
    queued->order_id = order_id;
    queued->group_speed = group_speed;
    if (point) queued->point = *point;
    if (target) {
        uint32_t const number = target->s.number;
        if (number >= globals.num_edicts || globals.edicts + number != target) return false;
        queued->target_number = number;
        queued->target_spawn_time = target->spawn_time;
    }
    queue->count++;
    return true;
}

static bool unit_queue_pop(LPEDICT self, unitOrder_t *out) {
    unitOrderQueue_t *queue;

    if (!self || !out) return false;
    queue = &self->order_queue;
    if (!queue->count) return false;
    *out = queue->entries[queue->head];
    memset(&queue->entries[queue->head], 0, sizeof(queue->entries[queue->head]));
    queue->head = (queue->head + 1) % MAX_UNIT_ORDER_QUEUE;
    queue->count--;
    if (!queue->count) queue->head = 0;
    return true;
}

void G_ClearUnitOrderQueue(LPEDICT self) {
    unitOrderQueue_t *queue;

    if (!self) return;
    queue = &self->order_queue;
    FOR_LOOP(i, queue->count) {
        uint32_t const slot = (queue->head + i) % MAX_UNIT_ORDER_QUEUE;
        S_UnitQueuedOrderEvent(self, &queue->entries[slot], A_QUEUE_ORDER_CANCEL);
    }
    memset(queue, 0, sizeof(*queue));
}

uint32_t G_UnitQueuedOrderCount(LPCEDICT self) {
    return self ? self->order_queue.count : 0;
}

static bool unit_issueorder_now(LPEDICT self, cstring_t order, LPCVECTOR2 point, float group_speed);

static bool unit_issuetargetorder_now(LPEDICT self, cstring_t order, LPEDICT target) {
    if (!self || !order || !target) return false;
    if (M_IsDead(self)) return false;
    if (S_GoldMineWorkerIsInside(self)) return false;

    self->movement.holding_position = false;
    if (!strcmp(order, "repair")) {
        return S_OrderRepair(self, target, 0);
    }
    if (!strcmp(order, "smart")) {
        if (G_IsItem(target)) {
            return G_OrderPickupItem(self, target);
        }
        if (G_ActorHasSkill(self, "Aaha") && G_ActorHasSkill(target, "Abgm")) {
            return S_AcolyteHarvestOrder(self, target);
        }
        if (S_HarvestCanGold(self) && S_GoldMineCanHarvest(target)) {
            return harvest_gold_order(self, target);
        }
        if (S_HarvestCanLumber(self) && target->targtype == TARG_TREE) {
            harvest_start(self, target);
            return true;
        }
        if ((S_HarvestCanLumber(self) || S_HarvestCanGold(self)) &&
            self->harvested_lumber > 0 && harvest_lumber_return_to(self, target))
            return true;
        if ((S_HarvestCanLumber(self) || S_HarvestCanGold(self)) &&
            self->harvested_gold > 0 && harvest_gold_return_to(self, target))
            return true;
        /* Smart/right-click only force-attacks ordinary breakable debris.
         * Other destructable classes require the explicit Attack command, and
         * every destructable must be allowed by the unit weapon target mask. */
        if (G_IsDestructable(target)) {
            if (S_UnitPolymorphed(self) || !G_DestructableAcceptsSmartAttack(self, target)) {
                return false;
            }
            return S_OrderAttack(self, target);
        }
        /* Target-owned Smart interactions run before relation-based attack/follow
         * fallback. The target ability owns validation and any persistent move. */
        if (S_UnitTargetAbilityOrder(target, self, order)) return true;
        if (unit_smart_target_is_enemy(self, target)) {
            if (S_UnitPolymorphed(self)) return false;
            return S_OrderAttack(self, target);
        }
        /* Friendly transports, including Orc Burrows, consume Smart as a
         * boarding order when this unit satisfies their cargo restrictions. */
        if (S_CargoOrderBoard(self, target)) {
            return true;
        }
        if (S_RepairSmart(self, target)) {
            return true;
        }
        if ((target->svflags & SVF_MONSTER) && unit_smart_target_is_followable(self, target)) {
            order_follow(self, target);
            return self->movement.follow_target == target;
        }
        return unit_issueorder_now(self, "move", &target->s.origin2, 0.0f);
    }
    if (!strcmp(order, "move") && (target->svflags & SVF_MONSTER)) {
        order_follow(self, target);
        return self->movement.follow_target == target;
    }
    if (!strcmp(order, "attack")) {
        if (S_UnitPolymorphed(self)) return false;
        if (G_IsDestructable(target) && !G_DestructableCanBeAttackedBy(self, target)) {
            return false;
        }
        return S_OrderAttack(self, target);
    }
    if (!strcmp(order, "militia") || !strcmp(order, "militiaoff")) {
        return S_MilitiaTargetOrder(self, order, target);
    }
    return false;
}

static bool unit_issueorder_now(LPEDICT self, cstring_t order, LPCVECTOR2 point, float group_speed) {
    VECTOR2 target;
    LPEDICT waypoint;

    if (!self || !order || !point) return false;
    if (M_IsDead(self)) return false;
    if (S_GoldMineWorkerIsInside(self)) return false;
    /* Attack Ground is an artillery firing order, not movement. Keep the exact
     * clicked point and allow immobile artillery to accept it. */
    if (!strcmp(order, "attackground")) return S_OrderAttackGround(self, point);
    if (self->aiflags & AI_IMMOBILE) return false;
    if (!strcmp(order, "attack") && S_UnitPolymorphed(self)) return false;

    target = *point;
    CM_ClosestPathablePointForRadiusFlags(point, self->collision, M_UnitStaticPathingFlags(self), &target);
    waypoint = Waypoint_add(&target);
    if (!waypoint) return false;
    self->movement.holding_position = false;
    if (!strcmp(order, "smart") || !strcmp(order, "move")) {
        order_move(self, waypoint);
        self->movement.group_speed = group_speed;
        return true;
    }
    if (!strcmp(order, "attack")) {
        order_attackmove(self, waypoint);
        return true;
    }
    return false;
}

bool G_IssueUnitTargetOrder(LPEDICT self, cstring_t order, LPEDICT target,
                            bool queue, uint32_t issuer_player) {
    if (!self || !order || !target || !target->inuse || !unit_order_name_valid(order)) {
        return false;
    }
    if (M_IsDead(self) || G_BuildingUpgradeActive(self)) {
        return false;
    }
    /* Rally is producer metadata rather than an interruptible unit behavior. */
    if (!strcmp(order, "setrally") || (!strcmp(order, "smart") && G_UnitHasRally(self))) {
        if (!queue) G_ClearUnitOrderQueue(self);
        return G_SetRallyEntity(self, target);
    }
    if (S_GoldMineWorkerIsInside(self)) {
        return false;
    }
    if (!strcmp(order, "harvest")) {
        if (G_ActorHasSkill(self, "Aaha") && G_ActorHasSkill(target, "Abgm"))
            return S_AcolyteHarvestOrder(self, target);
        if (G_ActorHasSkill(self, "Ahar") && S_GoldMineCanHarvest(target))
            return harvest_gold_order(self, target);
        return false;
    }
    {
        uint32_t const spell_code = unit_spell_code_for_order(self, order);
        if (spell_code) {
            bool accepted;
            /* Shift-queued spell casts need a spell-aware queue entry with a
             * stable target snapshot. Leave that unsupported rather than
             * silently enqueueing them as movement orders. */
            if (queue) return false;
            G_ClearUnitOrderQueue(self);
            accepted = S_IssueUnitTargetSpell(self, spell_code, target);
            if (accepted) {
                S_UnitAbilityOrderAccepted(self, order);
                unit_publish_target_order(self, order, target, issuer_player);
            }
            return accepted;
        }
    }
    if (strcmp(order, "smart") && strcmp(order, "move") && strcmp(order, "attack") &&
        strcmp(order, "repair") && strcmp(order, "harvest") && strcmp(order, "militia") && strcmp(order, "militiaoff")) {
        return false;
    }

    if (queue && G_UnitHasActiveOrder(self)) {
        bool const accepted = G_QueueUnitOrder(self, order, UNIT_ORDER_TARGET_ENTITY, NULL, target,
                                               issuer_player, 0.0f, 0);
        if (accepted) unit_publish_target_order(self, order, target, issuer_player);
        return accepted;
    }
    if (!queue) G_ClearUnitOrderQueue(self);
    {
        bool const accepted = unit_issuetargetorder_now(self, order, target);
        if (accepted) unit_publish_target_order(self, order, target, issuer_player);
        return accepted;
    }
}

bool G_IssueUnitPointOrder(LPEDICT self, cstring_t order, LPCVECTOR2 point,
                           bool queue, uint32_t issuer_player, float group_speed) {
    if (!self || !order || !point || !unit_order_name_valid(order)) return false;
    if (M_IsDead(self) || G_BuildingUpgradeActive(self)) return false;
    /* Rally-point changes are metadata and apply immediately even when Shift is down. */
    if (!strcmp(order, "setrally") || (!strcmp(order, "smart") && G_UnitHasRally(self))) {
        bool accepted;
        if (!queue) G_ClearUnitOrderQueue(self);
        accepted = G_SetRallyPoint(self, point);
        if (accepted) {
            G_PublishIssuedPointOrder(self, unit_order_event_id(order), point,
                                      issuer_player, order);
        }
        return accepted;
    }
    if (S_GoldMineWorkerIsInside(self)) return false;
    {
        uint32_t const spell_code = unit_spell_code_for_order(self, order);
        if (spell_code) {
            bool accepted;
            if (queue) return false;
            G_ClearUnitOrderQueue(self);
            accepted = S_CastPointTargetSpell(self, spell_code, point);
            if (accepted) {
                S_UnitAbilityOrderAccepted(self, order);
                G_PublishIssuedPointOrder(self, unit_order_event_id(order), point,
                                          issuer_player, order);
            }
            return accepted;
        }
    }
    if ((self->aiflags & AI_IMMOBILE) && strcmp(order, "attackground")) return false;
    if (strcmp(order, "smart") && strcmp(order, "move") && strcmp(order, "attack") &&
        strcmp(order, "attackground")) return false;

    if (queue && G_UnitHasActiveOrder(self)) {
        bool const accepted = G_QueueUnitOrder(self, order, UNIT_ORDER_TARGET_POINT, point, NULL,
                                               issuer_player, group_speed, 0);
        if (accepted) {
            G_PublishIssuedPointOrder(self, unit_order_event_id(order), point,
                                      issuer_player, order);
        }
        return accepted;
    }
    if (!queue) G_ClearUnitOrderQueue(self);
    {
        bool const accepted = unit_issueorder_now(self, order, point, group_speed);
        if (accepted) {
            G_PublishIssuedPointOrder(self, unit_order_event_id(order), point,
                                      issuer_player, order);
        }
        return accepted;
    }
}

bool G_UnitStartNextQueuedOrder(LPEDICT self) {
    unitOrder_t queued;

    if (!self || M_IsDead(self)) return false;
    while (unit_queue_pop(self, &queued)) {
        if (queued.target_type == UNIT_ORDER_TARGET_POINT) {
            if (unit_issueorder_now(self, queued.order, &queued.point, queued.group_speed))
                return true;
        } else if (queued.target_type == UNIT_ORDER_TARGET_ENTITY) {
            LPEDICT target;
            if (queued.target_number >= globals.num_edicts) continue;
            target = globals.edicts + queued.target_number;
            if (!target->inuse || target->spawn_time != queued.target_spawn_time) continue;
            if (unit_issuetargetorder_now(self, queued.order, target)) return true;
        } else if (S_UnitQueuedOrderEvent(self, &queued, A_QUEUE_ORDER_START)) return true;
    }
    return false;
}

bool unit_issuetargetorder(LPEDICT self, cstring_t order, LPEDICT target) {
    if (G_BuildingUpgradeActive(self)) return false;
    return G_IssueUnitTargetOrder(self, order, target, false,
                                  self ? self->s.player : 0);
}

bool unit_issueorder(LPEDICT self, cstring_t order, LPCVECTOR2 point) {
    if (G_BuildingUpgradeActive(self)) return false;
    return G_IssueUnitPointOrder(self, order, point, false,
                                 self ? self->s.player : 0, 0.0f);
}

/* Rebind an existing edict to another WC3 unit type while retaining its
 * authoritative identity and runtime ownership.  Transformation abilities
 * use this instead of CreateUnit/RemoveUnit so JASS handles, selection, and
 * trigger references keep pointing at the same unit. */
bool G_TransformUnitType(LPEDICT unit, uint32_t type) {
    LPGAMECLIENT client;
    float health_ratio, mana_ratio, temporary_armor, temporary_health, temporary_mana;
    float temporary_attack1, temporary_attack2;
    uint32_t old_flags;
    bool source_building, target_building;

    if (!unit || !type || !G_UnitUI(type)->modelFile) return false;
    source_building = G_UnitIsBuilding(unit->class_id);
    target_building = G_UnitIsBuilding(type);
    /* Keep pathing/lifecycle ownership coherent: morphs may stay within the
     * mobile-unit family or within the building family, but never cross it. */
    if (source_building != target_building) return false;
    health_ratio = unit->health.max_value > 0.0f ? unit->health.value / unit->health.max_value : 1.0f;
    mana_ratio = unit->mana.max_value > 0.0f ? unit->mana.value / unit->mana.max_value : 0.0f;
    temporary_armor = unit->temporary_armor_bonus;
    temporary_health = unit->temporary_health_bonus;
    temporary_mana = unit->temporary_mana_bonus;
    temporary_attack1 = unit->attack1.temporaryDamageBonus;
    temporary_attack2 = unit->attack2.temporaryDamageBonus;
    old_flags = unit->s.flags;

    G_ClearUnitFood(unit);
    if (source_building && unit->pathtex) {
        gi.MemFree(unit->pathtex);
        unit->pathtex = NULL;
    }
    if (old_flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    unit->class_id = unit->s.class_id = type;
    G_BindEntityData(unit);
    unit->s.flags &= ~(EF_BUILDING | EF_FOW_BLOCKER | EF_FOW_REVEALER);
    unit->aiflags &= ~(AI_FLYING | AI_IMMOBILE);
    unit->s.shadow = 0;
    memset(&unit->attack1, 0, sizeof(unit->attack1));
    memset(&unit->attack2, 0, sizeof(unit->attack2));
    unit->permanent_armor_bonus = 0.0f;
    unit->permanent_health_bonus = 0.0f;
    unit->temporary_armor_bonus = 0.0f;
    unit->temporary_health_bonus = 0.0f;
    unit->temporary_mana_bonus = 0.0f;
    SP_SpawnUnit(unit);
    G_ApplyTemporaryMaxHealthBonus(unit, temporary_health);
    G_ApplyTemporaryMaxManaBonus(unit, temporary_mana);
    G_SetHealth(unit, MIN(unit->health.max_value, MAX(0.0f, unit->health.max_value * health_ratio)));
    unit->mana.value = MIN(unit->mana.max_value, MAX(0.0f, unit->mana.max_value * mana_ratio));
    G_ApplyTemporaryArmorBonus(unit, temporary_armor);
    unit->attack1.temporaryDamageBonus = temporary_attack1;
    unit->attack2.temporaryDamageBonus = temporary_attack2;
    G_ActivateUnitFood(unit);
    unit->animation = NULL;
    gi.LinkEntity(unit);
    if (target_building) CM_BakeStaticObstacles();
    client = G_GetPlayerClientByNumber(unit->s.player);
    if (client && client->ps.number == unit->s.player) G_InvalidateCommands(client);
    G_InvalidateUnitInfoPanel(unit);
    G_InvalidateUnitPortrait(unit);
    G_InvalidateUnitShortcutsForUnit(unit);
    return true;
}

bool unit_issueimmediateorder(LPEDICT self, cstring_t order) {
//    printf("%.4s %s\n", &self->class_id, order);
    if (!self || !order) {
        return false;
    }
    if (G_BuildingUpgradeActive(self)) return false;
    if (M_IsDead(self)) return false;
    if (S_GoldMineWorkerIsInside(self))
        return false;
    if (!strcmp(order, "stop")) {
        G_ClearUnitOrderQueue(self);
        order_stop(self);
        G_PublishIssuedImmediateOrder(self, G_OrderId(order), self->s.player, order);
        return true;
    }
    if (!strcmp(order, "holdposition")) {
        bool const accepted = S_HoldPosition(self);
        if (accepted) G_PublishIssuedImmediateOrder(self, G_OrderId(order), self->s.player, order);
        return accepted;
    }
    ability_t const *ability = FindAbilityByOrder(order);
    if (ability) {
        abilityitem_t item = MAKE(abilityitem_t, .ability = ability);
        abilityCall_t call = MAKE(abilityCall_t, .item = &item, .order = order);
        bool const accepted = S_AbilityMessage(self, A_ORDER, &call);
        if (accepted) {
            S_UnitAbilityOrderAccepted(self, order);
            G_PublishIssuedImmediateOrder(self, G_OrderId(order), self->s.player, order);
            return true;
        }
    }
    {
        uint32_t const spell_code = unit_spell_code_for_order(self, order);
        if (spell_code) {
            bool const accepted = S_CastNoTargetSpell(self, spell_code);
            if (accepted) {
                S_UnitAbilityOrderAccepted(self, order);
                G_PublishIssuedImmediateOrder(self, G_OrderId(order), self->s.player, order);
            }
            return accepted;
        }
    }
    if (!strcmp(order, "repairon")) {
        bool const accepted = S_SetRepairAutocast(self, true);
        if (accepted) G_PublishIssuedImmediateOrder(self, G_OrderId(order), self->s.player, order);
        return accepted;
    }
    if (!strcmp(order, "repairoff")) {
        bool const accepted = S_SetRepairAutocast(self, false);
        if (accepted) G_PublishIssuedImmediateOrder(self, G_OrderId(order), self->s.player, order);
        return accepted;
    }
    if (!strcmp(order, "autoharvestgold")) {
        bool const accepted = harvest_auto_start_gold(self);
        if (accepted) G_PublishIssuedImmediateOrder(self, G_OrderId(order), self->s.player, order);
        return accepted;
    }
    if (!strcmp(order, "autoharvestlumber")) {
        bool const accepted = harvest_auto_start_lumber(self);
        if (accepted) G_PublishIssuedImmediateOrder(self, G_OrderId(order), self->s.player, order);
        return accepted;
    }
    return false;
}

/* Create a new runtime unit; explicit JASS creation must not reuse a nearby
 * entity because ReplaceUnitBJ destroys the returned replacement handle. */
LPEDICT unit_create(uint32_t player, uint32_t unitid, LPCVECTOR2 location, float facing) {
    /* CreateUnit returns an immediately usable unit. SP_SpawnAtLocation's
     * presentation birth is for callers that own a spawn lifecycle; applying
     * it here left a stale birth wait behind the explicit stand transition. */
    LPEDICT unit = SP_SpawnAtLocationNoBirth(unitid, player, location);
    if (!unit) {
        return NULL;
    }
    /* Warsmash CreateUnit delegates to createUnitSimple, which checks the
     * spawned unit against static pathing and nudges it to a legal point. */
    VECTOR2 position;
    if (G_FindUnitUnstuckPosition(unit, location, &position)) {
        unit->s.origin2 = position;
        unit->s.origin.x = position.x;
        unit->s.origin.y = position.y;
        M_CheckGround(unit);
        gi.LinkEntity(unit);
    } else fprintf(stderr, "WC3 CreateUnit: no legal spawn point for %c%c%c%c player %u at (%.1f, %.1f); retaining requested position\n",
                   unitid & 255, (unitid >> 8) & 255, (unitid >> 16) & 255, (unitid >> 24) & 255,
                   player, location->x, location->y);
    if (unit->stand) {
        unit->stand(unit);
    }
    unit->s.angle = facing * M_PI / 180;
    G_ActivateUnitFood(unit);
    return unit;
}

LPEDICT unit_createorfind(uint32_t player, uint32_t unitid, LPCVECTOR2 location, float facing) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (ent->inuse && !M_IsDead(ent) && ent->class_id == unitid &&
            Vector2_distance(location, &ent->s.origin2) < 10)
        {
            G_SetUnitPlayer(ent, player);
            ent->s.angle = facing * M_PI / 180;
            G_ActivateUnitFood(ent);
            return ent;
        }
    }
    return unit_create(player, unitid, location, facing);
}

bool unit_additemtoslot(LPEDICT edict, LPEDICT item, uint32_t i) {
    return G_AddItemToSlot(edict, item, i);
}

bool unit_additem(LPEDICT edict, LPEDICT item) {
    return G_PickupItem(edict, item);
}

static int unit_timed_status_debug_level(void) {
    cstring_t value;

    value = gi.CvarString("wc3_timed_status_debug", "0");
    return value ? atoi(value) : 0;
}

static void unit_timed_status_log(cstring_t stage, LPCEDICT ent, heroabilitystatus_t const *status) {
    char code[5] = { 0 };
    uint32_t now;
    int32_t remaining;

    if (unit_timed_status_debug_level() < 1 || !ent || !status ||
        !unit_statusshowstimedbar(status->code))
    {
        return;
    }
    now = G_Time();
    remaining = status->timestamp > now ? (int32_t)(status->timestamp - now) : 0;
    memcpy(code, &status->code, 4);
    fprintf(stderr,
            "WC3_TIMED_STATUS sim stage=%s unit=%u code=%s level=%u now=%u timestamp=%u duration_ms=%u remaining_ms=%ld fraction=%.4f\n",
            stage ? stage : "?", (unsigned)ent->s.number, code,
            (unsigned)status->level, (unsigned)now, (unsigned)status->timestamp,
            (unsigned)status->duration_ms, (long)remaining,
            unit_statusremainingfraction(status));
}

static bool unit_status_stuns(uint32_t code) {
    return code == MAKEFOURCC('B', 's', 't', 'u') || code == MAKEFOURCC('B', 'U', 's', 'l') ||
           code == MAKEFOURCC('B', 'p', 'o', 's') || /* Possession channel victim lock */
           code == MAKEFOURCC('B', 's', 't', 'a'); /* Stasis Trap stun */
}

static bool unit_status_timedlife(uint32_t code) {
    return code == MAKEFOURCC('B', 'T', 'L', 'F');
}

bool unit_statusshowstimedbar(uint32_t code) {
    return code == MAKEFOURCC('B', 'T', 'L', 'F') ||
           code == MAKEFOURCC('B', 'm', 'i', 'l');
}

float unit_statusremainingfraction(heroabilitystatus_t const *status) {
    uint32_t now;

    if (!status || !status->level || !status->timestamp || !status->duration_ms) {
        return 0.0f;
    }
    now = G_Time();
    if (now >= status->timestamp) {
        return 0.0f;
    }
    return MIN(1.0f, (float)(status->timestamp - now) / (float)status->duration_ms);
}

heroabilitystatus_t const *unit_findtimedbarstatus(LPCEDICT ent) {
    heroabilitystatus_t const *result = NULL;
    uint32_t now;

    if (!ent) return NULL;
    now = G_Time();
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t const *status = ent->abilstatus + i;
        if (!status->level || !status->timestamp || !status->duration_ms) continue;
        if (status->timestamp <= now || !unit_statusshowstimedbar(status->code)) continue;
        /* Warsmash owns one timed-status slot; later qualifying buffs replace
         * earlier ones during status population. Preserve that deterministic
         * single-slot behavior using abilstatus[] order. */
        result = status;
    }
    return result;
}

float G_UnitArmorValue(LPCEDICT ent) {
    float armor;

    if (!ent) return 0.0f;
    armor = ent->armor_value;

    /* Bdef is the stock Scroll of Protection status. Keep the authored armor
     * amount in AbilityData (AIda/DataA) rather than baking it into the unit or
     * status record; status expiry then removes the bonus automatically from
     * both combat and HUD calculations without adding save-state fields. */
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t const *status = ent->abilstatus + i;
        if (status->level && status->code == MAKEFOURCC('B', 'd', 'e', 'f')) {
            armor += G_AbilityLevel(MAKEFOURCC('A', 'I', 'd', 'a'), status->level)->data[0].number;
        }
    }
    return armor + S_DevotionArmorBonus((LPEDICT)ent) + S_SpikedArmorBonus(ent) + S_HumanArmorBonus(ent) +
        S_FaerieArmorDelta(ent) + S_FrenzyArmorDelta(ent) + S_BarkskinArmorBonus(ent) +
        S_ManaFlareArmorBonus(ent);
}



/* Generic status-lifecycle dispatch. The owner procedure resolves from the
 * status's origin ability rawcode, never from the victim's learned abilities
 * (the victim may not own the casting ability). REMOVE arrives while the slot
 * is still valid; the wipe happens after every owner has run. */
static void UnitDispatchStatus(LPEDICT ent, heroabilitystatus_t *slot, uint32_t ability, abilityMsg_t msg) {
    abilityitem_t item;
    abilityCall_t call;
    if (!ent || !slot || !slot->level || !ability) return;
    item = S_AbilityItem(ability);
    if (!item.ability || !item.ability->proc) return;
    call = MAKE(abilityCall_t, .item = &item);
    call.status.slot = slot; call.status.ability = ability;
    S_AbilityMessage(ent, msg, &call);
}

/* Return the live slot so an ability can attach its applying rawcode/source after insertion. */
heroabilitystatus_t *unit_findstatus(LPEDICT ent, uint32_t code) {
    if (ent) FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (ent->abilstatus[i].level && ent->abilstatus[i].code == code) return ent->abilstatus + i;
    return NULL;
}

/* Notify active victim statuses before death cleanup can discard their applying state. */
void unit_statusdeath(LPEDICT ent) {
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *slot = ent->abilstatus + i;
        if (slot->level && (!slot->timestamp || slot->timestamp > G_Time()))
            UnitDispatchStatus(ent, slot, slot->data, A_STATUS_DEATH);
    }
}

/* Derived locks come only from statuses that actually remain; owners
 * reconcile their own flight/height state through A_STATUS_REFRESH. */
void unit_refreshstatusflags(LPEDICT ent) {
    bool stunned = false;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = ent->abilstatus + i;
        if (!status->level) continue;
        if (unit_status_stuns(status->code)) stunned = true;
        UnitDispatchStatus(ent, status, status->data, A_STATUS_REFRESH);
    }
    ent->stunned = stunned;
}

/* Dispel/Purge share this so owners run their inverse before the slot is wiped. */
void unit_expirestatus(LPEDICT ent, heroabilitystatus_t *status) {
    uint32_t origin;
    if (!ent || !status || !status->level) return;
    S_HumanStatusExpired(ent, status->code, status->level);
    origin = status->data;
    UnitDispatchStatus(ent, status, origin, A_STATUS_REMOVE);
    memset(status, 0, sizeof(*status));
}

void unit_updatestatuses(LPEDICT ent) {
    uint32_t now = G_Time();
    bool changed = false;
    bool kill = false;
    bool militia_expired = false;

    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = ent->abilstatus + i;
        if (!status->level || !status->timestamp) {
            continue;
        }
        if (now >= status->timestamp) {
            if (unit_status_timedlife(status->code)) {
                kill = true;
            }
            if (status->code == MAKEFOURCC('B', 'O', 'w', 'k')) {
                ent->s.renderfx &= ~RF_HIDDEN;
            }
            if (status->code == MAKEFOURCC('B', 'm', 'i', 'l')) {
                militia_expired = true;
            }
            unit_timed_status_log("expire", ent, status);
            unit_expirestatus(ent, status);
            changed = true;
        } else if (!M_IsDead(ent)) UnitDispatchStatus(ent, status, status->data, A_STATUS_TICK);
    }
    if (changed) {
        unit_refreshstatusflags(ent);
        G_InvalidateUnitInfoPanel(ent);
    }
    if (militia_expired && !M_IsDead(ent)) {
        S_MilitiaExpire(ent);
    }
    if (kill && !M_IsDead(ent)) {
        G_SetHealth(ent, 0);
        if (ent->die) {
            ent->die(ent, ent->owner);
        }
    }
}

void unit_addtimedstatus(LPEDICT ent, cstring_t skill, uint32_t level, float duration) {
    uint32_t code;
    uint32_t now;
    uint32_t duration_ms;
    heroabilitystatus_t *slot = NULL;
    cstring_t stacktype;

    if (!ent || !skill || !*skill || level == 0) {
        return;
    }

    code = *((uint32_t const *)skill);
    now = G_Time();
    duration_ms = duration > 0.0f ? (uint32_t)(duration * 1000.0f) : 0;
    stacktype = S_SpellString(code, "BuffStackType", 0);

    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = ent->abilstatus + i;
        if (status->level && status->code == code) {
            /* Existing buff of same code found — apply stacking rule. */
            if (stacktype && !strcmp(stacktype, "Stack")) {
                status->level += level;
                if (duration_ms) {
                    status->timestamp = now + duration_ms;
                    status->duration_ms = duration_ms;
                }
            } else if (stacktype && !strcmp(stacktype, "Refresh")) {
                if (duration_ms) {
                    status->timestamp = now + duration_ms;
                    status->duration_ms = duration_ms;
                }
            } else {
                /* "Replace" (default): overwrite level and timestamp. */
                status->level = level;
                status->data = 0;
                status->source = NULL;
                status->source_spawn_time = status->rank = status->next_tick = 0;
                if (duration_ms) {
                    status->timestamp = now + duration_ms;
                    status->duration_ms = duration_ms;
                } else {
                    status->timestamp = 0;
                    status->duration_ms = 0;
                }
            }
            unit_refreshstatusflags(ent);
            unit_timed_status_log("refresh", ent, status);
            G_InvalidateUnitInfoPanel(ent);
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
    slot->timestamp = duration_ms ? now + duration_ms : 0;
    slot->duration_ms = duration_ms;
    slot->data = 0;
    slot->source = NULL;
    slot->source_spawn_time = slot->rank = slot->next_tick = 0;
    unit_refreshstatusflags(ent);
    unit_timed_status_log("add", ent, slot);
    G_InvalidateUnitInfoPanel(ent);
}

void unit_addstatus(LPEDICT ent, cstring_t skill, uint32_t level) {
    unit_addtimedstatus(ent, skill, level, 0);
}

uint32_t G_UnitStatusLevel(LPCEDICT ent, uint32_t code) {
    if (!ent || !code) return 0;
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (ent->abilstatus[i].level && ent->abilstatus[i].code == code &&
            (!ent->abilstatus[i].timestamp || ent->abilstatus[i].timestamp > G_Time()))
            return ent->abilstatus[i].level;
    return 0;
}

static heroability_t *G_FindRuntimeAbility(LPEDICT ent, uint32_t abilcode) {
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

static uint32_t G_HeroSkillLevel(LPCEDICT ent, uint32_t abilcode) {
    uint32_t const base_code = G_AbilityCode(abilcode);
    if (!ent || !abilcode) {
        return 0;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t const *ha = ent->heroabilities + i;
        if (ha->level && G_AbilityCode(ha->code) == base_code) {
            return ha->level;
        }
    }
    return 0;
}

void G_HeroInitializeProgression(LPEDICT ent) {
    uint32_t spent_points = 0;

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

bool G_HeroModifySkillPoints(LPEDICT ent, int32_t delta) {
    uint32_t old_points;
    uint32_t new_points;
    LPGAMECLIENT owner;

    if (!ent || !ent->data.UnitBalance || !G_UnitIsHero(ent)) {
        return false;
    }

    old_points = ent->hero.skillpoints;
    if (delta < 0) {
        unsigned long long const remove = (unsigned long long)(-(long long)delta);
        if (!old_points) {
            return false;
        }
        new_points = remove >= old_points ? 0 : old_points - (uint32_t)remove;
    } else if (delta > 0) {
        unsigned long long const sum = (unsigned long long)old_points + (unsigned long long)(uint32_t)delta;
        new_points = sum > (unsigned long long)INT32_MAX ? (uint32_t)INT32_MAX : (uint32_t)sum;
    } else {
        new_points = old_points;
    }

    ent->hero.skillpoints = new_points;
    owner = G_GetPlayerClientByNumber(ent->s.player);
    if (new_points != old_points) {
        if (owner && owner->ps.number == ent->s.player) G_InvalidateCommands(owner);
        /* Hero shortcut badges used to stay stale after learning/gaining points; refresh every viewer that can control this Hero. */
        G_InvalidateUnitShortcutsForUnit(ent);
    }
    return true;
}

uint32_t G_UnitAbilityLevel(LPCEDICT ent, uint32_t abilcode) {
    uint32_t const hero_level = G_HeroSkillLevel(ent, abilcode);
    char id[5] = { 0 };
    if (hero_level) {
        return hero_level;
    }
    if (!ent || !abilcode) return 0;
    memcpy(id, &abilcode, 4);
    return G_ActorHasSkill(ent, id) ? 1 : 0;
}

/* SetUnitAbilityLevel / IncUnitAbilityLevel: rank lives in heroabilities[].
 * Returns the new level, or 0 when the unit does not own the ability. */
uint32_t G_UnitSetAbilityLevel(LPEDICT ent, uint32_t abilcode, int32_t level) {
    heroability_t *existing;
    char id[5] = { 0 };
    uint32_t current;

    if (!ent || !abilcode) return 0;
    current = G_UnitAbilityLevel(ent, abilcode);
    if (!current) return 0;
    if (level < 1) level = 1;
    existing = G_FindRuntimeAbility(ent, abilcode);
    if (existing) {
        existing->level = (uint32_t)level;
        return existing->level;
    }
    /* Unit owns the skill via abilList/added but has no heroabilities slot yet. */
    memcpy(id, &abilcode, 4);
    if (!G_ActorHasSkill(ent, id)) return 0;
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t *ha = ent->heroabilities + i;
        if (ha->level == 0) {
            ha->code = abilcode;
            ha->level = (uint32_t)level;
            return ha->level;
        }
    }
    return 0;
}

void unit_learnability(LPEDICT ent, uint32_t abilcode) {
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

static uint32_t G_HeroAbilityLevelSkip(void) {
    cstring_t const value = Stb_IniCacheFind(&game.config.misc, "Misc", "HeroAbilityLevelSkip");
    uint32_t const skip = value ? (uint32_t)atoi(value) : 0;
    return skip > 0 ? skip : 2;
}

bool G_HeroHasCandidateSkill(LPCEDICT ent, uint32_t abilcode) {
    uint32_t const base_code = G_AbilityCode(abilcode);
    cstring_t list;
    if (!ent || !G_UnitIsHero(ent) || !ent->data.UnitAbilities || !abilcode) {
        return false;
    }
    list = ent->data.UnitAbilities->heroAbilList;
    if (!list) return false;
    while (*list) {
        cstring_t start;
        cstring_t end;
        uint32_t item = 0;

        while (*list == ',' || isspace((unsigned char)*list)) list++;
        if (!*list) break;
        start = list;
        while (*list && *list != ',') list++;
        end = list;
        while (end > start && isspace((unsigned char)end[-1])) end--;
        if ((size_t)(end - start) == sizeof(item)) {
            memcpy(&item, start, sizeof(item));
            if (G_AbilityCode(item) == base_code) return true;
        }
        if (*list == ',') list++;
    }
    return false;
}

uint32_t G_HeroSkillRequiredLevel(LPEDICT ent, uint32_t abilcode) {
    AbilityData_t const *ability = G_AbilityData(abilcode);
    uint32_t const current = G_HeroSkillLevel(ent, abilcode);
    uint32_t const base = ability->reqLevel > 0 ? (uint32_t)ability->reqLevel : 1;
    uint32_t const skip = ability->levelSkip > 0 ? (uint32_t)ability->levelSkip : G_HeroAbilityLevelSkip();
    return base + current * skip;
}

heroSkillState_t G_HeroSkillState(LPEDICT ent, uint32_t abilcode, uint32_t *next_level, uint32_t *required_level) {
    AbilityData_t const *ability;
    uint32_t current;
    uint32_t required;

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
    if (current >= (uint32_t)ability->levels) {
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

bool G_HeroLearnSkill(LPEDICT ent, uint32_t abilcode) {
    uint32_t const old_level = G_HeroSkillLevel(ent, abilcode);

    if (G_HeroSkillState(ent, abilcode, NULL, NULL) != HERO_SKILL_AVAILABLE) {
        return false;
    }
    unit_learnability(ent, abilcode);
    if (G_HeroSkillLevel(ent, abilcode) != old_level + 1) {
        return false;
    }
    if (!G_HeroModifySkillPoints(ent, -1)) {
        return false;
    }
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
    UnitBalance_t const *balance = ent->data.UnitBalance;
    int32_t const baseStr = balance->strength;
    int32_t const baseAgi = balance->agility;
    int32_t const baseInt = balance->intelligence;
    if (baseStr <= 0 && baseAgi <= 0 && baseInt <= 0) {
        return;
    }
    float const newMaxHP = balance->maxHealth + ((int32_t)ent->hero.str - baseStr) * 25.0f +
                           ent->permanent_health_bonus + ent->temporary_health_bonus;
    float const newMaxMana = balance->maxMana + ((int32_t)ent->hero.intel - baseInt) * 15.0f +
                             G_UnitUpgradeEffectBonus(ent, ID_UPGRADE_EFFECT_MAX_MANA) +
                             ent->temporary_mana_bonus;
    float const agiDefenseBonus = game.constants.combatConstantsLoaded
                                ? game.constants.agiDefenseBonus
                                : 0.3f;
    float const newArmor = balance->armor + ((int32_t)ent->hero.agi - baseAgi) * agiDefenseBonus;

    bool const alive = ent->health.value > 0.0f;
    float const dHP = newMaxHP - ent->health.max_value;
    ent->health.max_value = MAX(1.0f, newMaxHP);
    G_AddHealth(ent, dHP);
    if (alive && ent->health.value < 1.0f) {
        G_SetHealth(ent, 1.0f);
    }

    float const dMana = newMaxMana - ent->mana.max_value;
    ent->mana.max_value = MAX(0.0f, newMaxMana);
    ent->mana.value = MAX(0.0f, MIN(ent->mana.max_value, ent->mana.value + dMana));

    ent->armor_value = newArmor + ent->permanent_armor_bonus + ent->temporary_armor_bonus;

    /* Warsmash applies Misc.StrAttackBonus to whichever attribute is primary.
     * OpenRealm does not yet split hero base-vs-bonus attributes, so the current
     * primary value remains in the permanent displayed range; attack/item
     * bonuses themselves are kept separate below. */
    {
        cstring_t const prim = balance->primaryAttribute;
        uint32_t primVal = ent->hero.str;
        float const strAttackBonus = game.constants.combatConstantsLoaded
                                   ? game.constants.strAttackBonus
                                   : 1.0f;
        int32_t primaryDamage;
        if (prim) {
            if (!strcmp(prim, "AGI")) primVal = ent->hero.agi;
            else if (!strcmp(prim, "INT")) primVal = ent->hero.intel;
        }
        primaryDamage = (int32_t)((float)primVal * strAttackBonus);
        if (ent->data.UnitWeapons) {
            ent->attack1.damageBase = (uint32_t)MAX(0,
                (int32_t)ent->data.UnitWeapons->attack1.damageBase + primaryDamage
                + (int32_t)ent->attack1.permanentDamageBonus);
            ent->attack2.damageBase = (uint32_t)MAX(0,
                (int32_t)ent->data.UnitWeapons->attack2.damageBase + primaryDamage
                + (int32_t)ent->attack2.permanentDamageBonus);
        }
    }
}

/* ---- Hero experience / leveling (verified against WC3/Warsmash data flow) ---
 * - Max level: Misc/MaxHeroLevel gameplay constant (default 10).
 * - XP to REACH level L: sum the per-level Misc/NeedHeroXP table, extending it
 *   with NeedHeroXPFormulaA/B/C when the authored list is exhausted. Stock data
 *   yields L1=0, L2=200, L3=500, L4=900, L10=5400.
 * - Attributes are derived live from level, not stored per level-up: each
 *   primary attribute = base + trunc((level-1) * perLevelGain).  The product is
 *   TRUNCATED toward zero (the binary's attribute getter converts the float via
 *   a bare float->int, no rounding) — a (int32_t) cast matches that exactly.
 * - SetHeroLevel works by granting enough XP to reach the level; XP is the
 *   source of truth and level only ever increases. */
uint32_t G_MaxHeroLevel(void) {
    cstring_t const v = Stb_IniCacheFind(&game.config.misc, "Misc", "MaxHeroLevel");
    uint32_t const m = v ? (uint32_t)atoi(v) : 0;
    return m > 0 ? m : 10;
}

/* Warcraft stores NeedHeroXP as the XP required for each next level, not as
 * cumulative XP.  When the authored list runs out, MiscGame extends it with
 * f(i) = A*f(i-1) + B*i + C, where i is the zero-based table index used by
 * Warsmash's CGameplayConstants parser. */
static uint32_t G_HeroXPRequirement(uint32_t index) {
    cstring_t value = Stb_IniCacheFind(&game.config.misc, "Misc", "NeedHeroXP");
    uint32_t current = 0;
    uint32_t count = 0;

    /* When Misc data is unavailable, use the stock per-level sequence directly:
     * 200, 300, 400, ... .  Do not feed a synthetic single 200 entry through
     * the extension recurrence; doing that with A=1/B=100 produces
     * 200,300,500,... and breaks the canonical cumulative thresholds. */
    if (!value || !*value) {
        unsigned long long const stock = (unsigned long long)200 + (unsigned long long)100 * index;
        return stock > (unsigned long long)UINT32_MAX ? UINT32_MAX : (uint32_t)stock;
    }

    while (*value) {
        char *end = NULL;
        double parsed;
        while (*value == ' ' || *value == '\t') value++;
        parsed = strtod(value, &end);
        if (!end || end == value) break;
        if (parsed < 0.0) parsed = 0.0;
        if (parsed > (double)UINT32_MAX) parsed = (double)UINT32_MAX;
        current = (uint32_t)parsed;
        if (count++ == index) return current;
        value = end;
        while (*value == ' ' || *value == '\t') value++;
        if (*value == ',') value++;
        else if (*value) break;
    }
    if (!count) {
        unsigned long long const stock = (unsigned long long)200 + (unsigned long long)100 * index;
        return stock > (unsigned long long)UINT32_MAX ? UINT32_MAX : (uint32_t)stock;
    }
    if (index < count) return current;

    {
        cstring_t aText = Stb_IniCacheFind(&game.config.misc, "Misc", "NeedHeroXPFormulaA");
        cstring_t bText = Stb_IniCacheFind(&game.config.misc, "Misc", "NeedHeroXPFormulaB");
        cstring_t cText = Stb_IniCacheFind(&game.config.misc, "Misc", "NeedHeroXPFormulaC");
        double const a = (aText && *aText) ? atof(aText) :
            1.0; /* BZ_HARDCODED_DATA_FALLBACK: stock NeedHeroXP formula A. */
        double const b = (bText && *bText) ? atof(bText) :
            100.0; /* BZ_HARDCODED_DATA_FALLBACK: stock NeedHeroXP formula B. */
        double const c = (cText && *cText) ? atof(cText) :
            0.0; /* BZ_HARDCODED_DATA_FALLBACK: stock NeedHeroXP formula C. */
        for (uint32_t i = count; i <= index; i++) {
            double next = a * (double)current + b * (double)i + c;
            if (next < 0.0) next = 0.0;
            if (next > (double)UINT32_MAX) next = (double)UINT32_MAX;
            current = (uint32_t)next;
        }
    }
    return current;
}

uint32_t G_HeroXPForLevel(uint32_t level) {
    uint32_t total = 0;
    if (level <= 1) return 0;

    for (uint32_t i = 0; i < level - 1; i++) {
        uint32_t const need = G_HeroXPRequirement(i);
        if (UINT32_MAX - total < need) return UINT32_MAX;
        total += need;
    }
    return total;
}

uint32_t G_HeroLevelForXP(uint32_t xp) {
    uint32_t const maxLevel = G_MaxHeroLevel();
    uint32_t level = 1;
    while (level < maxLevel && xp >= G_HeroXPForLevel(level + 1)) {
        level++;
    }
    return level;
}

/* Set a hero's level and derive its attributes + HP/mana/armor for that level. */
void G_HeroApplyLevel(LPEDICT ent, uint32_t level) {
    UnitBalance_t const *balance = ent->data.UnitBalance;
    int32_t const baseStr = balance->strength;
    int32_t const baseAgi = balance->agility;
    int32_t const baseInt = balance->intelligence;
    if (baseStr <= 0 && baseAgi <= 0 && baseInt <= 0) {
        return; /* not a hero */
    }
    if (level < 1) level = 1;
    if (level > G_MaxHeroLevel()) level = G_MaxHeroLevel();

    float const steps = (float)(level - 1);
    ent->hero.level = level;
    ent->hero.str = (uint32_t)MAX(0, baseStr + (int32_t)(steps * balance->strengthPerLevel));
    ent->hero.agi = (uint32_t)MAX(0, baseAgi + (int32_t)(steps * balance->agilityPerLevel));
    ent->hero.intel = (uint32_t)MAX(0, baseInt + (int32_t)(steps * balance->intelligencePerLevel));
    G_RecomputeHeroStats(ent);
}

/* Update a hero's accumulated XP, leveling it up if a threshold was crossed. */
void G_HeroSetXP(LPEDICT ent, uint32_t xp) {
    uint32_t const oldLevel = ent->hero.level;
    uint32_t newLevel;

    /* Retail/Warsmash SetHeroXP is raise-only: a lower requested XP value does
     * not reduce either XP or level.  Keeping that rule in the shared mutation
     * path also prevents internally inconsistent high-level/low-XP states. */
    if (xp <= ent->hero.xp) {
        return;
    }
    ent->hero.xp = xp;
    newLevel = G_HeroLevelForXP(xp);
    if (newLevel > oldLevel) {
        /* WC3 exposes both player-unit and unit-specific Hero level events.
         * Queue both once for every crossed level; GetLevelingUnit resolves to
         * the same hero through the ordinary event context. */
        for (uint32_t lv = oldLevel + 1; lv <= newLevel; lv++) {
            G_HeroApplyLevel(ent, lv);
            G_HeroModifySkillPoints(ent, 1);
            G_PublishEvent(ent, EVENT_PLAYER_HERO_LEVEL);
            G_PublishEvent(ent, EVENT_UNIT_HERO_LEVEL);
        }
    }
}

/* --- XP-on-kill (data-driven from Units\MiscGame.txt) ------------------------
 * Constants read live from config.misc so map overrides stay 1:1; fallbacks are
 * the WC3 1.29 defaults: HeroExpRange=1200 (XP-share radius), GrantNormalXP=25 +
 * GrantNormalXPFormulaB=5/level (base XP by victim level), GrantHeroXP list
 * 100,120,160,220,300 (heroes), HeroFactorXP=80,70,60,50,0 (diminishing % when
 * the hero outlevels the victim by N), BuildingKillsGiveExp=0. */
static float G_MiscNum(cstring_t key, float fallback) {
    cstring_t const v = Stb_IniCacheFind(&game.config.misc, "Misc", key);
    return (v && *v) ? (float)atof(v) : fallback;
}

/* n-th (0-based) comma-separated entry of a Misc list, clamped to the last. */
static float G_MiscListNum(cstring_t key, uint32_t n, float fallback) {
    cstring_t v = Stb_IniCacheFind(&game.config.misc, "Misc", key);
    if (!v || !*v) {
        return fallback;
    }
    float val = fallback;
    for (uint32_t i = 0; ; i++) {
        val = (float)atof(v);
        cstring_t const comma = strchr(v, ',');
        if (i >= n || !comma) {
            break;
        }
        v = comma + 1;
    }
    return val;
}

bool G_UnitIsHero(LPCEDICT ent) {
    return ent && ent->data.UnitBalance &&
        (ent->data.UnitBalance->strength > 0 || ent->data.UnitBalance->agility > 0 ||
         ent->data.UnitBalance->intelligence > 0);
}

static bool G_HeroReceivesKillXP(LPCEDICT hero, LPCEDICT victim, LPCEDICT killer, float range) {
    if (!hero->inuse || !(hero->svflags & SVF_MONSTER) || !hero->data.UnitBalance ||
        hero->health.value <= 0 || hero->hero.suspend_xp || (hero->aiflags & AI_ILLUSION) ||
        !G_UnitIsHero(hero) ||
        (range >= 0.0f && Vector2_distance(&hero->s.origin2, &victim->s.origin2) > range)) {
        return false;
    }
    if (hero->s.player == killer->s.player) {
        return true;
    }
    return hero->s.player < MAX_PLAYERS && killer->s.player < MAX_PLAYERS &&
           (level.alliances[killer->s.player][hero->s.player] & (1 << ALLIANCE_SHARED_XP));
}

/* Award experience for killing `victim` to nearby heroes owned by the killer
 * or covered by the killer player's directional SHARED_XP alliance. Warcraft
 * divides the available victim XP across all eligible nearby heroes before
 * applying each receiving Hero's level factor. */
void G_GrantKillXP(LPEDICT victim, LPEDICT killer) {
    uint32_t const vcls = victim->class_id;
    if (victim->aiflags & AI_ILLUSION) return;
    uint32_t receivers = 0;
    if (G_PlayerTreatsPlayerAsAlly(killer->s.player, victim->s.player)) {
        return; /* forced attacks on passive allies do not award Hero XP */
    }
    if (G_UnitIsBuilding(vcls) && G_MiscNum("BuildingKillsGiveExp", 0.0f) == 0.0f) {
        return;
    }
    bool const victimHero = G_UnitIsHero(victim);
    uint32_t const victimLevel = victimHero ? (uint32_t)MAX(1, (int32_t)victim->hero.level)
                                         : (uint32_t)MAX(1, victim->data.UnitBalance->level);
    uint32_t baseXP;
    if (victimHero) {
        baseXP = (uint32_t)G_MiscListNum("GrantHeroXP", victimLevel - 1, 100.0f);
    } else {
        float const g0 = G_MiscNum("GrantNormalXP", 25.0f);
        float const gb = G_MiscNum("GrantNormalXPFormulaB", 5.0f);
        baseXP = (uint32_t)(g0 + gb * (float)(victimLevel - 1));
    }
    float const range = G_MiscNum("HeroExpRange", 1200.0f);

    FOR_LOOP(i, globals.num_edicts) {
        if (G_HeroReceivesKillXP(&globals.edicts[i], victim, killer, range)) {
            receivers++;
        }
    }

    /* Warsmash/Warcraft's GlobalExperience policy is a fallback: use global
     * eligible heroes only when no receiver is inside HeroExpRange. */
    bool const global = !receivers &&
        G_MiscNum("GlobalExperience",
            1.0f /* BZ_HARDCODED_DATA_FALLBACK: stock WC3 default. */) != 0.0f;
    if (global) {
        FOR_LOOP(i, globals.num_edicts) {
            if (G_HeroReceivesKillXP(&globals.edicts[i], victim, killer, -1.0f)) {
                receivers++;
            }
        }
    }
    if (!receivers) return;

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT h = &globals.edicts[i];
        if (!G_HeroReceivesKillXP(h, victim, killer, global ? -1.0f : range)) {
            continue;
        }
        /* Diminishing returns: hero N levels above the victim earns
         * HeroFactorXP[N-1] percent (full XP when at or below the victim). */
        int32_t const diff = (int32_t)h->hero.level - (int32_t)victimLevel;
        float factor = 1.0f;
        if (diff > 0) {
            factor = G_MiscListNum("HeroFactorXP", (uint32_t)(diff - 1), 0.0f) / 100.0f;
        }
        uint32_t const award = (uint32_t)(((float)baseXP / (float)receivers) * factor + 0.5f);
        if (award > 0) {
            G_HeroSetXP(h, h->hero.xp + award);
        }
    }
}

/* Scripted hero revival (ReviveHero native): bring a dead hero back to life at
 * (x,y) with HP/mana set from the MiscGame revive factors (defaults: full life,
 * no mana).  Dead heroes persist (unit_decay_think) so the edict is still valid. */
void G_ReviveHero(LPEDICT ent, float x, float y) {
    float mana;

    if (!ent) {
        return;
    }
    if (ent->revival.reviving) G_CancelHeroRevive(ent->revival.producer, ent);
    float const lifeFactor = G_MiscNum("HeroReviveLifeFactor", 1.0f);
    float const manaFactor = G_MiscNum("HeroReviveManaFactor", 0.0f);
    float const manaStart = G_MiscNum("HeroReviveManaStart", 0.0f);
    ent->svflags &= ~SVF_DEADMONSTER;
    ent->s.flags &= ~EF_NOT_SELECTABLE;
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
    G_SetHealth(ent, MIN(ent->health.max_value, MAX(1.0f, ent->health.max_value * lifeFactor)));
    mana = ent->mana.max_value * manaFactor;
    if (ent->data.UnitBalance) mana += ent->data.UnitBalance->initialMana * manaStart;
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
