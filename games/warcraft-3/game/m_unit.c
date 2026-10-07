#include "g_local.h"
#include "skills/s_skills.h"

//void unit_die(edict_t *self);
//void unit_decay2(edict_t *self);
void unit_decay1(edict_t *self);
void unit_begin_decay(edict_t *self);
void unit_decay_think(edict_t *self);
void unit_cooldown(edict_t *self);
void unit_stand(edict_t *self);
bool G_UnitIsHero(edict_t const *ent);

/* WC3 corpse lifetime: DecayTime (flesh, 2s) + BoneDecayTime (bone, 88s) = 90s
 * after the death animation, then the corpse is removed (MiscData.txt). */
#define UNIT_DEATH_TYPE_RAISE (1 << 0)
#define UNIT_DEATH_TYPE_DECAY (1 << 1)

void ai_birth2(edict_t *self) {
    unit_runwait(self, unit_stand);
}

//static mmove_t unit_move_decay2 = { "Decay Bone", NULL, unit_die };
//static mmove_t unit_move_decay1 = { "Decay Flesh", NULL, unit_decay2 };
static umove_t unit_move_birth = { "birth", ai_birth, unit_stand };
static umove_t unit_move_stand = { "stand", ai_stand, unit_stand };
static umove_t unit_move_stand_ready = { "stand ready", ai_stand, unit_stand };
static void unit_decay_flesh_think(edict_t *self);
static void unit_begin_bone_decay(edict_t *self);
static float unit_decay_flesh_duration(edict_t const *self);
static float unit_decay_bone_duration(edict_t const *self);
static float unit_dissipate_duration(edict_t const *self);
static umove_t unit_move_death = { "death", NULL, unit_begin_decay };
/* Simulation owns corpse lifetime. Presentation stretches the selected Decay
 * sequence across the matching gameplay phase, just as Warsmash does. */
static umove_t unit_move_decay_flesh = { "decay flesh", unit_decay_flesh_think, NULL, NULL, unit_decay_flesh_duration };
static umove_t unit_move_decay_bones = { "decay bone", unit_decay_think, NULL, NULL, unit_decay_bone_duration };
static umove_t unit_move_dissipate = { "dissipate", unit_decay_think, NULL, NULL, unit_dissipate_duration };
static umove_t unit_move_decay_remove = { "death", unit_decay_think, NULL };

void unit_decay1(edict_t *self) {
    self->aiflags |= AI_HOLD_FRAME;
}

static void hero_become_revivable(edict_t *self) {
    gameClient_t *owner;

    if (!self || !self->inuse || !G_UnitIsHero(self) ||
        (self->aiflags & AI_ILLUSION) || !(self->svflags & SVF_DEADMONSTER)) return;
    if (!self->revival) self->revival = G_AllocRevival();
    assert(self->revival);
    self->revival->awaiting = true;
    self->revival->reviving = false;
    G_SetEntityHidden(self,true);
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

static float unit_decay_flesh_duration(edict_t const *self) {
    (void)self;
    return unit_decay_wait(game.constants.decayTime);
}

static float unit_decay_bone_duration(edict_t const *self) {
    if (G_UnitIsStructure(self))
        return unit_decay_wait(game.constants.structureDecayTime);
    return unit_decay_wait(game.constants.boneDecayTime);
}

static float unit_dissipate_duration(edict_t const *self) {
    (void)self;
    return unit_decay_wait(game.constants.dissipateTime);
}

float G_UnitDissipatePresentationAlpha(edict_t const *self) {
    float duration;

    if (!self || !self->inuse || self->currentmove != &unit_move_dissipate ||
        !G_UnitIsHero(self) || (self->aiflags & AI_ILLUSION)) return 1.0f;
    duration = unit_dissipate_duration(self);
    if (duration <= 0.0f) return 0.0f;
    return MIN(1.0f, MAX(0.0f, self->wait / duration));
}

static void unit_set_decay_move(edict_t *self, umove_t *move) {
    unit_setmove(self, move);
    /* A missing exact secondary sequence may fall back to another sequence in
     * the same primary family. If the model has no usable family at all, keep
     * the final Death frame while the authoritative timer continues. */
    if (self->animation) self->aiflags &= ~AI_HOLD_FRAME;
    else self->aiflags |= AI_HOLD_FRAME;
}

static void unit_begin_bone_decay(edict_t *self) {
    unit_set_decay_move(self, &unit_move_decay_bones);
    self->wait = unit_decay_wait(game.constants.boneDecayTime);
}

/* Retail cargo preserves corpses indefinitely while stored, but dropping a
 * corpse restarts the bone-decay countdown rather than resuming the old
 * remainder.  Keep this phase-specific: the exact flesh-phase cargo behavior
 * is not established by the available retail evidence. */
void G_RestartCorpseBoneDecayAfterCargo(edict_t *corpse) {
    if (!corpse || !corpse->inuse || corpse->currentmove != &unit_move_decay_bones ||
        G_UnitIsHero(corpse) || G_UnitIsStructure(corpse)) return;
    corpse->wait = unit_decay_wait(game.constants.boneDecayTime);
}

static void unit_decay_flesh_think(edict_t *self) {
    /* An active corpse consumer owns the remains.  Freeze ordinary decay until
     * that reservation is released or, for Cannibalize, the corpse is consumed. */
    if (self->aiflags & (AI_CORPSE_RESERVED | AI_CORPSE_IN_CARGO)) return;
    unit_runwait(self, unit_begin_bone_decay);
}

/* Death animation finished.  UnitData.deathType is authoritative: bit 1 says
 * the remains decay at all.  Ordinary corpses then use the map's separate
 * flesh and bone constants; structures use StructureDecayTime; Heroes retain
 * their distinct dissipation/revival lifecycle. */
void unit_begin_decay(edict_t *self) {
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
    if (G_UnitIsStructure(self)) {
        unit_set_decay_move(self, &unit_move_decay_bones);
        self->wait = unit_decay_wait(game.constants.structureDecayTime);
        return;
    }
    unit_set_decay_move(self, &unit_move_decay_flesh);
    self->wait = unit_decay_wait(game.constants.decayTime);
}

/* Ordinary corpses are removed. Heroes instead finish their dissipation timer,
 * become hidden/awaiting-revive, and keep the same authoritative edict. */
void unit_decay_think(edict_t *self) {
    if (self->aiflags & (AI_CORPSE_RESERVED | AI_CORPSE_IN_CARGO)) return;
    if (G_UnitIsHero(self) && !(self->aiflags & AI_ILLUSION)) {
        if (!(self->revival && self->revival->awaiting)) unit_runwait(self, hero_become_revivable);
        return;
    }
    unit_runwait(self, G_FreeEdict);
}

void unit_entercombat(edict_t *self, edict_t *target) {
    if (!self || !target || target == self || M_IsDead(self) || M_IsDead(target)) {
        return;
    }
    self->combatentity = target;
}

void unit_leavecombat(edict_t *self) {
    if (self) {
        self->combatentity = NULL;
    }
}

bool unit_affectingcombat(edict_t *self) {
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

static void unit_prepare_stand(edict_t *self) {
    self->current_order_id = 0;
    self->build = NULL;
    self->s.renderfx &= ~RF_NO_UBERSPLAT;
    self->s.ability = 255;
    self->movement.last_distance = 0;
    self->movement.blocked_frames = 0;
}

/* Let a persistent ability claim the stand transition before the generic idle move is installed. */
static void unit_install_stand_move(edict_t *self) {
    if (S_UnitAbilityEvent(self, A_UNIT_STAND)) return;
    unit_setmove(self, unit_affectingcombat(self) ? &unit_move_stand_ready : &unit_move_stand);
}

void unit_stand_no_queue(edict_t *self) {
    if (!self) return;
    unit_prepare_stand(self);
    unit_install_stand_move(self);
}

void unit_stand(edict_t *self) {
    /* Reaching stand is the common completion edge for Move, direct Attack,
     * Repair, Harvest, and several cast behaviors. Retire transient state first,
     * then let a pending Shift order become authoritative before installing the
     * idle/default stand behavior. */
    unit_prepare_stand(self);
    if (G_UnitStartNextQueuedOrder(self)) {
        return;
    }
    unit_install_stand_move(self);
}

/* All runtime unit-health changes pass here so intrinsic ability levels transition exactly once. */
void G_SetHealth(edict_t *ent, float value) {
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
    if (G_UnitIsStructure(ent) && (old != next || value <= 0.0f))
        S_RefreshAbilityLevel(ent, FindAbilityByClassname("Afih"));
}

void G_AddHealth(edict_t *ent, float value) { G_SetHealth(ent, MIN(ent->health.max_value, ent->health.value + value)); }

static void unit_apply_health_cap_delta(edict_t *ent, float amount, float *ledger) {
    float old_max, fraction;
    if (!ent || amount == 0.0f) return;
    old_max = MAX(1.0f, ent->health.max_value);
    fraction = ent->health.value / old_max;
    *ledger += amount;
    ent->health.max_value = MAX(1.0f, ent->health.max_value + amount);
    G_SetHealth(ent, ent->health.max_value * fraction);
    G_InvalidateUnitInfoPanel(ent);
}

void G_ApplyPermanentMaxHealthBonus(edict_t *ent, float amount) {
    if (ent) unit_apply_health_cap_delta(ent, amount, &ent->permanent_health_bonus);
}

void G_ApplyTemporaryMaxHealthBonus(edict_t *ent, float amount) {
    if (ent) unit_apply_health_cap_delta(ent, amount, &ent->temporary_health_bonus);
}

void G_ApplyTemporaryMaxManaBonus(edict_t *ent, float amount) {
    float old_max, fraction;
    if (!ent || amount == 0.0f) return;
    old_max = MAX(1.0f, ent->mana.max_value);
    fraction = ent->mana.value / old_max;
    ent->temporary_mana_bonus += amount;
    ent->mana.max_value = MAX(0.0f, ent->mana.max_value + amount);
    ent->mana.value = ent->mana.max_value * fraction;
    G_InvalidateUnitInfoPanel(ent);
}

static void unit_apply_armor_delta(edict_t *ent, float amount, float *ledger) {
    if (!ent || amount == 0.0f) return;
    *ledger += amount;
    ent->armor_value += amount;
    G_InvalidateUnitInfoPanel(ent);
}

void G_ApplyPermanentArmorBonus(edict_t *ent, float amount) {
    if (ent) unit_apply_armor_delta(ent, amount, &ent->permanent_armor_bonus);
}

void G_ApplyTemporaryArmorBonus(edict_t *ent, float amount) {
    if (ent) unit_apply_armor_delta(ent, amount, &ent->temporary_armor_bonus);
}

void G_ApplyPermanentAttackDamageBonus(edict_t *ent, float amount) {
    if (!ent || amount == 0.0f) return;
    if (S_AttackProfileRead(ent, 0)->numberOfDice) {
        S_AttackProfileWrite(ent, 0)->permanentDamageBonus += amount;
        S_AttackProfileWrite(ent, 0)->damageBase = (uint32_t)MAX(0, (int32_t)S_AttackProfileRead(ent, 0)->damageBase + (int32_t)amount);
    }
    if (S_AttackProfileRead(ent, 1)->numberOfDice) {
        S_AttackProfileWrite(ent, 1)->permanentDamageBonus += amount;
        S_AttackProfileWrite(ent, 1)->damageBase = (uint32_t)MAX(0, (int32_t)S_AttackProfileRead(ent, 1)->damageBase + (int32_t)amount);
    }
    G_InvalidateUnitInfoPanel(ent);
}

void G_ApplyTemporaryAttackDamageBonus(edict_t *ent, float amount) {
    if (!ent || amount == 0.0f) return;
    S_AttackProfileWrite(ent, 0)->temporaryDamageBonus += amount;
    S_AttackProfileWrite(ent, 1)->temporaryDamageBonus += amount;
    G_InvalidateUnitInfoPanel(ent);
}

static bool unit_is_raisable_corpse(edict_t const *ent, bool stored) {
    UnitData_t const *data;
    if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER) ||
        !(ent->svflags & SVF_DEADMONSTER) || !M_IsDead(ent) ||
        (ent->aiflags & (AI_CORPSE_UNRAISABLE | AI_CORPSE_RESERVED))) return false;
    if (!!(ent->aiflags & AI_CORPSE_IN_CARGO) != stored) return false;
    data = ent->data.UnitData ? ent->data.UnitData : G_UnitData(ent->class_id);
    return data && (data->deathType & UNIT_DEATH_TYPE_RAISE) != 0;
}

bool G_UnitIsRaisableCorpse(edict_t const *ent) { return unit_is_raisable_corpse(ent, false); }
bool G_UnitIsRaisableStoredCorpse(edict_t const *ent) { return unit_is_raisable_corpse(ent, true); }
int32_t G_CorpseUnitLevel(edict_t const *ent) {
    UnitBalance_t const *balance = ent ? ent->data.UnitBalance : NULL;
    if (!balance && ent) balance = G_UnitBalance(ent->class_id);
    return balance ? balance->level : 0;
}

/* Corpse revival keeps handle identity while retiring every death-state owner.
 * Permanent revival restores ordinary corpse/food policy; temporary raised
 * summons deliberately remain outside food accounting and cannot create a new
 * raisable corpse when their timed life later ends. */
static void revive_corpse_state(edict_t *ent, float life_fraction, bool temporary_summon) {
    if (!ent) return;
    ent->svflags &= ~SVF_DEADMONSTER; ent->s.flags &= ~EF_NOT_SELECTABLE;
    ent->aiflags &= ~AI_HOLD_FRAME; G_SetEntityHidden(ent,false);
    S_SetMoveGoal(ent, &ent->secondarygoal, NULL);
    S_SetMoveGoal(ent, &ent->goalentity, NULL);
    ent->combatentity = NULL;
    ent->wait = 0; G_ClearUnitOrderQueue(ent);
    ent->aiflags &= ~(AI_CORPSE_RESERVED | AI_CORPSE_IN_CARGO);
    if (temporary_summon) ent->aiflags |= AI_CORPSE_UNRAISABLE | AI_CORPSE_NO_DECAY;
    else ent->aiflags &= ~(AI_CORPSE_UNRAISABLE | AI_CORPSE_NO_DECAY);
    G_SetHealth(ent, ent->health.max_value * MAX(0.0f, MIN(1.0f, life_fraction)));
    if (!temporary_summon) G_ActivateUnitFood(ent);
    unit_stand(ent); gi.LinkEntity(ent);
}

void G_ReviveCorpse(edict_t *ent, float life_fraction) {
    revive_corpse_state(ent, life_fraction, false);
}

void G_ReviveCorpseAsSummon(edict_t *ent, float life_fraction) {
    revive_corpse_state(ent, life_fraction, true);
}

void unit_die(edict_t *self, edict_t *attacker) {
    gameClient_t *owner;
    uint32_t selected_mask;

    if (!self || (self->svflags & SVF_DEADMONSTER)) return;
    selected_mask = self->selected;

    S_AvatarExpire(self);
    /* A dead polymorphed unit must not later restore as a living unit when its
     * timed buff expires.  Keep the death presentation chosen at the time of
     * death, but retire the reversible morph contract immediately. */
    G_FreePolymorph(self);
    G_ClearUnitOrderQueue(self);
    self->current_order_id = 0;
    G_InvalidateUnitShortcutsForUnit(self);
    G_SetHealth(self, 0.0f);
    /* Marks belong to their applying abilities, even when another unit lands the killing blow. */
    unit_statusdeath(self);
    /* Construction owns Repair workers and a self-linked HUD queue marker.
     * Tear that state down before generic production/revival death cleanup.
     * A structure upgrade is the same edict rather than a queued child; death
     * abandons it without the player-cancel refund. */
    if (G_BuildingUpgradeActive(self)) G_StopBuildingUpgrade(self, false);
    if (self->construction) G_StopConstruction(self);
    else if (self->build == self) {
        G_SetConstructionLoopSound(self, false);
        /* Legacy construction uses a self-link as a marker, not a production
         * queue. Clear it before generic death cleanup walks build links. */
        self->build = NULL;
    }
    if ((self->mineoverlay && self->mineoverlay->parent) || self->think == blight_mine_think) S_MineOverlayRelease(self);
    if (S_AcolyteHarvestIsActive(self)) S_AcolyteHarvestRelease(self);
    S_CargoReleaseUnit(self);
    if (self->training) G_ClearTrainingQueueFood(self);
    else { G_CancelHeroRevives(self); G_CancelTrainingQueue(self, true); }
    G_ClearUnitFood(self);
    if (G_UnitIsHero(self)) {
        if (!self->revival) self->revival = G_AllocRevival();
        assert(self->revival);
        self->revival->awaiting = false;
        self->revival->reviving = false;
        self->revival->producer = NULL;
        self->revival->queue_next = NULL;
        self->revival->player = 0;
        self->revival->gold = self->revival->lumber = 0;
        self->revival->progress = 0.0f;
    }
    unit_leavecombat(self);
    G_SetEntitySelectionMask(self, 0);
    self->s.flags |= EF_NOT_SELECTABLE;
    self->aiflags &= ~AI_HOLD_FRAME;
    unit_setmove(self, &unit_move_death);
    /* Warsmash advances the render Death animation after KillUnit even when
     * the simulation unit is paused by a cinematic. Keep the explicit frame
     * override active until the death move reaches its decay transition. */
    self->animation_override = true;
    if (self->animation) self->s.frame = self->animation->interval[0];
    if (G_UnitSoundProfile(self)->death) {
        self->sound.world_pending = G_UnitSoundProfile(self)->death;
        self->sound.world_pending_event = EV_DEATH;
    }
    /* Destroying a transport ejects its passengers at the wreck. */
    if (self->cargo && self->cargo->count > 0) {
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
    G_MarkMoveSpatialObject(self);
    S_UnitAbilityEvent(self, A_DEATH);
    S_ReincarnationOnDeath(self);
    /* Static building footprints are baked into pathmap.original. Rebuild after
     * the death flag becomes authoritative so destroyed/cancelled structures
     * stop blocking routes immediately. */
    if (G_UnitIsStructure(self)) CM_BakeStaticObstacles();
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
        gameClient_t *client = game.clients + i;
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

void unit_birth(edict_t *self) {
    unit_setmove(self, &unit_move_birth);
    self->wait = self->data.UnitBalance->buildTime;
    self->s.renderfx |= RF_NO_UBERSPLAT;
}

static bool unit_smart_target_is_enemy(edict_t *self, edict_t *target) {
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

static bool unit_smart_target_is_followable(edict_t *self, edict_t *target) {
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
    { "patrol", WC3_ORDER_ID_PATROL, 0 },
    { "holdposition", 851993, 0 },
    { "repair", 852024, 0 },
    { "board", 852043, 0 }, /* Public retail OrderId; Captain speed excludes this head. */
    { "ambush", 852131, MAKEFOURCC('A','h','i','d') },
    { "repairon", 852025, 0 },
    { "repairoff", 852026, 0 },
    { "renew", 852161, 0 },
    { "renewon", 852162, 0 },
    { "renewoff", 852163, 0 },
    { "restoration", 852202, 0 },
    { "restorationon", 852203, 0 },
    { "restorationoff", 852204, 0 },
    { "defend", 852055, 0 },
    { "undefend", 852056, 0 },

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
    { "entangle", 852147, 0 },
    { "entangleinstant", 852148, 0 },
    { "autoentangle", 852505, 0 },
    { "autoentangleinstant", 852506, 0 },
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

static uint32_t unit_spell_code_for_order(edict_t const *unit, cstring_t order) {
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
static vec2_t issued_order_points[MAX_ENTITIES];
static bool issued_order_point_valid[MAX_ENTITIES];

static uint32_t unit_order_event_id(cstring_t order) {
    return G_OrderId(order);
}

uint32_t G_GetIssuedOrderId(edict_t const *self) {
    if (!self || self->s.number >= MAX_ENTITIES) return 0;
    return issued_order_ids[self->s.number];
}

bool G_GetIssuedOrderPoint(edict_t const *self, vec2_t *point) {
    if (point) *point = (vec2_t){ 0.0f, 0.0f };
    if (!self || self->s.number >= MAX_ENTITIES || !point ||
        !issued_order_point_valid[self->s.number]) return false;
    *point = issued_order_points[self->s.number];
    return true;
}

void G_PublishIssuedPointOrder(edict_t *self, uint32_t order_id, vec2_t const *point,
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
    G_PublishEventWithPoint(&(gameEventPointParams_t){
        .edict = self, .type = EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,
        .value = order_id, .point = point });
    G_PublishEventWithPoint(&(gameEventPointParams_t){
        .edict = self, .type = EVENT_UNIT_ISSUED_POINT_ORDER,
        .value = order_id, .point = point });
}

void G_PublishIssuedImmediateOrder(edict_t *self, uint32_t order_id,
                                   uint32_t issuer_player, cstring_t debug_order) {
    if (!self || self->s.number >= MAX_ENTITIES) return;
    issued_order_ids[self->s.number] = order_id;
    issued_order_point_valid[self->s.number] = false;
    G_PublishEventWithValue(self, EVENT_PLAYER_UNIT_ISSUED_ORDER, NULL, order_id);
    G_PublishEventWithValue(self, EVENT_UNIT_ISSUED_ORDER, NULL, order_id);
}

static void unit_publish_target_order(edict_t *self, cstring_t order,
                                      edict_t *target, uint32_t issuer_player) {
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
    G_PublishEventWithValue(self, EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER, target, order_id);
    G_PublishEventWithValue(self, EVENT_UNIT_ISSUED_TARGET_ORDER, target, order_id);
}

bool G_UnitHasActiveOrder(edict_t const *self) {
    return self && self->currentmove && self->currentmove->proc != NULL &&
           !move_is_terminal_hold(self);
}

bool G_QueueUnitOrder(edict_t *self, cstring_t order, unitOrderTargetType_t target_type,
                      vec2_t const *point, edict_t *target, uint32_t issuer_player,
                      float group_speed, uint32_t order_id) {
    unitOrderQueue_t *queue;
    unitOrder_t *queued;
    uint32_t slot;

    if (!self || !unit_order_name_valid(order)) return false;
    queue = &self->order_queue;
    if (queue->count >= MAX_UNIT_ORDER_QUEUE) return false;
    if (!queue->entries) queue->entries = G_AllocUnitOrders()->entries;
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

static bool unit_queue_pop(edict_t *self, unitOrder_t *out) {
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

void G_ClearUnitOrderQueue(edict_t *self) {
    unitOrderQueue_t *queue;

    if (!self) return;
    queue = &self->order_queue;
    FOR_LOOP(i, queue->count) {
        uint32_t const slot = (queue->head + i) % MAX_UNIT_ORDER_QUEUE;
        S_UnitQueuedOrderEvent(self, &queue->entries[slot], A_QUEUE_ORDER_CANCEL);
    }
    if (queue->entries) memset(queue->entries, 0, sizeof(unitOrderStorage_t));
    queue->head = queue->count = 0;
}

uint32_t G_UnitQueuedOrderCount(edict_t const *self) {
    return self ? self->order_queue.count : 0;
}

static bool unit_issueorder_now(edict_t *self, cstring_t order, vec2_t const *point, float group_speed);

static bool unit_issuetargetorder_now(edict_t *self, cstring_t order, edict_t *target) {
    if (!self || !order || !target) return false;
    if (M_IsDead(self)) return false;
    if (S_GoldMineWorkerIsInside(self)) return false;

    self->movement.holding_position = false;
    if (!strcmp(order, "smart")) {
        if (G_IsItem(target)) {
            return G_OrderPickupItem(self, target);
        }
        {
            abilityOrderResult_t const result = S_UnitIssuedTargetOrder(self, order, target);
            if (result != ABILITY_ORDER_UNHANDLED) return result == ABILITY_ORDER_ACCEPTED;
        }
        if (G_ActorHasAbilityCode(self, MAKEFOURCC('A','a','h','a')) && G_ActorHasAbilityCode(target, MAKEFOURCC('A','b','g','m'))) {
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
            return S_IssueFollowOrder(self, target, G_OrderId(order));
        }
        return unit_issueorder_now(self, "move", &target->s.origin2, 0.0f);
    }
    if (!strcmp(order, "move") && (target->svflags & SVF_MONSTER)) {
        return S_IssueFollowOrder(self, target, G_OrderId(order));
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
    /* Authored unit-target abilities own their concrete order strings. Smart
     * dispatches here earlier so it can precede generic harvesting/following;
     * explicit orders such as Entangle arrive here after built-in commands. */
    {
        abilityOrderResult_t const result = S_UnitIssuedTargetOrder(self, order, target);
        if (result != ABILITY_ORDER_UNHANDLED) return result == ABILITY_ORDER_ACCEPTED;
    }
    return false;
}

static bool unit_issueorder_now(edict_t *self, cstring_t order, vec2_t const *point, float group_speed) {
    vec2_t target;
    edict_t *waypoint;

    if (!self || !order || !point) return false;
    if (M_IsDead(self)) return false;
    if (S_GoldMineWorkerIsInside(self)) return false;
    /* Attack Ground is an artillery firing order, not movement. Keep the exact
     * clicked point and allow immobile artillery to accept it. */
    if (!strcmp(order, "attackground")) return S_OrderAttackGround(self, point);
    if (self->aiflags & AI_IMMOBILE) return false;
    if (!strcmp(order, "attack") && S_UnitPolymorphed(self)) return false;

    if (!strcmp(order, "smart") || !strcmp(order, "move")) {
        /* A point Move retains the public click. Its owning path/formation
         * code admits intermediate and adjusted destinations; correcting the
         * user head here erases retail's blocked-goal retry lifetime. */
        waypoint = Waypoint_add(point);
        if (!waypoint) return false;
        self->movement.holding_position = false;
        S_IssueMoveOrder(self, waypoint, G_OrderId(order));
        self->movement.group_speed = group_speed;
        return true;
    }
    target = *point;
    pathAccelParams_t query = { point, NULL, self->collision, M_UnitStaticPathingFlags(self) };
    G_ClosestMovePathPoint(&query, &target);
    waypoint = Waypoint_add(&target);
    if (!waypoint) return false;
    self->movement.holding_position = false;
    if (!strcmp(order, "attack")) {
        order_attackmove(self, waypoint);
        return true;
    }
    if (!strcmp(order, "patrol")) return S_IssuePatrolOrder(self, waypoint);
    return false;
}

bool G_IssueUnitTargetOrder(edict_t *self, cstring_t order, edict_t *target,
                            bool queue, uint32_t issuer_player) {
    if (!self || !order || !target || !target->inuse || !unit_order_name_valid(order)) {
        return false;
    }
    if (M_IsDead(self) || G_BuildingUpgradeActive(self) || !S_AncientCanReceiveOrder(self)) {
        return false;
    }
    /* Rally owns native admission separately from its direct metadata setters. */
    if (!strcmp(order, "setrally") || (!strcmp(order, "smart") && G_UnitHasRally(self))) {
        return S_IssueRallyTargetOrder(self,target,queue);
    }
    if (S_GoldMineWorkerIsInside(self)) {
        return false;
    }
    /* Validate/intercept through the owner before clearing pending work. A
     * same-target interaction can retain its current task and public head. */
    {
        ability_t const *owner = FindAbilityByOrder(order);
        abilityProc_t const active = self->currentmove ? self->currentmove->proc : NULL;
        abilityCall_t call = {.issued_target_order = {target, order, queue}};
        intptr_t result = ABILITY_ORDER_UNHANDLED;
        if (owner) result = owner->proc(self, A_TARGET_ORDER_ADMIT, &call);
        if (result == ABILITY_ORDER_UNHANDLED && active && (!owner || owner->proc != active))
            result = active(self, A_TARGET_ORDER_ADMIT, &call);
        if (result == ABILITY_ORDER_REJECTED) return false;
        if (result == ABILITY_ORDER_INTERCEPTED) {
            unit_publish_target_order(self, order, target, issuer_player);
            return true;
        }
    }
    if (!strcmp(order, "harvest")) {
        bool accepted = false;
        if (G_ActorHasAbilityCode(self, MAKEFOURCC('A','a','h','a')) && G_ActorHasAbilityCode(target, MAKEFOURCC('A','b','g','m')))
            accepted = S_AcolyteHarvestOrder(self, target);
        else if (G_ActorHasAbilityCode(self, MAKEFOURCC('A','h','a','r')) && S_GoldMineCanHarvest(target))
            accepted = harvest_gold_order(self, target);
        if (accepted) S_UnitAbilityOrderAccepted(self, order);
        return accepted;
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
    if (queue && strcmp(order, "smart") && strcmp(order, "move") && strcmp(order, "attack") &&
        strcmp(order, "repair") && strcmp(order, "harvest") && strcmp(order, "militia") && strcmp(order, "militiaoff")) {
        /* Only orders with an owning ability may enter the FIFO. Generic
         * queued replay can then dispatch their concrete target order. */
        if (!FindAbilityByOrder(order)) return false;
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
        if (accepted) {
            S_UnitAbilityOrderAccepted(self, order);
            unit_publish_target_order(self, order, target, issuer_player);
        }
        return accepted;
    }
}

bool G_IssueUnitPointOrder(edict_t *self, cstring_t order, vec2_t const *point,
                           bool queue, uint32_t issuer_player, float group_speed) {
    if (!self || !order || !point || !unit_order_name_valid(order)) return false;
    if (M_IsDead(self) || G_BuildingUpgradeActive(self) || !S_AncientCanReceiveOrder(self)) return false;
    /* Rally's owner decides whether this producer replaces active work. */
    if (!strcmp(order, "setrally") || (!strcmp(order, "smart") && G_UnitHasRally(self))) {
        bool accepted;
        accepted = S_IssueRallyPointOrder(self,point,queue);
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
            accepted = S_IssuePointTargetSpell(self, spell_code, point);
            if (accepted) {
                S_UnitAbilityOrderAccepted(self, order);
                G_PublishIssuedPointOrder(self, unit_order_event_id(order), point,
                                          issuer_player, order);
            }
            return accepted;
        }
    }
    if ((self->aiflags & AI_IMMOBILE) && strcmp(order, "attackground")) return false;
    if (strcmp(order, "smart") && strcmp(order, "move") && strcmp(order, "attack") && strcmp(order, "patrol") &&
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
            S_UnitAbilityOrderAccepted(self, order);
            G_PublishIssuedPointOrder(self, unit_order_event_id(order), point,
                                      issuer_player, order);
        }
        return accepted;
    }
}

bool G_UnitStartNextQueuedOrder(edict_t *self) {
    unitOrder_t queued;

    if (!self || M_IsDead(self) || !S_AncientCanReceiveOrder(self)) return false;
    while (unit_queue_pop(self, &queued)) {
        if (queued.owner_context) {
            if (S_UnitQueuedOrderEvent(self,&queued,A_QUEUE_ORDER_START)) {
                S_UnitAbilityOrderAccepted(self,queued.order);
                return true;
            }
            continue;
        }
        if (queued.target_type == UNIT_ORDER_TARGET_POINT) {
            if (unit_issueorder_now(self, queued.order, &queued.point, queued.group_speed)) {
                S_UnitAbilityOrderAccepted(self, queued.order);
                return true;
            }
        } else if (queued.target_type == UNIT_ORDER_TARGET_ENTITY) {
            edict_t *target;
            if (queued.target_number >= globals.num_edicts) continue;
            target = globals.edicts + queued.target_number;
            if (!target->inuse || G_IsDeferredFree(target) || target->spawn_time != queued.target_spawn_time) continue;
            if (unit_issuetargetorder_now(self, queued.order, target)) {
                S_UnitAbilityOrderAccepted(self, queued.order);
                return true;
            }
        } else if (queued.target_type == UNIT_ORDER_TARGET_NONE) {
            if (!strcmp(queued.order, "stop")) {
                order_stop_queued(self);
                return true;
            }
            if (!strcmp(queued.order, "holdposition")) {
                return S_HoldPositionQueued(self);
            }
            if (S_UnitQueuedOrderEvent(self, &queued, A_QUEUE_ORDER_START)) return true;
        } else if (S_UnitQueuedOrderEvent(self, &queued, A_QUEUE_ORDER_START)) return true;
    }
    return false;
}

bool unit_issuetargetorder(edict_t *self, cstring_t order, edict_t *target) {
    if (G_BuildingUpgradeActive(self) || !S_AncientCanReceiveOrder(self)) return false;
    return G_IssueUnitTargetOrder(self, order, target, false,
                                  self ? self->s.player : 0);
}

bool unit_issueorder(edict_t *self, cstring_t order, vec2_t const *point) {
    if (G_BuildingUpgradeActive(self)) return false;
    return G_IssueUnitPointOrder(self, order, point, false,
                                 self ? self->s.player : 0, 0.0f);
}

/* Give the registered order owner the retained batch before ordinary per-unit
 * admission. Unhandled orders keep the same spell/build/point dispatch. */
bool G_IssueGroupPointOrder(groupPointOrder_t const *request) {
    if (!request || !request->point || request->count>BZ_WC3_GROUP_ORDER_UNITS) return false;
    /* Native23acd0 owns point words before attachment/admission. Stopping a
     * moving candidate or an issued-order callback may change caller storage;
     * nested orders must not change this batch's point or retained identities. */
    vec2_t point=*request->point;
    groupPointOrder_t retained=*request;
    retained.point=&point;request=&retained;
    ability_t const *owner=FindAbilityByOrder(request->order);
    if (owner) {
        abilityitem_t item={.code=FS_SLKKey(owner->classname),.ability=owner};
        abilityCall_t call={.item=&item,.group_order=request};
        intptr_t result=S_AbilityMessage(NULL,A_GROUP_POINT_ORDER,&call);
        if (result!=ABILITY_ORDER_UNHANDLED) return result==ABILITY_ORDER_ACCEPTED;
    }
    bool any=false;
    FOR_LOOP(i,request->count) {
        edict_t *unit=request->units[i].unit;
        if (!unit->inuse || unit->spawn_time!=request->units[i].spawn || G_IsDeferredFree(unit)) continue;
        if (G_UnitIsBuilding(request->order_id)) {
            if (G_IssueBuildOrder(unit,request->order_id,request->point)) any=true;
        } else if (unit_issueorder(unit,request->order,request->point)) any=true;
    }
    return any;
}

/* Rebind an existing edict to another WC3 unit type while retaining its
 * authoritative identity and runtime ownership.  Transformation abilities
 * use this instead of CreateUnit/RemoveUnit so JASS handles, selection, and
 * trigger references keep pointing at the same unit. */
bool G_TransformUnitType(edict_t *unit, uint32_t type) {
    gameClient_t *client;
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
    S_UnitAbilityEvent(unit, A_UNIT_TYPE_CHANGING);
    health_ratio = unit->health.max_value > 0.0f ? unit->health.value / unit->health.max_value : 1.0f;
    mana_ratio = unit->mana.max_value > 0.0f ? unit->mana.value / unit->mana.max_value : 0.0f;
    temporary_armor = unit->temporary_armor_bonus;
    temporary_health = unit->temporary_health_bonus;
    temporary_mana = unit->temporary_mana_bonus;
    temporary_attack1 = S_AttackProfileRead(unit, 0)->temporaryDamageBonus;
    temporary_attack2 = S_AttackProfileRead(unit, 1)->temporaryDamageBonus;
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
    G_FreeAttackOne(unit);
    unit->attack_profiles[0] = NULL;
    G_FreeAttackTwo(unit);
    unit->attack_profiles[1] = NULL;
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
    S_AttackProfileWrite(unit, 0)->temporaryDamageBonus = temporary_attack1;
    S_AttackProfileWrite(unit, 1)->temporaryDamageBonus = temporary_attack2;
    G_ActivateUnitFood(unit);
    unit->animation = NULL;
    gi.LinkEntity(unit);
    if (target_building) CM_BakeStaticObstacles();
    client = G_GetPlayerClientByNumber(unit->s.player);
    if (client && client->ps.number == unit->s.player) G_InvalidateCommands(client);
    G_InvalidateUnitInfoPanel(unit);
    G_InvalidateUnitPortrait(unit);
    G_InvalidateUnitShortcutsForUnit(unit);
    S_UnitAbilityEvent(unit, A_UNIT_TYPE_CHANGED);
    return true;
}

bool unit_issueimmediateorder(edict_t *self, cstring_t order) {
//    printf("%.4s %s\n", &self->class_id, order);
    if (!self || !order) {
        return false;
    }
    if (!S_AncientCanReceiveOrder(self)) return false;
    if (G_BuildingUpgradeActive(self)) return false;
    if (M_IsDead(self)) return false;
    if (S_GoldMineWorkerIsInside(self))
        return false;
    if (!strcmp(order, "stop")) {
        G_ClearUnitOrderQueue(self);
        order_stop(self);
        S_UnitAbilityOrderAccepted(self, order);
        G_PublishIssuedImmediateOrder(self, G_OrderId(order), self->s.player, order);
        return true;
    }
    if (!strcmp(order, "holdposition")) {
        bool const accepted = S_HoldPosition(self);
        if (accepted) {
            S_UnitAbilityOrderAccepted(self, order);
            G_PublishIssuedImmediateOrder(self, G_OrderId(order), self->s.player, order);
        }
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
        }
        /* A registered order owner also owns rejection. Falling through to
         * generic spell execution would turn a rejected direction into a
         * second, opposite toggle. */
        return accepted;
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
edict_t *unit_create(uint32_t player, uint32_t unitid, vec2_t const *location, float facing) {
    /* CreateUnit returns an immediately usable unit. SP_SpawnAtLocation's
     * presentation birth is for callers that own a spawn lifecycle; applying
     * it here left a stale birth wait behind the explicit stand transition. */
    edict_t *unit = SP_SpawnAtLocationNoBirth(unitid, player, location);
    if (!unit) {
        return NULL;
    }
    /* Retail public creation uses Move's32-ring admission and initial scalar
     * commit; the former64-unit circle spiral chose different destinations. */
    S_InitUnitPosition(unit,location);
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_PLACEMENT, unit, NULL);
    if (unit->stand) {
        unit->stand(unit);
    }
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_PUBLIC_STAND, unit, NULL);
    /* Public1fc930 multiplies by cd5444 with software truncation; host radians
     * leave the first group-request heading one word apart from retail. */
    unit->s.angle = wc3_mul(facing,wc3_float(0x3c8efa35));
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_FACING, unit, NULL);
    G_ActivateUnitFood(unit);
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_FOOD, unit, NULL);
    G_BotUnitReady(unit);
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_AI, unit, NULL);
    gi.FrameCheckpoint();
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_CHECKPOINT, unit, NULL);
    return unit;
}

edict_t *unit_createorfind(uint32_t player, uint32_t unitid, vec2_t const *location, float facing) {
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
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

bool unit_additemtoslot(edict_t *edict, edict_t *item, uint32_t i) {
    return G_AddItemToSlot(edict, item, i);
}

bool unit_additem(edict_t *edict, edict_t *item) {
    return G_PickupItem(edict, item);
}

static int unit_timed_status_debug_level(void) {
    cstring_t value;

    value = gi.CvarString("wc3_timed_status_debug", "0");
    return value ? atoi(value) : 0;
}

static void unit_timed_status_log(cstring_t stage, edict_t const *ent, heroabilitystatus_t const *status) {
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

heroabilitystatus_t const *unit_findtimedbarstatus(edict_t const *ent) {
    heroabilitystatus_t const *result = NULL;
    uint32_t now;

    if (!ent) return NULL;
    now = G_Time();
    FOR_LOOP(i, G_UnitStatusSlotCount(ent)) {
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

float G_UnitArmorValue(edict_t const *ent) {
    float armor;

    if (!ent) return 0.0f;
    armor = ent->armor_value;

    /* Bdef is the stock Scroll of Protection status. Keep the authored armor
     * amount in AbilityData (AIda/DataA) rather than baking it into the unit or
     * status record; status expiry then removes the bonus automatically from
     * both combat and HUD calculations without adding save-state fields. */
    FOR_LOOP(i, G_UnitStatusSlotCount(ent)) {
        heroabilitystatus_t const *status = ent->abilstatus + i;
        if (status->level && status->code == MAKEFOURCC('B', 'd', 'e', 'f')) {
            armor += G_AbilityLevel(MAKEFOURCC('A', 'I', 'd', 'a'), status->level)->data[0].number;
        }
    }
    return armor + S_DevotionArmorBonus((edict_t *)ent) + S_SpikedArmorBonus(ent) + S_HumanArmorBonus(ent) +
        S_FaerieArmorDelta(ent) + S_FrenzyArmorDelta(ent) + S_BarkskinArmorBonus(ent) +
        S_ManaFlareArmorBonus(ent);
}



/* Generic status-lifecycle dispatch. The owner procedure resolves from the
 * status's origin ability rawcode, never from the victim's learned abilities
 * (the victim may not own the casting ability). REMOVE arrives while the slot
 * is still valid; the wipe happens after every owner has run. */
static void UnitDispatchStatus(edict_t *ent, heroabilitystatus_t *slot, uint32_t ability, abilityMsg_t msg) {
    abilityitem_t item;
    abilityCall_t call;
    if (!ent || !slot || (!slot->level && msg != A_STATUS_REMOVED) || !ability) return;
    item = S_AbilityItem(ability);
    if (!item.ability || !item.ability->proc) return;
    call = MAKE(abilityCall_t, .item = &item);
    call.status.slot = slot; call.status.ability = ability;
    S_AbilityMessage(ent, msg, &call);
}

/* Return the live slot so an ability can attach its applying rawcode/source after insertion. */
heroabilitystatus_t *unit_findstatus(edict_t *ent, uint32_t code) {
    if (ent) FOR_LOOP(i, G_UnitStatusSlotCount(ent))
        if (ent->abilstatus[i].level && ent->abilstatus[i].code == code) return ent->abilstatus + i;
    return NULL;
}

/* Notify active victim statuses before death cleanup can discard their applying state. */
void unit_statusdeath(edict_t *ent) {
    FOR_LOOP(i, G_UnitStatusSlotCount(ent)) {
        heroabilitystatus_t *slot = ent->abilstatus + i;
        if (slot->level && (!slot->timestamp || slot->timestamp > G_Time()))
            UnitDispatchStatus(ent, slot, slot->data, A_STATUS_DEATH);
    }
}

/* Derived locks come only from statuses that actually remain; owners
 * reconcile their own flight/height state through A_STATUS_REFRESH. */
void unit_refreshstatusflags(edict_t *ent) {
    bool stunned = false;
    FOR_LOOP(i, G_UnitStatusSlotCount(ent)) {
        heroabilitystatus_t *status = ent->abilstatus + i;
        if (!status->level) continue;
        if(status->code==MAKEFOURCC('A','O','a','e'))S_MarkAuraSource(ent);
        if (unit_status_stuns(status->code)) stunned = true;
        UnitDispatchStatus(ent, status, status->data, A_STATUS_REFRESH);
    }
    ent->stunned = stunned;
}

/* Dispel/Purge share this so owners run their inverse before the slot is wiped. */
void unit_expirestatus(edict_t *ent, heroabilitystatus_t *status) {
    uint32_t origin;
    if (!ent || !status || !status->level) return;
    S_HumanStatusExpired(ent, status->code, status->level);
    origin = status->data;
    UnitDispatchStatus(ent, status, origin, A_STATUS_REMOVE);
    memset(status, 0, sizeof(*status));
    UnitDispatchStatus(ent, status, origin, A_STATUS_REMOVED);
}

/* The native filter asks the applying owner, not the victim's ability list.
 * Magic/physical selectors are independent requirements, as in48eb10.
 * Restart after an inverse: an owner may change other attached statuses. */
uint32_t unit_removebuffs(edict_t *ent, bool positive, bool negative,
                         bool magic, bool physical, bool timed_life,
                         bool aura, bool auto_dispel) {
    uint32_t removed = 0;
    if (!ent || (!positive && !negative)) return 0;
    for (unsigned i = 0; i < G_UnitStatusSlotCount(ent);) {
        heroabilitystatus_t *status = ent->abilstatus + i++;
        abilityitem_t item;
        abilityCall_t call;
        uint32_t policy;
        if (!status->level) continue;
        item = S_AbilityItem(status->data);
        policy = 0;
        if (item.ability && (item.ability->flags & AB_STATUS_POLICY)) {
            call = MAKE(abilityCall_t, .item = &item);
            call.status.slot = status; call.status.ability = status->data;
            policy = (uint32_t)S_AbilityMessage(ent, A_STATUS_POLICY, &call);
        }
        if (!(policy & UNIT_BUFF_KNOWN)) {
            fprintf(stderr, "UnitRemoveBuffs: unclassified status %.4s, applying ability %.4s on %s\n",
                    (char const *)&status->code, (char const *)&status->data, GetClassName(ent->class_id));
            continue;
        }
        if (!((positive && (policy & UNIT_BUFF_POSITIVE)) ||
              (negative && (policy & UNIT_BUFF_NEGATIVE)))) continue;
        if (!aura && (policy & UNIT_BUFF_AURA)) continue;
        if (!timed_life && (policy & UNIT_BUFF_TIMED_LIFE)) continue;
        if (physical && !(policy & UNIT_BUFF_PHYSICAL)) continue;
        if (magic && !(policy & UNIT_BUFF_MAGIC)) continue;
        if (auto_dispel && !(policy & UNIT_BUFF_AUTO_DISPEL)) continue;
        unit_expirestatus(ent, status);
        removed++; i = 0;
    }
    if (removed) {
        unit_refreshstatusflags(ent);
        G_InvalidateUnitInfoPanel(ent);
    }
    return removed;
}

void unit_updatestatuses(edict_t *ent) {
    uint32_t now = G_Time();
    bool changed = false;
    bool kill = false;
    bool militia_expired = false;

    FOR_LOOP(i, G_UnitStatusSlotCount(ent)) {
        heroabilitystatus_t *status = ent->abilstatus + i;
        if (!status->level || !status->timestamp) {
            continue;
        }
        if (now >= status->timestamp) {
            if (unit_status_timedlife(status->code)) {
                kill = true;
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

heroabilitystatus_t *G_EnsureUnitStatusSlots(edict_t *ent) {
    assert(ent);
    if (!ent->abilstatus) ent->abilstatus = G_AllocUnitStatus()->slots;
    return ent->abilstatus;
}

void unit_addtimedstatus(edict_t *ent, cstring_t skill, uint32_t level, float duration) {
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
    G_EnsureUnitStatusSlots(ent);

    FOR_LOOP(i, G_UnitStatusSlotCount(ent)) {
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
                UnitDispatchStatus(ent, status, status->data, A_STATUS_REPLACE);
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

void unit_addstatus(edict_t *ent, cstring_t skill, uint32_t level) {
    unit_addtimedstatus(ent, skill, level, 0);
}

unitStatusQuery_t *g_unit_status_query;
#ifdef BZ_TESTS
static uint32_t unit_status_queries;
static uint32_t unit_status_slot_visits;
uint32_t G_TestUnitStatusSlotVisits(bool reset) {
    uint32_t count = unit_status_slot_visits;
    if (reset) unit_status_slot_visits = 0;
    return count;
}
uint32_t G_TestUnitStatusQueries(bool reset) {
    uint32_t count = unit_status_queries;
    if (reset) unit_status_queries = 0;
    return count;
}
#endif

/* A pure mechanical calculation can resolve many status families from the
 * same unit. Discover occupied status/rank slots and decoded authored ownership
 * once, retaining slot order and live expiration checks. The scope never
 * crosses a gameplay callback or an ownership mutation. */
void G_BeginUnitStatusQuery(edict_t const *unit,unitStatusQuery_t *query) {
    *query=(unitStatusQuery_t){.unit=unit,.previous=g_unit_status_query};
    FOR_LOOP(i, G_UnitStatusSlotCount(unit))if(unit->abilstatus[i].level)query->slots|=1u<<i;
    FOR_LOOP(i,MAX_HERO_ABILITIES)if(unit->heroabilities[i].level)query->hero_slots|=1u<<i;
    query->abilities=G_UnitAbilityCodeSet(unit->data.UnitAbilities,&query->ability_count,&query->ability_membership);
    g_unit_status_query=query;
}

void G_EndUnitStatusQuery(unitStatusQuery_t *query) {
    assert(g_unit_status_query==query);
    g_unit_status_query=query->previous;
}

uint32_t G_UnitStatusLevel(edict_t const *ent, uint32_t code) {
#ifdef BZ_TESTS
    unit_status_queries++;
#endif
    if (!ent || !code || !ent->abilstatus) return 0;
    if(g_unit_status_query && g_unit_status_query->unit==ent) {
        for(uint32_t slots=g_unit_status_query->slots;slots;slots&=slots-1) {
            heroabilitystatus_t const *status=ent->abilstatus+__builtin_ctz(slots);
            if(status->level && status->code==code && (!status->timestamp || status->timestamp>G_Time()))
                return status->level;
        }
        return 0;
    }
    FOR_LOOP(i, G_UnitStatusSlotCount(ent)) {
#ifdef BZ_TESTS
        unit_status_slot_visits++;
#endif
        if (ent->abilstatus[i].level && ent->abilstatus[i].code == code &&
            (!ent->abilstatus[i].timestamp || ent->abilstatus[i].timestamp > G_Time()))
            return ent->abilstatus[i].level;
    }
    return 0;
}

static heroability_t *G_FindRuntimeAbility(edict_t *ent, uint32_t abilcode) {
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

/* Empty rank arrays are the common mechanical query. Read their levels
 * together before entering alias resolution; no cached ownership is involved. */
static inline bool G_HeroHasRanks(edict_t const *ent) {
    uint32_t levels = 0;
    FOR_LOOP(i, MAX_HERO_ABILITIES) levels |= ent->heroabilities[i].level;
    return levels != 0;
}

static uint32_t G_HeroSkillLevel(edict_t const *ent, uint32_t abilcode) {
    uint32_t base_code = 0;
    bool resolved = false;
    if (!ent || !abilcode) return 0;
    if (g_unit_status_query && g_unit_status_query->unit == ent) {
        if (!g_unit_status_query->hero_slots) return 0;
        for (uint32_t slots = g_unit_status_query->hero_slots; slots; slots &= slots - 1) {
            heroability_t const *ha = ent->heroabilities + __builtin_ctz(slots);
            if (!ha->level) continue;
            if (!resolved) { base_code = G_AbilityCode(abilcode); resolved = true; }
            if (G_AbilityCode(ha->code) == base_code) return ha->level;
        }
        return 0;
    }
    if (!G_HeroHasRanks(ent)) return 0;
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t const *ha = ent->heroabilities + i;
        if (!ha->level) continue;
        if (!resolved) { base_code = G_AbilityCode(abilcode); resolved = true; }
        if (G_AbilityCode(ha->code) == base_code) {
            return ha->level;
        }
    }
    return 0;
}

void G_HeroInitializeProgression(edict_t *ent) {
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

bool G_HeroModifySkillPoints(edict_t *ent, int32_t delta) {
    uint32_t old_points;
    uint32_t new_points;
    gameClient_t *owner;

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

uint32_t G_UnitAbilityLevel(edict_t const *ent, uint32_t abilcode) {
    uint32_t const timed_level=S_TimedLifeLevel(ent,abilcode);
    if(timed_level)return timed_level;
    uint32_t const hero_level = g_unit_status_query && g_unit_status_query->unit == ent &&
        !g_unit_status_query->hero_slots ? 0 : G_HeroSkillLevel(ent, abilcode);
    if (hero_level) {
        return hero_level;
    }
    if (!ent || !abilcode) return 0;
    uint32_t const status_level = G_QueryUnitStatusLevel(ent, abilcode);
    if (status_level) return status_level;
    return G_ActorHasAbilityCode(ent, abilcode) ? 1 : 0;
}

/* SetUnitAbilityLevel / IncUnitAbilityLevel: rank lives in heroabilities[].
 * Returns the new level, or 0 when the unit does not own the ability. */
uint32_t G_UnitSetAbilityLevel(edict_t *ent, uint32_t abilcode, int32_t level) {
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
        S_MarkAuraSource(ent);
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
            S_MarkAuraSource(ent);
            return ha->level;
        }
    }
    return 0;
}

void unit_learnability(edict_t *ent, uint32_t abilcode) {
    heroability_t *existing = G_FindRuntimeAbility(ent, abilcode);
    if (existing) {
        existing->level++;
        S_MarkAuraSource(ent);
        return;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t *ha = ent->heroabilities + i;
        if (ha->level == 0) {
            ha->level = 1;
            ha->code = abilcode;
            S_MarkAuraSource(ent);
            return;
        }
    }
}

static uint32_t G_HeroAbilityLevelSkip(void) {
    cstring_t const value = Stb_IniCacheFind(&game.config.misc, "Misc", "HeroAbilityLevelSkip");
    uint32_t const skip = value ? (uint32_t)atoi(value) : 0;
    return skip > 0 ? skip : 2;
}

bool G_HeroHasCandidateSkill(edict_t const *ent, uint32_t abilcode) {
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

uint32_t G_HeroSkillRequiredLevel(edict_t *ent, uint32_t abilcode) {
    AbilityData_t const *ability = G_AbilityData(abilcode);
    uint32_t const current = G_HeroSkillLevel(ent, abilcode);
    uint32_t const base = ability->reqLevel > 0 ? (uint32_t)ability->reqLevel : 1;
    uint32_t const skip = ability->levelSkip > 0 ? (uint32_t)ability->levelSkip : G_HeroAbilityLevelSkip();
    return base + current * skip;
}

heroSkillState_t G_HeroSkillState(edict_t *ent, uint32_t abilcode, uint32_t *next_level, uint32_t *required_level) {
    AbilityData_t const *ability;
    uint32_t current;
    uint32_t required;

    if (next_level) *next_level = 0;
    if (required_level) *required_level = 0;
    if (!G_HeroHasCandidateSkill(ent, abilcode)) {
        return HERO_SKILL_ABSENT;
    }
    if (!G_IsUnitAbilityAvailable(ent, abilcode)) {
        return HERO_SKILL_DISABLED;
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

bool G_HeroLearnSkill(edict_t *ent, uint32_t abilcode) {
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
void G_RecomputeHeroStats(edict_t *ent) {
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
            S_AttackProfileWrite(ent, 0)->damageBase = (uint32_t)MAX(0,
                (int32_t)ent->data.UnitWeapons->attack1.damageBase + primaryDamage
                + (int32_t)S_AttackProfileRead(ent, 0)->permanentDamageBonus);
            S_AttackProfileWrite(ent, 1)->damageBase = (uint32_t)MAX(0,
                (int32_t)ent->data.UnitWeapons->attack2.damageBase + primaryDamage
                + (int32_t)S_AttackProfileRead(ent, 1)->permanentDamageBonus);
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
void G_HeroApplyLevel(edict_t *ent, uint32_t level) {
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
void G_HeroSetXP(edict_t *ent, uint32_t xp) {
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
            G_BotHeroLevelUp(ent);
        }
    }
}

/* --- XP-on-kill (data-driven from Units\MiscGame.txt) ------------------------
 * Constants read live from config.misc so map overrides stay 1:1.  Stock TFT
 * uses HeroExpRange=1200, formula-driven normal/Hero kill XP, HeroFactorXP as a
 * receiving-Hero-level creep reduction table, BuildingKillsGiveExp=0,
 * SummonedKillFactor=0.50, and MaxLevelHeroesDrainExp=1. */

static float const WC3_XP_RANGE_DEFAULT =
    1200.0f; /* BZ_HARDCODED_DATA_FALLBACK: stock HeroExpRange. */
static float const WC3_XP_BUILDING_KILLS_DEFAULT =
    0.0f; /* BZ_HARDCODED_DATA_FALLBACK: stock BuildingKillsGiveExp. */
static float const WC3_XP_SUMMON_FACTOR_DEFAULT =
    0.50f; /* BZ_HARDCODED_DATA_FALLBACK: stock SummonedKillFactor. */
static float const WC3_XP_NORMAL_TABLE_DEFAULT[] = {
    25.0f
}; /* BZ_HARDCODED_DATA_FALLBACK: stock GrantNormalXP[0]. */
static float const WC3_XP_NORMAL_FORMULA_A_DEFAULT =
    1.0f; /* BZ_HARDCODED_DATA_FALLBACK: stock GrantNormalXPFormulaA. */
static float const WC3_XP_NORMAL_FORMULA_B_DEFAULT =
    5.0f; /* BZ_HARDCODED_DATA_FALLBACK: stock GrantNormalXPFormulaB. */
static float const WC3_XP_NORMAL_FORMULA_C_DEFAULT =
    5.0f; /* BZ_HARDCODED_DATA_FALLBACK: stock GrantNormalXPFormulaC. */
static float const WC3_XP_HERO_TABLE_DEFAULT[] = {
    100.0f, 120.0f, 160.0f, 220.0f, 300.0f
}; /* BZ_HARDCODED_DATA_FALLBACK: stock GrantHeroXP table. */
static float const WC3_XP_HERO_FORMULA_A_DEFAULT =
    1.0f; /* BZ_HARDCODED_DATA_FALLBACK: stock GrantHeroXPFormulaA. */
static float const WC3_XP_HERO_FORMULA_B_DEFAULT =
    0.0f; /* BZ_HARDCODED_DATA_FALLBACK: stock GrantHeroXPFormulaB. */
static float const WC3_XP_HERO_FORMULA_C_DEFAULT =
    100.0f; /* BZ_HARDCODED_DATA_FALLBACK: stock GrantHeroXPFormulaC. */
static float const WC3_XP_CREEP_FACTOR_DEFAULT[] = {
    80.0f, 70.0f, 60.0f, 50.0f, 0.0f
}; /* BZ_HARDCODED_DATA_FALLBACK: stock HeroFactorXP table. */

static bool G_KillXPDebugEnabled(void) {
    return gi.CvarString && atoi(gi.CvarString("wc3_kill_xp_debug", "0")) != 0;
}

static float G_MiscNum(cstring_t key, float fallback) {
    cstring_t const v = Stb_IniCacheFind(&game.config.misc, "Misc", key);
    return (v && *v) ? (float)atof(v) : fallback;
}

static float G_KillXPMiscNum(cstring_t key, float fallback) {
    cstring_t const value = Stb_IniCacheFind(&game.config.misc, "Misc", key);
    char *end = NULL;
    double parsed;

    if (!value || !*value) return fallback;
    parsed = strtod(value, &end);
    while (end && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end++;
    if (!end || end == value || *end || !isfinite(parsed) ||
        parsed > (double)FLT_MAX || parsed < -(double)FLT_MAX) {
        fprintf(stderr, "WC3 kill XP: invalid Misc/%s value '%s'; using fallback %.3f\n",
                key, value, fallback);
        return fallback;
    }
    return (float)parsed;
}

/* n-th (0-based) comma-separated entry of a Misc list, clamped to the last. */
static float G_MiscListNum(cstring_t key, uint32_t n, float fallback) {
    cstring_t v = Stb_IniCacheFind(&game.config.misc, "Misc", key);
    if (!v || !*v) {
        return fallback;
    }
    float val = fallback;
    for (uint32_t i = 0;; i++) {
        char *end = NULL;
        double parsed;
        while (*v == ' ' || *v == '\t') v++;
        parsed = strtod(v, &end);
        if (!end || end == v || !isfinite(parsed) ||
            parsed > (double)FLT_MAX || parsed < -(double)FLT_MAX) {
            fprintf(stderr, "WC3 kill XP: malformed Misc/%s list near '%s'; using %.3f\n",
                    key, v, fallback);
            return fallback;
        }
        if (parsed < 0.0) {
            fprintf(stderr, "WC3 kill XP: negative Misc/%s entry %.3f; using zero\n",
                    key, parsed);
            parsed = 0.0;
        }
        val = (float)parsed;
        if (i >= n) return val;

        v = end;
        while (*v == ' ' || *v == '\t') v++;
        if (*v != ',') {
            if (*v) {
                fprintf(stderr, "WC3 kill XP: malformed Misc/%s list near '%s'; using %.3f\n",
                        key, v, fallback);
            }
            return val;
        }
        v++;
    }
}

/* Warcraft extends the authored kill-XP tables with:
 *   f(level) = A * f(level - 1) + B * level + C
 * The table itself is 1-based by victim level. */
static uint32_t G_KillXPForLevel(cstring_t tableKey, cstring_t formulaAKey,
                                 cstring_t formulaBKey, cstring_t formulaCKey,
                                 uint32_t level, float const *tableFallback,
                                 uint32_t tableFallbackCount,
                                 float formulaAFallback, float formulaBFallback,
                                 float formulaCFallback) {
    cstring_t value = Stb_IniCacheFind(&game.config.misc, "Misc", tableKey);
    uint32_t count = 0;
    double current;
    double requested = 0.0;
    bool hasRequested = false;

    if (level < 1) level = 1;
    if (!value || !*value) {
        value = NULL;
    }
    while (value && *value) {
        char *end = NULL;
        double parsed;
        while (*value == ' ' || *value == '\t') value++;
        parsed = strtod(value, &end);
        if (!end || end == value || !isfinite(parsed)) {
            fprintf(stderr, "WC3 kill XP: malformed Misc/%s XP table near '%s'\n",
                    tableKey, value);
            break;
        }
        if (parsed < 0.0) {
            fprintf(stderr, "WC3 kill XP: negative Misc/%s XP table entry %.3f; using zero\n",
                    tableKey, parsed);
            parsed = 0.0;
        }
        if (parsed > (double)UINT32_MAX) {
            fprintf(stderr, "WC3 kill XP: Misc/%s XP table entry %.3f exceeds the XP limit; clamping\n",
                    tableKey, parsed);
            parsed = (double)UINT32_MAX;
        }
        current = parsed;
        count++;
        if (count == level) {
            requested = current;
            hasRequested = true;
        }
        value = end;
        while (*value == ' ' || *value == '\t') value++;
        if (*value == ',') {
            value++;
            cstring_t next = value;
            while (*next == ' ' || *next == '\t') next++;
            if (!*next) {
                fprintf(stderr, "WC3 kill XP: malformed Misc/%s XP table after trailing comma\n",
                        tableKey);
                break;
            }
        } else if (*value) {
            fprintf(stderr, "WC3 kill XP: malformed Misc/%s XP table near '%s'\n",
                    tableKey, value);
            break;
        }
    }
    if (hasRequested) return (uint32_t)requested;
    if (!count) {
        count = tableFallbackCount;
        current = tableFallback[count - 1];
        if (level <= count) return (uint32_t)tableFallback[level - 1];
    }

    double const a = G_KillXPMiscNum(formulaAKey, formulaAFallback);
    double const b = G_KillXPMiscNum(formulaBKey, formulaBFallback);
    double const c = G_KillXPMiscNum(formulaCKey, formulaCFallback);
    for (uint32_t victimLevel = count + 1; victimLevel <= level; victimLevel++) {
        current = a * current + b * (double)victimLevel + c;
        if (current < 0.0) current = 0.0;
        if (current > (double)UINT32_MAX) current = (double)UINT32_MAX;
    }
    return (uint32_t)current;
}

bool G_UnitIsHero(edict_t const *ent) {
    return ent && ent->data.UnitBalance &&
        (ent->data.UnitBalance->strength > 0 || ent->data.UnitBalance->agility > 0 ||
         ent->data.UnitBalance->intelligence > 0);
}

static cstring_t G_HeroKillXPRejectReason(edict_t const *hero, edict_t const *victim,
                                         edict_t const *killer, float range) {
    if (!hero->inuse) return "unused";
    if (!(hero->svflags & SVF_MONSTER)) return "not-monster";
    if (!hero->data.UnitBalance) return "no-unit-balance";
    if (hero->health.value <= 0) return "dead";
    if (hero->hero.suspend_xp) return "xp-suspended";
    if (hero->aiflags & AI_ILLUSION) return "illusion";
    if (!G_UnitIsHero(hero)) return "not-hero";
    if (G_KillXPMiscNum("MaxLevelHeroesDrainExp", 1.0f) == 0.0f &&
        hero->hero.level >= G_MaxHeroLevel()) return "max-level";
    if (range >= 0.0f && Vector2_distance(&hero->s.origin2, &victim->s.origin2) > range)
        return "out-of-range";
    if (hero->s.player != killer->s.player &&
        !(hero->s.player < MAX_PLAYERS && killer->s.player < MAX_PLAYERS &&
          (level.alliances[killer->s.player][hero->s.player] & (1 << ALLIANCE_SHARED_XP))))
        return "not-owned-or-shared-xp";
    return NULL;
}

static bool G_KillXPDebugHeroCandidate(edict_t const *ent) {
    return ent->inuse && ent->data.UnitBalance && G_UnitIsHero(ent);
}

static bool G_HeroReceivesKillXP(edict_t const *hero, edict_t const *victim, edict_t const *killer, float range) {
    return G_HeroKillXPRejectReason(hero, victim, killer, range) == NULL;
}

static bool G_StructureGrantsKillXP(edict_t const *victim) {
    if (!G_UnitIsStructure(victim)) return true;
    return victim->data.UnitWeapons && victim->data.UnitWeapons->attacksEnabled != 0;
}

/* Award experience for killing `victim` to nearby heroes owned by the killer
 * or covered by the killer player's directional SHARED_XP alliance. Warcraft
 * divides the available victim XP across all eligible nearby heroes before
 * applying each receiving Hero's creep/player modifiers. */
void G_GrantKillXP(edict_t *victim, edict_t *killer) {
    bool const debug = G_KillXPDebugEnabled();
    if (!victim || !killer) return;
    if (!victim->data.UnitBalance || (victim->aiflags & AI_ILLUSION)) {
        if (debug) fprintf(stderr, "WC3 kill XP: skip victim=%u killer=%u reason=%s\n",
            (unsigned)(victim - globals.edicts), (unsigned)(killer - globals.edicts),
            !victim->data.UnitBalance ? "no-unit-balance" : "illusion");
        return;
    }
    uint32_t receivers = 0;
    if (G_PlayerTreatsPlayerAsAlly(killer->s.player, victim->s.player)) {
        if (debug) fprintf(stderr, "WC3 kill XP: skip victim=%u owner=%u killer=%u owner=%u reason=allied-victim\n",
            (unsigned)(victim - globals.edicts), victim->s.player,
            (unsigned)(killer - globals.edicts), killer->s.player);
        return; /* forced attacks on passive allies do not award Hero XP */
    }
    /* Retail grants no Hero XP when the killing blow comes from a building,
     * unless the map explicitly enables BuildingKillsGiveExp. */
    if (G_UnitIsStructure(killer) && G_KillXPMiscNum("BuildingKillsGiveExp", WC3_XP_BUILDING_KILLS_DEFAULT) == 0.0f) {
        if (debug) fprintf(stderr, "WC3 kill XP: skip victim=%u killer=%u reason=building-killer\n",
            (unsigned)(victim - globals.edicts), (unsigned)(killer - globals.edicts));
        return;
    }
    /* Enemy buildings award XP only when they have an authored attack (towers,
     * attacking Ancients, etc.); passive structures such as Farms do not. */
    if (!G_StructureGrantsKillXP(victim)) {
        if (debug) fprintf(stderr, "WC3 kill XP: skip victim=%u killer=%u reason=passive-structure\n",
            (unsigned)(victim - globals.edicts), (unsigned)(killer - globals.edicts));
        return;
    }

    bool const victimHero = G_UnitIsHero(victim);
    uint32_t const victimLevel = victimHero ? (uint32_t)MAX(1, (int32_t)victim->hero.level)
                                         : (uint32_t)MAX(1, victim->data.UnitBalance->level);
    uint32_t baseXP;
    if (victimHero) {
        baseXP = G_KillXPForLevel("GrantHeroXP", "GrantHeroXPFormulaA",
                                  "GrantHeroXPFormulaB", "GrantHeroXPFormulaC",
                                  victimLevel, WC3_XP_HERO_TABLE_DEFAULT,
                                  (uint32_t)(sizeof(WC3_XP_HERO_TABLE_DEFAULT) /
                                             sizeof(WC3_XP_HERO_TABLE_DEFAULT[0])),
                                  WC3_XP_HERO_FORMULA_A_DEFAULT, WC3_XP_HERO_FORMULA_B_DEFAULT,
                                  WC3_XP_HERO_FORMULA_C_DEFAULT);
    } else {
        baseXP = G_KillXPForLevel("GrantNormalXP", "GrantNormalXPFormulaA",
                                  "GrantNormalXPFormulaB", "GrantNormalXPFormulaC",
                                  victimLevel, WC3_XP_NORMAL_TABLE_DEFAULT,
                                  (uint32_t)(sizeof(WC3_XP_NORMAL_TABLE_DEFAULT) /
                                             sizeof(WC3_XP_NORMAL_TABLE_DEFAULT[0])),
                                  WC3_XP_NORMAL_FORMULA_A_DEFAULT, WC3_XP_NORMAL_FORMULA_B_DEFAULT,
                                  WC3_XP_NORMAL_FORMULA_C_DEFAULT);
    }
    if (victim->summon_ability) {
        double summoned = (double)baseXP *
            (double)G_KillXPMiscNum("SummonedKillFactor", WC3_XP_SUMMON_FACTOR_DEFAULT);
        if (summoned < 0.0) summoned = 0.0;
        if (summoned > (double)UINT32_MAX) summoned = (double)UINT32_MAX;
        baseXP = (uint32_t)(summoned + 0.5);
    }

    if (debug) fprintf(stderr,
        "WC3 kill XP: death victim=%u code=%08x owner=%u level=%u hero=%u summoned=%u killer=%u code=%08x owner=%u pos=(%.1f,%.1f) base=%u range=%.1f global-config=%.1f hero-table='%s' normal-table='%s'\n",
        (unsigned)(victim - globals.edicts), victim->data.UnitBalance->id, victim->s.player,
        victimLevel, victimHero, victim->summon_ability != 0,
        (unsigned)(killer - globals.edicts), killer->data.UnitBalance ? killer->data.UnitBalance->id : 0,
        killer->s.player, victim->s.origin2.x, victim->s.origin2.y, baseXP,
        G_KillXPMiscNum("HeroExpRange", WC3_XP_RANGE_DEFAULT),
        G_KillXPMiscNum("GlobalExperience", 1.0f),
        Stb_IniCacheFind(&game.config.misc, "Misc", "GrantHeroXP") ?: "<default>",
        Stb_IniCacheFind(&game.config.misc, "Misc", "GrantNormalXP") ?: "<default>");

    float const range = G_KillXPMiscNum("HeroExpRange", WC3_XP_RANGE_DEFAULT);

    FOR_LOOP(i, globals.num_edicts) {
        edict_t const *hero = &globals.edicts[i];
        cstring_t const reason = G_HeroKillXPRejectReason(hero, victim, killer, range);
        if (reason) {
            if (debug && G_KillXPDebugHeroCandidate(hero))
                fprintf(stderr, "WC3 kill XP: range candidate hero=%u code=%08x owner=%u level=%u rejected=%s distance=%.1f\n",
                    i, hero->data.UnitBalance ? hero->data.UnitBalance->id : 0,
                    hero->s.player, hero->hero.level, reason,
                    Vector2_distance(&hero->s.origin2, &victim->s.origin2));
        } else {
            receivers++;
        }
    }

    /* Warcraft's GlobalExperience policy is a fallback: use global eligible
     * heroes only when no receiver is inside HeroExpRange. */
    bool const global = !receivers &&
        G_KillXPMiscNum("GlobalExperience",
            1.0f /* BZ_HARDCODED_DATA_FALLBACK: stock WC3 default. */) != 0.0f;
    if (global) {
        FOR_LOOP(i, globals.num_edicts) {
            edict_t const *hero = &globals.edicts[i];
            cstring_t const reason = G_HeroKillXPRejectReason(hero, victim, killer, -1.0f);
            if (reason) {
                if (debug && G_KillXPDebugHeroCandidate(hero))
                    fprintf(stderr, "WC3 kill XP: global candidate hero=%u code=%08x owner=%u level=%u rejected=%s distance=%.1f\n",
                        i, hero->data.UnitBalance ? hero->data.UnitBalance->id : 0,
                        hero->s.player, hero->hero.level, reason,
                        Vector2_distance(&hero->s.origin2, &victim->s.origin2));
            } else {
                receivers++;
            }
        }
    }
    if (debug) fprintf(stderr, "WC3 kill XP: receivers victim=%u count=%u mode=%s base=%u\n",
        (unsigned)(victim - globals.edicts), receivers,
        global ? "global-fallback" : "range", baseXP);
    if (!receivers || !baseXP) {
        if (debug) fprintf(stderr, "WC3 kill XP: no-award victim=%u reason=%s\n",
            (unsigned)(victim - globals.edicts), !receivers ? "no-eligible-heroes" : "zero-base-xp");
        return;
    }

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *h = &globals.edicts[i];
        if (!G_HeroReceivesKillXP(h, victim, killer, global ? -1.0f : range)) {
            continue;
        }

        double factor = 1.0;
        double creepFactor = 1.0;
        double handicap = 1.0;
        /* HeroFactorXP is the neutral-creep reduction table indexed by the
         * receiving Hero's level. Enemy player-controlled units and Heroes do
         * not use this reduction, so level-5+ Heroes can keep progressing. */
        if (victim->s.player == PLAYER_NEUTRAL_AGGRESSIVE) {
            uint32_t const index = h->hero.level ? h->hero.level - 1 : 0;
            uint32_t const fallbackIndex = MIN(index,
                (uint32_t)(sizeof(WC3_XP_CREEP_FACTOR_DEFAULT) /
                           sizeof(WC3_XP_CREEP_FACTOR_DEFAULT[0]) - 1));
            creepFactor = (double)G_MiscListNum("HeroFactorXP", index,
                WC3_XP_CREEP_FACTOR_DEFAULT[fallbackIndex]) / 100.0;
            factor *= creepFactor;
        }
        if (h->s.player < game.max_clients) {
            /* The JASS native stores an XP multiplier (1.0 = 100%, 3.0 =
             * 300%); Blizzard.j's *BJ wrapper converts authored percentages
             * to this scale before calling SetPlayerHandicapXP. */
            handicap = (double)MAX(0.0f, game.clients[h->s.player].jass.handicap_xp);
            factor *= handicap;
        }

        double awardValue = ((double)baseXP / (double)receivers) * factor + 0.5;
        if (!isfinite(awardValue) || awardValue > (double)UINT32_MAX) {
            fprintf(stderr, "WC3 kill XP: computed award for player %u exceeds the XP limit; clamping\n",
                    h->s.player);
            awardValue = (double)UINT32_MAX;
        }
        if (awardValue < 0.0) awardValue = 0.0;
        uint32_t const award = (uint32_t)awardValue;
        if (debug) fprintf(stderr,
            "WC3 kill XP: award hero=%u code=%08x owner=%u level=%u xp-before=%u victim=%u receivers=%u base=%u creep-factor=%.3f handicap-multiplier=%.3f total-factor=%.3f award=%u\n",
            i, h->data.UnitBalance->id, h->s.player, h->hero.level, h->hero.xp,
            (unsigned)(victim - globals.edicts), receivers, baseXP,
            creepFactor, handicap, factor,
            award);
        if (award > 0 && UINT32_MAX - h->hero.xp >= award) {
            G_HeroSetXP(h, h->hero.xp + award);
        } else if (award > 0) {
            G_HeroSetXP(h, UINT32_MAX);
        }
        if (debug) fprintf(stderr, "WC3 kill XP: result hero=%u xp-after=%u level-after=%u\n",
            i, h->hero.xp, h->hero.level);
    }
}

/* Scripted hero revival (ReviveHero native): bring a dead hero back to life at
 * (x,y) with HP/mana set from the MiscGame revive factors (defaults: full life,
 * no mana).  Dead heroes persist (unit_decay_think) so the edict is still valid. */
bool G_ReviveHero(edict_t *ent, float x, float y) {
    float mana;

    if (!ent || !ent->inuse || !G_UnitIsHero(ent) || !M_IsDead(ent) ||
        (ent->aiflags & AI_SOUL_TRAPPED)) return false;
    if ((ent->revival && ent->revival->reviving)) G_CancelHeroRevive(ent->revival->producer, ent);
    float const lifeFactor = G_MiscNum("HeroReviveLifeFactor", 1.0f);
    float const manaFactor = G_MiscNum("HeroReviveManaFactor", 0.0f);
    float const manaStart = G_MiscNum("HeroReviveManaStart", 0.0f);
    ent->svflags &= ~SVF_DEADMONSTER;
    ent->s.flags &= ~EF_NOT_SELECTABLE;
    ent->aiflags &= ~AI_HOLD_FRAME;
    ent->combatentity = NULL;
    if (!ent->revival) ent->revival = G_AllocRevival();
    assert(ent->revival);
    ent->revival->awaiting = false;
    ent->revival->reviving = false;
    ent->revival->producer = NULL;
    ent->revival->queue_next = NULL;
    ent->revival->player = 0;
    ent->revival->gold = ent->revival->lumber = 0;
    ent->revival->progress = 0.0f;
    G_SetEntityHidden(ent,false);
    G_SetHealth(ent, MIN(ent->health.max_value, MAX(1.0f, ent->health.max_value * lifeFactor)));
    mana = ent->mana.max_value * manaFactor;
    if (ent->data.UnitBalance) mana += ent->data.UnitBalance->initialMana * manaStart;
    ent->mana.value = MAX(0.0f, MIN(ent->mana.max_value, mana));
    ent->s.origin2.x = x;
    ent->s.origin2.y = y;
    ent->s.origin.x = x;
    ent->s.origin.y = y;
    ent->s.origin.z = CM_GetHeightAtPoint(x, y);
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    G_ActivateUnitFood(ent);
    unit_stand(ent); /* back to a living idle state */
    gi.LinkEntity(ent);
    return true;
}

void SP_monster_unit(edict_t *self) {
    self->movetype = unit_movedistance(self) > 0 ? MOVETYPE_STEP : MOVETYPE_NONE;
    self->die = unit_die;
    self->stand = unit_stand;
    self->birth = unit_birth;
    
    unit_setmove(self, &unit_move_stand);
    S_GoldMineInitUnit(self);
    monster_start(self);
}
